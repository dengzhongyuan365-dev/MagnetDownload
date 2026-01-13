// MagnetDownload - 元数据获取器实现

#include "magnet/protocols/metadata_fetcher.h"
#include "magnet/protocols/bt_message.h"
#include "magnet/utils/logger.h"

namespace magnet::protocols {

// 日志宏
#define LOG_DEBUG(msg) magnet::utils::Logger::instance().debug(std::string("[MetadataFetcher] ") + msg)
#define LOG_INFO(msg) magnet::utils::Logger::instance().info(std::string("[MetadataFetcher] ") + msg)
#define LOG_WARNING(msg) magnet::utils::Logger::instance().warn(std::string("[MetadataFetcher] ") + msg)
#define LOG_ERROR(msg) magnet::utils::Logger::instance().error(std::string("[MetadataFetcher] ") + msg)

// ============================================================================
// 构造与析构
// ============================================================================

MetadataFetcher::MetadataFetcher(asio::io_context& io_context,
                                 const InfoHash& info_hash,
                                 MetadataFetcherConfig config)
    : io_context_(io_context)
    , info_hash_(info_hash)
    , config_(config)
    , timeout_timer_(io_context)
{
    LOG_DEBUG("MetadataFetcher created for " + info_hash.toHex().substr(0, 16) + "...");
}

MetadataFetcher::~MetadataFetcher() {
    stop();
}

// ============================================================================
// 生命周期
// ============================================================================

void MetadataFetcher::start(MetadataCallback callback) {
    if (running_.exchange(true)) {
        LOG_WARNING("MetadataFetcher already running");
        return;
    }
    
    LOG_INFO("Starting metadata fetch for " + info_hash_.toHex().substr(0, 16) + "...");
    
    callback_ = std::move(callback);
    complete_.store(false);
    
    // 启动总超时定时器
    startTimeoutTimer();
}

void MetadataFetcher::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    
    LOG_INFO("Stopping metadata fetch");
    
    timeout_timer_.cancel();
    
    std::lock_guard<std::mutex> lock(mutex_);
    peers_.clear();
}

// ============================================================================
// Peer 管理
// ============================================================================

void MetadataFetcher::addPeer(std::shared_ptr<PeerConnection> peer) {
    if (!running_.load()) {
        LOG_WARNING("addPeer called but MetadataFetcher not running!");
        return;
    }
    if (complete_.load()) {
        LOG_DEBUG("addPeer called but already complete");
        return;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (peers_.size() >= config_.max_peers) {
        LOG_DEBUG("Max peers reached, ignoring new peer");
        return;
    }
    
    auto* raw_peer = peer.get();
    if (peers_.count(raw_peer) > 0) {
        return;
    }
    
    PeerState state;
    state.connection = peer;
    state.handshake_sent = false;
    state.supports_metadata = false;
    
    peers_[raw_peer] = std::move(state);
    
    LOG_DEBUG("Added peer, total: " + std::to_string(peers_.size()));
    
    // 发送扩展握手
    auto handshake_data = MetadataExtension::createExtensionHandshake(std::nullopt);
    auto msg = BtMessage::createExtended(extension::kExtensionHandshakeId, handshake_data);
    peer->sendMessage(msg);
    
    peers_[raw_peer].handshake_sent = true;
    LOG_DEBUG("Sent extension handshake");
}

void MetadataFetcher::removePeer(std::shared_ptr<PeerConnection> peer) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto* raw_peer = peer.get();
    auto it = peers_.find(raw_peer);
    if (it == peers_.end()) {
        return;
    }
    
    // 将该 peer 请求的块重新标记为 Pending
    for (uint32_t piece : it->second.requested_pieces) {
        if (piece < piece_states_.size() && 
            piece_states_[piece] == PieceState::Requested) {
            piece_states_[piece] = PieceState::Pending;
        }
    }
    
    peers_.erase(it);
    LOG_DEBUG("Removed peer, remaining: " + std::to_string(peers_.size()));
}

// ============================================================================
// 事件处理
// ============================================================================

void MetadataFetcher::onExtensionHandshake(PeerConnection* peer, 
                                            const ExtensionHandshake& handshake) {
    if (!running_.load() || complete_.load()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto* peer_state = findPeerState(peer);
    if (!peer_state) {
        return;
    }
    
    peer_state->supports_metadata = handshake.supportsMetadata();
    peer_state->their_metadata_id = handshake.metadataExtensionId();
    
    if (handshake.metadata_size.has_value()) {
        peer_state->their_metadata_size = handshake.metadata_size.value();
        
        LOG_INFO("Peer has metadata, size=" + 
                 std::to_string(peer_state->their_metadata_size) + 
                 ", ut_metadata=" + std::to_string(peer_state->their_metadata_id));
        
        // 检查大小限制
        if (peer_state->their_metadata_size > config_.max_metadata_size) {
            LOG_WARNING("Metadata too large: " + 
                       std::to_string(peer_state->their_metadata_size));
            return;
        }
        
        // 初始化元数据缓冲区（如果是第一个报告大小的 peer）
        if (metadata_size_ == 0) {
            initializePieces(peer_state->their_metadata_size);
        }
        
        // 开始请求元数据
        if (peer_state->supports_metadata && peer_state->their_metadata_id != 0) {
            requestNextPiece(*peer_state);
        }
    } else {
        LOG_DEBUG("Peer does not have metadata");
    }
}

void MetadataFetcher::onMetadataMessage(PeerConnection* peer, 
                                         const MetadataMessage& message) {
    if (!running_.load() || complete_.load()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto* peer_state = findPeerState(peer);
    if (!peer_state) {
        return;
    }
    
    if (message.isData()) {
        LOG_DEBUG("Received metadata piece " + std::to_string(message.piece_index) +
                  ", size=" + std::to_string(message.data.size()));
        
        // 移除请求记录
        peer_state->requested_pieces.erase(message.piece_index);
        
        // 处理数据
        onPieceReceived(message.piece_index, message.data);
        
        // 请求更多
        requestNextPiece(*peer_state);
        
    } else if (message.isReject()) {
        LOG_DEBUG("Peer rejected piece " + std::to_string(message.piece_index));
        onPieceRejected(*peer_state, message.piece_index);
        
    } else if (message.isRequest()) {
        // 我们没有元数据，发送拒绝
        auto reject = MetadataExtension::createMetadataReject(
            peer_state->their_metadata_id, message.piece_index);
        auto msg = BtMessage::createExtended(extension::kExtensionMessageId, reject);
        peer_state->connection->sendMessage(msg);
    }
}

void MetadataFetcher::onPeerDisconnected(PeerConnection* peer) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto* peer_state = findPeerState(peer);
    if (!peer_state) {
        return;
    }
    
    // 将该 peer 请求的块重新标记为 Pending
    for (uint32_t piece : peer_state->requested_pieces) {
        if (piece < piece_states_.size() && 
            piece_states_[piece] == PieceState::Requested) {
            piece_states_[piece] = PieceState::Pending;
        }
    }
    
    peers_.erase(peer);
    LOG_DEBUG("Peer disconnected, remaining: " + std::to_string(peers_.size()));
    
    // 尝试从其他 peer 请求待处理的块
    for (auto& [_, ps] : peers_) {
        if (ps.supports_metadata && ps.their_metadata_id != 0) {
            requestNextPiece(ps);
        }
    }
}

// ============================================================================
// 状态查询
// ============================================================================

float MetadataFetcher::progress() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (piece_states_.empty()) {
        return 0.0f;
    }
    
    return static_cast<float>(pieces_received_) / static_cast<float>(piece_states_.size());
}

std::optional<size_t> MetadataFetcher::metadataSize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return metadata_size_ > 0 ? std::optional<size_t>(metadata_size_) : std::nullopt;
}

size_t MetadataFetcher::peerCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return peers_.size();
}

// ============================================================================
// 内部方法
// ============================================================================

void MetadataFetcher::initializePieces(size_t metadata_size) {
    metadata_size_ = metadata_size;
    
    size_t piece_count = MetadataExtension::calculatePieceCount(metadata_size);
    piece_states_.resize(piece_count, PieceState::Pending);
    metadata_buffer_.resize(metadata_size);
    pieces_received_ = 0;
    
    LOG_INFO("Initialized for " + std::to_string(piece_count) + " pieces, " +
             std::to_string(metadata_size) + " bytes");
}

void MetadataFetcher::requestNextPiece(PeerState& peer_state) {
    if (!running_.load() || complete_.load()) {
        return;
    }
    
    // 找到下一个待请求的块
    uint32_t next_piece = findNextPiece(peer_state);
    if (next_piece == UINT32_MAX) {
        return;  // 没有更多块需要请求
    }
    
    requestPieceFromPeer(peer_state, next_piece);
}

void MetadataFetcher::requestPieceFromPeer(PeerState& peer_state, uint32_t piece_index) {
    if (piece_index >= piece_states_.size()) {
        return;
    }
    
    // 创建请求消息
    auto request = MetadataExtension::createMetadataRequest(
        peer_state.their_metadata_id, piece_index);
    
    // 注意：这里需要用对方的 ut_metadata ID 作为扩展消息 ID
    // createMetadataRequest 已经包含了这个 ID
    auto msg = BtMessage::createExtended(
        extension::kExtensionMessageId, 
        std::vector<uint8_t>(request.begin() + 1, request.end())  // 去掉第一个字节（扩展 ID）
    );
    
    // 实际上 createMetadataRequest 返回的数据格式是: [extension_id][bencode dict]
    // 我们需要发送的是: Extended 消息 (id=20) + [peer's extension_id][bencode dict]
    // 重新构建正确的消息
    msg = BtMessage::createExtended(peer_state.their_metadata_id, 
        std::vector<uint8_t>(request.begin() + 1, request.end()));
    
    peer_state.connection->sendMessage(msg);
    peer_state.requested_pieces.insert(piece_index);
    piece_states_[piece_index] = PieceState::Requested;
    
    LOG_DEBUG("Requested piece " + std::to_string(piece_index));
}

void MetadataFetcher::onPieceReceived(uint32_t piece_index, 
                                       const std::vector<uint8_t>& data) {
    if (piece_index >= piece_states_.size()) {
        LOG_WARNING("Invalid piece index: " + std::to_string(piece_index));
        return;
    }
    
    if (piece_states_[piece_index] == PieceState::Received) {
        LOG_DEBUG("Duplicate piece " + std::to_string(piece_index));
        return;
    }
    
    // 验证块大小
    size_t expected_size = MetadataExtension::calculatePieceSize(piece_index, metadata_size_);
    if (data.size() != expected_size) {
        LOG_WARNING("Piece size mismatch: expected " + std::to_string(expected_size) +
                    ", got " + std::to_string(data.size()));
        piece_states_[piece_index] = PieceState::Pending;  // 重新请求
        return;
    }
    
    // 复制数据到缓冲区
    size_t offset = piece_index * extension::kMetadataBlockSize;
    std::memcpy(metadata_buffer_.data() + offset, data.data(), data.size());
    
    piece_states_[piece_index] = PieceState::Received;
    pieces_received_++;
    
    // 计算进度（不调用 progress() 以避免重复锁定）
    float prog = piece_states_.empty() ? 0.0f : 
        static_cast<float>(pieces_received_) / static_cast<float>(piece_states_.size());
    
    LOG_INFO("Received piece " + std::to_string(piece_index) + "/" + 
             std::to_string(piece_states_.size()) + 
             " (" + std::to_string(static_cast<int>(prog * 100)) + "%)");
    
    // 检查是否完成
    checkCompletion();
}

void MetadataFetcher::onPieceRejected(PeerState& peer_state, uint32_t piece_index) {
    peer_state.requested_pieces.erase(piece_index);
    peer_state.failures++;
    
    if (piece_index < piece_states_.size() && 
        piece_states_[piece_index] == PieceState::Requested) {
        piece_states_[piece_index] = PieceState::Pending;
    }
    
    // 如果失败次数过多，移除这个 peer
    if (peer_state.failures >= config_.max_retries) {
        LOG_WARNING("Peer rejected too many times, removing");
        // 不在这里直接删除，让调用者处理
    } else {
        // 从其他 peer 请求
        for (auto& [_, ps] : peers_) {
            if (&ps != &peer_state && ps.supports_metadata && ps.their_metadata_id != 0) {
                requestNextPiece(ps);
                break;
            }
        }
    }
}

void MetadataFetcher::checkCompletion() {
    if (pieces_received_ != piece_states_.size()) {
        return;
    }
    
    LOG_INFO("All pieces received, verifying...");
    
    if (verifyAndParseMetadata()) {
        complete(result_.get(), MetadataError::Success);
    } else {
        // 验证失败，重新开始
        LOG_ERROR("Metadata verification failed, retrying...");
        
        // 重置状态
        std::fill(piece_states_.begin(), piece_states_.end(), PieceState::Pending);
        pieces_received_ = 0;
        metadata_buffer_.clear();
        metadata_buffer_.resize(metadata_size_);
        
        // 从所有 peer 重新请求
        for (auto& [_, peer_state] : peers_) {
            peer_state.requested_pieces.clear();
            if (peer_state.supports_metadata && peer_state.their_metadata_id != 0) {
                requestNextPiece(peer_state);
            }
        }
    }
}

bool MetadataFetcher::verifyAndParseMetadata() {
    auto metadata = MetadataExtension::parseTorrentMetadata(metadata_buffer_, info_hash_);
    
    if (!metadata.has_value()) {
        return false;
    }
    
    result_ = std::make_unique<TorrentMetadata>(std::move(metadata.value()));
    return true;
}

void MetadataFetcher::complete(const TorrentMetadata* metadata, MetadataError error) {
    if (complete_.exchange(true)) {
        return;  // 已经完成
    }
    
    running_.store(false);
    timeout_timer_.cancel();
    
    if (error == MetadataError::Success) {
        LOG_INFO("Metadata fetch completed successfully!");
    } else {
        LOG_ERROR("Metadata fetch failed with error " + std::to_string(static_cast<int>(error)));
    }
    
    if (callback_) {
        // 在 io_context 中调用回调
        auto callback = std::move(callback_);
        asio::post(io_context_, [callback, metadata, error]() {
            callback(metadata, error);
        });
    }
}

void MetadataFetcher::startTimeoutTimer() {
    timeout_timer_.expires_after(config_.fetch_timeout);
    
    auto self = shared_from_this();
    timeout_timer_.async_wait([self](const asio::error_code& ec) {
        if (!ec) {
            self->onTimeout();
        }
    });
}

void MetadataFetcher::onTimeout() {
    if (!running_.load() || complete_.load()) {
        return;
    }
    
    LOG_ERROR("Metadata fetch timeout");
    complete(nullptr, MetadataError::Timeout);
}

MetadataFetcher::PeerState* MetadataFetcher::findPeerState(PeerConnection* peer) {
    auto it = peers_.find(peer);
    return it != peers_.end() ? &it->second : nullptr;
}

uint32_t MetadataFetcher::findNextPiece(const PeerState& peer_state) {
    // 优先找 Pending 状态的块
    for (uint32_t i = 0; i < piece_states_.size(); ++i) {
        if (piece_states_[i] == PieceState::Pending &&
            peer_state.requested_pieces.count(i) == 0) {
            return i;
        }
    }
    
    return UINT32_MAX;
}

} // namespace magnet::protocols


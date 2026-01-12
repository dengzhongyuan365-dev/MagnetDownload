#include "magnet/application/download_controller.h"
#include "magnet/utils/logger.h"
#include "magnet/utils/sha1.h"

#include <random>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace magnet::application {

// 日志宏
#define LOG_DEBUG(msg) magnet::utils::Logger::instance().debug(msg)
#define LOG_INFO(msg) magnet::utils::Logger::instance().info(msg)
#define LOG_WARNING(msg) magnet::utils::Logger::instance().warn(msg)
#define LOG_ERROR(msg) magnet::utils::Logger::instance().error(msg)

// ============================================================================
// 构造和析构
// ============================================================================

DownloadController::DownloadController(asio::io_context& io_context)
    : io_context_(io_context)
    , progress_timer_(io_context)
    , peer_search_timer_(io_context)
    , metadata_timeout_timer_(io_context)
{
    my_peer_id_ = generatePeerId();
    LOG_DEBUG("DownloadController created, peer_id=" + my_peer_id_);
}

DownloadController::~DownloadController() {
    stop();
    LOG_DEBUG("DownloadController destroyed");
}

// ============================================================================
// 生命周期管理
// ============================================================================

bool DownloadController::start(const DownloadConfig& config) {
    // 检查状态
    DownloadState expected = DownloadState::Idle;
    if (!state_.compare_exchange_strong(expected, DownloadState::ResolvingMetadata)) {
        if (expected == DownloadState::Paused) {
            // 从暂停恢复
            resume();
            return true;
        }
        LOG_WARNING("Cannot start: invalid state " + std::string(downloadStateToString(expected)));
        return false;
    }
    
    config_ = config;
    start_time_ = std::chrono::steady_clock::now();
    
    LOG_INFO("Starting download: " + config.magnet_uri);
    
    // 解析 Magnet URI
    auto parse_result = protocols::parseMagnetUri(config.magnet_uri);
    if (!parse_result.is_ok()) {
        fail("Failed to parse magnet URI");
        return false;
    }
    
    auto& magnet_info = parse_result.value();
    
    // 检查是否有有效的 info_hash
    if (!magnet_info.info_hash.has_value()) {
        fail("Invalid magnet URI: no info_hash");
        return false;
    }
    
    // 提取 info_hash
    {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        metadata_.info_hash = magnet_info.info_hash.value();
        metadata_.name = magnet_info.display_name.empty() ? "unknown" : magnet_info.display_name;
    }
    
    LOG_INFO("info_hash: " + magnet_info.info_hash->toHex());
    
    // 通知状态变化
    setState(DownloadState::ResolvingMetadata);
    
    // 初始化 DHT
    initializeDht();
    
    // 开始查找 Peer
    findPeers();
    
    // 启动元数据超时定时器
    metadata_timeout_timer_.expires_after(config_.metadata_timeout);
    auto self = shared_from_this();
    metadata_timeout_timer_.async_wait([self](const asio::error_code& ec) {
        if (!ec && self->state_.load() == DownloadState::ResolvingMetadata) {
            self->fail("Metadata timeout");
        }
    });
    
    // 启动 Peer 搜索定时器
    startPeerSearchTimer();
    
    return true;
}

void DownloadController::pause() {
    DownloadState current = state_.load();
    if (current != DownloadState::Downloading) {
        return;
    }
    
    LOG_INFO("Pausing download");
    setState(DownloadState::Paused);
    
    // 取消定时器
    progress_timer_.cancel();
}

void DownloadController::resume() {
    DownloadState current = state_.load();
    if (current != DownloadState::Paused) {
        return;
    }
    
    LOG_INFO("Resuming download");
    setState(DownloadState::Downloading);
    
    // 重新启动定时器
    startProgressTimer();
    
    // 继续请求数据
    requestMoreBlocks();
}

void DownloadController::stop() {
    DownloadState current = state_.load();
    if (current == DownloadState::Idle || 
        current == DownloadState::Stopped ||
        current == DownloadState::Completed ||
        current == DownloadState::Failed) {
        return;
    }
    
    LOG_INFO("Stopping download");
    
    // 取消所有定时器
    progress_timer_.cancel();
    peer_search_timer_.cancel();
    metadata_timeout_timer_.cancel();
    
    // 停止组件
    if (peer_manager_) {
        peer_manager_->stop();
    }
    if (dht_client_) {
        dht_client_->stop();
    }
    
    setState(DownloadState::Stopped);
}

// ============================================================================
// 状态查询
// ============================================================================

DownloadProgress DownloadController::progress() const {
    std::lock_guard<std::mutex> lock(progress_mutex_);
    return current_progress_;
}

TorrentMetadata DownloadController::metadata() const {
    std::lock_guard<std::mutex> lock(metadata_mutex_);
    return metadata_;
}

// ============================================================================
// 回调设置
// ============================================================================

void DownloadController::setStateCallback(DownloadStateCallback callback) {
    state_callback_ = std::move(callback);
}

void DownloadController::setProgressCallback(DownloadProgressCallback callback) {
    progress_callback_ = std::move(callback);
}

void DownloadController::setCompletedCallback(DownloadCompletedCallback callback) {
    completed_callback_ = std::move(callback);
}

void DownloadController::setMetadataCallback(MetadataReceivedCallback callback) {
    metadata_callback_ = std::move(callback);
}

// ============================================================================
// 元数据设置
// ============================================================================

void DownloadController::setMetadata(const TorrentMetadata& metadata) {
    {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        metadata_ = metadata;
        has_metadata_ = true;
    }
    
    LOG_INFO("Metadata set: " + metadata.name + 
             ", size=" + std::to_string(metadata.total_size) +
             ", pieces=" + std::to_string(metadata.piece_count));
    
    // 取消元数据超时
    metadata_timeout_timer_.cancel();
    
    // 通知回调
    if (metadata_callback_) {
        metadata_callback_(metadata);
    }
    
    // 初始化分片状态
    initializePieces();
    
    // 转换到下载状态
    setState(DownloadState::Downloading);
    
    // 启动进度更新定时器
    startProgressTimer();
    
    // 开始请求数据
    requestMoreBlocks();
}

// ============================================================================
// 内部方法
// ============================================================================

void DownloadController::initializeDht() {
    protocols::DhtClientConfig dht_config;
    dht_config.listen_port = 0;  // 随机端口
    
    // 添加引导节点
    dht_config.bootstrap_nodes = {
        {"router.bittorrent.com", 6881},
        {"router.utorrent.com", 6881},
        {"dht.transmissionbt.com", 6881}
    };
    
    dht_client_ = std::make_shared<protocols::DhtClient>(io_context_, dht_config);
    dht_client_->start();
    
    LOG_INFO("DHT client started on port " + std::to_string(dht_client_->localPort()));
}

void DownloadController::findPeers() {
    if (!dht_client_) {
        return;
    }
    
    protocols::InfoHash info_hash;
    {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        info_hash = metadata_.info_hash;
    }
    
    LOG_DEBUG("Searching for peers for " + info_hash.toHex());
    
    auto self = shared_from_this();
    
    // 每找到一个 Peer 调用一次
    dht_client_->findPeers(info_hash, 
        [self](const protocols::PeerInfo& peer) {
            // 单个 Peer 回调
            std::vector<protocols::PeerInfo> peers = {peer};
            self->onPeersFound(peers);
        },
        [self](bool success, const std::vector<protocols::PeerInfo>& all_peers) {
            // 查找完成回调
            if (success && !all_peers.empty()) {
                LOG_INFO("Peer lookup complete, found " + std::to_string(all_peers.size()) + " peers");
            }
        });
}

void DownloadController::onPeersFound(const std::vector<protocols::PeerInfo>& peers) {
    if (peers.empty()) {
        LOG_DEBUG("No peers found");
        return;
    }
    
    LOG_INFO("Found " + std::to_string(peers.size()) + " peers");
    
    // 初始化 PeerManager（如果还没有）
    if (!peer_manager_) {
        protocols::InfoHash info_hash;
        {
            std::lock_guard<std::mutex> lock(metadata_mutex_);
            info_hash = metadata_.info_hash;
        }
        
        protocols::PeerManagerConfig pm_config;
        pm_config.max_connections = config_.max_connections;
        
        peer_manager_ = std::make_shared<protocols::PeerManager>(
            io_context_, info_hash, my_peer_id_, pm_config);
        
        // 设置回调
        auto self = shared_from_this();
        
        peer_manager_->setPieceCallback(
            [self](uint32_t piece, uint32_t begin, const std::vector<uint8_t>& data) {
                self->onPieceReceived(piece, begin, data);
            });
        
        peer_manager_->setPeerStatusCallback(
            [self](const network::TcpEndpoint& ep, bool connected) {
                self->onPeerStatusChanged(ep, connected);
            });
        
        peer_manager_->setNeedMorePeersCallback([self]() {
            self->findPeers();
        });
        
        peer_manager_->start();
    }
    
    // 添加 Peer
    std::vector<network::TcpEndpoint> endpoints;
    for (const auto& peer : peers) {
        endpoints.push_back({peer.ip, peer.port});
    }
    peer_manager_->addPeers(endpoints);
    
    // 更新进度中的 Peer 数量
    {
        std::lock_guard<std::mutex> lock(progress_mutex_);
        current_progress_.total_peers += peers.size();
    }
}

void DownloadController::onPieceReceived(uint32_t piece_index, uint32_t begin, 
                                          const std::vector<uint8_t>& data) {
    if (state_.load() != DownloadState::Downloading) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(pieces_mutex_);
    
    if (piece_index >= pieces_.size()) {
        return;
    }
    
    auto& piece = pieces_[piece_index];
    
    // 计算块索引
    size_t block_index = begin / kBlockSize;
    if (block_index >= piece.blocks.size()) {
        return;
    }
    
    // 检查是否已经收到
    if (piece.blocks[block_index]) {
        return;
    }
    
    // 保存数据
    if (piece.data.size() < begin + data.size()) {
        piece.data.resize(begin + data.size());
    }
    std::copy(data.begin(), data.end(), piece.data.begin() + begin);
    
    piece.blocks[block_index] = true;
    piece.downloaded += data.size();
    
    LOG_DEBUG("Received block: piece=" + std::to_string(piece_index) + 
              " begin=" + std::to_string(begin) + 
              " size=" + std::to_string(data.size()));
    
    // 检查分片是否完整
    if (piece.isComplete()) {
        piece.state = PieceState::Downloaded;
        
        // 异步验证
        auto self = shared_from_this();
        asio::post(io_context_, [self, piece_index]() {
            if (self->verifyPiece(piece_index)) {
                self->checkCompletion();
            }
            self->requestMoreBlocks();
        });
    }
    
    // 更新进度
    {
        std::lock_guard<std::mutex> progress_lock(progress_mutex_);
        current_progress_.downloaded_size += data.size();
    }
}

void DownloadController::onPeerStatusChanged(const network::TcpEndpoint& endpoint, 
                                              bool connected) {
    std::lock_guard<std::mutex> lock(progress_mutex_);
    
    if (connected) {
        current_progress_.connected_peers++;
        LOG_DEBUG("Peer connected: " + endpoint.toString());
    } else {
        if (current_progress_.connected_peers > 0) {
            current_progress_.connected_peers--;
        }
        LOG_DEBUG("Peer disconnected: " + endpoint.toString());
    }
}

void DownloadController::initializePieces() {
    std::lock_guard<std::mutex> lock(pieces_mutex_);
    
    TorrentMetadata meta;
    {
        std::lock_guard<std::mutex> meta_lock(metadata_mutex_);
        meta = metadata_;
    }
    
    pieces_.clear();
    pieces_.reserve(meta.piece_count);
    
    for (size_t i = 0; i < meta.piece_count; ++i) {
        PieceInfo piece;
        piece.index = static_cast<uint32_t>(i);
        piece.size = getPieceSize(static_cast<uint32_t>(i));
        
        // 计算块数
        size_t block_count = (piece.size + kBlockSize - 1) / kBlockSize;
        piece.blocks.resize(block_count, false);
        
        pieces_.push_back(std::move(piece));
    }
    
    // 初始化位图
    bitfield_.resize(meta.piece_count, false);
    
    // 更新进度
    {
        std::lock_guard<std::mutex> progress_lock(progress_mutex_);
        current_progress_.total_size = meta.total_size;
        current_progress_.total_pieces = meta.piece_count;
    }
    
    LOG_INFO("Initialized " + std::to_string(meta.piece_count) + " pieces");
}

int32_t DownloadController::selectNextPiece() {
    // 注意：调用时已持有 pieces_mutex_
    
    // 收集所有 Missing 的分片
    std::vector<uint32_t> missing_pieces;
    for (const auto& piece : pieces_) {
        if (piece.state == PieceState::Missing) {
            missing_pieces.push_back(piece.index);
        }
    }
    
    if (missing_pieces.empty()) {
        return -1;
    }
    
    // 稀有优先算法：计算每个分片的稀有度
    // 稀有度 = 拥有该分片的 Peer 数量（越少越稀有）
    
    if (peer_manager_) {
        int32_t best_piece = -1;
        size_t min_availability = SIZE_MAX;
        
        for (uint32_t piece_idx : missing_pieces) {
            auto peers = peer_manager_->getPeersWithPiece(piece_idx);
            size_t availability = peers.size();
            
            // 至少有一个 Peer 拥有这个分片
            if (availability > 0 && availability < min_availability) {
                min_availability = availability;
                best_piece = static_cast<int32_t>(piece_idx);
            }
        }
        
        if (best_piece >= 0) {
            return best_piece;
        }
    }
    
    // 如果没有找到有 Peer 的分片，返回第一个 Missing 的
    return static_cast<int32_t>(missing_pieces[0]);
}

void DownloadController::requestPiece(uint32_t piece_index) {
    // 注意：调用时已持有 pieces_mutex_
    
    if (piece_index >= pieces_.size()) {
        return;
    }
    
    auto& piece = pieces_[piece_index];
    if (piece.state != PieceState::Missing) {
        return;
    }
    
    piece.state = PieceState::Pending;
    
    // 生成块请求
    for (size_t i = 0; i < piece.blocks.size(); ++i) {
        if (piece.blocks[i]) {
            continue;  // 已下载
        }
        
        uint32_t begin = static_cast<uint32_t>(i * kBlockSize);
        uint32_t length = static_cast<uint32_t>(
            std::min(kBlockSize, piece.size - begin));
        
        protocols::BlockInfo block{piece_index, begin, length};
        
        if (peer_manager_) {
            peer_manager_->requestBlock(block);
        }
    }
    
    LOG_DEBUG("Requested piece " + std::to_string(piece_index));
}

void DownloadController::requestMoreBlocks() {
    if (state_.load() != DownloadState::Downloading) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(pieces_mutex_);
    
    // 统计正在下载的分片数
    size_t pending_count = 0;
    for (const auto& piece : pieces_) {
        if (piece.state == PieceState::Pending) {
            pending_count++;
        }
    }
    
    // 限制并发下载的分片数
    const size_t max_pending = 10;
    while (pending_count < max_pending) {
        int32_t next_piece = selectNextPiece();
        if (next_piece < 0) {
            break;
        }
        
        requestPiece(static_cast<uint32_t>(next_piece));
        pending_count++;
    }
    
    // 更新进度
    {
        std::lock_guard<std::mutex> progress_lock(progress_mutex_);
        current_progress_.pending_pieces = pending_count;
    }
}

bool DownloadController::verifyPiece(uint32_t piece_index) {
    std::lock_guard<std::mutex> lock(pieces_mutex_);
    
    if (piece_index >= pieces_.size()) {
        return false;
    }
    
    auto& piece = pieces_[piece_index];
    if (piece.state != PieceState::Downloaded) {
        return false;
    }
    
    TorrentMetadata meta;
    {
        std::lock_guard<std::mutex> meta_lock(metadata_mutex_);
        meta = metadata_;
    }
    
    // 计算 SHA1
    if (piece_index < meta.piece_hashes.size()) {
        auto expected_hash = meta.piece_hashes[piece_index];
        auto actual_hash = utils::sha1(piece.data.data(), piece.data.size());
        
        if (actual_hash != expected_hash) {
            LOG_WARNING("Piece " + std::to_string(piece_index) + " verification failed");
            piece.state = PieceState::Failed;
            // 重置，准备重新下载
            piece.state = PieceState::Missing;
            piece.downloaded = 0;
            std::fill(piece.blocks.begin(), piece.blocks.end(), false);
            piece.data.clear();
            return false;
        }
    }
    
    piece.state = PieceState::Verified;
    bitfield_[piece_index] = true;
    
    // 广播 Have
    if (peer_manager_) {
        peer_manager_->broadcastHave(piece_index);
    }
    
    LOG_DEBUG("Piece " + std::to_string(piece_index) + " verified");
    
    // 更新进度
    {
        std::lock_guard<std::mutex> progress_lock(progress_mutex_);
        current_progress_.completed_pieces++;
    }
    
    return true;
}

void DownloadController::updateProgress() {
    auto now = std::chrono::steady_clock::now();
    
    {
        std::lock_guard<std::mutex> lock(progress_mutex_);
        
        // 计算速度
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_progress_update_).count();
        
        if (elapsed > 0) {
            size_t bytes_diff = current_progress_.downloaded_size - last_downloaded_size_;
            current_progress_.download_speed = 
                static_cast<double>(bytes_diff) * 1000.0 / elapsed;
        }
        
        last_progress_update_ = now;
        last_downloaded_size_ = current_progress_.downloaded_size;
        
        // 获取 Peer 统计
        if (peer_manager_) {
            auto pm_stats = peer_manager_->getStatistics();
            current_progress_.connected_peers = pm_stats.peers_connected;
            current_progress_.total_peers = pm_stats.total_peers_known;
        }
    }
    
    // 通知回调
    if (progress_callback_) {
        std::lock_guard<std::mutex> lock(progress_mutex_);
        progress_callback_(current_progress_);
    }
}

void DownloadController::checkCompletion() {
    bool all_verified = false;
    
    {
        std::lock_guard<std::mutex> lock(pieces_mutex_);
        all_verified = std::all_of(pieces_.begin(), pieces_.end(), 
                                    [](const PieceInfo& p) {
                                        return p.state == PieceState::Verified;
                                    });
    }
    
    if (all_verified) {
        LOG_INFO("All pieces verified, download complete!");
        setState(DownloadState::Completed);
        
        // 停止组件
        progress_timer_.cancel();
        peer_search_timer_.cancel();
        
        if (peer_manager_) {
            peer_manager_->stop();
        }
        
        // 通知完成
        if (completed_callback_) {
            completed_callback_(true, "");
        }
    }
}

void DownloadController::setState(DownloadState new_state) {
    DownloadState old_state = state_.exchange(new_state);
    
    if (old_state != new_state) {
        LOG_INFO("State changed: " + std::string(downloadStateToString(old_state)) +
                 " -> " + std::string(downloadStateToString(new_state)));
        
        if (state_callback_) {
            state_callback_(new_state);
        }
    }
}

void DownloadController::fail(const std::string& error) {
    LOG_ERROR("Download failed: " + error);
    error_message_ = error;
    
    // 停止所有活动
    progress_timer_.cancel();
    peer_search_timer_.cancel();
    metadata_timeout_timer_.cancel();
    
    if (peer_manager_) {
        peer_manager_->stop();
    }
    if (dht_client_) {
        dht_client_->stop();
    }
    
    setState(DownloadState::Failed);
    
    if (completed_callback_) {
        completed_callback_(false, error);
    }
}

void DownloadController::startProgressTimer() {
    auto self = shared_from_this();
    
    progress_timer_.expires_after(std::chrono::seconds(1));
    progress_timer_.async_wait([self](const asio::error_code& ec) {
        if (!ec && self->state_.load() == DownloadState::Downloading) {
            self->updateProgress();
            self->startProgressTimer();  // 重新启动
        }
    });
}

void DownloadController::startPeerSearchTimer() {
    auto self = shared_from_this();
    
    peer_search_timer_.expires_after(config_.peer_search_interval);
    peer_search_timer_.async_wait([self](const asio::error_code& ec) {
        if (!ec) {
            auto state = self->state_.load();
            if (state == DownloadState::ResolvingMetadata || 
                state == DownloadState::Downloading) {
                self->findPeers();
                self->startPeerSearchTimer();  // 重新启动
            }
        }
    });
}

size_t DownloadController::getPieceSize(uint32_t piece_index) const {
    std::lock_guard<std::mutex> lock(metadata_mutex_);
    
    if (piece_index >= metadata_.piece_count) {
        return 0;
    }
    
    // 最后一个分片可能较小
    if (piece_index == metadata_.piece_count - 1) {
        size_t remainder = metadata_.total_size % metadata_.piece_length;
        return remainder > 0 ? remainder : metadata_.piece_length;
    }
    
    return metadata_.piece_length;
}

std::string DownloadController::generatePeerId() {
    // 格式: -MT0001-xxxxxxxxxxxx
    // MT = MagnetDownload, 0001 = 版本, 后 12 字符随机
    
    std::string peer_id = "-MT0001-";
    
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dist(0, 35);
    
    const char* chars = "0123456789abcdefghijklmnopqrstuvwxyz";
    
    for (int i = 0; i < 12; ++i) {
        peer_id += chars[dist(gen)];
    }
    
    return peer_id;
}

} // namespace magnet::application


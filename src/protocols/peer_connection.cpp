#include "magnet/protocols/peer_connection.h"
#include "magnet/protocols/metadata_extension.h"
#include "magnet/utils/logger.h"

namespace magnet::protocols {

// 日志宏
#define LOG_DEBUG(msg) magnet::utils::Logger::instance().debug(msg)
#define LOG_INFO(msg) magnet::utils::Logger::instance().info(msg)
#define LOG_WARNING(msg) magnet::utils::Logger::instance().warn(msg)
#define LOG_ERROR(msg) magnet::utils::Logger::instance().error(msg)

// ============================================================================
// 构造函数和析构函数
// ============================================================================

PeerConnection::PeerConnection(asio::io_context& io_context,
                               const InfoHash& info_hash,
                               const std::string& my_peer_id)
    : io_context_(io_context)
    , info_hash_(info_hash)
    , my_peer_id_(my_peer_id)
{
    // 确保 peer_id 是 20 字节
    if (my_peer_id_.size() < 20) {
        my_peer_id_.resize(20, '0');
    } else if (my_peer_id_.size() > 20) {
        my_peer_id_.resize(20);
    }
    
    LOG_DEBUG("PeerConnection created");
}

PeerConnection::~PeerConnection() {
    disconnect();
    LOG_DEBUG("PeerConnection destroyed");
}

// ============================================================================
// 连接管理
// ============================================================================

void PeerConnection::connect(const network::TcpEndpoint& endpoint, 
                             PeerConnectCallback callback) {
    // 检查状态
    PeerConnectionState expected = PeerConnectionState::Disconnected;
    if (!state_.compare_exchange_strong(expected, PeerConnectionState::Connecting)) {
        LOG_WARNING("PeerConnection::connect called in invalid state");
        if (callback) {
            asio::post(io_context_, [callback]() { callback(false); });
        }
        return;
    }
    
    peer_info_.ip = endpoint.ip;
    peer_info_.port = endpoint.port;
    connect_callback_ = std::move(callback);
    
    LOG_INFO("Connecting to peer " + endpoint.toString());
    
    // 创建 TCP 客户端
    tcp_client_ = std::make_shared<network::TcpClient>(io_context_);
    
    // 设置断线回调
    auto self = shared_from_this();
    tcp_client_->setDisconnectCallback([self](const asio::error_code& ec) {
        self->onDisconnect(ec);
    });
    
    // 连接
    tcp_client_->connect(endpoint, 
        [self](const asio::error_code& ec) {
            self->onConnected(ec);
        },
        std::chrono::seconds(30)  // 增加到 30 秒超时
    );
}

void PeerConnection::disconnect() {
    PeerConnectionState current = state_.load();
    if (current == PeerConnectionState::Disconnected) {
        return;
    }
    
    state_.store(PeerConnectionState::Closing);
    
    if (tcp_client_) {
        tcp_client_->close();
        tcp_client_.reset();
    }
    
    // 清理接收缓冲区
    receive_buffer_.clear();
    handshake_received_ = false;
    
    // 清理待处理请求
    {
        std::lock_guard<std::mutex> lock(requests_mutex_);
        pending_requests_.clear();
    }
    
    state_.store(PeerConnectionState::Disconnected);
    LOG_INFO("Disconnected from peer " + peer_info_.toString());
}

// ============================================================================
// 发送消息
// ============================================================================

void PeerConnection::sendInterested() {
    if (!isConnected()) return;
    
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        peer_state_.am_interested = true;
    }
    
    sendMessage(BtMessage::createInterested());
    LOG_DEBUG("Sent Interested to " + peer_info_.toString());
}

void PeerConnection::sendNotInterested() {
    if (!isConnected()) return;
    
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        peer_state_.am_interested = false;
    }
    
    sendMessage(BtMessage::createNotInterested());
    LOG_DEBUG("Sent NotInterested to " + peer_info_.toString());
}

void PeerConnection::sendChoke() {
    if (!isConnected()) return;
    
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        peer_state_.am_choking = true;
    }
    
    sendMessage(BtMessage::createChoke());
    LOG_DEBUG("Sent Choke to " + peer_info_.toString());
}

void PeerConnection::sendUnchoke() {
    if (!isConnected()) return;
    
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        peer_state_.am_choking = false;
    }
    
    sendMessage(BtMessage::createUnchoke());
    LOG_DEBUG("Sent Unchoke to " + peer_info_.toString());
}

void PeerConnection::sendHave(uint32_t piece_index) {
    if (!isConnected()) return;
    
    sendMessage(BtMessage::createHave(piece_index));
}

void PeerConnection::sendBitfield(const std::vector<bool>& bitfield) {
    if (!isConnected()) return;
    
    sendMessage(BtMessage::createBitfield(bitfield));
}

void PeerConnection::requestBlock(const BlockInfo& block) {
    if (!isConnected()) return;
    
    {
        std::lock_guard<std::mutex> lock(requests_mutex_);
        pending_requests_.push_back(block);
    }
    
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        statistics_.requests_pending++;
    }
    
    sendMessage(BtMessage::createRequest(block));
    LOG_DEBUG("Requested block: piece=" + std::to_string(block.piece_index) + 
              " begin=" + std::to_string(block.begin) + 
              " len=" + std::to_string(block.length));
}

void PeerConnection::cancelBlock(const BlockInfo& block) {
    if (!isConnected()) return;
    
    {
        std::lock_guard<std::mutex> lock(requests_mutex_);
        auto it = std::find(pending_requests_.begin(), pending_requests_.end(), block);
        if (it != pending_requests_.end()) {
            pending_requests_.erase(it);
        }
    }
    
    sendMessage(BtMessage::createCancel(block));
}

void PeerConnection::sendPiece(const PieceBlock& block) {
    if (!isConnected()) return;
    
    sendMessage(BtMessage::createPiece(block));
    
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        statistics_.bytes_uploaded += block.data.size();
        statistics_.pieces_sent++;
    }
}

void PeerConnection::sendKeepAlive() {
    if (!isConnected()) return;
    
    sendMessage(BtMessage::createKeepAlive());
}

// ============================================================================
// 状态查询
// ============================================================================

PeerState PeerConnection::peerState() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return peer_state_;
}

std::vector<bool> PeerConnection::peerBitfield() const {
    std::lock_guard<std::mutex> lock(bitfield_mutex_);
    return peer_bitfield_;
}

bool PeerConnection::hasPiece(uint32_t index) const {
    std::lock_guard<std::mutex> lock(bitfield_mutex_);
    if (index >= peer_bitfield_.size()) {
        return false;
    }
    return peer_bitfield_[index];
}

PeerStatistics PeerConnection::getStatistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return statistics_;
}

// ============================================================================
// 回调设置
// ============================================================================

void PeerConnection::setStateCallback(PeerStateCallback callback) {
    state_callback_ = std::move(callback);
}

void PeerConnection::setMessageCallback(PeerMessageCallback callback) {
    message_callback_ = std::move(callback);
}

void PeerConnection::setPieceCallback(PeerPieceCallback callback) {
    piece_callback_ = std::move(callback);
}

void PeerConnection::setErrorCallback(PeerErrorCallback callback) {
    error_callback_ = std::move(callback);
}

// ============================================================================
// 内部方法
// ============================================================================

void PeerConnection::onConnected(const asio::error_code& ec) {
    if (ec) {
        LOG_ERROR("Failed to connect to peer: " + ec.message());
        setState(PeerConnectionState::Disconnected);
        if (connect_callback_) {
            connect_callback_(false);
            connect_callback_ = nullptr;
        }
        return;
    }
    
    LOG_INFO("TCP connected to " + peer_info_.toString());
    setState(PeerConnectionState::Handshaking);
    
    // 记录连接时间
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        statistics_.connect_time = std::chrono::steady_clock::now();
        statistics_.last_activity = statistics_.connect_time;
    }
    
    // 开始接收数据
    auto self = shared_from_this();
    tcp_client_->startReceive([self](const asio::error_code& ec, 
                                      const std::vector<uint8_t>& data) {
        self->onReceive(ec, data);
    });
    
    // 发送握手
    sendHandshake();
}

void PeerConnection::onReceive(const asio::error_code& ec, 
                               const std::vector<uint8_t>& data) {
    if (ec) {
        if (ec != asio::error::operation_aborted) {
            LOG_WARNING("Receive error: " + ec.message());
            reportError("Receive error: " + ec.message());
        }
        return;
    }
    
    // 追加到接收缓冲区
    receive_buffer_.insert(receive_buffer_.end(), data.begin(), data.end());
    
    // 更新活动时间
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        statistics_.last_activity = std::chrono::steady_clock::now();
    }
    
    // 处理消息
    if (!handshake_received_) {
        // 还在等待握手
        if (handleHandshake()) {
            // 握手成功，继续处理后续消息
            processMessages();
        }
    } else {
        // 已握手，处理普通消息
        processMessages();
    }
}

void PeerConnection::onDisconnect(const asio::error_code& ec) {
    LOG_INFO("Peer disconnected: " + ec.message());
    setState(PeerConnectionState::Disconnected);
    reportError("Disconnected: " + ec.message());
}

void PeerConnection::sendHandshake() {
    auto handshake = Handshake::create(info_hash_, my_peer_id_);
    auto data = handshake.encode();
    
    LOG_DEBUG("Sending handshake to " + peer_info_.toString() + 
              ", size=" + std::to_string(data.size()) + " bytes" +
              ", info_hash=" + info_hash_.toHex().substr(0, 16) + "..." +
              ", peer_id=" + my_peer_id_.substr(0, 8) + "...");
    
    tcp_client_->send(data, [this](const asio::error_code& ec, size_t bytes_sent) {
        if (ec) {
            LOG_ERROR("Failed to send handshake: " + ec.message());
        } else {
            LOG_DEBUG("Handshake sent successfully, " + std::to_string(bytes_sent) + " bytes");
        }
    });
}

bool PeerConnection::handleHandshake() {
    // 握手消息是 68 字节
    if (receive_buffer_.size() < Handshake::kSize) {
        LOG_DEBUG("Handshake incomplete, need " + std::to_string(Handshake::kSize) + 
                  " bytes, have " + std::to_string(receive_buffer_.size()));
        return false;  // 数据不足
    }
    
    LOG_DEBUG("Attempting to decode handshake from " + peer_info_.toString() + 
              ", buffer size=" + std::to_string(receive_buffer_.size()));
    
    // 解码握手
    auto handshake = Handshake::decode(receive_buffer_.data(), receive_buffer_.size());
    if (!handshake) {
        LOG_ERROR("Invalid handshake from " + peer_info_.toString() + 
                  " - failed to decode");
        disconnect();
        if (connect_callback_) {
            connect_callback_(false);
            connect_callback_ = nullptr;
        }
        return false;
    }
    
    LOG_DEBUG("Handshake decoded successfully from " + peer_info_.toString());
    
    // 验证 info_hash
    if (!handshake->matchInfoHash(info_hash_)) {
        LOG_ERROR("info_hash mismatch from " + peer_info_.toString() + 
                  " - expected " + info_hash_.toHex().substr(0, 16) + "...");
        disconnect();
        if (connect_callback_) {
            connect_callback_(false);
            connect_callback_ = nullptr;
        }
        return false;
    }
    
    LOG_DEBUG("info_hash verified for " + peer_info_.toString());
    
    // 保存 peer_id
    peer_info_.peer_id = handshake->peer_id;
    
    // 检查对方是否支持扩展协议
    bool peer_supports_extension = handshake->supportsExtension();
    LOG_DEBUG("Peer " + peer_info_.toString() + " supports extension: " + 
              (peer_supports_extension ? "yes" : "no"));
    
    // 从缓冲区移除握手数据
    receive_buffer_.erase(receive_buffer_.begin(), 
                          receive_buffer_.begin() + Handshake::kSize);
    
    handshake_received_ = true;
    setState(PeerConnectionState::Connected);
    
    LOG_INFO("Handshake successful with " + peer_info_.toString());
    
    // 如果对方支持扩展协议，主动发送扩展握手
    if (peer_supports_extension) {
        sendExtensionHandshake();
    }
    
    // 回调通知
    if (connect_callback_) {
        connect_callback_(true);
        connect_callback_ = nullptr;
    }
    
    return true;
}

void PeerConnection::processMessages() {
    while (true) {
        // 至少需要 4 字节来读取长度
        if (receive_buffer_.size() < 4) {
            break;
        }
        
        // 获取消息总长度
        size_t msg_len = BtMessage::getMessageLength(receive_buffer_.data(), 
                                                      receive_buffer_.size());
        if (msg_len == 0 || receive_buffer_.size() < msg_len) {
            break;  // 数据不足
        }
        
        // 解码消息
        auto msg = BtMessage::decode(receive_buffer_.data(), msg_len);
        if (msg) {
            handleMessage(*msg);
            
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                statistics_.messages_received++;
            }
        }
        
        // 从缓冲区移除已处理的数据
        receive_buffer_.erase(receive_buffer_.begin(), 
                              receive_buffer_.begin() + msg_len);
    }
}

void PeerConnection::handleMessage(const BtMessage& msg) {
    LOG_DEBUG("Received BT message type=" + std::to_string(static_cast<int>(msg.type())) +
              " from " + peer_info_.toString());
    
    switch (msg.type()) {
        case BtMessageType::KeepAlive:
            // 只更新活动时间，已在 onReceive 中处理
            break;
            
        case BtMessageType::Choke:
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                peer_state_.peer_choking = true;
            }
            LOG_DEBUG("Received Choke from " + peer_info_.toString());
            break;
            
        case BtMessageType::Unchoke:
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                peer_state_.peer_choking = false;
            }
            LOG_DEBUG("Received Unchoke from " + peer_info_.toString());
            break;
            
        case BtMessageType::Interested:
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                peer_state_.peer_interested = true;
            }
            LOG_DEBUG("Received Interested from " + peer_info_.toString());
            break;
            
        case BtMessageType::NotInterested:
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                peer_state_.peer_interested = false;
            }
            LOG_DEBUG("Received NotInterested from " + peer_info_.toString());
            break;
            
        case BtMessageType::Have:
            {
                std::lock_guard<std::mutex> lock(bitfield_mutex_);
                uint32_t index = msg.pieceIndex();
                if (index >= peer_bitfield_.size()) {
                    peer_bitfield_.resize(index + 1, false);
                }
                peer_bitfield_[index] = true;
            }
            break;
            
        case BtMessageType::Bitfield:
            {
                std::lock_guard<std::mutex> lock(bitfield_mutex_);
                peer_bitfield_ = msg.bitfield();
            }
            LOG_DEBUG("Received Bitfield from " + peer_info_.toString() + 
                      ", pieces=" + std::to_string(msg.bitfield().size()));
            break;
            
        case BtMessageType::Request:
            // 对方请求数据，通知上层处理
            break;
            
        case BtMessageType::Piece:
            {
                auto block = msg.toPieceBlock();
                
                // 从待处理请求中移除
                {
                    std::lock_guard<std::mutex> lock(requests_mutex_);
                    auto it = std::find_if(pending_requests_.begin(), 
                                           pending_requests_.end(),
                                           [&block](const BlockInfo& req) {
                                               return req.piece_index == block.piece_index &&
                                                      req.begin == block.begin;
                                           });
                    if (it != pending_requests_.end()) {
                        pending_requests_.erase(it);
                    }
                }
                
                // 更新统计
                {
                    std::lock_guard<std::mutex> lock(stats_mutex_);
                    statistics_.bytes_downloaded += block.data.size();
                    statistics_.pieces_received++;
                    if (statistics_.requests_pending > 0) {
                        statistics_.requests_pending--;
                    }
                }
                
                // 回调通知
                if (piece_callback_) {
                    piece_callback_(block);
                }
                
                LOG_DEBUG("Received Piece: index=" + std::to_string(block.piece_index) +
                          " begin=" + std::to_string(block.begin) +
                          " size=" + std::to_string(block.data.size()));
            }
            break;
            
        case BtMessageType::Cancel:
            // 对方取消请求
            break;
            
        case BtMessageType::Port:
            // DHT 端口通知
            break;
            
        case BtMessageType::Extended:
            handleExtendedMessage(msg);
            break;
    }
    
    // 通知消息回调
    if (message_callback_) {
        message_callback_(msg);
    }
}

void PeerConnection::sendMessage(const BtMessage& msg) {
    if (!tcp_client_ || !isConnected()) {
        return;
    }
    
    auto data = msg.encode();
    tcp_client_->send(data, [](const asio::error_code& ec, size_t) {
        if (ec) {
            LOG_DEBUG("Failed to send message: " + ec.message());
        }
    });
    
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        statistics_.messages_sent++;
    }
}

void PeerConnection::setState(PeerConnectionState new_state) {
    PeerConnectionState old_state = state_.exchange(new_state);
    
    if (old_state != new_state && state_callback_) {
        state_callback_(new_state);
    }
}

void PeerConnection::reportError(const std::string& error) {
    if (error_callback_) {
        error_callback_(error);
    }
}

void PeerConnection::setExtensionHandshakeCallback(ExtensionHandshakeCallback callback) {
    extension_handshake_callback_ = std::move(callback);
}

void PeerConnection::setMetadataMessageCallback(MetadataMessageCallback callback) {
    metadata_message_callback_ = std::move(callback);
}

void PeerConnection::handleExtendedMessage(const BtMessage& msg) {
    LOG_DEBUG("handleExtendedMessage called");
    
    if (!msg.isExtended()) {
        LOG_WARNING("handleExtendedMessage: message is not Extended type!");
        return;
    }
    
    uint8_t ext_id = msg.extendedId();
    const auto& payload = msg.payload();
    LOG_DEBUG("Extended message: id=" + std::to_string(ext_id) + 
              ", payload_size=" + std::to_string(payload.size()));
    
    if (ext_id == extension::kExtensionHandshakeId) {
        // 扩展握手消息
        auto handshake = MetadataExtension::parseExtensionHandshake(payload);
        if (handshake.has_value()) {
            supports_extension_ = true;
            peer_metadata_ext_id_ = handshake->metadataExtensionId();
            
            LOG_DEBUG("Received extension handshake from " + peer_info_.toString() +
                      ", ut_metadata=" + std::to_string(peer_metadata_ext_id_) +
                      ", metadata_size=" + 
                      (handshake->metadata_size.has_value() ? 
                       std::to_string(handshake->metadata_size.value()) : "none"));
            
            if (extension_handshake_callback_) {
                extension_handshake_callback_(handshake.value());
            }
        } else {
            LOG_WARNING("Failed to parse extension handshake from " + peer_info_.toString());
        }
    } else if (ext_id == 1) {  // 我方的 ut_metadata ID
        // 元数据消息
        auto message = MetadataExtension::parseMetadataMessage(payload);
        if (message.has_value()) {
            LOG_DEBUG("Received metadata message: type=" + 
                      std::to_string(static_cast<int>(message->type)) +
                      ", piece=" + std::to_string(message->piece_index));
            
            if (metadata_message_callback_) {
                metadata_message_callback_(message.value());
            }
        } else {
            LOG_WARNING("Failed to parse metadata message from " + peer_info_.toString());
        }
    } else {
        LOG_DEBUG("Received unknown extension message id=" + std::to_string(ext_id));
    }
}

void PeerConnection::sendExtensionHandshake() {
    if (!isConnected()) {
        LOG_WARNING("Cannot send extension handshake: not connected");
        return;
    }
    
    // 创建扩展握手消息
    auto handshake_data = MetadataExtension::createExtensionHandshake(std::nullopt);
    auto msg = BtMessage::createExtended(extension::kExtensionHandshakeId, handshake_data);
    
    sendMessage(msg);
    LOG_DEBUG("Sent extension handshake to " + peer_info_.toString());
}

} // namespace magnet::protocols


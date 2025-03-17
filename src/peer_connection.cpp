#include "peer_connection.h"
#include <iostream>
#include <iomanip>

namespace bt {

constexpr char PeerConnection::PROTOCOL_NAME[];

PeerConnection::PeerConnection(asio::io_context& io_context, 
                             const Peer& peer,
                             const std::string& info_hash,
                             const std::string& peer_id)
    : io_context_(io_context),
      peer_(peer),
      info_hash_(info_hash),
      peer_id_(peer_id),
      socket_(io_context) {
}

void PeerConnection::connect(ConnectHandler handler) {
    auto self = shared_from_this();
    
    socket_.async_connect(
        peer_.getEndpoint(),
        [this, self, handler](const asio::error_code& error) {
            if (!error) {
                connected_ = true;
                sendHandshake();
                readHandshake(handler);
            } else {
                handler(false);
            }
        });
}

void PeerConnection::disconnect() {
    if (connected_) {
        asio::error_code ec;
        socket_.close(ec);
        connected_ = false;
        handshaked_ = false;
        
        if (disconnect_handler_) {
            disconnect_handler_();
        }
    }
}

void PeerConnection::sendMessage(const Message& message) {
    message_queue_.push(message);
    
    if (!writing_ && connected_) {
        startWrite();
    }
}

void PeerConnection::setMessageHandler(MessageHandler handler) {
    message_handler_ = std::move(handler);
}

void PeerConnection::setDisconnectHandler(DisconnectHandler handler) {
    disconnect_handler_ = std::move(handler);
}

void PeerConnection::sendHandshake() {
    // 构造握手消息
    // <pstrlen><pstr><reserved><info_hash><peer_id>
    handshake_buffer_[0] = PROTOCOL_NAME_LENGTH;
    
    // 复制协议名称
    std::copy(PROTOCOL_NAME, PROTOCOL_NAME + PROTOCOL_NAME_LENGTH, 
              handshake_buffer_.data() + 1);
    
    // 保留字段，全部置为0
    std::fill(handshake_buffer_.data() + 1 + PROTOCOL_NAME_LENGTH, 
              handshake_buffer_.data() + 1 + PROTOCOL_NAME_LENGTH + 8, 0);
    
    // 复制info_hash (20字节)
    std::copy(info_hash_.begin(), info_hash_.end(),
              handshake_buffer_.data() + 1 + PROTOCOL_NAME_LENGTH + 8);
    
    // 复制peer_id (20字节)
    std::copy(peer_id_.begin(), peer_id_.end(),
              handshake_buffer_.data() + 1 + PROTOCOL_NAME_LENGTH + 8 + 20);
    
    auto self = shared_from_this();
    
    asio::async_write(
        socket_,
        asio::buffer(handshake_buffer_),
        [this, self](const asio::error_code& error, size_t /*bytes_transferred*/) {
            if (error) {
                disconnect();
            }
        });
}

void PeerConnection::readHandshake(ConnectHandler handler) {
    auto self = shared_from_this();
    
    asio::async_read(
        socket_,
        asio::buffer(handshake_buffer_),
        [this, self, handler](const asio::error_code& error, size_t /*bytes_transferred*/) {
            if (!error) {
                // 验证握手消息
                if (handshake_buffer_[0] == PROTOCOL_NAME_LENGTH &&
                    std::equal(PROTOCOL_NAME, PROTOCOL_NAME + PROTOCOL_NAME_LENGTH,
                              handshake_buffer_.data() + 1)) {
                    
                    // 验证info_hash
                    std::string received_info_hash(
                        reinterpret_cast<char*>(handshake_buffer_.data() + 1 + PROTOCOL_NAME_LENGTH + 8),
                        20);
                    
                    if (received_info_hash == info_hash_) {
                        handshaked_ = true;
                        handler(true);
                        
                        // 开始接收消息
                        startReadMessage();
                    } else {
                        // info_hash不匹配
                        disconnect();
                        handler(false);
                    }
                } else {
                    // 协议名称不匹配
                    disconnect();
                    handler(false);
                }
            } else {
                disconnect();
                handler(false);
            }
        });
}

void PeerConnection::startReadMessage() {
    if (!connected_ || !handshaked_) {
        return;
    }
    
    readMessageLength();
}

void PeerConnection::readMessageLength() {
    auto self = shared_from_this();
    
    asio::async_read(
        socket_,
        asio::buffer(length_buffer_),
        [this, self](const asio::error_code& error, size_t /*bytes_transferred*/) {
            if (!error) {
                // 解析消息长度 (网络字节序，大端)
                uint32_t length = (static_cast<uint32_t>(length_buffer_[0]) << 24) |
                                  (static_cast<uint32_t>(length_buffer_[1]) << 16) |
                                  (static_cast<uint32_t>(length_buffer_[2]) << 8) |
                                  static_cast<uint32_t>(length_buffer_[3]);
                
                if (length == 0) {
                    // 保持活动(keep-alive)消息，继续读取下一个消息
                    readMessageLength();
                } else {
                    // 读取消息体
                    readMessageBody(length);
                }
            } else {
                disconnect();
            }
        });
}

void PeerConnection::readMessageBody(uint32_t length) {
    auto self = shared_from_this();
    
    // 调整缓冲区大小
    message_buffer_.resize(length);
    
    asio::async_read(
        socket_,
        asio::buffer(message_buffer_),
        [this, self](const asio::error_code& error, size_t /*bytes_transferred*/) {
            if (!error) {
                // 解析消息类型
                MessageType type = static_cast<MessageType>(message_buffer_[0]);
                
                // 消息负载 (不包括类型字段)
                std::vector<uint8_t> payload(message_buffer_.begin() + 1, message_buffer_.end());
                
                // 处理消息
                handleMessage(type, payload);
                
                // 继续读取下一个消息
                readMessageLength();
            } else {
                disconnect();
            }
        });
}

void PeerConnection::handleMessage(MessageType type, const std::vector<uint8_t>& payload) {
    // 更新状态
    switch (type) {
        case MessageType::CHOKE:
            choked_ = true;
            break;
        case MessageType::UNCHOKE:
            choked_ = false;
            break;
        case MessageType::INTERESTED:
            // 对方感兴趣
            break;
        case MessageType::NOT_INTERESTED:
            // 对方不感兴趣
            break;
        default:
            break;
    }
    
    // 调用消息处理器
    if (message_handler_) {
        message_handler_(Message(type, payload));
    }
}

void PeerConnection::startWrite() {
    if (message_queue_.empty()) {
        writing_ = false;
        return;
    }
    
    writing_ = true;
    
    const Message& message = message_queue_.front();
    
    // 消息格式: <length_prefix><message_id><payload>
    // length_prefix是4字节的整数，表示message_id和payload的总长度
    
    uint32_t payload_size = message.payload.size();
    uint32_t message_size = payload_size + 1; // +1 for message_id
    
    // 创建完整消息缓冲区
    std::vector<asio::const_buffer> buffers;
    
    // 转换长度为网络字节序 (大端)
    uint8_t length_prefix[4];
    length_prefix[0] = (message_size >> 24) & 0xFF;
    length_prefix[1] = (message_size >> 16) & 0xFF;
    length_prefix[2] = (message_size >> 8) & 0xFF;
    length_prefix[3] = message_size & 0xFF;
    
    // 消息类型
    uint8_t message_id = static_cast<uint8_t>(message.type);
    
    buffers.push_back(asio::buffer(length_prefix, 4));
    buffers.push_back(asio::buffer(&message_id, 1));
    
    if (!message.payload.empty()) {
        buffers.push_back(asio::buffer(message.payload));
    }
    
    auto self = shared_from_this();
    
    asio::async_write(
        socket_,
        buffers,
        [this, self](const asio::error_code& error, size_t bytes_transferred) {
            onWriteComplete(error, bytes_transferred);
        });
}

void PeerConnection::onWriteComplete(const asio::error_code& error, size_t /*bytes_transferred*/) {
    if (!error) {
        message_queue_.pop();
        
        if (!message_queue_.empty()) {
            startWrite();
        } else {
            writing_ = false;
        }
    } else {
        disconnect();
    }
}

} // namespace bt 
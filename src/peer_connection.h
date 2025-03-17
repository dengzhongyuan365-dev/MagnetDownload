#pragma once

#include <array>
#include <vector>
#include <queue>
#include <memory>
#include <functional>
#include <asio.hpp>
#include "peer.h"

namespace bt {

// BitTorrent消息类型
enum class MessageType : uint8_t {
    CHOKE = 0,
    UNCHOKE = 1,
    INTERESTED = 2,
    NOT_INTERESTED = 3,
    HAVE = 4,
    BITFIELD = 5,
    REQUEST = 6,
    PIECE = 7,
    CANCEL = 8,
    PORT = 9,
    // 扩展消息
    EXTENDED = 20
};

// BitTorrent消息
struct Message {
    MessageType type;
    std::vector<uint8_t> payload;
    
    Message(MessageType t, std::vector<uint8_t> p = {})
        : type(t), payload(std::move(p)) {}
};

class PeerConnection : public std::enable_shared_from_this<PeerConnection> {
public:
    using MessageHandler = std::function<void(const Message&)>;
    using ConnectHandler = std::function<void(bool)>;
    using DisconnectHandler = std::function<void()>;
    
    PeerConnection(asio::io_context& io_context, 
                  const Peer& peer,
                  const std::string& info_hash,
                  const std::string& peer_id);
    
    // 连接到Peer
    void connect(ConnectHandler handler);
    
    // 断开连接
    void disconnect();
    
    // 发送消息
    void sendMessage(const Message& message);
    
    // 设置消息处理器
    void setMessageHandler(MessageHandler handler);
    
    // 设置断开连接处理器
    void setDisconnectHandler(DisconnectHandler handler);
    
    // 获取Peer信息
    const Peer& getPeer() const { return peer_; }
    
    // 是否已连接
    bool isConnected() const { return connected_; }
    
    // 是否已握手
    bool isHandshaked() const { return handshaked_; }
    
    // 是否已被choke
    bool isChoked() const { return choked_; }
    
    // 是否感兴趣
    bool isInterested() const { return interested_; }
    
    // 设置choke状态
    void setChoked(bool choked) { choked_ = choked; }
    
    // 设置感兴趣状态
    void setInterested(bool interested) { interested_ = interested; }
    
private:
    asio::io_context& io_context_;
    Peer peer_;
    std::string info_hash_;
    std::string peer_id_;
    asio::ip::tcp::socket socket_;
    bool connected_ = false;
    bool handshaked_ = false;
    bool choked_ = true;        // 默认被choke
    bool interested_ = false;   // 默认不感兴趣
    
    MessageHandler message_handler_;
    DisconnectHandler disconnect_handler_;
    
    std::queue<Message> message_queue_;
    bool writing_ = false;
    
    // 握手相关数据
    static constexpr uint8_t PROTOCOL_NAME_LENGTH = 19;
    static constexpr char PROTOCOL_NAME[] = "BitTorrent protocol";
    std::array<uint8_t, 68> handshake_buffer_; // 握手消息缓冲区
    
    // 读取消息相关数据
    std::array<uint8_t, 4> length_buffer_;    // 消息长度缓冲区
    std::vector<uint8_t> message_buffer_;     // 消息内容缓冲区
    
    // 发送握手消息
    void sendHandshake();
    
    // 读取握手响应
    void readHandshake(ConnectHandler handler);
    
    // 开始读取消息
    void startReadMessage();
    
    // 读取消息长度
    void readMessageLength();
    
    // 读取消息内容
    void readMessageBody(uint32_t length);
    
    // 处理接收到的消息
    void handleMessage(MessageType type, const std::vector<uint8_t>& payload);
    
    // 启动写入消息
    void startWrite();
    
    // 写入完成回调
    void onWriteComplete(const asio::error_code& error, size_t bytes_transferred);
};

} // namespace bt 
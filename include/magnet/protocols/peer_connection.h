#pragma once

#include "bt_message.h"
#include "magnet_types.h"
#include "../network/tcp_client.h"

#include <asio.hpp>
#include <functional>
#include <memory>
#include <atomic>
#include <mutex>
#include <deque>
#include <chrono>

namespace magnet::protocols {

// ============================================================================
// 连接状态
// ============================================================================

enum class PeerConnectionState {
    Disconnected,   // 未连接
    Connecting,     // TCP 连接中
    Handshaking,    // 握手中
    Connected,      // 已连接（可通信）
    Closing         // 关闭中
};

/**
 * @brief 状态转字符串
 */
inline const char* peerStateToString(PeerConnectionState state) {
    switch (state) {
        case PeerConnectionState::Disconnected: return "Disconnected";
        case PeerConnectionState::Connecting: return "Connecting";
        case PeerConnectionState::Handshaking: return "Handshaking";
        case PeerConnectionState::Connected: return "Connected";
        case PeerConnectionState::Closing: return "Closing";
        default: return "Unknown";
    }
}

// ============================================================================
// Peer 状态（choke/interested）
// ============================================================================

/**
 * @struct PeerState
 * @brief BitTorrent 协议中的 Peer 状态
 */
struct PeerState {
    bool am_choking{true};       // 我阻塞对方（不发送数据给对方）
    bool am_interested{false};   // 我对对方的数据感兴趣
    bool peer_choking{true};     // 对方阻塞我（不发送数据给我）
    bool peer_interested{false}; // 对方对我的数据感兴趣
    
    /**
     * @brief 是否可以请求数据
     * 条件：我感兴趣 且 对方没有阻塞我
     */
    bool canRequest() const {
        return am_interested && !peer_choking;
    }
    
    /**
     * @brief 是否可以发送数据
     * 条件：对方感兴趣 且 我没有阻塞对方
     */
    bool canSend() const {
        return peer_interested && !am_choking;
    }
};

// ============================================================================
// Peer 信息
// ============================================================================

/**
 * @struct PeerInfo
 * @brief Peer 基本信息
 */
struct PeerInfo {
    std::string ip;
    uint16_t port{0};
    std::array<uint8_t, 20> peer_id{};  // 握手后获得
    
    PeerInfo() = default;
    PeerInfo(const std::string& ip_, uint16_t port_) : ip(ip_), port(port_) {}
    
    std::string toString() const {
        return ip + ":" + std::to_string(port);
    }
    
    /**
     * @brief 获取 Peer ID 字符串
     */
    std::string peerIdString() const {
        return std::string(reinterpret_cast<const char*>(peer_id.data()), 20);
    }
};

// ============================================================================
// 统计信息
// ============================================================================

/**
 * @struct PeerStatistics
 * @brief Peer 连接统计信息
 */
struct PeerStatistics {
    size_t bytes_downloaded{0};     // 从该 Peer 下载的字节数
    size_t bytes_uploaded{0};       // 上传给该 Peer 的字节数
    size_t pieces_received{0};      // 收到的数据块数
    size_t pieces_sent{0};          // 发送的数据块数
    size_t requests_pending{0};     // 待处理的请求数
    size_t messages_received{0};    // 收到的消息数
    size_t messages_sent{0};        // 发送的消息数
    
    std::chrono::steady_clock::time_point connect_time;  // 连接时间
    std::chrono::steady_clock::time_point last_activity; // 最后活动时间
    
    /**
     * @brief 连接持续时间
     */
    std::chrono::seconds connectionDuration() const {
        if (connect_time == std::chrono::steady_clock::time_point{}) {
            return std::chrono::seconds{0};
        }
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - connect_time);
    }
    
    void reset() {
        bytes_downloaded = 0;
        bytes_uploaded = 0;
        pieces_received = 0;
        pieces_sent = 0;
        requests_pending = 0;
        messages_received = 0;
        messages_sent = 0;
    }
};

// ============================================================================
// 回调类型
// ============================================================================

/** @brief 连接状态变化回调 */
using PeerStateCallback = std::function<void(PeerConnectionState state)>;

/** @brief 收到消息回调 */
using PeerMessageCallback = std::function<void(const BtMessage& message)>;

/** @brief 收到数据块回调 */
using PeerPieceCallback = std::function<void(const PieceBlock& block)>;

/** @brief 错误回调 */
using PeerErrorCallback = std::function<void(const std::string& error)>;

/** @brief 连接完成回调 */
using PeerConnectCallback = std::function<void(bool success)>;

// ============================================================================
// PeerConnection 类
// ============================================================================

/**
 * @class PeerConnection
 * @brief 单个 BitTorrent Peer 连接管理
 * 
 * 管理与一个 Peer 的完整生命周期：
 * - TCP 连接
 * - BitTorrent 握手
 * - 消息收发
 * - 状态管理
 * 
 * 使用示例：
 * @code
 * auto peer = std::make_shared<PeerConnection>(io_context, info_hash, "-MT0001-xxxx");
 * 
 * peer->setPieceCallback([](const PieceBlock& block) {
 *     // 处理收到的数据块
 * });
 * 
 * peer->connect({"192.168.1.100", 6881}, [peer](bool success) {
 *     if (success) {
 *         peer->sendInterested();
 *     }
 * });
 * @endcode
 */
class PeerConnection : public std::enable_shared_from_this<PeerConnection> {
public:
    // ========================================================================
    // 构造和析构
    // ========================================================================
    
    /**
     * @brief 构造函数
     * @param io_context 事件循环
     * @param info_hash 文件的 info_hash
     * @param my_peer_id 本客户端的 peer_id（20 字节）
     */
    PeerConnection(asio::io_context& io_context,
                   const InfoHash& info_hash,
                   const std::string& my_peer_id);
    
    /**
     * @brief 析构函数
     */
    ~PeerConnection();
    
    // 禁止拷贝和移动
    PeerConnection(const PeerConnection&) = delete;
    PeerConnection& operator=(const PeerConnection&) = delete;
    
    // ========================================================================
    // 连接管理
    // ========================================================================
    
    /**
     * @brief 连接到 Peer
     * @param endpoint Peer 地址
     * @param callback 连接完成回调（成功/失败）
     */
    void connect(const network::TcpEndpoint& endpoint, 
                 PeerConnectCallback callback = nullptr);
    
    /**
     * @brief 断开连接
     */
    void disconnect();
    
    // ========================================================================
    // 发送消息
    // ========================================================================
    
    /** @brief 发送 Interested 消息（表示对对方数据感兴趣） */
    void sendInterested();
    
    /** @brief 发送 NotInterested 消息 */
    void sendNotInterested();
    
    /** @brief 发送 Choke 消息（阻塞对方） */
    void sendChoke();
    
    /** @brief 发送 Unchoke 消息（解除阻塞） */
    void sendUnchoke();
    
    /** 
     * @brief 发送 Have 消息（通知拥有某个分片）
     * @param piece_index 分片索引
     */
    void sendHave(uint32_t piece_index);
    
    /**
     * @brief 发送 Bitfield 消息（发送分片位图）
     * @param bitfield 分片位图
     */
    void sendBitfield(const std::vector<bool>& bitfield);
    
    /**
     * @brief 请求数据块
     * @param block 块信息
     */
    void requestBlock(const BlockInfo& block);
    
    /**
     * @brief 取消数据块请求
     * @param block 块信息
     */
    void cancelBlock(const BlockInfo& block);
    
    /**
     * @brief 发送数据块
     * @param block 数据块
     */
    void sendPiece(const PieceBlock& block);
    
    /**
     * @brief 发送 KeepAlive 消息
     */
    void sendKeepAlive();
    
    // ========================================================================
    // 状态查询
    // ========================================================================
    
    /** @brief 获取连接状态 */
    PeerConnectionState state() const { return state_.load(); }
    
    /** @brief 是否已连接 */
    bool isConnected() const { return state_.load() == PeerConnectionState::Connected; }
    
    /** @brief 获取 Peer 状态 */
    PeerState peerState() const;
    
    /** @brief 获取 Peer 信息 */
    const PeerInfo& peerInfo() const { return peer_info_; }
    
    /** @brief 获取 Peer 的分片位图 */
    std::vector<bool> peerBitfield() const;
    
    /** 
     * @brief 检查 Peer 是否拥有某个分片
     * @param index 分片索引
     */
    bool hasPiece(uint32_t index) const;
    
    /** @brief 获取统计信息 */
    PeerStatistics getStatistics() const;
    
    // ========================================================================
    // 回调设置
    // ========================================================================
    
    /** @brief 设置状态变化回调 */
    void setStateCallback(PeerStateCallback callback);
    
    /** @brief 设置消息回调 */
    void setMessageCallback(PeerMessageCallback callback);
    
    /** @brief 设置数据块回调 */
    void setPieceCallback(PeerPieceCallback callback);
    
    /** @brief 设置错误回调 */
    void setErrorCallback(PeerErrorCallback callback);

private:
    // ========================================================================
    // 内部方法
    // ========================================================================
    
    /** @brief TCP 连接完成处理 */
    void onConnected(const asio::error_code& ec);
    
    /** @brief 收到数据处理 */
    void onReceive(const asio::error_code& ec, const std::vector<uint8_t>& data);
    
    /** @brief TCP 断线处理 */
    void onDisconnect(const asio::error_code& ec);
    
    /** @brief 发送握手 */
    void sendHandshake();
    
    /** @brief 处理握手响应 */
    bool handleHandshake();
    
    /** @brief 处理普通消息 */
    void processMessages();
    
    /** @brief 处理单个消息 */
    void handleMessage(const BtMessage& msg);
    
    /** @brief 发送消息 */
    void sendMessage(const BtMessage& msg);
    
    /** @brief 更新状态并通知 */
    void setState(PeerConnectionState new_state);
    
    /** @brief 报告错误 */
    void reportError(const std::string& error);

private:
    asio::io_context& io_context_;
    std::shared_ptr<network::TcpClient> tcp_client_;
    
    // 身份信息
    InfoHash info_hash_;
    std::string my_peer_id_;
    PeerInfo peer_info_;
    
    // 状态
    std::atomic<PeerConnectionState> state_{PeerConnectionState::Disconnected};
    PeerState peer_state_;
    mutable std::mutex state_mutex_;
    
    // 位图
    std::vector<bool> peer_bitfield_;
    mutable std::mutex bitfield_mutex_;
    
    // 接收缓冲区
    std::vector<uint8_t> receive_buffer_;
    bool handshake_received_{false};
    
    // 待处理请求
    std::deque<BlockInfo> pending_requests_;
    mutable std::mutex requests_mutex_;
    
    // 统计
    mutable std::mutex stats_mutex_;
    PeerStatistics statistics_;
    
    // 回调
    PeerConnectCallback connect_callback_;
    PeerStateCallback state_callback_;
    PeerMessageCallback message_callback_;
    PeerPieceCallback piece_callback_;
    PeerErrorCallback error_callback_;
};

} // namespace magnet::protocols


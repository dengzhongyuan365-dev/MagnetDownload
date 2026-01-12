#pragma once

#include "network_types.h"
#include <asio.hpp>
#include <functional>
#include <memory>
#include <atomic>
#include <mutex>
#include <vector>
#include <chrono>
#include <queue>

namespace magnet::network {

// ============================================================================
// TCP 连接状态
// ============================================================================

enum class TcpConnectionState {
    Disconnected,   // 未连接
    Connecting,     // 连接中
    Connected,      // 已连接
    Closing         // 关闭中
};

/**
 * @brief 将状态转换为字符串
 */
inline const char* tcpStateToString(TcpConnectionState state) {
    switch (state) {
        case TcpConnectionState::Disconnected: return "Disconnected";
        case TcpConnectionState::Connecting: return "Connecting";
        case TcpConnectionState::Connected: return "Connected";
        case TcpConnectionState::Closing: return "Closing";
        default: return "Unknown";
    }
}

// ============================================================================
// TCP 统计信息
// ============================================================================

struct TcpStatistics {
    size_t bytes_sent{0};           // 发送的总字节数
    size_t bytes_received{0};       // 接收的总字节数
    size_t messages_sent{0};        // 发送次数
    size_t messages_received{0};    // 接收次数
    size_t connect_attempts{0};     // 连接尝试次数
    size_t connect_failures{0};     // 连接失败次数
    size_t send_errors{0};          // 发送错误次数
    size_t receive_errors{0};       // 接收错误次数
    
    std::chrono::steady_clock::time_point connect_time;  // 连接建立时间
    
    /**
     * @brief 重置统计信息
     */
    void reset() {
        bytes_sent = 0;
        bytes_received = 0;
        messages_sent = 0;
        messages_received = 0;
        connect_attempts = 0;
        connect_failures = 0;
        send_errors = 0;
        receive_errors = 0;
    }
    
    /**
     * @brief 获取连接持续时间
     */
    std::chrono::seconds connectionDuration() const {
        if (connect_time == std::chrono::steady_clock::time_point{}) {
            return std::chrono::seconds{0};
        }
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - connect_time);
    }
};

// ============================================================================
// TcpClient 类
// ============================================================================

/**
 * @class TcpClient
 * @brief 异步 TCP 客户端
 * 
 * 提供异步的 TCP 通信能力，用于与 BitTorrent Peer 进行数据传输。
 * 
 * 特性：
 * - 异步连接、发送、接收
 * - 连接状态管理
 * - 自动断线检测
 * - 统计信息收集
 * - 线程安全
 * 
 * 使用示例：
 * @code
 * asio::io_context io_context;
 * auto tcp = std::make_shared<TcpClient>(io_context);
 * 
 * tcp->connect({"192.168.1.100", 6881}, [tcp](const asio::error_code& ec) {
 *     if (!ec) {
 *         tcp->startReceive([](const asio::error_code& ec, 
 *                              const std::vector<uint8_t>& data) {
 *             // 处理接收到的数据
 *         });
 *         
 *         tcp->send({0x13, 'B', 'i', 't', 'T', 'o', 'r', 'r', 'e', 'n', 't'});
 *     }
 * });
 * 
 * io_context.run();
 * @endcode
 */
class TcpClient : public std::enable_shared_from_this<TcpClient> {
public:
    // ========================================================================
    // 类型定义
    // ========================================================================
    
    /** @brief 连接完成回调 */
    using ConnectCallback = std::function<void(const asio::error_code& ec)>;
    
    /** @brief 发送完成回调 */
    using SendCallback = std::function<void(const asio::error_code& ec, size_t bytes_sent)>;
    
    /** @brief 接收数据回调 */
    using ReceiveCallback = std::function<void(const asio::error_code& ec, 
                                               const std::vector<uint8_t>& data)>;
    
    /** @brief 连接断开回调 */
    using DisconnectCallback = std::function<void(const asio::error_code& ec)>;

    // ========================================================================
    // 构造和析构
    // ========================================================================
    
    /**
     * @brief 构造函数
     * @param io_context 事件循环
     */
    explicit TcpClient(asio::io_context& io_context);
    
    /**
     * @brief 析构函数
     * 
     * 自动关闭连接并清理资源
     */
    ~TcpClient();
    
    // 禁止拷贝和移动
    TcpClient(const TcpClient&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;
    TcpClient(TcpClient&&) = delete;
    TcpClient& operator=(TcpClient&&) = delete;

    // ========================================================================
    // 连接管理
    // ========================================================================
    
    /**
     * @brief 异步连接到远程端点
     * @param endpoint 目标地址（IP + 端口）
     * @param callback 连接完成回调
     * @param timeout 连接超时时间（0 = 无超时）
     * 
     * 特性：
     * - 异步执行，不阻塞调用线程
     * - 支持域名解析
     * - 可选超时设置
     * 
     * 注意：
     * - 只能在 Disconnected 状态下调用
     * - 回调在 io_context 线程中执行
     */
    void connect(const TcpEndpoint& endpoint, 
                 ConnectCallback callback,
                 std::chrono::milliseconds timeout = std::chrono::milliseconds{0});
    
    /**
     * @brief 关闭连接
     * 
     * 执行操作：
     * - 取消所有待处理的异步操作
     * - 关闭 socket
     * - 状态变为 Disconnected
     * 
     * 注意：
     * - 可以在任何状态下调用
     * - 不会触发断线回调
     */
    void close();
    
    /**
     * @brief 设置断线回调
     * @param callback 连接断开时调用（对端关闭或网络错误）
     * 
     * 注意：主动调用 close() 不会触发此回调
     */
    void setDisconnectCallback(DisconnectCallback callback);

    // ========================================================================
    // 数据传输
    // ========================================================================
    
    /**
     * @brief 异步发送数据
     * @param data 要发送的数据
     * @param callback 发送完成回调（可选）
     * 
     * 特性：
     * - 异步执行，不阻塞
     * - data 会被拷贝，调用后可立即释放
     * - 保证数据完整发送（使用 async_write）
     * 
     * 注意：
     * - 只能在 Connected 状态下调用
     * - 多次调用会按顺序发送
     */
    void send(const std::vector<uint8_t>& data, SendCallback callback = nullptr);
    
    /**
     * @brief 开始接收数据
     * @param callback 每次收到数据时调用
     * 
     * 特性：
     * - 持续接收，直到调用 stopReceive() 或连接断开
     * - 使用 async_read_some，每次返回可用数据
     * 
     * 注意：
     * - 只能在 Connected 状态下调用
     * - 重复调用会替换之前的回调
     */
    void startReceive(ReceiveCallback callback);
    
    /**
     * @brief 停止接收数据
     * 
     * 停止接收但不关闭连接，仍可发送数据
     */
    void stopReceive();

    // ========================================================================
    // 状态查询
    // ========================================================================
    
    /**
     * @brief 获取当前连接状态
     */
    TcpConnectionState state() const { return state_.load(); }
    
    /**
     * @brief 检查是否已连接
     */
    bool isConnected() const { return state_.load() == TcpConnectionState::Connected; }
    
    /**
     * @brief 获取远程端点
     */
    TcpEndpoint remoteEndpoint() const;
    
    /**
     * @brief 获取本地端点
     */
    TcpEndpoint localEndpoint() const;

    // ========================================================================
    // 统计信息
    // ========================================================================
    
    /**
     * @brief 获取统计信息
     */
    TcpStatistics getStatistics() const;
    
    /**
     * @brief 重置统计信息
     */
    void resetStatistics();

private:
    // ========================================================================
    // 内部方法
    // ========================================================================
    
    /**
     * @brief 执行连接操作
     */
    void doConnect(const asio::ip::tcp::endpoint& endpoint, ConnectCallback callback);
    
    /**
     * @brief 执行接收操作
     */
    void doReceive();
    
    /**
     * @brief 处理连接结果
     */
    void handleConnect(const asio::error_code& ec, ConnectCallback callback);
    
    /**
     * @brief 处理接收结果
     */
    void handleReceive(const asio::error_code& ec, size_t bytes_received);
    
    /**
     * @brief 处理发送结果
     */
    void handleSend(const asio::error_code& ec, size_t bytes_sent, 
                    std::shared_ptr<std::vector<uint8_t>> buffer, SendCallback callback);
    
    /**
     * @brief 处理断线
     */
    void handleDisconnect(const asio::error_code& ec);
    
    /**
     * @brief 解析端点地址
     */
    asio::ip::tcp::endpoint resolveEndpoint(const TcpEndpoint& endpoint);
    
    /**
     * @brief 更新发送统计
     */
    void updateSendStats(size_t bytes, bool success);
    
    /**
     * @brief 更新接收统计
     */
    void updateReceiveStats(size_t bytes, bool success);
    
    /**
     * @brief 配置 socket 选项
     */
    void configureSocket();

private:
    asio::io_context& io_context_;
    asio::ip::tcp::socket socket_;
    asio::steady_timer connect_timer_;
    
    std::atomic<TcpConnectionState> state_{TcpConnectionState::Disconnected};
    std::atomic<bool> receiving_{false};
    
    TcpEndpoint remote_endpoint_;
    
    // 接收缓冲区
    static constexpr size_t kReceiveBufferSize = 65536;  // 64KB
    std::vector<uint8_t> receive_buffer_;
    
    // 回调
    ReceiveCallback receive_callback_;
    DisconnectCallback disconnect_callback_;
    
    // 统计信息
    mutable std::mutex stats_mutex_;
    TcpStatistics statistics_;
};

} // namespace magnet::network


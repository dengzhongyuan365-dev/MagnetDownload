#include "magnet/network/tcp_client.h"
#include "magnet/utils/logger.h"

namespace magnet::network {

// 日志宏
#define LOG_DEBUG(msg) magnet::utils::Logger::instance().debug(msg)
#define LOG_INFO(msg) magnet::utils::Logger::instance().info(msg)
#define LOG_WARNING(msg) magnet::utils::Logger::instance().warn(msg)
#define LOG_ERROR(msg) magnet::utils::Logger::instance().error(msg)

// ============================================================================
// 构造函数和析构函数
// ============================================================================

TcpClient::TcpClient(asio::io_context& io_context)
    : io_context_(io_context)
    , socket_(io_context)
    , connect_timer_(io_context)
    , receive_buffer_(kReceiveBufferSize)
{
    LOG_DEBUG("TcpClient created");
}

TcpClient::~TcpClient() {
    close();
    LOG_DEBUG("TcpClient destroyed");
}

// ============================================================================
// 连接管理
// ============================================================================

void TcpClient::connect(const TcpEndpoint& endpoint, 
                        ConnectCallback callback,
                        std::chrono::milliseconds timeout) {
    // 检查状态
    TcpConnectionState expected = TcpConnectionState::Disconnected;
    if (!state_.compare_exchange_strong(expected, TcpConnectionState::Connecting)) {
        LOG_WARNING("TcpClient::connect called in invalid state: " + 
                    std::string(tcpStateToString(expected)));
        if (callback) {
            asio::post(io_context_, [callback]() {
                callback(asio::error::already_connected);
            });
        }
        return;
    }
    
    remote_endpoint_ = endpoint;
    
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        statistics_.connect_attempts++;
    }
    
    LOG_INFO("Connecting to " + endpoint.ip + ":" + std::to_string(endpoint.port));
    
    // 解析端点
    asio::ip::tcp::endpoint resolved;
    try {
        resolved = resolveEndpoint(endpoint);
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to resolve endpoint: " + std::string(e.what()));
        state_.store(TcpConnectionState::Disconnected);
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            statistics_.connect_failures++;
        }
        if (callback) {
            asio::post(io_context_, [callback]() {
                callback(asio::error::host_not_found);
            });
        }
        return;
    }
    
    // 设置超时
    if (timeout.count() > 0) {
        auto self = shared_from_this();
        connect_timer_.expires_after(timeout);
        connect_timer_.async_wait([self](const asio::error_code& ec) {
            if (!ec) {
                // 超时，取消连接
                LOG_WARNING("Connection timeout");
                asio::error_code ignored;
                self->socket_.cancel(ignored);
            }
        });
    }
    
    // 执行连接
    doConnect(resolved, std::move(callback));
}

void TcpClient::doConnect(const asio::ip::tcp::endpoint& endpoint, ConnectCallback callback) {
    auto self = shared_from_this();
    
    socket_.async_connect(endpoint, 
        [self, callback = std::move(callback)](const asio::error_code& ec) {
            self->handleConnect(ec, callback);
        }
    );
}

void TcpClient::handleConnect(const asio::error_code& ec, ConnectCallback callback) {
    // 取消超时定时器
    connect_timer_.cancel();
    
    if (ec) {
        LOG_ERROR("Connection failed: " + ec.message());
        state_.store(TcpConnectionState::Disconnected);
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            statistics_.connect_failures++;
        }
        if (callback) {
            callback(ec);
        }
        return;
    }
    
    // 连接成功
    state_.store(TcpConnectionState::Connected);
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        statistics_.connect_time = std::chrono::steady_clock::now();
    }
    
    // 配置 socket 选项
    configureSocket();
    
    LOG_INFO("Connected to " + remote_endpoint_.ip + ":" + 
             std::to_string(remote_endpoint_.port));
    
    if (callback) {
        callback(ec);
    }
}

void TcpClient::close() {
    TcpConnectionState current = state_.load();
    if (current == TcpConnectionState::Disconnected) {
        return;
    }
    
    state_.store(TcpConnectionState::Closing);
    receiving_.store(false);
    
    // 取消定时器
    connect_timer_.cancel();
    
    // 关闭 socket
    asio::error_code ignored;
    if (socket_.is_open()) {
        socket_.cancel(ignored);
        socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
        socket_.close(ignored);
    }
    
    state_.store(TcpConnectionState::Disconnected);
    LOG_INFO("Connection closed");
}

void TcpClient::setDisconnectCallback(DisconnectCallback callback) {
    disconnect_callback_ = std::move(callback);
}

// ============================================================================
// 数据传输
// ============================================================================

void TcpClient::send(const std::vector<uint8_t>& data, SendCallback callback) {
    if (state_.load() != TcpConnectionState::Connected) {
        LOG_WARNING("TcpClient::send called while not connected");
        if (callback) {
            asio::post(io_context_, [callback]() {
                callback(asio::error::not_connected, 0);
            });
        }
        return;
    }
    
    // 拷贝数据到 shared_ptr，确保在异步操作期间数据有效
    auto buffer = std::make_shared<std::vector<uint8_t>>(data);
    auto self = shared_from_this();
    
    asio::async_write(socket_, asio::buffer(*buffer),
        [self, buffer, callback](const asio::error_code& ec, size_t bytes_sent) {
            self->handleSend(ec, bytes_sent, buffer, callback);
        }
    );
}

void TcpClient::handleSend(const asio::error_code& ec, size_t bytes_sent,
                           std::shared_ptr<std::vector<uint8_t>> /*buffer*/, 
                           SendCallback callback) {
    if (ec) {
        LOG_DEBUG("Send failed: " + ec.message());
        updateSendStats(0, false);
        
        // 检查是否是连接错误
        if (ec == asio::error::connection_reset ||
            ec == asio::error::broken_pipe ||
            ec == asio::error::eof) {
            handleDisconnect(ec);
        }
    } else {
        updateSendStats(bytes_sent, true);
    }
    
    if (callback) {
        callback(ec, bytes_sent);
    }
}

void TcpClient::startReceive(ReceiveCallback callback) {
    if (state_.load() != TcpConnectionState::Connected) {
        LOG_WARNING("TcpClient::startReceive called while not connected");
        return;
    }
    
    receive_callback_ = std::move(callback);
    receiving_.store(true);
    doReceive();
}

void TcpClient::stopReceive() {
    receiving_.store(false);
    receive_callback_ = nullptr;
}

void TcpClient::doReceive() {
    if (!receiving_.load() || state_.load() != TcpConnectionState::Connected) {
        return;
    }
    
    auto self = shared_from_this();
    socket_.async_read_some(asio::buffer(receive_buffer_),
        [self](const asio::error_code& ec, size_t bytes_received) {
            self->handleReceive(ec, bytes_received);
        }
    );
}

void TcpClient::handleReceive(const asio::error_code& ec, size_t bytes_received) {
    if (ec == asio::error::operation_aborted) {
        // 操作被取消（通常是主动关闭），忽略
        return;
    }
    
    if (ec == asio::error::eof || 
        ec == asio::error::connection_reset ||
        ec == asio::error::connection_aborted) {
        // 连接断开
        LOG_INFO("Connection closed by peer: " + ec.message());
        handleDisconnect(ec);
        return;
    }
    
    if (ec) {
        LOG_WARNING("Receive error: " + ec.message());
        updateReceiveStats(0, false);
        
        // 通知回调
        if (receive_callback_) {
            receive_callback_(ec, {});
        }
        
        // 继续接收（非致命错误）
        if (receiving_.load() && state_.load() == TcpConnectionState::Connected) {
            doReceive();
        }
        return;
    }
    
    // 成功接收
    updateReceiveStats(bytes_received, true);
    
    // 构造数据并调用回调
    if (receive_callback_) {
        std::vector<uint8_t> data(receive_buffer_.begin(), 
                                   receive_buffer_.begin() + bytes_received);
        receive_callback_(ec, data);
    }
    
    // 继续接收
    if (receiving_.load() && state_.load() == TcpConnectionState::Connected) {
        doReceive();
    }
}

void TcpClient::handleDisconnect(const asio::error_code& ec) {
    TcpConnectionState expected = TcpConnectionState::Connected;
    if (!state_.compare_exchange_strong(expected, TcpConnectionState::Disconnected)) {
        // 已经不是 Connected 状态，可能已经在关闭中
        return;
    }
    
    receiving_.store(false);
    
    // 关闭 socket
    asio::error_code ignored;
    if (socket_.is_open()) {
        socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
        socket_.close(ignored);
    }
    
    // 调用断线回调
    if (disconnect_callback_) {
        disconnect_callback_(ec);
    }
}

// ============================================================================
// 状态查询
// ============================================================================

TcpEndpoint TcpClient::remoteEndpoint() const {
    return remote_endpoint_;
}

TcpEndpoint TcpClient::localEndpoint() const {
    if (!socket_.is_open()) {
        return TcpEndpoint{};
    }
    
    try {
        auto ep = socket_.local_endpoint();
        return TcpEndpoint{ep.address().to_string(), ep.port()};
    } catch (...) {
        return TcpEndpoint{};
    }
}

// ============================================================================
// 统计信息
// ============================================================================

TcpStatistics TcpClient::getStatistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return statistics_;
}

void TcpClient::resetStatistics() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    statistics_.reset();
}

// ============================================================================
// 内部方法
// ============================================================================

asio::ip::tcp::endpoint TcpClient::resolveEndpoint(const TcpEndpoint& endpoint) {
    // 尝试直接解析为 IP 地址
    asio::error_code ec;
    auto addr = asio::ip::make_address(endpoint.ip, ec);
    
    if (!ec) {
        // 是有效的 IP 地址
        return asio::ip::tcp::endpoint(addr, endpoint.port);
    }
    
    // 需要 DNS 解析
    asio::ip::tcp::resolver resolver(io_context_);
    auto results = resolver.resolve(endpoint.ip, std::to_string(endpoint.port));
    
    if (results.empty()) {
        throw std::runtime_error("DNS resolution failed for: " + endpoint.ip);
    }
    
    return *results.begin();
}

void TcpClient::updateSendStats(size_t bytes, bool success) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    if (success) {
        statistics_.bytes_sent += bytes;
        statistics_.messages_sent++;
    } else {
        statistics_.send_errors++;
    }
}

void TcpClient::updateReceiveStats(size_t bytes, bool success) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    if (success) {
        statistics_.bytes_received += bytes;
        statistics_.messages_received++;
    } else {
        statistics_.receive_errors++;
    }
}

void TcpClient::configureSocket() {
    asio::error_code ec;
    
    // 禁用 Nagle 算法，减少延迟
    socket_.set_option(asio::ip::tcp::no_delay(true), ec);
    if (ec) {
        LOG_WARNING("Failed to set TCP_NODELAY: " + ec.message());
    }
    
    // 启用 Keep-Alive
    socket_.set_option(asio::socket_base::keep_alive(true), ec);
    if (ec) {
        LOG_WARNING("Failed to set SO_KEEPALIVE: " + ec.message());
    }
    
    // 设置接收缓冲区大小（256KB 提高吞吐量）
    socket_.set_option(asio::socket_base::receive_buffer_size(262144), ec);
    if (ec) {
        LOG_WARNING("Failed to set receive buffer size: " + ec.message());
    }
    
    // 设置发送缓冲区大小（256KB）
    socket_.set_option(asio::socket_base::send_buffer_size(262144), ec);
    if (ec) {
        LOG_WARNING("Failed to set send buffer size: " + ec.message());
    }
}

} // namespace magnet::network


#pragma once

#include "interfaces.h"
#include <asio.hpp>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace bt {

/**
 * @brief 基于Asio的端点实现
 */
class AsioEndpoint : public Endpoint {
public:
    /**
     * @brief 从IP和端口构造端点
     */
    AsioEndpoint(std::string ip, uint16_t port);
    
    /**
     * @brief 从Asio端点构造
     */
    explicit AsioEndpoint(const asio::ip::tcp::endpoint& endpoint);
    explicit AsioEndpoint(const asio::ip::udp::endpoint& endpoint);
    
    /**
     * @brief 获取IP地址
     */
    std::string getIP() const override;
    
    /**
     * @brief 获取端口
     */
    uint16_t getPort() const override;
    
    /**
     * @brief 比较端点是否相等
     */
    bool operator==(const Endpoint& other) const override;
    
    /**
     * @brief 获取TCP端点
     */
    asio::ip::tcp::endpoint getTCPEndpoint() const;
    
    /**
     * @brief 获取UDP端点
     */
    asio::ip::udp::endpoint getUDPEndpoint() const;
    
private:
    std::string ip_;
    uint16_t port_;
};

/**
 * @brief Asio TCP连接实现
 */
class AsioTCPConnection : public ITCPConnection {
public:
    /**
     * @brief 从套接字构造连接
     * 
     * @param socket 已连接的套接字
     */
    explicit AsioTCPConnection(asio::ip::tcp::socket socket);
    
    /**
     * @brief 析构函数
     */
    ~AsioTCPConnection() override;
    
    /**
     * @brief 发送数据
     * 
     * @param data 要发送的数据
     */
    void send(ByteSpan data) override;
    
    /**
     * @brief 注册数据处理函数
     * 
     * @param handler 处理函数
     */
    void registerDataHandler(std::function<void(ByteSpan)> handler) override;
    
    /**
     * @brief 注册连接关闭处理函数
     * 
     * @param handler 处理函数
     */
    void registerCloseHandler(std::function<void()> handler) override;
    
    /**
     * @brief 关闭连接
     */
    void close() override;
    
    /**
     * @brief 获取远程端点
     * 
     * @return 远程端点
     */
    Endpoint getRemoteEndpoint() const override;
    
    /**
     * @brief 检查连接是否建立
     * 
     * @return 连接状态
     */
    bool isConnected() const override;
    
private:
    /**
     * @brief 开始异步读取
     */
    void startRead();
    
    /**
     * @brief 处理读取完成
     * 
     * @param error 错误码
     * @param bytesTransferred 传输的字节数
     */
    void handleRead(const asio::error_code& error, std::size_t bytesTransferred);
    
    /**
     * @brief 处理写入完成
     * 
     * @param error 错误码
     * @param bytesTransferred 传输的字节数
     */
    void handleWrite(const asio::error_code& error, std::size_t bytesTransferred);

private:
    asio::ip::tcp::socket socket_;
    bool connected_;
    std::function<void(ByteSpan)> dataHandler_;
    std::function<void()> closeHandler_;
    std::vector<std::byte> readBuffer_;
    std::queue<std::vector<std::byte>> writeQueue_;
    bool writing_;
    mutable std::mutex mutex_;
};

/**
 * @brief 网络管理器实现
 */
class NetworkManager : public INetworkManager {
public:
    /**
     * @brief 构造函数
     */
    NetworkManager();
    
    /**
     * @brief 析构函数
     */
    ~NetworkManager() override;
    
    // 禁用拷贝和赋值
    NetworkManager(const NetworkManager&) = delete;
    NetworkManager& operator=(const NetworkManager&) = delete;
    
    /**
     * @brief 发送UDP数据
     * 
     * @param data 数据
     * @param endpoint 目标端点
     */
    void sendUDP(ByteSpan data, const Endpoint& endpoint) override;
    
    /**
     * @brief 监听UDP端口
     * 
     * @param port 端口
     */
    void listenUDP(uint16_t port) override;
    
    /**
     * @brief 注册UDP数据处理函数
     * 
     * @param handler 处理函数
     */
    void registerUDPDataHandler(std::function<void(ByteSpan, const Endpoint&)> handler) override;
    
    /**
     * @brief 连接到TCP端点
     * 
     * @param endpoint 目标端点
     * @return 连接对象
     */
    std::shared_ptr<ITCPConnection> connectTCP(const Endpoint& endpoint) override;
    
    /**
     * @brief 监听TCP端口
     * 
     * @param port 端口
     */
    void listenTCP(uint16_t port) override;
    
    /**
     * @brief 注册TCP连接处理函数
     * 
     * @param handler 处理函数
     */
    void registerTCPConnectionHandler(std::function<void(std::shared_ptr<ITCPConnection>)> handler) override;
    
    /**
     * @brief 停止网络服务
     */
    void stop();
    
private:
    /**
     * @brief 开始接收UDP数据
     */
    void startReceiveUDP();
    
    /**
     * @brief 处理UDP数据接收
     * 
     * @param error 错误码
     * @param bytesTransferred 传输的字节数
     */
    void handleReceiveUDP(const asio::error_code& error, std::size_t bytesTransferred);
    
    /**
     * @brief 开始接受TCP连接
     */
    void startAcceptTCP();
    
    /**
     * @brief 处理TCP连接接受
     * 
     * @param connection 新连接
     * @param error 错误码
     */
    void handleAcceptTCP(std::shared_ptr<AsioTCPConnection> connection, const asio::error_code& error);
    
    /**
     * @brief 运行IO服务
     */
    void runIOContext();

private:
    asio::io_context ioContext_;
    std::unique_ptr<asio::io_context::work> workGuard_;
    std::thread ioThread_;
    
    std::unique_ptr<asio::ip::udp::socket> udpSocket_;
    std::unique_ptr<asio::ip::tcp::acceptor> tcpAcceptor_;
    
    std::vector<std::byte> udpReceiveBuffer_;
    asio::ip::udp::endpoint udpSenderEndpoint_;
    
    std::function<void(ByteSpan, const Endpoint&)> udpDataHandler_;
    std::function<void(std::shared_ptr<ITCPConnection>)> tcpConnectionHandler_;
    
    std::mutex udpMutex_;
    std::mutex tcpMutex_;
};

} // namespace bt 
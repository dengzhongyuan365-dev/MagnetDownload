#pragma once

#include "network_types.h"
#include <asio.hpp>
#include <functional>
#include <memory>
#include <atomic>
#include <mutex>
#include <array>

namespace magnet::network {
/*
* @brief UDP 客户端
提供异步的UDP通信功能
包括：
- 发送UDP数据包到指定的地址
- 接受UDP数据包并触发回调
- 统计发送、接受的字节数和消息数

*/

class UdpClient: public std::enable_shared_from_this<UdpClient> {
    public:
        /*
            message 接收到的消息，包含数据和来源地址
        */
        using ReceiveCallback = std::function<void(const UdpMessage& message)>;
        
        using SendCallback = std::function<void(const asio::error_code&ec, size_t bytes_sent)>;

        explicit UdpClient(asio::io_context& io_context, uint16_t local_port = 0);

        ~UdpClient();

        // 禁止拷贝和移动（因为持有 io_context 引用）
        UdpClient(const UdpClient&) = delete;
        UdpClient& operator=(const UdpClient&) = delete;
        UdpClient(UdpClient&&) = delete;
        UdpClient& operator=(UdpClient&&) = delete;

        /*
        * @brief 发送 UDP 数据包
        * @param endpoint 目标地址（IP + 端口）
        * @param data 要发送的数据（字节数组）
        * @param callback 发送完成回调（可选，nullptr 表示不需要回调）
        * 特性：
        * - 异步发送，不阻塞调用线程
        * - 线程安全，可以从任何线程调用
        * - 支持域名解析（如果 endpoint.ip 是域名）
        * - 自动更新统计信息
        * 注意：
        * - 如果 socket 未打开，会自动打开
        * - 回调在 io_context 线程中执行
        * - data 会被拷贝，调用后可以立即释放
        */
        void send(const UdpEndpoint& endpoint, const std::vector<uint8_t>& data, SendCallback callback = nullptr);

        // @brief 开始接收 UDP 数据包
        // @param callback 接收回调（每次收到数据时调用）
        // @throw std::runtime_error 如果已经在接收中
        /* 特性：
        * - 持续接收，每次收到数据后自动继续接收下一个
        * - 回调在 io_context 线程中执行
        * - 自动更新统计信息        
        * 注意：
        * - 只能调用一次，重复调用会抛出异常
        * - 要停止接收需要调用 stop_receive()
        * - 接收缓冲区大小为 64KB，足够大多数 UDP 数据包
        */
        void startReceive(ReceiveCallback callback);

        /*
        * @brief 停止接收 UDP 数据包
        * 特性：
        * - 停止后可以再次调用 start_receive()
        * - 不会关闭 socket，仍可以发送数据
        * - 会取消当前的接收操作
        * 注意：
        * - 如果没有在接收，调用此方法无效果
        */
        void stopReceive();

        /*
        * @brief 关闭 UDP 客户端            
        * 执行操作：
        * - 停止接收
        * - 关闭 socket
        * - 取消所有待处理的异步操作
        * 注意：
        * - 关闭后不能再发送或接收数据
        */  
        void close();

        /*
            * 获取本地监听的端口号
            * 如果构造的时候传入0，这里会返回系统分配的实际的端口号        
        */
        uint16_t localPort() const {
            return socket_.is_open() ? socket_.local_endpoint().port() : 0;
        }

        bool isReceiving() const;

        struct Statistics {
            size_t bytes_sent{0};           // 发送的总字节数
            size_t bytes_received{0};       // 接收的总字节数
            size_t messages_sent{0};        // 发送的消息数
            size_t messages_received{0};    // 接收的消息数
            size_t send_errors{0};          // 发送错误次数
            size_t receive_errors{0};       // 接收错误次数
            
            /*
            * @brief 重置所有统计数据为 0
            */
            void reset() {
                bytes_sent = 0;
                bytes_received = 0;
                messages_sent = 0;
                messages_received = 0;
                send_errors = 0;
                receive_errors = 0;
            }
        };
        

        Statistics getStatistics() const;

        void resetStatistics();

private:
        /**
         * @brief 执行异步接收操作
         * 
         * 内部方法，启动一次异步接收
         */
        void doReceive();

        /**
         * @brief 处理接收完成事件
         * @param ec 错误码
         * @param bytes_received 接收的字节数
         * 
         * 内部方法，处理接收完成后的逻辑：
         * - 更新统计信息
         * - 构造 UdpMessage
         * - 调用用户回调
         * - 继续接收下一个数据包
         */
        void handleReceive(const asio::error_code& ec, size_t bytes_received);
        /**
         * @brief 更新发送统计信息
         * @param bytes 发送的字节数
         * @param success 是否成功
         * 
         * 内部方法，线程安全
         */
        void updateSendStats(size_t bytes, bool success);
        /**
         * @brief 更新接收统计信息
         * @param bytes 接收的字节数
         * @param success 是否成功
         * 
         * 内部方法，线程安全
         */
        void updateReceiveStats(size_t bytes, bool success);
        /**
         * @brief 解析端点地址
         * @param endpoint 端点信息（可能包含域名）
         * @return asio 端点对象
         * 
         * 内部方法，支持：
         * - IPv4 地址
         * - IPv6 地址
         * - 域名（会进行 DNS 解析）
         */
        asio::ip::udp::endpoint resolveEndpoint(const UdpEndpoint& endpoint);

private:
        asio::io_context& io_context_;              // io_context 引用
        asio::ip::udp::socket socket_;              // UDP socket
        std::atomic<bool> receiving_{false};    // 是否正在接收标志
        ReceiveCallback receive_callback_;          // 接收回调函数
        std::array<uint8_t, 65536> receive_buffer_; // 接收缓冲区（64KB，足够大多数 UDP 包）
        asio::ip::udp::endpoint remote_endpoint_;   // 远程端点（接收时用于存储发送方

        mutable std::mutex stats_mutex_;            // 统计信息互斥锁
        Statistics statistics_;                     // 统计信息

};



}; // namespace magnet::network

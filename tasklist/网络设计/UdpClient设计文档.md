# UdpClient 设计文档

> **模块名称**：UdpClient - UDP 网络通信客户端
> 
> **设计时间**：2025-12-31
> 
> **设计目标**：提供通用的、异步的 UDP 通信封装

---

## 📋 目录

1. [模块定位](#模块定位)
2. [核心职责](#核心职责)
3. [数据流向](#数据流向)
4. [接口设计](#接口设计)
5. [使用示例](#使用示例)
6. [实现要点](#实现要点)
7. [错误处理](#错误处理)

---

## 🎯 模块定位

### 在架构中的位置

```
┌─────────────────────────────────────────────────────────┐
│                    应用层                                │
│              DownloadManager                            │
└─────────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────────┐
│                    协议层                                │
│              DHTClient（使用 UdpClient）                 │
│              TrackerClient（使用 UdpClient）             │
└─────────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────────┐
│                    网络层                                │
│              UdpClient ← 你在这里！                      │
└─────────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────────┐
│                    基础层                                │
│              asio::ip::udp::socket                      │
└─────────────────────────────────────────────────────────┘
```

### 为什么需要 UdpClient？

**问题**：直接使用 `asio::ip::udp::socket` 太底层
- 需要手动管理缓冲区
- 需要处理异步回调的生命周期
- 需要自己实现错误处理
- 需要自己实现统计功能

**解决**：UdpClient 提供更高层的封装
- ✅ 自动管理缓冲区
- ✅ 简化的回调接口
- ✅ 统一的错误处理
- ✅ 内置统计功能

---

## 🔧 核心职责

### UdpClient 只做三件事

1. **发送 UDP 数据包**
   - 输入：目标地址 + 数据
   - 输出：发送结果（成功/失败）

2. **接收 UDP 数据包**
   - 输入：无（持续监听）
   - 输出：收到的数据 + 来源地址

3. **提供统计信息**
   - 发送/接收字节数
   - 发送/接收消息数
   - 错误次数

### UdpClient 不做什么

- ❌ 不解析协议（DHT、Tracker 等）
- ❌ 不管理连接状态（UDP 是无连接的）
- ❌ 不实现重试机制（由上层决定）
- ❌ 不做数据编解码（Bencode 等）

---

## 📊 数据流向

### 发送流程

```
┌─────────────────────────────────────────────────────────┐
│ DHT 客户端                                               │
│ "我要发送查询消息到 router.bittorrent.com:6881"          │
└─────────────────────────────────────────────────────────┘
                        ↓
              调用 udp_client.send()
                        ↓
┌─────────────────────────────────────────────────────────┐
│ UdpClient                                               │
│ 1. 解析目标地址（DNS 查询）                              │
│ 2. 调用 socket.async_send_to()                          │
│ 3. 等待发送完成                                          │
└─────────────────────────────────────────────────────────┘
                        ↓
              asio 异步发送
                        ↓
┌─────────────────────────────────────────────────────────┐
│ 操作系统网络栈                                           │
│ 通过网络发送 UDP 数据包                                  │
└─────────────────────────────────────────────────────────┘
                        ↓
              网络传输
                        ↓
┌─────────────────────────────────────────────────────────┐
│ 远程 DHT 节点                                            │
│ 接收到 UDP 数据包                                        │
└─────────────────────────────────────────────────────────┘
                        ↓
              发送完成回调
                        ↓
┌─────────────────────────────────────────────────────────┐
│ DHT 客户端                                               │
│ "发送成功，等待响应"                                     │
└─────────────────────────────────────────────────────────┘
```

### 接收流程

```
┌─────────────────────────────────────────────────────────┐
│ 远程 DHT 节点                                            │
│ 发送响应消息                                             │
└─────────────────────────────────────────────────────────┘
                        ↓
              网络传输
                        ↓
┌─────────────────────────────────────────────────────────┐
│ 操作系统网络栈                                           │
│ 接收到 UDP 数据包                                        │
└─────────────────────────────────────────────────────────┘
                        ↓
              asio 异步接收完成
                        ↓
┌─────────────────────────────────────────────────────────┐
│ UdpClient                                               │
│ 1. 从缓冲区读取数据                                      │
│ 2. 记录来源地址                                          │
│ 3. 触发接收回调                                          │
│ 4. 继续监听下一个数据包                                  │
└─────────────────────────────────────────────────────────┘
                        ↓
              调用 receive_callback
                        ↓
┌─────────────────────────────────────────────────────────┐
│ DHT 客户端                                               │
│ "收到响应，解析 Bencode 数据"                            │
└─────────────────────────────────────────────────────────┘
```

---

## 🏗️ 接口设计

### 1. 数据类型定义

```cpp
// include/magnet/network/network_types.h

#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace magnet::network {

/**
 * @brief UDP 端点（IP + 端口）
 */
struct UdpEndpoint {
    std::string ip;      // IP 地址（IPv4 或 IPv6）
    uint16_t port;       // 端口号
    
    UdpEndpoint() : port(0) {}
    UdpEndpoint(std::string ip_, uint16_t port_) 
        : ip(std::move(ip_)), port(port_) {}
    
    // 转换为字符串（用于日志）
    std::string to_string() const {
        return ip + ":" + std::to_string(port);
    }
};

/**
 * @brief UDP 消息（数据 + 来源地址）
 */
struct UdpMessage {
    std::vector<uint8_t> data;           // 消息数据
    UdpEndpoint remote_endpoint;         // 来源地址
    
    UdpMessage() = default;
    UdpMessage(std::vector<uint8_t> data_, UdpEndpoint endpoint_)
        : data(std::move(data_)), remote_endpoint(std::move(endpoint_)) {}
    
    // 获取数据大小
    size_t size() const { return data.size(); }
    
    // 检查是否为空
    bool empty() const { return data.empty(); }
};

} // namespace magnet::network
```

### 2. UdpClient 类接口

```cpp
// include/magnet/network/udp_client.h

#pragma once
#include "network_types.h"
#include <asio.hpp>
#include <functional>
#include <memory>
#include <atomic>
#include <mutex>

namespace magnet::network {

/**
 * @brief UDP 客户端
 * 
 * 提供异步的 UDP 通信功能，包括：
 * - 发送 UDP 数据包
 * - 接收 UDP 数据包
 * - 统计信息
 * 
 * 线程安全性：
 * - send() 方法是线程安全的
 * - 回调在 io_context 线程中执行
 * 
 * 使用示例：
 * @code
 * asio::io_context io_context;
 * UdpClient client(io_context, 6881);
 * 
 * // 开始接收
 * client.start_receive([](const UdpMessage& msg) {
 *     std::cout << "收到 " << msg.size() << " 字节" << std::endl;
 * });
 * 
 * // 发送数据
 * UdpEndpoint target{"192.168.1.100", 6881};
 * std::vector<uint8_t> data = {1, 2, 3, 4};
 * client.send(target, data, [](const asio::error_code& ec, size_t bytes) {
 *     if (!ec) {
 *         std::cout << "发送成功: " << bytes << " 字节" << std::endl;
 *     }
 * });
 * 
 * io_context.run();
 * @endcode
 */
class UdpClient : public std::enable_shared_from_this<UdpClient> {
public:
    /**
     * @brief 接收回调类型
     * @param message 收到的消息（包含数据和来源地址）
     */
    using ReceiveCallback = std::function<void(const UdpMessage& message)>;
    
    /**
     * @brief 发送回调类型
     * @param ec 错误码（成功时为空）
     * @param bytes_sent 实际发送的字节数
     */
    using SendCallback = std::function<void(const asio::error_code& ec, size_t bytes_sent)>;
    
    /**
     * @brief 构造函数
     * @param io_context Asio io_context 引用
     * @param local_port 本地监听端口（0 表示随机端口）
     * @throw std::runtime_error 如果端口绑定失败
     */
    explicit UdpClient(asio::io_context& io_context, uint16_t local_port = 0);
    
    /**
     * @brief 析构函数，自动关闭 socket
     */
    ~UdpClient();
    
    // 禁止拷贝和移动
    UdpClient(const UdpClient&) = delete;
    UdpClient& operator=(const UdpClient&) = delete;
    UdpClient(UdpClient&&) = delete;
    UdpClient& operator=(UdpClient&&) = delete;
    
    /**
     * @brief 发送 UDP 数据包
     * @param endpoint 目标地址
     * @param data 要发送的数据
     * @param callback 发送完成回调（可选）
     * 
     * 注意：
     * - 此方法是线程安全的
     * - 回调在 io_context 线程中执行
     * - 如果 socket 未打开，会自动打开
     */
    void send(const UdpEndpoint& endpoint, 
              const std::vector<uint8_t>& data,
              SendCallback callback = nullptr);
    
    /**
     * @brief 开始接收 UDP 数据包
     * @param callback 接收回调（每次收到数据时调用）
     * 
     * 注意：
     * - 只能调用一次，重复调用会抛出异常
     * - 回调在 io_context 线程中执行
     * - 会持续接收直到调用 stop_receive()
     */
    void start_receive(ReceiveCallback callback);
    
    /**
     * @brief 停止接收 UDP 数据包
     * 
     * 注意：
     * - 停止后可以再次调用 start_receive()
     * - 不会关闭 socket，仍可以发送数据
     */
    void stop_receive();
    
    /**
     * @brief 关闭 UDP 客户端
     * 
     * 注意：
     * - 关闭 socket
     * - 停止接收
     * - 取消所有待处理的操作
     */
    void close();
    
    /**
     * @brief 获取本地端口
     * @return 本地监听端口号
     */
    uint16_t local_port() const;
    
    /**
     * @brief 检查是否正在接收
     * @return true 如果正在接收
     */
    bool is_receiving() const;
    
    /**
     * @brief 统计信息结构
     */
    struct Statistics {
        size_t bytes_sent{0};           // 发送的总字节数
        size_t bytes_received{0};       // 接收的总字节数
        size_t messages_sent{0};        // 发送的消息数
        size_t messages_received{0};    // 接收的消息数
        size_t send_errors{0};          // 发送错误次数
        size_t receive_errors{0};       // 接收错误次数
        
        // 重置统计
        void reset() {
            bytes_sent = 0;
            bytes_received = 0;
            messages_sent = 0;
            messages_received = 0;
            send_errors = 0;
            receive_errors = 0;
        }
    };
    
    /**
     * @brief 获取统计信息
     * @return 当前的统计信息
     */
    Statistics get_statistics() const;
    
    /**
     * @brief 重置统计信息
     */
    void reset_statistics();

private:
    asio::io_context& io_context_;              // io_context 引用
    asio::ip::udp::socket socket_;              // UDP socket
    
    // 接收相关
    std::atomic<bool> receiving_{false};        // 是否正在接收
    ReceiveCallback receive_callback_;          // 接收回调
    std::array<uint8_t, 65536> receive_buffer_; // 接收缓冲区（64KB）
    asio::ip::udp::endpoint remote_endpoint_;   // 远程端点（接收时使用）
    
    // 统计信息
    mutable std::mutex stats_mutex_;            // 统计信息互斥锁
    Statistics statistics_;                     // 统计数据
    
    /**
     * @brief 异步接收处理函数
     */
    void do_receive();
    
    /**
     * @brief 接收完成处理函数
     * @param ec 错误码
     * @param bytes_received 接收的字节数
     */
    void handle_receive(const asio::error_code& ec, size_t bytes_received);
    
    /**
     * @brief 更新发送统计
     * @param bytes 发送的字节数
     * @param success 是否成功
     */
    void update_send_stats(size_t bytes, bool success);
    
    /**
     * @brief 更新接收统计
     * @param bytes 接收的字节数
     * @param success 是否成功
     */
    void update_receive_stats(size_t bytes, bool success);
    
    /**
     * @brief 解析端点（支持域名解析）
     * @param endpoint 端点信息
     * @return asio 端点
     */
    asio::ip::udp::endpoint resolve_endpoint(const UdpEndpoint& endpoint);
};

} // namespace magnet::network
```

---

## 💡 使用示例

### 示例 1：简单的发送和接收

```cpp
#include <magnet/network/udp_client.h>
#include <iostream>

using namespace magnet::network;

int main() {
    asio::io_context io_context;
    
    // 创建 UDP 客户端，监听 6881 端口
    UdpClient client(io_context, 6881);
    
    std::cout << "监听端口: " << client.local_port() << std::endl;
    
    // 开始接收
    client.start_receive([](const UdpMessage& message) {
        std::cout << "收到来自 " << message.remote_endpoint.to_string() 
                  << " 的消息: " << message.size() << " 字节" << std::endl;
        
        // 打印前 10 个字节
        for (size_t i = 0; i < std::min(message.size(), size_t(10)); ++i) {
            std::cout << std::hex << (int)message.data[i] << " ";
        }
        std::cout << std::endl;
    });
    
    // 发送测试消息
    UdpEndpoint target{"127.0.0.1", 6881};
    std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04};
    
    client.send(target, data, [](const asio::error_code& ec, size_t bytes) {
        if (!ec) {
            std::cout << "发送成功: " << bytes << " 字节" << std::endl;
        } else {
            std::cerr << "发送失败: " << ec.message() << std::endl;
        }
    });
    
    // 运行 5 秒
    io_context.run_for(std::chrono::seconds(5));
    
    // 显示统计信息
    auto stats = client.get_statistics();
    std::cout << "\n统计信息:" << std::endl;
    std::cout << "  发送: " << stats.messages_sent << " 消息, " 
              << stats.bytes_sent << " 字节" << std::endl;
    std::cout << "  接收: " << stats.messages_received << " 消息, " 
              << stats.bytes_received << " 字节" << std::endl;
    
    return 0;
}
```

### 示例 2：DHT 客户端使用 UdpClient

```cpp
class DHTClient {
public:
    explicit DHTClient(asio::io_context& io_context) 
        : udp_client_(std::make_shared<UdpClient>(io_context, 6881)) {
        
        // 开始接收 DHT 消息
        udp_client_->start_receive([this](const UdpMessage& message) {
            handle_dht_message(message);
        });
    }
    
    void find_peers(const InfoHash& info_hash) {
        // 构造 DHT get_peers 查询
        BencodeDict query;
        query["q"] = "get_peers";
        query["a"]["id"] = node_id_;
        query["a"]["info_hash"] = std::string(
            reinterpret_cast<const char*>(info_hash.bytes().data()), 20);
        
        // 编码为 Bencode
        std::string encoded = Bencode::encode(query);
        std::vector<uint8_t> data(encoded.begin(), encoded.end());
        
        // 发送到引导节点
        UdpEndpoint bootstrap{"router.bittorrent.com", 6881};
        
        udp_client_->send(bootstrap, data, [](const asio::error_code& ec, size_t bytes) {
            if (!ec) {
                std::cout << "DHT 查询已发送: " << bytes << " 字节" << std::endl;
            }
        });
    }
    
private:
    std::shared_ptr<UdpClient> udp_client_;
    std::array<uint8_t, 20> node_id_;
    
    void handle_dht_message(const UdpMessage& message) {
        // 解析 Bencode 消息
        auto bencode = Bencode::decode(message.data);
        if (!bencode) {
            return;  // 解析失败
        }
        
        // 处理 DHT 响应
        if (bencode->is_dict()) {
            auto& dict = bencode->as_dict();
            
            // 检查是否是 get_peers 响应
            if (dict.count("r") && dict.at("r").is_dict()) {
                auto& r = dict.at("r").as_dict();
                
                // 提取 Peer 列表
                if (r.count("values")) {
                    auto peers = parse_peer_list(r.at("values"));
                    std::cout << "找到 " << peers.size() << " 个 Peer" << std::endl;
                }
            }
        }
    }
};
```

### 示例 3：错误处理

```cpp
void send_with_retry(UdpClient& client, 
                     const UdpEndpoint& endpoint,
                     const std::vector<uint8_t>& data,
                     int max_retries = 3) {
    
    auto retry_count = std::make_shared<int>(0);
    
    std::function<void()> do_send = [&, retry_count, do_send]() {
        client.send(endpoint, data, 
            [retry_count, do_send, max_retries](const asio::error_code& ec, size_t bytes) {
                if (ec) {
                    // 发送失败
                    (*retry_count)++;
                    
                    if (*retry_count < max_retries) {
                        std::cout << "发送失败，重试 " << *retry_count 
                                  << "/" << max_retries << std::endl;
                        
                        // 延迟后重试
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                        do_send();
                    } else {
                        std::cerr << "发送失败，已达最大重试次数" << std::endl;
                    }
                } else {
                    std::cout << "发送成功: " << bytes << " 字节" << std::endl;
                }
            });
    };
    
    do_send();
}
```

---

## 🔨 实现要点

### 1. 构造函数实现

```cpp
UdpClient::UdpClient(asio::io_context& io_context, uint16_t local_port)
    : io_context_(io_context)
    , socket_(io_context) {
    
    // 打开 socket（IPv4）
    socket_.open(asio::ip::udp::v4());
    
    // 绑定到本地端口
    asio::ip::udp::endpoint local_endpoint(asio::ip::udp::v4(), local_port);
    socket_.bind(local_endpoint);
    
    // 设置 socket 选项
    socket_.set_option(asio::socket_base::reuse_address(true));
}
```

### 2. 发送实现

```cpp
void UdpClient::send(const UdpEndpoint& endpoint, 
                     const std::vector<uint8_t>& data,
                     SendCallback callback) {
    // 解析端点
    auto remote_endpoint = resolve_endpoint(endpoint);
    
    // 异步发送
    socket_.async_send_to(asio::buffer(data), remote_endpoint,
        [this, callback, size = data.size()](const asio::error_code& ec, size_t bytes_sent) {
            // 更新统计
            update_send_stats(bytes_sent, !ec);
            
            // 调用回调
            if (callback) {
                callback(ec, bytes_sent);
            }
        });
}
```

### 3. 接收实现

```cpp
void UdpClient::start_receive(ReceiveCallback callback) {
    if (receiving_.exchange(true)) {
        throw std::runtime_error("已经在接收中");
    }
    
    receive_callback_ = std::move(callback);
    do_receive();
}

void UdpClient::do_receive() {
    socket_.async_receive_from(
        asio::buffer(receive_buffer_), 
        remote_endpoint_,
        [this, self = shared_from_this()](const asio::error_code& ec, size_t bytes_received) {
            handle_receive(ec, bytes_received);
        });
}

void UdpClient::handle_receive(const asio::error_code& ec, size_t bytes_received) {
    if (!ec && bytes_received > 0) {
        // 更新统计
        update_receive_stats(bytes_received, true);
        
        // 构造消息
        UdpMessage message;
        message.data.assign(receive_buffer_.begin(), 
                           receive_buffer_.begin() + bytes_received);
        message.remote_endpoint.ip = remote_endpoint_.address().to_string();
        message.remote_endpoint.port = remote_endpoint_.port();
        
        // 调用回调
        if (receive_callback_) {
            receive_callback_(message);
        }
    } else {
        // 更新错误统计
        update_receive_stats(0, false);
    }
    
    // 继续接收
    if (receiving_.load()) {
        do_receive();
    }
}
```

---

## ⚠️ 错误处理

### 常见错误

1. **端口已被占用**
   ```cpp
   try {
       UdpClient client(io_context, 6881);
   } catch (const std::exception& e) {
       std::cerr << "端口绑定失败: " << e.what() << std::endl;
       // 尝试使用随机端口
       UdpClient client(io_context, 0);
   }
   ```

2. **发送失败**
   ```cpp
   client.send(endpoint, data, [](const asio::error_code& ec, size_t bytes) {
       if (ec) {
           if (ec == asio::error::host_not_found) {
               std::cerr << "主机不存在" << std::endl;
           } else if (ec == asio::error::network_unreachable) {
               std::cerr << "网络不可达" << std::endl;
           } else {
               std::cerr << "发送失败: " << ec.message() << std::endl;
           }
       }
   });
   ```

3. **接收缓冲区溢出**
   ```cpp
   // UdpClient 使用 64KB 缓冲区，足够大多数情况
   // 如果需要更大的缓冲区，可以修改 receive_buffer_ 的大小
   ```

---

## 📝 总结

### UdpClient 的核心价值

1. **简化 UDP 通信**
   - 封装 asio 的底层细节
   - 提供简单易用的接口

2. **统一错误处理**
   - 通过回调返回错误
   - 提供统计信息

3. **支持异步操作**
   - 不阻塞主线程
   - 高性能并发

### 下一步

1. **实现 UdpClient**
   - 按照接口实现 .cpp 文件
   - 编写单元测试

2. **实现 Bencode**
   - DHT 协议需要 Bencode 编解码

3. **实现 DHTClient**
   - 使用 UdpClient 和 Bencode
   - 实现 DHT 协议

---

**设计完成！可以开始实现了。**

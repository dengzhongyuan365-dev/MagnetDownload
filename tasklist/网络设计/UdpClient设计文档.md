# UdpClient 设计文档

> **模块名称**：UdpClient - UDP 网络通信客户端  
> **版本**：v1.0  
> **最后更新**：2025-01-12  
> **作者**：MagnetDownload Team

---

## 1. 为什么需要 UdpClient？

### 1.1 问题背景

在 BitTorrent/DHT 协议中，节点之间的通信主要依赖 **UDP 协议**。原因如下：

1. **DHT 网络特点**：
   - 需要与大量节点通信（可能同时与数百个节点交互）
   - 消息通常很小（几百字节到几KB）
   - 消息是"发后即忘"的查询/响应模式
   - 不需要可靠传输（协议层会处理重试）

2. **为什么不用 TCP？**
   - TCP 需要三次握手建立连接，开销大
   - 每个连接占用系统资源（文件描述符、内存）
   - 对于 DHT 的短消息交互，TCP 的可靠性是多余的

3. **直接使用 asio::udp::socket 的问题**：

```cpp
// 直接使用 asio 的问题示例
asio::ip::udp::socket socket(io_context);
socket.open(asio::ip::udp::v4());
socket.bind(asio::ip::udp::endpoint(asio::ip::udp::v4(), 6881));

// 问题1: 需要手动管理接收缓冲区
std::array<uint8_t, 65536> buffer;
asio::ip::udp::endpoint sender_endpoint;

// 问题2: 异步接收的回调需要处理很多细节
socket.async_receive_from(
    asio::buffer(buffer), 
    sender_endpoint,
    [&](const asio::error_code& ec, size_t bytes_received) {
        // 问题3: 需要在这里处理错误
        // 问题4: 需要手动构造消息对象
        // 问题5: 需要手动继续接收下一个包
        // 问题6: 需要自己实现统计功能
    });
```

### 1.2 UdpClient 的解决方案

UdpClient 的设计目标是：**封装底层细节，提供简洁易用的 UDP 通信接口**。

```cpp
// 使用 UdpClient 后的代码
UdpClient client(io_context, 6881);

// 一行代码开始接收，所有细节被封装
client.startReceive([](const UdpMessage& msg) {
    // 直接使用消息，不需要关心缓冲区、错误处理等
    std::cout << "收到 " << msg.size() << " 字节" << std::endl;
});

// 发送也很简单
client.send({"192.168.1.1", 6881}, data);
```

---

## 2. 设计目标与原则

### 2.1 设计目标

| 目标 | 说明 | 优先级 |
|------|------|--------|
| **简单易用** | 上层代码只需要关心业务逻辑，不需要了解 asio 细节 | 高 |
| **异步非阻塞** | 所有 I/O 操作都是异步的，不会阻塞调用线程 | 高 |
| **线程安全** | send() 可以从任何线程调用 | 高 |
| **资源安全** | 使用 RAII 管理资源，析构时自动清理 | 高 |
| **可观测性** | 提供统计信息，便于监控和调试 | 中 |
| **可扩展性** | 接口设计允许将来添加新功能 | 中 |

### 2.2 设计原则

#### 原则1：单一职责

UdpClient **只负责 UDP 数据包的收发**，不涉及：
- 协议解析（DHT、BitTorrent）→ 由协议层处理
- 消息编解码（Bencode）→ 由 Bencode 模块处理
- 重试机制 → 由上层业务决定
- 连接管理 → UDP 是无连接的，没有这个概念

**为什么？**
- 职责清晰，便于测试和维护
- 上层可以灵活组合不同的协议实现
- 符合"组合优于继承"的原则

#### 原则2：回调而非 Future/Promise

我们选择使用回调函数而不是 std::future：

```cpp
// 我们的设计：回调方式
void send(const UdpEndpoint& endpoint, 
          const std::vector<uint8_t>& data,
          SendCallback callback);

// 另一种选择：future 方式（我们没有采用）
std::future<size_t> send(const UdpEndpoint& endpoint,
                         const std::vector<uint8_t>& data);
```

**为什么选择回调？**
1. **与 asio 模型一致**：asio 本身是回调驱动的
2. **避免阻塞**：future.get() 会阻塞线程
3. **更好的性能**：回调不需要创建额外的同步对象
4. **链式操作更自然**：在回调中可以直接发起下一个操作

#### 原则3：接收回调持续触发

```cpp
// 我们的设计：一次注册，多次回调
client.startReceive([](const UdpMessage& msg) {
    // 每收到一个包，这个回调就会被调用一次
    // 不需要每次都重新注册
});
```

**为什么这样设计？**
- DHT 需要持续接收消息，不是"接收一次就结束"
- 减少重复代码，上层不需要在每次回调后重新调用 startReceive
- 内部实现会自动继续监听下一个数据包

---

## 3. 架构设计

### 3.1 在系统中的位置

```mermaid
graph TB
    subgraph 应用层["应用层 - 用户交互"]
        CLI[命令行界面]
        GUI[图形界面]
    end
    
    subgraph 业务层["业务层 - 下载管理"]
        DM[DownloadManager<br/>下载管理器]
        SM[SessionManager<br/>会话管理器]
    end
    
    subgraph 协议层["协议层 - P2P协议实现"]
        DHT[DHTClient<br/>DHT客户端]
        BT[BitTorrent<br/>BT协议]
        Tracker[TrackerClient<br/>Tracker客户端]
    end
    
    subgraph 编解码层["编解码层"]
        Bencode[Bencode<br/>编解码器]
    end
    
    subgraph 网络层["网络层 - 底层通信"]
        UDP[UdpClient<br/>UDP客户端]
        TCP[TcpClient<br/>TCP客户端]
    end
    
    subgraph 基础层["基础层 - 运行时支持"]
        ELM[EventLoopManager<br/>事件循环管理器]
        Logger[Logger<br/>日志系统]
    end
    
    CLI --> DM
    GUI --> DM
    DM --> DHT
    DM --> BT
    SM --> Tracker
    
    DHT --> Bencode
    DHT --> UDP
    Tracker --> UDP
    BT --> TCP
    
    UDP --> ELM
    TCP --> ELM
    
    style UDP fill:#ff9800,stroke:#e65100,stroke-width:3px,color:#fff
    style DHT fill:#4caf50,stroke:#2e7d32
    style Bencode fill:#9c27b0,stroke:#6a1b9a
```

### 3.2 为什么放在网络层？

**网络层的职责**：提供基础的网络通信能力，对上层隐藏操作系统和网络库的差异。

**UdpClient 在网络层的原因**：
1. 它是对 asio UDP socket 的封装
2. 它不理解任何业务协议（DHT、Tracker）
3. 它只处理"字节流"的发送和接收
4. 它可以被不同的协议层模块复用

### 3.3 与其他模块的关系

```mermaid
graph LR
    subgraph 依赖 UdpClient 的模块
        DHT[DHTClient]
        Tracker[TrackerClient]
    end
    
    subgraph UdpClient
        Send[send 方法]
        Recv[startReceive 方法]
        Stats[统计功能]
    end
    
    subgraph UdpClient 依赖的模块
        ASIO[asio 库]
        Types[network_types.h]
        ELM[EventLoopManager<br/>提供 io_context]
    end
    
    DHT -->|发送DHT查询| Send
    DHT -->|接收DHT响应| Recv
    Tracker -->|发送Tracker请求| Send
    Tracker -->|接收Tracker响应| Recv
    
    Send --> ASIO
    Recv --> ASIO
    Send --> Types
    Recv --> Types
    
    style Send fill:#4caf50
    style Recv fill:#2196f3
```

---

## 4. 接口设计详解

### 4.1 为什么使用 `enable_shared_from_this`？

```cpp
class UdpClient : public std::enable_shared_from_this<UdpClient> {
    // ...
};
```

**问题场景**：
```cpp
void UdpClient::doReceive() {
    socket_.async_receive_from(
        asio::buffer(receive_buffer_), 
        remote_endpoint_,
        [this](const asio::error_code& ec, size_t bytes) {
            // 危险！如果 UdpClient 对象已经被销毁，
            // 这里的 this 指针就是悬空的！
            handleReceive(ec, bytes);
        });
}
```

当异步操作还没完成时，如果 UdpClient 对象被销毁了，回调中的 `this` 指针就会指向已释放的内存。

**解决方案**：
```cpp
void UdpClient::doReceive() {
    socket_.async_receive_from(
        asio::buffer(receive_buffer_), 
        remote_endpoint_,
        [this, self = shared_from_this()](const asio::error_code& ec, size_t bytes) {
            // 安全！self 持有 shared_ptr，保证对象在回调执行期间不会被销毁
            handleReceive(ec, bytes);
        });
}
```

`shared_from_this()` 会返回一个指向自己的 `shared_ptr`，被 lambda 捕获后，会延长对象的生命周期。

### 4.2 回调类型设计

```cpp
// 接收回调：收到数据时调用
using ReceiveCallback = std::function<void(const UdpMessage& message)>;

// 发送回调：发送完成时调用
using SendCallback = std::function<void(const asio::error_code& ec, size_t bytes_sent)>;
```

**为什么接收回调不包含错误码？**

对于接收操作，我们选择"只在成功时回调"：
- 成功接收 → 调用回调，传递消息
- 接收失败 → 更新错误统计，不调用回调，继续接收

**原因**：
1. DHT 协议下，偶尔的接收失败（如 ICMP 错误）是正常的
2. 上层代码通常只关心成功收到的消息
3. 错误可以通过统计信息监控，不需要每次都通知

**发送回调为什么包含错误码？**

发送操作是"有明确预期"的：
- 调用 `send()` 时，调用者期望知道发送是否成功
- 发送失败可能需要重试或通知用户
- 错误码可以帮助判断失败原因（网络不可达、主机不存在等）

### 4.3 UdpEndpoint 设计

```cpp
struct UdpEndpoint {
    std::string ip;      // 为什么用 string 而不是 asio::ip::address？
    uint16_t port;
    
    // 为什么要 toString()？
    std::string toString() const;
    
    // 为什么要 isValid()？
    bool isValid() const;
};
```

**为什么 ip 用 `std::string` 而不是 `asio::ip::address`？**

1. **隐藏 asio 依赖**：上层代码不需要包含 asio 头文件
2. **支持域名**：`"router.bittorrent.com"` 可以直接传入，UdpClient 内部解析
3. **易于序列化**：字符串便于存储和传输
4. **与配置文件兼容**：配置文件中的地址就是字符串形式

**为什么需要 `isValid()`？**

```cpp
bool UdpEndpoint::isValid() const {
    return !ip.empty() && port != 0;
}
```

防止无效参数：
```cpp
UdpEndpoint endpoint;  // 默认构造，ip 为空，port 为 0
client.send(endpoint, data);  // 应该被拒绝

// 使用 isValid() 检查
if (!endpoint.isValid()) {
    throw std::invalid_argument("Invalid endpoint");
}
```

### 4.4 UdpMessage 设计

```cpp
struct UdpMessage {
    std::vector<uint8_t> data;       // 为什么用 vector 而不是 array？
    UdpEndpoint remote_endpoint;     // 为什么要包含来源地址？
};
```

**为什么用 `std::vector<uint8_t>` 而不是固定大小的数组？**

1. **大小可变**：UDP 包大小不固定（从几字节到 64KB）
2. **所有权转移**：可以 `std::move` 避免拷贝
3. **内存效率**：不浪费内存在小包上

**为什么要包含 `remote_endpoint`？**

DHT 协议需要知道消息的来源：
- 回复消息需要发送到正确的地址
- 路由表需要记录节点的地址
- 用于验证消息是否来自预期的节点

---

## 5. 核心方法详解

### 5.1 构造函数

```cpp
explicit UdpClient(asio::io_context& io_context, uint16_t local_port = 0);
```

**参数说明**：

| 参数 | 说明 |
|------|------|
| `io_context` | 引用而非拷贝，UdpClient 不拥有 io_context |
| `local_port` | 本地端口，0 表示让系统自动分配 |

**为什么 `local_port` 默认为 0？**

```cpp
// 场景1：DHT 客户端 - 需要固定端口，因为其他节点可能记住了这个端口
UdpClient dht_client(io_context, 6881);

// 场景2：一次性查询 - 不需要固定端口，让系统分配更安全
UdpClient query_client(io_context, 0);  // 系统分配一个可用端口
```

**构造函数内部做了什么？**

```mermaid
flowchart TD
    A[构造函数调用] --> B[创建 UDP socket]
    B --> C[打开 socket - IPv4]
    C --> D{local_port == 0?}
    D -->|是| E[绑定到随机端口]
    D -->|否| F[绑定到指定端口]
    E --> G[设置 socket 选项]
    F --> G
    G --> H[构造完成]
    
    F -->|端口被占用| X[抛出异常]
    
    style X fill:#f44336,color:#fff
```

### 5.2 send() 方法

```cpp
void send(const UdpEndpoint& endpoint, 
          const std::vector<uint8_t>& data,
          SendCallback callback = nullptr);
```

**为什么 data 是 `const std::vector<uint8_t>&`？**

```cpp
// 方案1：const 引用（我们的选择）
void send(const std::vector<uint8_t>& data);
// 优点：调用者可以传入临时对象或已有对象
// 缺点：需要内部拷贝（因为异步操作，原数据可能在发送完成前被修改）

// 方案2：值传递
void send(std::vector<uint8_t> data);
// 优点：对于临时对象可以 move 避免拷贝
// 缺点：对于已有对象总是会拷贝

// 方案3：右值引用
void send(std::vector<uint8_t>&& data);
// 优点：强制 move，性能好
// 缺点：调用者必须 move，不能传入需要保留的数据
```

我们选择方案1的原因：
- 接口最友好，调用者负担最小
- 性能差异在 DHT 场景下可以忽略（消息很小）

**为什么 callback 是可选的？**

```cpp
// 场景1：不关心发送结果
client.send(endpoint, data);  // 简洁

// 场景2：需要知道发送结果
client.send(endpoint, data, [](auto ec, auto bytes) {
    if (ec) { /* 处理错误 */ }
});
```

很多场景下调用者不需要知道发送是否成功（UDP 本身就是不可靠的），提供默认参数可以简化调用。

**send() 内部流程**：

```mermaid
sequenceDiagram
    participant Caller as 调用者
    participant UDP as UdpClient
    participant Socket as asio::socket
    participant OS as 操作系统
    
    Caller->>UDP: send(endpoint, data, callback)
    
    Note over UDP: 1. 解析 endpoint
    UDP->>UDP: resolveEndpoint()
    
    alt endpoint 是域名
        UDP->>OS: DNS 查询
        OS-->>UDP: IP 地址
    end
    
    Note over UDP: 2. 发起异步发送
    UDP->>Socket: async_send_to(data, endpoint, handler)
    
    Note over Socket,OS: 异步执行...
    
    Socket-->>UDP: handler(ec, bytes_sent)
    
    Note over UDP: 3. 更新统计
    UDP->>UDP: updateSendStats()
    
    Note over UDP: 4. 调用回调
    alt callback != nullptr
        UDP-->>Caller: callback(ec, bytes_sent)
    end
```

### 5.3 startReceive() 方法

```cpp
void startReceive(ReceiveCallback callback);
```

**为什么只能调用一次？**

```cpp
client.startReceive(callback1);  // OK
client.startReceive(callback2);  // 抛出异常！
```

原因：
1. 同一个 socket 上只需要一个接收监听
2. 多次调用会导致混乱：哪个回调应该被调用？
3. 强制这个约束可以防止使用错误

**如果需要更换回调怎么办？**

```cpp
client.stopReceive();
client.startReceive(newCallback);  // OK
```

**startReceive() 内部流程**：

```mermaid
stateDiagram-v2
    [*] --> Idle: 构造完成
    
    Idle --> Receiving: startReceive()
    note right of Receiving: 开始异步接收
    
    Receiving --> Receiving: 收到数据，处理后继续接收
    
    Receiving --> Idle: stopReceive()
    
    Idle --> Closed: close()
    Receiving --> Closed: close()
    
    Closed --> [*]
```

### 5.4 stopReceive() 与 close() 的区别

```cpp
void stopReceive();  // 只停止接收
void close();        // 关闭整个 socket
```

**为什么要分成两个方法？**

| 场景 | 使用的方法 |
|------|-----------|
| 暂时不想接收数据，但还要发送 | `stopReceive()` |
| 完全结束通信 | `close()` |
| 需要更换接收回调 | `stopReceive()` → `startReceive(newCallback)` |

```mermaid
graph TB
    subgraph stopReceive 后的状态
        A1[可以发送] 
        A2[不能接收]
        A3[可以重新 startReceive]
    end
    
    subgraph close 后的状态
        B1[不能发送]
        B2[不能接收]
        B3[对象应该被销毁]
    end
```

---

## 6. 线程安全设计

### 6.1 哪些操作是线程安全的？

| 方法 | 线程安全 | 说明 |
|------|----------|------|
| `send()` | ✅ 是 | 可以从任何线程调用 |
| `startReceive()` | ⚠️ 部分 | 只能调用一次，需要保证不并发调用 |
| `stopReceive()` | ✅ 是 | 可以从任何线程调用 |
| `close()` | ✅ 是 | 可以从任何线程调用 |
| `getStatistics()` | ✅ 是 | 有锁保护 |

### 6.2 为什么 send() 是线程安全的？

```cpp
void UdpClient::send(const UdpEndpoint& endpoint, 
                     const std::vector<uint8_t>& data,
                     SendCallback callback) {
    // asio::async_send_to 本身是线程安全的
    // 它会把操作提交到 io_context 的队列中
    socket_.async_send_to(asio::buffer(data), resolve_endpoint(endpoint), ...);
}
```

asio 的设计保证了：
- `async_send_to` 可以从任何线程调用
- 实际的发送操作会在 io_context 线程中执行
- 多个发送操作会排队执行，不会相互干扰

### 6.3 统计信息的线程安全

```cpp
class UdpClient {
    mutable std::mutex stats_mutex_;  // 为什么用 mutable？
    Statistics statistics_;
    
    void updateSendStats(size_t bytes, bool success) {
        std::lock_guard<std::mutex> lock(stats_mutex_);  // 加锁
        statistics_.bytes_sent += bytes;
        if (success) statistics_.messages_sent++;
        else statistics_.send_errors++;
    }
    
    Statistics getStatistics() const {
        std::lock_guard<std::mutex> lock(stats_mutex_);  // const 方法也需要锁
        return statistics_;  // 返回拷贝
    }
};
```

**为什么 mutex 是 mutable？**

`getStatistics()` 是 const 方法，但需要加锁。`mutable` 允许在 const 方法中修改 mutex。

**为什么返回拷贝而不是引用？**

```cpp
// 错误做法：返回引用
const Statistics& getStatistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return statistics_;  // 锁释放后，调用者访问 statistics_ 就不安全了！
}

// 正确做法：返回拷贝
Statistics getStatistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return statistics_;  // 返回的是拷贝，锁释放后调用者访问的是自己的拷贝
}
```

---

## 7. 类图

```mermaid
classDiagram
    class UdpEndpoint {
        +string ip
        +uint16_t port
        +UdpEndpoint()
        +UdpEndpoint(string ip, uint16_t port)
        +toString() string
        +isValid() bool
    }
    note for UdpEndpoint "UDP通信端点\n- ip可以是IPv4/IPv6/域名\n- port为0表示无效"
    
    class UdpMessage {
        +vector~uint8_t~ data
        +UdpEndpoint remote_endpoint
        +size() size_t
        +empty() bool
    }
    note for UdpMessage "接收到的UDP消息\n- 包含数据和来源地址\n- 用于回调函数参数"
    
    class Statistics {
        +size_t bytes_sent
        +size_t bytes_received
        +size_t messages_sent
        +size_t messages_received
        +size_t send_errors
        +size_t receive_errors
        +reset() void
    }
    note for Statistics "统计信息\n- 用于监控和调试\n- 所有字段都是累计值"
    
    class UdpClient {
        -io_context& io_context_
        -udp::socket socket_
        -atomic~bool~ receiving_
        -ReceiveCallback receive_callback_
        -array~uint8_t,65536~ receive_buffer_
        -udp::endpoint remote_endpoint_
        -mutex stats_mutex_
        -Statistics statistics_
        
        +UdpClient(io_context, local_port)
        +~UdpClient()
        +send(endpoint, data, callback) void
        +startReceive(callback) void
        +stopReceive() void
        +close() void
        +localPort() uint16_t
        +isReceiving() bool
        +getStatistics() Statistics
        +resetStatistics() void
        
        -doReceive() void
        -handleReceive(ec, bytes) void
        -updateSendStats(bytes, success) void
        -updateReceiveStats(bytes, success) void
        -resolveEndpoint(endpoint) udp::endpoint
    }
    note for UdpClient "核心类\n- 封装asio UDP socket\n- 提供异步收发功能\n- 线程安全"
    
    UdpClient *-- Statistics : 包含
    UdpClient ..> UdpEndpoint : 使用
    UdpClient ..> UdpMessage : 创建
    UdpMessage *-- UdpEndpoint : 包含
```

---

## 8. 时序图

### 8.1 发送消息时序

```mermaid
sequenceDiagram
    participant App as 应用层
    participant UDP as UdpClient
    participant Socket as asio::socket
    participant OS as 操作系统
    participant Net as 网络
    
    App->>+UDP: send(endpoint, data, callback)
    
    Note over UDP: 解析endpoint（可能涉及DNS）
    
    UDP->>+Socket: async_send_to(buffer, endpoint, handler)
    Note right of Socket: 异步操作<br/>立即返回
    Socket-->>-UDP: (操作已提交)
    
    UDP-->>-App: (函数返回，不等待发送完成)
    
    Note over App: 应用可以继续其他工作
    
    Socket->>+OS: 系统调用
    OS->>Net: 发送UDP包
    Net-->>OS: (发送完成)
    OS-->>-Socket: 完成通知
    
    Socket->>+UDP: handler(ec, bytes_sent)
    UDP->>UDP: updateSendStats()
    UDP-->>-App: callback(ec, bytes_sent)
```

### 8.2 接收消息时序

```mermaid
sequenceDiagram
    participant App as 应用层
    participant UDP as UdpClient
    participant Socket as asio::socket
    participant OS as 操作系统
    participant Remote as 远程节点
    
    App->>+UDP: startReceive(callback)
    UDP->>UDP: receiving_ = true
    UDP->>UDP: receive_callback_ = callback
    UDP->>+Socket: async_receive_from(buffer, handler)
    Socket-->>-UDP: (操作已提交)
    UDP-->>-App: (函数返回)
    
    Note over Socket: 等待数据到达...
    
    Remote->>OS: UDP数据包
    OS->>Socket: 数据就绪
    
    Socket->>+UDP: handler(ec, bytes_received)
    UDP->>UDP: updateReceiveStats()
    UDP->>UDP: 构造UdpMessage
    UDP->>App: receive_callback_(message)
    
    Note over UDP: 继续接收下一个包
    UDP->>Socket: async_receive_from(buffer, handler)
    deactivate UDP
```

---

## 9. 流程图

### 9.1 发送流程

```mermaid
flowchart TD
    A[send 被调用] --> B{endpoint 有效?}
    B -->|否| C[记录错误，返回]
    B -->|是| D{endpoint.ip 是域名?}
    
    D -->|是| E[DNS解析]
    E --> F{解析成功?}
    F -->|否| G[调用callback报错]
    F -->|是| H[构造asio::endpoint]
    
    D -->|否| H
    
    H --> I[async_send_to]
    I --> J[等待发送完成...]
    J --> K{发送成功?}
    
    K -->|是| L[更新成功统计]
    K -->|否| M[更新错误统计]
    
    L --> N{callback != null?}
    M --> N
    
    N -->|是| O[调用callback]
    N -->|否| P[结束]
    O --> P
    
    style A fill:#4caf50,color:#fff
    style C fill:#f44336,color:#fff
    style G fill:#f44336,color:#fff
    style P fill:#9e9e9e
```

### 9.2 接收流程

```mermaid
flowchart TD
    A[startReceive 被调用] --> B{已经在接收?}
    B -->|是| C[抛出异常]
    B -->|否| D[receiving_ = true]
    
    D --> E[保存callback]
    E --> F[doReceive]
    
    F --> G[async_receive_from]
    G --> H[等待数据...]
    
    H --> I{接收成功?}
    I -->|是| J[更新成功统计]
    I -->|否| K[更新错误统计]
    
    J --> L[构造UdpMessage]
    L --> M[调用receive_callback]
    
    K --> N{还在接收状态?}
    M --> N
    
    N -->|是| F
    N -->|否| O[结束]
    
    style A fill:#4caf50,color:#fff
    style C fill:#f44336,color:#fff
    style O fill:#9e9e9e
```

---

## 10. 错误处理

### 10.1 错误分类

```mermaid
graph TB
    subgraph 构造时错误
        E1[端口被占用]
        E2[权限不足]
        E3[网络不可用]
    end
    
    subgraph 发送时错误
        E4[目标不可达]
        E5[网络断开]
        E6[缓冲区满]
    end
    
    subgraph 接收时错误
        E7[连接被重置]
        E8[socket已关闭]
    end
    
    E1 --> 抛出异常
    E2 --> 抛出异常
    E3 --> 抛出异常
    
    E4 --> 回调返回错误码
    E5 --> 回调返回错误码
    E6 --> 回调返回错误码
    
    E7 --> 更新统计继续接收
    E8 --> 停止接收
```

### 10.2 错误处理策略

| 错误类型 | 处理方式 | 原因 |
|----------|----------|------|
| 构造失败 | 抛出异常 | 构造失败说明无法正常工作，应该尽早失败 |
| 发送失败 | 回调返回错误码 | 调用者可能需要重试或通知用户 |
| 接收失败 | 记录统计，继续接收 | 网络偶发错误不应该中断服务 |

---

## 11. 实现检查清单

### 11.1 必须实现的功能

- [ ] 构造函数：创建socket、绑定端口、设置选项
- [ ] send()：解析地址、异步发送、统计更新、回调调用
- [ ] startReceive()：状态检查、保存回调、启动接收循环
- [ ] stopReceive()：更新状态、取消当前接收操作
- [ ] close()：停止接收、关闭socket
- [ ] 统计功能：线程安全的统计更新和查询

### 11.2 边界条件处理

- [ ] endpoint 为空或无效
- [ ] data 为空
- [ ] callback 为 nullptr
- [ ] 重复调用 startReceive()
- [ ] 对已关闭的 socket 调用 send()
- [ ] DNS 解析失败

### 11.3 测试要点

- [ ] 基本发送接收测试
- [ ] 本地回环测试
- [ ] 多线程并发发送测试
- [ ] 统计信息准确性测试
- [ ] 错误处理测试
- [ ] 生命周期测试（对象销毁时有待处理的异步操作）

---

**设计文档完成，可以开始实现！**

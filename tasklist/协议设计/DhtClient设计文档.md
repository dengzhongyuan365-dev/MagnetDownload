# DhtClient 设计文档

> **模块名称**：DhtClient - DHT 主控制器  
> **版本**：v1.0  
> **最后更新**：2025-01-12  
> **作者**：MagnetDownload Team

---

## 1. 承上启下：DhtClient 的角色

### 1.1 已完成模块回顾

在实现 DhtClient 之前，我们已经完成了 DHT 协议栈的所有基础组件：

```mermaid
graph TB
    subgraph 已完成["已完成的模块 ✅"]
        UDP[UdpClient<br/>UDP 收发]
        MSG[DhtMessage<br/>消息构造/解析]
        RT[RoutingTable<br/>路由表]
        QM[QueryManager<br/>查询管理]
        BEN[Bencode<br/>编解码]
        NODE[NodeId/DhtNode<br/>节点标识]
    end
    
    subgraph 待实现["待实现 ❌"]
        CLIENT[DhtClient<br/>主控制器]
    end
    
    CLIENT --> QM
    CLIENT --> RT
    CLIENT --> MSG
    QM --> UDP
    MSG --> BEN
    RT --> NODE
    
    style CLIENT fill:#ff9800,stroke:#e65100,stroke-width:3px,color:#fff
```

| 模块 | 职责 | 状态 |
|------|------|------|
| **UdpClient** | UDP 数据包收发 | ✅ |
| **Bencode** | 消息编解码 | ✅ |
| **NodeId/DhtNode** | 节点标识和信息 | ✅ |
| **DhtMessage** | KRPC 消息构造和解析 | ✅ |
| **RoutingTable** | K-Bucket 路由表 | ✅ |
| **QueryManager** | 查询超时和重试管理 | ✅ |
| **DhtClient** | **整合所有模块，提供完整 DHT 功能** | ❌ |

### 1.2 DhtClient 要解决的问题

**用户（应用层）的需求很简单**：

```cpp
// 用户只想做这件事：
dht_client.findPeers(info_hash, [](PeerInfo peer) {
    // 拿到 peer，开始下载
    connectAndDownload(peer);
});
```

**但底层需要协调多个模块**：

```mermaid
sequenceDiagram
    participant App as 应用层
    participant DHT as DhtClient
    participant QM as QueryManager
    participant RT as RoutingTable
    participant MSG as DhtMessage
    participant UDP as UdpClient
    participant Net as DHT网络
    
    App->>DHT: findPeers(info_hash)
    
    Note over DHT: 1. 从路由表获取初始节点
    DHT->>RT: findClosest(info_hash)
    RT-->>DHT: [node1, node2, node3...]
    
    Note over DHT: 2. 构造查询消息
    DHT->>MSG: createGetPeers(my_id, info_hash)
    MSG-->>DHT: message
    
    Note over DHT: 3. 发送查询（自动超时重试）
    DHT->>QM: sendQuery(node1, message, callback)
    QM->>UDP: send(endpoint, data)
    UDP->>Net: UDP packet
    
    Note over DHT: 4. 处理响应
    Net->>UDP: UDP packet
    UDP->>DHT: onReceive(data)
    DHT->>MSG: parse(data)
    MSG-->>DHT: response
    DHT->>QM: handleResponse(response)
    QM-->>DHT: callback(result)
    
    Note over DHT: 5. 更新路由表 & 继续查找
    DHT->>RT: addNode(responding_node)
    DHT->>RT: addNodes(closer_nodes)
    
    alt 找到 Peers
        DHT-->>App: callback(peer)
    else 找到更近的节点
        DHT->>DHT: 继续向更近的节点查询
    end
```

**DhtClient 的核心价值**：
- **统一入口**：对外提供简单的 `findPeers()` API
- **协调模块**：内部协调 QueryManager、RoutingTable、DhtMessage
- **实现算法**：实现迭代查找算法（α-并发、收敛检测）
- **状态管理**：管理 DHT 客户端的生命周期

---

## 2. 设计目标

### 2.1 功能目标

| 功能 | 说明 | 优先级 |
|------|------|--------|
| **findPeers** | 根据 InfoHash 查找拥有文件的 Peer | 高 |
| **bootstrap** | 加入 DHT 网络（从引导节点开始） | 高 |
| **announce** | 宣告自己拥有某个文件 | 中 |
| **维护** | 定期刷新路由表，保持在线状态 | 中 |

### 2.2 非功能目标

| 目标 | 说明 |
|------|------|
| **异步** | 所有操作都是异步的，不阻塞调用线程 |
| **高效** | 并发查询（α=3），避免重复查询 |
| **健壮** | 处理网络错误、恶意节点、超时 |
| **可观测** | 提供统计信息和日志 |

---

## 3. 架构设计

### 3.1 整体架构

```mermaid
graph TB
    subgraph 应用层
        APP[DownloadManager]
    end
    
    subgraph DhtClient["DhtClient 主控制器"]
        API[公共 API<br/>findPeers / bootstrap / announce]
        LOOKUP[LookupManager<br/>迭代查找算法]
        RECV[消息接收处理]
        MAINT[维护任务<br/>刷新/清理]
    end
    
    subgraph 基础模块
        QM[QueryManager]
        RT[RoutingTable]
        MSG[DhtMessage]
        UDP[UdpClient]
    end
    
    APP --> API
    API --> LOOKUP
    API --> MAINT
    
    LOOKUP --> QM
    LOOKUP --> RT
    LOOKUP --> MSG
    
    RECV --> MSG
    RECV --> RT
    RECV --> QM
    
    MAINT --> RT
    MAINT --> QM
    
    QM --> UDP
    UDP --> RECV
    
    style API fill:#4caf50,color:#fff
    style LOOKUP fill:#2196f3,color:#fff
```

### 3.2 模块交互

```mermaid
flowchart LR
    subgraph DhtClient
        direction TB
        A[findPeers]
        B[LookupState]
        C[onResponse]
    end
    
    subgraph QueryManager
        D[sendQuery]
        E[handleResponse]
    end
    
    subgraph RoutingTable
        F[findClosest]
        G[addNode]
    end
    
    subgraph UdpClient
        H[send]
        I[receive]
    end
    
    A -->|1. 获取初始节点| F
    A -->|2. 发送查询| D
    D -->|3. UDP发送| H
    I -->|4. 收到数据| C
    C -->|5. 匹配查询| E
    E -->|6. 回调| B
    B -->|7. 更新路由表| G
    B -->|8. 继续查询| D
```

---

## 4. 核心数据结构

### 4.1 DhtClient 配置

```cpp
struct DhtClientConfig {
    uint16_t listen_port{6881};              // 监听端口
    size_t alpha{3};                          // 并发查询数
    size_t k{8};                              // 每次查询返回的节点数
    std::chrono::seconds refresh_interval{15min};  // 路由表刷新间隔
    std::chrono::seconds announce_interval{30min}; // 重新宣告间隔
    
    // Bootstrap 节点
    std::vector<std::pair<std::string, uint16_t>> bootstrap_nodes{
        {"router.bittorrent.com", 6881},
        {"dht.transmissionbt.com", 6881},
        {"router.utorrent.com", 6881}
    };
};
```

### 4.2 Lookup 状态（迭代查找）

```cpp
struct LookupState {
    InfoHash target;                         // 查找目标
    NodeId target_id;                        // 目标转换为 NodeId
    
    std::set<NodeId> queried;               // 已查询的节点
    std::set<NodeId> pending;               // 待查询的节点
    std::map<NodeId, DhtNode> candidates;   // 候选节点（按距离排序）
    
    std::vector<PeerInfo> found_peers;      // 找到的 Peers
    std::string token;                       // 用于 announce 的 token
    
    PeerCallback callback;                   // 结果回调
    size_t alpha;                            // 并发数
    bool completed{false};                   // 是否完成
    
    // 检查是否应该继续查找
    bool shouldContinue() const;
    
    // 获取下一批要查询的节点
    std::vector<DhtNode> getNextNodes(size_t count);
    
    // 添加新发现的节点
    void addNodes(const std::vector<DhtNode>& nodes);
    
    // 记录找到的 Peer
    void addPeers(const std::vector<PeerInfo>& peers);
};
```

### 4.3 回调类型

```cpp
// Peer 发现回调（每找到一个 Peer 调用一次）
using PeerCallback = std::function<void(const PeerInfo& peer)>;

// 查找完成回调
using LookupCompleteCallback = std::function<void(
    bool success,
    const std::vector<PeerInfo>& peers
)>;

// Bootstrap 完成回调
using BootstrapCallback = std::function<void(bool success, size_t node_count)>;
```

---

## 5. 类图

```mermaid
classDiagram
    class DhtClientConfig {
        +uint16_t listen_port
        +size_t alpha
        +size_t k
        +seconds refresh_interval
        +vector~pair~ bootstrap_nodes
    }
    
    class LookupState {
        +InfoHash target
        +NodeId target_id
        +set~NodeId~ queried
        +set~NodeId~ pending
        +map~NodeId,DhtNode~ candidates
        +vector~PeerInfo~ found_peers
        +string token
        +PeerCallback callback
        +bool completed
        +shouldContinue() bool
        +getNextNodes(count) vector~DhtNode~
        +addNodes(nodes) void
        +addPeers(peers) void
    }
    
    class DhtClient {
        -io_context& io_context_
        -shared_ptr~UdpClient~ udp_client_
        -shared_ptr~QueryManager~ query_manager_
        -RoutingTable routing_table_
        -NodeId my_id_
        -DhtClientConfig config_
        -map~string,LookupState~ active_lookups_
        -steady_timer refresh_timer_
        -bool running_
        
        +DhtClient(io_context, config)
        +start() void
        +stop() void
        +bootstrap(callback) void
        +findPeers(info_hash, callback) void
        +announce(info_hash, port) void
        +getStatistics() Statistics
        +nodeCount() size_t
        
        -onReceive(message) void
        -handleQuery(message, sender) void
        -handleResponse(message) void
        -handleError(message) void
        -startLookup(target, callback) void
        -continueLookup(lookup_id) void
        -completeLookup(lookup_id) void
        -scheduleRefresh() void
        -refreshRoutingTable() void
        -generateToken(node) string
        -verifyToken(node, token) bool
    }
    
    class QueryManager {
        +sendQuery(node, msg, callback) void
        +handleResponse(response) bool
    }
    
    class RoutingTable {
        +findClosest(target, count) vector~DhtNode~
        +addNode(node) bool
        +markNodeResponded(id) void
        +markNodeFailed(id) void
    }
    
    class UdpClient {
        +send(endpoint, data, callback) void
        +startReceive(callback) void
    }
    
    DhtClient *-- DhtClientConfig
    DhtClient *-- LookupState : 管理多个
    DhtClient --> QueryManager : 使用
    DhtClient --> RoutingTable : 使用
    DhtClient --> UdpClient : 使用
```

---

## 6. 核心流程

### 6.1 Bootstrap 流程（加入 DHT 网络）

```mermaid
flowchart TD
    A[启动 DhtClient] --> B[生成随机 NodeId]
    B --> C[向 Bootstrap 节点发送 find_node]
    C --> D{收到响应?}
    
    D -->|是| E[将响应节点加入路由表]
    E --> F{路由表足够满?}
    
    D -->|超时| G[尝试下一个 Bootstrap 节点]
    G --> H{还有 Bootstrap 节点?}
    H -->|是| C
    H -->|否| I[Bootstrap 失败]
    
    F -->|否| J[向新节点发送 find_node]
    J --> D
    
    F -->|是| K[Bootstrap 成功]
    K --> L[启动定期刷新]
    
    style A fill:#4caf50,color:#fff
    style K fill:#4caf50,color:#fff
    style I fill:#f44336,color:#fff
```

**Bootstrap 的目的**：
1. 让自己被其他节点知道
2. 填充路由表，获取足够的节点信息
3. 为后续的 `findPeers` 做准备

### 6.2 findPeers 流程（迭代查找算法）

```mermaid
flowchart TD
    A[findPeers 被调用] --> B[创建 LookupState]
    B --> C[从路由表获取 K 个最近节点]
    C --> D[选择 α 个节点发送 get_peers]
    
    D --> E{等待响应}
    
    E -->|收到 peers| F[回调通知应用层]
    F --> G{继续查找?}
    
    E -->|收到 nodes| H[将更近的节点加入候选]
    H --> I[标记该节点已查询]
    I --> G
    
    E -->|超时| J[标记节点失败]
    J --> G
    
    G -->|还有更近的未查询节点| K[选择下一批节点查询]
    K --> D
    
    G -->|没有更近的节点| L[查找收敛，完成]
    G -->|达到最大轮次| L
    
    L --> M[回调通知完成]
    
    style A fill:#4caf50,color:#fff
    style F fill:#2196f3,color:#fff
    style L fill:#ff9800,color:#fff
```

### 6.3 迭代查找算法详解

```
输入：目标 InfoHash
输出：拥有该文件的 Peer 列表

1. 初始化
   - 从路由表获取离目标最近的 K 个节点
   - 创建候选集 candidates = {这些节点}
   - 创建已查询集 queried = {}
   - 创建待处理集 pending = {}

2. 循环
   a. 从 candidates 中选择 α 个最近的、未查询的节点
   b. 向这些节点发送 get_peers 查询
   c. 将它们加入 pending
   
   d. 等待响应：
      - 如果收到 peers：
        * 记录 peers，回调通知应用
        * 记录 token（用于后续 announce）
      - 如果收到 nodes：
        * 将比当前最近节点更近的节点加入 candidates
      - 更新 queried 和 pending
   
   e. 终止条件检查：
      - 所有 K 个最近节点都已查询 → 收敛，结束
      - 连续 N 轮没有发现更近的节点 → 收敛，结束
      - 达到最大查询次数 → 超时，结束

3. 返回所有找到的 peers
```

### 6.4 消息接收处理流程

```mermaid
flowchart TD
    A[UDP 收到数据] --> B[Bencode 解码]
    B --> C{解码成功?}
    
    C -->|否| D[丢弃，记录日志]
    C -->|是| E[DhtMessage 解析]
    
    E --> F{消息类型?}
    
    F -->|Query| G[handleQuery]
    F -->|Response| H[handleResponse]
    F -->|Error| I[handleError]
    
    G --> G1{查询类型?}
    G1 -->|ping| G2[回复 pong]
    G1 -->|find_node| G3[返回最近节点]
    G1 -->|get_peers| G4[返回 peers 或 nodes]
    G1 -->|announce_peer| G5[记录 peer 信息]
    
    H --> H1[QueryManager.handleResponse]
    H1 --> H2[触发查询回调]
    H2 --> H3[更新路由表]
    
    I --> I1[记录错误日志]
    I1 --> I2[标记节点可疑]
```

---

## 7. 时序图

### 7.1 完整的 findPeers 时序

```mermaid
sequenceDiagram
    participant App as 应用层
    participant DHT as DhtClient
    participant LS as LookupState
    participant QM as QueryManager
    participant RT as RoutingTable
    participant UDP as UdpClient
    participant Net as DHT网络
    
    App->>DHT: findPeers(info_hash, callback)
    DHT->>LS: 创建 LookupState
    DHT->>RT: findClosest(target, K=8)
    RT-->>DHT: [A, B, C, D, E, F, G, H]
    
    Note over DHT: 第1轮：发送 α=3 个查询
    
    par 并发查询
        DHT->>QM: sendQuery(A, get_peers)
        QM->>UDP: send
        UDP->>Net: to A
    and
        DHT->>QM: sendQuery(B, get_peers)
        QM->>UDP: send
        UDP->>Net: to B
    and
        DHT->>QM: sendQuery(C, get_peers)
        QM->>UDP: send
        UDP->>Net: to C
    end
    
    Net->>UDP: from A (nodes: [X, Y, Z])
    UDP->>DHT: onReceive
    DHT->>QM: handleResponse
    QM-->>DHT: callback
    DHT->>LS: addNodes([X, Y, Z])
    DHT->>RT: addNode(A), addNodes([X, Y, Z])
    
    Net->>UDP: from B (peers: [P1, P2])
    UDP->>DHT: onReceive
    DHT->>QM: handleResponse
    QM-->>DHT: callback
    DHT->>LS: addPeers([P1, P2])
    DHT-->>App: callback(P1)
    DHT-->>App: callback(P2)
    
    Note over DHT: 第2轮：查询更近的节点
    
    DHT->>LS: getNextNodes(α)
    LS-->>DHT: [X, Y]
    
    par 并发查询
        DHT->>QM: sendQuery(X, get_peers)
    and
        DHT->>QM: sendQuery(Y, get_peers)
    end
    
    Note over DHT: ... 继续迭代直到收敛 ...
    
    DHT->>LS: shouldContinue() = false
    DHT->>App: onComplete(true, [P1, P2, ...])
```

### 7.2 响应查询时序

```mermaid
sequenceDiagram
    participant Remote as 远程节点
    participant UDP as UdpClient
    participant DHT as DhtClient
    participant RT as RoutingTable
    participant MSG as DhtMessage
    
    Remote->>UDP: get_peers 查询
    UDP->>DHT: onReceive(data)
    DHT->>MSG: parse(data)
    MSG-->>DHT: query
    
    DHT->>DHT: verifyToken(if announce)
    DHT->>RT: findClosest(info_hash, K)
    RT-->>DHT: closest_nodes
    
    alt 本地有该文件的 Peer 信息
        DHT->>MSG: createGetPeersResponseWithPeers(tid, id, token, peers)
    else 没有 Peer 信息
        DHT->>MSG: createGetPeersResponseWithNodes(tid, id, token, nodes)
    end
    
    MSG-->>DHT: response
    DHT->>UDP: send(response)
    UDP->>Remote: 响应数据
    
    DHT->>RT: addNode(remote)
```

---

## 8. 线程模型

### 8.1 线程分配

```mermaid
graph TB
    subgraph 用户线程
        A[应用层调用<br/>findPeers/bootstrap/announce]
    end
    
    subgraph io_context线程["io_context 线程（可能多个）"]
        B[UDP 接收回调]
        C[QueryManager 超时检查]
        D[定时器回调]
        E[查询结果回调]
    end
    
    subgraph 共享数据
        F[routing_table_]
        G[active_lookups_]
        H[statistics_]
    end
    
    A --> F
    A --> G
    B --> F
    B --> G
    C --> G
    E --> F
    E --> G
    E --> H
```

### 8.2 线程安全设计

```cpp
class DhtClient {
    // 所有公共方法都是线程安全的
    mutable std::mutex mutex_;
    
    // RoutingTable 自带线程安全
    RoutingTable routing_table_;
    
    // active_lookups_ 需要锁保护
    std::map<std::string, LookupState> active_lookups_;
};
```

**原则**：
- 公共 API 方法通过 `asio::post()` 将工作提交到 io_context
- 所有回调都在 io_context 线程中执行
- 共享数据使用 mutex 保护

---

## 9. 错误处理

### 9.1 错误类型

| 错误场景 | 处理方式 |
|----------|----------|
| Bootstrap 失败 | 重试，报告失败 |
| 查询超时 | 标记节点失败，重试其他节点 |
| 恶意响应 | 丢弃，记录日志 |
| 路由表为空 | 重新 Bootstrap |
| 网络不可用 | 暂停查询，等待恢复 |

### 9.2 节点质量管理

```cpp
void DhtClient::onQueryResult(const DhtNode& node, QueryResult result) {
    if (result.is_ok()) {
        routing_table_.markNodeResponded(node.id_);
    } else if (result.error() == QueryError::Timeout) {
        routing_table_.markNodeFailed(node.id_);
        // 如果节点变成 "bad"，从路由表移除
    }
}
```

---

## 10. 配置参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `listen_port` | 6881 | UDP 监听端口 |
| `alpha` | 3 | 并发查询数（BEP-0005 推荐） |
| `k` | 8 | 路由表桶大小 / 返回节点数 |
| `query_timeout` | 2s | 单次查询超时 |
| `max_retries` | 2 | 最大重试次数 |
| `refresh_interval` | 15min | 路由表刷新间隔 |
| `max_lookup_rounds` | 20 | 最大查找轮次 |

---

## 11. 公共 API

```cpp
class DhtClient {
public:
    // 构造和生命周期
    DhtClient(asio::io_context& io_context, DhtClientConfig config = {});
    ~DhtClient();
    
    void start();
    void stop();
    bool isRunning() const;
    
    // 核心功能
    void bootstrap(BootstrapCallback callback = nullptr);
    void findPeers(const InfoHash& info_hash, 
                   PeerCallback on_peer,
                   LookupCompleteCallback on_complete = nullptr);
    void announce(const InfoHash& info_hash, uint16_t port);
    
    // 状态查询
    NodeId localId() const;
    size_t nodeCount() const;
    Statistics getStatistics() const;
};
```

---

## 12. 使用示例

### 12.1 基本使用

```cpp
// 创建并启动
asio::io_context io_context;
DhtClient dht(io_context);
dht.start();

// 加入网络
dht.bootstrap([](bool success, size_t nodes) {
    if (success) {
        std::cout << "Bootstrap 成功，路由表有 " << nodes << " 个节点\n";
    }
});

// 查找 Peers
InfoHash hash = ...;
dht.findPeers(hash,
    // 每找到一个 Peer
    [](const PeerInfo& peer) {
        std::cout << "发现 Peer: " << peer.ip << ":" << peer.port << "\n";
        // 可以立即开始连接
    },
    // 查找完成
    [](bool success, const std::vector<PeerInfo>& all_peers) {
        std::cout << "查找完成，共找到 " << all_peers.size() << " 个 Peers\n";
    }
);

// 运行事件循环
io_context.run();
```

### 12.2 与下载管理器集成

```cpp
class DownloadManager {
    DhtClient dht_;
    
    void download(const std::string& magnet_uri) {
        auto info = MagnetUriParser::parse(magnet_uri);
        if (!info.info_hash) return;
        
        dht_.findPeers(*info.info_hash,
            [this](const PeerInfo& peer) {
                // 立即尝试连接
                connectToPeer(peer);
            }
        );
    }
};
```

---

## 13. 实现检查清单

### 13.1 必须实现

- [ ] 构造函数：初始化所有子模块
- [ ] start() / stop()：管理生命周期
- [ ] bootstrap()：加入 DHT 网络
- [ ] findPeers()：迭代查找算法
- [ ] onReceive()：处理收到的消息
- [ ] handleQuery()：响应其他节点的查询
- [ ] refreshRoutingTable()：定期刷新

### 13.2 可选增强

- [ ] announce()：宣告文件
- [ ] Peer 存储（记住哪些 Peer 有哪些文件）
- [ ] Token 验证（防止滥用 announce）
- [ ] 黑名单（屏蔽恶意节点）

### 13.3 测试要点

- [ ] Bootstrap 成功/失败
- [ ] findPeers 找到/未找到
- [ ] 查询超时和重试
- [ ] 并发查找
- [ ] 响应其他节点的查询
- [ ] 长时间运行稳定性

---

## 14. 依赖关系总结

```mermaid
graph BT
    subgraph 基础层
        ASIO[asio]
        LOG[Logger]
    end
    
    subgraph 网络层
        UDP[UdpClient]
    end
    
    subgraph 协议层
        BEN[Bencode]
        NODE[NodeId/DhtNode]
        MSG[DhtMessage]
        RT[RoutingTable]
        QM[QueryManager]
        DHT[DhtClient]
    end
    
    UDP --> ASIO
    QM --> UDP
    QM --> ASIO
    
    MSG --> BEN
    MSG --> NODE
    RT --> NODE
    
    DHT --> QM
    DHT --> RT
    DHT --> MSG
    DHT --> UDP
    DHT --> LOG
    
    style DHT fill:#ff9800,stroke:#e65100,stroke-width:3px,color:#fff
```

---

**设计文档完成，可以开始实现 DhtClient！**


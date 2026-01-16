# MagnetDownload 多任务架构设计

## 🎯 设计目标

支持同时下载多个磁力链接，实现类似qBittorrent的多任务管理能力。

## 🏗️ 架构重构方案

### 当前架构问题

**单任务架构的限制**：
```cpp
// 当前设计：每个下载一个DownloadController
class DownloadController {
    std::shared_ptr<protocols::DhtClient> dht_client_;      // 每个任务独立DHT
    std::shared_ptr<protocols::PeerManager> peer_manager_;  // 每个任务独立Peer管理
    asio::io_context& io_context_;                          // 共享事件循环
};
```

**多任务场景的问题**：
1. **资源浪费** - 每个任务独立的DHT客户端
2. **连接冲突** - 多个任务争抢网络连接
3. **无法协调** - 任务间无法共享Peer信息
4. **管理困难** - 没有统一的任务管理器

## 🔄 新架构设计

### 1. 全局会话管理器 (SessionManager)

```cpp
namespace magnet::application {

/**
 * @brief 全局会话管理器
 * 
 * 管理所有下载任务，协调资源分配
 */
class SessionManager {
public:
    SessionManager(asio::io_context& io_context);
    ~SessionManager();
    
    // 任务管理
    TaskId addDownload(const std::string& magnet_uri, const DownloadConfig& config);
    bool removeDownload(TaskId task_id);
    bool pauseDownload(TaskId task_id);
    bool resumeDownload(TaskId task_id);
    
    // 状态查询
    std::vector<TaskId> getAllTasks() const;
    DownloadStatus getTaskStatus(TaskId task_id) const;
    DownloadProgress getTaskProgress(TaskId task_id) const;
    
    // 全局配置
    void setGlobalMaxConnections(size_t count);
    void setGlobalUploadLimit(size_t bytes_per_sec);
    void setGlobalDownloadLimit(size_t bytes_per_sec);
    
    // 回调设置
    void setTaskStateCallback(TaskStateCallback callback);
    void setTaskProgressCallback(TaskProgressCallback callback);

private:
    asio::io_context& io_context_;
    
    // 全局资源
    std::shared_ptr<protocols::DhtClient> global_dht_;
    std::shared_ptr<network::ConnectionPool> connection_pool_;
    std::shared_ptr<async::TaskScheduler> task_scheduler_;
    
    // 任务管理
    std::map<TaskId, std::unique_ptr<DownloadTask>> tasks_;
    mutable std::mutex tasks_mutex_;
    
    // 资源分配
    void redistributeResources();
    void balanceBandwidth();
};

} // namespace magnet::application
```

### 2. 下载任务 (DownloadTask)

```cpp
/**
 * @brief 单个下载任务
 * 
 * 不再独立管理资源，而是使用全局共享资源
 */
class DownloadTask {
public:
    DownloadTask(TaskId id, const std::string& magnet_uri, 
                 const DownloadConfig& config, SessionManager* session);
    
    // 生命周期
    void start();
    void pause();
    void resume();
    void stop();
    
    // 状态查询
    DownloadState state() const { return state_.load(); }
    DownloadProgress progress() const;
    TorrentMetadata metadata() const;
    
    // 资源分配（由SessionManager调用）
    void setMaxConnections(size_t count);
    void setUploadLimit(size_t bytes_per_sec);
    void setDownloadLimit(size_t bytes_per_sec);

private:
    TaskId task_id_;
    std::string magnet_uri_;
    DownloadConfig config_;
    SessionManager* session_manager_;
    
    // 任务状态
    std::atomic<DownloadState> state_{DownloadState::Idle};
    
    // 元数据和分片管理
    mutable std::mutex metadata_mutex_;
    TorrentMetadata metadata_;
    std::vector<PieceInfo> pieces_;
    
    // 使用全局资源（不拥有）
    protocols::DhtClient* dht_client_;
    network::ConnectionPool* connection_pool_;
    async::TaskScheduler* task_scheduler_;
    
    // 任务特定的管理器
    std::unique_ptr<TaskPeerManager> peer_manager_;
    std::unique_ptr<storage::FileManager> file_manager_;
};
```

### 3. 任务调度器 (TaskScheduler) - 现在必需！

```cpp
namespace magnet::async {

/**
 * @brief 多任务优先级调度器
 * 
 * 在多任务场景下协调不同任务的资源使用
 */
class TaskScheduler {
public:
    explicit TaskScheduler(EventLoopManager& loop_manager);
    
    // 任务优先级
    enum class TaskPriority {
        CRITICAL = 0,  // 用户交互（暂停/恢复/删除）
        HIGH     = 1,  // 活跃任务的DHT查询、Peer连接
        NORMAL   = 2,  // 数据传输、文件写入
        LOW      = 3   // 暂停任务的维护、统计收集
    };
    
    // 任务投递
    TaskId post_task(TaskPriority priority, TaskFunction func);
    TaskId post_task_for_download(TaskId download_id, TaskPriority priority, TaskFunction func);
    
    // 任务管理
    bool cancel_task(TaskId task_id);
    bool cancel_tasks_for_download(TaskId download_id);
    
    // 优先级调整（用于任务暂停/恢复）
    void setDownloadPriority(TaskId download_id, TaskPriority base_priority);
    
private:
    // 按下载任务分组的任务队列
    struct DownloadTaskGroup {
        TaskId download_id;
        TaskPriority base_priority;
        std::queue<std::shared_ptr<Task>> tasks;
    };
    
    std::map<TaskId, DownloadTaskGroup> download_groups_;
    // ... 调度逻辑
};

} // namespace magnet::async
```

### 4. 连接池管理器 (ConnectionPool)

```cpp
namespace magnet::network {

/**
 * @brief 全局连接池管理器
 * 
 * 在多个下载任务间分配和复用TCP连接
 */
class ConnectionPool {
public:
    ConnectionPool(asio::io_context& io_context, size_t max_connections);
    
    // 连接请求
    void requestConnection(const TcpEndpoint& endpoint, 
                          TaskId requester,
                          ConnectionCallback callback);
    
    // 连接释放
    void releaseConnection(const TcpEndpoint& endpoint, TaskId owner);
    
    // 资源分配
    void setTaskMaxConnections(TaskId task_id, size_t max_count);
    void redistributeConnections();
    
    // 统计信息
    size_t getTotalConnections() const;
    size_t getTaskConnections(TaskId task_id) const;

private:
    struct ConnectionInfo {
        std::shared_ptr<TcpClient> client;
        TaskId owner;
        std::chrono::steady_clock::time_point last_used;
        bool in_use;
    };
    
    asio::io_context& io_context_;
    size_t max_total_connections_;
    
    // 连接池
    std::map<TcpEndpoint, ConnectionInfo> connections_;
    
    // 任务连接分配
    std::map<TaskId, size_t> task_max_connections_;
    std::map<TaskId, std::set<TcpEndpoint>> task_connections_;
    
    mutable std::mutex pool_mutex_;
};

} // namespace magnet::network
```

### 5. 全局DHT客户端增强

```cpp
namespace magnet::protocols {

/**
 * @brief 增强的DHT客户端
 * 
 * 支持多任务的Peer查找和信息共享
 */
class DhtClient {
public:
    // 现有接口保持不变...
    
    // 多任务支持
    void findPeersForTask(TaskId task_id, const InfoHash& info_hash,
                         PeerCallback on_peer, LookupCompleteCallback on_complete);
    
    void cancelLookupForTask(TaskId task_id);
    
    // Peer信息共享
    void sharePeerInfo(const InfoHash& info_hash, const std::vector<PeerInfo>& peers);
    std::vector<PeerInfo> getCachedPeers(const InfoHash& info_hash);

private:
    // 按任务分组的查找
    std::map<TaskId, std::set<std::string>> task_lookups_;
    
    // Peer信息缓存（跨任务共享）
    std::map<InfoHash, std::vector<PeerInfo>> peer_cache_;
    mutable std::mutex peer_cache_mutex_;
};

} // namespace magnet::protocols
```

## 🔄 数据流重构

### 多任务下载流程

```
用户添加多个磁力链接
    ↓
┌─────────────────────────────────────────────────────────────┐
│ SessionManager 创建多个 DownloadTask                         │
└─────────────────────────────────────────────────────────────┘
    ↓
┌─────────────────────────────────────────────────────────────┐
│ 全局资源分配                                                 │
│ ├─ DHT客户端：并发查找多个InfoHash                           │
│ ├─ 连接池：在任务间分配TCP连接                               │
│ └─ 任务调度器：按优先级调度任务                               │
└─────────────────────────────────────────────────────────────┘
    ↓
┌─────────────────────────────────────────────────────────────┐
│ 并发下载执行                                                 │
│ ├─ 任务A：高优先级（用户正在查看）                           │
│ ├─ 任务B：正常优先级（后台下载）                             │
│ └─ 任务C：低优先级（已暂停）                                 │
└─────────────────────────────────────────────────────────────┘
    ↓
┌─────────────────────────────────────────────────────────────┐
│ 动态资源调整                                                 │
│ ├─ 用户暂停任务A → 释放连接给任务B                           │
│ ├─ 任务B完成 → 连接分配给新任务                             │
│ └─ 网络拥塞 → 降低所有任务优先级                             │
└─────────────────────────────────────────────────────────────┘
```

## 📊 资源分配策略

### 1. 连接分配算法

```cpp
class ConnectionDistributor {
    struct TaskInfo {
        TaskId id;
        DownloadState state;
        size_t requested_connections;
        size_t current_connections;
        double priority_weight;  // 基于用户优先级和下载进度
    };
    
    void redistributeConnections() {
        // 1. 收集所有活跃任务信息
        std::vector<TaskInfo> active_tasks = getActiveTasks();
        
        // 2. 按优先级权重排序
        std::sort(active_tasks.begin(), active_tasks.end(),
                 [](const TaskInfo& a, const TaskInfo& b) {
                     return a.priority_weight > b.priority_weight;
                 });
        
        // 3. 分配连接（高优先级任务优先）
        size_t available_connections = max_total_connections_;
        for (auto& task : active_tasks) {
            size_t allocated = std::min(task.requested_connections, 
                                       available_connections);
            allocateConnectionsToTask(task.id, allocated);
            available_connections -= allocated;
        }
    }
};
```

### 2. 带宽分配算法

```cpp
class BandwidthManager {
    void distributeBandwidth() {
        // 1. 获取全局带宽限制
        size_t total_download_limit = global_config_.max_download_speed;
        size_t total_upload_limit = global_config_.max_upload_speed;
        
        // 2. 按任务优先级分配
        auto active_tasks = getActiveTasks();
        size_t high_priority_count = 0;
        size_t normal_priority_count = 0;
        
        for (const auto& task : active_tasks) {
            if (task.priority == TaskPriority::HIGH) high_priority_count++;
            else normal_priority_count++;
        }
        
        // 3. 高优先级任务获得更多带宽
        size_t high_priority_bandwidth = total_download_limit * 0.7 / high_priority_count;
        size_t normal_priority_bandwidth = total_download_limit * 0.3 / normal_priority_count;
        
        // 4. 应用限制
        for (auto& task : active_tasks) {
            size_t allocated = (task.priority == TaskPriority::HIGH) ? 
                              high_priority_bandwidth : normal_priority_bandwidth;
            task.setDownloadLimit(allocated);
        }
    }
};
```

## 🎯 实现优先级（多任务场景）

### 🔴 立即需要实现：

1. **SessionManager** - 多任务管理核心
2. **TaskScheduler** - 任务优先级调度（现在必需！）
3. **ConnectionPool** - 连接资源管理
4. **DownloadTask重构** - 从独立控制器改为任务

### 🟡 中期需要实现：

5. **BandwidthManager** - 带宽分配管理
6. **TaskPersistence** - 任务持久化（重启后恢复）
7. **Config增强** - 支持多任务配置

### 🟢 未来扩展：

8. **TaskPriority动态调整** - 用户可调整任务优先级
9. **智能调度** - 基于网络状况的自适应调度
10. **任务分组** - 支持任务分类和批量操作

## 🔄 迁移策略

### 阶段1：保持兼容性
```cpp
// 保留现有DownloadController作为单任务接口
class DownloadController {
    // 内部使用SessionManager
    static SessionManager* global_session_;
    TaskId task_id_;
    
public:
    bool start(const DownloadConfig& config) {
        task_id_ = global_session_->addDownload(config.magnet_uri, config);
        return task_id_ != INVALID_TASK_ID;
    }
};
```

### 阶段2：新增多任务接口
```cpp
// 新的多任务接口
class MultiTaskDownloader {
    SessionManager session_manager_;
    
public:
    TaskId addDownload(const std::string& magnet_uri);
    std::vector<DownloadStatus> getAllDownloads();
    // ...
};
```

### 阶段3：统一接口
```cpp
// 最终统一接口
class MagnetDownloader {
    SessionManager session_manager_;
    
public:
    // 单任务便捷接口
    TaskId download(const std::string& magnet_uri, const std::string& save_path);
    
    // 多任务管理接口
    TaskId addDownload(const std::string& magnet_uri, const DownloadConfig& config);
    void removeDownload(TaskId task_id);
    // ...
};
```

## 📋 总结

如果要支持多任务场景，你需要进行**重大架构重构**：

### 必需的新模块：
1. ✅ **SessionManager** - 全局任务管理
2. ✅ **TaskScheduler** - 现在确实需要了！
3. ✅ **ConnectionPool** - 连接资源管理
4. ✅ **DownloadTask** - 重构后的任务类

### 需要增强的模块：
5. ✅ **DhtClient** - 支持多任务查找
6. ✅ **Config** - 支持全局和任务级配置

### 架构复杂度：
- 📈 **大幅增加** - 从简单的单任务控制器变为复杂的多任务系统
- 🔄 **需要重构** - 现有代码需要大量修改
- ⏱️ **开发时间** - 预计需要2-3周重构

**你确定要支持多任务吗？** 这会让项目复杂度提升一个数量级！
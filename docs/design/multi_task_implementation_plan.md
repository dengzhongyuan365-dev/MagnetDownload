# MagnetDownload 多任务架构实现计划

## 🎯 项目目标

将MagnetDownload从单任务下载器升级为功能完整的多任务BitTorrent客户端，支持：
- 同时下载多个磁力链接
- 智能资源分配和调度
- 任务优先级管理
- 高性能和稳定性

## 🏗️ 架构升级路线图

### 阶段1：核心基础设施 (1-2周)

#### 1.1 TaskScheduler - 任务调度器
**优先级**：🔴 最高
**依赖**：EventLoopManager

```cpp
// 目标接口
class TaskScheduler {
    enum class Priority { CRITICAL, HIGH, NORMAL, LOW };
    
    TaskId post_task(Priority priority, TaskFunction func);
    TaskId post_delayed_task(Duration delay, Priority priority, TaskFunction func);
    TaskId post_periodic_task(Duration interval, Priority priority, TaskFunction func);
    
    // 多任务支持
    void setDownloadPriority(TaskId download_id, Priority base_priority);
    void cancel_tasks_for_download(TaskId download_id);
    
    // 老化防饥饿
    void enable_aging(Duration aging_interval);
};
```

**实现要点**：
- 四级优先级队列
- 任务老化机制防止饥饿
- 按下载任务分组管理
- 与EventLoopManager集成

#### 1.2 ConnectionPool - 连接池管理器
**优先级**：🔴 最高
**依赖**：TcpClient, TaskScheduler

```cpp
class ConnectionPool {
    ConnectionPool(asio::io_context& io_context, size_t max_connections);
    
    // 连接请求
    void requestConnection(const TcpEndpoint& endpoint, TaskId requester,
                          ConnectionCallback callback);
    void releaseConnection(const TcpEndpoint& endpoint, TaskId owner);
    
    // 资源分配
    void setTaskMaxConnections(TaskId task_id, size_t max_count);
    void redistributeConnections();
    
    // 连接复用
    std::shared_ptr<TcpClient> getOrCreateConnection(const TcpEndpoint& endpoint);
    void returnConnection(const TcpEndpoint& endpoint, std::shared_ptr<TcpClient> client);
};
```

**实现要点**：
- 连接复用和生命周期管理
- 按任务分配连接配额
- 动态资源重分配
- 连接健康检查和自动重连

#### 1.3 增强DhtClient - 多任务DHT支持
**优先级**：🔴 最高
**依赖**：现有DhtClient

```cpp
class DhtClient {
    // 现有接口保持不变...
    
    // 多任务支持
    void findPeersForTask(TaskId task_id, const InfoHash& info_hash,
                         PeerCallback on_peer, LookupCompleteCallback on_complete);
    void cancelLookupForTask(TaskId task_id);
    
    // Peer信息共享
    void sharePeerInfo(const InfoHash& info_hash, const std::vector<PeerInfo>& peers);
    std::vector<PeerInfo> getCachedPeers(const InfoHash& info_hash);
    
    // 统计信息
    DhtStatistics getTaskStatistics(TaskId task_id);
};
```

**实现要点**：
- 按任务分组的查找管理
- Peer信息跨任务缓存和共享
- 查找优先级（活跃任务优先）
- 任务取消时的资源清理

### 阶段2：任务管理核心 (1-2周)

#### 2.1 SessionManager - 全局会话管理器
**优先级**：🔴 最高
**依赖**：TaskScheduler, ConnectionPool, DhtClient

```cpp
class SessionManager {
    SessionManager(asio::io_context& io_context, const SessionConfig& config);
    
    // 任务管理
    TaskId addDownload(const std::string& magnet_uri, const DownloadConfig& config);
    bool removeDownload(TaskId task_id);
    bool pauseDownload(TaskId task_id);
    bool resumeDownload(TaskId task_id);
    
    // 批量操作
    std::vector<TaskId> addMultipleDownloads(const std::vector<std::string>& magnet_uris);
    void pauseAllDownloads();
    void resumeAllDownloads();
    
    // 状态查询
    std::vector<TaskId> getAllTasks() const;
    DownloadStatus getTaskStatus(TaskId task_id) const;
    std::vector<DownloadStatus> getAllTaskStatuses() const;
    
    // 全局配置
    void setGlobalMaxConnections(size_t count);
    void setGlobalUploadLimit(size_t bytes_per_sec);
    void setGlobalDownloadLimit(size_t bytes_per_sec);
    
    // 任务优先级
    void setTaskPriority(TaskId task_id, TaskPriority priority);
    void moveTaskUp(TaskId task_id);
    void moveTaskDown(TaskId task_id);
    
    // 回调设置
    void setTaskStateCallback(TaskStateCallback callback);
    void setTaskProgressCallback(TaskProgressCallback callback);
    void setGlobalStatsCallback(GlobalStatsCallback callback);
};
```

**实现要点**：
- 任务生命周期管理
- 全局资源协调
- 任务优先级动态调整
- 事件通知系统

#### 2.2 DownloadTask - 重构后的下载任务
**优先级**：🔴 最高
**依赖**：SessionManager, 现有DownloadController逻辑

```cpp
class DownloadTask {
    DownloadTask(TaskId id, const std::string& magnet_uri, 
                 const DownloadConfig& config, SessionManager* session);
    
    // 生命周期
    void start();
    void pause();
    void resume();
    void stop();
    
    // 状态查询
    TaskId id() const { return task_id_; }
    DownloadState state() const { return state_.load(); }
    DownloadProgress progress() const;
    TorrentMetadata metadata() const;
    
    // 资源分配（由SessionManager调用）
    void setMaxConnections(size_t count);
    void setUploadLimit(size_t bytes_per_sec);
    void setDownloadLimit(size_t bytes_per_sec);
    void setPriority(TaskPriority priority);
    
    // 统计信息
    TaskStatistics getStatistics() const;
    
private:
    TaskId task_id_;
    std::string magnet_uri_;
    DownloadConfig config_;
    SessionManager* session_manager_;
    
    // 任务状态
    std::atomic<DownloadState> state_{DownloadState::Idle};
    std::atomic<TaskPriority> priority_{TaskPriority::NORMAL};
    
    // 使用全局资源（不拥有）
    protocols::DhtClient* dht_client_;
    network::ConnectionPool* connection_pool_;
    async::TaskScheduler* task_scheduler_;
    
    // 任务特定的管理器
    std::unique_ptr<TaskPeerManager> peer_manager_;
    std::unique_ptr<storage::FileManager> file_manager_;
    std::unique_ptr<TaskMetadataManager> metadata_manager_;
};
```

**实现要点**：
- 从DownloadController迁移核心逻辑
- 使用全局共享资源而不是独立资源
- 支持动态优先级调整
- 详细的统计信息收集

### 阶段3：高级功能 (1-2周)

#### 3.1 BandwidthManager - 带宽管理器
**优先级**：🟡 中等
**依赖**：SessionManager

```cpp
class BandwidthManager {
    BandwidthManager(SessionManager* session);
    
    // 全局限制
    void setGlobalDownloadLimit(size_t bytes_per_sec);
    void setGlobalUploadLimit(size_t bytes_per_sec);
    
    // 任务限制
    void setTaskDownloadLimit(TaskId task_id, size_t bytes_per_sec);
    void setTaskUploadLimit(TaskId task_id, size_t bytes_per_sec);
    
    // 智能分配
    void enableSmartAllocation(bool enable);
    void redistributeBandwidth();
    
    // 统计信息
    BandwidthStatistics getGlobalStats() const;
    BandwidthStatistics getTaskStats(TaskId task_id) const;
    
private:
    // 分配算法
    void distributeBandwidthByPriority();
    void distributeBandwidthByProgress();
    void distributeBandwidthByPeerCount();
};
```

**实现要点**：
- 全局和任务级带宽限制
- 智能带宽分配算法
- 实时带宽监控和调整
- 支持多种分配策略

#### 3.2 TaskPersistence - 任务持久化
**优先级**：🟡 中等
**依赖**：SessionManager

```cpp
class TaskPersistence {
    TaskPersistence(const std::string& data_dir);
    
    // 任务保存/加载
    bool saveTask(const DownloadTask& task);
    std::vector<TaskInfo> loadAllTasks();
    bool removeTask(TaskId task_id);
    
    // 会话保存/恢复
    bool saveSession(const SessionManager& session);
    bool restoreSession(SessionManager& session);
    
    // 统计信息持久化
    bool saveStatistics(const GlobalStatistics& stats);
    GlobalStatistics loadStatistics();
    
private:
    std::string data_dir_;
    
    // 文件格式
    bool saveTaskToJson(const DownloadTask& task, const std::string& file_path);
    TaskInfo loadTaskFromJson(const std::string& file_path);
};
```

**实现要点**：
- JSON格式的任务信息存储
- 增量保存（只保存变化的任务）
- 会话恢复（重启后继续下载）
- 统计信息历史记录

#### 3.3 Config增强 - 多任务配置管理
**优先级**：🟡 中等
**依赖**：无

```cpp
class Config {
    // 全局配置
    struct GlobalConfig {
        size_t max_total_connections{200};
        size_t max_download_speed{0};  // 0 = unlimited
        size_t max_upload_speed{0};
        size_t max_concurrent_tasks{10};
        std::string default_save_path{"./downloads"};
        bool enable_dht{true};
        bool enable_pex{true};
    };
    
    // 任务默认配置
    struct TaskDefaultConfig {
        size_t max_connections_per_task{50};
        size_t max_download_speed_per_task{0};
        size_t max_upload_speed_per_task{0};
        TaskPriority default_priority{TaskPriority::NORMAL};
        bool auto_start{true};
        bool verify_on_complete{true};
    };
    
    // 配置管理
    bool loadFromFile(const std::string& config_file);
    bool saveToFile(const std::string& config_file);
    bool loadFromEnv();
    
    // 运行时配置更新
    void updateGlobalConfig(const GlobalConfig& config);
    void updateTaskDefaultConfig(const TaskDefaultConfig& config);
    
    // 配置验证
    bool validateConfig() const;
    std::vector<std::string> getConfigErrors() const;
};
```

**实现要点**：
- 分层配置（全局、任务默认、任务特定）
- 多种配置源（文件、环境变量、命令行）
- 运行时配置更新
- 配置验证和错误报告

### 阶段4：用户界面升级 (1周)

#### 4.1 多任务CLI界面
**优先级**：🟡 中等
**依赖**：SessionManager

```cpp
class MultiTaskCLI {
    MultiTaskCLI(SessionManager& session);
    
    // 命令处理
    int run(int argc, char* argv[]);
    
private:
    // 交互式模式
    void runInteractiveMode();
    void showMainMenu();
    void showTaskList();
    void showTaskDetails(TaskId task_id);
    
    // 命令处理
    void handleAddCommand(const std::vector<std::string>& args);
    void handleRemoveCommand(const std::vector<std::string>& args);
    void handlePauseCommand(const std::vector<std::string>& args);
    void handleResumeCommand(const std::vector<std::string>& args);
    void handlePriorityCommand(const std::vector<std::string>& args);
    
    // 显示功能
    void displayTaskProgress(const std::vector<DownloadStatus>& tasks);
    void displayGlobalStatistics(const GlobalStatistics& stats);
    void displayTaskStatistics(TaskId task_id);
    
    SessionManager& session_manager_;
    bool interactive_mode_{false};
    bool show_detailed_progress_{false};
};
```

**实现要点**：
- 支持批量操作命令
- 交互式任务管理界面
- 实时进度显示（多任务）
- 丰富的统计信息展示

#### 4.2 Web API接口 (可选)
**优先级**：🟢 低
**依赖**：SessionManager

```cpp
class WebAPI {
    WebAPI(SessionManager& session, uint16_t port = 8080);
    
    void start();
    void stop();
    
private:
    // REST API端点
    void setupRoutes();
    
    // 任务管理API
    void handleGetTasks(const HttpRequest& req, HttpResponse& resp);
    void handleAddTask(const HttpRequest& req, HttpResponse& resp);
    void handleRemoveTask(const HttpRequest& req, HttpResponse& resp);
    void handlePauseTask(const HttpRequest& req, HttpResponse& resp);
    void handleResumeTask(const HttpRequest& req, HttpResponse& resp);
    
    // 统计信息API
    void handleGetGlobalStats(const HttpRequest& req, HttpResponse& resp);
    void handleGetTaskStats(const HttpRequest& req, HttpResponse& resp);
    
    // WebSocket支持（实时更新）
    void handleWebSocketConnection(const WebSocketConnection& conn);
    void broadcastTaskUpdate(TaskId task_id, const DownloadStatus& status);
};
```

**实现要点**：
- RESTful API设计
- WebSocket实时更新
- JSON格式数据交换
- 简单的Web管理界面

## 📊 实现时间表

### 第1-2周：核心基础设施
- [ ] TaskScheduler实现和测试
- [ ] ConnectionPool实现和测试
- [ ] DhtClient多任务增强
- [ ] 基础单元测试

### 第3-4周：任务管理核心
- [ ] SessionManager实现
- [ ] DownloadTask重构
- [ ] 现有代码迁移
- [ ] 集成测试

### 第5-6周：高级功能
- [ ] BandwidthManager实现
- [ ] TaskPersistence实现
- [ ] Config增强
- [ ] 性能优化

### 第7周：用户界面
- [ ] 多任务CLI实现
- [ ] Web API实现（可选）
- [ ] 文档完善
- [ ] 最终测试

## 🧪 测试策略

### 单元测试
```cpp
// 每个模块的单元测试
TEST(TaskSchedulerTest, PriorityOrdering)
TEST(ConnectionPoolTest, ResourceAllocation)
TEST(SessionManagerTest, TaskLifecycle)
TEST(DownloadTaskTest, StateTransitions)
```

### 集成测试
```cpp
// 模块间协作测试
TEST(MultiTaskIntegrationTest, ConcurrentDownloads)
TEST(ResourceSharingTest, DhtPeerSharing)
TEST(BandwidthTest, FairAllocation)
```

### 性能测试
```cpp
// 性能和压力测试
TEST(PerformanceTest, HundredConcurrentTasks)
TEST(MemoryTest, LongRunningSession)
TEST(NetworkTest, HighBandwidthUtilization)
```

### 真实场景测试
- 同时下载10个不同大小的文件
- 网络中断和恢复测试
- 长时间运行稳定性测试
- 资源限制下的行为测试

## 🎯 成功指标

### 功能指标
- [ ] 支持同时下载至少10个任务
- [ ] 任务间资源公平分配
- [ ] 用户操作响应时间 < 100ms
- [ ] 任务状态持久化和恢复

### 性能指标
- [ ] 内存使用 < 100MB（10个并发任务）
- [ ] CPU使用率 < 50%（正常负载）
- [ ] 网络带宽利用率 > 80%
- [ ] 连接建立成功率 > 95%

### 稳定性指标
- [ ] 24小时连续运行无崩溃
- [ ] 网络异常自动恢复
- [ ] 内存泄漏为零
- [ ] 所有资源正确释放

## 🔧 开发工具和环境

### 构建系统
```cmake
# CMake配置增强
option(BUILD_MULTI_TASK "Build multi-task support" ON)
option(BUILD_WEB_API "Build web API support" OFF)
option(ENABLE_PERFORMANCE_PROFILING "Enable performance profiling" OFF)
```

### 调试工具
- **Valgrind** - 内存泄漏检测
- **GDB** - 调试多线程问题
- **Wireshark** - 网络协议分析
- **htop/top** - 资源使用监控

### 性能分析
- **perf** - CPU性能分析
- **gperftools** - 内存和CPU profiling
- **自定义统计** - 任务级性能监控

## 📚 学习资源

### 参考项目深入研究
1. **libtorrent-rasterbar**
   - `src/session.cpp` - 会话管理
   - `src/torrent.cpp` - 任务管理
   - `src/bandwidth_manager.cpp` - 带宽管理

2. **qBittorrent**
   - `src/base/bittorrent/session.cpp` - 多任务管理
   - `src/base/bittorrent/torrenthandle.cpp` - 任务封装

3. **transmission**
   - `libtransmission/session.c` - 简洁的会话管理
   - `libtransmission/bandwidth.c` - 带宽控制

### 技术文档
- **BitTorrent协议规范** - 理解协议细节
- **DHT协议规范** - 优化DHT性能
- **C++并发编程** - 多线程最佳实践
- **Asio文档** - 异步编程模式

## 🚀 开始实施

### 第一步：创建新分支
```bash
git checkout -b feature/multi-task-architecture
```

### 第二步：设置项目结构
```
src/
├── application/
│   ├── session_manager.cpp        # 新增
│   ├── download_task.cpp          # 重构自download_controller
│   └── multi_task_cli.cpp         # 新增
├── async/
│   ├── task_scheduler.cpp         # 增强现有
│   └── bandwidth_manager.cpp     # 新增
├── network/
│   ├── connection_pool.cpp       # 新增
│   └── tcp_client.cpp            # 增强现有
└── storage/
    └── task_persistence.cpp      # 新增
```

### 第三步：实现TaskScheduler
从最基础的模块开始，确保每个模块都经过充分测试后再进行下一个。

你准备好开始这个激动人心的重构之旅了吗？我们从TaskScheduler开始！
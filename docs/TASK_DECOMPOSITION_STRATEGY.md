# 任务拆分与调度策略

> **核心问题**：如何将磁力链接下载拆分成可调度的任务？

---

## 📋 目录

1. [任务层次结构](#1-任务层次结构)
2. [单个磁力链接的任务拆分](#2-单个磁力链接的任务拆分)
3. [多个磁力链接的任务管理](#3-多个磁力链接的任务管理)
4. [任务优先级分配](#4-任务优先级分配)
5. [任务调度策略](#5-任务调度策略)
6. [实际代码示例](#6-实际代码示例)

---

## 1. 任务层次结构

### 1.1 三层任务模型

```
┌─────────────────────────────────────────────────────────┐
│                  下载任务 (DownloadTask)                 │
│              一个磁力链接 = 一个下载任务                  │
│                    TaskId: "task_001"                   │
└─────────────────────────────────────────────────────────┘
                            │
                            │ 包含
                            ↓
┌─────────────────────────────────────────────────────────┐
│                  阶段任务 (PhaseTask)                    │
│        解析、DHT查询、连接、下载、校验等阶段              │
│     PhaseId: "task_001_phase_dht"                       │
└─────────────────────────────────────────────────────────┘
                            │
                            │ 包含
                            ↓
┌─────────────────────────────────────────────────────────┐
│                  原子任务 (AtomicTask)                   │
│        发送消息、接收数据、写文件等不可分割操作           │
│     AtomicId: "task_001_phase_dht_send_001"             │
└─────────────────────────────────────────────────────────┘
```

### 1.2 任务类型定义

```cpp
// 下载任务（顶层）
struct DownloadTask {
    TaskId id;                          // 唯一标识
    std::string magnet_uri;             // 磁力链接
    std::string save_path;              // 保存路径
    DownloadState state;                // 当前状态
    std::vector<PhaseTask> phases;      // 包含的阶段任务
    DownloadProgress progress;          // 下载进度
};

// 阶段任务（中层）
struct PhaseTask {
    PhaseId id;
    PhaseType type;                     // 解析/DHT/连接/下载/校验
    TaskPriority priority;              // 优先级
    std::vector<AtomicTask> atomic_tasks; // 原子任务
    PhaseState state;
};

// 原子任务（底层）
struct AtomicTask {
    AtomicId id;
    TaskPriority priority;
    std::function<void()> execute;      // 执行函数
    TaskState state;
};
```

---

## 2. 单个磁力链接的任务拆分

### 2.1 完整的任务分解流程

```
用户输入：magnet:?xt=urn:btih:HASH&dn=file.mp4
    ↓
创建 DownloadTask (task_001)
    ↓
拆分为 5 个阶段任务 (PhaseTask)
    ↓
┌─────────────────────────────────────────────────────────┐
│ 阶段 1：解析磁力链接 (PARSE)                             │
│ Priority: CRITICAL                                      │
│                                                         │
│ AtomicTask 1.1: 解析 URI 格式                           │
│ AtomicTask 1.2: 提取 InfoHash                           │
│ AtomicTask 1.3: 提取文件名和 Tracker                     │
└─────────────────────────────────────────────────────────┘
    ↓
┌─────────────────────────────────────────────────────────┐
│ 阶段 2：DHT 网络查询 (DHT_QUERY)                         │
│ Priority: HIGH                                          │
│                                                         │
│ AtomicTask 2.1: 连接引导节点                             │
│ AtomicTask 2.2: 发送 find_node 请求                     │
│ AtomicTask 2.3: 处理 find_node 响应                     │
│ AtomicTask 2.4: 发送 get_peers 请求                     │
│ AtomicTask 2.5: 处理 get_peers 响应                     │
│ AtomicTask 2.6: 收集 Peer 列表                          │
└─────────────────────────────────────────────────────────┘
    ↓
┌─────────────────────────────────────────────────────────┐
│ 阶段 3：连接 Peer 并获取元数据 (METADATA)                │
│ Priority: HIGH                                          │
│                                                         │
│ 对每个 Peer (并发):                                     │
│   AtomicTask 3.1: 建立 TCP 连接                         │
│   AtomicTask 3.2: 发送握手消息                          │
│   AtomicTask 3.3: 接收握手响应                          │
│   AtomicTask 3.4: 请求元数据分片                        │
│   AtomicTask 3.5: 接收元数据分片                        │
│   AtomicTask 3.6: 组装完整元数据                        │
└─────────────────────────────────────────────────────────┘
    ↓
┌─────────────────────────────────────────────────────────┐
│ 阶段 4：下载文件数据 (DOWNLOAD)                          │
│ Priority: NORMAL                                        │
│                                                         │
│ 对每个分片 (并发):                                       │
│   AtomicTask 4.1: 选择下载分片                          │
│   AtomicTask 4.2: 选择 Peer                             │
│   AtomicTask 4.3: 发送 request 消息                     │
│   AtomicTask 4.4: 接收 piece 数据                       │
│   AtomicTask 4.5: 校验分片哈希                          │
│   AtomicTask 4.6: 写入磁盘                              │
└─────────────────────────────────────────────────────────┘
    ↓
┌─────────────────────────────────────────────────────────┐
│ 阶段 5：最终校验和清理 (FINALIZE)                        │
│ Priority: NORMAL                                        │
│                                                         │
│ AtomicTask 5.1: 校验所有分片完整性                       │
│ AtomicTask 5.2: 关闭所有连接                            │
│ AtomicTask 5.3: 更新任务状态                            │
│ AtomicTask 5.4: 通知用户完成                            │
└─────────────────────────────────────────────────────────┘
```

### 2.2 任务状态转换

```
DownloadTask 状态机：

CREATED → PARSING → DHT_QUERYING → CONNECTING → DOWNLOADING → FINALIZING → COMPLETED
   ↓         ↓           ↓              ↓            ↓             ↓            ↓
 FAILED   FAILED      FAILED         FAILED       FAILED        FAILED      SUCCESS

可暂停状态：DOWNLOADING
可恢复状态：DOWNLOADING
可取消状态：任何状态
```

---

## 3. 多个磁力链接的任务管理

### 3.1 多任务并发模型

```
用户输入 3 个磁力链接：
    magnet_uri_1 (电影)
    magnet_uri_2 (音乐)
    magnet_uri_3 (文档)
    ↓
DownloadManager 创建 3 个 DownloadTask
    ↓
┌─────────────────────────────────────────────────────────┐
│              DownloadTask Queue                         │
│                                                         │
│  task_001 (电影) ─┐                                     │
│  task_002 (音乐) ─┼─> 并发执行                          │
│  task_003 (文档) ─┘                                     │
└─────────────────────────────────────────────────────────┘
    ↓
每个 DownloadTask 独立拆分为阶段任务
    ↓
┌─────────────────────────────────────────────────────────┐
│              TaskScheduler (全局调度器)                  │
│                                                         │
│  所有 DownloadTask 的所有 AtomicTask 统一调度            │
│                                                         │
│  优先级队列：                                            │
│    CRITICAL: [task_001_parse, task_002_parse, ...]     │
│    HIGH:     [task_001_dht, task_002_dht, ...]         │
│    NORMAL:   [task_001_download, task_002_download]    │
│    LOW:      [task_001_stats, task_002_stats]          │
└─────────────────────────────────────────────────────────┘
    ↓
EventLoopManager 执行任务
```

### 3.2 资源分配策略

**并发限制**：
```cpp
struct ResourceLimits {
    size_t max_concurrent_downloads = 3;      // 最多同时下载 3 个文件
    size_t max_peers_per_download = 50;       // 每个下载最多 50 个 Peer
    size_t max_total_connections = 150;       // 全局最多 150 个连接
    size_t max_disk_io_tasks = 10;            // 最多 10 个磁盘 I/O 任务
};
```

**资源分配算法**：
```
1. 优先分配给高优先级任务
2. 公平分配：每个活跃任务至少分配最小资源
3. 动态调整：根据任务进度动态分配
```

### 3.3 任务间协调

```cpp
class DownloadManager {
private:
    // 所有下载任务
    std::unordered_map<TaskId, DownloadTask> tasks_;
    
    // 资源管理
    ResourceManager resource_manager_;
    
    // 全局调度器
    TaskScheduler& scheduler_;
    
public:
    // 添加新任务
    TaskId add_download(const std::string& magnet_uri) {
        auto task = create_download_task(magnet_uri);
        tasks_[task.id] = task;
        
        // 拆分并调度任务
        decompose_and_schedule(task);
        
        return task.id;
    }
    
    // 拆分任务
    void decompose_and_schedule(DownloadTask& task) {
        // 阶段 1：解析（立即执行）
        schedule_parse_phase(task);
        
        // 阶段 2-5：根据前一阶段结果动态调度
        // 使用回调链接各阶段
    }
};
```

---

## 4. 任务优先级分配

### 4.1 优先级分配规则

```cpp
enum class TaskPriority {
    CRITICAL = 0,  // 用户交互、紧急错误
    HIGH     = 1,  // DHT 查询、Peer 连接
    NORMAL   = 2,  // 数据下载、文件写入
    LOW      = 3   // 统计、日志、清理
};

// 优先级分配策略
TaskPriority assign_priority(const AtomicTask& task) {
    switch (task.phase_type) {
        case PhaseType::PARSE:
            return TaskPriority::CRITICAL;  // 解析必须立即完成
            
        case PhaseType::DHT_QUERY:
            return TaskPriority::HIGH;      // DHT 查询影响后续流程
            
        case PhaseType::METADATA:
            return TaskPriority::HIGH;      // 元数据获取是关键路径
            
        case PhaseType::DOWNLOAD:
            // 根据用户设置和任务状态动态调整
            if (task.is_user_priority) {
                return TaskPriority::HIGH;
            }
            return TaskPriority::NORMAL;
            
        case PhaseType::FINALIZE:
            return TaskPriority::NORMAL;
            
        case PhaseType::STATISTICS:
            return TaskPriority::LOW;       // 统计不影响功能
    }
}
```

### 4.2 动态优先级调整

```cpp
// 老化机制：防止低优先级任务饥饿
void TaskScheduler::age_tasks() {
    auto now = std::chrono::steady_clock::now();
    
    for (auto& task : pending_tasks_) {
        auto wait_time = now - task.created_time;
        
        // 等待超过 5 秒，提升优先级
        if (wait_time > std::chrono::seconds(5)) {
            if (task.priority < TaskPriority::CRITICAL) {
                task.priority = static_cast<TaskPriority>(
                    static_cast<int>(task.priority) - 1
                );
            }
        }
    }
}
```

---

## 5. 任务调度策略

### 5.1 调度算法

```cpp
class TaskScheduler {
public:
    void schedule() {
        while (running_) {
            // 1. 从优先级队列获取最高优先级任务
            auto task = get_highest_priority_task();
            
            // 2. 检查资源是否可用
            if (!resource_manager_.can_execute(task)) {
                // 资源不足，等待或降级
                handle_resource_shortage(task);
                continue;
            }
            
            // 3. 分配资源
            resource_manager_.allocate(task);
            
            // 4. 提交到 EventLoopManager 执行
            event_loop_manager_.post_to_least_loaded([task]() {
                task.execute();
            });
            
            // 5. 更新统计信息
            update_statistics(task);
        }
    }
    
private:
    std::priority_queue<AtomicTask, 
                        std::vector<AtomicTask>,
                        TaskComparator> task_queue_;
};
```

### 5.2 任务依赖处理

```cpp
// 任务依赖图
struct TaskDependency {
    AtomicTask task;
    std::vector<AtomicTask*> dependencies;  // 依赖的任务
    std::atomic<size_t> pending_count;      // 未完成的依赖数
};

// 依赖解析
void schedule_with_dependencies(AtomicTask task) {
    auto deps = analyze_dependencies(task);
    
    if (deps.empty()) {
        // 无依赖，直接调度
        scheduler_.post_task(task.priority, task.execute);
    } else {
        // 有依赖，等待依赖完成
        for (auto& dep : deps) {
            dep->on_complete([task, this]() {
                if (--task.pending_count == 0) {
                    // 所有依赖完成，调度任务
                    scheduler_.post_task(task.priority, task.execute);
                }
            });
        }
    }
}
```

---

## 6. 实际代码示例

### 6.1 单个磁力链接的完整拆分

```cpp
class DownloadManager {
public:
    TaskId start_download(const std::string& magnet_uri, 
                          const std::string& save_path) {
        // 创建下载任务
        auto task_id = generate_task_id();
        auto task = std::make_shared<DownloadTask>();
        task->id = task_id;
        task->magnet_uri = magnet_uri;
        task->save_path = save_path;
        task->state = DownloadState::CREATED;
        
        tasks_[task_id] = task;
        
        // 开始任务拆分和调度
        decompose_and_schedule(task);
        
        return task_id;
    }
    
private:
    void decompose_and_schedule(std::shared_ptr<DownloadTask> task) {
        // 阶段 1：解析磁力链接
        schedule_parse_phase(task);
    }
    
    void schedule_parse_phase(std::shared_ptr<DownloadTask> task) {
        scheduler_.post_task(TaskPriority::CRITICAL, [this, task]() {
            // 原子任务 1.1: 解析 URI
            auto magnet_info = magnet_parser_.parse(task->magnet_uri);
            
            if (!magnet_info) {
                task->state = DownloadState::FAILED;
                return;
            }
            
            task->magnet_info = *magnet_info;
            task->state = DownloadState::PARSING_COMPLETE;
            
            // 解析完成，调度下一阶段
            schedule_dht_phase(task);
        });
    }
    
    void schedule_dht_phase(std::shared_ptr<DownloadTask> task) {
        task->state = DownloadState::DHT_QUERYING;
        
        // 原子任务 2.1-2.6: DHT 查询
        scheduler_.post_task(TaskPriority::HIGH, [this, task]() {
            dht_client_.find_peers(
                task->magnet_info.info_hash,
                [this, task](std::vector<PeerInfo> peers) {
                    task->peers = peers;
                    task->state = DownloadState::DHT_COMPLETE;
                    
                    // DHT 完成，调度连接阶段
                    schedule_connect_phase(task);
                }
            );
        });
    }
    
    void schedule_connect_phase(std::shared_ptr<DownloadTask> task) {
        task->state = DownloadState::CONNECTING;
        
        // 对每个 Peer 并发连接（原子任务 3.1-3.6）
        for (const auto& peer : task->peers) {
            scheduler_.post_task(TaskPriority::HIGH, [this, task, peer]() {
                peer_manager_.connect_to_peer(
                    peer,
                    task->magnet_info.info_hash,
                    [this, task](PeerConnection::Ptr connection) {
                        if (!connection) return;
                        
                        // 获取元数据
                        connection->fetch_metadata([this, task](TorrentMetadata metadata) {
                            task->metadata = metadata;
                            task->state = DownloadState::METADATA_COMPLETE;
                            
                            // 元数据完成，调度下载阶段
                            schedule_download_phase(task);
                        });
                    }
                );
            });
        }
    }
    
    void schedule_download_phase(std::shared_ptr<DownloadTask> task) {
        task->state = DownloadState::DOWNLOADING;
        
        // 初始化分片管理
        piece_manager_.initialize(task->metadata);
        
        // 对每个分片创建下载任务（原子任务 4.1-4.6）
        for (size_t i = 0; i < task->metadata.piece_count; ++i) {
            scheduler_.post_task(TaskPriority::NORMAL, [this, task, i]() {
                download_piece(task, i);
            });
        }
    }
    
    void download_piece(std::shared_ptr<DownloadTask> task, size_t piece_index) {
        // 选择 Peer
        auto peer = select_peer_for_piece(task, piece_index);
        
        // 请求分片
        peer->request_piece(piece_index, [this, task, piece_index](std::vector<byte> data) {
            // 校验数据
            if (data_verifier_.verify(piece_index, data, task->metadata)) {
                // 写入磁盘
                file_writer_.write_piece(piece_index, data, [this, task]() {
                    // 更新进度
                    task->progress.completed_pieces++;
                    
                    // 检查是否完成
                    if (task->progress.completed_pieces == task->metadata.piece_count) {
                        schedule_finalize_phase(task);
                    }
                });
            } else {
                // 校验失败，重新下载
                scheduler_.post_task(TaskPriority::NORMAL, [this, task, piece_index]() {
                    download_piece(task, piece_index);
                });
            }
        });
    }
    
    void schedule_finalize_phase(std::shared_ptr<DownloadTask> task) {
        scheduler_.post_task(TaskPriority::NORMAL, [this, task]() {
            // 最终校验
            if (verify_complete_file(task)) {
                task->state = DownloadState::COMPLETED;
                notify_user(task->id, "下载完成");
            } else {
                task->state = DownloadState::FAILED;
                notify_user(task->id, "校验失败");
            }
        });
    }
};
```

### 6.2 多个磁力链接的并发管理

```cpp
// 用户添加 3 个下载任务
auto task1 = download_manager.start_download("magnet:?xt=...", "./downloads/");
auto task2 = download_manager.start_download("magnet:?xt=...", "./downloads/");
auto task3 = download_manager.start_download("magnet:?xt=...", "./downloads/");

// DownloadManager 内部处理
class DownloadManager {
private:
    // 所有任务共享同一个调度器
    TaskScheduler& scheduler_;
    
    // 资源管理器控制并发
    ResourceManager resource_manager_;
    
    void enforce_resource_limits() {
        // 限制并发下载数
        size_t active_downloads = count_active_downloads();
        if (active_downloads >= resource_manager_.max_concurrent_downloads) {
            // 暂停新任务，等待资源释放
            pause_new_tasks();
        }
        
        // 限制总连接数
        size_t total_connections = count_total_connections();
        if (total_connections >= resource_manager_.max_total_connections) {
            // 关闭部分低优先级连接
            close_low_priority_connections();
        }
    }
};
```

---

## 7. 总结

### 任务拆分的核心思想

1. **三层模型**：DownloadTask → PhaseTask → AtomicTask
2. **阶段划分**：解析 → DHT → 连接 → 下载 → 校验
3. **并发执行**：多个 DownloadTask 并发，每个 Task 内部也并发
4. **优先级调度**：关键路径高优先级，数据传输普通优先级
5. **资源控制**：全局资源限制，防止过载

### 关键设计点

- **任务粒度**：AtomicTask 是不可分割的最小执行单元
- **依赖管理**：阶段间有依赖，阶段内可并发
- **动态调度**：根据执行结果动态创建后续任务
- **资源公平**：多任务间公平分配资源
- **优先级老化**：防止低优先级任务饥饿

这就是磁力链接下载的完整任务拆分和调度策略！🎯

# EventLoopManager 技术设计文档

## 1. 文档信息

| 属性 | 值 |
|------|-----|
| 文档版本 | v1.0 |
| 创建日期 | 2024-12-19 |
| 最后更新 | 2024-12-19 |
| 作者 | 磁力下载器开发团队 |
| 审核状态 | 草案 |

## 2. 文档目的和范围

### 2.1 目的
本文档详细说明EventLoopManager的技术设计和实现规范，作为编码实现的权威指导文档。确保实现严格遵循架构设计，满足磁力下载器项目的性能和功能要求。

### 2.2 范围
- EventLoopManager类的完整接口设计
- 内部实现机制和算法详述
- 线程模型和并发策略
- 错误处理和异常安全保证
- 性能要求和优化策略
- 测试策略和验收标准
- 与其他模块的集成接口

### 2.3 设计原则
- **架构一致性**: 严格遵循ARCHITECTURE.md中的分层架构设计
- **学习应用**: 基于ASIO实验6掌握的多线程事件循环知识
- **简单优先**: 优先考虑简单性和可维护性，避免过度工程
- **扩展性**: 为未来功能扩展预留设计空间
- **性能导向**: 针对磁力下载器的高并发需求优化

## 3. 系统概述

### 3.1 功能概述
EventLoopManager是磁力下载器项目的核心基础设施，提供高性能的异步任务执行平台。主要功能包括：

- **多线程事件循环管理**: 管理多个io_context实例形成高效线程池
- **统一任务投递接口**: 提供类型安全、高性能的任务投递API
- **智能负载均衡**: 实现多种负载均衡策略，优化任务分发
- **生命周期管理**: 完整的启动、运行、停止生命周期控制
- **资源监控**: 提供运行时状态监控和性能统计

### 3.2 在系统架构中的位置

```
┌─────────────────────────────────────────────────────┐
│                   应用层                            │
│  DHT客户端 │ Peer管理器 │ 下载引擎 │ 用户界面        │
└─────────────────────┬───────────────────────────────┘
                      │ 使用异步任务接口
┌─────────────────────▼───────────────────────────────┐
│                 基础层                              │
│              EventLoopManager                       │ ← 本设计文档
│           (多线程事件循环管理器)                     │
└─────────────────────┬───────────────────────────────┘
                      │ 基于
┌─────────────────────▼───────────────────────────────┐
│                 系统层                              │
│    Asio库 │ 操作系统线程 │ 网络栈 │ 文件系统         │
└─────────────────────────────────────────────────────┘
```

### 3.3 关键设计决策
1. **基于Asio**: 使用经过验证的Asio库作为异步I/O基础
2. **线程池模型**: 采用固定大小线程池，避免动态创建线程的开销
3. **模板接口**: 使用模板提供高性能、类型安全的任务投递接口
4. **轮询负载均衡**: 实现简单高效的Round-Robin负载均衡算法

## 4. 详细设计

### 4.1 类接口设计

#### 4.1.1 完整类声明

```cpp
#include <asio.hpp>
#include <vector>
#include <thread>
#include <memory>
#include <atomic>
#include <functional>
#include <future>

namespace magnet_downloader {
namespace core {

class EventLoopManager {
public:
    // 4.1.1.1 构造和析构
    explicit EventLoopManager(size_t thread_count = std::thread::hardware_concurrency());
    ~EventLoopManager();
    
    // 禁止拷贝，允许移动
    EventLoopManager(const EventLoopManager&) = delete;
    EventLoopManager& operator=(const EventLoopManager&) = delete;
    EventLoopManager(EventLoopManager&&) = default;
    EventLoopManager& operator=(EventLoopManager&&) = default;

    // 4.1.1.2 生命周期管理
    void start();                        // 启动所有工作线程
    void stop();                         // 请求停止
    void wait_for_stop();               // 等待所有线程完成
    bool is_running() const;            // 检查运行状态

    // 4.1.1.3 任务投递接口
    template<typename Task>
    void post(Task&& task);              // 投递任务到最优io_context
    
    template<typename Task>
    void dispatch(Task&& task);          // 立即执行或投递任务
    
    template<typename Task>
    auto post_with_result(Task&& task) -> std::future<decltype(task())>;  // 投递并返回future

    // 4.1.1.4 监控和统计
    size_t get_thread_count() const;     // 获取线程数量
    size_t get_pending_tasks() const;    // 获取待处理任务数（估算）
    std::vector<size_t> get_per_thread_load() const;  // 获取各线程负载

    // 4.1.1.5 高级接口
    asio::io_context& get_io_context();  // 获取用于投递任务的io_context
    asio::io_context& get_io_context(size_t index);  // 获取指定索引的io_context

private:
    // 4.1.2 私有成员变量
    std::vector<std::unique_ptr<asio::io_context>> contexts_;
    std::vector<std::thread> threads_;
    std::vector<asio::executor_work_guard<asio::io_context::executor_type>> work_guards_;
    
    std::atomic<size_t> next_context_index_{0};  // 负载均衡计数器
    std::atomic<bool> running_{false};           // 运行状态标记
    mutable std::mutex state_mutex_;             // 状态变更保护锁

    // 4.1.3 私有方法
    void worker_thread_main(size_t thread_index);  // 工作线程主函数
    size_t select_optimal_context();               // 选择最优io_context
    void cleanup_resources();                      // 清理资源
};

} // namespace core
} // namespace magnet_downloader
```

#### 4.1.2 接口详细说明

##### 4.1.2.1 构造函数设计
```cpp
explicit EventLoopManager(size_t thread_count = std::thread::hardware_concurrency());
```

**参数说明**:
- `thread_count`: 工作线程数量，默认为CPU核心数
- 建议值: 对于I/O密集型应用，使用CPU核心数或核心数×2

**设计考虑**:
- 默认值使用`hardware_concurrency()`，适合大多数应用场景
- 允许用户根据具体需求调整线程数量
- 最小线程数为1，最大线程数为128（合理性检查）

##### 4.1.2.2 任务投递接口设计

**post() 方法**:
```cpp
template<typename Task>
void post(Task&& task);
```
- **用途**: 异步投递任务，保证不在当前线程执行
- **参数**: 任何可调用对象（lambda、函数对象、绑定函数等）
- **线程安全**: 是
- **性能**: 高（无动态内存分配，完美转发）

**dispatch() 方法**:
```cpp
template<typename Task>
void dispatch(Task&& task);
```
- **用途**: 智能执行任务，如果当前线程是工作线程则立即执行，否则投递
- **优势**: 减少不必要的任务切换
- **适用场景**: 回调函数中的后续操作

**post_with_result() 方法**:
```cpp
template<typename Task>
auto post_with_result(Task&& task) -> std::future<decltype(task())>;
```
- **用途**: 投递任务并返回std::future获取结果
- **返回类型**: 自动推导的future类型
- **异常处理**: 任务中的异常会传播到future中

### 4.2 内部实现机制

#### 4.2.1 多线程事件循环模型

```cpp
// 每个工作线程的执行模型
void EventLoopManager::worker_thread_main(size_t thread_index) {
    try {
        // 记录线程启动
        LOG_INFO("工作线程 {} 启动", thread_index);
        
        // 获取线程专属的io_context
        auto& context = *contexts_[thread_index];
        
        // 进入事件循环
        size_t handlers_executed = context.run();
        
        // 记录线程退出信息
        LOG_INFO("工作线程 {} 退出，执行了 {} 个处理器", 
                thread_index, handlers_executed);
                
    } catch (const std::exception& e) {
        LOG_ERROR("工作线程 {} 异常退出: {}", thread_index, e.what());
    }
}
```

**关键特性**:
- 每个线程独占一个io_context，避免锁竞争
- 使用work_guard防止io_context提前退出
- 完善的异常处理和日志记录

#### 4.2.2 负载均衡算法

```cpp
size_t EventLoopManager::select_optimal_context() {
    // Round-Robin算法实现
    size_t index = next_context_index_.fetch_add(1, std::memory_order_relaxed);
    return index % contexts_.size();
}
```

**算法选择理由**:
- **Round-Robin**: 简单、高效、公平
- **原子操作**: 无锁实现，高并发性能
- **memory_order_relaxed**: 最低开销的内存序

**未来扩展**:
- 可以添加基于负载的智能选择算法
- 支持任务亲和性（相关任务在同一线程执行）

#### 4.2.3 生命周期管理

**启动序列**:
```cpp
void EventLoopManager::start() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    if (running_.load()) {
        throw std::runtime_error("EventLoopManager已经在运行");
    }
    
    // 1. 创建io_context对象
    for (size_t i = 0; i < thread_count_; ++i) {
        contexts_.emplace_back(std::make_unique<asio::io_context>());
        work_guards_.emplace_back(asio::make_work_guard(*contexts_[i]));
    }
    
    // 2. 启动工作线程
    for (size_t i = 0; i < thread_count_; ++i) {
        threads_.emplace_back(&EventLoopManager::worker_thread_main, this, i);
    }
    
    // 3. 标记为运行状态
    running_.store(true);
    
    LOG_INFO("EventLoopManager启动完成，线程数: {}", thread_count_);
}
```

**停止序列**:
```cpp
void EventLoopManager::stop() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    if (!running_.load()) {
        return;  // 已经停止
    }
    
    // 1. 移除work_guard，允许io_context退出
    work_guards_.clear();
    
    // 2. 停止所有io_context
    for (auto& context : contexts_) {
        context->stop();
    }
    
    // 3. 标记为停止状态
    running_.store(false);
    
    LOG_INFO("EventLoopManager停止请求已发送");
}

void EventLoopManager::wait_for_stop() {
    // 等待所有线程完成
    for (auto& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    // 清理资源
    cleanup_resources();
    
    LOG_INFO("EventLoopManager完全停止");
}
```

### 4.3 错误处理和异常安全

#### 4.3.1 异常安全等级
- **构造函数**: 强异常安全保证
- **任务投递**: 无抛出保证（noexcept）
- **生命周期管理**: 基本异常安全保证

#### 4.3.2 错误处理策略

**工作线程异常**:
```cpp
void EventLoopManager::worker_thread_main(size_t thread_index) {
    try {
        // 正常执行逻辑
        auto& context = *contexts_[thread_index];
        context.run();
    } catch (const std::exception& e) {
        // 记录异常但不传播，避免程序崩溃
        LOG_ERROR("工作线程 {} 发生异常: {}", thread_index, e.what());
        
        // 可选：触发异常恢复机制
        // recover_from_worker_exception(thread_index);
    } catch (...) {
        LOG_ERROR("工作线程 {} 发生未知异常", thread_index);
    }
}
```

**任务异常隔离**:
```cpp
template<typename Task>
void EventLoopManager::post(Task&& task) {
    if (!running_.load()) {
        throw std::runtime_error("EventLoopManager未运行");
    }
    
    size_t context_index = select_optimal_context();
    auto& context = *contexts_[context_index];
    
    // 包装任务以捕获异常
    asio::post(context, [task = std::forward<Task>(task)]() {
        try {
            task();
        } catch (const std::exception& e) {
            LOG_ERROR("任务执行异常: {}", e.what());
        } catch (...) {
            LOG_ERROR("任务执行发生未知异常");
        }
    });
}
```

## 5. 性能要求和优化策略

### 5.1 性能目标

| 指标 | 目标值 | 测量方法 |
|------|--------|----------|
| 任务投递延迟 | < 1μs | 微基准测试 |
| 吞吐量 | > 100万任务/秒 | 压力测试 |
| 内存开销 | < 1MB基础开销 | 内存分析工具 |
| 线程切换开销 | 最小化 | 性能分析器 |

### 5.2 优化策略

#### 5.2.1 编译时优化
- **模板特化**: 为常见任务类型提供特化实现
- **内联优化**: 小函数使用inline关键字
- **常量传播**: 使用constexpr和const

#### 5.2.2 运行时优化
- **内存局部性**: 相关数据结构紧密排列
- **原子操作优化**: 使用合适的内存序
- **避免动态分配**: 预分配资源，使用对象池

#### 5.2.3 并发优化
- **无锁算法**: 关键路径使用原子操作
- **任务亲和性**: 相关任务在同一线程执行
- **批量处理**: 支持批量任务投递

## 6. 测试策略

### 6.1 单元测试

#### 6.1.1 基本功能测试
```cpp
TEST(EventLoopManagerTest, BasicLifecycle) {
    EventLoopManager manager(4);
    
    // 测试启动
    EXPECT_NO_THROW(manager.start());
    EXPECT_TRUE(manager.is_running());
    
    // 测试任务执行
    std::atomic<int> counter{0};
    for (int i = 0; i < 100; ++i) {
        manager.post([&counter]() { counter.fetch_add(1); });
    }
    
    // 等待任务完成
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(counter.load(), 100);
    
    // 测试停止
    manager.stop();
    manager.wait_for_stop();
    EXPECT_FALSE(manager.is_running());
}
```

#### 6.1.2 并发安全测试
```cpp
TEST(EventLoopManagerTest, ConcurrentPost) {
    EventLoopManager manager(4);
    manager.start();
    
    const int thread_count = 10;
    const int tasks_per_thread = 1000;
    std::atomic<int> total_counter{0};
    
    std::vector<std::thread> test_threads;
    for (int i = 0; i < thread_count; ++i) {
        test_threads.emplace_back([&]() {
            for (int j = 0; j < tasks_per_thread; ++j) {
                manager.post([&total_counter]() {
                    total_counter.fetch_add(1);
                });
            }
        });
    }
    
    for (auto& t : test_threads) {
        t.join();
    }
    
    // 等待所有任务完成
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_EQ(total_counter.load(), thread_count * tasks_per_thread);
    
    manager.stop();
    manager.wait_for_stop();
}
```

### 6.2 集成测试

#### 6.2.1 与Asio集成测试
```cpp
TEST(EventLoopManagerTest, AsioIntegration) {
    EventLoopManager manager(4);
    manager.start();
    
    // 测试定时器
    std::atomic<bool> timer_fired{false};
    manager.post([&]() {
        auto timer = std::make_shared<asio::steady_timer>(
            manager.get_io_context(), std::chrono::milliseconds(50));
        timer->async_wait([&timer_fired, timer](const asio::error_code& ec) {
            if (!ec) {
                timer_fired.store(true);
            }
        });
    });
    
    // 等待定时器触发
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(timer_fired.load());
    
    manager.stop();
    manager.wait_for_stop();
}
```

### 6.3 性能测试

#### 6.3.1 吞吐量测试
```cpp
TEST(EventLoopManagerTest, ThroughputBenchmark) {
    EventLoopManager manager(4);
    manager.start();
    
    const int total_tasks = 1000000;
    std::atomic<int> completed_tasks{0};
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < total_tasks; ++i) {
        manager.post([&completed_tasks]() {
            completed_tasks.fetch_add(1);
        });
    }
    
    // 等待所有任务完成
    while (completed_tasks.load() < total_tasks) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    
    double throughput = total_tasks / (duration.count() / 1000.0);
    LOG_INFO("吞吐量: {:.0f} 任务/秒", throughput);
    
    // 验证性能目标
    EXPECT_GT(throughput, 100000);  // 期望 > 10万任务/秒
    
    manager.stop();
    manager.wait_for_stop();
}
```

## 7. 使用示例

### 7.1 基本使用模式

```cpp
#include "EventLoopManager.h"

int main() {
    using namespace magnet_downloader::core;
    
    // 创建事件循环管理器
    EventLoopManager event_manager(4);  // 4个工作线程
    
    // 启动
    event_manager.start();
    
    // 投递简单任务
    event_manager.post([]() {
        LOG_INFO("Hello from event loop!");
    });
    
    // 投递带参数的任务
    std::string message = "Processing data";
    event_manager.post([message]() {
        LOG_INFO("Task: {}", message);
    });
    
    // 投递返回结果的任务
    auto future = event_manager.post_with_result([]() -> int {
        // 模拟计算
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return 42;
    });
    
    // 获取结果
    int result = future.get();
    LOG_INFO("Computation result: {}", result);
    
    // 优雅停止
    event_manager.stop();
    event_manager.wait_for_stop();
    
    return 0;
}
```

### 7.2 在磁力下载器中的应用

```cpp
class DHTClient {
private:
    EventLoopManager& event_manager_;
    
public:
    explicit DHTClient(EventLoopManager& manager) 
        : event_manager_(manager) {}
    
    void query_peers(const std::string& info_hash) {
        // DHT查询任务
        event_manager_.post([this, info_hash]() {
            perform_dht_query(info_hash);
        });
    }
    
    void maintain_routing_table() {
        // 路由表维护任务
        event_manager_.post([this]() {
            cleanup_dead_nodes();
            ping_random_nodes();
        });
    }
    
private:
    void perform_dht_query(const std::string& info_hash) {
        // 实际的DHT查询逻辑
        LOG_INFO("Querying DHT for: {}", info_hash);
    }
};

class PeerManager {
private:
    EventLoopManager& event_manager_;
    
public:
    explicit PeerManager(EventLoopManager& manager)
        : event_manager_(manager) {}
    
    void connect_to_peer(const PeerInfo& peer) {
        // Peer连接任务
        event_manager_.post([this, peer]() {
            establish_connection(peer);
        });
    }
    
    void download_piece(int piece_index, const PeerInfo& peer) {
        // 数据块下载任务
        event_manager_.post([this, piece_index, peer]() {
            request_piece_from_peer(piece_index, peer);
        });
    }
};
```

## 8. 实现检查清单

### 8.1 核心功能实现
- [ ] 基本的EventLoopManager类框架
- [ ] 构造函数和析构函数
- [ ] start()和stop()方法
- [ ] 模板化的post()方法
- [ ] 工作线程管理
- [ ] work_guard生命周期管理

### 8.2 高级功能实现
- [ ] dispatch()方法实现
- [ ] post_with_result()方法实现
- [ ] 负载均衡算法
- [ ] 监控和统计接口
- [ ] 异常处理机制

### 8.3 测试覆盖
- [ ] 基本功能单元测试
- [ ] 并发安全测试
- [ ] 性能基准测试
- [ ] 内存泄漏测试
- [ ] 异常场景测试

### 8.4 文档和示例
- [ ] API文档完善
- [ ] 使用示例更新
- [ ] 最佳实践指南
- [ ] 故障排除指南

## 9. 风险评估和缓解策略

### 9.1 技术风险

| 风险 | 概率 | 影响 | 缓解策略 |
|------|------|------|----------|
| 线程死锁 | 中 | 高 | 仔细设计锁策略，使用原子操作 |
| 内存泄漏 | 低 | 中 | RAII设计，完善测试 |
| 性能不达标 | 中 | 中 | 早期性能测试，优化关键路径 |
| Asio版本兼容 | 低 | 低 | 固定Asio版本，兼容性测试 |

### 9.2 实施风险

| 风险 | 概率 | 影响 | 缓解策略 |
|------|------|------|----------|
| 开发周期延长 | 中 | 中 | 分阶段实现，MVP优先 |
| 接口设计变更 | 低 | 中 | 详细设计评审，早期原型验证 |
| 测试覆盖不足 | 中 | 高 | 测试驱动开发，代码覆盖率要求 |

## 10. 架构设计决策和模块化策略

### 10.1 库类型选择：静态库 vs 动态库

#### 10.1.1 决策依据分析

基于对主流软件框架的深入分析，我们确定了EventLoopManager的库类型选择标准：

**主流框架库类型对比**：

| 框架类型 | 代表框架 | 库类型 | 选择原因 |
|---------|---------|--------|----------|
| **GUI框架** | Qt, GTK+ | 动态库 | 跨平台兼容、插件系统、内存共享、LGPL许可 |
| **网络库** | Boost.Asio, POCO | 静态库/Header-Only | 高性能、简化部署、编译优化 |
| **游戏引擎** | Unreal, Unity | 静态库+动态插件 | 核心性能+扩展能力 |
| **系统软件** | Chromium, Firefox | 静态库 | 最高性能、完全控制 |

#### 10.1.2 磁力下载器项目特征分析

```cpp
// 项目特征评估
✅ 高性能网络I/O需求      → 适合静态库
✅ 单一应用程序模式       → 适合静态库  
✅ 无插件系统需求         → 适合静态库
✅ 简化部署要求           → 适合静态库
✅ 性能敏感应用           → 适合静态库
❌ 不需要跨应用共享       → 不需要动态库
❌ 不需要运行时模块替换   → 不需要动态库
```

#### 10.1.3 最终决策：分层静态库架构

**决策结论**：采用**分层静态库 + 可选动态插件**的混合架构

```cpp
// 推荐的库架构设计
namespace magnet_downloader {
    // 第1层：系统抽象层 (静态库 - libmagnet_platform.a)
    namespace platform {
        class EventLoop;          // 对Asio io_context的薄包装
        class ThreadPool;         // 跨平台线程池抽象  
        class FileSystem;         // 文件系统操作抽象
        class NetworkInterface;   // 网络接口抽象
    }
    
    // 第2层：核心基础设施 (静态库 - libmagnet_core.a)
    namespace core {
        class EventLoopManager;   // 多线程事件循环管理器
        class TaskScheduler;      // 优先级任务调度器
        class Logger;            // 结构化日志系统
        class ConfigManager;     // 配置管理器
    }
    
    // 第3层：业务功能模块 (静态库)
    namespace network {         // libmagnet_network.a
        class DHTClient;         // DHT网络客户端
        class PeerManager;       // BitTorrent Peer管理
        class ProtocolHandler;   // 协议处理器
    }
    
    namespace storage {         // libmagnet_storage.a
        class TorrentManager;    // 种子文件管理
        class FileManager;       // 文件I/O管理
        class PieceManager;      // 数据块管理
    }
    
    // 第4层：用户界面 (可选动态库，支持多种实现)
    namespace ui {
        // libmagnet_ui_qt.so (可选)
        class QtMainWindow;
        
        // libmagnet_ui_console.so (可选)  
        class ConsoleInterface;
        
        // libmagnet_ui_web.so (未来扩展)
        class WebInterface;
    }
}
```

### 10.2 EventLoopManager在架构中的定位

#### 10.2.1 与主流框架的对比定位

```cpp
// EventLoopManager的设计定位对比
Qt模式:          QEventLoop + QCoreApplication
Chromium模式:    MessageLoop + MessagePump  
Node.js模式:     uv_loop_t的高级C++包装
我们的定位:      Asio io_context的企业级封装
```

#### 10.2.2 设计理念

**核心理念**：EventLoopManager不继承`enable_shared_from_this`的设计理由

1. **明确的生命周期管理**：
```cpp
class Application {
private:
    core::EventLoopManager event_manager_;  // 作为成员变量，生命周期同步
    
public:
    void run() {
        event_manager_.start();              // 明确启动
        setup_application_components();      // 应用运行期间保持存活
        event_manager_.stop();               // 明确停止
        event_manager_.wait_for_stop();      // 确保完全停止后才销毁
    }
};
```

2. **执行者而非发起者角色**：
```cpp
// EventLoopManager只负责执行任务，不发起引用自身的异步操作
template<typename Task>
void EventLoopManager::post(Task&& task) {
    // 只是转发任务，不需要保持自身引用
    asio::post(get_optimal_context(), std::forward<Task>(task));
}

// 对比：需要shared_from_this的业务对象
class PeerConnection : public std::enable_shared_from_this<PeerConnection> {
    void start_read() {
        // 发起引用自身的异步操作
        socket_.async_read_some(buffer_, [self = shared_from_this()](auto ec, auto bytes) {
            self->on_read(ec, bytes);  // 回调中需要访问PeerConnection对象
        });
    }
};
```

3. **基础设施vs业务对象的设计模式**：
```cpp
// 基础设施模式 (EventLoopManager)
class EventLoopManager {
    // 特点：
    // - 生命周期由应用控制
    // - 通过依赖注入使用
    // - 主动控制异步操作的停止
    // - 通过join()确保同步销毁
};

// 业务对象模式 (需要shared_from_this)
class NetworkClient : public std::enable_shared_from_this<NetworkClient> {
    // 特点：
    // - 可能在异步操作进行中销毁
    // - 回调中需要访问对象成员
    // - 生命周期与异步操作绑定
};
```

### 10.3 CMake构建系统设计

#### 10.3.1 模块化构建配置

```cmake
# 顶层CMakeLists.txt - 模块化构建配置
project(MagnetDownloader VERSION 1.0.0)

# 构建选项配置
option(BUILD_CORE_MODULE "构建核心基础设施模块" ON)
option(BUILD_NETWORK_MODULE "构建网络功能模块" ON) 
option(BUILD_STORAGE_MODULE "构建存储管理模块" ON)
option(BUILD_UI_QT "构建Qt图形界面" OFF)
option(BUILD_UI_CONSOLE "构建控制台界面" ON)
option(BUILD_TESTS "构建单元测试" OFF)
option(BUILD_BENCHMARKS "构建性能测试" OFF)

# 静态库构建
if(BUILD_CORE_MODULE)
    add_subdirectory(src/core)
endif()

if(BUILD_NETWORK_MODULE)
    add_subdirectory(src/network) 
endif()

if(BUILD_STORAGE_MODULE)
    add_subdirectory(src/storage)
endif()

# 主应用程序
add_subdirectory(src/application)

# 可选UI模块 (动态库)
if(BUILD_UI_QT)
    add_subdirectory(src/ui/qt)
endif()

if(BUILD_UI_CONSOLE)
    add_subdirectory(src/ui/console)
endif()
```

#### 10.3.2 核心模块构建示例

```cmake
# src/core/CMakeLists.txt - 核心模块
add_library(magnet_core STATIC
    EventLoopManager.cpp
    TaskScheduler.cpp  
    Logger.cpp
    ConfigManager.cpp
)

target_include_directories(magnet_core
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(magnet_core
    PUBLIC
        asio::asio          # Header-only库
        fmt::fmt            # 高性能格式化库
    PRIVATE  
        Threads::Threads    # 系统线程库
)

# 编译选项优化
target_compile_features(magnet_core PUBLIC cxx_std_17)
target_compile_options(magnet_core PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Werror -O3>
)
```

### 10.4 接口设计原则和最佳实践

#### 10.4.1 依赖注入模式

```cpp
// 推荐：通过构造函数注入依赖，明确依赖关系
class DHTClient {
private:
    core::EventLoopManager& event_manager_;
    
public:
    explicit DHTClient(core::EventLoopManager& manager) 
        : event_manager_(manager) {
        // 明确的依赖关系，便于测试和管理
    }
    
    void query_peers(const std::string& info_hash) {
        event_manager_.post([this, info_hash]() {
            perform_dht_query(info_hash);
        });
    }
};

// 避免：全局单例或静态依赖
class DHTClient {
public:
    void query() {
        // ❌ 隐式依赖，难以测试和管理
        GlobalEventManager::instance().post(task);
    }
};
```

#### 10.4.2 接口稳定性保证

```cpp
namespace magnet_downloader::core {
    // 公共接口：一旦发布就保持稳定
    class MAGNET_CORE_API EventLoopManager {
    public:
        // 稳定的核心接口
        void start();
        void stop(); 
        void wait_for_stop();
        bool is_running() const;
        
        template<typename Task>
        void post(Task&& task);
        
        template<typename Task>
        auto post_with_result(Task&& task) -> std::future<decltype(task())>;
        
    private:
        // 内部实现：可以自由改进和优化
        std::vector<std::unique_ptr<asio::io_context>> contexts_;
        std::vector<std::thread> threads_;
        std::atomic<size_t> next_context_index_{0};
        // 实现细节随版本演进
    };
}
```

### 10.5 版本规划和演进策略

#### 10.5.1 MVP版本（v1.0）
- **核心目标**：基本的多线程事件循环管理
- **关键特性**：
  - EventLoopManager基础实现
  - Round-Robin负载均衡
  - 模板化任务投递接口
  - 优雅的启动和停止机制
- **库形态**：单一静态库(libmagnet_core.a)

#### 10.5.2 稳定版本（v1.1）
- **核心目标**：生产就绪的稳定版本
- **关键特性**：
  - 完善的异常处理和错误恢复
  - 性能监控和统计接口
  - 完整的单元测试和集成测试
  - 详细的性能基准测试
- **库形态**：分层静态库架构

#### 10.5.3 扩展版本（v1.2）
- **核心目标**：高级特性和优化
- **关键特性**：
  - 智能负载均衡算法(基于任务类型和负载)
  - 任务优先级和调度策略
  - 高级监控和运维支持
  - 可插拔的UI模块(动态库)
- **库形态**：混合架构(静态库核心+动态UI插件)

### 10.6 架构决策总结

#### 10.6.1 关键设计决策

| 决策项 | 选择 | 理由 |
|--------|------|------|
| **库类型** | 静态库 | 高性能、简化部署、编译优化 |
| **生命周期** | 不使用shared_from_this | 明确的拥有关系、同步销毁 |
| **依赖注入** | 构造函数注入 | 明确依赖、便于测试 |
| **模块化** | 分层静态库 | 清晰职责、独立开发 |
| **接口设计** | 模板+类型安全 | 高性能、编译时优化 |

#### 10.6.2 设计原则遵循

1. **简单性优先**：避免过度工程，专注核心需求
2. **性能导向**：针对高并发网络I/O优化
3. **可维护性**：清晰的模块边界和接口
4. **可测试性**：依赖注入、接口抽象
5. **可扩展性**：为未来需求预留设计空间

这些架构决策确保了EventLoopManager既满足当前磁力下载器的性能需求，又为未来的功能扩展奠定了坚实的基础。

---

**文档状态**: 草案  
**下次审核**: 实现完成后  
**批准者**: 项目负责人  

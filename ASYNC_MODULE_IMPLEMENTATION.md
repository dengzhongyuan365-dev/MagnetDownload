# 🚀 Async模块实现指南

基于你完成的Asio实验，现在开始实现MagnetDownload的异步基础设施！

## 📋 **实现顺序和策略**

### 🎯 **实现优先级**
1. ✅ **EventLoopManager** - 事件循环管理器 (最核心)
2. ✅ **TaskScheduler** - 任务调度器 (依赖EventLoopManager)
3. ⏸️ **Timer** - 定时器工具 (后续需要时实现)

---

## 🔧 **第1步：创建头文件结构**

### 📁 **需要创建的头文件**

```
include/magnet/async/
├── event_loop_manager.h     # 事件循环管理器接口
├── task_scheduler.h         # 任务调度器接口
└── types.h                  # 异步模块通用类型定义
```

---

## 🔧 **第2步：EventLoopManager设计**

### 🎯 **设计目标**
基于你在实验6学到的多线程io_context模式，设计一个生产级的事件循环管理器。

### 💡 **核心设计思路**

```cpp
// include/magnet/async/event_loop_manager.h
#pragma once
#include <asio.hpp>
#include <vector>
#include <thread>
#include <atomic>
#include <memory>
#include <functional>

namespace magnet::async {

class EventLoopManager {
public:
    explicit EventLoopManager(size_t thread_count = std::thread::hardware_concurrency());
    ~EventLoopManager();
    
    // 生命周期管理
    void start();
    void stop();
    bool is_running() const { return running_.load(); }
    
    // 负载均衡的任务分发
    asio::io_context& get_io_context();
    asio::io_context& get_least_loaded_context();
    
    // 任务投递接口
    template<typename Handler>
    void post(Handler&& handler);
    
    template<typename Handler>
    void post_to_least_loaded(Handler&& handler);
    
    // 统计信息
    struct Statistics {
        size_t thread_count;
        std::vector<size_t> tasks_per_thread;
        size_t total_tasks_handled;
    };
    Statistics get_statistics() const;

private:
    struct ThreadContext {
        std::unique_ptr<asio::io_context> io_context;
        std::unique_ptr<asio::executor_work_guard<asio::io_context::executor_type>> work_guard;
        std::unique_ptr<std::thread> thread;
        std::atomic<size_t> task_count{0};
        
        ThreadContext();
        ~ThreadContext();
    };
    
    std::vector<ThreadContext> thread_contexts_;
    std::atomic<bool> running_{false};
    std::atomic<size_t> next_context_index_{0};
    
    void worker_thread_func(size_t thread_index);
    size_t select_least_loaded_context() const;
};

} // namespace magnet::async
```

### 🔍 **你需要思考的关键问题**

1. **线程数量策略**：
   - 使用`std::thread::hardware_concurrency()`合适吗？
   - 如何处理超线程？
   - 是否需要运行时动态调整？

2. **负载均衡算法**：
   - 轮询(Round-Robin) vs 最少任务(Least-Loaded)？
   - 如何准确统计每个线程的负载？
   - 任务计数何时增加，何时减少？

3. **工作守护(Work Guard)管理**：
   - 何时创建和销毁work_guard？
   - stop()时如何优雅退出？
   - 如何处理正在执行的任务？

---

## 🔧 **第3步：TaskScheduler设计**

### 🎯 **设计目标**
实现优先级任务调度，支持延迟任务和周期性任务。

### 💡 **核心设计思路**

```cpp
// include/magnet/async/task_scheduler.h
#pragma once
#include "event_loop_manager.h"
#include <functional>
#include <chrono>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>

namespace magnet::async {

enum class TaskPriority {
    LOW = 0,
    NORMAL = 1,
    HIGH = 2,
    CRITICAL = 3
};

class Task {
public:
    using TaskFunction = std::function<void()>;
    using TaskId = uint64_t;
    
    Task(TaskFunction func, TaskPriority priority = TaskPriority::NORMAL);
    
    void execute() const;
    TaskPriority priority() const { return priority_; }
    TaskId id() const { return id_; }
    
private:
    TaskFunction function_;
    TaskPriority priority_;
    TaskId id_;
    
    static TaskId generate_id();
};

class TaskScheduler {
public:
    explicit TaskScheduler(EventLoopManager& loop_manager);
    ~TaskScheduler();
    
    // 立即执行任务
    Task::TaskId post_task(TaskPriority priority, Task::TaskFunction func);
    
    // 延迟执行任务
    Task::TaskId post_delayed_task(
        std::chrono::milliseconds delay,
        TaskPriority priority,
        Task::TaskFunction func
    );
    
    // 周期性任务
    Task::TaskId post_periodic_task(
        std::chrono::milliseconds interval,
        TaskPriority priority,
        Task::TaskFunction func
    );
    
    // 取消任务
    bool cancel_task(Task::TaskId task_id);
    
    // 统计信息
    struct Statistics {
        size_t pending_tasks;
        size_t completed_tasks;
        std::array<size_t, 4> tasks_by_priority;
    };
    Statistics get_statistics() const;

private:
    EventLoopManager& loop_manager_;
    
    // 优先级队列
    struct TaskComparator {
        bool operator()(const std::shared_ptr<Task>& a, const std::shared_ptr<Task>& b) const;
    };
    
    std::priority_queue<
        std::shared_ptr<Task>,
        std::vector<std::shared_ptr<Task>>,
        TaskComparator
    > task_queue_;
    
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::atomic<bool> running_{true};
    std::thread scheduler_thread_;
    
    // 取消机制
    std::unordered_set<Task::TaskId> cancelled_tasks_;
    std::mutex cancelled_mutex_;
    
    // 统计信息
    mutable std::mutex stats_mutex_;
    Statistics statistics_;
    
    void scheduler_thread_func();
    bool is_task_cancelled(Task::TaskId task_id) const;
};

} // namespace magnet::async
```

### 🔍 **你需要解决的设计挑战**

1. **优先级队列实现**：
   - `std::priority_queue`的比较函数如何写？
   - 高优先级应该在队列的前面还是后面？
   - 相同优先级的任务如何排序？

2. **延迟任务处理**：
   - 使用`asio::steady_timer`还是自己的时间轮？
   - 如何与优先级队列结合？
   - 定时器取消如何处理？

3. **任务取消机制**：
   - 已经在执行的任务如何取消？
   - 取消列表的内存管理？
   - 取消操作的线程安全？

---

## 🔧 **第4步：实现步骤建议**

### 📅 **Day 1: EventLoopManager基础实现**

1. **创建基础结构**：
   ```bash
   # 创建头文件目录
   mkdir -p include/magnet/async
   
   # 实现EventLoopManager头文件
   # 实现EventLoopManager源文件
   ```

2. **核心功能实现**：
   - ThreadContext的构造和析构
   - start()和stop()方法
   - 基本的任务投递功能

3. **测试验证**：
   - 创建简单的测试程序
   - 验证多线程启动和停止
   - 测试基本任务投递

### 📅 **Day 2: EventLoopManager负载均衡**

1. **负载均衡算法**：
   - 实现任务计数机制
   - 实现最少负载选择算法
   - 添加统计信息收集

2. **性能优化**：
   - 优化原子操作使用
   - 减少锁竞争
   - 内存访问优化

### 📅 **Day 3: TaskScheduler基础实现**

1. **Task类实现**：
   - 任务ID生成
   - 任务执行接口
   - 优先级管理

2. **调度器核心**：
   - 优先级队列实现
   - 调度线程循环
   - 与EventLoopManager集成

### 📅 **Day 4: 延迟和周期任务**

1. **延迟任务**：
   - asio::steady_timer集成
   - 延迟任务队列管理
   - 时间精度处理

2. **周期性任务**：
   - 重复调度逻辑
   - 任务间隔管理
   - 取消机制

### 📅 **Day 5: 集成测试和优化**

1. **综合测试**：
   - 压力测试
   - 优先级验证
   - 性能基准测试

2. **文档完善**：
   - API文档
   - 使用示例
   - 性能指标

---

## 🧪 **测试策略**

### 🔍 **单元测试重点**

1. **EventLoopManager测试**：
   ```cpp
   // 测试线程启动和停止
   void test_lifecycle();
   
   // 测试负载均衡
   void test_load_balancing();
   
   // 测试并发任务投递
   void test_concurrent_posting();
   ```

2. **TaskScheduler测试**：
   ```cpp
   // 测试优先级排序
   void test_priority_ordering();
   
   // 测试延迟任务
   void test_delayed_tasks();
   
   // 测试任务取消
   void test_task_cancellation();
   ```

### 📊 **性能基准测试**

1. **吞吐量测试**：每秒能处理多少任务？
2. **延迟测试**：任务投递到执行的时间？
3. **负载均衡效果**：任务是否均匀分布？
4. **内存使用**：长时间运行的内存稳定性？

---

## 💡 **实现提示**

### ⚡ **基于你的Asio实验经验**

1. **实验2经验应用**：
   - work_guard的正确使用时机
   - io_context.run()的生命周期管理

2. **实验3经验应用**：
   - async_wait的非阻塞特性
   - 定时器的异步机制

3. **实验4经验应用**：
   - 对象生命周期管理
   - 避免悬挂指针

4. **实验6经验应用**：
   - 多线程io_context的最佳实践
   - 线程安全的任务分发

### 🚨 **常见陷阱避免**

1. **避免循环依赖**：EventLoopManager不应该依赖TaskScheduler
2. **正确的异常处理**：任务执行失败不应该影响调度器
3. **资源泄漏防护**：确保所有线程都能正确退出
4. **数据竞争避免**：仔细处理共享状态的访问

---

## 🎯 **验收标准**

完成后，你的async模块应该能够：

✅ **功能验收**：
- [ ] 能够启动和停止多个工作线程
- [ ] 能够按优先级调度任务
- [ ] 支持延迟任务和周期性任务
- [ ] 提供负载均衡的任务分发
- [ ] 支持任务取消

✅ **性能验收**：
- [ ] 1000个任务能在1秒内完成
- [ ] 任务分发延迟小于1毫秒
- [ ] 长时间运行内存稳定

✅ **稳定性验收**：
- [ ] 能够正确处理异常任务
- [ ] 多次启动停止无资源泄漏
- [ ] 高并发情况下无死锁

准备开始实现了吗？我建议从EventLoopManager的头文件开始！

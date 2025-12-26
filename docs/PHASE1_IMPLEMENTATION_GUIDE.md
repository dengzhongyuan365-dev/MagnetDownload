# 🚀 阶段1实现指南：核心基础设施

基于你的Asio实验经验，现在开始构建磁力下载器的核心基础设施！

## 📋 阶段1概览

### 🎯 **核心目标**
建立整个系统的"脊梁骨" - 多线程事件循环和任务调度系统，为后续所有模块提供高效、稳定的运行基础。

### 📦 **实现模块优先级**
1. ✅ **Logger** (最简单，其他模块都需要)
2. ✅ **Config** (配置支持)
3. ✅ **EventLoopManager** (核心事件循环)
4. ✅ **TaskScheduler** (任务调度系统)

---

## 🔧 模块1：Logger - 多级日志系统

### 🎯 **设计目标**
- 线程安全的日志输出
- 支持多个日志级别
- 可配置的输出格式
- 高性能异步日志

### 🏗️ **核心设计思路**

基于你在实验6中掌握的多线程知识：

```cpp
// include/core/logger.h
#pragma once
#include <iostream>
#include <fstream>
#include <sstream>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <queue>
#include <condition_variable>
#include <chrono>

namespace magnet {

enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARN = 2,
    ERROR = 3,
    FATAL = 4
};

struct LogEntry {
    LogLevel level;
    std::string message;
    std::chrono::system_clock::time_point timestamp;
    std::thread::id thread_id;
    
    LogEntry(LogLevel lvl, std::string msg) 
        : level(lvl), message(std::move(msg))
        , timestamp(std::chrono::system_clock::now())
        , thread_id(std::this_thread::get_id()) {}
};

class Logger {
public:
    static Logger& instance();
    
    void set_level(LogLevel level);
    void set_output_file(const std::string& filename);
    
    void log(LogLevel level, const std::string& message);
    
    // 便捷宏定义后会用到的接口
    void debug(const std::string& message) { log(LogLevel::DEBUG, message); }
    void info(const std::string& message) { log(LogLevel::INFO, message); }
    void warn(const std::string& message) { log(LogLevel::WARN, message); }
    void error(const std::string& message) { log(LogLevel::ERROR, message); }
    void fatal(const std::string& message) { log(LogLevel::FATAL, message); }
    
    ~Logger();

private:
    Logger();
    
    // 异步日志处理
    void worker_thread();
    std::string format_entry(const LogEntry& entry) const;
    std::string level_to_string(LogLevel level) const;
    
    std::atomic<LogLevel> min_level_{LogLevel::INFO};
    std::queue<LogEntry> log_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::atomic<bool> stop_flag_{false};
    std::thread worker_thread_;
    
    std::unique_ptr<std::ofstream> file_output_;
    std::mutex output_mutex_;
};

// 便捷宏定义
#define LOG_DEBUG(msg) Logger::instance().debug(msg)
#define LOG_INFO(msg) Logger::instance().info(msg)
#define LOG_WARN(msg) Logger::instance().warn(msg)
#define LOG_ERROR(msg) Logger::instance().error(msg)
#define LOG_FATAL(msg) Logger::instance().fatal(msg)

// 格式化日志宏
#define LOG_INFO_F(fmt, ...) do { \
    char buffer[1024]; \
    snprintf(buffer, sizeof(buffer), fmt, __VA_ARGS__); \
    Logger::instance().info(buffer); \
} while(0)

} // namespace magnet
```

### 💡 **实现要点**

#### **异步日志队列**
基于你在实验6学到的线程安全队列思想：
- 主线程快速投递日志到队列
- 专门的工作线程负责格式化和写入
- 避免IO操作阻塞业务线程

#### **单例模式的线程安全实现**
```cpp
Logger& Logger::instance() {
    static Logger instance;  // C++11保证线程安全的初始化
    return instance;
}
```

#### **条件变量的正确使用**
```cpp
void Logger::log(LogLevel level, const std::string& message) {
    if (level < min_level_.load()) {
        return;  // 早期过滤，避免不必要的字符串操作
    }
    
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        log_queue_.emplace(level, message);
    }
    queue_cv_.notify_one();  // 通知工作线程
}
```

### 🧪 **测试用例设计**
```cpp
// tests/test_logger.cpp
void test_logger_basic() {
    Logger& logger = Logger::instance();
    logger.set_level(LogLevel::DEBUG);
    
    LOG_INFO("Logger测试开始");
    LOG_DEBUG("这是调试信息");
    LOG_ERROR("这是错误信息");
    
    // 等待异步日志写入完成
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

void test_logger_multithread() {
    const int thread_count = 10;
    const int messages_per_thread = 100;
    
    std::vector<std::thread> threads;
    
    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back([i, messages_per_thread]() {
            for (int j = 0; j < messages_per_thread; ++j) {
                LOG_INFO_F("线程%d消息%d", i, j);
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    LOG_INFO("多线程日志测试完成");
}
```

---

## 🔧 模块2：Config - 配置管理系统

### 🎯 **设计目标**
- 支持从文件和命令行读取配置
- 类型安全的配置访问
- 配置热重载支持
- 默认值管理

### 🏗️ **核心设计思路**

```cpp
// include/core/config.h
#pragma once
#include <unordered_map>
#include <string>
#include <variant>
#include <optional>
#include <mutex>

namespace magnet {

// 支持的配置值类型
using ConfigValue = std::variant<std::string, int, double, bool>;

class Config {
public:
    static Config& instance();
    
    // 从文件加载配置
    bool load_from_file(const std::string& filename);
    
    // 命令行参数解析
    bool parse_command_line(int argc, char* argv[]);
    
    // 获取配置值（带默认值）
    template<typename T>
    T get(const std::string& key, const T& default_value) const;
    
    // 设置配置值
    template<typename T>
    void set(const std::string& key, const T& value);
    
    // 检查配置是否存在
    bool has(const std::string& key) const;
    
    // 获取所有配置（用于调试）
    std::unordered_map<std::string, ConfigValue> get_all() const;

private:
    Config() = default;
    
    mutable std::shared_mutex config_mutex_;  // 读写锁
    std::unordered_map<std::string, ConfigValue> config_data_;
    
    // 字符串到其他类型的转换
    template<typename T>
    std::optional<T> string_to_value(const std::string& str) const;
};

// 便捷宏
#define GET_CONFIG(key, default_val) Config::instance().get(key, default_val)
#define SET_CONFIG(key, val) Config::instance().set(key, val)

} // namespace magnet
```

### 💡 **实现要点**

#### **类型安全的配置访问**
基于你学过的`std::variant`：
```cpp
template<typename T>
T Config::get(const std::string& key, const T& default_value) const {
    std::shared_lock<std::shared_mutex> lock(config_mutex_);
    
    auto it = config_data_.find(key);
    if (it == config_data_.end()) {
        return default_value;
    }
    
    try {
        return std::get<T>(it->second);
    } catch (const std::bad_variant_access&) {
        LOG_WARN_F("配置项 %s 类型不匹配，使用默认值", key.c_str());
        return default_value;
    }
}
```

#### **配置文件格式设计**
简单的key=value格式：
```ini
# MagnetDownloader配置文件
log_level=INFO
max_connections=50
download_dir=./downloads
listen_port=6881
dht_bootstrap_nodes=router.bittorrent.com:6881,dht.transmissionbt.com:6881
```

### 🧪 **测试用例设计**
```cpp
void test_config_basic() {
    Config& config = Config::instance();
    
    // 测试基本的set/get
    config.set("test_string", std::string("hello"));
    config.set("test_int", 42);
    config.set("test_bool", true);
    
    assert(config.get<std::string>("test_string", "") == "hello");
    assert(config.get<int>("test_int", 0) == 42);
    assert(config.get<bool>("test_bool", false) == true);
    
    LOG_INFO("配置系统基础测试通过");
}
```

---

## 🔧 模块3：EventLoopManager - 事件循环管理器

### 🎯 **设计目标**
- 管理多个工作线程的io_context
- 负载均衡的任务分配
- 优雅的启动和停止
- 线程池动态调整
- **支持任务优先级处理**（与TaskScheduler协同工作）

### 🤔 **设计决策：事件循环是否需要优先级？**

这是一个重要的架构决策。我们有两种方案：

#### **方案A：简单分层（推荐学习阶段）**
- **EventLoopManager**: 负责负载均衡，所有io_context平等处理任务
- **TaskScheduler**: 负责优先级排序，然后将任务提交给EventLoopManager

**优点**：
- 职责清晰，模块间解耦
- EventLoopManager实现简单
- 易于理解和调试

**缺点**：
- 所有任务都要经过TaskScheduler，增加一层间接性
- 对于紧急网络事件（如连接断开）响应可能不够快

#### **方案B：混合优先级（产品阶段）**
- **EventLoopManager**: 提供多个优先级队列和专用线程池
- **TaskScheduler**: 根据任务类型选择合适的事件循环

**优点**：
- 网络事件可直接进入高优先级事件循环
- 更好的实时响应性能
- 系统吞吐量更高

**缺点**：
- 实现复杂度大幅增加
- 线程间负载不均衡的风险
- 调试难度提升

#### **我们的渐进策略**

基于你的学习目标，建议：

1. **阶段1**: 采用方案A，专注理解基础概念
2. **阶段4-6**: 根据实际性能需求，考虑升级到方案B

这样你既能掌握清晰的架构思维，又不会一开始就陷入过度复杂的设计中。

### 🏗️ **核心设计思路**

直接应用你在实验6中学到的多线程io_context模式：

```cpp
// include/core/event_loop_manager.h
#pragma once
#include <asio.hpp>
#include <vector>
#include <thread>
#include <atomic>
#include <memory>
#include <functional>

namespace magnet {

class EventLoopManager {
public:
    explicit EventLoopManager(size_t thread_count = std::thread::hardware_concurrency());
    ~EventLoopManager();
    
    // 启动所有工作线程
    void start();
    
    // 停止所有工作线程
    void stop();
    
    // 获取负载最轻的io_context
    asio::io_context& get_io_context();
    
    // 投递任务到指定的io_context
    template<typename Handler>
    void post(Handler&& handler);
    
    // 投递任务到负载最轻的io_context
    template<typename Handler>
    void post_to_least_loaded(Handler&& handler);
    
    // 获取统计信息
    struct Statistics {
        size_t thread_count;
        std::vector<size_t> tasks_per_thread;
        size_t total_tasks_handled;
    };
    Statistics get_statistics() const;
    
    bool is_running() const { return running_.load(); }

private:
    struct ThreadContext {
        std::unique_ptr<asio::io_context> io_context;
        std::unique_ptr<asio::executor_work_guard<asio::io_context::executor_type>> work_guard;
        std::unique_ptr<std::thread> thread;
        std::atomic<size_t> task_count{0};  // 负载均衡用
        
        ThreadContext() 
            : io_context(std::make_unique<asio::io_context>())
            , work_guard(std::make_unique<asio::executor_work_guard<asio::io_context::executor_type>>(
                asio::make_work_guard(*io_context))) {}
    };
    
    std::vector<ThreadContext> thread_contexts_;
    std::atomic<bool> running_{false};
    std::atomic<size_t> next_context_index_{0};  // 轮询分配
    
    void worker_thread_func(size_t index);
    size_t select_least_loaded_context() const;
};

template<typename Handler>
void EventLoopManager::post(Handler&& handler) {
    if (!running_.load()) {
        LOG_WARN("EventLoopManager未运行，任务被丢弃");
        return;
    }
    
    auto& context = get_io_context();
    asio::post(context, std::forward<Handler>(handler));
}

template<typename Handler>
void EventLoopManager::post_to_least_loaded(Handler&& handler) {
    if (!running_.load()) {
        LOG_WARN("EventLoopManager未运行，任务被丢弃");
        return;
    }
    
    size_t index = select_least_loaded_context();
    auto& context = thread_contexts_[index];
    context.task_count.fetch_add(1);
    
    asio::post(*context.io_context, [handler = std::forward<Handler>(handler), &context]() {
        handler();
        context.task_count.fetch_sub(1);
    });
}

} // namespace magnet
```

### 💡 **实现要点**

#### **智能负载均衡**
基于实际任务计数而不是简单轮询：
```cpp
size_t EventLoopManager::select_least_loaded_context() const {
    size_t min_tasks = SIZE_MAX;
    size_t best_index = 0;
    
    for (size_t i = 0; i < thread_contexts_.size(); ++i) {
        size_t task_count = thread_contexts_[i].task_count.load();
        if (task_count < min_tasks) {
            min_tasks = task_count;
            best_index = i;
        }
    }
    
    return best_index;
}
```

#### **优雅停止机制**
基于你掌握的work_guard机制：
```cpp
void EventLoopManager::stop() {
    if (!running_.exchange(false)) {
        return;  // 已经停止
    }
    
    LOG_INFO("正在停止EventLoopManager...");
    
    // 1. 释放所有work_guard，允许io_context.run()退出
    for (auto& context : thread_contexts_) {
        context.work_guard.reset();
    }
    
    // 2. 等待所有线程完成
    for (auto& context : thread_contexts_) {
        if (context.thread && context.thread->joinable()) {
            context.thread->join();
        }
    }
    
    LOG_INFO("EventLoopManager已停止");
}
```

### 🧪 **测试用例设计**
```cpp
void test_event_loop_manager() {
    EventLoopManager manager(4);
    manager.start();
    
    std::atomic<int> completed_tasks{0};
    const int total_tasks = 100;
    
    // 投递100个任务
    for (int i = 0; i < total_tasks; ++i) {
        manager.post([&completed_tasks, i]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            completed_tasks.fetch_add(1);
            if (i % 10 == 0) {
                LOG_INFO_F("完成任务 %d", i);
            }
        });
    }
    
    // 等待所有任务完成
    while (completed_tasks.load() < total_tasks) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    auto stats = manager.get_statistics();
    LOG_INFO_F("统计：%zu个线程，总计处理%zu个任务", stats.thread_count, stats.total_tasks_handled);
    
    manager.stop();
}
```

---

## 🔧 模块4：TaskScheduler - 任务调度系统

### 🎯 **设计目标**
- 支持任务优先级
- 延迟任务执行
- 周期性任务
- 任务取消机制
- **与EventLoopManager协同工作**

### 🤝 **模块协作关系**

在我们的分层设计中：

```
应用层任务
    ↓
TaskScheduler（优先级排序、延迟调度）
    ↓
EventLoopManager（负载均衡分发）
    ↓
多个io_context工作线程（实际执行）
```

**核心协作原理**：
- TaskScheduler维护优先级队列，确保重要任务先执行
- TaskScheduler使用定时器处理延迟任务和周期性任务
- 当任务就绪时，TaskScheduler调用EventLoopManager的负载均衡接口
- EventLoopManager选择最合适的工作线程执行具体任务

这种设计让每个模块专注自己的核心职责，同时保持高效协作。

### 🏗️ **核心设计思路**

```cpp
// include/core/task_scheduler.h
#pragma once
#include "event_loop_manager.h"
#include <functional>
#include <chrono>
#include <queue>
#include <memory>
#include <atomic>

namespace magnet {

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
    
    Task(TaskFunction func, TaskPriority priority = TaskPriority::NORMAL)
        : function_(std::move(func)), priority_(priority), id_(generate_id()) {}
    
    void execute() const { if (function_) function_(); }
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
    
    // 获取统计信息
    struct Statistics {
        size_t pending_tasks;
        size_t completed_tasks;
        std::array<size_t, 4> tasks_by_priority;  // 按优先级统计
    };
    Statistics get_statistics() const;

private:
    EventLoopManager& loop_manager_;
    
    // 优先级队列比较器
    struct TaskComparator {
        bool operator()(const std::shared_ptr<Task>& a, const std::shared_ptr<Task>& b) const {
            return a->priority() < b->priority();  // 高优先级先执行
        }
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

} // namespace magnet
```

### 💡 **实现要点**

#### **优先级队列的使用**
基于C++标准库的priority_queue：
```cpp
void TaskScheduler::scheduler_thread_func() {
    while (running_.load()) {
        std::shared_ptr<Task> task;
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this]() {
                return !task_queue_.empty() || !running_.load();
            });
            
            if (!running_.load()) break;
            
            task = task_queue_.top();
            task_queue_.pop();
        }
        
        // 检查任务是否被取消
        if (!is_task_cancelled(task->id())) {
            // 提交到EventLoopManager执行
            loop_manager_.post_to_least_loaded([task]() {
                task->execute();
            });
            
            // 更新统计
            {
                std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                statistics_.completed_tasks++;
            }
        }
    }
}
```

#### **延迟任务的实现**
使用asio::steady_timer：
```cpp
Task::TaskId TaskScheduler::post_delayed_task(
    std::chrono::milliseconds delay,
    TaskPriority priority,
    Task::TaskFunction func) {
    
    auto timer = std::make_shared<asio::steady_timer>(
        loop_manager_.get_io_context(), delay);
    
    auto task = std::make_shared<Task>(std::move(func), priority);
    Task::TaskId task_id = task->id();
    
    timer->async_wait([this, task, timer](const asio::error_code& ec) {
        if (!ec && !is_task_cancelled(task->id())) {
            // 将延迟任务加入优先级队列
            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                task_queue_.push(task);
            }
            queue_cv_.notify_one();
        }
    });
    
    return task_id;
}
```

### 🧪 **测试用例设计**
```cpp
void test_task_scheduler() {
    EventLoopManager loop_manager(2);
    loop_manager.start();
    
    TaskScheduler scheduler(loop_manager);
    
    std::atomic<int> execution_order{0};
    
    // 测试优先级排序
    auto low_task = scheduler.post_task(TaskPriority::LOW, [&]() {
        int order = execution_order.fetch_add(1);
        LOG_INFO_F("低优先级任务执行，顺序: %d", order);
    });
    
    auto high_task = scheduler.post_task(TaskPriority::HIGH, [&]() {
        int order = execution_order.fetch_add(1);
        LOG_INFO_F("高优先级任务执行，顺序: %d", order);
    });
    
    auto critical_task = scheduler.post_task(TaskPriority::CRITICAL, [&]() {
        int order = execution_order.fetch_add(1);
        LOG_INFO_F("关键优先级任务执行，顺序: %d", order);
    });
    
    // 测试延迟任务
    scheduler.post_delayed_task(std::chrono::milliseconds(100), TaskPriority::NORMAL, []() {
        LOG_INFO("延迟任务执行");
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    auto stats = scheduler.get_statistics();
    LOG_INFO_F("调度器统计：完成%zu个任务", stats.completed_tasks);
    
    loop_manager.stop();
}
```

---

## 🎪 阶段1集成测试

### 🎯 **目标：Hello MagnetDownloader**
创建一个综合测试程序，验证所有模块协同工作：

```cpp
// examples/hello_magnet_downloader.cpp
#include "core/logger.h"
#include "core/config.h"
#include "core/event_loop_manager.h"
#include "core/task_scheduler.h"
#include <iostream>
#include <thread>

using namespace magnet;

int main() {
    try {
        std::cout << "🚀 MagnetDownloader 阶段1演示程序" << std::endl;
        std::cout << "================================================" << std::endl;
        
        // 1. 初始化日志系统
        Logger::instance().set_level(LogLevel::DEBUG);
        LOG_INFO("🔧 日志系统初始化完成");
        
        // 2. 加载配置
        Config::instance().set("worker_threads", 4);
        Config::instance().set("max_tasks", 100);
        LOG_INFO("⚙️ 配置系统初始化完成");
        
        // 3. 启动事件循环管理器
        int thread_count = GET_CONFIG("worker_threads", 4);
        EventLoopManager loop_manager(thread_count);
        loop_manager.start();
        LOG_INFO_F("🔄 事件循环管理器启动，%d个工作线程", thread_count);
        
        // 4. 启动任务调度器
        TaskScheduler scheduler(loop_manager);
        LOG_INFO("📋 任务调度器启动完成");
        
        // 5. 演示多种任务
        std::atomic<int> demo_counter{0};
        
        // 高优先级任务
        for (int i = 0; i < 5; ++i) {
            scheduler.post_task(TaskPriority::HIGH, [&demo_counter, i]() {
                demo_counter.fetch_add(1);
                LOG_INFO_F("🔥 高优先级任务 %d 执行", i);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            });
        }
        
        // 普通任务
        for (int i = 0; i < 10; ++i) {
            scheduler.post_task(TaskPriority::NORMAL, [&demo_counter, i]() {
                demo_counter.fetch_add(1);
                LOG_INFO_F("⚡ 普通任务 %d 执行", i);
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
            });
        }
        
        // 延迟任务
        scheduler.post_delayed_task(
            std::chrono::milliseconds(1000),
            TaskPriority::CRITICAL,
            [&demo_counter]() {
                demo_counter.fetch_add(1);
                LOG_INFO("⏰ 延迟任务执行");
            }
        );
        
        // 周期性任务（执行3次）
        std::shared_ptr<std::atomic<int>> periodic_count = std::make_shared<std::atomic<int>>(0);
        std::function<void()> periodic_task = [&scheduler, periodic_count, &demo_counter]() {
            int count = periodic_count->fetch_add(1);
            demo_counter.fetch_add(1);
            LOG_INFO_F("🔄 周期性任务执行 %d 次", count + 1);
            
            if (count < 2) {  // 执行3次
                scheduler.post_delayed_task(
                    std::chrono::milliseconds(500),
                    TaskPriority::LOW,
                    periodic_task
                );
            }
        };
        scheduler.post_task(TaskPriority::LOW, periodic_task);
        
        // 🎯 展示优先级效果的特殊测试
        LOG_INFO("🎭 开始优先级效果演示...");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        // 同时提交不同优先级任务，观察执行顺序
        for (int i = 0; i < 3; ++i) {
            scheduler.post_task(TaskPriority::LOW, [i]() {
                LOG_INFO_F("🐌 低优先级任务 %d 开始执行", i);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                LOG_INFO_F("🐌 低优先级任务 %d 完成", i);
            });
        }
        
        // 稍后提交高优先级任务，应该插队执行
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        scheduler.post_task(TaskPriority::CRITICAL, []() {
            LOG_INFO("🚨 紧急任务插队执行！");
        });
        
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        
        // 6. 等待所有任务完成
        LOG_INFO("⏳ 等待所有任务完成...");
        const int expected_tasks = 5 + 10 + 1 + 3 + 3 + 1;  // 23个任务（新增优先级演示任务）
        
        while (demo_counter.load() < expected_tasks) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            auto loop_stats = loop_manager.get_statistics();
            auto scheduler_stats = scheduler.get_statistics();
            
            if (demo_counter.load() % 5 == 0) {
                LOG_INFO_F("📊 进度：%d/%d 任务完成", demo_counter.load(), expected_tasks);
            }
        }
        
        // 7. 展示统计信息
        auto loop_stats = loop_manager.get_statistics();
        auto scheduler_stats = scheduler.get_statistics();
        
        std::cout << "\n📊 最终统计信息：" << std::endl;
        std::cout << "====================" << std::endl;
        std::cout << "事件循环管理器：" << std::endl;
        std::cout << "  - 工作线程数: " << loop_stats.thread_count << std::endl;
        std::cout << "  - 总处理任务: " << loop_stats.total_tasks_handled << std::endl;
        
        std::cout << "任务调度器：" << std::endl;
        std::cout << "  - 完成任务数: " << scheduler_stats.completed_tasks << std::endl;
        std::cout << "  - 高优先级: " << scheduler_stats.tasks_by_priority[3] << std::endl;
        std::cout << "  - 普通优先级: " << scheduler_stats.tasks_by_priority[1] << std::endl;
        std::cout << "  - 低优先级: " << scheduler_stats.tasks_by_priority[0] << std::endl;
        
        // 8. 优雅停止
        LOG_INFO("🛑 开始优雅停止...");
        loop_manager.stop();
        
        std::cout << "\n✅ MagnetDownloader 阶段1演示完成！" << std::endl;
        std::cout << "💡 所有核心基础设施模块工作正常" << std::endl;
        
        return 0;
        
    } catch (const std::exception& e) {
        LOG_FATAL(std::string("程序异常退出: ") + e.what());
        std::cerr << "❌ 程序异常退出: " << e.what() << std::endl;
        return 1;
    }
}
```

---

## 📋 阶段1验收标准

### ✅ **功能验收**
- [ ] Logger能够在多线程环境下正常输出日志
- [ ] Config能够正确加载和访问配置
- [ ] EventLoopManager能够启动多个工作线程
- [ ] TaskScheduler能够按优先级调度任务
- [ ] 延迟任务和周期性任务正常工作
- [ ] 程序能够优雅启动和停止

### ✅ **性能验收**
- [ ] 1000个任务能在5秒内完成
- [ ] 内存使用稳定，无明显泄漏
- [ ] CPU使用合理分布在所有工作线程

### ✅ **稳定性验收**
- [ ] 程序能连续运行30分钟无崩溃
- [ ] 所有异常都有正确的错误处理
- [ ] 多次启动停止无资源泄漏

---

## 🎓 阶段1学习成果

完成阶段1后，你将掌握：

### 🔧 **技术技能**
- **多线程编程**：事件循环、任务队列、线程安全
- **现代C++特性**：智能指针、原子操作、条件变量
- **设计模式**：单例、RAII、观察者模式
- **异步编程**：基于回调的异步任务处理

### 🏗️ **架构能力**
- **模块化设计**：如何设计松耦合的系统架构
- **性能优化**：负载均衡、优先级调度
- **可扩展性**：如何设计支持后续功能扩展的基础框架

### 🎯 **下一步预告**
阶段1完成后，你就有了一个强大的"引擎"，可以支撑任何复杂的异步网络应用。

阶段2我们将实现磁力链接解析，让这个"引擎"开始理解BitTorrent世界的"语言"！

---

## 💡 实现建议

### 🚀 **开发顺序**
1. **Day 1-2**: 实现Logger，立即可用于调试
2. **Day 3**: 实现Config，支持后续模块配置
3. **Day 4-6**: 实现EventLoopManager，最核心的模块
4. **Day 7-9**: 实现TaskScheduler，添加调度能力
5. **Day 10**: 集成测试和优化

### ⚠️ **常见陷阱**
- **过早优化**：先保证功能正确，再考虑性能
- **异常安全**：确保所有资源都有正确的RAII管理
- **线程安全**：仔细审查所有共享状态的访问

### 🔍 **调试技巧**
- 充分利用你的Logger系统
- 使用线程ID来追踪任务执行
- 添加统计信息帮助性能分析

---

## 🚀 未来扩展思考

### 📈 **性能优化方向**

当你在后续阶段遇到性能瓶颈时，可以考虑这些优化：

1. **EventLoopManager升级**：
   ```cpp
   // 支持专用网络IO线程池
   enum class ThreadPoolType {
       GENERAL,     // 通用任务处理
       NETWORK_IO,  // 网络IO专用
       DISK_IO,     // 磁盘IO专用
       DHT_CRAWLER  // DHT爬虫专用
   };
   ```

2. **TaskScheduler增强**：
   ```cpp
   // 任务亲和性（任务绑定到特定线程）
   TaskId post_task_with_affinity(
       TaskPriority priority,
       ThreadPoolType pool_type,
       TaskFunction func
   );
   ```

3. **智能负载均衡**：
   ```cpp
   // 基于任务类型的智能路由
   class SmartTaskRouter {
       // 网络任务 → 网络专用线程
       // 磁盘任务 → IO专用线程
       // 计算任务 → 通用线程池
   };
   ```

### 🎯 **架构演进路径**

```
阶段1: 基础设施 → 学会基本概念
阶段2-3: 网络功能 → 发现性能热点
阶段4-5: 数据处理 → 明确优化需求
阶段6: 性能优化 → 应用高级技巧
```

这种渐进式的架构演进，让你既不会一开始就过度设计，又保证系统具备良好的扩展性！

---

## 🎉 开始你的征程

准备好开始实现了吗？从Logger开始，一步步构建你的磁力下载器基础设施！

记住：**优秀的架构师不是一开始就设计出完美系统，而是能够随着需求变化优雅地演进系统**。

让我们开始这个精彩的学习之旅！ 🚀

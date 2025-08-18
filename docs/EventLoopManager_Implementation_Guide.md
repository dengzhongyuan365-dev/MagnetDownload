# EventLoopManager.cpp 实现指南

## 构造函数和析构函数

### 1. 构造函数
```cpp
EventLoopManager::EventLoopManager(size_t thread_count) {
    // 参数验证
    if (thread_count == 0) {
        thread_count = 1;  // 至少需要1个线程
    }
    if (thread_count > 128) {
        thread_count = 128;  // 防止创建过多线程
    }
    
    // 预留容器空间
    contexts_.reserve(thread_count);
    threads_.reserve(thread_count);
    work_guards.reserve(thread_count);
    
    // 创建io_context对象
    for (size_t i = 0; i < thread_count; ++i) {
        contexts_.emplace_back(std::make_unique<asio::io_context>());
    }
    
    // 日志输出
    // LOG_INFO("EventLoopManager created with {} threads", thread_count);
}
```

### 2. 析构函数
```cpp
EventLoopManager::~EventLoopManager() {
    if (running_.load()) {
        stop();
        wait_for_stop();
    }
    cleanup_resources();
    // LOG_INFO("EventLoopManager destroyed");
}
```

## 生命周期管理接口

### 3. start()
```cpp
void EventLoopManager::start() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    if (running_.load()) {
        throw std::runtime_error("EventLoopManager is already running");
    }
    
    try {
        // 创建work_guard
        for (auto& context : contexts_) {
            work_guards.emplace_back(asio::make_work_guard(*context));
        }
        
        // 启动工作线程
        for (size_t i = 0; i < contexts_.size(); ++i) {
            threads_.emplace_back(&EventLoopManager::work_thread_main, this, i);
        }
        
        running_.store(true);
        // LOG_INFO("EventLoopManager started with {} threads", contexts_.size());
        
    } catch (...) {
        // 启动失败时清理
        cleanup_resources();
        throw;
    }
}
```

### 4. stop()
```cpp
void EventLoopManager::stop() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    if (!running_.load()) {
        return;  // 已经停止
    }
    
    // 移除work_guard，允许io_context退出
    work_guards.clear();
    
    // 停止所有io_context
    for (auto& context : contexts_) {
        context->stop();
    }
    
    running_.store(false);
    // LOG_INFO("EventLoopManager stop requested");
}
```

### 5. wait_for_stop()
```cpp
void EventLoopManager::wait_for_stop() {
    // 等待所有线程完成
    for (auto& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    cleanup_resources();
    // LOG_INFO("EventLoopManager completely stopped");
}
```

### 6. is_running()
```cpp
bool EventLoopManager::is_running() const {
    return running_.load();
}
```

## 任务投递接口（模板实现在头文件中）

### 7. post() - 头文件中实现
```cpp
template<typename Task>
void EventLoopManager::post(Task&& task) {
    if (!running_.load()) {
        throw std::runtime_error("EventLoopManager is not running");
    }
    
    size_t context_index = select_optimal_context();
    auto& context = *contexts_[context_index];
    
    // 包装任务以捕获异常
    asio::post(context, [task = std::forward<Task>(task)]() {
        try {
            task();
        } catch (const std::exception& e) {
            // LOG_ERROR("Task execution exception: {}", e.what());
        } catch (...) {
            // LOG_ERROR("Task execution unknown exception");
        }
    });
}
```

### 8. dispatch() - 头文件中实现
```cpp
template<typename Task>
void EventLoopManager::dispatch(Task&& task) {
    if (!running_.load()) {
        throw std::runtime_error("EventLoopManager is not running");
    }
    
    size_t context_index = select_optimal_context();
    auto& context = *contexts_[context_index];
    
    // dispatch会在当前线程是io_context线程时直接执行
    asio::dispatch(context, [task = std::forward<Task>(task)]() {
        try {
            task();
        } catch (const std::exception& e) {
            // LOG_ERROR("Task dispatch exception: {}", e.what());
        } catch (...) {
            // LOG_ERROR("Task dispatch unknown exception");
        }
    });
}
```

### 9. post_with_result() - 头文件中实现
```cpp
template<typename Task>
auto EventLoopManager::post_with_result(Task&& task) -> std::future<decltype(task())> {
    using ResultType = decltype(task());
    
    if (!running_.load()) {
        throw std::runtime_error("EventLoopManager is not running");
    }
    
    auto promise = std::make_shared<std::promise<ResultType>>();
    auto future = promise->get_future();
    
    size_t context_index = select_optimal_context();
    auto& context = *contexts_[context_index];
    
    asio::post(context, [task = std::forward<Task>(task), promise]() {
        try {
            if constexpr (std::is_void_v<ResultType>) {
                task();
                promise->set_value();
            } else {
                auto result = task();
                promise->set_value(std::move(result));
            }
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    });
    
    return future;
}
```

## 监控和统计接口

### 10. get_thread_count()
```cpp
size_t EventLoopManager::get_thread_count() const {
    return contexts_.size();
}
```

### 11. get_pending_tasks()
```cpp
size_t EventLoopManager::get_pending_tasks() const {
    if (!running_.load()) {
        return 0;
    }
    
    size_t total_pending = 0;
    for (const auto& context : contexts_) {
        // 注意：这是估算值，asio没有直接的API获取精确的待处理任务数
        // 可以实现自己的计数器来跟踪
        if (!context->stopped()) {
            total_pending += 1;  // 简化实现
        }
    }
    return total_pending;
}
```

### 12. get_per_thread_load()
```cpp
std::vector<size_t> EventLoopManager::get_per_thread_load() const {
    std::vector<size_t> loads;
    loads.reserve(contexts_.size());
    
    for (const auto& context : contexts_) {
        // 简化实现：返回运行状态
        loads.push_back(context->stopped() ? 0 : 1);
    }
    
    return loads;
}
```

## 高级接口

### 13. get_io_context()
```cpp
asio::io_context& EventLoopManager::get_io_context() {
    if (contexts_.empty()) {
        throw std::runtime_error("No io_context available");
    }
    
    size_t index = select_optimal_context();
    return *contexts_[index];
}
```

### 14. get_io_context(size_t index)
```cpp
asio::io_context& EventLoopManager::get_io_context(size_t index) {
    if (index >= contexts_.size()) {
        throw std::out_of_range("io_context index out of range");
    }
    
    return *contexts_[index];
}
```

## 私有方法实现

### 15. work_thread_main()
```cpp
void EventLoopManager::work_thread_main(size_t thread_index) {
    try {
        // LOG_INFO("Worker thread {} started", thread_index);
        
        auto& context = *contexts_[thread_index];
        
        // 进入事件循环
        size_t handlers_executed = context.run();
        
        // LOG_INFO("Worker thread {} stopped, executed {} handlers", 
        //          thread_index, handlers_executed);
                 
    } catch (const std::exception& e) {
        // LOG_ERROR("Worker thread {} exception: {}", thread_index, e.what());
    } catch (...) {
        // LOG_ERROR("Worker thread {} unknown exception", thread_index);
    }
}
```

### 16. select_optimal_context()
```cpp
size_t EventLoopManager::select_optimal_context() {
    // Round-Robin负载均衡
    size_t index = next_context_index_.fetch_add(1, std::memory_order_relaxed);
    return index % contexts_.size();
}
```

### 17. cleanup_resources()
```cpp
void EventLoopManager::cleanup_resources() {
    // 确保work_guards被清理
    work_guards.clear();
    
    // 清理线程容器（线程对象应该已经被join）
    threads_.clear();
    
    // 清理io_context容器
    contexts_.clear();
    
    // 重置状态
    next_context_index_.store(0);
    running_.store(false);
}
```

## 重要注意事项

### 头文件中的模板实现

由于模板函数需要在头文件中实现，您需要在头文件的类定义后添加：

```cpp
// 在 EventLoopManager.h 的类定义后添加

template<typename Task>
void EventLoopManager::post(Task&& task) {
    // 实现代码...
}

template<typename Task>
void EventLoopManager::dispatch(Task&& task) {
    // 实现代码...
}

template<typename Task>
auto EventLoopManager::post_with_result(Task&& task) -> std::future<decltype(task())> {
    // 实现代码...
}
```

### 错误处理策略

1. **构造函数**: 参数验证，资源分配失败时抛出异常
2. **启动函数**: 状态检查，启动失败时清理资源
3. **任务投递**: 运行状态检查，任务异常隔离
4. **工作线程**: 异常捕获，避免线程崩溃

### 线程安全保证

1. **原子变量**: `running_`, `next_context_index_`
2. **互斥锁**: `state_mutex_` 保护状态变更
3. **异常安全**: RAII模式，资源自动清理

这个实现提供了完整的事件循环管理功能，支持高并发任务处理，并具有良好的错误处理和资源管理机制。

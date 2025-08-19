// MagnetDownload - Event Loop Manager Implementation
// 多线程事件循环管理器的具体实现

#include <magnet/async/event_loop_manager.h>
#include <stdexcept>
#include <algorithm>

namespace magnet::async {

// ThreadContext实现

EventLoopManager::ThreadContext::ThreadContext() 
    : io_context(std::make_unique<asio::io_context>())
    , work_guard(std::make_unique<asio::executor_work_guard<asio::io_context::executor_type>>(
        asio::make_work_guard(*io_context))) {
}

EventLoopManager::ThreadContext::~ThreadContext() {
    // 确保work_guard先被销毁，然后是线程，最后是io_context
    if (work_guard) {
        work_guard.reset();
    }
    
    if (thread && thread->joinable()) {
        thread->join();
    }
}

// EventLoopManager实现

EventLoopManager::EventLoopManager(size_t thread_count) {
    if (thread_count == 0) {
        thread_count = std::thread::hardware_concurrency();
        if (thread_count == 0) {
            thread_count = 4; // 后备默认值
        }
    }
    
    thread_contexts_.reserve(thread_count);
    for (size_t i = 0; i < thread_count; ++i) {
        thread_contexts_.emplace_back(std::make_unique<ThreadContext>());
    }
}

EventLoopManager::~EventLoopManager() {
    stop();
}

void EventLoopManager::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        throw std::runtime_error("EventLoopManager已经在运行中");
    }
    
    try {
        // 启动所有工作线程
        for (size_t i = 0; i < thread_contexts_.size(); ++i) {
            auto& context_ptr = thread_contexts_[i];
            
            context_ptr->thread = std::make_unique<std::thread>([this, i]() {
                worker_thread_func(i);
            });
        }
    } catch (...) {
        running_.store(false, std::memory_order_release);
        throw;
    }
}

void EventLoopManager::stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        return; // 已经停止或从未启动
    }
    
    // 释放所有work_guard，让io_context.run()能够退出
    for (auto& context_ptr : thread_contexts_) {
        if (context_ptr && context_ptr->work_guard) {
            context_ptr->work_guard.reset();
        }
    }
    
    // 等待所有线程退出
    for (auto& context_ptr : thread_contexts_) {
        if (context_ptr && context_ptr->thread && context_ptr->thread->joinable()) {
            context_ptr->thread->join();
        }
    }
}

asio::io_context& EventLoopManager::get_io_context() {
    ensure_running();
    
    // 轮询分配
    size_t index = next_context_index_.fetch_add(1, std::memory_order_relaxed) 
                   % thread_contexts_.size();
    return *thread_contexts_[index]->io_context;
}

asio::io_context& EventLoopManager::get_least_loaded_context() {
    ensure_running();
    
    size_t index = select_least_loaded_context();
    return *thread_contexts_[index]->io_context;
}

EventLoopManager::Statistics EventLoopManager::get_statistics() const {
    Statistics stats;
    stats.thread_count = thread_contexts_.size();
    stats.tasks_per_thread.reserve(thread_contexts_.size());
    
    for (const auto& context_ptr : thread_contexts_) {
        if (context_ptr) {
            size_t current_tasks = context_ptr->task_count.load(std::memory_order_relaxed);
            size_t total_handled = context_ptr->total_handled.load(std::memory_order_relaxed);
            
            stats.tasks_per_thread.push_back(current_tasks);
            stats.total_tasks_handled += total_handled;
        }
    }
    
    return stats;
}

void EventLoopManager::worker_thread_func(size_t thread_index) {
    auto& context_ptr = thread_contexts_[thread_index];
    
    try {
        // 运行io_context，直到work_guard被释放
        context_ptr->io_context->run();
    } catch (const std::exception& e) {
        // 记录异常，但不让单个线程的异常影响整个系统
        // 在实际项目中，这里应该使用Logger
        // LOG_ERROR("EventLoop线程异常: " + std::string(e.what()));
    } catch (...) {
        // 处理未知异常
        // LOG_ERROR("EventLoop线程发生未知异常");
    }
}

size_t EventLoopManager::select_least_loaded_context() const {
    size_t best_index = 0;
    size_t min_tasks = SIZE_MAX;
    
    for (size_t i = 0; i < thread_contexts_.size(); ++i) {
        if (thread_contexts_[i]) {
            size_t task_count = thread_contexts_[i]->task_count.load(std::memory_order_relaxed);
            if (task_count < min_tasks) {
                min_tasks = task_count;
                best_index = i;
            }
        }
    }
    
    return best_index;
}

void EventLoopManager::ensure_running() const {
    if (!running_.load(std::memory_order_acquire)) {
        throw std::runtime_error("EventLoopManager未启动");
    }
}

} // namespace magnet::async

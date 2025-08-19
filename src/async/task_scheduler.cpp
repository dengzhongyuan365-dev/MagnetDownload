// MagnetDownload - Task Scheduler Implementation
// 优先级任务调度器的具体实现

#include <magnet/async/task_scheduler.h>
#include <asio/steady_timer.hpp>
#include <memory>

namespace magnet::async {

// Task实现

TaskId Task::generate_id() {
    static std::atomic<TaskId> next_id{1};
    return next_id.fetch_add(1, std::memory_order_relaxed);
}

Task::Task(TaskFunction func, TaskPriority priority)
    : function_(std::move(func))
    , priority_(priority)
    , id_(generate_id()) {
}

void Task::execute() const {
    if (function_) {
        function_();
    }
}

// TaskScheduler实现

TaskScheduler::TaskScheduler(EventLoopManager& loop_manager)
    : loop_manager_(loop_manager) {
    
    // 启动调度器线程
    scheduler_thread_ = std::thread([this]() {
        scheduler_thread_func();
    });
}

TaskScheduler::~TaskScheduler() {
    // 设置停止标志
    running_.store(false, std::memory_order_release);
    
    // 唤醒调度器线程
    queue_cv_.notify_all();
    
    // 等待调度器线程退出
    if (scheduler_thread_.joinable()) {
        scheduler_thread_.join();
    }
}

TaskId TaskScheduler::post_task(TaskPriority priority, TaskFunction func) {
    auto task = std::make_shared<Task>(std::move(func), priority);
    TaskId task_id = task->id();
    
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        task_queue_.push(task);
        update_statistics(priority, false);
    }
    
    queue_cv_.notify_one();
    return task_id;
}

TaskId TaskScheduler::post_delayed_task(
    std::chrono::milliseconds delay,
    TaskPriority priority,
    TaskFunction func) {
    
    auto task = std::make_shared<Task>(std::move(func), priority);
    TaskId task_id = task->id();
    
    // 使用asio定时器实现延迟
    auto timer = std::make_shared<asio::steady_timer>(
        loop_manager_.get_io_context(), delay);
    
    timer->async_wait([this, task, timer](const asio::error_code& ec) {
        if (!ec && !is_task_cancelled(task->id())) {
            // 定时器到期，将任务加入队列
            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                task_queue_.push(task);
                update_statistics(task->priority(), false);
            }
            queue_cv_.notify_one();
        }
    });
    
    return task_id;
}

TaskId TaskScheduler::post_periodic_task(
    std::chrono::milliseconds interval,
    TaskPriority priority,
    TaskFunction func) {
    
    TaskId task_id = Task::generate_id();
    
    // 启动周期性调度
    schedule_periodic_task(interval, priority, std::move(func), task_id);
    
    return task_id;
}

bool TaskScheduler::cancel_task(TaskId task_id) {
    std::lock_guard<std::mutex> lock(cancelled_mutex_);
    return cancelled_tasks_.insert(task_id).second;
}

TaskScheduler::Statistics TaskScheduler::get_statistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    Statistics stats = statistics_;
    
    // 更新待执行任务数
    {
        std::lock_guard<std::mutex> queue_lock(queue_mutex_);
        stats.pending_tasks = task_queue_.size();
    }
    
    return stats;
}

void TaskScheduler::scheduler_thread_func() {
    while (running_.load(std::memory_order_acquire)) {
        std::shared_ptr<Task> task;
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            
            // 等待任务或停止信号
            queue_cv_.wait(lock, [this]() {
                return !task_queue_.empty() || !running_.load(std::memory_order_acquire);
            });
            
            // 检查是否需要退出
            if (!running_.load(std::memory_order_acquire)) {
                break;
            }
            
            // 获取最高优先级的任务
            if (!task_queue_.empty()) {
                task = task_queue_.top();
                task_queue_.pop();
            }
        }
        
        // 执行任务
        if (task && !is_task_cancelled(task->id())) {
            execute_task(task);
        }
    }
}

bool TaskScheduler::is_task_cancelled(TaskId task_id) const {
    std::lock_guard<std::mutex> lock(cancelled_mutex_);
    return cancelled_tasks_.find(task_id) != cancelled_tasks_.end();
}

void TaskScheduler::execute_task(std::shared_ptr<Task> task) {
    try {
        // 将任务投递到事件循环执行
        loop_manager_.post_to_least_loaded([this, task]() {
            try {
                task->execute();
            } catch (...) {
                // 任务执行异常不应该影响调度器
                // 在实际项目中，这里应该记录日志
            }
            
            // 更新完成统计
            update_statistics(task->priority(), true);
        });
    } catch (...) {
        // 任务投递失败，也算完成（失败的完成）
        update_statistics(task->priority(), true);
    }
}

void TaskScheduler::update_statistics(TaskPriority priority, bool completed) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    if (completed) {
        statistics_.completed_tasks++;
    }
    
    size_t priority_index = static_cast<size_t>(priority);
    if (priority_index < statistics_.tasks_by_priority.size()) {
        if (completed) {
            // 这里可以统计每个优先级的完成数量
            // 当前简化实现，只统计总的完成数
        } else {
            statistics_.tasks_by_priority[priority_index]++;
        }
    }
}

void TaskScheduler::schedule_periodic_task(
    std::chrono::milliseconds interval,
    TaskPriority priority,
    TaskFunction func,
    TaskId task_id) {
    
    // 如果任务已被取消，不再调度
    if (is_task_cancelled(task_id)) {
        return;
    }
    
    // 使用asio定时器实现周期性调度
    auto timer = std::make_shared<asio::steady_timer>(
        loop_manager_.get_io_context(), interval);
    
    timer->async_wait([this, interval, priority, func, task_id, timer](const asio::error_code& ec) {
        if (!ec && !is_task_cancelled(task_id)) {
            // 执行任务
            auto task = std::make_shared<Task>(func, priority);
            execute_task(task);
            
            // 调度下一次执行
            schedule_periodic_task(interval, priority, func, task_id);
        }
    });
}

} // namespace magnet::async

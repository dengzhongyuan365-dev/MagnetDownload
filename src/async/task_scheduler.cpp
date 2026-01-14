// MagnetDownload - Enhanced Task Scheduler Implementation
// 增强型优先级任务调度器的具体实现，支持多任务场景

#include <magnet/async/task_scheduler.h>
#include <asio/steady_timer.hpp>
#include <memory>
#include <algorithm>
#include <thread>

namespace magnet::async {

// ============================================================================
// Task 实现
// ============================================================================

TaskId Task::generate_id() {
    static std::atomic<TaskId> next_id{1};
    return next_id.fetch_add(1, std::memory_order_relaxed);
}

Task::Task(TaskFunction func, TaskPriority priority, TaskGroupId group_id)
    : function_(std::move(func))
    , base_priority_(priority)
    , id_(generate_id())
    , group_id_(group_id)
    , created_time_(std::chrono::steady_clock::now()) {
}

void Task::execute() const {
    if (function_) {
        function_();
    }
}

TaskPriority Task::effective_priority() const {
    auto now = std::chrono::steady_clock::now();
    auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - created_time_);
    
    // 老化机制：每5秒提升一个优先级等级，最多提升2级
    int aging_boost = std::min(MAX_AGING_BOOST, 
                              static_cast<int>(age / AGING_INTERVAL));
    
    int effective_priority = static_cast<int>(base_priority_) - aging_boost;
    effective_priority = std::max(0, effective_priority);
    
    return static_cast<TaskPriority>(effective_priority);
}

// ============================================================================
// TaskComparator 实现
// ============================================================================

bool TaskScheduler::TaskComparator::operator()(
    const std::shared_ptr<Task>& a, 
    const std::shared_ptr<Task>& b) const {
    
    // 获取有效优先级（考虑老化）
    TaskPriority priority_a = a->effective_priority();
    TaskPriority priority_b = b->effective_priority();
    
    // 优先级不同时，高优先级排在前面
    if (priority_a != priority_b) {
        return priority_a > priority_b;  // priority_queue是最大堆，所以用>
    }
    
    // 优先级相同时，创建时间早的排在前面（FIFO）
    return a->created_time() > b->created_time();
}

// ============================================================================
// TaskScheduler 实现
// ============================================================================

TaskScheduler::TaskScheduler(EventLoopManager& loop_manager, size_t worker_threads)
    : loop_manager_(loop_manager)
    , lastStatsUpdate_(std::chrono::steady_clock::now()) {
    
    // 确定工作线程数量
    if (worker_threads == 0) {
        worker_threads = std::max(1u, std::thread::hardware_concurrency());
    }
    
    statistics_.worker_threads = worker_threads;
    
    // 启动工作线程
    workerThreads_.reserve(worker_threads);
    for (size_t i = 0; i < worker_threads; ++i) {
        workerThreads_.emplace_back([this, i]() {
            workerThreadFunc(i);
        });
    }
}

TaskScheduler::~TaskScheduler() {
    // 设置停止标志
    running_.store(false, std::memory_order_release);
    
    // 唤醒所有工作线程
    queueCv_.notify_all();
    
    // 等待所有工作线程退出
    for (auto& thread : workerThreads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

TaskGroupId TaskScheduler::createTaskGroup(const std::string& name, TaskPriority priority) {
    std::lock_guard<std::mutex> lock(groupsMutex_);
    
    TaskGroupId groupId = nextGroupId_.fetch_add(1, std::memory_order_relaxed);
    auto group = std::make_shared<TaskGroup>(groupId, name, priority);
    
    taskGroups_[groupId] = group;
    
    // 更新统计信息
    {
        std::lock_guard<std::mutex> statsLock(statsMutex_);
        statistics_.active_groups++;
    }
    
    return groupId;
}

bool TaskScheduler::removeTaskGroup(TaskGroupId groupId) {
    if (groupId == 0) return false;  // 默认组不能删除
    
    // 取消组内所有任务
    size_t cancelledCount = cancelGroupTasks(groupId);
    
    std::lock_guard<std::mutex> lock(groupsMutex_);
    
    auto it = taskGroups_.find(groupId);
    if (it == taskGroups_.end()) {
        return false;
    }
    
    taskGroups_.erase(it);
    pausedGroups_.erase(groupId);
    
    // 更新统计信息
    {
        std::lock_guard<std::mutex> statsLock(statsMutex_);
        if (statistics_.active_groups > 0) {
            statistics_.active_groups--;
        }
        statistics_.cancelled_tasks += cancelledCount;
    }
    
    return true;
}

bool TaskScheduler::pauseTaskGroup(TaskGroupId groupId) {
    std::lock_guard<std::mutex> lock(groupsMutex_);
    
    auto it = taskGroups_.find(groupId);
    if (it == taskGroups_.end()) {
        return false;
    }
    
    pausedGroups_.insert(groupId);
    return true;
}

bool TaskScheduler::resumeTaskGroup(TaskGroupId groupId) {
    std::lock_guard<std::mutex> lock(groupsMutex_);
    
    auto it = taskGroups_.find(groupId);
    if (it == taskGroups_.end()) {
        return false;
    }
    
    pausedGroups_.erase(groupId);
    
    // 唤醒工作线程处理恢复的任务
    queueCv_.notify_all();
    
    return true;
}

TaskId TaskScheduler::postTask(TaskPriority priority, TaskFunction func, TaskGroupId groupId) {
    auto task = std::make_shared<Task>(std::move(func), priority, groupId);
    TaskId taskId = task->id();
    
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        task_queue_.push(task);
    }
    
    // 更新任务组统计
    if (groupId != 0) {
        std::lock_guard<std::mutex> groupsLock(groupsMutex_);
        auto it = taskGroups_.find(groupId);
        if (it != taskGroups_.end()) {
            it->second->active_tasks.fetch_add(1, std::memory_order_relaxed);
        }
    }
    
    // 更新统计信息
    updateStatistics(task, false);
    
    // 唤醒工作线程
    queueCv_.notify_one();
    
    return taskId;
}

TaskId TaskScheduler::postDelayedTask(
    std::chrono::milliseconds delay,
    TaskPriority priority,
    TaskFunction func,
    TaskGroupId groupId) {
    
    auto task = std::make_shared<Task>(std::move(func), priority, groupId);
    TaskId taskId = task->id();
    
    // 使用asio定时器实现延迟
    auto timer = std::make_shared<asio::steady_timer>(
        loop_manager_.get_io_context(), delay);
    
    timer->async_wait([this, task, timer](const asio::error_code& ec) {
        if (!ec && !isTaskCancelled(task->id()) && !isGroupPaused(task->group_id())) {
            // 定时器到期，将任务加入队列
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                task_queue_.push(task);
            }
            updateStatistics(task, false);
            queueCv_.notify_one();
        }
    });
    
    return taskId;
}

TaskId TaskScheduler::postPeriodicTask(
    std::chrono::milliseconds interval,
    TaskPriority priority,
    TaskFunction func,
    TaskGroupId groupId) {
    
    TaskId taskId = Task::generate_id();
    
    // 启动周期性调度
    schedulePeriodicTask(interval, priority, std::move(func), taskId, groupId);
    
    return taskId;
}

bool TaskScheduler::cancelTask(TaskId taskId) {
    std::lock_guard<std::mutex> lock(cancelledMutex_);
    return cancelledTasks_.insert(taskId).second;
}

size_t TaskScheduler::cancelGroupTasks(TaskGroupId groupId) {
    if (groupId == 0) return 0;  // 不能取消默认组的所有任务
    
    std::lock_guard<std::mutex> lock(cancelledMutex_);
    
    // 这里简化实现，实际应该维护任务到组的映射
    // 为了演示，我们返回一个估计值
    size_t cancelledCount = 0;
    
    // 在实际实现中，应该维护 groupId -> taskIds 的映射
    // 然后遍历该组的所有任务ID并取消它们
    
    return cancelledCount;
}

TaskScheduler::Statistics TaskScheduler::getStatistics() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    Statistics stats = statistics_;
    
    // 更新待执行任务数
    {
        std::lock_guard<std::mutex> queueLock(queueMutex_);
        stats.pending_tasks = task_queue_.size();
    }
    
    // 更新活跃任务组数
    {
        std::lock_guard<std::mutex> groupsLock(groupsMutex_);
        stats.active_groups = taskGroups_.size();
    }
    
    return stats;
}

std::vector<TaskScheduler::GroupStatistics> TaskScheduler::getGroupStatistics(TaskGroupId groupId) const {
    std::lock_guard<std::mutex> lock(groupsMutex_);
    
    std::vector<GroupStatistics> result;
    
    if (groupId == 0) {
        // 返回所有组的统计信息
        for (const auto& [id, group] : taskGroups_) {
            GroupStatistics stats;
            stats.groupId = id;
            stats.name = group->name;
            stats.basePriority = group->base_priority;
            stats.activeTasks = group->active_tasks.load();
            stats.completedTasks = group->completed_tasks.load();
            stats.createdTime = group->created_time;
            stats.isPaused = pausedGroups_.find(id) != pausedGroups_.end();
            
            result.push_back(stats);
        }
    } else {
        // 返回指定组的统计信息
        auto it = taskGroups_.find(groupId);
        if (it != taskGroups_.end()) {
            GroupStatistics stats;
            stats.groupId = groupId;
            stats.name = it->second->name;
            stats.basePriority = it->second->base_priority;
            stats.activeTasks = it->second->active_tasks.load();
            stats.completedTasks = it->second->completed_tasks.load();
            stats.createdTime = it->second->created_time;
            stats.isPaused = pausedGroups_.find(groupId) != pausedGroups_.end();
            
            result.push_back(stats);
        }
    }
    
    return result;
}

void TaskScheduler::workerThreadFunc(size_t threadId) {
    while (running_.load(std::memory_order_acquire)) {
        std::shared_ptr<Task> task;
        
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            
            // 等待任务或停止信号
            queueCv_.wait(lock, [this]() {
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
        if (task && !isTaskCancelled(task->id()) && !isGroupPaused(task->group_id())) {
            executeTask(task);
        }
    }
}

bool TaskScheduler::isTaskCancelled(TaskId taskId) const {
    std::lock_guard<std::mutex> lock(cancelledMutex_);
    return cancelledTasks_.find(taskId) != cancelledTasks_.end();
}

bool TaskScheduler::isGroupPaused(TaskGroupId groupId) const {
    if (groupId == 0) return false;  // 默认组不会被暂停
    
    std::lock_guard<std::mutex> lock(groupsMutex_);
    return pausedGroups_.find(groupId) != pausedGroups_.end();
}

void TaskScheduler::executeTask(std::shared_ptr<Task> task) {
    auto startTime = std::chrono::steady_clock::now();
    
    try {
        // 将任务投递到事件循环执行
        loop_manager_.post_to_least_loaded([this, task, startTime]() {
            try {
                task->execute();
            } catch (...) {
                // 任务执行异常不应该影响调度器
                // 在实际项目中，这里应该记录日志
            }
            
            auto endTime = std::chrono::steady_clock::now();
            auto executionTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                endTime - startTime);
            
            // 更新任务组统计
            TaskGroupId groupId = task->group_id();
            if (groupId != 0) {
                std::lock_guard<std::mutex> groupsLock(groupsMutex_);
                auto it = taskGroups_.find(groupId);
                if (it != taskGroups_.end()) {
                    it->second->active_tasks.fetch_sub(1, std::memory_order_relaxed);
                    it->second->completed_tasks.fetch_add(1, std::memory_order_relaxed);
                }
            }
            
            // 更新完成统计
            updateStatistics(task, true);
        });
    } catch (...) {
        // 任务投递失败，也算完成（失败的完成）
        updateStatistics(task, true);
    }
}

void TaskScheduler::updateStatistics(const std::shared_ptr<Task>& task, bool completed, bool cancelled) {
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    if (completed) {
        statistics_.completed_tasks++;
    } else if (cancelled) {
        statistics_.cancelled_tasks++;
    }
    
    // 更新按优先级统计
    size_t priorityIndex = static_cast<size_t>(task->base_priority());
    if (priorityIndex < statistics_.tasks_by_priority.size()) {
        if (completed || cancelled) {
            // 任务完成或取消时，从对应优先级计数中减去
            if (statistics_.tasks_by_priority[priorityIndex] > 0) {
                statistics_.tasks_by_priority[priorityIndex]--;
            }
        } else {
            // 新任务添加时，增加对应优先级计数
            statistics_.tasks_by_priority[priorityIndex]++;
        }
    }
    
    // 更新平均等待时间
    if (completed) {
        auto now = std::chrono::steady_clock::now();
        auto waitTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - task->created_time());
        
        // 简单的移动平均
        statistics_.avg_task_wait_time_ms = 
            (statistics_.avg_task_wait_time_ms * 0.9) + (waitTime.count() * 0.1);
    }
}

void TaskScheduler::schedulePeriodicTask(
    std::chrono::milliseconds interval,
    TaskPriority priority,
    TaskFunction func,
    TaskId taskId,
    TaskGroupId groupId) {
    
    // 如果任务已被取消或组被暂停，不再调度
    if (isTaskCancelled(taskId) || isGroupPaused(groupId)) {
        return;
    }
    
    // 使用asio定时器实现周期性调度
    auto timer = std::make_shared<asio::steady_timer>(
        loop_manager_.get_io_context(), interval);
    
    timer->async_wait([this, interval, priority, func, taskId, groupId, timer](const asio::error_code& ec) {
        if (!ec && !isTaskCancelled(taskId) && !isGroupPaused(groupId)) {
            // 执行任务
            auto task = std::make_shared<Task>(func, priority, groupId);
            executeTask(task);
            
            // 调度下一次执行
            schedulePeriodicTask(interval, priority, func, taskId, groupId);
        }
    });
}

} // namespace magnet::async

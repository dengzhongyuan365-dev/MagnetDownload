#pragma once
// MagnetDownload - Task Scheduler
// 优先级任务调度器：支持立即任务、延迟任务和周期性任务

#include "types.h"
#include "event_loop_manager.h"
#include <functional>
#include <chrono>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <unordered_set>
#include <array>
#include <thread>

namespace magnet::async {

/**
 * @brief 任务对象
 * 
 * 封装单个可执行任务，包含优先级和唯一ID
 */
class Task {
public:
    /**
     * @brief 构造函数
     * @param func 任务函数
     * @param priority 任务优先级
     */
    Task(TaskFunction func, TaskPriority priority = TaskPriority::NORMAL);
    
    /**
     * @brief 执行任务
     */
    void execute() const;
    
    /**
     * @brief 获取任务优先级
     */
    TaskPriority priority() const { return priority_; }
    
    /**
     * @brief 获取任务ID
     */
    TaskId id() const { return id_; }
    
    /**
     * @brief 生成唯一的任务ID
     */
    static TaskId generate_id();

private:
    TaskFunction function_;     // 任务函数
    TaskPriority priority_;     // 任务优先级
    TaskId id_;                 // 唯一ID
};

/**
 * @brief 优先级任务调度器
 * 
 * 提供任务优先级调度、延迟执行和周期性执行功能
 */
class TaskScheduler {
public:
    /**
     * @brief 构造函数
     * @param loop_manager 事件循环管理器引用
     */
    explicit TaskScheduler(EventLoopManager& loop_manager);
    
    /**
     * @brief 析构函数，确保调度器正确停止
     */
    ~TaskScheduler();
    
    // 禁用拷贝和移动
    TaskScheduler(const TaskScheduler&) = delete;
    TaskScheduler& operator=(const TaskScheduler&) = delete;
    TaskScheduler(TaskScheduler&&) = delete;
    TaskScheduler& operator=(TaskScheduler&&) = delete;
    
    /**
     * @brief 投递立即执行的任务
     * @param priority 任务优先级
     * @param func 任务函数
     * @return 任务ID，可用于取消任务
     */
    TaskId post_task(TaskPriority priority, TaskFunction func);
    
    /**
     * @brief 投递延迟执行的任务
     * @param delay 延迟时间
     * @param priority 任务优先级
     * @param func 任务函数
     * @return 任务ID，可用于取消任务
     */
    TaskId post_delayed_task(
        std::chrono::milliseconds delay,
        TaskPriority priority,
        TaskFunction func
    );
    
    /**
     * @brief 投递周期性执行的任务
     * @param interval 执行间隔
     * @param priority 任务优先级
     * @param func 任务函数
     * @return 任务ID，可用于取消任务
     * @note 周期性任务会一直执行直到被取消
     */
    TaskId post_periodic_task(
        std::chrono::milliseconds interval,
        TaskPriority priority,
        TaskFunction func
    );
    
    /**
     * @brief 取消指定的任务
     * @param task_id 要取消的任务ID
     * @return true 如果成功取消，false 如果任务不存在或已执行
     */
    bool cancel_task(TaskId task_id);
    
    /**
     * @brief 统计信息结构
     */
    struct Statistics {
        size_t pending_tasks;                      // 待执行任务数
        size_t completed_tasks;                    // 已完成任务数
        std::array<size_t, 4> tasks_by_priority;   // 按优先级统计
        
        Statistics() : pending_tasks(0), completed_tasks(0) {
            tasks_by_priority.fill(0);
        }
    };
    
    /**
     * @brief 获取统计信息
     * @return 当前统计信息
     */
    Statistics get_statistics() const;

private:
    EventLoopManager& loop_manager_;    // 事件循环管理器引用
    
    /**
     * @brief 优先级队列比较器
     * 高优先级任务排在前面
     */
    struct TaskComparator {
        bool operator()(const std::shared_ptr<Task>& a, const std::shared_ptr<Task>& b) const {
            // priority_queue是最大堆，我们要高优先级在前，所以比较时要反过来
            return a->priority() < b->priority();
        }
    };
    
    // 优先级任务队列
    std::priority_queue<
        std::shared_ptr<Task>,
        std::vector<std::shared_ptr<Task>>,
        TaskComparator
    > task_queue_;
    
    // 队列同步
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    
    // 调度器状态
    std::atomic<bool> running_{true};
    std::thread scheduler_thread_;
    
    // 任务取消机制
    std::unordered_set<TaskId> cancelled_tasks_;
    mutable std::mutex cancelled_mutex_;
    
    // 统计信息
    mutable std::mutex stats_mutex_;
    Statistics statistics_;
    
    /**
     * @brief 调度器主线程函数
     */
    void scheduler_thread_func();
    
    /**
     * @brief 检查任务是否已被取消
     * @param task_id 任务ID
     * @return true 如果任务已被取消
     */
    bool is_task_cancelled(TaskId task_id) const;
    
    /**
     * @brief 执行单个任务
     * @param task 要执行的任务
     */
    void execute_task(std::shared_ptr<Task> task);
    
    /**
     * @brief 更新统计信息
     * @param priority 任务优先级
     * @param completed 是否是完成的任务
     */
    void update_statistics(TaskPriority priority, bool completed);
    
    /**
     * @brief 创建周期性任务的递归调度
     * @param interval 执行间隔
     * @param priority 任务优先级
     * @param func 任务函数
     * @param task_id 任务ID
     */
    void schedule_periodic_task(
        std::chrono::milliseconds interval,
        TaskPriority priority,
        TaskFunction func,
        TaskId task_id
    );
};

} // namespace magnet::async

#pragma once
// MagnetDownload - Enhanced Task Scheduler
// 增强型优先级任务调度器：支持多任务场景、任务分组、优先级继承和老化机制

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
#include <unordered_map>
#include <array>
#include <thread>
#include <string>

namespace magnet::async {

/**
 * @brief 任务组ID类型
 */
using TaskGroupId = uint32_t;

/**
 * @brief 任务组信息
 */
struct TaskGroup {
    TaskGroupId id;
    std::string name;
    TaskPriority base_priority;
    std::chrono::steady_clock::time_point created_time;
    std::atomic<size_t> active_tasks{0};
    std::atomic<size_t> completed_tasks{0};
    
    TaskGroup(TaskGroupId id, std::string name, TaskPriority priority)
        : id(id), name(std::move(name)), base_priority(priority)
        , created_time(std::chrono::steady_clock::now()) {}
};

/**
 * @brief 增强型任务对象
 * 
 * 封装单个可执行任务，包含优先级、任务组和老化机制
 */
class Task {
public:
    /**
     * @brief 构造函数
     * @param func 任务函数
     * @param priority 任务优先级
     * @param group_id 任务组ID（可选）
     */
    Task(TaskFunction func, TaskPriority priority = TaskPriority::NORMAL, TaskGroupId group_id = 0);
    
    /**
     * @brief 执行任务
     */
    void execute() const;
    
    /**
     * @brief 获取任务优先级（考虑老化）
     */
    TaskPriority effective_priority() const;
    
    /**
     * @brief 获取基础优先级
     */
    TaskPriority base_priority() const { return base_priority_; }
    
    /**
     * @brief 获取任务ID
     */
    TaskId id() const { return id_; }
    
    /**
     * @brief 获取任务组ID
     */
    TaskGroupId group_id() const { return group_id_; }
    
    /**
     * @brief 获取创建时间
     */
    std::chrono::steady_clock::time_point created_time() const { return created_time_; }
    
    /**
     * @brief 生成唯一的任务ID
     */
    static TaskId generate_id();

private:
    TaskFunction function_;                                    // 任务函数
    TaskPriority base_priority_;                              // 基础优先级
    TaskId id_;                                               // 唯一ID
    TaskGroupId group_id_;                                    // 任务组ID
    std::chrono::steady_clock::time_point created_time_;      // 创建时间
    
    // 老化机制配置
    static constexpr std::chrono::milliseconds AGING_INTERVAL{5000};  // 5秒老化一次
    static constexpr int MAX_AGING_BOOST = 2;                         // 最多提升2个优先级等级
};

/**
 * @brief 增强型优先级任务调度器
 * 
 * 新增功能：
 * - 任务分组管理
 * - 优先级继承（高优先级组的任务可以提升低优先级任务）
 * - 任务老化机制（长时间等待的任务优先级会提升）
 * - 更精细的统计信息
 * - 任务组级别的控制（暂停/恢复整个组）
 */
class TaskScheduler {
public:
    /**
     * @brief 构造函数
     * @param loop_manager 事件循环管理器引用
     * @param worker_threads 工作线程数量（默认为CPU核心数）
     */
    explicit TaskScheduler(EventLoopManager& loop_manager, size_t worker_threads = 0);
    
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
     * @brief 创建任务组
     * @param name 任务组名称
     * @param priority 任务组基础优先级
     * @return 任务组ID
     */
    TaskGroupId createTaskGroup(const std::string& name, TaskPriority priority);
    
    /**
     * @brief 删除任务组（会取消组内所有任务）
     * @param groupId 任务组ID
     * @return 是否成功删除
     */
    bool removeTaskGroup(TaskGroupId groupId);
    
    /**
     * @brief 暂停任务组
     * @param groupId 任务组ID
     * @return 是否成功暂停
     */
    bool pauseTaskGroup(TaskGroupId groupId);
    
    /**
     * @brief 恢复任务组
     * @param groupId 任务组ID
     * @return 是否成功恢复
     */
    bool resumeTaskGroup(TaskGroupId groupId);
    
    /**
     * @brief 投递立即执行的任务
     * @param priority 任务优先级
     * @param func 任务函数
     * @param groupId 任务组ID（可选）
     * @return 任务ID，可用于取消任务
     */
    TaskId postTask(TaskPriority priority, TaskFunction func, TaskGroupId groupId = 0);
    
    /**
     * @brief 投递延迟执行的任务
     * @param delay 延迟时间
     * @param priority 任务优先级
     * @param func 任务函数
     * @param groupId 任务组ID（可选）
     * @return 任务ID，可用于取消任务
     */
    TaskId postDelayedTask(
        std::chrono::milliseconds delay,
        TaskPriority priority,
        TaskFunction func,
        TaskGroupId groupId = 0
    );
    
    /**
     * @brief 投递周期性执行的任务
     * @param interval 执行间隔
     * @param priority 任务优先级
     * @param func 任务函数
     * @param groupId 任务组ID（可选）
     * @return 任务ID，可用于取消任务
     * @note 周期性任务会一直执行直到被取消
     */
    TaskId postPeriodicTask(
        std::chrono::milliseconds interval,
        TaskPriority priority,
        TaskFunction func,
        TaskGroupId groupId = 0
    );
    
    /**
     * @brief 取消指定的任务
     * @param taskId 要取消的任务ID
     * @return true 如果成功取消，false 如果任务不存在或已执行
     */
    bool cancelTask(TaskId taskId);
    
    /**
     * @brief 取消任务组内的所有任务
     * @param groupId 任务组ID
     * @return 取消的任务数量
     */
    size_t cancelGroupTasks(TaskGroupId groupId);
    
    /**
     * @brief 增强型统计信息
     */
    struct Statistics {
        size_t pending_tasks;                                  // 待执行任务数
        size_t completed_tasks;                                // 已完成任务数
        size_t cancelled_tasks;                                // 已取消任务数
        std::array<size_t, 4> tasks_by_priority;               // 按优先级统计
        size_t active_groups;                                  // 活跃任务组数
        size_t worker_threads;                                 // 工作线程数
        double avg_task_wait_time_ms;                          // 平均任务等待时间
        
        Statistics() : pending_tasks(0), completed_tasks(0), cancelled_tasks(0)
                     , active_groups(0), worker_threads(0), avg_task_wait_time_ms(0.0) {
            tasks_by_priority.fill(0);
        }
    };
    
    /**
     * @brief 任务组统计信息
     */
    struct GroupStatistics {
        TaskGroupId groupId;
        std::string name;
        TaskPriority basePriority;
        size_t activeTasks;
        size_t completedTasks;
        std::chrono::steady_clock::time_point createdTime;
        bool isPaused;
    };
    
    /**
     * @brief 获取总体统计信息
     * @return 当前统计信息
     */
    Statistics getStatistics() const;
    
    /**
     * @brief 获取任务组统计信息
     * @param groupId 任务组ID，0表示获取所有组
     * @return 任务组统计信息列表
     */
    std::vector<GroupStatistics> getGroupStatistics(TaskGroupId groupId = 0) const;

private:
    EventLoopManager& loop_manager_;    // 事件循环管理器引用
    
    /**
     * @brief 任务比较器（支持老化机制）
     */
    struct TaskComparator {
        bool operator()(const std::shared_ptr<Task>& a, const std::shared_ptr<Task>& b) const;
    };
    
    // 任务队列
    std::priority_queue<
        std::shared_ptr<Task>,
        std::vector<std::shared_ptr<Task>>,
        TaskComparator
    > task_queue_;
    
    // 任务组管理
    std::unordered_map<TaskGroupId, std::shared_ptr<TaskGroup>> taskGroups_;
    std::unordered_set<TaskGroupId> pausedGroups_;
    std::atomic<TaskGroupId> nextGroupId_{1};
    
    // 同步原语
    mutable std::mutex queueMutex_;
    mutable std::mutex groupsMutex_;
    std::condition_variable queueCv_;
    
    // 调度器状态
    std::atomic<bool> running_{true};
    std::vector<std::thread> workerThreads_;
    
    // 任务取消机制
    std::unordered_set<TaskId> cancelledTasks_;
    mutable std::mutex cancelledMutex_;
    
    // 统计信息
    mutable std::mutex statsMutex_;
    Statistics statistics_;
    std::chrono::steady_clock::time_point lastStatsUpdate_;
    
    /**
     * @brief 工作线程函数
     * @param threadId 线程ID
     */
    void workerThreadFunc(size_t threadId);
    
    /**
     * @brief 检查任务是否已被取消
     */
    bool isTaskCancelled(TaskId taskId) const;
    
    /**
     * @brief 检查任务组是否被暂停
     */
    bool isGroupPaused(TaskGroupId groupId) const;
    
    /**
     * @brief 执行单个任务
     */
    void executeTask(std::shared_ptr<Task> task);
    
    /**
     * @brief 更新统计信息
     */
    void updateStatistics(const std::shared_ptr<Task>& task, bool completed, bool cancelled = false);
    
    /**
     * @brief 创建周期性任务的递归调度
     */
    void schedulePeriodicTask(
        std::chrono::milliseconds interval,
        TaskPriority priority,
        TaskFunction func,
        TaskId taskId,
        TaskGroupId groupId
    );
};

} // namespace magnet::async

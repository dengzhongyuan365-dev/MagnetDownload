#pragma once
// MagnetDownload - Event Loop Manager
// 多线程事件循环管理器：基于你的Asio实验6的生产级实现

#include <asio.hpp>
#include <vector>
#include <thread>
#include <atomic>
#include <memory>
#include <functional>

namespace magnet::async {

/**
 * @brief 多线程事件循环管理器
 * 
 * 基于Asio io_context实现的高性能事件循环池，提供：
 * - 多线程并发处理
 * - 智能负载均衡
 * - 优雅的生命周期管理
 * - 实时统计信息
 */
class EventLoopManager {
public:
    /**
     * @brief 构造函数
     * @param thread_count 工作线程数量，默认为CPU核心数
     */
    explicit EventLoopManager(size_t thread_count = std::thread::hardware_concurrency());
    
    /**
     * @brief 析构函数，确保所有线程正确退出
     */
    ~EventLoopManager();
    
    // 禁用拷贝和移动
    EventLoopManager(const EventLoopManager&) = delete;
    EventLoopManager& operator=(const EventLoopManager&) = delete;
    EventLoopManager(EventLoopManager&&) = delete;
    EventLoopManager& operator=(EventLoopManager&&) = delete;
    
    /**
     * @brief 启动所有工作线程
     * @throw std::runtime_error 如果已经启动或启动失败
     */
    void start();
    
    /**
     * @brief 停止所有工作线程（优雅停止）
     * 会等待当前正在执行的任务完成
     */
    void stop();
    
    /**
     * @brief 检查是否正在运行
     * @return true 如果事件循环正在运行
     */
    bool is_running() const { return running_.load(std::memory_order_acquire); }
    
    /**
     * @brief 获取一个io_context（轮询方式）
     * @return io_context引用
     * @throw std::runtime_error 如果未启动
     */
    asio::io_context& get_io_context();
    
    /**
     * @brief 获取负载最轻的io_context
     * @return 当前任务数最少的io_context引用
     * @throw std::runtime_error 如果未启动
     */
    asio::io_context& get_least_loaded_context();
    
    /**
     * @brief 投递任务到轮询选择的io_context
     * @param handler 要执行的任务
     */
    template<typename Handler>
    void post(Handler&& handler);
    
    /**
     * @brief 投递任务到负载最轻的io_context
     * @param handler 要执行的任务
     */
    template<typename Handler>
    void post_to_least_loaded(Handler&& handler);
    
    /**
     * @brief 统计信息结构
     */
    struct Statistics {
        size_t thread_count;                    // 线程数量
        std::vector<size_t> tasks_per_thread;   // 每个线程的任务数
        size_t total_tasks_handled;             // 总处理任务数
        
        Statistics() : thread_count(0), total_tasks_handled(0) {}
    };
    
    /**
     * @brief 获取实时统计信息
     * @return 当前的统计信息
     */
    Statistics get_statistics() const;

private:
    /**
     * @brief 单个线程的上下文信息
     */
    struct ThreadContext {
        std::unique_ptr<asio::io_context> io_context;
        std::unique_ptr<asio::executor_work_guard<asio::io_context::executor_type>> work_guard;
        std::unique_ptr<std::thread> thread;
        std::atomic<size_t> task_count{0};          // 当前任务数（用于负载均衡）
        std::atomic<size_t> total_handled{0};       // 总处理任务数（用于统计）
        
        ThreadContext();
        ~ThreadContext();
        
        // 禁用拷贝和移动
        ThreadContext(const ThreadContext&) = delete;
        ThreadContext& operator=(const ThreadContext&) = delete;
        ThreadContext(ThreadContext&&) = delete;
        ThreadContext& operator=(ThreadContext&&) = delete;
    };
    
    std::vector<std::unique_ptr<ThreadContext>> thread_contexts_;    // 线程上下文数组
    std::atomic<bool> running_{false};              // 运行状态
    std::atomic<size_t> next_context_index_{0};     // 轮询索引
    
    /**
     * @brief 工作线程函数
     * @param thread_index 线程索引
     */
    void worker_thread_func(size_t thread_index);
    
    /**
     * @brief 选择负载最轻的线程上下文
     * @return 最轻负载的线程索引
     */
    size_t select_least_loaded_context() const;
    
    /**
     * @brief 验证管理器是否已启动
     * @throw std::runtime_error 如果未启动
     */
    void ensure_running() const;
};

// 模板方法实现

template<typename Handler>
void EventLoopManager::post(Handler&& handler) {
    ensure_running();
    
    auto& context = get_io_context();
    asio::post(context, std::forward<Handler>(handler));
}

template<typename Handler>
void EventLoopManager::post_to_least_loaded(Handler&& handler) {
    ensure_running();
    
    size_t index = select_least_loaded_context();
    auto& context_ptr = thread_contexts_[index];
    
    // 增加任务计数
    context_ptr->task_count.fetch_add(1, std::memory_order_relaxed);
    
    // 获取原始指针用于回调（EventLoopManager生命周期保证安全）
    ThreadContext* context_raw = context_ptr.get();
    
    // 投递任务，执行完成后减少计数
    asio::post(*context_ptr->io_context, [handler = std::forward<Handler>(handler), context_raw]() {
        // 执行实际任务
        try {
            handler();
        } catch (...) {
            // 任务执行异常不应该影响事件循环
        }
        
        // 减少任务计数，增加处理计数
        context_raw->task_count.fetch_sub(1, std::memory_order_relaxed);
        context_raw->total_handled.fetch_add(1, std::memory_order_relaxed);
    });
}

} // namespace magnet::async

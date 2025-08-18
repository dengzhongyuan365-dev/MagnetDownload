#include <iostream>
#include <asio.hpp>
#include <thread>
#include <chrono>
#include <memory>
#include <vector>
#include <atomic>
#include <mutex>

// 实验函数声明
void experiment_01_hello_asio();
void experiment_02_work_guard();
void experiment_03_async_timer();
void experiment_04_object_lifetime();
void experiment_05_udp_basic();
void experiment_06_multithreading();

// 实验1: Hello Asio
void experiment_01_hello_asio() {
    std::cout << "\n=== 实验1: Hello Asio ===\n";
    
    asio::io_context io_context;
    
    std::cout << "调用 io_context.run() 之前\n";
    
    std::size_t handlers_run = io_context.run();
    
    std::cout << "调用 io_context.run() 之后\n";
    std::cout << "执行了 " << handlers_run << " 个处理器\n";
}

// 实验2: Work Guard
void experiment_02_work_guard() {
    std::cout << "\n=== 实验2: Work Guard ===\n";
    
    asio::io_context io_context;
    
    auto work_guard = asio::make_work_guard(io_context);
    
    std::cout << "启动工作线程...\n";
    
    std::thread worker([&io_context]() {
        std::cout << "工作线程: 开始运行 io_context.run()\n";
        std::size_t handlers_run = io_context.run();
        std::cout << "工作线程: io_context.run() 结束，执行了 " 
                  << handlers_run << " 个处理器\n";
    });
    
    std::cout << "主线程: 模拟其他工作...\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    std::cout << "主线程: 投递任务到 io_context\n";
    for (int i = 0; i < 3; ++i) {
        asio::post(io_context, [i]() {
            std::cout << "  任务 " << i << " 执行\n";
        });
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    std::cout << "主线程: 移除 work_guard\n";
    work_guard.reset();
    
    std::cout << "主线程: 等待工作线程完成\n";
    worker.join();
    
    std::cout << "实验结束\n";
}

// 实验3: 异步定时器
void experiment_03_async_timer() {
    std::cout << "\n=== 实验3: 异步定时器 ===\n";
    
    asio::io_context io_context;
    
    asio::steady_timer timer(io_context, std::chrono::seconds(1));
    
    std::cout << "启动异步定时器...\n";
    std::cout << "当前时间: " << std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count() << "ms\n";
    
    timer.async_wait([](const asio::error_code& ec) {
        std::cout << "定时器回调执行！\n";
        std::cout << "当前时间: " << std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count() << "ms\n";
        
        if (ec) {
            std::cout << "定时器错误: " << ec.message() << "\n";
        } else {
            std::cout << "定时器正常到期\n";
        }
    });
    
    std::cout << "async_wait() 调用完成，非阻塞！\n";
    std::cout << "现在运行 io_context.run()...\n";
    
    std::size_t handlers_run = io_context.run();
    
    std::cout << "io_context.run() 完成，执行了 " << handlers_run << " 个处理器\n";
}

// 实验4: 对象生命周期管理
class TimerDemo : public std::enable_shared_from_this<TimerDemo> {
private:
    asio::io_context& io_context_;
    asio::steady_timer timer_;
    int counter_;
    
public:
    TimerDemo(asio::io_context& io_context) 
        : io_context_(io_context)
        , timer_(io_context)
        , counter_(0) {
        std::cout << "TimerDemo 对象创建\n";
    }
    
    ~TimerDemo() {
        std::cout << "TimerDemo 对象销毁\n";
    }
    
    void start() {
        std::cout << "启动定时器循环...\n";
        schedule_next_timer();
    }
    
private:
    void schedule_next_timer() {
        timer_.expires_after(std::chrono::milliseconds(500));
        
        timer_.async_wait([self = shared_from_this()](const asio::error_code& ec) {
            self->handle_timer(ec);
        });
    }
    
    void handle_timer(const asio::error_code& ec) {
        if (ec) {
            std::cout << "定时器错误: " << ec.message() << "\n";
            return;
        }
        
        ++counter_;
        std::cout << "定时器触发 " << counter_ << " 次\n";
        
        if (counter_ < 5) {
            schedule_next_timer();
        } else {
            std::cout << "定时器循环完成\n";
        }
    }
};

void experiment_04_object_lifetime() {
    std::cout << "\n=== 实验4: 对象生命周期管理 ===\n";
    
    asio::io_context io_context;
    
    {
        std::cout << "创建 TimerDemo 对象...\n";
        auto demo = std::make_shared<TimerDemo>(io_context);
        
        demo->start();
        
        std::cout << "demo 即将离开作用域...\n";
    }
    
    std::cout << "运行 io_context...\n";
    io_context.run();
    
    std::cout << "实验完成\n";
}

// 实验6: 多线程事件循环
class MultiThreadDemo {
private:
    asio::io_context& io_context_;
    asio::executor_work_guard<asio::io_context::executor_type> work_guard_;
    std::atomic<int> counter_{0};
    std::atomic<int> completed_tasks_{0};
    mutable std::mutex print_mutex_;
    const int total_tasks_ = 10;
    
public:
    MultiThreadDemo(asio::io_context& io_context)
        : io_context_(io_context)
        , work_guard_(asio::make_work_guard(io_context)) {
    }
    
    void start_work() {
        for (int i = 0; i < total_tasks_; ++i) {
            asio::post(io_context_, [this, i]() {
                this->worker_task(i);
            });
        }
        
        auto timer = std::make_shared<asio::steady_timer>(io_context_, std::chrono::seconds(5));
        timer->async_wait([this, timer](const asio::error_code& ec) {
            if (!ec) {
                safe_print("定时器到期，停止工作...");
                work_guard_.reset();
            }
        });
    }
    
private:
    void worker_task(int task_id) {
        auto thread_id = std::this_thread::get_id();
        
        safe_print("线程 " + std::to_string(std::hash<std::thread::id>{}(thread_id) % 10000) + 
                  " 开始处理任务 " + std::to_string(task_id));
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100 + (task_id * 50)));
        
        int current_value = counter_.fetch_add(1);
        int completed = completed_tasks_.fetch_add(1);
        
        safe_print("线程 " + std::to_string(std::hash<std::thread::id>{}(thread_id) % 10000) + 
                  " 完成任务 " + std::to_string(task_id) + 
                  " (当前值: " + std::to_string(current_value + 1) + ")");
        
        if (completed + 1 >= total_tasks_) {
            safe_print("🎉 所有任务完成！正在停止...");
            asio::post(io_context_, [this]() {
                work_guard_.reset();
            });
        }
    }
    
    void safe_print(const std::string& message) {
        std::lock_guard<std::mutex> lock(print_mutex_);
        auto now = std::chrono::steady_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        std::cout << "[" << timestamp << "ms] " << message << std::endl;
    }
};

void experiment_06_multithreading() {
    std::cout << "\n=== 实验6: 多线程事件循环 ===\n";
    
    asio::io_context io_context;
    MultiThreadDemo demo(io_context);
    
    demo.start_work();
    
    const int thread_count = 4;
    std::vector<std::thread> threads;
    
    std::cout << "启动 " << thread_count << " 个工作线程...\n";
    
    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back([&io_context, i]() {
            std::size_t handlers_run = io_context.run();
            std::cout << "工作线程 " << i << " 退出，处理了 " << handlers_run << " 个处理器" << std::endl;
        });
    }
    
    std::cout << "等待所有线程完成...\n";
    for (auto& thread : threads) {
        thread.join();
    }
    
    std::cout << "实验6完成\n";
}

int main(int argc, char* argv[]) {
    std::cout << "ASIO学习实验程序\n";
    std::cout << "==================\n";
    
    if (argc < 2) {
        std::cout << "用法: " << argv[0] << " <实验号>\n";
        std::cout << "实验列表:\n";
        std::cout << "  1 - Hello Asio\n";
        std::cout << "  2 - Work Guard\n";
        std::cout << "  3 - 异步定时器\n";
        std::cout << "  4 - 对象生命周期\n";
        std::cout << "  5 - UDP网络编程\n";
        std::cout << "  6 - 多线程事件循环\n";
        return 1;
    }
    
    int experiment_num = std::atoi(argv[1]);
    
    try {
        switch (experiment_num) {
            case 1:
                experiment_01_hello_asio();
                break;
            case 2:
                experiment_02_work_guard();
                break;
            case 3:
                experiment_03_async_timer();
                break;
            case 4:
                experiment_04_object_lifetime();
                break;
            case 5:
                experiment_05_udp_basic();
                break;
            case 6:
                experiment_06_multithreading();
                break;
            default:
                std::cout << "无效的实验号: " << experiment_num << "\n";
                return 1;
        }
    } catch (std::exception& e) {
        std::cerr << "实验执行出错: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}

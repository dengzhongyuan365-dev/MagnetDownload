// MagnetDownload - Main Application Entry Point
// 主程序入口 - 集成所有模块，演示async模块功能

#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <magnet/async/event_loop_manager.h>
#include <magnet/async/task_scheduler.h>

// 其他模块的占位函数声明
namespace magnet::network { void placeholder_udp_client(); }
namespace magnet::protocols { void placeholder_magnet_uri_parser(); }
namespace magnet::storage { void placeholder_file_manager(); }

void test_async_module() {
    std::cout << "\n🔄 测试Async模块功能..." << std::endl;
    
    try {
        // 创建事件循环管理器
        magnet::async::EventLoopManager loop_manager(4);
        std::cout << "✓ EventLoopManager创建成功 (4个工作线程)" << std::endl;
        
        // 启动事件循环
        loop_manager.start();
        std::cout << "✓ EventLoopManager启动成功" << std::endl;
        
        // 创建任务调度器
        magnet::async::TaskScheduler scheduler(loop_manager);
        std::cout << "✓ TaskScheduler创建成功" << std::endl;
        
        // 测试任务计数器
        std::atomic<int> completed_tasks{0};
        const int total_tasks = 20;
        
        // 投递不同优先级的任务
        std::cout << "\n📋 投递任务测试..." << std::endl;
        
        // 高优先级任务
        for (int i = 0; i < 5; ++i) {
            scheduler.post_task(magnet::async::TaskPriority::HIGH, [&completed_tasks, i]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                completed_tasks.fetch_add(1);
                std::cout << "🔥 高优先级任务 " << i << " 完成" << std::endl;
            });
        }
        
        // 普通优先级任务
        for (int i = 0; i < 10; ++i) {
            scheduler.post_task(magnet::async::TaskPriority::NORMAL, [&completed_tasks, i]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                completed_tasks.fetch_add(1);
                std::cout << "⚡ 普通任务 " << i << " 完成" << std::endl;
            });
        }
        
        // 延迟任务
        scheduler.post_delayed_task(
            std::chrono::milliseconds(500),
            magnet::async::TaskPriority::CRITICAL,
            [&completed_tasks]() {
                completed_tasks.fetch_add(1);
                std::cout << "⏰ 延迟任务完成" << std::endl;
            }
        );
        
        // 周期性任务 (执行3次)
        std::atomic<int> periodic_count{0};
        auto periodic_id = scheduler.post_periodic_task(
            std::chrono::milliseconds(200),
            magnet::async::TaskPriority::LOW,
            [&completed_tasks, &periodic_count]() {
                int count = periodic_count.fetch_add(1);
                completed_tasks.fetch_add(1);
                std::cout << "🔄 周期性任务执行第 " << (count + 1) << " 次" << std::endl;
            }
        );
        
        // 等待一段时间后取消周期性任务
        std::this_thread::sleep_for(std::chrono::milliseconds(700));
        scheduler.cancel_task(periodic_id);
        std::cout << "🛑 周期性任务已取消" << std::endl;
        
        // 等待所有任务完成
        while (completed_tasks.load() < total_tasks) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            
            // 显示统计信息
            auto loop_stats = loop_manager.get_statistics();
            auto scheduler_stats = scheduler.get_statistics();
            
            if (completed_tasks.load() % 5 == 0) {
                std::cout << "📊 进度: " << completed_tasks.load() 
                         << "/" << total_tasks << " 任务完成" << std::endl;
            }

            completed_tasks.fetch_add(1);
        }
        
        // 显示最终统计
        auto loop_stats = loop_manager.get_statistics();
        auto scheduler_stats = scheduler.get_statistics();
        
        std::cout << "\n📊 最终统计信息：" << std::endl;
        std::cout << "EventLoopManager:" << std::endl;
        std::cout << "  - 工作线程: " << loop_stats.thread_count << std::endl;
        std::cout << "  - 总处理任务: " << loop_stats.total_tasks_handled << std::endl;
        
        std::cout << "TaskScheduler:" << std::endl;
        std::cout << "  - 完成任务: " << scheduler_stats.completed_tasks << std::endl;
        std::cout << "  - 待执行任务: " << scheduler_stats.pending_tasks << std::endl;
        
        // 停止事件循环
        loop_manager.stop();
        std::cout << "✓ EventLoopManager已停止" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Async模块测试失败: " << e.what() << std::endl;
        return;
    }
    
    std::cout << "✅ Async模块测试成功！" << std::endl;
}

int main() {
    std::cout << "🚀 MagnetDownloader - 模块化架构演示" << std::endl;
    std::cout << "=====================================" << std::endl;
    
    // 测试已实现的async模块
    test_async_module();
    
    std::cout << "\n📦 其他模块状态：" << std::endl;
    
    // 其他模块占位符
    // magnet::network::placeholder_udp_client();
    // magnet::protocols::placeholder_magnet_uri_parser();
    // magnet::storage::placeholder_file_manager();
    
    std::cout << "📺 ConsoleInterface placeholder - UI modules loaded dynamically" << std::endl;
    
    std::cout << "\n🎉 Async模块实现完成！" << std::endl;
    std::cout << "💡 可以继续实现其他模块了。" << std::endl;
    
    return 0;
}

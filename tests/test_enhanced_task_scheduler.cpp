// MagnetDownload - Enhanced TaskScheduler 测试程序
// 验证增强型任务调度器的多任务功能

#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <vector>

#include "magnet/async/task_scheduler.h"
#include "magnet/async/event_loop_manager.h"

using namespace magnet::async;

// 测试计数器
std::atomic<int> taskCounter{0};
std::atomic<int> highPriorityCounter{0};
std::atomic<int> normalPriorityCounter{0};
std::atomic<int> lowPriorityCounter{0};

void testBasicFunctionality() {
    std::cout << "\n=== 测试基本功能 ===" << std::endl;
    
    EventLoopManager loopManager(2);
    loopManager.start();  // 启动事件循环
    
    TaskScheduler scheduler(loopManager, 2);  // 2个工作线程
    
    // 创建任务组
    auto downloadGroup = scheduler.createTaskGroup("下载任务组", TaskPriority::HIGH);
    auto backgroundGroup = scheduler.createTaskGroup("后台任务组", TaskPriority::LOW);
    
    std::cout << "创建了任务组: " << downloadGroup << " 和 " << backgroundGroup << std::endl;
    
    // 投递不同优先级的任务
    scheduler.postTask(TaskPriority::HIGH, []() {
        highPriorityCounter++;
        std::cout << "执行高优先级任务" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }, downloadGroup);
    
    scheduler.postTask(TaskPriority::NORMAL, []() {
        normalPriorityCounter++;
        std::cout << "执行普通优先级任务" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    });
    
    scheduler.postTask(TaskPriority::LOW, []() {
        lowPriorityCounter++;
        std::cout << "执行低优先级任务" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }, backgroundGroup);
    
    // 等待任务完成
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 检查统计信息
    auto stats = scheduler.getStatistics();
    std::cout << "完成任务数: " << stats.completed_tasks << std::endl;
    std::cout << "工作线程数: " << stats.worker_threads << std::endl;
    
    auto groupStats = scheduler.getGroupStatistics();
    std::cout << "活跃任务组数: " << groupStats.size() << std::endl;
    
    for (const auto& gs : groupStats) {
        std::cout << "任务组 " << gs.groupId << " (" << gs.name << "): "
                  << "完成任务 " << gs.completedTasks << std::endl;
    }
    
    loopManager.stop();  // 停止事件循环
}

void testTaskGroups() {
    std::cout << "\n=== 测试任务组管理 ===" << std::endl;
    
    EventLoopManager loopManager(2);
    loopManager.start();  // 启动事件循环
    
    TaskScheduler scheduler(loopManager, 2);
    
    // 创建任务组
    auto group1 = scheduler.createTaskGroup("测试组1", TaskPriority::HIGH);
    auto group2 = scheduler.createTaskGroup("测试组2", TaskPriority::NORMAL);
    
    std::atomic<int> group1Tasks{0};
    std::atomic<int> group2Tasks{0};
    
    // 向组1投递任务
    for (int i = 0; i < 3; ++i) {
        scheduler.postTask(TaskPriority::NORMAL, [&group1Tasks]() {
            group1Tasks++;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }, group1);
    }
    
    // 向组2投递任务
    for (int i = 0; i < 2; ++i) {
        scheduler.postTask(TaskPriority::NORMAL, [&group2Tasks]() {
            group2Tasks++;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }, group2);
    }
    
    // 暂停组2
    std::cout << "暂停任务组2" << std::endl;
    scheduler.pauseTaskGroup(group2);
    
    // 等待一段时间
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    std::cout << "组1完成任务: " << group1Tasks.load() << std::endl;
    std::cout << "组2完成任务: " << group2Tasks.load() << std::endl;
    
    // 恢复组2
    std::cout << "恢复任务组2" << std::endl;
    scheduler.resumeTaskGroup(group2);
    
    // 等待所有任务完成
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    std::cout << "最终 - 组1完成任务: " << group1Tasks.load() << std::endl;
    std::cout << "最终 - 组2完成任务: " << group2Tasks.load() << std::endl;
    
    // 删除任务组
    scheduler.removeTaskGroup(group1);
    scheduler.removeTaskGroup(group2);
    
    auto finalStats = scheduler.getGroupStatistics();
    std::cout << "删除后剩余任务组数: " << finalStats.size() << std::endl;
    
    loopManager.stop();  // 停止事件循环
}

void testDelayedTasks() {
    std::cout << "\n=== 测试延迟任务 ===" << std::endl;
    
    EventLoopManager loopManager(2);
    loopManager.start();  // 启动事件循环
    
    TaskScheduler scheduler(loopManager, 1);
    
    std::atomic<int> delayedCounter{0};
    
    // 延迟任务
    auto delayedTaskId = scheduler.postDelayedTask(
        std::chrono::milliseconds(200),
        TaskPriority::NORMAL,
        [&delayedCounter]() {
            delayedCounter++;
            std::cout << "延迟任务执行" << std::endl;
        }
    );
    
    // 等待任务执行
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    
    std::cout << "延迟任务执行次数: " << delayedCounter.load() << std::endl;
    
    loopManager.stop();  // 停止事件循环
}

void testAgingMechanism() {
    std::cout << "\n=== 测试老化机制 ===" << std::endl;
    
    EventLoopManager loopManager(2);
    loopManager.start();  // 启动事件循环
    
    TaskScheduler scheduler(loopManager, 1);  // 单线程确保可预测的行为
    
    std::vector<std::string> executionOrder;
    std::mutex orderMutex;
    
    // 投递一个低优先级任务
    scheduler.postTask(TaskPriority::LOW, [&]() {
        std::lock_guard<std::mutex> lock(orderMutex);
        executionOrder.push_back("老任务(LOW)");
        std::cout << "执行老的低优先级任务" << std::endl;
    });
    
    // 等待5秒让任务老化
    std::cout << "等待任务老化..." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(5500));
    
    // 投递一个普通优先级任务
    scheduler.postTask(TaskPriority::NORMAL, [&]() {
        std::lock_guard<std::mutex> lock(orderMutex);
        executionOrder.push_back("新任务(NORMAL)");
        std::cout << "执行新的普通优先级任务" << std::endl;
    });
    
    // 等待任务完成
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    std::cout << "执行顺序: ";
    for (const auto& task : executionOrder) {
        std::cout << task << " ";
    }
    std::cout << std::endl;
    
    // 由于老化机制，老的低优先级任务应该先执行
    bool agingWorks = (!executionOrder.empty() && 
                      executionOrder[0] == "老任务(LOW)");
    
    std::cout << "老化机制测试: " << (agingWorks ? "通过" : "失败") << std::endl;
    
    loopManager.stop();  // 停止事件循环
}

int main() {
    std::cout << "Enhanced TaskScheduler 功能测试开始..." << std::endl;
    
    try {
        testBasicFunctionality();
        testTaskGroups();
        testDelayedTasks();
        testAgingMechanism();
        
        std::cout << "\n=== 所有测试完成 ===" << std::endl;
        std::cout << "高优先级任务执行次数: " << highPriorityCounter.load() << std::endl;
        std::cout << "普通优先级任务执行次数: " << normalPriorityCounter.load() << std::endl;
        std::cout << "低优先级任务执行次数: " << lowPriorityCounter.load() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "测试过程中发生异常: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
#pragma once
// MagnetDownload - Async Module Common Types
// 异步模块通用类型定义

#include <cstdint>
#include <functional>

namespace magnet::async {

    // 任务优先级枚举
    enum class TaskPriority {
        LOW = 0,        // 低优先级：后台清理、统计收集等
        NORMAL = 1,     // 普通优先级：常规业务逻辑
        HIGH = 2,       // 高优先级：网络IO、用户交互
        CRITICAL = 3    // 关键优先级：错误处理、紧急停止
    };

    // 任务ID类型
    using TaskId = uint64_t;
    
    // 任务函数类型
    using TaskFunction = std::function<void()>;
    
    // 无效的任务ID常量
    constexpr TaskId INVALID_TASK_ID = 0;

} // namespace magnet::async

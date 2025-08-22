# Logger系统实现指南

## 📋 文档概述

本文档详细说明MagnetDownload项目中Logger系统的设计与实现，包括实现目的、技术方案、关键注意事项以及常见问题的解决方案。

---

## 🎯 实现目的与价值

### 1.1 为什么需要专门的Logger系统

#### **问题背景**
在复杂的异步网络应用中，调试和监控是关键挑战：

```cpp
// ❌ 原始调试方式的问题
void some_async_function() {
    std::cout << "开始处理..." << std::endl;  // 线程不安全
    
    asio::async_read(socket, buffer, [](auto ec, auto bytes) {
        if (ec) {
            std::cerr << "错误: " << ec.message() << std::endl;  // 混乱输出
        }
        std::cout << "处理了 " << bytes << " 字节" << std::endl;  // 无时间戳
    });
}
```

**存在问题**：
- 🚨 **线程安全问题**: `std::cout`在多线程环境下输出混乱
- 📅 **无时间信息**: 无法追踪事件发生的时间顺序
- 🔍 **难以过滤**: 无法按重要性级别过滤信息
- 💾 **无持久化**: 程序退出后丢失所有调试信息
- 🎯 **定位困难**: 无法知道日志来自哪个线程或模块

#### **Logger系统的价值**

```cpp
// ✅ 使用Logger系统后
void some_async_function() {
    LOG_DEBUG("开始异步读取操作");
    
    asio::async_read(socket, buffer, [](auto ec, auto bytes) {
        if (ec) {
            LOG_ERROR("读取失败: " + ec.message());
        } else {
            LOG_INFO("成功读取 " + std::to_string(bytes) + " 字节");
        }
    });
}
```

**获得收益**：
- ✅ **线程安全**: 多线程环境下输出有序
- ✅ **时间追踪**: 精确到毫秒的时间戳
- ✅ **级别过滤**: 生产环境只显示重要信息
- ✅ **持久存储**: 日志文件永久保存
- ✅ **精确定位**: 显示线程ID和模块信息

### 1.2 对MagnetDownload项目的特殊意义

#### **网络协议调试需求**
```cpp
// DHT协议调试示例
void dht_client_send_query() {
    LOG_DEBUG("准备发送DHT查询");
    LOG_TRACE("目标节点: " + target_node.to_string());
    LOG_TRACE("查询类型: find_peers");
    
    // 发送后记录
    LOG_INFO("DHT查询已发送, transaction_id: " + tx_id);
}

void on_dht_response(const std::vector<uint8_t>& response) {
    LOG_DEBUG("收到DHT响应, 长度: " + std::to_string(response.size()));
    LOG_TRACE("响应内容: " + hex_dump(response));
    
    // 解析后记录结果
    if (peers.empty()) {
        LOG_WARN("DHT响应不包含任何peers");
    } else {
        LOG_INFO("发现 " + std::to_string(peers.size()) + " 个peers");
    }
}
```

#### **性能监控需求**
```cpp
// 性能监控示例
void download_performance_monitor() {
    LOG_INFO("下载统计 - 速度: " + format_speed(current_speed) + 
             ", 进度: " + format_progress(percentage) + "%");
    
    if (current_speed < MIN_EXPECTED_SPEED) {
        LOG_WARN("下载速度低于预期: " + format_speed(current_speed));
    }
}
```

---

## 💻 完整代码实现

### 9.1 头文件实现

#### **include/magnet/utils/logger.h**

```cpp
#pragma once
// MagnetDownload - Logger System
// 高性能异步日志系统，支持多线程、多输出目标、级别过滤

#include <string>
#include <memory>
#include <sstream>
#include <mutex>
#include <fstream>
#include <atomic>
#include <thread>
#include <queue>
#include <condition_variable>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <filesystem>

namespace magnet::utils {

/**
 * @brief 日志级别枚举
 * 数值越大优先级越高
 */
enum class LogLevel {
    TRACE = 0,  // 详细跟踪信息(开发阶段)
    DEBUG = 1,  // 调试信息(开发/测试阶段)
    INFO  = 2,  // 一般信息(生产环境默认)
    WARN  = 3,  // 警告信息(需要注意但不影响功能)
    ERROR = 4,  // 错误信息(功能受影响)
    FATAL = 5   // 致命错误(程序无法继续)
};

/**
 * @brief 高性能线程安全日志系统
 * 
 * 特性:
 * - 多级别日志过滤
 * - 异步写入(可选)
 * - 多输出目标(控制台+文件)
 * - 线程安全
 * - 高性能优化
 */
class Logger {
public:
    /**
     * @brief 获取Logger单例实例
     * @return Logger引用
     */
    static Logger& instance();
    
    /**
     * @brief 析构函数，确保所有日志都被写入
     */
    ~Logger();
    
    // 禁用拷贝和移动
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;
    
    // === 配置接口 ===
    
    /**
     * @brief 设置最小日志级别
     * @param level 低于此级别的日志将被忽略
     */
    void set_level(LogLevel level);
    
    /**
     * @brief 启用/禁用控制台输出
     * @param enable true启用，false禁用
     */
    void set_console_output(bool enable);
    
    /**
     * @brief 设置文件输出
     * @param filename 日志文件路径，如果为空则禁用文件输出
     */
    void set_file_output(const std::string& filename);
    
    /**
     * @brief 启用/禁用异步模式
     * @param enable true异步模式，false同步模式
     */
    void set_async_mode(bool enable);
    
    /**
     * @brief 设置日志轮转大小
     * @param max_size 单个日志文件最大字节数
     */
    void set_max_file_size(size_t max_size);
    
    // === 日志输出接口 ===
    
    /**
     * @brief 输出指定级别的日志
     * @param level 日志级别
     * @param message 日志消息
     */
    void log(LogLevel level, const std::string& message);
    
    /**
     * @brief 检查是否应该记录指定级别的日志
     * @param level 要检查的日志级别
     * @return true如果应该记录
     */
    bool should_log(LogLevel level) const;
    
    /**
     * @brief 强制刷新所有缓冲的日志
     */
    void flush();
    
    // === 便利方法 ===
    
    void trace(const std::string& message) { log(LogLevel::TRACE, message); }
    void debug(const std::string& message) { log(LogLevel::DEBUG, message); }
    void info(const std::string& message)  { log(LogLevel::INFO, message); }
    void warn(const std::string& message)  { log(LogLevel::WARN, message); }
    void error(const std::string& message) { log(LogLevel::ERROR, message); }
    void fatal(const std::string& message) { log(LogLevel::FATAL, message); }
    
    // === 格式化方法 ===
    
    /**
     * @brief 格式化日志输出
     * @tparam Args 参数类型包
     * @param level 日志级别
     * @param format 格式字符串(支持{}占位符)
     * @param args 格式化参数
     */
    template<typename... Args>
    void log_format(LogLevel level, const std::string& format, Args&&... args);
    
    // === 统计信息 ===
    
    struct Statistics {
        size_t total_messages{0};       // 总消息数
        size_t dropped_messages{0};     // 丢弃的消息数
        size_t queue_size{0};          // 当前队列大小
        size_t max_queue_size{0};      // 历史最大队列大小
        double avg_processing_time{0}; // 平均处理时间(ms)
    };
    
    /**
     * @brief 获取统计信息
     * @return 当前统计数据
     */
    Statistics get_statistics() const;

private:
    /**
     * @brief 私有构造函数(单例模式)
     */
    Logger();
    
    // === 内部实现 ===
    
    /**
     * @brief 日志条目结构
     */
    struct LogEntry {
        LogLevel level;
        std::string message;
        std::chrono::system_clock::time_point timestamp;
        std::thread::id thread_id;
        
        LogEntry() = default;
        LogEntry(LogLevel lvl, std::string msg) 
            : level(lvl), message(std::move(msg))
            , timestamp(std::chrono::system_clock::now())
            , thread_id(std::this_thread::get_id()) {}
    };
    
    /**
     * @brief 直接写入日志(同步)
     * @param entry 日志条目
     */
    void write_log_entry(const LogEntry& entry);
    
    /**
     * @brief 异步写入线程主函数
     */
    void async_writer_thread();
    
    /**
     * @brief 格式化日志消息
     * @param entry 日志条目
     * @return 格式化后的字符串
     */
    std::string format_message(const LogEntry& entry) const;
    
    /**
     * @brief 日志级别转字符串
     * @param level 日志级别
     * @return 级别字符串
     */
    std::string level_to_string(LogLevel level) const;
    
    /**
     * @brief 检查是否需要轮转日志文件
     */
    void check_log_rotation();
    
    /**
     * @brief 创建日志目录
     * @param filename 文件路径
     * @return true如果成功
     */
    bool create_log_directory(const std::string& filename);
    
    /**
     * @brief 简单的字符串格式化实现
     */
    template<typename T>
    void format_impl(std::ostringstream& oss, const std::string& format, size_t& pos, T&& value);
    
    template<typename T, typename... Args>
    void format_impl(std::ostringstream& oss, const std::string& format, size_t& pos, T&& value, Args&&... args);
    
    std::string format_string(const std::string& format);
    
    template<typename... Args>
    std::string format_string(const std::string& format, Args&&... args);
    
    // === 配置参数 ===
    std::atomic<LogLevel> min_level_{LogLevel::INFO};
    std::atomic<bool> console_enabled_{true};
    std::atomic<bool> file_enabled_{false};
    std::atomic<bool> async_enabled_{true};
    std::atomic<size_t> max_file_size_{100 * 1024 * 1024}; // 100MB
    
    // === 文件输出 ===
    std::string log_filename_;
    std::ofstream log_file_;
    std::atomic<size_t> current_file_size_{0};
    
    // === 异步写入系统 ===
    static constexpr size_t MAX_QUEUE_SIZE = 10000;
    static constexpr size_t BATCH_SIZE = 100;
    
    std::queue<LogEntry> log_queue_;
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::atomic<bool> shutdown_{false};
    std::thread writer_thread_;
    
    // === 同步写入保护 ===
    mutable std::mutex write_mutex_;
    
    // === 统计信息 ===
    mutable std::mutex stats_mutex_;
    Statistics statistics_;
};

// === 模板方法实现 ===

template<typename... Args>
void Logger::log_format(LogLevel level, const std::string& format, Args&&... args) {
    if (!should_log(level)) {
        return;
    }
    
    std::string formatted = format_string(format, std::forward<Args>(args)...);
    log(level, formatted);
}

template<typename T>
void Logger::format_impl(std::ostringstream& oss, const std::string& format, size_t& pos, T&& value) {
    size_t placeholder = format.find("{}", pos);
    if (placeholder != std::string::npos) {
        oss << format.substr(pos, placeholder - pos);
        oss << std::forward<T>(value);
        pos = placeholder + 2;
    }
}

template<typename T, typename... Args>
void Logger::format_impl(std::ostringstream& oss, const std::string& format, size_t& pos, T&& value, Args&&... args) {
    format_impl(oss, format, pos, std::forward<T>(value));
    format_impl(oss, format, pos, std::forward<Args>(args)...);
}

template<typename... Args>
std::string Logger::format_string(const std::string& format, Args&&... args) {
    std::ostringstream oss;
    size_t pos = 0;
    format_impl(oss, format, pos, std::forward<Args>(args)...);
    oss << format.substr(pos);
    return oss.str();
}

} // namespace magnet::utils

// === 便利宏定义 ===

#define LOG_TRACE(msg) do { \
    if (magnet::utils::Logger::instance().should_log(magnet::utils::LogLevel::TRACE)) { \
        magnet::utils::Logger::instance().trace(msg); \
    } \
} while(0)

#define LOG_DEBUG(msg) do { \
    if (magnet::utils::Logger::instance().should_log(magnet::utils::LogLevel::DEBUG)) { \
        magnet::utils::Logger::instance().debug(msg); \
    } \
} while(0)

#define LOG_INFO(msg) do { \
    if (magnet::utils::Logger::instance().should_log(magnet::utils::LogLevel::INFO)) { \
        magnet::utils::Logger::instance().info(msg); \
    } \
} while(0)

#define LOG_WARN(msg) do { \
    if (magnet::utils::Logger::instance().should_log(magnet::utils::LogLevel::WARN)) { \
        magnet::utils::Logger::instance().warn(msg); \
    } \
} while(0)

#define LOG_ERROR(msg) do { \
    if (magnet::utils::Logger::instance().should_log(magnet::utils::LogLevel::ERROR)) { \
        magnet::utils::Logger::instance().error(msg); \
    } \
} while(0)

#define LOG_FATAL(msg) do { \
    if (magnet::utils::Logger::instance().should_log(magnet::utils::LogLevel::FATAL)) { \
        magnet::utils::Logger::instance().fatal(msg); \
    } \
} while(0)

// 格式化宏
#define LOG_TRACE_FMT(fmt, ...) do { \
    if (magnet::utils::Logger::instance().should_log(magnet::utils::LogLevel::TRACE)) { \
        magnet::utils::Logger::instance().log_format(magnet::utils::LogLevel::TRACE, fmt, __VA_ARGS__); \
    } \
} while(0)

#define LOG_DEBUG_FMT(fmt, ...) do { \
    if (magnet::utils::Logger::instance().should_log(magnet::utils::LogLevel::DEBUG)) { \
        magnet::utils::Logger::instance().log_format(magnet::utils::LogLevel::DEBUG, fmt, __VA_ARGS__); \
    } \
} while(0)

#define LOG_INFO_FMT(fmt, ...) do { \
    if (magnet::utils::Logger::instance().should_log(magnet::utils::LogLevel::INFO)) { \
        magnet::utils::Logger::instance().log_format(magnet::utils::LogLevel::INFO, fmt, __VA_ARGS__); \
    } \
} while(0)

#define LOG_WARN_FMT(fmt, ...) do { \
    if (magnet::utils::Logger::instance().should_log(magnet::utils::LogLevel::WARN)) { \
        magnet::utils::Logger::instance().log_format(magnet::utils::LogLevel::WARN, fmt, __VA_ARGS__); \
    } \
} while(0)

#define LOG_ERROR_FMT(fmt, ...) do { \
    if (magnet::utils::Logger::instance().should_log(magnet::utils::LogLevel::ERROR)) { \
        magnet::utils::Logger::instance().log_format(magnet::utils::LogLevel::ERROR, fmt, __VA_ARGS__); \
    } \
} while(0)

#define LOG_FATAL_FMT(fmt, ...) do { \
    if (magnet::utils::Logger::instance().should_log(magnet::utils::LogLevel::FATAL)) { \
        magnet::utils::Logger::instance().log_format(magnet::utils::LogLevel::FATAL, fmt, __VA_ARGS__); \
    } \
} while(0)
```

### 9.2 源文件实现

#### **src/utils/logger.cpp**

```cpp
// MagnetDownload - Logger Implementation
// 高性能异步日志系统实现

#include <magnet/utils/logger.h>
#include <iostream>
#include <iomanip>
#include <sstream>

namespace magnet::utils {

// === 单例实现 ===

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

Logger::Logger() {
    // 初始化统计信息
    statistics_.total_messages = 0;
    statistics_.dropped_messages = 0;
    statistics_.queue_size = 0;
    statistics_.max_queue_size = 0;
    statistics_.avg_processing_time = 0.0;
    
    // 如果启用异步模式，启动写入线程
    if (async_enabled_.load()) {
        writer_thread_ = std::thread(&Logger::async_writer_thread, this);
    }
}

Logger::~Logger() {
    // 设置关闭标志
    shutdown_.store(true);
    
    // 通知写入线程
    queue_cv_.notify_all();
    
    // 等待写入线程结束
    if (writer_thread_.joinable()) {
        writer_thread_.join();
    }
    
    // 关闭文件
    if (log_file_.is_open()) {
        log_file_.close();
    }
}

// === 配置方法 ===

void Logger::set_level(LogLevel level) {
    min_level_.store(level, std::memory_order_relaxed);
}

void Logger::set_console_output(bool enable) {
    console_enabled_.store(enable, std::memory_order_relaxed);
}

void Logger::set_file_output(const std::string& filename) {
    std::lock_guard<std::mutex> lock(write_mutex_);
    
    // 关闭现有文件
    if (log_file_.is_open()) {
        log_file_.close();
    }
    
    if (filename.empty()) {
        file_enabled_.store(false, std::memory_order_relaxed);
        return;
    }
    
    // 创建目录
    if (!create_log_directory(filename)) {
        std::cerr << "Logger: 无法创建日志目录: " << filename << std::endl;
        file_enabled_.store(false, std::memory_order_relaxed);
        return;
    }
    
    // 打开新文件
    log_filename_ = filename;
    log_file_.open(filename, std::ios::app);
    
    if (log_file_.is_open()) {
        file_enabled_.store(true, std::memory_order_relaxed);
        
        // 获取当前文件大小
        log_file_.seekp(0, std::ios::end);
        current_file_size_.store(static_cast<size_t>(log_file_.tellp()), std::memory_order_relaxed);
    } else {
        std::cerr << "Logger: 无法打开日志文件: " << filename << std::endl;
        file_enabled_.store(false, std::memory_order_relaxed);
    }
}

void Logger::set_async_mode(bool enable) {
    bool old_value = async_enabled_.exchange(enable, std::memory_order_acq_rel);
    
    if (enable && !old_value) {
        // 启动异步线程
        if (!writer_thread_.joinable()) {
            shutdown_.store(false);
            writer_thread_ = std::thread(&Logger::async_writer_thread, this);
        }
    } else if (!enable && old_value) {
        // 停止异步线程
        shutdown_.store(true);
        queue_cv_.notify_all();
        
        if (writer_thread_.joinable()) {
            writer_thread_.join();
        }
    }
}

void Logger::set_max_file_size(size_t max_size) {
    max_file_size_.store(max_size, std::memory_order_relaxed);
}

// === 核心日志方法 ===

bool Logger::should_log(LogLevel level) const {
    return level >= min_level_.load(std::memory_order_relaxed);
}

void Logger::log(LogLevel level, const std::string& message) {
    if (!should_log(level)) {
        return;
    }
    
    // 更新统计信息
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        statistics_.total_messages++;
    }
    
    LogEntry entry(level, message);
    
    if (async_enabled_.load(std::memory_order_relaxed)) {
        // 异步模式：加入队列
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            
            // 检查队列是否已满
            if (log_queue_.size() >= MAX_QUEUE_SIZE) {
                // 队列满时的策略：丢弃最老的普通日志，保留重要日志
                if (level >= LogLevel::ERROR) {
                    // 重要日志：尝试丢弃一些低级别的日志
                    bool found_low_priority = false;
                    std::queue<LogEntry> temp_queue;
                    
                    while (!log_queue_.empty()) {
                        auto& front = log_queue_.front();
                        if (!found_low_priority && front.level < LogLevel::WARN) {
                            found_low_priority = true;
                            statistics_.dropped_messages++;
                        } else {
                            temp_queue.push(std::move(front));
                        }
                        log_queue_.pop();
                    }
                    
                    log_queue_ = std::move(temp_queue);
                    
                    if (!found_low_priority) {
                        // 没找到可丢弃的日志，丢弃最老的一条
                        if (!log_queue_.empty()) {
                            log_queue_.pop();
                            statistics_.dropped_messages++;
                        }
                    }
                } else {
                    // 普通日志：直接丢弃
                    statistics_.dropped_messages++;
                    return;
                }
            }
            
            log_queue_.push(std::move(entry));
            
            // 更新统计信息
            statistics_.queue_size = log_queue_.size();
            if (statistics_.queue_size > statistics_.max_queue_size) {
                statistics_.max_queue_size = statistics_.queue_size;
            }
        }
        
        // 通知写入线程
        queue_cv_.notify_one();
    } else {
        // 同步模式：直接写入
        write_log_entry(entry);
    }
}

void Logger::flush() {
    if (async_enabled_.load(std::memory_order_relaxed)) {
        // 异步模式：等待队列清空
        std::unique_lock<std::mutex> lock(queue_mutex_);
        
        // 设置一个临时的刷新标志，通过添加特殊条目
        LogEntry flush_marker(LogLevel::TRACE, "__FLUSH_MARKER__");
        log_queue_.push(flush_marker);
        queue_cv_.notify_one();
        
        // 等待队列处理完毕
        // 注意：这是一个简化实现，生产环境可能需要更复杂的同步机制
        while (!log_queue_.empty()) {
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            lock.lock();
        }
    } else {
        // 同步模式：直接刷新文件
        std::lock_guard<std::mutex> lock(write_mutex_);
        if (log_file_.is_open()) {
            log_file_.flush();
        }
    }
}

// === 内部实现方法 ===

void Logger::write_log_entry(const LogEntry& entry) {
    // 检查是否是刷新标志
    if (entry.message == "__FLUSH_MARKER__") {
        std::lock_guard<std::mutex> lock(write_mutex_);
        if (log_file_.is_open()) {
            log_file_.flush();
        }
        return;
    }
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    std::string formatted = format_message(entry);
    
    {
        std::lock_guard<std::mutex> lock(write_mutex_);
        
        // 控制台输出
        if (console_enabled_.load(std::memory_order_relaxed)) {
            if (entry.level >= LogLevel::ERROR) {
                std::cerr << formatted << std::endl;
            } else {
                std::cout << formatted << std::endl;
            }
        }
        
        // 文件输出
        if (file_enabled_.load(std::memory_order_relaxed) && log_file_.is_open()) {
            log_file_ << formatted << std::endl;
            
            // 更新文件大小
            size_t msg_size = formatted.length() + 1; // +1 for newline
            current_file_size_.fetch_add(msg_size, std::memory_order_relaxed);
            
            // 检查是否需要轮转
            check_log_rotation();
        }
    }
    
    // 更新性能统计
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        double processing_time = duration.count() / 1000.0; // 转换为毫秒
        statistics_.avg_processing_time = 
            (statistics_.avg_processing_time * (statistics_.total_messages - 1) + processing_time) 
            / statistics_.total_messages;
    }
}

void Logger::async_writer_thread() {
    std::vector<LogEntry> batch;
    batch.reserve(BATCH_SIZE);
    
    while (!shutdown_.load(std::memory_order_relaxed)) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            
            // 等待日志条目或关闭信号
            queue_cv_.wait(lock, [this] {
                return !log_queue_.empty() || shutdown_.load(std::memory_order_relaxed);
            });
            
            // 批量取出日志条目
            while (!log_queue_.empty() && batch.size() < BATCH_SIZE) {
                batch.push_back(std::move(log_queue_.front()));
                log_queue_.pop();
            }
            
            // 更新队列大小统计
            {
                std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                statistics_.queue_size = log_queue_.size();
            }
        }
        
        // 批量写入日志
        for (const auto& entry : batch) {
            write_log_entry(entry);
        }
        
        batch.clear();
    }
    
    // 处理剩余的日志条目
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        while (!log_queue_.empty()) {
            write_log_entry(log_queue_.front());
            log_queue_.pop();
        }
    }
}

std::string Logger::format_message(const LogEntry& entry) const {
    std::ostringstream oss;
    
    // 时间戳
    auto time_t = std::chrono::system_clock::to_time_t(entry.timestamp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        entry.timestamp.time_since_epoch()) % 1000;
    
    oss << "[" << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    oss << "." << std::setfill('0') << std::setw(3) << ms.count() << "]";
    
    // 日志级别
    oss << "[" << std::setw(5) << level_to_string(entry.level) << "]";
    
    // 线程ID
    oss << "[" << entry.thread_id << "]";
    
    // 消息内容
    oss << " " << entry.message;
    
    return oss.str();
}

std::string Logger::level_to_string(LogLevel level) const {
    switch (level) {
        case LogLevel::TRACE: return "TRACE";
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::FATAL: return "FATAL";
        default: return "UNKNW";
    }
}

void Logger::check_log_rotation() {
    if (current_file_size_.load(std::memory_order_relaxed) > max_file_size_.load(std::memory_order_relaxed)) {
        // 简单的日志轮转：重命名当前文件，创建新文件
        if (log_file_.is_open()) {
            log_file_.close();
        }
        
        // 生成备份文件名
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        
        std::ostringstream backup_name;
        backup_name << log_filename_ << "."
                   << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
        
        // 重命名文件
        try {
            std::filesystem::rename(log_filename_, backup_name.str());
        } catch (const std::exception& e) {
            std::cerr << "Logger: 日志轮转失败: " << e.what() << std::endl;
        }
        
        // 创建新文件
        log_file_.open(log_filename_, std::ios::app);
        current_file_size_.store(0, std::memory_order_relaxed);
    }
}

bool Logger::create_log_directory(const std::string& filename) {
    try {
        std::filesystem::path log_path(filename);
        auto parent_dir = log_path.parent_path();
        
        if (!parent_dir.empty() && !std::filesystem::exists(parent_dir)) {
            std::filesystem::create_directories(parent_dir);
        }
        
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

Logger::Statistics Logger::get_statistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return statistics_;
}

// === 模板方法的具体实现 ===

std::string Logger::format_string(const std::string& format) {
    return format;
}

} // namespace magnet::utils
```

### 9.3 CMake配置更新

#### **src/utils/CMakeLists.txt** (更新)

```cmake
# MagnetDownload 通用工具库
# 提供日志、配置、字符串处理等基础工具

message(STATUS "Configuring Utils Library...")

add_library(magnet_utils STATIC
    logger.cpp                     # Logger实现
    # config.cpp                   # 你将要实现的
    # string_utils.cpp             # 你将要实现的  
    # hash_utils.cpp               # 你将要实现的
)

target_include_directories(magnet_utils
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# 依赖系统库和C++17文件系统
target_link_libraries(magnet_utils
    PRIVATE  
        Threads::Threads            # Logger需要线程支持
)

# 设置编译特性
target_compile_features(magnet_utils PUBLIC cxx_std_17)

# 编译选项
target_compile_options(magnet_utils PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# 导出符号定义
target_compile_definitions(magnet_utils
    PUBLIC 
        MAGNET_UTILS_AVAILABLE
        # 添加Logger相关的编译定义
        MAGNET_LOGGER_MAX_MESSAGE_SIZE=4096
        MAGNET_LOGGER_DEFAULT_QUEUE_SIZE=10000
)

# 链接文件系统库(某些编译器需要)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0")
    target_link_libraries(magnet_utils PRIVATE stdc++fs)
endif()

message(STATUS "Utils library configured with Logger support!")
```

### 9.4 集成到主程序

#### **src/application/main.cpp** (更新)

```cpp
// MagnetDownload - Main Application Entry Point
// 主程序入口 - 集成所有模块，演示Logger和Async模块功能

#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <magnet/async/event_loop_manager.h>
#include <magnet/async/task_scheduler.h>
#include <magnet/utils/logger.h>

// 其他模块的占位函数声明
namespace magnet::network { void placeholder_udp_client(); }
namespace magnet::protocols { void placeholder_magnet_uri_parser(); }
namespace magnet::storage { void placeholder_file_manager(); }

void setup_logger() {
    auto& logger = magnet::utils::Logger::instance();
    
    // 开发环境配置
    logger.set_level(magnet::utils::LogLevel::DEBUG);
    logger.set_console_output(true);
    logger.set_file_output("./logs/magnetdownload.log");
    logger.set_async_mode(true);
    logger.set_max_file_size(50 * 1024 * 1024); // 50MB
    
    LOG_INFO("🚀 MagnetDownloader Logger系统已启动");
    LOG_DEBUG("Logger配置: DEBUG级别, 异步模式, 文件+控制台输出");
}

void test_logger_features() {
    LOG_INFO("📋 开始Logger功能测试...");
    
    // 测试不同级别的日志
    LOG_TRACE("这是TRACE级别日志 - 详细跟踪信息");
    LOG_DEBUG("这是DEBUG级别日志 - 调试信息");
    LOG_INFO("这是INFO级别日志 - 一般信息");
    LOG_WARN("这是WARN级别日志 - 警告信息");
    LOG_ERROR("这是ERROR级别日志 - 错误信息");
    LOG_FATAL("这是FATAL级别日志 - 致命错误");
    
    // 测试格式化日志
    int task_count = 42;
    double progress = 75.5;
    LOG_INFO_FMT("任务进度更新: {}/{} 完成, 进度: {:.1f}%", 
                 task_count, 56, progress);
    
    // 测试多线程日志
    LOG_INFO("🧵 开始多线程日志测试...");
    std::vector<std::thread> threads;
    
    for (int i = 0; i < 5; ++i) {
        threads.emplace_back([i]() {
            for (int j = 0; j < 10; ++j) {
                LOG_DEBUG_FMT("线程{} - 消息{}: 时间戳测试", i, j);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    LOG_INFO("✅ Logger多线程测试完成");
    
    // 显示统计信息
    auto stats = magnet::utils::Logger::instance().get_statistics();
    LOG_INFO_FMT("📊 Logger统计信息:");
    LOG_INFO_FMT("  - 总消息数: {}", stats.total_messages);
    LOG_INFO_FMT("  - 丢弃消息数: {}", stats.dropped_messages);
    LOG_INFO_FMT("  - 当前队列大小: {}", stats.queue_size);
    LOG_INFO_FMT("  - 历史最大队列: {}", stats.max_queue_size);
    LOG_INFO_FMT("  - 平均处理时间: {:.3f}ms", stats.avg_processing_time);
}

void test_async_module() {
    LOG_INFO("🔄 测试Async模块功能...");
    
    try {
        // 创建事件循环管理器
        magnet::async::EventLoopManager loop_manager(4);
        LOG_INFO("✓ EventLoopManager创建成功 (4个工作线程)");
        
        // 启动事件循环
        loop_manager.start();
        LOG_INFO("✓ EventLoopManager启动成功");
        
        // 创建任务调度器
        magnet::async::TaskScheduler scheduler(loop_manager);
        LOG_INFO("✓ TaskScheduler创建成功");
        
        // 测试任务计数器
        std::atomic<int> completed_tasks{0};
        const int total_tasks = 20;
        
        // 投递不同优先级的任务
        LOG_INFO("📋 投递任务测试...");
        
        // 高优先级任务
        for (int i = 0; i < 5; ++i) {
            scheduler.post_task(magnet::async::TaskPriority::HIGH, [&completed_tasks, i]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                completed_tasks.fetch_add(1);
                LOG_DEBUG_FMT("🔥 高优先级任务 {} 完成", i);
            });
        }
        
        // 普通优先级任务
        for (int i = 0; i < 10; ++i) {
            scheduler.post_task(magnet::async::TaskPriority::NORMAL, [&completed_tasks, i]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                completed_tasks.fetch_add(1);
                LOG_DEBUG_FMT("⚡ 普通任务 {} 完成", i);
            });
        }
        
        // 低优先级任务
        for (int i = 0; i < 5; ++i) {
            scheduler.post_task(magnet::async::TaskPriority::LOW, [&completed_tasks, i]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(3));
                completed_tasks.fetch_add(1);
                LOG_DEBUG_FMT("🔽 低优先级任务 {} 完成", i);
            });
        }
        
        // 测试延迟任务
        auto delay_task_id = scheduler.post_delayed_task(
            std::chrono::milliseconds(500),
            magnet::async::TaskPriority::NORMAL,
            []() {
                LOG_INFO("⏰ 延迟任务完成");
            }
        );
        
        // 测试周期性任务
        auto periodic_task_id = scheduler.post_periodic_task(
            std::chrono::milliseconds(200),
            magnet::async::TaskPriority::LOW,
            []() {
                static int count = 0;
                LOG_DEBUG_FMT("🔄 周期性任务执行第 {} 次", ++count);
            }
        );
        
        // 等待任务完成
        LOG_INFO("⏳ 等待任务完成...");
        while (completed_tasks.load() < total_tasks) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // 取消周期性任务
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        if (scheduler.cancel_task(periodic_task_id)) {
            LOG_INFO("🛑 周期性任务已取消");
        }
        
        // 获取统计信息
        auto loop_stats = loop_manager.get_statistics();
        auto task_stats = scheduler.get_statistics();
        
        LOG_INFO("📊 最终统计信息:");
        LOG_INFO("EventLoopManager:");
        LOG_INFO_FMT("  - 工作线程: {}", loop_stats.thread_count);
        LOG_INFO_FMT("  - 总处理任务: {}", loop_stats.total_tasks_handled);
        
        LOG_INFO("TaskScheduler:");
        LOG_INFO_FMT("  - 完成任务: {}", task_stats.completed_tasks);
        LOG_INFO_FMT("  - 待执行任务: {}", task_stats.pending_tasks);
        
        // 停止事件循环
        loop_manager.stop();
        LOG_INFO("✓ EventLoopManager已停止");
        
        LOG_INFO("✅ Async模块测试成功！");
        
    } catch (const std::exception& e) {
        LOG_FATAL_FMT("Async模块测试异常: {}", e.what());
        throw;
    }
}

int main() {
    try {
        // 设置Logger系统
        setup_logger();
        
        std::cout << "🚀 MagnetDownloader - 模块化架构演示" << std::endl;
        std::cout << "=====================================" << std::endl;
        
        // 测试Logger功能
        test_logger_features();
        
        // 测试已实现的async模块
        test_async_module();
        
        LOG_INFO("📦 其他模块状态：");
        
        // 其他模块占位符
        magnet::network::placeholder_udp_client();
        magnet::protocols::placeholder_magnet_uri_parser();
        magnet::storage::placeholder_file_manager();
        
        LOG_INFO("📺 ConsoleInterface placeholder - UI modules loaded dynamically");
        
        LOG_INFO("🎉 Logger + Async模块集成测试完成！");
        LOG_INFO("💡 可以继续实现其他模块了。");
        
        // 刷新所有日志
        magnet::utils::Logger::instance().flush();
        
        return 0;
        
    } catch (const std::exception& e) {
        LOG_FATAL_FMT("程序异常退出: {}", e.what());
        std::cerr << "❌ 程序异常退出: " << e.what() << std::endl;
        return 1;
    }
}
```

### 9.5 测试用例实现

#### **tests/test_logger.cpp** (新建)

```cpp
// MagnetDownload - Logger Test Cases
// Logger系统的全面测试用例

#include <magnet/utils/logger.h>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include <fstream>
#include <filesystem>
#include <cassert>
#include <iostream>

namespace magnet::tests {

class LoggerTester {
public:
    static void run_all_tests() {
        std::cout << "🧪 开始Logger系统测试..." << std::endl;
        
        test_basic_logging();
        test_level_filtering();
        test_file_output();
        test_async_mode();
        test_multithreaded_stress();
        test_performance();
        test_error_handling();
        
        std::cout << "✅ 所有Logger测试通过！" << std::endl;
    }
    
private:
    static void test_basic_logging() {
        std::cout << "📝 测试基本日志功能..." << std::endl;
        
        auto& logger = magnet::utils::Logger::instance();
        
        // 配置Logger
        logger.set_level(magnet::utils::LogLevel::TRACE);
        logger.set_console_output(true);
        
        // 测试所有级别
        LOG_TRACE("TRACE消息测试");
        LOG_DEBUG("DEBUG消息测试");
        LOG_INFO("INFO消息测试");
        LOG_WARN("WARN消息测试");
        LOG_ERROR("ERROR消息测试");
        LOG_FATAL("FATAL消息测试");
        
        // 测试格式化
        LOG_INFO_FMT("格式化测试: {} + {} = {}", 1, 2, 3);
        
        std::cout << "✓ 基本日志功能测试通过" << std::endl;
    }
    
    static void test_level_filtering() {
        std::cout << "🔍 测试级别过滤..." << std::endl;
        
        auto& logger = magnet::utils::Logger::instance();
        
        // 设置为WARN级别
        logger.set_level(magnet::utils::LogLevel::WARN);
        
        auto stats_before = logger.get_statistics();
        
        // 这些不应该被记录
        LOG_DEBUG("这条不应该输出");
        LOG_INFO("这条也不应该输出");
        
        // 这些应该被记录
        LOG_WARN("这条应该输出");
        LOG_ERROR("这条也应该输出");
        
        auto stats_after = logger.get_statistics();
        
        // 应该只增加了2条消息
        assert(stats_after.total_messages - stats_before.total_messages == 2);
        
        // 恢复DEBUG级别
        logger.set_level(magnet::utils::LogLevel::DEBUG);
        
        std::cout << "✓ 级别过滤测试通过" << std::endl;
    }
    
    static void test_file_output() {
        std::cout << "📁 测试文件输出..." << std::endl;
        
        auto& logger = magnet::utils::Logger::instance();
        
        const std::string test_file = "./test_logs/logger_test.log";
        
        // 确保目录存在
        std::filesystem::create_directories("./test_logs");
        
        // 设置文件输出
        logger.set_file_output(test_file);
        
        const std::string test_message = "文件输出测试消息";
        LOG_INFO(test_message);
        
        // 刷新确保写入
        logger.flush();
        
        // 检查文件内容
        std::ifstream file(test_file);
        assert(file.is_open());
        
        std::string line;
        bool found = false;
        while (std::getline(file, line)) {
            if (line.find(test_message) != std::string::npos) {
                found = true;
                break;
            }
        }
        
        assert(found);
        
        // 清理
        std::filesystem::remove(test_file);
        
        std::cout << "✓ 文件输出测试通过" << std::endl;
    }
    
    static void test_async_mode() {
        std::cout << "⚡ 测试异步模式..." << std::endl;
        
        auto& logger = magnet::utils::Logger::instance();
        
        // 启用异步模式
        logger.set_async_mode(true);
        
        const int message_count = 1000;
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < message_count; ++i) {
            LOG_DEBUG_FMT("异步测试消息 {}", i);
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        std::cout << "投递" << message_count << "条消息耗时: " << duration.count() << "ms" << std::endl;
        
        // 异步投递应该很快
        assert(duration.count() < 100);
        
        // 等待处理完成
        logger.flush();
        
        std::cout << "✓ 异步模式测试通过" << std::endl;
    }
    
    static void test_multithreaded_stress() {
        std::cout << "🧵 测试多线程压力..." << std::endl;
        
        auto& logger = magnet::utils::Logger::instance();
        logger.set_async_mode(true);
        
        const int thread_count = 10;
        const int messages_per_thread = 1000;
        
        std::vector<std::thread> threads;
        std::atomic<int> completed_threads{0};
        
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int t = 0; t < thread_count; ++t) {
            threads.emplace_back([t, messages_per_thread, &completed_threads]() {
                for (int i = 0; i < messages_per_thread; ++i) {
                    LOG_DEBUG_FMT("线程{}消息{}", t, i);
                }
                completed_threads.fetch_add(1);
            });
        }
        
        for (auto& thread : threads) {
            thread.join();
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        // 等待所有消息处理完成
        logger.flush();
        
        std::cout << thread_count << "个线程，每个" << messages_per_thread 
                  << "条消息，总耗时: " << duration.count() << "ms" << std::endl;
        
        // 获取统计信息
        auto stats = logger.get_statistics();
        std::cout << "丢弃消息数: " << stats.dropped_messages << std::endl;
        
        assert(completed_threads.load() == thread_count);
        
        std::cout << "✓ 多线程压力测试通过" << std::endl;
    }
    
    static void test_performance() {
        std::cout << "🚀 测试性能基准..." << std::endl;
        
        auto& logger = magnet::utils::Logger::instance();
        logger.set_async_mode(true);
        logger.set_console_output(false); // 关闭控制台输出提高性能
        
        const int message_count = 100000;
        
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < message_count; ++i) {
            LOG_INFO_FMT("性能测试消息 {}", i);
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        double messages_per_second = static_cast<double>(message_count) / duration.count() * 1000;
        
        std::cout << "性能结果:" << std::endl;
        std::cout << "  - 消息数量: " << message_count << std::endl;
        std::cout << "  - 总耗时: " << duration.count() << "ms" << std::endl;
        std::cout << "  - 吞吐量: " << static_cast<int>(messages_per_second) << " msg/s" << std::endl;
        
        // 性能目标: >50,000 msg/s
        assert(messages_per_second > 50000);
        
        // 等待处理完成
        logger.flush();
        
        logger.set_console_output(true); // 恢复控制台输出
        
        std::cout << "✓ 性能基准测试通过" << std::endl;
    }
    
    static void test_error_handling() {
        std::cout << "⚠️ 测试错误处理..." << std::endl;
        
        auto& logger = magnet::utils::Logger::instance();
        
        // 测试无效文件路径
        logger.set_file_output("/invalid/path/that/does/not/exist/test.log");
        
        // 应该不会崩溃，而是回退到控制台输出
        LOG_WARN("错误处理测试 - 无效文件路径");
        
        // 测试大消息
        std::string large_message(10000, 'X');
        LOG_DEBUG(large_message);
        
        // 测试空消息
        LOG_INFO("");
        
        std::cout << "✓ 错误处理测试通过" << std::endl;
    }
};

} // namespace magnet::tests

// 测试主函数
int main() {
    try {
        magnet::tests::LoggerTester::run_all_tests();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "测试失败: " << e.what() << std::endl;
        return 1;
    }
}
```

---

## 🏗️ 技术设计方案

### 2.1 整体架构设计

#### **设计原则**
1. **高性能**: 异步写入，最小化对主业务的影响
2. **线程安全**: 支持多线程并发写入
3. **可配置性**: 运行时调整级别和输出目标
4. **可扩展性**: 易于添加新的输出目标和格式

#### **架构图**
```
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   应用代码      │    │   Logger API    │    │  输出目标        │
│                │────▶│                │────▶│                │
│ LOG_INFO(...)  │    │ • 级别过滤      │    │ • Console       │
│ LOG_ERROR(...) │    │ • 格式化        │    │ • File          │
│ LOG_DEBUG(...) │    │ • 队列管理      │    │ • Network       │
└─────────────────┘    └─────────────────┘    └─────────────────┘
                                ↓
                       ┌─────────────────┐
                       │  异步写入线程    │
                       │                │
                       │ • 批量处理      │
                       │ • 性能优化      │
                       │ • 异常处理      │
                       └─────────────────┘
```

### 2.2 核心组件设计

#### **2.2.1 日志级别系统**

```cpp
enum class LogLevel {
    TRACE = 0,  // 详细跟踪信息 (开发阶段)
    DEBUG = 1,  // 调试信息 (开发/测试阶段)
    INFO  = 2,  // 一般信息 (生产环境默认)
    WARN  = 3,  // 警告信息 (需要注意但不影响功能)
    ERROR = 4,  // 错误信息 (功能受影响)
    FATAL = 5   // 致命错误 (程序无法继续)
};
```

**级别使用指导**：
- **TRACE**: 函数进入/退出、变量值变化
- **DEBUG**: 算法步骤、状态转换、配置信息  
- **INFO**: 重要操作完成、连接建立、文件处理
- **WARN**: 重试操作、性能下降、配置问题
- **ERROR**: 操作失败、连接断开、数据错误
- **FATAL**: 系统崩溃、不可恢复错误

#### **2.2.2 消息格式设计**

```cpp
// 标准格式: [时间戳][级别][线程ID] 消息内容
// 示例: [2024-08-22 14:30:45.123][INFO][12345] DHT客户端启动成功
std::string format_message(LogLevel level, const std::string& message) {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    ss << "[" << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << ms.count() << "]";
    ss << "[" << level_to_string(level) << "]";
    ss << "[" << std::this_thread::get_id() << "] ";
    ss << message;
    
    return ss.str();
}
```

#### **2.2.3 异步写入系统**

```cpp
class AsyncLogWriter {
    // 日志条目结构
    struct LogEntry {
        LogLevel level;
        std::string message;
        std::chrono::system_clock::time_point timestamp;
        std::thread::id thread_id;
    };
    
    // 线程安全队列
    std::queue<LogEntry> log_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    
    // 后台写入线程
    void writer_thread_main() {
        while (!shutdown_) {
            // 等待日志条目
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] { 
                return !log_queue_.empty() || shutdown_; 
            });
            
            // 批量处理日志
            std::vector<LogEntry> batch;
            while (!log_queue_.empty() && batch.size() < BATCH_SIZE) {
                batch.push_back(std::move(log_queue_.front()));
                log_queue_.pop();
            }
            lock.unlock();
            
            // 写入所有输出目标
            write_batch(batch);
        }
    }
};
```

### 2.3 性能优化策略

#### **2.3.1 零拷贝优化**
```cpp
// 使用移动语义避免字符串拷贝
void log(LogLevel level, std::string&& message) {
    if (level < min_level_) return;
    
    LogEntry entry;
    entry.level = level;
    entry.message = std::move(message);  // 移动而不是拷贝
    entry.timestamp = std::chrono::system_clock::now();
    
    enqueue_log(std::move(entry));
}
```

#### **2.3.2 内存池优化**
```cpp
class LoggerMemoryPool {
    // 预分配字符串池
    std::vector<std::string> string_pool_;
    std::atomic<size_t> pool_index_{0};
    
public:
    std::string* acquire_string() {
        size_t index = pool_index_.fetch_add(1) % string_pool_.size();
        string_pool_[index].clear();
        return &string_pool_[index];
    }
};
```

#### **2.3.3 批量写入优化**
```cpp
void write_batch(const std::vector<LogEntry>& entries) {
    // 构建批量输出缓冲区
    std::stringstream buffer;
    for (const auto& entry : entries) {
        buffer << format_message(entry.level, entry.message) << '\n';
    }
    
    // 一次性写入
    std::string batch_output = buffer.str();
    write_to_targets(batch_output);
}
```

---

## ⚠️ 关键注意事项

### 3.1 线程安全问题

#### **问题1: 单例模式的线程安全**
```cpp
// ❌ 非线程安全的单例实现
class Logger {
    static Logger* instance_;
public:
    static Logger& instance() {
        if (!instance_) {
            instance_ = new Logger();  // 竞争条件！
        }
        return *instance_;
    }
};
```

```cpp
// ✅ 线程安全的单例实现
class Logger {
public:
    static Logger& instance() {
        static Logger logger;  // C++11保证线程安全
        return logger;
    }
};
```

#### **问题2: 文件输出的线程安全**
```cpp
// ❌ 多线程同时写文件会导致内容混乱
void unsafe_file_write(const std::string& message) {
    log_file_ << message << std::endl;  // 竞争条件！
}
```

```cpp
// ✅ 使用锁保护文件写入
void safe_file_write(const std::string& message) {
    std::lock_guard<std::mutex> lock(file_mutex_);
    log_file_ << message << std::endl;
    log_file_.flush();
}
```

### 3.2 内存管理问题

#### **问题1: 异步队列的内存泄漏**
```cpp
// ⚠️ 需要确保程序退出时清空队列
~Logger() {
    shutdown_ = true;
    queue_cv_.notify_all();
    
    if (writer_thread_.joinable()) {
        writer_thread_.join();
    }
    
    // 清理剩余日志
    while (!log_queue_.empty()) {
        write_log_entry(log_queue_.front());
        log_queue_.pop();
    }
}
```

#### **问题2: 字符串临时对象的性能影响**
```cpp
// ❌ 产生大量临时字符串对象
LOG_INFO("处理了 " + std::to_string(count) + " 个文件");

// ✅ 使用格式化函数减少临时对象
LOG_INFO_FMT("处理了 {} 个文件", count);
```

### 3.3 性能影响控制

#### **问题1: 日志级别检查优化**
```cpp
// ❌ 即使不输出也会构造字符串
LOG_DEBUG("复杂计算结果: " + expensive_calculation());

// ✅ 先检查级别再计算
if (logger.should_log(LogLevel::DEBUG)) {
    LOG_DEBUG("复杂计算结果: " + expensive_calculation());
}

// ✅ 更好的解决方案：宏自动检查
#define LOG_DEBUG(msg) do { \
    if (magnet::utils::Logger::instance().should_log(LogLevel::DEBUG)) { \
        magnet::utils::Logger::instance().debug(msg); \
    } \
} while(0)
```

#### **问题2: 异步队列的反压控制**
```cpp
class Logger {
    static constexpr size_t MAX_QUEUE_SIZE = 10000;
    
public:
    void log(LogLevel level, const std::string& message) {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        
        if (log_queue_.size() >= MAX_QUEUE_SIZE) {
            // 队列满时的策略
            if (level >= LogLevel::ERROR) {
                // 重要日志：丢弃最老的普通日志
                remove_old_normal_logs();
            } else {
                // 普通日志：直接丢弃
                return;
            }
        }
        
        log_queue_.emplace(level, message, std::chrono::system_clock::now());
        queue_cv_.notify_one();
    }
};
```

---

## 🔧 常见问题与解决方案

### 4.1 编译问题

#### **问题1: 头文件找不到**
```bash
# 错误信息
error: 'magnet/utils/logger.h' file not found

# 解决方案：检查CMake配置
target_include_directories(your_target PRIVATE
    ${CMAKE_SOURCE_DIR}/include  # 确保include路径正确
)
```

#### **问题2: 链接错误**
```bash
# 错误信息
undefined reference to 'magnet::utils::Logger::instance()'

# 解决方案：确保链接utils库
target_link_libraries(your_target PRIVATE
    magnet_utils  # 确保链接了utils库
)
```

### 4.2 运行时问题

#### **问题1: 日志文件创建失败**
```cpp
void Logger::set_file_output(const std::string& filename) {
    // 确保目录存在
    std::filesystem::path log_path(filename);
    auto parent_dir = log_path.parent_path();
    
    if (!parent_dir.empty() && !std::filesystem::exists(parent_dir)) {
        try {
            std::filesystem::create_directories(parent_dir);
        } catch (const std::exception& e) {
            // 回退到控制台输出
            error("无法创建日志目录: " + std::string(e.what()));
            return;
        }
    }
    
    log_file_.open(filename, std::ios::app);
    if (!log_file_.is_open()) {
        error("无法打开日志文件: " + filename);
    }
}
```

#### **问题2: 程序退出时日志丢失**
```cpp
// 在main函数退出前确保日志刷新
int main() {
    try {
        // 程序主逻辑
        run_application();
    } catch (...) {
        LOG_FATAL("程序异常退出");
    }
    
    // 确保所有日志都写入
    magnet::utils::Logger::instance().flush();
    
    return 0;
}
```

### 4.3 性能问题

#### **问题1: 日志输出影响程序性能**
```cpp
// 解决方案1: 使用更高的日志级别
Logger::instance().set_level(LogLevel::INFO);  // 生产环境

// 解决方案2: 增大异步队列批处理大小
class Logger {
    static constexpr size_t BATCH_SIZE = 100;  // 批量处理100条日志
};

// 解决方案3: 条件编译
#ifdef DEBUG_BUILD
    #define LOG_DEBUG(msg) Logger::instance().debug(msg)
#else
    #define LOG_DEBUG(msg) do {} while(0)  // 编译时优化掉
#endif
```

#### **问题2: 内存使用过高**
```cpp
// 解决方案：限制单条日志的长度
void Logger::log(LogLevel level, std::string message) {
    constexpr size_t MAX_MESSAGE_SIZE = 4096;
    
    if (message.length() > MAX_MESSAGE_SIZE) {
        message.resize(MAX_MESSAGE_SIZE - 3);
        message += "...";
    }
    
    // 继续处理...
}
```

---

## 🧪 测试验证方案

### 5.1 功能测试

#### **测试1: 基本日志输出**
```cpp
void test_basic_logging() {
    auto& logger = Logger::instance();
    
    logger.set_level(LogLevel::TRACE);
    logger.set_console_output(true);
    logger.set_file_output("test.log");
    
    // 测试所有级别
    LOG_TRACE("TRACE消息测试");
    LOG_DEBUG("DEBUG消息测试");
    LOG_INFO("INFO消息测试");
    LOG_WARN("WARN消息测试");
    LOG_ERROR("ERROR消息测试");
    LOG_FATAL("FATAL消息测试");
    
    // 验证文件内容
    assert(file_contains("test.log", "TRACE消息测试"));
    assert(file_contains("test.log", "FATAL消息测试"));
}
```

#### **测试2: 级别过滤**
```cpp
void test_level_filtering() {
    auto& logger = Logger::instance();
    logger.set_level(LogLevel::WARN);
    
    LOG_DEBUG("这条不应该输出");
    LOG_INFO("这条也不应该输出");
    LOG_WARN("这条应该输出");
    LOG_ERROR("这条也应该输出");
    
    // 验证只有WARN和ERROR被输出
}
```

### 5.2 性能测试

#### **测试1: 吞吐量测试**
```cpp
void test_throughput() {
    auto& logger = Logger::instance();
    const int MESSAGE_COUNT = 100000;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < MESSAGE_COUNT; ++i) {
        LOG_INFO("性能测试消息 " + std::to_string(i));
    }
    
    logger.flush();  // 确保所有消息都处理完
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "处理 " << MESSAGE_COUNT << " 条消息耗时: " 
              << duration.count() << "ms" << std::endl;
    
    // 性能目标: 100,000条消息在1秒内完成
    assert(duration.count() < 1000);
}
```

#### **测试2: 多线程压力测试**
```cpp
void test_multithreaded_stress() {
    const int THREAD_COUNT = 10;
    const int MESSAGES_PER_THREAD = 10000;
    
    std::vector<std::thread> threads;
    std::atomic<int> total_messages{0};
    
    for (int t = 0; t < THREAD_COUNT; ++t) {
        threads.emplace_back([t, &total_messages]() {
            for (int i = 0; i < MESSAGES_PER_THREAD; ++i) {
                LOG_INFO("线程" + std::to_string(t) + "消息" + std::to_string(i));
                total_messages.fetch_add(1);
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    Logger::instance().flush();
    
    // 验证所有消息都被处理
    assert(total_messages.load() == THREAD_COUNT * MESSAGES_PER_THREAD);
}
```

### 5.3 稳定性测试

#### **测试1: 长时间运行测试**
```cpp
void test_long_running() {
    auto& logger = Logger::instance();
    const auto TEST_DURATION = std::chrono::minutes(10);
    auto start_time = std::chrono::steady_clock::now();
    
    while (std::chrono::steady_clock::now() - start_time < TEST_DURATION) {
        LOG_INFO("长时间运行测试 - " + 
                format_timestamp(std::chrono::system_clock::now()));
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // 检查内存泄漏和文件完整性
}
```

---

## 📊 性能基准与优化目标

### 6.1 性能目标

| 指标 | 目标值 | 测量方法 |
|------|--------|----------|
| 单线程吞吐量 | >50,000 msg/s | 连续写入测试 |
| 多线程吞吐量 | >200,000 msg/s | 10线程并发测试 |
| 内存使用 | <10MB | 长时间运行监控 |
| 延迟影响 | <1μs | 同步vs异步对比 |
| 文件写入延迟 | <10ms | 异步队列处理时间 |

### 6.2 与现有系统的集成

#### **与EventLoopManager集成**
```cpp
// 利用现有的事件循环进行异步处理
class Logger {
    EventLoopManager* event_loop_manager_;
    
public:
    void set_event_loop_manager(EventLoopManager* manager) {
        event_loop_manager_ = manager;
    }
    
    void log_async(LogLevel level, const std::string& message) {
        if (event_loop_manager_) {
            // 使用现有的事件循环
            event_loop_manager_->post([this, level, message]() {
                write_log_immediate(level, message);
            });
        } else {
            // 回退到内置异步机制
            log(level, message);
        }
    }
};
```

#### **与TaskScheduler集成**
```cpp
// 定期清理和维护任务
void setup_log_maintenance() {
    auto& scheduler = TaskScheduler::instance();
    
    // 每小时清理旧日志文件
    scheduler.post_periodic_task(
        std::chrono::hours(1),
        TaskPriority::LOW,
        []() {
            Logger::instance().cleanup_old_logs();
        }
    );
    
    // 每分钟刷新缓冲区
    scheduler.post_periodic_task(
        std::chrono::minutes(1),
        TaskPriority::NORMAL,
        []() {
            Logger::instance().flush();
        }
    );
}
```

---

## 🎯 最佳实践建议

### 7.1 日志使用原则

#### **DO's (推荐做法)**
```cpp
// ✅ 使用有意义的消息
LOG_INFO("DHT查询完成: 找到" + std::to_string(peers.size()) + "个peers");

// ✅ 包含关键上下文信息
LOG_ERROR("连接失败: " + endpoint.to_string() + ", 错误: " + ec.message());

// ✅ 使用适当的日志级别
LOG_DEBUG("开始解析磁力链接");  // 调试信息
LOG_INFO("文件下载完成");      // 重要事件
LOG_WARN("重试连接操作");      // 警告但可处理
LOG_ERROR("文件校验失败");     // 错误需要注意

// ✅ 记录性能关键点
auto start = std::chrono::steady_clock::now();
perform_operation();
auto duration = std::chrono::steady_clock::now() - start;
LOG_DEBUG("操作耗时: " + format_duration(duration));
```

#### **DON'Ts (避免做法)**
```cpp
// ❌ 避免无意义的日志
LOG_INFO("函数开始");  // 太泛泛

// ❌ 避免在循环中使用高级别日志
for (const auto& item : large_collection) {
    LOG_INFO("处理: " + item.name);  // 会产生大量日志
}

// ✅ 改为批量记录
LOG_INFO("开始处理 " + std::to_string(large_collection.size()) + " 个项目");

// ❌ 避免记录敏感信息
LOG_INFO("用户密码: " + password);  // 安全风险

// ❌ 避免在性能关键路径使用低级别日志
void high_frequency_function() {
    LOG_TRACE("进入函数");  // 会严重影响性能
    // 性能关键代码
}
```

### 7.2 配置建议

#### **开发环境配置**
```cpp
void setup_development_logging() {
    auto& logger = Logger::instance();
    logger.set_level(LogLevel::DEBUG);
    logger.set_console_output(true);
    logger.set_file_output("./logs/dev.log");
    logger.set_async_mode(false);  // 同步模式便于调试
}
```

#### **生产环境配置**
```cpp
void setup_production_logging() {
    auto& logger = Logger::instance();
    logger.set_level(LogLevel::INFO);
    logger.set_console_output(false);
    logger.set_file_output("/var/log/magnetdownload/app.log");
    logger.set_async_mode(true);   // 异步模式提高性能
    logger.enable_log_rotation(100 * 1024 * 1024);  // 100MB轮转
}
```

---

## 📈 未来扩展计划

### 8.1 高级特性

#### **结构化日志支持**
```cpp
// 支持JSON格式日志
LOG_STRUCTURED({
    {"event", "peer_connected"},
    {"peer_id", peer.id()},
    {"endpoint", peer.endpoint().to_string()},
    {"timestamp", std::chrono::system_clock::now()}
});
```

#### **分布式日志聚合**
```cpp
// 支持发送到日志聚合系统
class NetworkLogSink {
public:
    void send_to_elasticsearch(const LogEntry& entry);
    void send_to_fluentd(const LogEntry& entry);
};
```

#### **智能采样**
```cpp
// 高频日志的智能采样
class AdaptiveSampler {
    std::unordered_map<std::string, RateLimiter> rate_limiters_;
    
public:
    bool should_log(const std::string& message_pattern) {
        auto& limiter = rate_limiters_[message_pattern];
        return limiter.try_acquire();
    }
};
```

### 8.2 集成计划

- **Phase 1**: 基础Logger实现 (当前阶段)
- **Phase 2**: 与现有异步框架深度集成
- **Phase 3**: 添加结构化日志和网络输出
- **Phase 4**: 智能采样和性能优化
- **Phase 5**: 分布式日志和监控集成

---

## 📝 总结

Logger系统是MagnetDownload项目中的关键基础设施，它提供了：

1. **可靠的调试能力**: 支持复杂网络协议的开发和调试
2. **生产级监控**: 为运行时监控和问题诊断提供数据
3. **高性能设计**: 异步处理确保不影响主业务性能
4. **扩展性架构**: 为未来的高级特性预留接口

通过遵循本文档的设计原则和最佳实践，可以构建一个既满足当前需求又具备未来扩展能力的日志系统，为整个磁力下载器项目的成功奠定坚实基础。

---

*文档版本: v1.0*  
*创建日期: 2024-08-22*  
*适用项目: MagnetDownload v1.0*

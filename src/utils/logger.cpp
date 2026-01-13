#include "magnet/utils/logger.h"

#include <iomanip>
#include <filesystem>

namespace magnet::utils {

// ============================================================================
// 单例
// ============================================================================

Logger& Logger::instance() {
    static Logger instance;
    return instance;
}

// ============================================================================
// 构造和析构
// ============================================================================

Logger::Logger() {
    // 启动异步写入线程
    if (async_enabled_.load()) {
        writer_thread_ = std::thread(&Logger::async_writer_thread, this);
    }
}

Logger::~Logger() {
    // 停止异步写入
    shutdown_.store(true);
    queue_cv_.notify_all();
    
    if (writer_thread_.joinable()) {
        writer_thread_.join();
    }
    
    // 刷新剩余日志
    flush();
    
    // 关闭文件
    if (log_file_.is_open()) {
        log_file_.close();
    }
}

// ============================================================================
// 配置方法
// ============================================================================

void Logger::set_level(LogLevel level) {
    min_level_.store(level);
}

void Logger::set_console_output(bool enable) {
    console_enabled_.store(enable);
}

void Logger::set_file_output(const std::string& filename) {
    std::lock_guard<std::mutex> lock(write_mutex_);
    
    if (log_file_.is_open()) {
        log_file_.close();
    }
    
    log_filename_ = filename;
    
    if (!filename.empty()) {
        create_log_directory(filename);
        log_file_.open(filename, std::ios::app);
        file_enable_.store(log_file_.is_open());
        current_file_size_.store(0);
        
        if (log_file_.is_open()) {
            log_file_.seekp(0, std::ios::end);
            current_file_size_.store(static_cast<size_t>(log_file_.tellp()));
        }
    } else {
        file_enable_.store(false);
    }
}

void Logger::set_async_mode(bool enable) {
    if (async_enabled_.load() == enable) {
        return;
    }
    
    if (enable && !async_enabled_.load()) {
        // 启动异步线程
        shutdown_.store(false);
        writer_thread_ = std::thread(&Logger::async_writer_thread, this);
    } else if (!enable && async_enabled_.load()) {
        // 停止异步线程
        shutdown_.store(true);
        queue_cv_.notify_all();
        if (writer_thread_.joinable()) {
            writer_thread_.join();
        }
    }
    
    async_enabled_.store(enable);
}

void Logger::set_max_file_size(size_t max_size) {
    max_file_size_.store(max_size);
}

// ============================================================================
// 日志方法
// ============================================================================

bool Logger::should_log(LogLevel level) const {
    return static_cast<int>(level) >= static_cast<int>(min_level_.load());
}

void Logger::log(LogLevel level, const std::string& message) {
    if (!should_log(level)) {
        return;
    }
    
    LogEntry entry(level, message);
    
    if (async_enabled_.load()) {
        // 异步模式：加入队列
        std::lock_guard<std::mutex> lock(queue_mutex_);
        
        if (log_queue_.size() < MAX_QUEUE_SIZE) {
            log_queue_.push(std::move(entry));
            queue_cv_.notify_one();
            
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            staticstics_.total_message++;
            staticstics_.queue_size = log_queue_.size();
            if (staticstics_.queue_size > staticstics_.max_queue_size) {
                staticstics_.max_queue_size = staticstics_.queue_size;
            }
        } else {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            staticstics_.dropped_message++;
        }
    } else {
        // 同步模式：直接写入
        write_log_entry(entry);
        
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        staticstics_.total_message++;
    }
}

void Logger::flush() {
    if (async_enabled_.load()) {
        // 等待队列清空
        std::unique_lock<std::mutex> lock(queue_mutex_);
        while (!log_queue_.empty() && !shutdown_.load()) {
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            lock.lock();
        }
    }
    
    std::lock_guard<std::mutex> lock(write_mutex_);
    if (log_file_.is_open()) {
        log_file_.flush();
    }
}

Logger::Staticstics Logger::get_statistic() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return staticstics_;
}

// ============================================================================
// 内部方法
// ============================================================================

void Logger::write_log_entry(const LogEntry& entry) {
    std::string formatted = format_message(entry);
    
    std::lock_guard<std::mutex> lock(write_mutex_);
    
    if (console_enabled_.load()) {
        std::cout << formatted << std::flush;
    }
    
    if (file_enable_.load() && log_file_.is_open()) {
        check_log_rotation();
        log_file_ << formatted << std::flush;
        current_file_size_.fetch_add(formatted.size());
    }
}

void Logger::async_writer_thread() {
    std::vector<LogEntry> batch;
    batch.reserve(BATCH_SIZE);
    
    while (!shutdown_.load()) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] {
                return !log_queue_.empty() || shutdown_.load();
            });
            
            // 批量取出
            while (!log_queue_.empty() && batch.size() < BATCH_SIZE) {
                batch.push_back(std::move(log_queue_.front()));
                log_queue_.pop();
            }
            
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            staticstics_.queue_size = log_queue_.size();
        }
        
        // 批量写入
        for (const auto& entry : batch) {
            write_log_entry(entry);
        }
        batch.clear();
    }
    
    // 写入剩余的日志
    std::lock_guard<std::mutex> lock(queue_mutex_);
    while (!log_queue_.empty()) {
        write_log_entry(log_queue_.front());
        log_queue_.pop();
    }
}

std::string Logger::format_message(const LogEntry& entry) const {
    std::ostringstream oss;
    
    // 时间戳
    auto time_t = std::chrono::system_clock::to_time_t(entry.timestamp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        entry.timestamp.time_since_epoch()) % 1000;
    
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time_t);
#else
    localtime_r(&time_t, &tm_buf);
#endif
    
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    
    // 日志级别
    oss << " [" << level_to_string(entry.level) << "]";
    
    // 线程 ID
    oss << " [" << entry.thread_id << "]";
    
    // 消息
    oss << " " << entry.message << "\n";
    
    return oss.str();
}

std::string Logger::level_to_string(LogLevel level) const {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
        default: return "UNKN ";
    }
}

void Logger::check_log_rotation() {
    if (current_file_size_.load() >= max_file_size_.load()) {
        // 关闭当前文件
        log_file_.close();
        
        // 重命名旧文件
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &time_t);
#else
        localtime_r(&time_t, &tm_buf);
#endif
        
        std::ostringstream oss;
        oss << log_filename_ << "." << std::put_time(&tm_buf, "%Y%m%d_%H%M%S");
        
        try {
            std::filesystem::rename(log_filename_, oss.str());
        } catch (...) {
            // 忽略重命名错误
        }
        
        // 打开新文件
        log_file_.open(log_filename_, std::ios::app);
        current_file_size_.store(0);
    }
}

bool Logger::create_log_directory(const std::string& filename) {
    try {
        std::filesystem::path path(filename);
        auto parent = path.parent_path();
        if (!parent.empty() && !std::filesystem::exists(parent)) {
            std::filesystem::create_directories(parent);
        }
        return true;
    } catch (...) {
        return false;
    }
}

std::string Logger::format_string(const std::string& format) {
    return format;
}

} // namespace magnet::utils


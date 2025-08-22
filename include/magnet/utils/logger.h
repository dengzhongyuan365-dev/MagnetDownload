#pragma once
#include <string>
#include <chrono>
#include <thread>
#include <atomic>
#include <iostream>
#include <queue>
#include <mutex>
#include <condition_variable>

namespace magnet::utils {
    enum class LogLevel {
        TRACE = 0,
        DEBUG = 1,
        INFO = 2,
        WARN = 3,
        ERROR = 4,
        FATAL = 5
    };


    class Logger
    {
    public:
        static Logger& instance();

        ~Logger();

        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&)=delete;
        Logger(Logger&&) = delete;
        Logger& operator=(Logger&&) = delete;

        void set_level(LogLevel level);

        void set_console_output(bool enable);

        void set_file_output(const std::string& filename);

        void set_async_mode(bool enable);

        void set_max_file_size(size_t max_size);

        void log(LogLevel level, const std::string& message);

        bool should_log(LogLevel level) const;

        void flush();

        void trace(const std::string& message) { log(LogLevel::TRACE, message);}

        void debug(const std::string& message) { log(LogLevel::DEBUG, message);}

        void info(const std::string& message) { log(LogLevel::INFO, message);}
        void warn(const std::string& message) { log(LogLevel::WARN, message);}
        void error(const std::string& message) { log(LogLevel::ERROR, message);}
        void fatal(const std::string& message) { log(LogLevel::FATAL, message);}

        template<typename... Args>
        void log_format(LogLevel level, const std::string& format, Args&&... args);

        struct  Staticstics
        {
            size_t total_message{0};
            size_t dropped_message{0};
            size_t queue_size  {0};
            size_t max_queue_size {0};
            double avg_processing_time{0};
        };

        Staticstics get_statistic() const;


    private:
        Logger();

        struct LogEntry {
            LogLevel level;
            std::string message;
            std::chrono::system_clock::time_point timestamp;
            std::thread::id thread_id;

            LogEntry() = default;
            LogEntry(LogLevel lvl, std::string msg)
                : level(lvl)
                , message(std::move(msg))
                , timestamp(std::chrono::system_clock::now())
                ,thread_id(std::this_thread::get_id()){}


        };

        // 直接写入日志条目
        void write_log_entry(const LogEntry&entry);
        // 异步写入县城主函数
        void async_writer_thread();
        //
        std::string format_message(const LogEntry& entry) const;

        std::string level_to_string(LogLevel level) const;
        // 检查是否需要轮转日志文件
        void check_log_rotation();

        bool create_log_directory(const std::string& fileName);

        template<typename T>
        void format_impl(std::ostringstream& oss, const std::string& format, size_t& pos, T&& value);

        template<typename T, typename... Args>
        void format_impl(std::ostringstream& oss, const std::string format, size_t pos, T&&value, Args&&...args);

        std::string format_string(const std::string& format);

        template<typename... Args>
        std::string fornat_string(const std::string& format, Args&&... args);

        // config
    private:
        std::atomic<LogLevel> min_level_{LogLevel::INFO};
        std::atomic<bool> console_enabled_{true};
        std::atomic<bool> file_enable_{false};
        std::atomic<bool> async_enabled_{true};
        std::atomic<size_t> max_file_size_{100*1024*1024};

        // file output
        std::string log_filename_;
        std::ofstream log_file_;
        std::atomic<size_t> current_file_size_{0};

        //
        static constexpr size_t MAX_QUEUE_SIZE=10000;
        static constexpr size_t BATCH_SIZE = 100;

        std::queue<LogEntry> log_queue_;
        mutable std::mutex queue_mutex_;
        std::condition_variable queue_cv_;
        std::atomic<bool> shutdown_{false};
        std::thread writer_thread_;


        mutable std::mutex write_mutex_;
        mutable std::mutex stats_mutex_;
        Staticstics staticstics_;


    };

    template<typename... Args>
    void Logger::log_format(LogLevel level, const std::string& format, Args&&... args) {
        if(!should_log(level))
            return;
        std::string formatted = format_string(format, std::forward<Args>(args)...);
        log(level,formatted);
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

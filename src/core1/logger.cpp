#include "logger.h"

namespace bt {

std::shared_ptr<Logger> Logger::createConsoleLogger(Level level) {
    return std::shared_ptr<Logger>(new Logger(true, false, "", level));
}

std::shared_ptr<Logger> Logger::createFileLogger(const std::string& filename, Level level) {
    return std::shared_ptr<Logger>(new Logger(false, true, filename, level));
}

std::shared_ptr<Logger> Logger::createConsoleFileLogger(const std::string& filename, Level level) {
    return std::shared_ptr<Logger>(new Logger(true, true, filename, level));
}

Logger::Logger(bool toConsole, bool toFile, const std::string& filename, Level level)
    : level_(level), toConsole_(toConsole), toFile_(toFile) {
    if (toFile_) {
        fileStream_.open(filename, std::ios::out | std::ios::app);
        if (!fileStream_.is_open()) {
            throw std::runtime_error("Failed to open log file: " + filename);
        }
    }
}

Logger::~Logger() {
    if (fileStream_.is_open()) {
        fileStream_.close();
    }
}

void Logger::setLevel(Level level) {
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
}

void Logger::debug(std::string_view message) {
    log(Level::DEBUG, message);
}

void Logger::info(std::string_view message) {
    log(Level::INFO, message);
}

void Logger::warning(std::string_view message) {
    log(Level::WARNING, message);
}

void Logger::error(std::string_view message) {
    log(Level::ERROR, message);
}

void Logger::critical(std::string_view message) {
    log(Level::CRITICAL, message);
}

void Logger::log(Level level, std::string_view message) {
    // 检查日志级别
    if (level < level_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 构建日志消息
    std::stringstream ss;
    ss << getCurrentTimeString() << " [" << getLevelString(level) << "] " << message;
    std::string formattedMessage = ss.str();
    
    // 输出到控制台
    if (toConsole_) {
        // 根据日志级别设置控制台颜色
        switch (level) {
            case Level::DEBUG:
                std::cout << "\033[90m"; // 深灰色
                break;
            case Level::INFO:
                std::cout << "\033[0m"; // 默认颜色
                break;
            case Level::WARNING:
                std::cout << "\033[33m"; // 黄色
                break;
            case Level::ERROR:
                std::cout << "\033[31m"; // 红色
                break;
            case Level::CRITICAL:
                std::cout << "\033[1;31m"; // 加粗红色
                break;
        }
        
        std::cout << formattedMessage << "\033[0m" << std::endl;
    }
    
    // 输出到文件
    if (toFile_ && fileStream_.is_open()) {
        fileStream_ << formattedMessage << std::endl;
        fileStream_.flush();
    }
}

std::string Logger::getLevelString(Level level) {
    switch (level) {
        case Level::DEBUG:
            return "DEBUG";
        case Level::INFO:
            return "INFO";
        case Level::WARNING:
            return "WARNING";
        case Level::ERROR:
            return "ERROR";
        case Level::CRITICAL:
            return "CRITICAL";
        default:
            return "UNKNOWN";
    }
}

std::string Logger::getCurrentTimeString() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    
    return ss.str();
}

} // namespace bt 
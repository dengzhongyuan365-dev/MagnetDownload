#pragma once

#include "interfaces.h"
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <chrono>
#include <iomanip>
#include <memory>
#include <format>  // C++20格式化库，如果不可用可以使用fmt库或自行实现

namespace bt {

/**
 * @brief 日志系统实现
 */
class Logger : public ILogger {
public:
    /**
     * @brief 创建只输出到控制台的日志器
     * 
     * @param level 日志级别
     * @return 日志器实例
     */
    static std::shared_ptr<Logger> createConsoleLogger(Level level = Level::INFO);
    
    /**
     * @brief 创建输出到文件的日志器
     * 
     * @param filename 日志文件名
     * @param level 日志级别
     * @return 日志器实例
     */
    static std::shared_ptr<Logger> createFileLogger(const std::string& filename, Level level = Level::INFO);
    
    /**
     * @brief 创建同时输出到控制台和文件的日志器
     * 
     * @param filename 日志文件名
     * @param level 日志级别
     * @return 日志器实例
     */
    static std::shared_ptr<Logger> createConsoleFileLogger(const std::string& filename, Level level = Level::INFO);
    
    /**
     * @brief 析构函数
     */
    ~Logger() override;
    
    /**
     * @brief 设置日志级别
     * 
     * @param level 日志级别
     */
    void setLevel(Level level) override;
    
    /**
     * @brief 记录调试日志
     * 
     * @param message 日志消息
     */
    void debug(std::string_view message) override;
    
    /**
     * @brief 记录信息日志
     * 
     * @param message 日志消息
     */
    void info(std::string_view message) override;
    
    /**
     * @brief 记录警告日志
     * 
     * @param message 日志消息
     */
    void warning(std::string_view message) override;
    
    /**
     * @brief 记录错误日志
     * 
     * @param message 日志消息
     */
    void error(std::string_view message) override;
    
    /**
     * @brief 记录严重错误日志
     * 
     * @param message 日志消息
     */
    void critical(std::string_view message) override;
    
private:
    /**
     * @brief 构造函数
     * 
     * @param toConsole 是否输出到控制台
     * @param toFile 是否输出到文件
     * @param filename 日志文件名
     * @param level 日志级别
     */
    Logger(bool toConsole, bool toFile, const std::string& filename, Level level);
    
    /**
     * @brief 记录日志
     * 
     * @param level 日志级别
     * @param message 日志消息
     */
    void log(Level level, std::string_view message);
    
    /**
     * @brief 获取日志级别字符串
     * 
     * @param level 日志级别
     * @return 级别字符串
     */
    static std::string getLevelString(Level level);
    
    /**
     * @brief 获取当前时间字符串
     * 
     * @return 时间字符串
     */
    static std::string getCurrentTimeString();

private:
    Level level_;              // 日志级别
    bool toConsole_;           // 是否输出到控制台
    bool toFile_;              // 是否输出到文件
    std::ofstream fileStream_; // 日志文件流
    std::mutex mutex_;         // 互斥锁保护并发写入
};

// 模板函数实现
template<typename... Args>
void ILogger::debugf(std::string_view format, Args&&... args) {
    try {
        // 使用C++20的std::format或类似库格式化字符串
        std::string message = std::format(format, std::forward<Args>(args)...);
        debug(message);
    } catch (const std::exception& e) {
        // 格式化失败，记录原始格式和错误
        std::string errorMsg = "Format error: ";
        errorMsg += e.what();
        error(errorMsg);
    }
}

template<typename... Args>
void ILogger::infof(std::string_view format, Args&&... args) {
    try {
        std::string message = std::format(format, std::forward<Args>(args)...);
        info(message);
    } catch (const std::exception& e) {
        std::string errorMsg = "Format error: ";
        errorMsg += e.what();
        error(errorMsg);
    }
}

template<typename... Args>
void ILogger::warningf(std::string_view format, Args&&... args) {
    try {
        std::string message = std::format(format, std::forward<Args>(args)...);
        warning(message);
    } catch (const std::exception& e) {
        std::string errorMsg = "Format error: ";
        errorMsg += e.what();
        error(errorMsg);
    }
}

template<typename... Args>
void ILogger::errorf(std::string_view format, Args&&... args) {
    try {
        std::string message = std::format(format, std::forward<Args>(args)...);
        error(message);
    } catch (const std::exception& e) {
        std::string errorMsg = "Format error: ";
        errorMsg += e.what();
        error(errorMsg);
    }
}

template<typename... Args>
void ILogger::criticalf(std::string_view format, Args&&... args) {
    try {
        std::string message = std::format(format, std::forward<Args>(args)...);
        critical(message);
    } catch (const std::exception& e) {
        std::string errorMsg = "Format error: ";
        errorMsg += e.what();
        error(errorMsg);
    }
}

} // namespace bt 
#pragma once

#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <mutex>
#include <cstdint>
#include <array>
#include <memory>

namespace magnet::storage {

// ============================================================================
// 文件条目
// ============================================================================

/**
 * @struct FileEntry
 * @brief 单个文件信息
 */
struct FileEntry {
    std::string path;           // 相对路径
    size_t size{0};             // 文件大小
    size_t offset{0};           // 在总数据中的起始偏移
    
    FileEntry() = default;
    FileEntry(const std::string& p, size_t s, size_t o = 0)
        : path(p), size(s), offset(o) {}
};

// ============================================================================
// 存储配置
// ============================================================================

/**
 * @struct StorageConfig
 * @brief 存储配置
 */
struct StorageConfig {
    std::string base_path;              // 保存目录
    size_t piece_length{0};             // 分片大小
    size_t total_size{0};               // 总大小
    std::vector<FileEntry> files;       // 文件列表
    std::vector<std::array<uint8_t, 20>> piece_hashes; // 分片哈希
    
    bool preallocate{true};             // 是否预分配空间
    size_t write_buffer_size{1024*1024};// 写缓冲大小 (1MB)
    
    /**
     * @brief 获取分片数量
     */
    size_t pieceCount() const {
        if (piece_length == 0) return 0;
        return (total_size + piece_length - 1) / piece_length;
    }
    
    /**
     * @brief 获取指定分片的大小
     */
    size_t getPieceSize(size_t index) const {
        if (piece_length == 0) return 0;
        size_t count = pieceCount();
        if (index >= count) return 0;
        
        // 最后一个分片可能较小
        if (index == count - 1) {
            size_t remainder = total_size % piece_length;
            return remainder > 0 ? remainder : piece_length;
        }
        return piece_length;
    }
    
    /**
     * @brief 检查配置是否有效
     */
    bool isValid() const {
        return !base_path.empty() && piece_length > 0 && total_size > 0;
    }
};

// ============================================================================
// FileManager 类
// ============================================================================

/**
 * @class FileManager
 * @brief 文件管理器
 * 
 * 负责底层文件操作：
 * - 创建目录和文件
 * - 读写指定偏移的数据
 * - 预分配磁盘空间
 * - 管理多个文件
 * 
 * 使用示例：
 * @code
 * StorageConfig config;
 * config.base_path = "/downloads/MyTorrent";
 * config.piece_length = 262144;
 * config.total_size = 1024 * 1024 * 100;
 * config.files = {{"file1.txt", 100 * 1024 * 1024, 0}};
 * 
 * FileManager fm(config);
 * fm.initialize();
 * 
 * fm.write(0, data);
 * auto read_data = fm.read(0, 1024);
 * @endcode
 */
class FileManager {
public:
    // ========================================================================
    // 构造和析构
    // ========================================================================
    
    /**
     * @brief 构造函数
     * @param config 存储配置
     */
    explicit FileManager(const StorageConfig& config);
    
    /**
     * @brief 析构函数
     */
    ~FileManager();
    
    // 禁止拷贝
    FileManager(const FileManager&) = delete;
    FileManager& operator=(const FileManager&) = delete;
    
    // ========================================================================
    // 初始化
    // ========================================================================
    
    /**
     * @brief 初始化存储
     * 
     * 执行操作：
     * - 创建目录结构
     * - 创建所有文件
     * - 如果配置了预分配，预留磁盘空间
     * 
     * @return true 如果成功
     */
    bool initialize();
    
    /**
     * @brief 检查是否已初始化
     */
    bool isInitialized() const { return initialized_; }
    
    // ========================================================================
    // 读写操作
    // ========================================================================
    
    /**
     * @brief 读取数据
     * @param offset 全局偏移（从所有文件的起始位置算起）
     * @param length 读取长度
     * @return 读取的数据，失败返回空向量
     */
    std::vector<uint8_t> read(size_t offset, size_t length);
    
    /**
     * @brief 写入数据
     * @param offset 全局偏移
     * @param data 要写入的数据
     * @return true 如果成功
     */
    bool write(size_t offset, const std::vector<uint8_t>& data);
    
    /**
     * @brief 刷新所有文件缓冲
     */
    void flush();
    
    /**
     * @brief 关闭所有文件
     */
    void close();
    
    // ========================================================================
    // 查询
    // ========================================================================
    
    /**
     * @brief 检查文件是否存在
     */
    bool exists() const;
    
    /**
     * @brief 获取总大小
     */
    size_t getTotalSize() const { return config_.total_size; }
    
    /**
     * @brief 获取文件数量
     */
    size_t getFileCount() const { return config_.files.size(); }
    
    /**
     * @brief 获取文件列表
     */
    const std::vector<FileEntry>& getFiles() const { return config_.files; }
    
    /**
     * @brief 获取基础路径
     */
    const std::string& getBasePath() const { return config_.base_path; }

private:
    // ========================================================================
    // 内部方法
    // ========================================================================
    
    /**
     * @brief 创建目录结构
     */
    bool createDirectories();
    
    /**
     * @brief 创建单个文件
     */
    bool createFile(const FileEntry& file);
    
    /**
     * @brief 预分配文件空间
     */
    bool preallocateFile(const std::string& path, size_t size);
    
    /**
     * @brief 打开文件
     */
    std::fstream* openFile(const std::string& path);
    
    /**
     * @brief 获取包含指定偏移的文件
     */
    const FileEntry* getFileForOffset(size_t offset) const;
    
    /**
     * @brief 获取完整文件路径
     */
    std::string getFullPath(const std::string& relative_path) const;

private:
    StorageConfig config_;
    bool initialized_{false};
    
    mutable std::mutex mutex_;
    std::map<std::string, std::unique_ptr<std::fstream>> open_files_;
};

} // namespace magnet::storage


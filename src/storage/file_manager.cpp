#include "magnet/storage/file_manager.h"
#include "magnet/utils/logger.h"

#include <filesystem>
#include <algorithm>

namespace magnet::storage {

namespace fs = std::filesystem;

// 日志宏
#define LOG_DEBUG(msg) magnet::utils::Logger::instance().debug(msg)
#define LOG_INFO(msg) magnet::utils::Logger::instance().info(msg)
#define LOG_WARNING(msg) magnet::utils::Logger::instance().warn(msg)
#define LOG_ERROR(msg) magnet::utils::Logger::instance().error(msg)

// ============================================================================
// 构造和析构
// ============================================================================

FileManager::FileManager(const StorageConfig& config)
    : config_(config)
{
    LOG_DEBUG("FileManager created: " + config.base_path);
}

FileManager::~FileManager() {
    close();
    LOG_DEBUG("FileManager destroyed");
}

// ============================================================================
// 初始化
// ============================================================================

bool FileManager::initialize() {
    if (initialized_) {
        return true;
    }
    
    if (!config_.isValid()) {
        LOG_ERROR("Invalid storage config");
        return false;
    }
    
    LOG_INFO("Initializing storage at: " + config_.base_path);
    
    // 创建目录结构
    if (!createDirectories()) {
        LOG_ERROR("Failed to create directories");
        return false;
    }
    
    // 创建所有文件
    for (const auto& file : config_.files) {
        if (!createFile(file)) {
            LOG_ERROR("Failed to create file: " + file.path);
            return false;
        }
    }
    
    initialized_ = true;
    LOG_INFO("Storage initialized successfully, " + 
             std::to_string(config_.files.size()) + " files");
    
    return true;
}

// ============================================================================
// 读写操作
// ============================================================================

std::vector<uint8_t> FileManager::read(size_t offset, size_t length) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        LOG_ERROR("FileManager not initialized");
        return {};
    }
    
    if (offset + length > config_.total_size) {
        LOG_ERROR("Read out of bounds: offset=" + std::to_string(offset) +
                  " length=" + std::to_string(length));
        return {};
    }
    
    std::vector<uint8_t> result;
    result.reserve(length);
    
    size_t remaining = length;
    size_t current_offset = offset;
    
    while (remaining > 0) {
        // 找到包含当前偏移的文件
        const FileEntry* file = getFileForOffset(current_offset);
        if (!file) {
            LOG_ERROR("No file found for offset: " + std::to_string(current_offset));
            return {};
        }
        
        // 计算文件内偏移
        size_t file_offset = current_offset - file->offset;
        
        // 计算可以从该文件读取的长度
        size_t can_read = std::min(remaining, file->size - file_offset);
        
        // 打开文件
        std::fstream* fs = openFile(file->path);
        if (!fs || !fs->is_open()) {
            LOG_ERROR("Failed to open file: " + file->path);
            return {};
        }
        
        // 读取
        fs->seekg(static_cast<std::streamoff>(file_offset));
        if (!fs->good()) {
            LOG_ERROR("Failed to seek in file: " + file->path);
            return {};
        }
        
        size_t old_size = result.size();
        result.resize(old_size + can_read);
        fs->read(reinterpret_cast<char*>(result.data() + old_size), 
                 static_cast<std::streamsize>(can_read));
        
        if (fs->fail() && !fs->eof()) {
            LOG_ERROR("Failed to read from file: " + file->path);
            return {};
        }
        
        current_offset += can_read;
        remaining -= can_read;
    }
    
    return result;
}

bool FileManager::write(size_t offset, const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        LOG_ERROR("FileManager not initialized");
        return false;
    }
    
    if (data.empty()) {
        return true;
    }
    
    if (offset + data.size() > config_.total_size) {
        LOG_ERROR("Write out of bounds: offset=" + std::to_string(offset) +
                  " size=" + std::to_string(data.size()));
        return false;
    }
    
    size_t remaining = data.size();
    size_t current_offset = offset;
    size_t data_offset = 0;
    
    while (remaining > 0) {
        // 找到包含当前偏移的文件
        const FileEntry* file = getFileForOffset(current_offset);
        if (!file) {
            LOG_ERROR("No file found for offset: " + std::to_string(current_offset));
            return false;
        }
        
        // 计算文件内偏移
        size_t file_offset = current_offset - file->offset;
        
        // 计算可以写入该文件的长度
        size_t can_write = std::min(remaining, file->size - file_offset);
        
        // 打开文件
        std::fstream* fs = openFile(file->path);
        if (!fs || !fs->is_open()) {
            LOG_ERROR("Failed to open file for writing: " + file->path);
            return false;
        }
        
        // 写入
        fs->seekp(static_cast<std::streamoff>(file_offset));
        if (!fs->good()) {
            LOG_ERROR("Failed to seek for writing: " + file->path);
            return false;
        }
        
        fs->write(reinterpret_cast<const char*>(data.data() + data_offset),
                  static_cast<std::streamsize>(can_write));
        
        if (fs->fail()) {
            LOG_ERROR("Failed to write to file: " + file->path);
            return false;
        }
        
        current_offset += can_write;
        data_offset += can_write;
        remaining -= can_write;
    }
    
    return true;
}

void FileManager::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& [path, fs] : open_files_) {
        if (fs && fs->is_open()) {
            fs->flush();
        }
    }
}

void FileManager::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& [path, fs] : open_files_) {
        if (fs && fs->is_open()) {
            fs->close();
        }
    }
    open_files_.clear();
    
    LOG_DEBUG("All files closed");
}

// ============================================================================
// 查询
// ============================================================================

bool FileManager::exists() const {
    if (config_.files.empty()) {
        return false;
    }
    
    // 检查第一个文件是否存在
    std::string path = getFullPath(config_.files[0].path);
    return fs::exists(path);
}

// ============================================================================
// 内部方法
// ============================================================================

bool FileManager::createDirectories() {
    try {
        // 创建基础目录
        if (!fs::exists(config_.base_path)) {
            fs::create_directories(config_.base_path);
            LOG_DEBUG("Created base directory: " + config_.base_path);
        }
        
        // 创建文件所需的子目录
        for (const auto& file : config_.files) {
            std::string full_path = getFullPath(file.path);
            fs::path parent = fs::path(full_path).parent_path();
            
            if (!parent.empty() && !fs::exists(parent)) {
                fs::create_directories(parent);
                LOG_DEBUG("Created directory: " + parent.string());
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create directories: " + std::string(e.what()));
        return false;
    }
}

bool FileManager::createFile(const FileEntry& file) {
    std::string full_path = getFullPath(file.path);
    
    try {
        // 检查文件是否已存在
        if (fs::exists(full_path)) {
            size_t existing_size = fs::file_size(full_path);
            if (existing_size == file.size) {
                LOG_DEBUG("File already exists with correct size: " + file.path);
                return true;
            }
            // 文件存在但大小不对，需要重新创建
            LOG_DEBUG("File exists but size mismatch, recreating: " + file.path);
        }
        
        // 创建文件
        std::ofstream ofs(full_path, std::ios::binary | std::ios::out);
        if (!ofs.is_open()) {
            LOG_ERROR("Failed to create file: " + full_path);
            return false;
        }
        ofs.close();
        
        // 预分配空间
        if (config_.preallocate && file.size > 0) {
            if (!preallocateFile(full_path, file.size)) {
                LOG_WARNING("Failed to preallocate file: " + file.path);
                // 不视为致命错误
            }
        }
        
        LOG_DEBUG("Created file: " + file.path + " (" + std::to_string(file.size) + " bytes)");
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create file " + file.path + ": " + e.what());
        return false;
    }
}

bool FileManager::preallocateFile(const std::string& path, size_t size) {
    try {
        // 使用 resize_file 预分配空间
        fs::resize_file(path, size);
        return true;
    } catch (const std::exception& e) {
        LOG_WARNING("preallocate failed: " + std::string(e.what()));
        return false;
    }
}

std::fstream* FileManager::openFile(const std::string& relative_path) {
    // 检查是否已打开
    auto it = open_files_.find(relative_path);
    if (it != open_files_.end() && it->second && it->second->is_open()) {
        return it->second.get();
    }
    
    // 打开文件
    std::string full_path = getFullPath(relative_path);
    auto fs = std::make_unique<std::fstream>(
        full_path, 
        std::ios::binary | std::ios::in | std::ios::out);
    
    if (!fs->is_open()) {
        LOG_ERROR("Failed to open file: " + full_path);
        return nullptr;
    }
    
    std::fstream* ptr = fs.get();
    open_files_[relative_path] = std::move(fs);
    
    return ptr;
}

const FileEntry* FileManager::getFileForOffset(size_t offset) const {
    for (const auto& file : config_.files) {
        if (offset >= file.offset && offset < file.offset + file.size) {
            return &file;
        }
    }
    return nullptr;
}

std::string FileManager::getFullPath(const std::string& relative_path) const {
    fs::path base(config_.base_path);
    fs::path rel(relative_path);
    return (base / rel).string();
}

} // namespace magnet::storage

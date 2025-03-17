#include "file_manager.h"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <openssl/sha.h>

namespace fs = std::filesystem;

namespace bt {

FileManager::FileManager(const std::string& save_path)
    : save_path_(save_path), total_length_(0), piece_length_(0) {
    
    // 确保保存路径存在
    createDirectories(save_path_);
}

FileManager::~FileManager() {
    // 保存文件
    saveFiles();
}

bool FileManager::init(const std::string& name, uint64_t total_length, uint32_t piece_length,
                      const std::vector<std::string>& piece_hashes,
                      const std::vector<FileInfo>& files) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    name_ = name;
    total_length_ = total_length;
    piece_length_ = piece_length;
    
    // 初始化片段列表
    uint32_t piece_count = (total_length_ + piece_length_ - 1) / piece_length_;
    pieces_.clear();
    
    for (uint32_t i = 0; i < piece_count; ++i) {
        uint32_t length = (i == piece_count - 1)
            ? (total_length_ - i * piece_length_) // 最后一个片段可能不足piece_length_
            : piece_length_;
        
        if (i < piece_hashes.size()) {
            pieces_.emplace_back(i, length, piece_hashes[i]);
        } else {
            // 如果没有提供哈希值，使用空字符串
            pieces_.emplace_back(i, length, "");
        }
    }
    
    // 初始化文件列表
    files_ = files;
    
    // 如果没有提供文件列表，则创建一个单文件
    if (files_.empty()) {
        FileInfo file;
        file.path = name_;
        file.length = total_length_;
        file.offset = 0;
        files_.push_back(file);
    }
    
    return true;
}

std::vector<Block> FileManager::getBlocksForPiece(uint32_t piece_index, uint32_t block_size) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (piece_index >= pieces_.size()) {
        return {};
    }
    
    const Piece& piece = pieces_[piece_index];
    std::vector<Block> blocks;
    
    uint32_t remaining = piece.length;
    uint32_t offset = 0;
    
    while (remaining > 0) {
        uint32_t length = std::min(remaining, block_size);
        blocks.emplace_back(piece_index, offset, length);
        offset += length;
        remaining -= length;
    }
    
    return blocks;
}

bool FileManager::saveBlock(const Block& block, const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (block.piece_index >= pieces_.size()) {
        return false;
    }
    
    Piece& piece = pieces_[block.piece_index];
    
    if (block.offset + block.length > piece.length) {
        return false;
    }
    
    // 获取片段数据
    std::vector<uint8_t>& piece_data = getPieceData(block.piece_index);
    
    // 确保piece_data大小足够
    if (piece_data.size() < piece.length) {
        piece_data.resize(piece.length);
    }
    
    // 复制数据
    std::copy(data.begin(), data.end(), piece_data.begin() + block.offset);
    
    return true;
}

bool FileManager::verifyPiece(uint32_t piece_index) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (piece_index >= pieces_.size()) {
        return false;
    }
    
    Piece& piece = pieces_[piece_index];
    
    // 如果没有哈希值，无法验证
    if (piece.hash.empty()) {
        return true;
    }
    
    // 获取片段数据
    const std::vector<uint8_t>& piece_data = getPieceData(piece_index);
    
    // 验证大小
    if (piece_data.size() < piece.length) {
        return false;
    }
    
    // 计算哈希
    std::string hash = calculateSHA1(std::vector<uint8_t>(piece_data.begin(), 
                                                         piece_data.begin() + piece.length));
    
    // 验证哈希
    if (hash == piece.hash) {
        piece.downloaded = true;
        return true;
    }
    
    return false;
}

std::vector<uint8_t> FileManager::readPiece(uint32_t piece_index) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (piece_index >= pieces_.size()) {
        return {};
    }
    
    const Piece& piece = pieces_[piece_index];
    
    // 获取片段数据
    const std::vector<uint8_t>& piece_data = getPieceData(piece_index);
    
    if (piece_data.size() < piece.length) {
        return {};
    }
    
    return std::vector<uint8_t>(piece_data.begin(), piece_data.begin() + piece.length);
}

bool FileManager::isComplete() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (const auto& piece : pieces_) {
        if (!piece.downloaded) {
            return false;
        }
    }
    
    return true;
}

uint64_t FileManager::getDownloadedBytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    uint64_t downloaded = 0;
    
    for (const auto& piece : pieces_) {
        if (piece.downloaded) {
            downloaded += piece.length;
        }
    }
    
    return downloaded;
}

uint64_t FileManager::getRemainingBytes() const {
    return total_length_ - getDownloadedBytes();
}

bool FileManager::saveFiles() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 创建所有目录
    for (const auto& file : files_) {
        fs::path path = fs::path(save_path_) / file.path;
        createDirectories(path.parent_path().string());
    }
    
    // 为每个文件创建输出流
    std::vector<std::ofstream> output_streams;
    for (const auto& file : files_) {
        fs::path path = fs::path(save_path_) / file.path;
        output_streams.emplace_back(path.string(), std::ios::binary);
        
        if (!output_streams.back()) {
            std::cerr << "Failed to open file: " << path.string() << std::endl;
            return false;
        }
    }
    
    // 写入每个片段的数据
    for (size_t i = 0; i < pieces_.size(); ++i) {
        if (!pieces_[i].downloaded) {
            continue;
        }
        
        const std::vector<uint8_t>& piece_data = getPieceData(i);
        uint64_t piece_offset = i * piece_length_;
        
        // 处理每个文件
        for (size_t j = 0; j < files_.size(); ++j) {
            const FileInfo& file = files_[j];
            
            // 检查这个片段是否属于这个文件
            if (piece_offset + piece_data.size() <= file.offset || 
                piece_offset >= file.offset + file.length) {
                continue;
            }
            
            // 计算文件内的偏移
            uint64_t file_offset = (piece_offset > file.offset) 
                ? 0 
                : (file.offset - piece_offset);
                
            // 计算文件内的长度
            uint64_t file_length = std::min(
                piece_data.size() - file_offset,
                file.offset + file.length - (piece_offset + file_offset)
            );
            
            // 设置文件偏移
            output_streams[j].seekp(piece_offset + file_offset - file.offset);
            
            // 写入数据
            output_streams[j].write(reinterpret_cast<const char*>(piece_data.data() + file_offset), 
                                   file_length);
            
            if (!output_streams[j]) {
                std::cerr << "Failed to write to file: " << file.path << std::endl;
                return false;
            }
        }
    }
    
    // 关闭所有文件
    for (auto& stream : output_streams) {
        stream.close();
    }
    
    return true;
}

bool FileManager::createDirectories(const std::string& path) {
    try {
        if (!fs::exists(path)) {
            fs::create_directories(path);
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to create directory: " << path << " (" << e.what() << ")" << std::endl;
        return false;
    }
}

std::vector<uint8_t>& FileManager::getPieceData(uint32_t piece_index) {
    // 如果片段数据不存在，创建它
    if (piece_data_.find(piece_index) == piece_data_.end()) {
        piece_data_[piece_index] = std::vector<uint8_t>();
    }
    
    return piece_data_[piece_index];
}

std::string FileManager::calculateSHA1(const std::vector<uint8_t>& data) {
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(data.data(), data.size(), hash);
    
    std::string result;
    result.reserve(SHA_DIGEST_LENGTH);
    
    for (int i = 0; i < SHA_DIGEST_LENGTH; ++i) {
        result.push_back(hash[i]);
    }
    
    return result;
}

} // namespace bt 
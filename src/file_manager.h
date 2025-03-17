#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <fstream>
#include <mutex>
#include <cstdint>

namespace bt {

// 表示一个文件片段 (Piece)
struct Piece {
    uint32_t index;       // 片段索引
    uint32_t length;      // 片段长度
    std::string hash;     // 片段哈希值 (SHA-1)
    bool downloaded;      // 是否已下载
    
    Piece(uint32_t idx, uint32_t len, const std::string& h)
        : index(idx), length(len), hash(h), downloaded(false) {}
};

// 表示一个文件块 (Block)，是片段的一部分
struct Block {
    uint32_t piece_index;  // 所属片段索引
    uint32_t offset;       // 在片段中的偏移
    uint32_t length;       // 块长度
    bool requested;        // 是否已请求
    bool downloaded;       // 是否已下载
    
    Block(uint32_t pidx, uint32_t off, uint32_t len)
        : piece_index(pidx), offset(off), length(len), 
          requested(false), downloaded(false) {}
};

// 文件信息
struct FileInfo {
    std::string path;     // 文件路径
    uint64_t length;      // 文件长度
    uint64_t offset;      // 在整个下载内容中的偏移
};

class FileManager {
public:
    FileManager(const std::string& save_path);
    ~FileManager();
    
    // 初始化下载任务
    bool init(const std::string& name, uint64_t total_length, uint32_t piece_length,
              const std::vector<std::string>& piece_hashes,
              const std::vector<FileInfo>& files = {});
    
    // 获取总长度
    uint64_t getTotalLength() const { return total_length_; }
    
    // 获取片段长度
    uint32_t getPieceLength() const { return piece_length_; }
    
    // 获取片段数量
    uint32_t getPieceCount() const { return pieces_.size(); }
    
    // 获取所有片段信息
    const std::vector<Piece>& getPieces() const { return pieces_; }
    
    // 获取一个片段的Blocks
    std::vector<Block> getBlocksForPiece(uint32_t piece_index, uint32_t block_size = 16384);
    
    // 保存一个块数据
    bool saveBlock(const Block& block, const std::vector<uint8_t>& data);
    
    // 验证一个片段
    bool verifyPiece(uint32_t piece_index);
    
    // 获取一个片段数据
    std::vector<uint8_t> readPiece(uint32_t piece_index);
    
    // 检查下载是否完成
    bool isComplete() const;
    
    // 获取已下载的字节数
    uint64_t getDownloadedBytes() const;
    
    // 获取剩余的字节数
    uint64_t getRemainingBytes() const;
    
    // 保存已下载的数据
    bool saveFiles();
    
private:
    std::string save_path_;                  // 保存路径
    std::string name_;                       // 下载内容名称
    uint64_t total_length_;                  // 总长度
    uint32_t piece_length_;                  // 片段长度
    std::vector<Piece> pieces_;              // 片段列表
    std::vector<FileInfo> files_;            // 文件列表
    std::map<uint32_t, std::vector<uint8_t>> piece_data_; // 片段数据缓存
    std::mutex mutex_;                       // 互斥锁
    
    // 创建必要的目录
    bool createDirectories(const std::string& path);
    
    // 获取片段数据缓存
    std::vector<uint8_t>& getPieceData(uint32_t piece_index);
    
    // 计算SHA-1哈希
    std::string calculateSHA1(const std::vector<uint8_t>& data);
};

} // namespace bt 
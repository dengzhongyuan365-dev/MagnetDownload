#pragma once

#include "file_manager.h"

#include <vector>
#include <mutex>
#include <atomic>
#include <cstdint>

namespace magnet::storage {

// ============================================================================
// 分片状态
// ============================================================================

enum class PieceStatus {
    Missing,        // 未下载
    Partial,        // 部分下载
    Complete,       // 下载完成（未验证）
    Verified,       // 已验证
    Failed          // 验证失败
};

/**
 * @brief 状态转字符串
 */
inline const char* pieceStatusToString(PieceStatus status) {
    switch (status) {
        case PieceStatus::Missing: return "Missing";
        case PieceStatus::Partial: return "Partial";
        case PieceStatus::Complete: return "Complete";
        case PieceStatus::Verified: return "Verified";
        case PieceStatus::Failed: return "Failed";
        default: return "Unknown";
    }
}

// ============================================================================
// 分片信息
// ============================================================================

/**
 * @struct PieceState
 * @brief 单个分片的状态
 */
struct PieceState {
    PieceStatus status{PieceStatus::Missing};
    size_t downloaded{0};           // 已下载字节数
    std::vector<bool> blocks;       // 块下载状态
    
    /**
     * @brief 检查分片是否完整
     */
    bool isComplete(size_t piece_size) const {
        return downloaded >= piece_size;
    }
    
    /**
     * @brief 获取下载进度
     */
    double progress(size_t piece_size) const {
        return piece_size > 0 ? static_cast<double>(downloaded) / piece_size : 0;
    }
};

// ============================================================================
// PieceManager 类
// ============================================================================

/**
 * @class PieceManager
 * @brief 分片管理器
 * 
 * 管理分片的下载状态和数据：
 * - 跟踪每个分片的下载进度
 * - 管理块（Block）级别的状态
 * - 验证分片的 SHA1 哈希
 * - 提供位图（Bitfield）
 * 
 * 使用示例：
 * @code
 * StorageConfig config = {...};
 * FileManager file_manager(config);
 * file_manager.initialize();
 * 
 * PieceManager piece_manager(file_manager, config);
 * piece_manager.initialize();
 * 
 * // 写入数据块
 * piece_manager.writeBlock(0, 0, block_data);
 * 
 * // 检查分片状态
 * if (piece_manager.getPieceStatus(0) == PieceStatus::Verified) {
 *     // 分片已完成并验证
 * }
 * @endcode
 */
class PieceManager {
public:
    // 标准块大小
    static constexpr size_t kBlockSize = 16384;  // 16KB
    
    // ========================================================================
    // 构造和析构
    // ========================================================================
    
    /**
     * @brief 构造函数
     * @param file_manager 文件管理器引用
     * @param config 存储配置
     */
    PieceManager(FileManager& file_manager, const StorageConfig& config);
    
    /**
     * @brief 析构函数
     */
    ~PieceManager();
    
    // 禁止拷贝
    PieceManager(const PieceManager&) = delete;
    PieceManager& operator=(const PieceManager&) = delete;
    
    // ========================================================================
    // 初始化
    // ========================================================================
    
    /**
     * @brief 初始化分片管理器
     * 
     * 执行操作：
     * - 初始化分片状态
     * - 扫描已有数据（如果存在）
     * 
     * @return true 如果成功
     */
    bool initialize();
    
    /**
     * @brief 从现有数据恢复状态
     * 
     * 扫描并验证已下载的分片
     * @return 已验证的分片数
     */
    size_t recoverFromExisting();
    
    // ========================================================================
    // 写入操作
    // ========================================================================
    
    /**
     * @brief 写入完整分片
     * @param index 分片索引
     * @param data 分片数据
     * @return true 如果写入成功
     */
    bool writePiece(uint32_t index, const std::vector<uint8_t>& data);
    
    /**
     * @brief 写入数据块
     * @param piece_index 分片索引
     * @param block_offset 块在分片内的偏移
     * @param data 块数据
     * @return true 如果写入成功，false 如果失败或分片验证失败
     */
    bool writeBlock(uint32_t piece_index, uint32_t block_offset, 
                    const std::vector<uint8_t>& data);
    
    // ========================================================================
    // 读取操作
    // ========================================================================
    
    /**
     * @brief 读取完整分片
     * @param index 分片索引
     * @return 分片数据，失败返回空向量
     */
    std::vector<uint8_t> readPiece(uint32_t index);
    
    /**
     * @brief 读取数据块
     * @param piece_index 分片索引
     * @param block_offset 块偏移
     * @param length 读取长度
     * @return 块数据
     */
    std::vector<uint8_t> readBlock(uint32_t piece_index, uint32_t block_offset, 
                                    size_t length);
    
    // ========================================================================
    // 验证
    // ========================================================================
    
    /**
     * @brief 验证单个分片
     * @param index 分片索引
     * @return true 如果验证通过
     */
    bool verifyPiece(uint32_t index);
    
    /**
     * @brief 验证所有分片
     * @return 验证通过的分片数
     */
    size_t verifyAll();
    
    // ========================================================================
    // 状态查询
    // ========================================================================
    
    /**
     * @brief 获取分片状态
     */
    PieceStatus getPieceStatus(uint32_t index) const;
    
    /**
     * @brief 获取分片详细状态
     */
    PieceState getPieceState(uint32_t index) const;
    
    /**
     * @brief 获取已完成的分片列表
     */
    std::vector<uint32_t> getCompletedPieces() const;
    
    /**
     * @brief 获取缺失的分片列表
     */
    std::vector<uint32_t> getMissingPieces() const;
    
    /**
     * @brief 获取位图
     */
    std::vector<bool> getBitfield() const;
    
    /**
     * @brief 检查分片是否完成
     */
    bool isPieceComplete(uint32_t index) const;
    
    /**
     * @brief 检查分片是否已验证
     */
    bool isPieceVerified(uint32_t index) const;
    
    // ========================================================================
    // 进度统计
    // ========================================================================
    
    /**
     * @brief 获取已完成分片数
     */
    size_t getCompletedCount() const;
    
    /**
     * @brief 获取已验证分片数
     */
    size_t getVerifiedCount() const;
    
    /**
     * @brief 获取总分片数
     */
    size_t getTotalCount() const { return piece_count_; }
    
    /**
     * @brief 获取下载进度 (0.0 - 1.0)
     */
    double getProgress() const;
    
    /**
     * @brief 获取已下载字节数
     */
    size_t getDownloadedBytes() const;
    
    /**
     * @brief 获取分片大小
     */
    size_t getPieceSize(uint32_t index) const;
    
    /**
     * @brief 获取分片的块数
     */
    size_t getBlockCount(uint32_t index) const;

private:
    // ========================================================================
    // 内部方法
    // ========================================================================
    
    /**
     * @brief 计算分片的全局偏移
     */
    size_t getPieceOffset(uint32_t index) const;
    
    /**
     * @brief 计算块索引
     */
    size_t getBlockIndex(uint32_t offset) const;
    
    /**
     * @brief 检查分片所有块是否完成
     */
    bool areAllBlocksComplete(uint32_t index) const;
    
    /**
     * @brief 更新分片状态
     */
    void updatePieceStatus(uint32_t index);
    
    /**
     * @brief 重置分片状态（验证失败时）
     */
    void resetPiece(uint32_t index);

private:
    FileManager& file_manager_;
    StorageConfig config_;
    
    size_t piece_count_{0};
    size_t piece_length_{0};
    
    mutable std::mutex mutex_;
    std::vector<PieceState> pieces_;
    
    // 统计
    std::atomic<size_t> completed_count_{0};
    std::atomic<size_t> verified_count_{0};
    std::atomic<size_t> downloaded_bytes_{0};
};

} // namespace magnet::storage


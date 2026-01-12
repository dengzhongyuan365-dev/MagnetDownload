#include "magnet/storage/piece_manager.h"
#include "magnet/utils/logger.h"
#include "magnet/utils/sha1.h"

#include <algorithm>

namespace magnet::storage {

// 日志宏
#define LOG_DEBUG(msg) magnet::utils::Logger::instance().debug(msg)
#define LOG_INFO(msg) magnet::utils::Logger::instance().info(msg)
#define LOG_WARNING(msg) magnet::utils::Logger::instance().warn(msg)
#define LOG_ERROR(msg) magnet::utils::Logger::instance().error(msg)

// ============================================================================
// 构造和析构
// ============================================================================

PieceManager::PieceManager(FileManager& file_manager, const StorageConfig& config)
    : file_manager_(file_manager)
    , config_(config)
    , piece_count_(config.pieceCount())
    , piece_length_(config.piece_length)
{
    LOG_DEBUG("PieceManager created: " + std::to_string(piece_count_) + " pieces");
}

PieceManager::~PieceManager() {
    LOG_DEBUG("PieceManager destroyed");
}

// ============================================================================
// 初始化
// ============================================================================

bool PieceManager::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (piece_count_ == 0 || piece_length_ == 0) {
        LOG_ERROR("Invalid piece configuration");
        return false;
    }
    
    // 初始化分片状态
    pieces_.resize(piece_count_);
    
    for (size_t i = 0; i < piece_count_; ++i) {
        size_t piece_size = getPieceSize(static_cast<uint32_t>(i));
        size_t block_count = (piece_size + kBlockSize - 1) / kBlockSize;
        
        pieces_[i].status = PieceStatus::Missing;
        pieces_[i].downloaded = 0;
        pieces_[i].blocks.resize(block_count, false);
    }
    
    LOG_INFO("PieceManager initialized: " + std::to_string(piece_count_) + 
             " pieces, block_size=" + std::to_string(kBlockSize));
    
    return true;
}

size_t PieceManager::recoverFromExisting() {
    LOG_INFO("Recovering from existing data...");
    
    size_t recovered = 0;
    
    for (size_t i = 0; i < piece_count_; ++i) {
        if (verifyPiece(static_cast<uint32_t>(i))) {
            recovered++;
        }
    }
    
    LOG_INFO("Recovered " + std::to_string(recovered) + " pieces");
    return recovered;
}

// ============================================================================
// 写入操作
// ============================================================================

bool PieceManager::writePiece(uint32_t index, const std::vector<uint8_t>& data) {
    if (index >= piece_count_) {
        LOG_ERROR("Invalid piece index: " + std::to_string(index));
        return false;
    }
    
    size_t expected_size = getPieceSize(index);
    if (data.size() != expected_size) {
        LOG_ERROR("Piece size mismatch: expected=" + std::to_string(expected_size) +
                  " got=" + std::to_string(data.size()));
        return false;
    }
    
    // 写入文件
    size_t offset = getPieceOffset(index);
    if (!file_manager_.write(offset, data)) {
        LOG_ERROR("Failed to write piece " + std::to_string(index));
        return false;
    }
    
    // 更新状态
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& piece = pieces_[index];
        piece.downloaded = data.size();
        std::fill(piece.blocks.begin(), piece.blocks.end(), true);
        piece.status = PieceStatus::Complete;
    }
    
    downloaded_bytes_.fetch_add(data.size());
    completed_count_.fetch_add(1);
    
    // 验证
    if (verifyPiece(index)) {
        LOG_DEBUG("Piece " + std::to_string(index) + " written and verified");
        return true;
    }
    
    LOG_WARNING("Piece " + std::to_string(index) + " written but verification failed");
    return false;
}

bool PieceManager::writeBlock(uint32_t piece_index, uint32_t block_offset,
                               const std::vector<uint8_t>& data) {
    if (piece_index >= piece_count_) {
        LOG_ERROR("Invalid piece index: " + std::to_string(piece_index));
        return false;
    }
    
    size_t piece_size = getPieceSize(piece_index);
    if (block_offset + data.size() > piece_size) {
        LOG_ERROR("Block out of bounds: piece=" + std::to_string(piece_index) +
                  " offset=" + std::to_string(block_offset) +
                  " size=" + std::to_string(data.size()));
        return false;
    }
    
    // 计算块索引
    size_t block_index = getBlockIndex(block_offset);
    
    // 检查是否已经写入
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& piece = pieces_[piece_index];
        
        if (block_index >= piece.blocks.size()) {
            LOG_ERROR("Invalid block index");
            return false;
        }
        
        if (piece.blocks[block_index]) {
            // 已经写入过
            return true;
        }
    }
    
    // 写入文件
    size_t global_offset = getPieceOffset(piece_index) + block_offset;
    if (!file_manager_.write(global_offset, data)) {
        LOG_ERROR("Failed to write block");
        return false;
    }
    
    // 更新状态
    bool piece_complete = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& piece = pieces_[piece_index];
        
        piece.blocks[block_index] = true;
        piece.downloaded += data.size();
        
        if (piece.status == PieceStatus::Missing) {
            piece.status = PieceStatus::Partial;
        }
        
        // 检查分片是否完成
        piece_complete = areAllBlocksComplete(piece_index);
        if (piece_complete) {
            piece.status = PieceStatus::Complete;
        }
    }
    
    downloaded_bytes_.fetch_add(data.size());
    
    // 如果分片完成，验证
    if (piece_complete) {
        completed_count_.fetch_add(1);
        
        if (verifyPiece(piece_index)) {
            LOG_DEBUG("Piece " + std::to_string(piece_index) + " complete and verified");
            return true;
        } else {
            LOG_WARNING("Piece " + std::to_string(piece_index) + " complete but verification failed");
            resetPiece(piece_index);
            return false;
        }
    }
    
    return true;
}

// ============================================================================
// 读取操作
// ============================================================================

std::vector<uint8_t> PieceManager::readPiece(uint32_t index) {
    if (index >= piece_count_) {
        LOG_ERROR("Invalid piece index: " + std::to_string(index));
        return {};
    }
    
    size_t offset = getPieceOffset(index);
    size_t size = getPieceSize(index);
    
    return file_manager_.read(offset, size);
}

std::vector<uint8_t> PieceManager::readBlock(uint32_t piece_index, uint32_t block_offset,
                                              size_t length) {
    if (piece_index >= piece_count_) {
        return {};
    }
    
    size_t global_offset = getPieceOffset(piece_index) + block_offset;
    return file_manager_.read(global_offset, length);
}

// ============================================================================
// 验证
// ============================================================================

bool PieceManager::verifyPiece(uint32_t index) {
    if (index >= piece_count_) {
        return false;
    }
    
    // 检查是否有期望的哈希
    if (index >= config_.piece_hashes.size()) {
        // 没有哈希，直接标记为验证通过
        std::lock_guard<std::mutex> lock(mutex_);
        if (pieces_[index].status == PieceStatus::Complete) {
            pieces_[index].status = PieceStatus::Verified;
            verified_count_.fetch_add(1);
        }
        return true;
    }
    
    // 读取分片数据
    auto data = readPiece(index);
    if (data.empty()) {
        return false;
    }
    
    // 计算 SHA1
    auto actual_hash = utils::sha1(data);
    const auto& expected_hash = config_.piece_hashes[index];
    
    bool match = (actual_hash == expected_hash);
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (match) {
            pieces_[index].status = PieceStatus::Verified;
            verified_count_.fetch_add(1);
        } else {
            pieces_[index].status = PieceStatus::Failed;
        }
    }
    
    return match;
}

size_t PieceManager::verifyAll() {
    size_t verified = 0;
    
    for (size_t i = 0; i < piece_count_; ++i) {
        if (verifyPiece(static_cast<uint32_t>(i))) {
            verified++;
        }
    }
    
    return verified;
}

// ============================================================================
// 状态查询
// ============================================================================

PieceStatus PieceManager::getPieceStatus(uint32_t index) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (index >= pieces_.size()) {
        return PieceStatus::Missing;
    }
    
    return pieces_[index].status;
}

PieceState PieceManager::getPieceState(uint32_t index) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (index >= pieces_.size()) {
        return {};
    }
    
    return pieces_[index];
}

std::vector<uint32_t> PieceManager::getCompletedPieces() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<uint32_t> result;
    for (size_t i = 0; i < pieces_.size(); ++i) {
        if (pieces_[i].status == PieceStatus::Complete ||
            pieces_[i].status == PieceStatus::Verified) {
            result.push_back(static_cast<uint32_t>(i));
        }
    }
    
    return result;
}

std::vector<uint32_t> PieceManager::getMissingPieces() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<uint32_t> result;
    for (size_t i = 0; i < pieces_.size(); ++i) {
        if (pieces_[i].status == PieceStatus::Missing ||
            pieces_[i].status == PieceStatus::Partial ||
            pieces_[i].status == PieceStatus::Failed) {
            result.push_back(static_cast<uint32_t>(i));
        }
    }
    
    return result;
}

std::vector<bool> PieceManager::getBitfield() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<bool> bitfield(pieces_.size());
    for (size_t i = 0; i < pieces_.size(); ++i) {
        bitfield[i] = (pieces_[i].status == PieceStatus::Verified);
    }
    
    return bitfield;
}

bool PieceManager::isPieceComplete(uint32_t index) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (index >= pieces_.size()) {
        return false;
    }
    
    auto status = pieces_[index].status;
    return status == PieceStatus::Complete || status == PieceStatus::Verified;
}

bool PieceManager::isPieceVerified(uint32_t index) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (index >= pieces_.size()) {
        return false;
    }
    
    return pieces_[index].status == PieceStatus::Verified;
}

// ============================================================================
// 进度统计
// ============================================================================

size_t PieceManager::getCompletedCount() const {
    return completed_count_.load();
}

size_t PieceManager::getVerifiedCount() const {
    return verified_count_.load();
}

double PieceManager::getProgress() const {
    if (piece_count_ == 0) {
        return 0;
    }
    return static_cast<double>(verified_count_.load()) / piece_count_;
}

size_t PieceManager::getDownloadedBytes() const {
    return downloaded_bytes_.load();
}

size_t PieceManager::getPieceSize(uint32_t index) const {
    return config_.getPieceSize(index);
}

size_t PieceManager::getBlockCount(uint32_t index) const {
    size_t piece_size = getPieceSize(index);
    return (piece_size + kBlockSize - 1) / kBlockSize;
}

// ============================================================================
// 内部方法
// ============================================================================

size_t PieceManager::getPieceOffset(uint32_t index) const {
    return static_cast<size_t>(index) * piece_length_;
}

size_t PieceManager::getBlockIndex(uint32_t offset) const {
    return offset / kBlockSize;
}

bool PieceManager::areAllBlocksComplete(uint32_t index) const {
    // 注意：调用时应持有锁
    if (index >= pieces_.size()) {
        return false;
    }
    
    const auto& blocks = pieces_[index].blocks;
    return std::all_of(blocks.begin(), blocks.end(), [](bool b) { return b; });
}

void PieceManager::updatePieceStatus(uint32_t index) {
    // 注意：调用时应持有锁
    if (index >= pieces_.size()) {
        return;
    }
    
    auto& piece = pieces_[index];
    
    if (areAllBlocksComplete(index)) {
        if (piece.status != PieceStatus::Verified) {
            piece.status = PieceStatus::Complete;
        }
    } else if (piece.downloaded > 0) {
        piece.status = PieceStatus::Partial;
    } else {
        piece.status = PieceStatus::Missing;
    }
}

void PieceManager::resetPiece(uint32_t index) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (index >= pieces_.size()) {
        return;
    }
    
    auto& piece = pieces_[index];
    
    // 从统计中减去
    if (piece.status == PieceStatus::Verified) {
        verified_count_.fetch_sub(1);
    }
    if (piece.status == PieceStatus::Complete || piece.status == PieceStatus::Verified) {
        completed_count_.fetch_sub(1);
    }
    downloaded_bytes_.fetch_sub(piece.downloaded);
    
    // 重置状态
    piece.status = PieceStatus::Missing;
    piece.downloaded = 0;
    std::fill(piece.blocks.begin(), piece.blocks.end(), false);
    
    LOG_DEBUG("Piece " + std::to_string(index) + " reset");
}

} // namespace magnet::storage


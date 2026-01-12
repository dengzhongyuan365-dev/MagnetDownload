#pragma once

/**
 * @file metadata_extension.h
 * @brief BEP-9 元数据扩展协议 和 BEP-10 扩展协议实现
 * 
 * BEP-9: Extension for Peers to Send Metadata Files
 * BEP-10: Extension Protocol
 * 
 * 用于从其他 peers 获取 torrent 元数据（磁力链接只有 info_hash）
 */

#include "magnet_types.h"
#include "bencode_types.h"
#include <vector>
#include <string>
#include <map>
#include <optional>
#include <cstdint>

namespace magnet::protocols {

// ============================================================================
// 常量定义
// ============================================================================

namespace extension {
    // BEP-10 扩展消息 ID
    constexpr uint8_t kExtensionMessageId = 20;
    
    // BEP-10 扩展握手消息 ID
    constexpr uint8_t kExtensionHandshakeId = 0;
    
    // BEP-9 元数据块大小 (16KB)
    constexpr size_t kMetadataBlockSize = 16384;
    
    // 扩展名称
    constexpr const char* kUtMetadata = "ut_metadata";
    constexpr const char* kUtPex = "ut_pex";
    
    // 默认客户端版本
    constexpr const char* kClientVersion = "MagnetDownload/0.1";
    
    // 最大元数据大小 (10MB)
    constexpr size_t kMaxMetadataSize = 10 * 1024 * 1024;
    
    // 我方 ut_metadata 扩展 ID
    constexpr uint8_t kMyMetadataExtensionId = 1;
    
    // BEP-10 扩展握手 Bencode 键名
    constexpr const char* kKeyExtensions = "m";           // 扩展映射
    constexpr const char* kKeyMetadataSize = "metadata_size";
    constexpr const char* kKeyClientVersion = "v";
    constexpr const char* kKeyRequestQueue = "reqq";
    constexpr const char* kKeyLocalPort = "p";
    
    // BEP-9 元数据消息 Bencode 键名
    constexpr const char* kKeyMsgType = "msg_type";
    constexpr const char* kKeyPiece = "piece";
    constexpr const char* kKeyTotalSize = "total_size";
    
    // 默认请求队列大小
    constexpr uint16_t kDefaultRequestQueue = 250;
    
    // Torrent info 字典 Bencode 键名
    constexpr const char* kKeyName = "name";
    constexpr const char* kKeyPieceLength = "piece length";
    constexpr const char* kKeyPieces = "pieces";
    constexpr const char* kKeyLength = "length";
    constexpr const char* kKeyFiles = "files";
    constexpr const char* kKeyPath = "path";
    
    // SHA1 哈希大小
    constexpr size_t kSha1Size = 20;
}

// ============================================================================
// 元数据消息类型
// ============================================================================

enum class MetadataMessageType : uint8_t {
    Request = 0,    // 请求元数据块
    Data    = 1,    // 元数据块数据
    Reject  = 2     // 拒绝请求
};

// ============================================================================
// 扩展握手数据
// ============================================================================

struct ExtensionHandshake {
    // 扩展映射: 扩展名 -> 本地消息 ID
    std::map<std::string, uint8_t> extensions;
    
    // 元数据大小（如果已有）
    std::optional<size_t> metadata_size;
    
    // 客户端版本
    std::string client_version;
    
    // 请求队列大小
    uint16_t request_queue_size{250};
    
    // 本地 TCP 端口（可选）
    std::optional<uint16_t> local_port;
    
    /**
     * @brief 检查是否支持元数据扩展
     */
    bool supportsMetadata() const {
        return extensions.count(extension::kUtMetadata) > 0;
    }
    
    /**
     * @brief 获取元数据扩展的消息 ID
     * @return 消息 ID，如果不支持返回 0
     */
    uint8_t metadataExtensionId() const {
        auto it = extensions.find(extension::kUtMetadata);
        return it != extensions.end() ? it->second : 0;
    }
    
    /**
     * @brief 检查是否有元数据
     */
    bool hasMetadata() const {
        return metadata_size.has_value() && metadata_size.value() > 0;
    }
};

// ============================================================================
// 元数据消息
// ============================================================================

struct MetadataMessage {
    MetadataMessageType type;
    uint32_t piece_index;
    std::optional<size_t> total_size;  // 仅 Data 消息
    std::vector<uint8_t> data;         // 仅 Data 消息
    
    MetadataMessage() : type(MetadataMessageType::Request), piece_index(0) {}
    
    bool isRequest() const { return type == MetadataMessageType::Request; }
    bool isData() const { return type == MetadataMessageType::Data; }
    bool isReject() const { return type == MetadataMessageType::Reject; }
};

// ============================================================================
// Torrent 元数据
// ============================================================================

struct TorrentMetadata {
    // 基本信息
    std::string name;                     // 种子名称
    size_t piece_length{0};               // 每个 piece 的大小
    std::vector<std::array<uint8_t, 20>> piece_hashes;  // 所有 piece 的 SHA1 hash
    
    // 单文件种子
    std::optional<size_t> length;         // 文件大小
    
    // 多文件种子
    struct FileInfo {
        std::string path;
        size_t length;
    };
    std::vector<FileInfo> files;
    
    // 原始 info 字典（用于验证）
    std::vector<uint8_t> raw_info;
    
    // info_hash
    InfoHash info_hash;
    
    /**
     * @brief 计算总大小
     */
    size_t totalSize() const {
        if (length.has_value()) {
            return length.value();
        }
        size_t total = 0;
        for (const auto& f : files) {
            total += f.length;
        }
        return total;
    }
    
    /**
     * @brief 计算 piece 数量
     */
    size_t pieceCount() const {
        if (piece_length == 0) return 0;
        return (totalSize() + piece_length - 1) / piece_length;
    }
    
    /**
     * @brief 是否为多文件种子
     */
    bool isMultiFile() const { return !files.empty(); }
    
    /**
     * @brief 获取指定 piece 的大小
     */
    size_t getPieceSize(size_t piece_index) const {
        size_t total = totalSize();
        size_t regular_pieces = total / piece_length;
        
        if (piece_index < regular_pieces) {
            return piece_length;
        } else if (piece_index == regular_pieces) {
            return total % piece_length;
        }
        return 0;
    }
};

// ============================================================================
// 元数据错误
// ============================================================================

enum class MetadataError {
    Success = 0,
    Timeout,            // 获取超时
    AllPeersRejected,   // 所有 peers 拒绝
    NoPeersAvailable,   // 没有可用的 peers
    VerificationFailed, // SHA1 验证失败
    ParseError,         // 解析错误
    InvalidPieceIndex,  // 无效的块索引
    SizeMismatch,       // 大小不匹配
    TooLarge            // 元数据太大
};

// ============================================================================
// MetadataExtension 类 - 协议编解码
// ============================================================================

class MetadataExtension {
public:
    // === 扩展握手 ===
    
    /**
     * @brief 创建扩展握手消息
     * @param metadata_size 元数据大小（如果已有）
     * @param client_version 客户端版本
     * @return 编码后的消息（不含消息长度前缀和消息 ID）
     */
    static std::vector<uint8_t> createExtensionHandshake(
        std::optional<size_t> metadata_size = std::nullopt,
        const std::string& client_version = extension::kClientVersion
    );
    
    /**
     * @brief 解析扩展握手消息
     * @param data 消息数据（不含消息长度前缀和消息 ID）
     * @return 解析结果
     */
    static std::optional<ExtensionHandshake> parseExtensionHandshake(
        const std::vector<uint8_t>& data
    );
    
    // === 元数据消息 ===
    
    /**
     * @brief 创建元数据请求消息
     * @param extension_id 对方的 ut_metadata 扩展 ID
     * @param piece_index 请求的块索引
     * @return 编码后的消息（不含消息长度前缀和消息 ID）
     */
    static std::vector<uint8_t> createMetadataRequest(
        uint8_t extension_id,
        uint32_t piece_index
    );
    
    /**
     * @brief 创建元数据数据消息
     * @param extension_id 对方的 ut_metadata 扩展 ID
     * @param piece_index 块索引
     * @param total_size 元数据总大小
     * @param data 块数据
     * @return 编码后的消息
     */
    static std::vector<uint8_t> createMetadataData(
        uint8_t extension_id,
        uint32_t piece_index,
        size_t total_size,
        const std::vector<uint8_t>& data
    );
    
    /**
     * @brief 创建元数据拒绝消息
     * @param extension_id 对方的 ut_metadata 扩展 ID
     * @param piece_index 被拒绝的块索引
     * @return 编码后的消息
     */
    static std::vector<uint8_t> createMetadataReject(
        uint8_t extension_id,
        uint32_t piece_index
    );
    
    /**
     * @brief 解析元数据消息
     * @param data 消息数据（不含扩展消息 ID）
     * @return 解析结果
     */
    static std::optional<MetadataMessage> parseMetadataMessage(
        const std::vector<uint8_t>& data
    );
    
    // === 辅助函数 ===
    
    /**
     * @brief 计算元数据块数量
     * @param metadata_size 元数据大小
     * @return 块数量
     */
    static size_t calculatePieceCount(size_t metadata_size);
    
    /**
     * @brief 计算指定块的大小
     * @param piece_index 块索引
     * @param metadata_size 元数据总大小
     * @return 块大小
     */
    static size_t calculatePieceSize(uint32_t piece_index, size_t metadata_size);
    
    /**
     * @brief 解析 torrent 元数据（info 字典）
     * @param data 原始 info 字典数据
     * @param expected_hash 期望的 info_hash（用于验证）
     * @return 解析结果
     */
    static std::optional<TorrentMetadata> parseTorrentMetadata(
        const std::vector<uint8_t>& data,
        const InfoHash& expected_hash
    );
    
private:
    // 内部辅助方法
    static BencodeDict createHandshakeDict(
        std::optional<size_t> metadata_size,
        const std::string& client_version
    );
};

} // namespace magnet::protocols


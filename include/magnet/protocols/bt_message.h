#pragma once

#include "magnet_types.h"

#include <array>
#include <vector>
#include <string>
#include <optional>
#include <cstdint>

namespace magnet::protocols {

// ============================================================================
// BitTorrent 消息类型
// ============================================================================

enum class BtMessageType : uint8_t {
    KeepAlive = 0xFF,       // 特殊值，实际协议中无 ID（length=0）
    Choke = 0,              // 阻塞
    Unchoke = 1,            // 解除阻塞
    Interested = 2,         // 感兴趣
    NotInterested = 3,      // 不感兴趣
    Have = 4,               // 拥有某个分片
    Bitfield = 5,           // 分片位图
    Request = 6,            // 请求数据块
    Piece = 7,              // 数据块
    Cancel = 8,             // 取消请求
    Port = 9                // DHT 端口
};

/**
 * @brief 消息类型转字符串
 */
inline const char* btMessageTypeToString(BtMessageType type) {
    switch (type) {
        case BtMessageType::KeepAlive: return "KeepAlive";
        case BtMessageType::Choke: return "Choke";
        case BtMessageType::Unchoke: return "Unchoke";
        case BtMessageType::Interested: return "Interested";
        case BtMessageType::NotInterested: return "NotInterested";
        case BtMessageType::Have: return "Have";
        case BtMessageType::Bitfield: return "Bitfield";
        case BtMessageType::Request: return "Request";
        case BtMessageType::Piece: return "Piece";
        case BtMessageType::Cancel: return "Cancel";
        case BtMessageType::Port: return "Port";
        default: return "Unknown";
    }
}

// ============================================================================
// 握手消息
// ============================================================================

/**
 * @struct Handshake
 * @brief BitTorrent 握手消息
 * 
 * 格式：
 * - 1 字节: pstrlen (19)
 * - 19 字节: pstr ("BitTorrent protocol")
 * - 8 字节: reserved
 * - 20 字节: info_hash
 * - 20 字节: peer_id
 * 
 * 总长度: 68 字节
 */
struct Handshake {
    static constexpr size_t kSize = 68;
    static constexpr uint8_t kProtocolLength = 19;
    static constexpr const char* kProtocol = "BitTorrent protocol";
    
    std::array<uint8_t, 8> reserved{};      // 保留字节（用于扩展协议）
    std::array<uint8_t, 20> info_hash{};    // 文件的 info_hash
    std::array<uint8_t, 20> peer_id{};      // Peer ID
    
    /**
     * @brief 编码为字节数组
     */
    std::vector<uint8_t> encode() const;
    
    /**
     * @brief 从字节数组解码
     */
    static std::optional<Handshake> decode(const std::vector<uint8_t>& data);
    static std::optional<Handshake> decode(const uint8_t* data, size_t len);
    
    /**
     * @brief 创建握手消息
     * @param info_hash 文件的 info_hash
     * @param peer_id Peer ID（20 字节，通常格式为 "-XX0000-xxxxxxxxxxxx"）
     */
    static Handshake create(const InfoHash& info_hash, const std::string& peer_id);
    
    /**
     * @brief 验证 info_hash 是否匹配
     */
    bool matchInfoHash(const InfoHash& hash) const;
};

// ============================================================================
// 块信息
// ============================================================================

/**
 * @struct BlockInfo
 * @brief 数据块信息（用于 Request 和 Cancel 消息）
 */
struct BlockInfo {
    uint32_t piece_index{0};    // 分片索引
    uint32_t begin{0};          // 块在分片中的偏移
    uint32_t length{0};         // 块长度
    
    static constexpr uint32_t kDefaultBlockSize = 16384;  // 16KB（标准块大小）
    
    BlockInfo() = default;
    BlockInfo(uint32_t index, uint32_t offset, uint32_t len)
        : piece_index(index), begin(offset), length(len) {}
    
    bool operator==(const BlockInfo& other) const {
        return piece_index == other.piece_index &&
               begin == other.begin &&
               length == other.length;
    }
    
    bool operator!=(const BlockInfo& other) const {
        return !(*this == other);
    }
};

// ============================================================================
// 数据块
// ============================================================================

/**
 * @struct PieceBlock
 * @brief 数据块（用于 Piece 消息）
 */
struct PieceBlock {
    uint32_t piece_index{0};        // 分片索引
    uint32_t begin{0};              // 块偏移
    std::vector<uint8_t> data;      // 实际数据
    
    PieceBlock() = default;
    PieceBlock(uint32_t index, uint32_t offset, std::vector<uint8_t> block_data)
        : piece_index(index), begin(offset), data(std::move(block_data)) {}
    
    /**
     * @brief 转换为 BlockInfo
     */
    BlockInfo toBlockInfo() const {
        return BlockInfo{piece_index, begin, static_cast<uint32_t>(data.size())};
    }
};

// ============================================================================
// BtMessage 类
// ============================================================================

/**
 * @class BtMessage
 * @brief BitTorrent 协议消息
 * 
 * 消息格式：
 * - 4 字节: length（大端序，不包含 length 本身）
 * - 1 字节: id（消息类型）
 * - N 字节: payload（消息内容）
 * 
 * 特殊情况：length=0 表示 KeepAlive 消息（无 id 和 payload）
 * 
 * 使用示例：
 * @code
 * // 创建请求消息
 * BlockInfo block{5, 0, 16384};
 * auto msg = BtMessage::createRequest(block);
 * auto data = msg.encode();
 * 
 * // 解码消息
 * auto received = BtMessage::decode(data);
 * if (received && received->type() == BtMessageType::Request) {
 *     auto info = received->toBlockInfo();
 * }
 * @endcode
 */
class BtMessage {
public:
    BtMessage() = default;
    
    // ========================================================================
    // 静态工厂方法
    // ========================================================================
    
    /** @brief 创建 KeepAlive 消息 */
    static BtMessage createKeepAlive();
    
    /** @brief 创建 Choke 消息 */
    static BtMessage createChoke();
    
    /** @brief 创建 Unchoke 消息 */
    static BtMessage createUnchoke();
    
    /** @brief 创建 Interested 消息 */
    static BtMessage createInterested();
    
    /** @brief 创建 NotInterested 消息 */
    static BtMessage createNotInterested();
    
    /** 
     * @brief 创建 Have 消息
     * @param piece_index 拥有的分片索引
     */
    static BtMessage createHave(uint32_t piece_index);
    
    /**
     * @brief 创建 Bitfield 消息
     * @param bitfield 分片位图（true 表示拥有）
     */
    static BtMessage createBitfield(const std::vector<bool>& bitfield);
    
    /**
     * @brief 创建 Request 消息
     * @param block 请求的块信息
     */
    static BtMessage createRequest(const BlockInfo& block);
    
    /**
     * @brief 创建 Piece 消息
     * @param block 数据块
     */
    static BtMessage createPiece(const PieceBlock& block);
    
    /**
     * @brief 创建 Cancel 消息
     * @param block 要取消的块信息
     */
    static BtMessage createCancel(const BlockInfo& block);
    
    /**
     * @brief 创建 Port 消息
     * @param port DHT 监听端口
     */
    static BtMessage createPort(uint16_t port);
    
    // ========================================================================
    // 编解码
    // ========================================================================
    
    /**
     * @brief 编码为字节数组
     */
    std::vector<uint8_t> encode() const;
    
    /**
     * @brief 从字节数组解码
     * @param data 数据（必须包含完整消息）
     * @return 解码后的消息，失败返回 nullopt
     */
    static std::optional<BtMessage> decode(const std::vector<uint8_t>& data);
    static std::optional<BtMessage> decode(const uint8_t* data, size_t len);
    
    /**
     * @brief 获取完整消息所需的长度
     * @param header 消息头部（至少 4 字节）
     * @return 完整消息长度（包括 4 字节的 length 字段），数据不足返回 0
     * 
     * 用于流式解析：先读取 4 字节获取总长度，再读取剩余数据
     */
    static size_t getMessageLength(const uint8_t* header, size_t available);
    
    // ========================================================================
    // 类型检查
    // ========================================================================
    
    BtMessageType type() const { return type_; }
    
    bool isKeepAlive() const { return type_ == BtMessageType::KeepAlive; }
    bool isChoke() const { return type_ == BtMessageType::Choke; }
    bool isUnchoke() const { return type_ == BtMessageType::Unchoke; }
    bool isInterested() const { return type_ == BtMessageType::Interested; }
    bool isNotInterested() const { return type_ == BtMessageType::NotInterested; }
    bool isHave() const { return type_ == BtMessageType::Have; }
    bool isBitfield() const { return type_ == BtMessageType::Bitfield; }
    bool isRequest() const { return type_ == BtMessageType::Request; }
    bool isPiece() const { return type_ == BtMessageType::Piece; }
    bool isCancel() const { return type_ == BtMessageType::Cancel; }
    bool isPort() const { return type_ == BtMessageType::Port; }
    
    // ========================================================================
    // 字段访问
    // ========================================================================
    
    /** @brief 获取分片索引（Have, Request, Piece, Cancel） */
    uint32_t pieceIndex() const { return piece_index_; }
    
    /** @brief 获取块偏移（Request, Piece, Cancel） */
    uint32_t begin() const { return begin_; }
    
    /** @brief 获取块长度（Request, Cancel） */
    uint32_t length() const { return length_; }
    
    /** @brief 获取端口（Port） */
    uint16_t port() const { return port_; }
    
    /** @brief 获取数据（Piece） */
    const std::vector<uint8_t>& data() const { return data_; }
    
    /** @brief 获取位图（Bitfield） */
    const std::vector<bool>& bitfield() const { return bitfield_; }
    
    // ========================================================================
    // 结构化数据访问
    // ========================================================================
    
    /**
     * @brief 转换为 BlockInfo（Request, Cancel）
     */
    BlockInfo toBlockInfo() const {
        return BlockInfo{piece_index_, begin_, length_};
    }
    
    /**
     * @brief 转换为 PieceBlock（Piece）
     */
    PieceBlock toPieceBlock() const {
        return PieceBlock{piece_index_, begin_, data_};
    }

private:
    BtMessageType type_ = BtMessageType::KeepAlive;
    
    // Have, Request, Piece, Cancel
    uint32_t piece_index_{0};
    
    // Request, Piece, Cancel
    uint32_t begin_{0};
    uint32_t length_{0};
    
    // Port
    uint16_t port_{0};
    
    // Piece
    std::vector<uint8_t> data_;
    
    // Bitfield
    std::vector<bool> bitfield_;
};

// ============================================================================
// 工具函数
// ============================================================================

namespace bt {

/**
 * @brief 写入 32 位大端整数
 */
inline void writeUint32BE(std::vector<uint8_t>& buffer, uint32_t value) {
    buffer.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    buffer.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    buffer.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    buffer.push_back(static_cast<uint8_t>(value & 0xFF));
}

/**
 * @brief 写入 16 位大端整数
 */
inline void writeUint16BE(std::vector<uint8_t>& buffer, uint16_t value) {
    buffer.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    buffer.push_back(static_cast<uint8_t>(value & 0xFF));
}

/**
 * @brief 读取 32 位大端整数
 */
inline uint32_t readUint32BE(const uint8_t* data) {
    return (static_cast<uint32_t>(data[0]) << 24) |
           (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) |
           static_cast<uint32_t>(data[3]);
}

/**
 * @brief 读取 16 位大端整数
 */
inline uint16_t readUint16BE(const uint8_t* data) {
    return (static_cast<uint16_t>(data[0]) << 8) |
           static_cast<uint16_t>(data[1]);
}

} // namespace bt

} // namespace magnet::protocols


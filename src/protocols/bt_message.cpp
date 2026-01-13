#include "magnet/protocols/bt_message.h"

#include <cstring>
#include <algorithm>

namespace magnet::protocols {

// ============================================================================
// Handshake 实现
// ============================================================================

std::vector<uint8_t> Handshake::encode() const {
    std::vector<uint8_t> result;
    result.reserve(kSize);
    
    // pstrlen (1 byte)
    result.push_back(kProtocolLength);
    
    // pstr (19 bytes)
    result.insert(result.end(), kProtocol, kProtocol + kProtocolLength);
    
    // reserved (8 bytes)
    result.insert(result.end(), reserved.begin(), reserved.end());
    
    // info_hash (20 bytes)
    result.insert(result.end(), info_hash.begin(), info_hash.end());
    
    // peer_id (20 bytes)
    result.insert(result.end(), peer_id.begin(), peer_id.end());
    
    return result;
}

std::optional<Handshake> Handshake::decode(const std::vector<uint8_t>& data) {
    return decode(data.data(), data.size());
}

std::optional<Handshake> Handshake::decode(const uint8_t* data, size_t len) {
    if (len < kSize) {
        return std::nullopt;
    }
    
    // 验证 pstrlen
    if (data[0] != kProtocolLength) {
        return std::nullopt;
    }
    
    // 验证 pstr
    if (std::memcmp(data + 1, kProtocol, kProtocolLength) != 0) {
        return std::nullopt;
    }
    
    Handshake hs;
    
    // reserved (offset 20)
    std::memcpy(hs.reserved.data(), data + 20, 8);
    
    // info_hash (offset 28)
    std::memcpy(hs.info_hash.data(), data + 28, 20);
    
    // peer_id (offset 48)
    std::memcpy(hs.peer_id.data(), data + 48, 20);
    
    return hs;
}

Handshake Handshake::create(const InfoHash& info_hash, const std::string& peer_id) {
    Handshake hs;
    
    // 设置支持 BEP-10 扩展协议（这是获取元数据的关键！）
    hs.setExtensionSupport();
    
    // 复制 info_hash
    std::memcpy(hs.info_hash.data(), info_hash.bytes().data(), 20);
    
    // 复制 peer_id（截断或填充到 20 字节）
    hs.peer_id.fill(0);  // 先清零
    size_t copy_len = std::min(peer_id.size(), size_t(20));
    std::memcpy(hs.peer_id.data(), peer_id.data(), copy_len);
    
    return hs;
}

bool Handshake::matchInfoHash(const InfoHash& hash) const {
    return std::memcmp(info_hash.data(), hash.bytes().data(), 20) == 0;
}

// ============================================================================
// BtMessage 静态工厂方法
// ============================================================================

BtMessage BtMessage::createKeepAlive() {
    BtMessage msg;
    msg.type_ = BtMessageType::KeepAlive;
    return msg;
}

BtMessage BtMessage::createChoke() {
    BtMessage msg;
    msg.type_ = BtMessageType::Choke;
    return msg;
}

BtMessage BtMessage::createUnchoke() {
    BtMessage msg;
    msg.type_ = BtMessageType::Unchoke;
    return msg;
}

BtMessage BtMessage::createInterested() {
    BtMessage msg;
    msg.type_ = BtMessageType::Interested;
    return msg;
}

BtMessage BtMessage::createNotInterested() {
    BtMessage msg;
    msg.type_ = BtMessageType::NotInterested;
    return msg;
}

BtMessage BtMessage::createHave(uint32_t piece_index) {
    BtMessage msg;
    msg.type_ = BtMessageType::Have;
    msg.piece_index_ = piece_index;
    return msg;
}

BtMessage BtMessage::createBitfield(const std::vector<bool>& bitfield) {
    BtMessage msg;
    msg.type_ = BtMessageType::Bitfield;
    msg.bitfield_ = bitfield;
    return msg;
}

BtMessage BtMessage::createRequest(const BlockInfo& block) {
    BtMessage msg;
    msg.type_ = BtMessageType::Request;
    msg.piece_index_ = block.piece_index;
    msg.begin_ = block.begin;
    msg.length_ = block.length;
    return msg;
}

BtMessage BtMessage::createPiece(const PieceBlock& block) {
    BtMessage msg;
    msg.type_ = BtMessageType::Piece;
    msg.piece_index_ = block.piece_index;
    msg.begin_ = block.begin;
    msg.data_ = block.data;
    return msg;
}

BtMessage BtMessage::createCancel(const BlockInfo& block) {
    BtMessage msg;
    msg.type_ = BtMessageType::Cancel;
    msg.piece_index_ = block.piece_index;
    msg.begin_ = block.begin;
    msg.length_ = block.length;
    return msg;
}

BtMessage BtMessage::createPort(uint16_t port) {
    BtMessage msg;
    msg.type_ = BtMessageType::Port;
    msg.port_ = port;
    return msg;
}

BtMessage BtMessage::createExtended(uint8_t extension_id, const std::vector<uint8_t>& payload) {
    BtMessage msg;
    msg.type_ = BtMessageType::Extended;
    msg.extended_id_ = extension_id;
    msg.payload_ = payload;
    return msg;
}

// ============================================================================
// BtMessage 编码
// ============================================================================

std::vector<uint8_t> BtMessage::encode() const {
    std::vector<uint8_t> result;
    
    switch (type_) {
        case BtMessageType::KeepAlive:
            // length = 0
            bt::writeUint32BE(result, 0);
            break;
            
        case BtMessageType::Choke:
        case BtMessageType::Unchoke:
        case BtMessageType::Interested:
        case BtMessageType::NotInterested:
            // length = 1, id only
            bt::writeUint32BE(result, 1);
            result.push_back(static_cast<uint8_t>(type_));
            break;
            
        case BtMessageType::Have:
            // length = 5, id + piece_index
            bt::writeUint32BE(result, 5);
            result.push_back(static_cast<uint8_t>(type_));
            bt::writeUint32BE(result, piece_index_);
            break;
            
        case BtMessageType::Bitfield: {
            // 将 bitfield 转换为字节数组
            size_t byte_count = (bitfield_.size() + 7) / 8;
            std::vector<uint8_t> bitfield_bytes(byte_count, 0);
            
            for (size_t i = 0; i < bitfield_.size(); ++i) {
                if (bitfield_[i]) {
                    bitfield_bytes[i / 8] |= (1 << (7 - (i % 8)));
                }
            }
            
            // length = 1 + N
            bt::writeUint32BE(result, static_cast<uint32_t>(1 + bitfield_bytes.size()));
            result.push_back(static_cast<uint8_t>(type_));
            result.insert(result.end(), bitfield_bytes.begin(), bitfield_bytes.end());
            break;
        }
            
        case BtMessageType::Request:
        case BtMessageType::Cancel:
            // length = 13, id + index + begin + length
            bt::writeUint32BE(result, 13);
            result.push_back(static_cast<uint8_t>(type_));
            bt::writeUint32BE(result, piece_index_);
            bt::writeUint32BE(result, begin_);
            bt::writeUint32BE(result, length_);
            break;
            
        case BtMessageType::Piece:
            // length = 9 + N, id + index + begin + data
            bt::writeUint32BE(result, static_cast<uint32_t>(9 + data_.size()));
            result.push_back(static_cast<uint8_t>(type_));
            bt::writeUint32BE(result, piece_index_);
            bt::writeUint32BE(result, begin_);
            result.insert(result.end(), data_.begin(), data_.end());
            break;
            
        case BtMessageType::Port:
            // length = 3, id + port
            bt::writeUint32BE(result, 3);
            result.push_back(static_cast<uint8_t>(type_));
            bt::writeUint16BE(result, port_);
            break;
            
        case BtMessageType::Extended:
            // length = 2 + N, id + extension_id + payload
            bt::writeUint32BE(result, static_cast<uint32_t>(2 + payload_.size()));
            result.push_back(static_cast<uint8_t>(type_));
            result.push_back(extended_id_);
            result.insert(result.end(), payload_.begin(), payload_.end());
            break;
    }
    
    return result;
}

// ============================================================================
// BtMessage 解码
// ============================================================================

size_t BtMessage::getMessageLength(const uint8_t* header, size_t available) {
    if (available < 4) {
        return 0;  // 数据不足
    }
    
    uint32_t payload_length = bt::readUint32BE(header);
    return 4 + payload_length;  // 4 字节 length + payload
}

std::optional<BtMessage> BtMessage::decode(const std::vector<uint8_t>& data) {
    return decode(data.data(), data.size());
}

std::optional<BtMessage> BtMessage::decode(const uint8_t* data, size_t len) {
    if (len < 4) {
        return std::nullopt;  // 至少需要 4 字节的 length
    }
    
    uint32_t payload_length = bt::readUint32BE(data);
    
    // 检查数据是否完整
    if (len < 4 + payload_length) {
        return std::nullopt;
    }
    
    // KeepAlive
    if (payload_length == 0) {
        return createKeepAlive();
    }
    
    // 读取消息 ID
    uint8_t id = data[4];
    const uint8_t* payload = data + 5;
    size_t payload_size = payload_length - 1;
    
    BtMessage msg;
    
    switch (id) {
        case 0:  // Choke
            msg.type_ = BtMessageType::Choke;
            break;
            
        case 1:  // Unchoke
            msg.type_ = BtMessageType::Unchoke;
            break;
            
        case 2:  // Interested
            msg.type_ = BtMessageType::Interested;
            break;
            
        case 3:  // NotInterested
            msg.type_ = BtMessageType::NotInterested;
            break;
            
        case 4:  // Have
            if (payload_size < 4) return std::nullopt;
            msg.type_ = BtMessageType::Have;
            msg.piece_index_ = bt::readUint32BE(payload);
            break;
            
        case 5:  // Bitfield
            msg.type_ = BtMessageType::Bitfield;
            // 将字节数组转换为 bool 数组
            for (size_t i = 0; i < payload_size; ++i) {
                for (int bit = 7; bit >= 0; --bit) {
                    msg.bitfield_.push_back((payload[i] >> bit) & 1);
                }
            }
            break;
            
        case 6:  // Request
            if (payload_size < 12) return std::nullopt;
            msg.type_ = BtMessageType::Request;
            msg.piece_index_ = bt::readUint32BE(payload);
            msg.begin_ = bt::readUint32BE(payload + 4);
            msg.length_ = bt::readUint32BE(payload + 8);
            break;
            
        case 7:  // Piece
            if (payload_size < 8) return std::nullopt;
            msg.type_ = BtMessageType::Piece;
            msg.piece_index_ = bt::readUint32BE(payload);
            msg.begin_ = bt::readUint32BE(payload + 4);
            msg.data_.assign(payload + 8, payload + payload_size);
            break;
            
        case 8:  // Cancel
            if (payload_size < 12) return std::nullopt;
            msg.type_ = BtMessageType::Cancel;
            msg.piece_index_ = bt::readUint32BE(payload);
            msg.begin_ = bt::readUint32BE(payload + 4);
            msg.length_ = bt::readUint32BE(payload + 8);
            break;
            
        case 9:  // Port
            if (payload_size < 2) return std::nullopt;
            msg.type_ = BtMessageType::Port;
            msg.port_ = bt::readUint16BE(payload);
            break;
            
        case 20:  // Extended (BEP-10)
            if (payload_size < 1) return std::nullopt;
            msg.type_ = BtMessageType::Extended;
            msg.extended_id_ = payload[0];
            if (payload_size > 1) {
                msg.payload_.assign(payload + 1, payload + payload_size);
            }
            break;
            
        default:
            // 未知消息类型，忽略
            return std::nullopt;
    }
    
    return msg;
}

} // namespace magnet::protocols


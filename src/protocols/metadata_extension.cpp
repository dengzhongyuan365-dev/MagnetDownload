// MagnetDownload - BEP-9/10 元数据扩展协议实现

#include "magnet/protocols/metadata_extension.h"
#include "magnet/protocols/bencode.h"
#include "magnet/utils/sha1.h"
#include "magnet/utils/logger.h"

#include <algorithm>
#include <cstring>
#include <cstdint>

namespace magnet::protocols {

#define LOG_DEBUG(msg) magnet::utils::Logger::instance().debug(std::string("[MetadataExt] ") + msg)
#define LOG_INFO(msg) magnet::utils::Logger::instance().info(std::string("[MetadataExt] ") + msg)
#define LOG_WARNING(msg) magnet::utils::Logger::instance().warn(std::string("[MetadataExt] ") + msg)

// ============================================================================
// 扩展握手
// ============================================================================

std::vector<uint8_t> MetadataExtension::createExtensionHandshake(
    std::optional<size_t> metadata_size,
    const std::string& client_version
) {
    BencodeDict dict = createHandshakeDict(metadata_size, client_version);
    std::string encoded = Bencode::encode(BencodeValue(dict));
    return std::vector<uint8_t>(encoded.begin(), encoded.end());
}

BencodeDict MetadataExtension::createHandshakeDict(
    std::optional<size_t> metadata_size,
    const std::string& client_version
) {
    BencodeDict dict;
    
    // 扩展映射
    BencodeDict m;
    m[extension::kUtMetadata] = BencodeValue(
        static_cast<BencodeInt>(extension::kMyMetadataExtensionId));
    dict[extension::kKeyExtensions] = BencodeValue(m);
    
    // 元数据大小（如果有）
    if (metadata_size.has_value()) {
        dict[extension::kKeyMetadataSize] = BencodeValue(
            static_cast<BencodeInt>(metadata_size.value()));
    }
    
    // 客户端版本
    dict[extension::kKeyClientVersion] = BencodeValue(client_version);
    
    // 请求队列大小
    dict[extension::kKeyRequestQueue] = BencodeValue(
        static_cast<BencodeInt>(extension::kDefaultRequestQueue));
    
    return dict;
}

std::optional<ExtensionHandshake> MetadataExtension::parseExtensionHandshake(
    const std::vector<uint8_t>& data
) {
    if (data.empty()) {
        LOG_DEBUG("Empty handshake data");
        return std::nullopt;
    }
    
    std::string data_str(data.begin(), data.end());
    auto parsed = Bencode::decode(data_str);
    
    if (!parsed || !parsed->isDict()) {
        LOG_DEBUG("Failed to parse handshake as bencode dict");
        return std::nullopt;
    }
    
    const auto& dict = parsed->asDict();
    ExtensionHandshake handshake;
    
    // 解析扩展映射
    auto m_it = dict.find(extension::kKeyExtensions);
    if (m_it != dict.end() && m_it->second.isDict()) {
        const auto& m = m_it->second.asDict();
        for (const auto& [name, value] : m) {
            if (value.isInt()) {
                handshake.extensions[name] = static_cast<uint8_t>(value.asInt());
            }
        }
    }
    
    // 解析元数据大小
    auto size_it = dict.find(extension::kKeyMetadataSize);
    if (size_it != dict.end() && size_it->second.isInt()) {
        handshake.metadata_size = static_cast<size_t>(size_it->second.asInt());
    }
    
    // 解析客户端版本
    auto v_it = dict.find(extension::kKeyClientVersion);
    if (v_it != dict.end() && v_it->second.isString()) {
        handshake.client_version = v_it->second.asString();
    }
    
    // 解析请求队列大小
    auto reqq_it = dict.find(extension::kKeyRequestQueue);
    if (reqq_it != dict.end() && reqq_it->second.isInt()) {
        handshake.request_queue_size = static_cast<uint16_t>(reqq_it->second.asInt());
    }
    
    // 解析本地端口
    auto port_it = dict.find(extension::kKeyLocalPort);
    if (port_it != dict.end() && port_it->second.isInt()) {
        handshake.local_port = static_cast<uint16_t>(port_it->second.asInt());
    }
    
    LOG_DEBUG("Parsed extension handshake: ut_metadata=" + 
              std::to_string(handshake.metadataExtensionId()) +
              ", metadata_size=" + 
              (handshake.metadata_size.has_value() ? 
               std::to_string(handshake.metadata_size.value()) : "none") +
              ", client=" + handshake.client_version);
    
    return handshake;
}

// ============================================================================
// 元数据消息
// ============================================================================

std::vector<uint8_t> MetadataExtension::createMetadataRequest(
    uint8_t extension_id,
    uint32_t piece_index
) {
    BencodeDict dict;
    dict[extension::kKeyMsgType] = BencodeValue(
        static_cast<BencodeInt>(MetadataMessageType::Request));
    dict[extension::kKeyPiece] = BencodeValue(static_cast<BencodeInt>(piece_index));
    
    std::string encoded = Bencode::encode(BencodeValue(dict));
    
    // 添加扩展 ID 前缀
    std::vector<uint8_t> result;
    result.reserve(1 + encoded.size());
    result.push_back(extension_id);
    result.insert(result.end(), encoded.begin(), encoded.end());
    
    return result;
}

std::vector<uint8_t> MetadataExtension::createMetadataData(
    uint8_t extension_id,
    uint32_t piece_index,
    size_t total_size,
    const std::vector<uint8_t>& data
) {
    BencodeDict dict;
    dict[extension::kKeyMsgType] = BencodeValue(
        static_cast<BencodeInt>(MetadataMessageType::Data));
    dict[extension::kKeyPiece] = BencodeValue(static_cast<BencodeInt>(piece_index));
    dict[extension::kKeyTotalSize] = BencodeValue(static_cast<BencodeInt>(total_size));
    
    std::string encoded = Bencode::encode(BencodeValue(dict));
    
    // 添加扩展 ID 前缀和数据
    std::vector<uint8_t> result;
    result.reserve(1 + encoded.size() + data.size());
    result.push_back(extension_id);
    result.insert(result.end(), encoded.begin(), encoded.end());
    result.insert(result.end(), data.begin(), data.end());
    
    return result;
}

std::vector<uint8_t> MetadataExtension::createMetadataReject(
    uint8_t extension_id,
    uint32_t piece_index
) {
    BencodeDict dict;
    dict[extension::kKeyMsgType] = BencodeValue(
        static_cast<BencodeInt>(MetadataMessageType::Reject));
    dict[extension::kKeyPiece] = BencodeValue(static_cast<BencodeInt>(piece_index));
    
    std::string encoded = Bencode::encode(BencodeValue(dict));
    
    // 添加扩展 ID 前缀
    std::vector<uint8_t> result;
    result.reserve(1 + encoded.size());
    result.push_back(extension_id);
    result.insert(result.end(), encoded.begin(), encoded.end());
    
    return result;
}

std::optional<MetadataMessage> MetadataExtension::parseMetadataMessage(
    const std::vector<uint8_t>& data
) {
    if (data.empty()) {
        LOG_DEBUG("Empty metadata message");
        return std::nullopt;
    }
    
    // 找到 bencode 字典的结束位置（以 'e' 结尾）
    // 格式: d...e<binary data>
    size_t dict_end = 0;
    int depth = 0;
    for (size_t i = 0; i < data.size(); ++i) {
        char c = static_cast<char>(data[i]);
        if (c == 'd' || c == 'l') {
            depth++;
        } else if (c == 'e') {
            depth--;
            if (depth == 0) {
                dict_end = i + 1;
                break;
            }
        } else if (c == 'i') {
            // 整数，跳到 'e'
            while (i < data.size() && data[i] != 'e') i++;
        } else if (c >= '0' && c <= '9') {
            // 字符串，读取长度
            size_t len = 0;
            while (i < data.size() && data[i] >= '0' && data[i] <= '9') {
                len = len * 10 + (data[i] - '0');
                i++;
            }
            // 跳过 ':' 和字符串内容
            i += len;
        }
    }
    
    if (dict_end == 0) {
        LOG_DEBUG("Failed to find bencode dict end");
        return std::nullopt;
    }
    
    std::string dict_str(data.begin(), data.begin() + dict_end);
    auto parsed = Bencode::decode(dict_str);
    
    if (!parsed || !parsed->isDict()) {
        LOG_DEBUG("Failed to parse metadata message dict");
        return std::nullopt;
    }
    
    const auto& dict = parsed->asDict();
    MetadataMessage msg;
    
    // 解析消息类型
    auto type_it = dict.find(extension::kKeyMsgType);
    if (type_it == dict.end() || !type_it->second.isInt()) {
        LOG_DEBUG("Missing or invalid msg_type");
        return std::nullopt;
    }
    msg.type = static_cast<MetadataMessageType>(type_it->second.asInt());
    
    // 解析块索引
    auto piece_it = dict.find(extension::kKeyPiece);
    if (piece_it == dict.end() || !piece_it->second.isInt()) {
        LOG_DEBUG("Missing or invalid piece");
        return std::nullopt;
    }
    msg.piece_index = static_cast<uint32_t>(piece_it->second.asInt());
    
    // 对于 Data 消息，解析 total_size 和数据
    if (msg.type == MetadataMessageType::Data) {
        auto size_it = dict.find(extension::kKeyTotalSize);
        if (size_it != dict.end() && size_it->second.isInt()) {
            msg.total_size = static_cast<size_t>(size_it->second.asInt());
        }
        
        // 数据在字典之后
        if (dict_end < data.size()) {
            msg.data.assign(data.begin() + dict_end, data.end());
        }
    }
    
    return msg;
}

// ============================================================================
// 辅助函数
// ============================================================================

size_t MetadataExtension::calculatePieceCount(size_t metadata_size) {
    if (metadata_size == 0) return 0;
    return (metadata_size + extension::kMetadataBlockSize - 1) / extension::kMetadataBlockSize;
}

size_t MetadataExtension::calculatePieceSize(uint32_t piece_index, size_t metadata_size) {
    if (metadata_size == 0) return 0;
    
    size_t piece_count = calculatePieceCount(metadata_size);
    if (piece_index >= piece_count) return 0;
    
    // 最后一块可能小于 16KB
    if (piece_index == piece_count - 1) {
        size_t remaining = metadata_size % extension::kMetadataBlockSize;
        return remaining == 0 ? extension::kMetadataBlockSize : remaining;
    }
    
    return extension::kMetadataBlockSize;
}

std::optional<TorrentMetadata> MetadataExtension::parseTorrentMetadata(
    const std::vector<uint8_t>& data,
    const InfoHash& expected_hash
) {
    // 验证 hash
    auto computed_hash = utils::sha1(data);
    if (std::memcmp(computed_hash.data(), expected_hash.bytes().data(), 
                    extension::kSha1Size) != 0) {
        LOG_WARNING("Metadata hash mismatch!");
        return std::nullopt;
    }
    
    LOG_INFO("Metadata hash verified successfully");
    
    // 解析 bencode
    std::string data_str(data.begin(), data.end());
    auto parsed = Bencode::decode(data_str);
    
    if (!parsed || !parsed->isDict()) {
        LOG_WARNING("Failed to parse metadata as bencode dict");
        return std::nullopt;
    }
    
    const auto& dict = parsed->asDict();
    TorrentMetadata metadata;
    metadata.raw_info = data;
    metadata.info_hash = expected_hash;
    
    // 解析 name
    auto name_it = dict.find(extension::kKeyName);
    if (name_it == dict.end() || !name_it->second.isString()) {
        LOG_WARNING("Missing or invalid 'name' field");
        return std::nullopt;
    }
    metadata.name = name_it->second.asString();
    
    // 解析 piece length
    auto pl_it = dict.find(extension::kKeyPieceLength);
    if (pl_it == dict.end() || !pl_it->second.isInt()) {
        LOG_WARNING("Missing or invalid 'piece length' field");
        return std::nullopt;
    }
    metadata.piece_length = static_cast<size_t>(pl_it->second.asInt());
    
    // 解析 pieces (SHA1 hash 列表)
    auto pieces_it = dict.find(extension::kKeyPieces);
    if (pieces_it == dict.end() || !pieces_it->second.isString()) {
        LOG_WARNING("Missing or invalid 'pieces' field");
        return std::nullopt;
    }
    
    const std::string& pieces_str = pieces_it->second.asString();
    if (pieces_str.size() % extension::kSha1Size != 0) {
        LOG_WARNING("Invalid pieces length: " + std::to_string(pieces_str.size()));
        return std::nullopt;
    }
    
    size_t piece_count = pieces_str.size() / extension::kSha1Size;
    metadata.piece_hashes.resize(piece_count);
    for (size_t i = 0; i < piece_count; ++i) {
        std::memcpy(metadata.piece_hashes[i].data(), 
                    pieces_str.data() + i * extension::kSha1Size, 
                    extension::kSha1Size);
    }
    
    // 解析文件信息
    auto length_it = dict.find(extension::kKeyLength);
    auto files_it = dict.find(extension::kKeyFiles);
    
    if (length_it != dict.end() && length_it->second.isInt()) {
        // 单文件种子
        metadata.length = static_cast<size_t>(length_it->second.asInt());
    } else if (files_it != dict.end() && files_it->second.isList()) {
        // 多文件种子
        const auto& files_list = files_it->second.asList();
        for (const auto& file_val : files_list) {
            if (!file_val.isDict()) continue;
            
            const auto& file_dict = file_val.asDict();
            TorrentMetadata::FileInfo file_info;
            
            // 文件长度
            auto flen_it = file_dict.find(extension::kKeyLength);
            if (flen_it == file_dict.end() || !flen_it->second.isInt()) continue;
            file_info.length = static_cast<size_t>(flen_it->second.asInt());
            
            // 文件路径
            auto path_it = file_dict.find(extension::kKeyPath);
            if (path_it != file_dict.end() && path_it->second.isList()) {
                const auto& path_list = path_it->second.asList();
                for (const auto& p : path_list) {
                    if (p.isString()) {
                        if (!file_info.path.empty()) {
                            file_info.path += "/";
                        }
                        file_info.path += p.asString();
                    }
                }
            }
            
            metadata.files.push_back(std::move(file_info));
        }
    } else {
        LOG_WARNING("No 'length' or 'files' field found");
        return std::nullopt;
    }
    
    LOG_INFO("Parsed torrent metadata: name=" + metadata.name +
             ", size=" + std::to_string(metadata.totalSize()) +
             ", pieces=" + std::to_string(metadata.pieceCount()));
    
    return metadata;
}

} // namespace magnet::protocols


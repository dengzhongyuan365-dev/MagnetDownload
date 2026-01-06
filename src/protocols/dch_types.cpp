#include "../../include/magnet/protocols/dch_types.h"
#include <cstring>
#include <random>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

namespace magnet::protocols {

    NodeId::NodeId():data_{}{}

    NodeId::NodeId(const ByteArray& bytes):data_(bytes){}

    NodeId NodeId::fromInfoHash(const InfoHash& hash) {
        ByteArray bytes;
        std::memcpy(bytes.data(), hash.bytes().data(), s_KNodeSize);
        return NodeId(bytes);
    }

    NodeId NodeId::random() {
        ByteArray bytes;
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint16_t> dist(0,255);
        for(auto& b: bytes) {
            b = static_cast<uint8_t>(dist(gen));
        }

        return NodeId(bytes);
    }

    NodeId NodeId::distance(const NodeId& other) const {
        ByteArray result;

        for(size_t i = 0;i<s_KNodeSize;++i) {
            result[i] = data_[i] ^ other.data_[i];
        }

        return NodeId(result);
    }

    size_t NodeId::bucketIndex() const {
        for (size_t i = 0; i < s_KNodeSize; ++i) {
            if (data_[i] != 0) {
                uint8_t byte = data_[i];
                size_t bit = 7;
                while ((byte & 0x80) == 0) {
                    byte <<= 1;
                    --bit;
                }
                return (s_KNodeSize - 1 - i) * 8 + bit;
            }
        }
        return 0;
    }

    std::string NodeId::toHex() const {
        static const char hex[] = "0123456789abcdef";
        std::string result;
        result.reserve(s_KNodeSize * 2);

        for(uint8_t b : data_) {
            result.push_back(hex[b>>4]);
            result.push_back(hex[b & 0x0f]);
        }
        return result;
    }

    std::string NodeId::toString() const {
        return std::string(reinterpret_cast<const char*>(data_.data()), s_KNodeSize);
    }

    const NodeId::ByteArray& NodeId::bytes() const {
        return data_;
    }

    int NodeId::compareDistance(const NodeId& a, const NodeId& b) const {
        // 比较 a 和 b 谁离 this 更近
        for (size_t i = 0; i < s_KNodeSize; ++i) {
            uint8_t dist_a = data_[i] ^ a.data_[i];
            uint8_t dist_b = data_[i] ^ b.data_[i];
            if (dist_a < dist_b) return -1;  // a 更近
            if (dist_a > dist_b) return 1;   // b 更近
        }
        return 0;  
    }

    int NodeId::leadingZeroBits() const {
        for (size_t i = 0; i < s_KNodeSize; ++i) {
            if (data_[i] != 0) {
                uint8_t byte = data_[i];
                int bits = 0;
                while ((byte & 0x80) == 0) {
                    byte <<= 1;
                    ++bits;
                }
                return static_cast<int>(i * 8) + bits;
            }
        }
        return 160;  
    }

    bool NodeId::isZero() const {
        for (uint8_t b : data_) {
            if (b != 0) return false;
        }
        return true;
    }

    bool NodeId::operator==(const NodeId& other) const {
        return data_ == other.data_;
    }

    bool NodeId::operator!=(const NodeId& other) const {
        return data_ != other.data_;
    }

    bool NodeId::operator<(const NodeId& other) const {
        return data_ < other.data_;
    }

    std::optional<CompactNodeInfo> CompactNodeInfo::fromBytes(const uint8_t* data, size_t len) {
        if (len < s_kCompactNodeSize) 
            return std::nullopt;
        
            CompactNodeInfo info;
            NodeId::ByteArray id_bytes;

            std::memcpy(id_bytes.data(), data, 20);
            info.id_ = NodeId(id_bytes);

            std::memcpy(&info.ip_, data + 20,4);
            std::memcpy(&info.port_, data + 24, 2);
            return info;
    }

    std::vector<CompactNodeInfo> CompactNodeInfo::parseNodes(const std::string& data) {
        std::vector<CompactNodeInfo> nodes;
        for (size_t i = 0; i + s_kCompactNodeSize <= data.size(); i += s_kCompactNodeSize) {
            auto node = fromBytes(reinterpret_cast<const uint8_t*>(data.data() + i), s_kCompactNodeSize);
            if (node) {
                nodes.push_back(*node);
            }
        }
        return nodes;
    }

    std::array<uint8_t, s_kCompactNodeSize> CompactNodeInfo::toBytes() const {
        std::array<uint8_t, s_kCompactNodeSize> result;
        std::memcpy(result.data(), id_.bytes().data(), 20);
        std::memcpy(result.data() + 20, &ip_, 4);
        std::memcpy(result.data() + 24, &port_, 2);
        return result;
    }

    DhtNode CompactNodeInfo::toDhtNode() const {
        // 将网络字节序转换为主机字节序
        uint32_t host_ip = ntohl(ip_);
        uint16_t host_port = ntohs(port_);
        
        // 转换 IP 为字符串
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), "%u.%u.%u.%u",
            (host_ip >> 24) & 0xFF,
            (host_ip >> 16) & 0xFF,
            (host_ip >> 8) & 0xFF,
            host_ip & 0xFF);
        
        return DhtNode(id_, ip_str, host_port);
    }
};
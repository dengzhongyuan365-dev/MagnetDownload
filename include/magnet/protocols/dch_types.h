#pragma once

#include "magnet_types.h"
#include "../network/network_types.h"

#include <array>
#include <string>
#include <optional>
#include <chrono>
#include <vector>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

namespace magnet::protocols {
    class NodeId {
        public:
            // 标识长度为20
            static constexpr size_t s_KNodeSize =20;
            using ByteArray = std::array<uint8_t, s_KNodeSize>;

            NodeId();

            explicit NodeId(const ByteArray& bytes);

            static NodeId random();

            // static std::optional<NodeId> fromHex(const std::string& hex);

            // 文件hash
            static NodeId fromInfoHash(const InfoHash& hash);

            std::string toHex() const;
            // 原始20字节
            std::string toString() const;
            const ByteArray& bytes() const;

            // 距离计算(XOR)
            NodeId distance(const NodeId& other) const;
            // 比较谁更接近 (返回 -1: a更近, 0: 相等, 1: b更近)
            int compareDistance(const NodeId& a, const NodeId& b) const;
            // 前导零的位数
            int leadingZeroBits() const;

            /*
            * @brief 计算距离对应的通索引（0-159）
            * 返回最高有效的位置，用于确定放到哪一个 k-bucket
            * 距离越小，索引越大（越近的节点放在高索引的桶里）
            */
            size_t bucketIndex() const;

            bool isZero() const;

            bool operator==(const NodeId& other) const;
            bool operator!=(const NodeId& other) const;
            bool operator<(const NodeId& other) const;

        private:
            ByteArray data_;

    };

    /* 
    *@struct DhtNode
    *@brief DHT 网络中的节点信息
    */

    struct DhtNode {
        NodeId id_;
        std::string ip_;
        uint16_t port_;
        int failed_queries_ = 0;
        std::chrono::steady_clock::time_point last_seen_;

        DhtNode() : port_(0), failed_queries_(0), last_seen_(std::chrono::steady_clock::now()) {}

        DhtNode(const NodeId& id, const std::string& ip, uint16_t port):
        id_(id),ip_(ip),port_(port),failed_queries_(0),last_seen_(std::chrono::steady_clock::now()){}

        bool isGood() const {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now-last_seen_);
            return elapsed.count() <15 && failed_queries_ == 0;
        }

        bool isQuestionable() const {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - last_seen_);
            return elapsed.count() >= 15 && failed_queries_ < 3;
        }

        bool isBad() const {
            return failed_queries_ >= 3;
        }

        void markResponded() {
            last_seen_ = std::chrono::steady_clock::now();
            failed_queries_ = 0;
        }
        void markFailed() {
            ++failed_queries_;
        }

        network::UdpEndpoint toEndpoint() const {
            return network::UdpEndpoint(ip_, port_);
        }
    };

    /*
    * @struct CompactNodeInfo
    * @brief DHT 协议中的紧凑节点格式
    * 格式： 20字节 nodeId + 4字节 IPV4 + 2 字节端口（网络字节序）
    */

    struct CompactNodeInfo{
        static constexpr size_t s_kCompactNodeSize = 26;

        NodeId id_;
        uint32_t ip_;
        uint16_t port_;

        /*
        * @brief 从字节数组解析
        */

        static std::optional<CompactNodeInfo> fromBytes(const uint8_t* data, size_t len);

        /*
        * @brief 从字符串解析多个节点
        */

        static std::vector<CompactNodeInfo> parseNodes(const std::string& data);

        /*
        * 转换字节数组
        */

        std::array<uint8_t, s_kCompactNodeSize> toBytes() const;

        /*
        *   @brief 转换为DhtNode
        */
        DhtNode toDhtNode() const;

    };


    /**
     * @struct CompactPeerInfo
     * @brief DHT 协议中的紧凑 Peer 格式
     * 
     * 格式：4 字节 IPv4 + 2 字节端口（网络字节序）
     */
    struct CompactPeerInfo {
        static constexpr size_t s_kCompactPeerSize = 6;  // 4 + 2
        
        uint32_t ip;    // 网络字节序
        uint16_t port;  // 网络字节序
        
        /**
         * @brief 从字节数组解析
         */
        static std::optional<CompactPeerInfo> fromBytes(const uint8_t* data, size_t len) {
            if (len < s_kCompactPeerSize) return std::nullopt;
            
            CompactPeerInfo info;
            std::memcpy(&info.ip, data, 4);
            std::memcpy(&info.port, data + 4, 2);
            return info;
        }
        
        /**
         * @brief 从字符串解析多个 Peer
         */
        static std::vector<CompactPeerInfo> parsePeers(const std::string& data) {
            std::vector<CompactPeerInfo> peers;
            for (size_t i = 0; i + s_kCompactPeerSize <= data.size(); i += s_kCompactPeerSize) {
                auto peer = fromBytes(reinterpret_cast<const uint8_t*>(data.data() + i), s_kCompactPeerSize);
                if (peer) {
                    peers.push_back(*peer);
                }
            }
            return peers;
        }
        
        /**
         * @brief 获取 IP 字符串
         */
        std::string ipString() const {
            uint32_t host_ip = ntohl(ip);
            char ip_str[16];
            snprintf(ip_str, sizeof(ip_str), "%u.%u.%u.%u",
                (host_ip >> 24) & 0xFF,
                (host_ip >> 16) & 0xFF,
                (host_ip >> 8) & 0xFF,
                host_ip & 0xFF);
            return ip_str;
        }
        
        /**
         * @brief 获取端口（主机字节序）
         */
        uint16_t hostPort() const {
            return ntohs(port);
        }
    };

    // ============================================================================
    // DHT 消息类型枚举
    // ============================================================================

    enum class DhtMessageType {
        Query,      // 查询 (y = "q")
        Response,   // 响应 (y = "r")
        Error       // 错误 (y = "e")
    };

    enum class DhtQueryType {
        Ping,           // ping
        FindNode,       // find_node
        GetPeers,       // get_peers
        AnnouncePeer    // announce_peer
    };

    // ============================================================================
    // DHT 错误码
    // ============================================================================

    enum class DhtErrorCode {
        GENERIC = 201,
        SERVER = 202,
        PROTOCOL = 203,
        METHOD_UNKNOWN = 204
    };
    
};
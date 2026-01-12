#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace magnet::network {

// ============================================================================
// 网络端点（IP 地址 + 端口号）
// ============================================================================

/**
 * @brief 网络端点（IP 地址 + 端口号）
 * 
 * 用于标识网络中的一个通信端点，适用于 TCP 和 UDP
 */
struct Endpoint {
    std::string ip;   // IP 地址（IPv4 或 IPv6 字符串格式）
    uint16_t port;    // 端口号（0-65535）

    Endpoint() : port(0) {}

    Endpoint(std::string ip_, uint16_t port_) 
        : ip(std::move(ip_)), port(port_) {}
    
    std::string toString() const {
        return ip + ":" + std::to_string(port);
    }

    bool isValid() const {
        return !ip.empty() && port != 0;
    }
    
    bool operator==(const Endpoint& other) const {
        return ip == other.ip && port == other.port;
    }
    
    bool operator!=(const Endpoint& other) const {
        return !(*this == other);
    }
};

// 类型别名，保持语义清晰
using UdpEndpoint = Endpoint;
using TcpEndpoint = Endpoint;

// ============================================================================
// UDP 消息
// ============================================================================

/**
 * @brief UDP 消息（数据 + 来源地址）
 * 
 * 封装接收到的 UDP 数据包，包含：
 * - 消息数据（字节数组）
 * - 发送方的地址信息
 */
struct UdpMessage {
    std::vector<uint8_t> data;
    Endpoint remote_endpoint;

    UdpMessage() = default;

    UdpMessage(std::vector<uint8_t> data_, Endpoint endpoint_)
        : data(std::move(data_)), remote_endpoint(std::move(endpoint_)) {}

    size_t size() const {
        return data.size(); 
    }

    bool empty() const {
        return data.empty();
    }
};

} // namespace magnet::network

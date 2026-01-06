#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace magnet::network {

    /*
        * @brief UDP 端点（IP 地址 + 端口号）
        * 
        * 用于标识网络中的一个 UDP 通信端点
    */
    struct UdpEndpoint {
        std::string ip; // ip 地址（IPv4 或 IPv6 字符串格式）
        uint16_t port;  // 端口号（0-65535）

        UdpEndpoint() : port(0) {}

        UdpEndpoint(std::string ip_, uint16_t port_) 
            : ip(std::move(ip_)), port(port_) {}
        
        std::string toString() const {
            return ip + ":" + std::to_string(port);
        }

        bool isValid() const {
            return !ip.empty() && port != 0;
        }
    };

    /*
        * @brief UDP 消息（数据 + 来源地址）
        * 
        * 封装接收到的 UDP 数据包，包含：
        * - 消息数据（字节数组）
        * - 发送方的地址信息
    */
    struct UdpMessage {
        std::vector<uint8_t> data;
        UdpEndpoint remote_endpoint;

        UdpMessage() =default;

        UdpMessage(std::vector<uint8_t> data_, UdpEndpoint endpoint_)
            : data(std::move(data_)), remote_endpoint(std::move(endpoint_)) {}

        size_t size() const {
            return data.size(); 
        }

        bool empty() const {
            return data.empty();
        }

    };
};
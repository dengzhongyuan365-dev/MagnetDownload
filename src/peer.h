#pragma once

#include <string>
#include <array>
#include <asio/ip/tcp.hpp>

namespace bt {

// 表示BitTorrent网络中的一个节点
class Peer {
public:
    Peer() : port_(0) {}
    
    Peer(const std::string& ip, uint16_t port) : ip_(ip), port_(port) {}
    
    Peer(const std::string& ip, uint16_t port, const std::string& peer_id)
        : ip_(ip), port_(port), peer_id_(peer_id) {}
    
    const std::string& getIp() const { return ip_; }
    uint16_t getPort() const { return port_; }
    const std::string& getPeerId() const { return peer_id_; }
    
    bool operator==(const Peer& other) const {
        return ip_ == other.ip_ && port_ == other.port_;
    }
    
    bool operator!=(const Peer& other) const {
        return !(*this == other);
    }
    
    asio::ip::tcp::endpoint getEndpoint() const {
        return asio::ip::tcp::endpoint(asio::ip::address::from_string(ip_), port_);
    }
    
    std::string toString() const {
        return ip_ + ":" + std::to_string(port_);
    }
    
private:
    std::string ip_;
    uint16_t port_;
    std::string peer_id_;
};

} // namespace bt 
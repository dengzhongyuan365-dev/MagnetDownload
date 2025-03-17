#include "tracker.h"
#include <sstream>
#include <iomanip>
#include <asio/ip/tcp.hpp>
#include <asio/buffer.hpp>
#include <asio/connect.hpp>
#include <asio/streambuf.hpp>
#include <asio/write.hpp>
#include <asio/read_until.hpp>
#include <asio/read.hpp>

namespace bt {

Tracker::Tracker(asio::io_context& io_context, const std::string& tracker_url)
    : io_context_(io_context), tracker_url_(tracker_url) {
}

std::future<TrackerResponse> Tracker::announce(const TrackerRequest& request) {
    return std::async(std::launch::async, [this, request]() {
        try {
            std::string url = buildRequestUrl(request);
            std::string response = httpGet(url);
            return parseResponse(response);
        } catch (const std::exception& e) {
            TrackerResponse resp;
            resp.failure_reason = e.what();
            return resp;
        }
    });
}

std::string Tracker::buildRequestUrl(const TrackerRequest& request) {
    std::ostringstream url;
    url << tracker_url_ << "?";
    
    // URL编码函数
    auto urlEncode = [](const std::string& data) {
        std::ostringstream escaped;
        escaped.fill('0');
        escaped << std::hex;
        
        for (char c : data) {
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                escaped << c;
            } else {
                escaped << std::uppercase;
                escaped << '%' << std::setw(2) << int((unsigned char)c);
                escaped << std::nouppercase;
            }
        }
        
        return escaped.str();
    };
    
    // 二进制转十六进制函数
    auto binToHex = [](const std::string& bin) {
        std::ostringstream hex;
        hex << std::hex << std::setfill('0');
        for (unsigned char c : bin) {
            hex << std::setw(2) << static_cast<int>(c);
        }
        return hex.str();
    };
    
    // 添加info_hash参数 (info_hash应该是20字节二进制，我们需要URL编码)
    url << "info_hash=" << urlEncode(request.info_hash);
    
    // 添加peer_id参数
    url << "&peer_id=" << urlEncode(request.peer_id);
    
    // 添加其他参数
    url << "&port=" << request.port;
    url << "&uploaded=" << request.uploaded;
    url << "&downloaded=" << request.downloaded;
    url << "&left=" << request.left;
    url << "&compact=" << (request.compact ? "1" : "0");
    url << "&no_peer_id=" << (request.no_peer_id ? "1" : "0");
    
    if (!request.event.empty()) {
        url << "&event=" << request.event;
    }
    
    return url.str();
}

std::string Tracker::httpGet(const std::string& url) {
    // 解析URL
    std::string protocol, host, path;
    
    size_t protocolEnd = url.find("://");
    if (protocolEnd != std::string::npos) {
        protocol = url.substr(0, protocolEnd);
        size_t hostStart = protocolEnd + 3;
        size_t pathStart = url.find("/", hostStart);
        if (pathStart != std::string::npos) {
            host = url.substr(hostStart, pathStart - hostStart);
            path = url.substr(pathStart);
        } else {
            host = url.substr(hostStart);
            path = "/";
        }
    } else {
        throw std::runtime_error("Invalid URL format");
    }
    
    // 确保我们支持的协议
    if (protocol != "http") {
        throw std::runtime_error("Unsupported protocol: " + protocol);
    }
    
    // 分离主机名和端口
    std::string hostname;
    std::string port = "80"; // 默认HTTP端口
    
    size_t portPos = host.find(":");
    if (portPos != std::string::npos) {
        hostname = host.substr(0, portPos);
        port = host.substr(portPos + 1);
    } else {
        hostname = host;
    }
    
    // 创建socket并连接
    asio::ip::tcp::resolver resolver(io_context_);
    asio::ip::tcp::resolver::results_type endpoints = resolver.resolve(hostname, port);
    
    asio::ip::tcp::socket socket(io_context_);
    asio::connect(socket, endpoints);
    
    // 创建HTTP请求
    std::ostringstream request_stream;
    request_stream << "GET " << path << " HTTP/1.1\r\n";
    request_stream << "Host: " << host << "\r\n";
    request_stream << "Accept: */*\r\n";
    request_stream << "Connection: close\r\n\r\n";
    
    std::string request = request_stream.str();
    
    // 发送请求
    asio::write(socket, asio::buffer(request));
    
    // 读取响应
    asio::streambuf response;
    asio::read_until(socket, response, "\r\n\r\n");
    
    // 处理响应头
    std::istream response_stream(&response);
    std::string http_version;
    response_stream >> http_version;
    
    unsigned int status_code;
    response_stream >> status_code;
    
    std::string status_message;
    std::getline(response_stream, status_message);
    
    if (status_code != 200) {
        throw std::runtime_error("HTTP Error: " + std::to_string(status_code) + " " + status_message);
    }
    
    // 跳过响应头
    std::string header;
    while (std::getline(response_stream, header) && header != "\r");
    
    // 读取响应体
    std::stringstream response_body;
    if (response.size() > 0) {
        response_body << &response;
    }
    
    // 继续读取可能的剩余数据
    asio::error_code error;
    while (asio::read(socket, response, asio::transfer_at_least(1), error)) {
        response_body << &response;
    }
    
    if (error != asio::error::eof) {
        throw std::runtime_error("Read error: " + error.message());
    }
    
    socket.close();
    
    return response_body.str();
}

TrackerResponse Tracker::parseResponse(const std::string& response) {
    TrackerResponse result;
    
    bencode::BencodeParser parser;
    auto root = parser.parse(response);
    
    if (!root || root->getType() != bencode::BencodeValue::Type::DICTIONARY) {
        throw std::runtime_error("Invalid tracker response format");
    }
    
    const auto& dict = root->getDictionary();
    
    // 检查是否有失败信息
    auto it = dict.find("failure reason");
    if (it != dict.end()) {
        result.failure_reason = it->second->getString();
        return result;
    }
    
    // 检查是否有警告信息
    it = dict.find("warning message");
    if (it != dict.end()) {
        result.warning_message = it->second->getString();
    }
    
    // 解析间隔
    it = dict.find("interval");
    if (it != dict.end()) {
        result.interval = static_cast<int>(it->second->getInteger());
    }
    
    // 解析最小间隔
    it = dict.find("min interval");
    if (it != dict.end()) {
        result.min_interval = static_cast<int>(it->second->getInteger());
    }
    
    // 解析tracker id
    it = dict.find("tracker id");
    if (it != dict.end()) {
        result.tracker_id = it->second->getString();
    }
    
    // 解析完整种子数
    it = dict.find("complete");
    if (it != dict.end()) {
        result.complete = static_cast<int>(it->second->getInteger());
    }
    
    // 解析不完整种子数
    it = dict.find("incomplete");
    if (it != dict.end()) {
        result.incomplete = static_cast<int>(it->second->getInteger());
    }
    
    // 解析节点列表
    it = dict.find("peers");
    if (it != dict.end()) {
        if (it->second->getType() == bencode::BencodeValue::Type::STRING) {
            // 紧凑格式
            result.peers = parseCompactPeers(it->second->getString());
        } else if (it->second->getType() == bencode::BencodeValue::Type::LIST) {
            // 字典格式
            result.peers = parseDictionaryPeers(it->second->getList());
        }
    }
    
    return result;
}

std::vector<Peer> Tracker::parseCompactPeers(const std::string& compact_peers) {
    std::vector<Peer> peers;
    
    // 紧凑格式的peers是6字节一组：4字节IP地址 + 2字节端口
    if (compact_peers.size() % 6 != 0) {
        throw std::runtime_error("Invalid compact peers format");
    }
    
    for (size_t i = 0; i < compact_peers.size(); i += 6) {
        // 解析IP地址
        std::ostringstream ip;
        ip << static_cast<int>(static_cast<unsigned char>(compact_peers[i])) << "."
           << static_cast<int>(static_cast<unsigned char>(compact_peers[i+1])) << "."
           << static_cast<int>(static_cast<unsigned char>(compact_peers[i+2])) << "."
           << static_cast<int>(static_cast<unsigned char>(compact_peers[i+3]));
        
        // 解析端口
        uint16_t port = (static_cast<unsigned char>(compact_peers[i+4]) << 8) 
                       | static_cast<unsigned char>(compact_peers[i+5]);
        
        peers.emplace_back(ip.str(), port);
    }
    
    return peers;
}

std::vector<Peer> Tracker::parseDictionaryPeers(const bencode::List& peers_list) {
    std::vector<Peer> peers;
    
    for (const auto& peer_value : peers_list) {
        if (peer_value->getType() != bencode::BencodeValue::Type::DICTIONARY) {
            continue;
        }
        
        const auto& peer_dict = peer_value->getDictionary();
        
        auto ip_it = peer_dict.find("ip");
        auto port_it = peer_dict.find("port");
        auto id_it = peer_dict.find("peer id");
        
        if (ip_it != peer_dict.end() && port_it != peer_dict.end()) {
            std::string ip = ip_it->second->getString();
            uint16_t port = static_cast<uint16_t>(port_it->second->getInteger());
            
            if (id_it != peer_dict.end()) {
                std::string peer_id = id_it->second->getString();
                peers.emplace_back(ip, port, peer_id);
            } else {
                peers.emplace_back(ip, port);
            }
        }
    }
    
    return peers;
}

} // namespace bt 
#include "magnet/protocols/tracker_client.h"
#include "magnet/protocols/bencode.h"
#include "magnet/utils/logger.h"

#include <sstream>
#include <iomanip>
#include <regex>

namespace magnet::protocols {

#define LOG_DEBUG(msg) magnet::utils::Logger::instance().debug(std::string("[Tracker] ") + msg)
#define LOG_INFO(msg) magnet::utils::Logger::instance().info(std::string("[Tracker] ") + msg)
#define LOG_WARN(msg) magnet::utils::Logger::instance().warn(std::string("[Tracker] ") + msg)
#define LOG_ERROR(msg) magnet::utils::Logger::instance().error(std::string("[Tracker] ") + msg)

// ============================================================================
// 构造和析构
// ============================================================================

TrackerClient::TrackerClient(asio::io_context& io_context,
                             const InfoHash& info_hash,
                             const std::string& peer_id,
                             uint16_t listen_port)
    : io_context_(io_context)
    , info_hash_(info_hash)
    , peer_id_(peer_id)
    , listen_port_(listen_port)
    , resolver_(io_context)
    , http_socket_(io_context)
    , timeout_timer_(io_context)
{
    LOG_DEBUG("TrackerClient created");
}

TrackerClient::~TrackerClient() {
    cancel();
    LOG_DEBUG("TrackerClient destroyed");
}

// ============================================================================
// 公共接口
// ============================================================================

void TrackerClient::announce(const std::string& tracker_url,
                             uint64_t downloaded,
                             uint64_t uploaded,
                             uint64_t left,
                             TrackerCallback callback) {
    if (cancelled_) {
        return;
    }
    
    LOG_INFO("Announcing to: " + tracker_url);
    
    if (tracker_url.find("http://") == 0 || tracker_url.find("https://") == 0) {
        announceHttp(tracker_url, downloaded, uploaded, left, callback);
    } else if (tracker_url.find("udp://") == 0) {
        announceUdp(tracker_url, downloaded, uploaded, left, callback);
    } else {
        LOG_WARN("Unsupported tracker protocol: " + tracker_url);
        if (callback) {
            TrackerResponse resp;
            resp.failure_reason = "Unsupported protocol";
            callback(resp);
        }
    }
}

void TrackerClient::announceAll(const std::vector<std::string>& tracker_urls,
                                uint64_t downloaded,
                                uint64_t uploaded,
                                uint64_t left,
                                TrackerCallback callback) {
    for (const auto& url : tracker_urls) {
        announce(url, downloaded, uploaded, left, callback);
    }
}

void TrackerClient::cancel() {
    cancelled_ = true;
    asio::error_code ec;
    timeout_timer_.cancel();
    resolver_.cancel();
    if (http_socket_.is_open()) {
        http_socket_.cancel(ec);
        http_socket_.close(ec);
    }
}

// ============================================================================
// HTTP Tracker
// ============================================================================

void TrackerClient::announceHttp(const std::string& tracker_url,
                                 uint64_t downloaded,
                                 uint64_t uploaded,
                                 uint64_t left,
                                 TrackerCallback callback) {
    auto self = shared_from_this();
    
    // 构建完整 URL
    std::string full_url = buildHttpUrl(tracker_url, downloaded, uploaded, left);
    LOG_DEBUG("Full URL: " + full_url);
    
    // 解析 URL
    std::regex url_regex(R"(https?://([^:/]+)(?::(\d+))?(/.*))");
    std::smatch match;
    
    if (!std::regex_match(full_url, match, url_regex)) {
        LOG_ERROR("Invalid tracker URL format: " + full_url);
        if (callback) {
            TrackerResponse resp;
            resp.failure_reason = "Invalid URL format";
            callback(resp);
        }
        return;
    }
    
    std::string host = match[1].str();
    std::string port = match[2].matched ? match[2].str() : "80";
    std::string path = match[3].str();
    
    // 设置超时
    timeout_timer_.expires_after(std::chrono::seconds(15));
    timeout_timer_.async_wait([self](const asio::error_code& ec) {
        if (!ec) {
            LOG_WARN("Tracker request timeout");
            asio::error_code ignored;
            self->http_socket_.cancel(ignored);
        }
    });
    
    // DNS 解析
    resolver_.async_resolve(host, port,
        [self, host, path, callback](const asio::error_code& ec,
                                     asio::ip::tcp::resolver::results_type results) {
            if (ec) {
                LOG_ERROR("DNS resolve failed: " + ec.message());
                if (callback) {
                    TrackerResponse resp;
                    resp.failure_reason = "DNS resolve failed: " + ec.message();
                    callback(resp);
                }
                return;
            }
            
            // 连接
            asio::async_connect(self->http_socket_, results,
                [self, host, path, callback](const asio::error_code& ec,
                                             const asio::ip::tcp::endpoint&) {
                    if (ec) {
                        LOG_ERROR("Connect failed: " + ec.message());
                        if (callback) {
                            TrackerResponse resp;
                            resp.failure_reason = "Connect failed: " + ec.message();
                            callback(resp);
                        }
                        return;
                    }
                    
                    // 构建 HTTP 请求
                    std::ostringstream request;
                    request << "GET " << path << " HTTP/1.1\r\n"
                            << "Host: " << host << "\r\n"
                            << "User-Agent: MagnetDownload/1.0\r\n"
                            << "Accept: */*\r\n"
                            << "Connection: close\r\n"
                            << "\r\n";
                    
                    auto request_str = std::make_shared<std::string>(request.str());
                    
                    // 发送请求
                    asio::async_write(self->http_socket_, asio::buffer(*request_str),
                        [self, request_str, callback](const asio::error_code& ec, size_t) {
                            if (ec) {
                                LOG_ERROR("Send failed: " + ec.message());
                                if (callback) {
                                    TrackerResponse resp;
                                    resp.failure_reason = "Send failed";
                                    callback(resp);
                                }
                                return;
                            }
                            
                            // 读取响应
                            self->response_buffer_.clear();
                            self->response_buffer_.resize(65536);
                            
                            self->http_socket_.async_read_some(
                                asio::buffer(self->response_buffer_),
                                [self, callback](const asio::error_code& ec, size_t bytes) {
                                    self->timeout_timer_.cancel();
                                    
                                    if (ec && ec != asio::error::eof) {
                                        LOG_ERROR("Read failed: " + ec.message());
                                        if (callback) {
                                            TrackerResponse resp;
                                            resp.failure_reason = "Read failed";
                                            callback(resp);
                                        }
                                        return;
                                    }
                                    
                                    self->response_buffer_.resize(bytes);
                                    
                                    // 关闭连接
                                    asio::error_code ignored;
                                    self->http_socket_.close(ignored);
                                    
                                    // 解析响应
                                    auto response = self->parseHttpResponse(self->response_buffer_);
                                    
                                    if (callback) {
                                        callback(response);
                                    }
                                });
                        });
                });
        });
}

void TrackerClient::announceUdp(const std::string& tracker_url,
                                uint64_t /*downloaded*/,
                                uint64_t /*uploaded*/,
                                uint64_t /*left*/,
                                TrackerCallback callback) {
    // UDP Tracker 实现较复杂，暂时返回失败
    // 大多数 Tracker 都支持 HTTP
    LOG_WARN("UDP tracker not yet implemented: " + tracker_url);
    if (callback) {
        TrackerResponse resp;
        resp.failure_reason = "UDP tracker not implemented";
        callback(resp);
    }
}

// ============================================================================
// URL 构建
// ============================================================================

std::string TrackerClient::buildHttpUrl(const std::string& base_url,
                                        uint64_t downloaded,
                                        uint64_t uploaded,
                                        uint64_t left) {
    std::ostringstream url;
    url << base_url;
    
    // 添加分隔符
    if (base_url.find('?') == std::string::npos) {
        url << '?';
    } else {
        url << '&';
    }
    
    // info_hash（URL 编码的 20 字节）
    url << "info_hash=" << urlEncode(std::string(
        reinterpret_cast<const char*>(info_hash_.bytes().data()), 20));
    
    // peer_id
    url << "&peer_id=" << urlEncode(peer_id_);
    
    // 端口
    url << "&port=" << listen_port_;
    
    // 统计信息
    url << "&downloaded=" << downloaded;
    url << "&uploaded=" << uploaded;
    url << "&left=" << left;
    
    // 其他参数
    url << "&compact=1";  // 紧凑格式
    url << "&numwant=200";  // 请求更多 peers
    url << "&event=started";
    
    return url.str();
}

std::string TrackerClient::urlEncode(const std::string& str) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    
    for (unsigned char c : str) {
        // 保持字母数字和一些特殊字符
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::setw(2) << std::uppercase << static_cast<int>(c);
        }
    }
    
    return escaped.str();
}

// ============================================================================
// 响应解析
// ============================================================================

TrackerResponse TrackerClient::parseHttpResponse(const std::vector<uint8_t>& data) {
    TrackerResponse response;
    
    // 将数据转换为字符串
    std::string data_str(data.begin(), data.end());
    
    // 查找 HTTP 响应体（空行后的内容）
    size_t body_start = data_str.find("\r\n\r\n");
    if (body_start == std::string::npos) {
        LOG_ERROR("Invalid HTTP response: no body separator");
        response.failure_reason = "Invalid HTTP response";
        return response;
    }
    body_start += 4;
    
    // 检查 HTTP 状态码
    if (data_str.find("200 OK") == std::string::npos &&
        data_str.find("200 ok") == std::string::npos) {
        LOG_ERROR("HTTP request failed: " + data_str.substr(0, 50));
        response.failure_reason = "HTTP error";
        return response;
    }
    
    // 解析 Bencode 响应体
    std::string body = data_str.substr(body_start);
    LOG_DEBUG("Response body size: " + std::to_string(body.size()));
    
    auto parsed = Bencode::decode(body);
    if (!parsed) {
        LOG_ERROR("Failed to parse Bencode response");
        response.failure_reason = "Bencode parse error";
        return response;
    }
    
    if (!parsed->isDict()) {
        LOG_ERROR("Response is not a dictionary");
        response.failure_reason = "Invalid response format";
        return response;
    }
    
    const auto& dict = parsed->asDict();
    
    // 检查失败原因
    if (dict.count("failure reason")) {
        const auto& reason_val = dict.at("failure reason");
        if (reason_val.isString()) {
            response.failure_reason = reason_val.asString();
            LOG_ERROR("Tracker error: " + response.failure_reason);
            return response;
        }
    }
    
    response.success = true;
    
    // 解析 interval
    if (dict.count("interval")) {
        const auto& interval_val = dict.at("interval");
        if (interval_val.isInt()) {
            response.interval = static_cast<int>(interval_val.asInt());
        }
    }
    
    // 解析 complete/incomplete
    if (dict.count("complete")) {
        const auto& complete_val = dict.at("complete");
        if (complete_val.isInt()) {
            response.complete = static_cast<int>(complete_val.asInt());
        }
    }
    if (dict.count("incomplete")) {
        const auto& incomplete_val = dict.at("incomplete");
        if (incomplete_val.isInt()) {
            response.incomplete = static_cast<int>(incomplete_val.asInt());
        }
    }
    
    // 解析 peers（紧凑格式：每 6 字节一个 peer）
    if (dict.count("peers")) {
        const auto& peers_val = dict.at("peers");
        if (peers_val.isString()) {
            response = parseCompactPeers(peers_val.asString());
            response.success = true;
            LOG_INFO("Got " + std::to_string(response.peers.size()) + " peers from tracker");
        }
    }
    
    return response;
}

TrackerResponse TrackerClient::parseCompactPeers(const std::string& peers_data) {
    TrackerResponse response;
    
    // 紧凑格式：每 6 字节 = 4 字节 IP + 2 字节端口
    if (peers_data.size() % 6 != 0) {
        LOG_WARN("Invalid compact peers format");
        return response;
    }
    
    size_t peer_count = peers_data.size() / 6;
    response.peers.reserve(peer_count);
    
    for (size_t i = 0; i < peers_data.size(); i += 6) {
        // 解析 IP（4 字节，网络字节序）
        uint8_t ip1 = static_cast<uint8_t>(peers_data[i]);
        uint8_t ip2 = static_cast<uint8_t>(peers_data[i + 1]);
        uint8_t ip3 = static_cast<uint8_t>(peers_data[i + 2]);
        uint8_t ip4 = static_cast<uint8_t>(peers_data[i + 3]);
        
        // 解析端口（2 字节，大端序）
        uint16_t port = (static_cast<uint8_t>(peers_data[i + 4]) << 8) |
                        static_cast<uint8_t>(peers_data[i + 5]);
        
        // 构建 IP 字符串
        std::ostringstream ip;
        ip << static_cast<int>(ip1) << "."
           << static_cast<int>(ip2) << "."
           << static_cast<int>(ip3) << "."
           << static_cast<int>(ip4);
        
        network::TcpEndpoint endpoint;
        endpoint.ip = ip.str();
        endpoint.port = port;
        
        response.peers.push_back(endpoint);
    }
    
    return response;
}

} // namespace magnet::protocols


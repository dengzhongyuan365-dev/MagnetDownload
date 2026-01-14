#pragma once

#include "magnet_types.h"
#include "../network/network_types.h"

#include <asio.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <chrono>

namespace magnet::protocols {

// ============================================================================
// Tracker 响应结构
// ============================================================================

/**
 * @struct TrackerResponse
 * @brief Tracker 服务器的响应数据
 */
struct TrackerResponse {
    bool success{false};
    std::string failure_reason;
    
    int interval{1800};                  // 下次请求间隔（秒）
    int min_interval{60};                // 最小请求间隔
    std::string tracker_id;              // Tracker 标识
    int complete{0};                     // Seeders 数量
    int incomplete{0};                   // Leechers 数量
    
    std::vector<network::TcpEndpoint> peers;  // Peer 列表
};

// ============================================================================
// 回调类型
// ============================================================================

using TrackerCallback = std::function<void(const TrackerResponse& response)>;

// ============================================================================
// TrackerClient 类
// ============================================================================

/**
 * @class TrackerClient
 * @brief HTTP/UDP Tracker 客户端
 * 
 * 支持从 Tracker 服务器获取 Peer 列表
 */
class TrackerClient : public std::enable_shared_from_this<TrackerClient> {
public:
    /**
     * @brief 构造函数
     */
    TrackerClient(asio::io_context& io_context,
                  const InfoHash& info_hash,
                  const std::string& peer_id,
                  uint16_t listen_port);
    
    ~TrackerClient();
    
    // 禁止拷贝
    TrackerClient(const TrackerClient&) = delete;
    TrackerClient& operator=(const TrackerClient&) = delete;
    
    /**
     * @brief 向 Tracker 发送 announce 请求
     * @param tracker_url Tracker URL (http:// 或 udp://)
     * @param downloaded 已下载字节数
     * @param uploaded 已上传字节数
     * @param left 剩余字节数
     * @param callback 回调函数
     */
    void announce(const std::string& tracker_url,
                  uint64_t downloaded,
                  uint64_t uploaded,
                  uint64_t left,
                  TrackerCallback callback);
    
    /**
     * @brief 向多个 Tracker 并行发送请求
     */
    void announceAll(const std::vector<std::string>& tracker_urls,
                     uint64_t downloaded,
                     uint64_t uploaded,
                     uint64_t left,
                     TrackerCallback callback);
    
    /**
     * @brief 取消所有请求
     */
    void cancel();

private:
    void announceHttp(const std::string& tracker_url,
                      uint64_t downloaded,
                      uint64_t uploaded,
                      uint64_t left,
                      TrackerCallback callback);
    
    void announceUdp(const std::string& tracker_url,
                     uint64_t downloaded,
                     uint64_t uploaded,
                     uint64_t left,
                     TrackerCallback callback);
    
    std::string buildHttpUrl(const std::string& base_url,
                             uint64_t downloaded,
                             uint64_t uploaded,
                             uint64_t left);
    
    TrackerResponse parseHttpResponse(const std::vector<uint8_t>& data);
    TrackerResponse parseCompactPeers(const std::string& peers_data);
    
    static std::string urlEncode(const std::string& str);

private:
    asio::io_context& io_context_;
    InfoHash info_hash_;
    std::string peer_id_;
    uint16_t listen_port_;
    
    // HTTP 连接
    asio::ip::tcp::resolver resolver_;
    asio::ip::tcp::socket http_socket_;
    asio::steady_timer timeout_timer_;
    
    std::vector<uint8_t> response_buffer_;
    bool cancelled_{false};
};

} // namespace magnet::protocols


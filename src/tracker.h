#pragma once

#include <string>
#include <vector>
#include <memory>
#include <future>
#include <asio.hpp>
#include "bencode.h"
#include "peer.h"

namespace bt {

struct TrackerRequest {
    std::string info_hash;      // 20字节的infohash (必须是二进制形式，非十六进制字符串)
    std::string peer_id;        // 20字节的peer ID
    uint16_t port;              // 监听端口
    uint64_t uploaded;          // 已上传字节数
    uint64_t downloaded;        // 已下载字节数
    uint64_t left;              // 剩余字节数
    bool compact;               // 是否请求紧凑响应
    bool no_peer_id;            // 是否省略peer_id
    std::string event;          // 事件类型：started, completed, stopped
    
    TrackerRequest() : port(6881), uploaded(0), downloaded(0), left(0), 
                       compact(true), no_peer_id(true), event("started") {
        // 生成随机的peer_id
        peer_id = "-MD0001-";
        for (int i = 0; i < 12; i++) {
            peer_id += static_cast<char>(rand() % 26 + 'a');
        }
    }
};

struct TrackerResponse {
    std::string failure_reason;  // 失败原因
    std::string warning_message; // 警告信息
    int interval;                // 重新请求间隔 (秒)
    int min_interval;            // 最小重新请求间隔 (秒)
    std::string tracker_id;      // Tracker ID
    int complete;                // 完整种子数
    int incomplete;              // 不完整种子数
    std::vector<Peer> peers;     // 节点列表
    
    bool success() const { return failure_reason.empty(); }
};

class Tracker {
public:
    Tracker(asio::io_context& io_context, const std::string& tracker_url);
    
    // 向Tracker发送请求
    std::future<TrackerResponse> announce(const TrackerRequest& request);
    
private:
    asio::io_context& io_context_;
    std::string tracker_url_;
    
    // 解析HTTP响应
    TrackerResponse parseResponse(const std::string& response);
    
    // 解析紧凑格式的Peers列表
    std::vector<Peer> parseCompactPeers(const std::string& compact_peers);
    
    // 解析字典格式的Peers列表
    std::vector<Peer> parseDictionaryPeers(const bencode::List& peers_list);
    
    // 构建HTTP GET请求URL
    std::string buildRequestUrl(const TrackerRequest& request);
    
    // 进行HTTP请求
    std::string httpGet(const std::string& url);
};

} // namespace bt 
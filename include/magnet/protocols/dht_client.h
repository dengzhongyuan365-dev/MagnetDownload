#pragma once

#include "dht_message.h"
#include "routing_table.h"
#include "query_manager.h"
#include "dch_types.h"
#include "magnet_types.h"
#include "../network/udp_client.h"

#include <asio.hpp>
#include <functional>
#include <memory>
#include <map>
#include <set>
#include <mutex>
#include <atomic>
#include <chrono>

namespace magnet::protocols {

// ============================================================================
// 回调类型定义
// ============================================================================

/**
 * @brief Peer 发现回调（每找到一个 Peer 调用一次）
 */
using PeerCallback = std::function<void(const PeerInfo& peer)>;

/**
 * @brief 查找完成回调
 * @param success 是否成功找到至少一个 Peer
 * @param peers 找到的所有 Peer
 */
using LookupCompleteCallback = std::function<void(bool success, const std::vector<PeerInfo>& peers)>;

/**
 * @brief Bootstrap 完成回调
 * @param success 是否成功加入网络
 * @param node_count 路由表中的节点数
 */
using BootstrapCallback = std::function<void(bool success, size_t node_count)>;

// ============================================================================
// DhtClient 配置
// ============================================================================

struct DhtClientConfig {
    uint16_t listen_port{0};                  // UDP 监听端口（0 = 系统随机分配）
    size_t alpha{3};                          // 并发查询数（BEP-0005 推荐）
    size_t k{8};                              // 路由表桶大小 / 返回节点数
    
    std::chrono::seconds refresh_interval{900};   // 路由表刷新间隔（15分钟）
    std::chrono::seconds announce_interval{1800}; // 重新宣告间隔（30分钟）
    
    size_t max_lookup_rounds{20};            // 最大查找轮次
    
    // Bootstrap 节点
    std::vector<std::pair<std::string, uint16_t>> bootstrap_nodes{
        {"router.bittorrent.com", 6881},
        {"dht.transmissionbt.com", 6881},
        {"router.utorrent.com", 6881}
    };
    
    // QueryManager 配置
    QueryManagerConfig query_config{};
};

// ============================================================================
// DhtClient 统计信息
// ============================================================================

struct DhtClientStatistics {
    // 查找统计
    size_t lookups_started{0};       // 启动的查找数
    size_t lookups_completed{0};     // 完成的查找数
    size_t lookups_successful{0};    // 成功的查找数（找到 Peer）
    size_t peers_found{0};           // 找到的 Peer 总数
    
    // 消息统计
    size_t queries_received{0};      // 收到的查询数
    size_t responses_sent{0};        // 发送的响应数
    
    // 状态
    bool bootstrapped{false};        // 是否已加入网络
    size_t node_count{0};            // 当前路由表节点数
    
    void reset() {
        lookups_started = 0;
        lookups_completed = 0;
        lookups_successful = 0;
        peers_found = 0;
        queries_received = 0;
        responses_sent = 0;
    }
};

// ============================================================================
// LookupState - 迭代查找状态
// ============================================================================

/**
 * @brief 迭代查找状态
 * 
 * 管理一次 findPeers 或 findNode 操作的状态
 */
struct LookupState {
    std::string id;                           // 查找 ID
    InfoHash target;                          // 查找目标
    NodeId target_id;                         // 目标转换为 NodeId
    
    std::set<NodeId> queried;                 // 已查询的节点
    std::set<NodeId> pending;                 // 正在查询中的节点
    std::map<NodeId, DhtNode> candidates;     // 候选节点（按距离排序）
    
    std::vector<PeerInfo> found_peers;        // 找到的 Peers
    std::string token;                        // 用于 announce 的 token
    
    PeerCallback on_peer;                     // Peer 回调
    LookupCompleteCallback on_complete;       // 完成回调
    
    size_t alpha;                             // 并发数
    size_t max_rounds;                        // 最大轮次
    size_t current_round{0};                  // 当前轮次
    bool completed{false};                    // 是否完成
    
    std::chrono::steady_clock::time_point start_time;  // 开始时间
    
    /**
     * @brief 检查是否应该继续查找
     */
    bool shouldContinue() const {
        if (completed) return false;
        if (current_round >= max_rounds) return false;
        
        // 如果还有待查询的候选节点，继续
        for (const auto& [node_id, node] : candidates) {
            if (queried.find(node_id) == queried.end() && 
                pending.find(node_id) == pending.end()) {
                return true;
            }
        }
        return false;
    }
    
    /**
     * @brief 获取下一批要查询的节点
     */
    std::vector<DhtNode> getNextNodes(size_t count) {
        std::vector<DhtNode> result;
        
        for (const auto& [node_id, node] : candidates) {
            if (result.size() >= count) break;
            if (queried.find(node_id) == queried.end() && 
                pending.find(node_id) == pending.end()) {
                result.push_back(node);
            }
        }
        
        return result;
    }
    
    /**
     * @brief 添加新发现的节点
     */
    void addNodes(const std::vector<DhtNode>& nodes) {
        for (const auto& node : nodes) {
            // 不添加自己或已查询的节点
            if (queried.find(node.id_) == queried.end()) {
                candidates[node.id_] = node;
            }
        }
    }
    
    /**
     * @brief 记录找到的 Peer
     */
    void addPeers(const std::vector<PeerInfo>& peers) {
        for (const auto& peer : peers) {
            // 简单去重
            bool exists = false;
            for (const auto& existing : found_peers) {
                if (existing.ip == peer.ip && existing.port == peer.port) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                found_peers.push_back(peer);
            }
        }
    }
};

// ============================================================================
// DhtClient 类
// ============================================================================

/**
 * @class DhtClient
 * @brief DHT 主控制器
 * 
 * 整合所有 DHT 子模块，提供完整的 DHT 功能：
 * - bootstrap: 加入 DHT 网络
 * - findPeers: 根据 InfoHash 查找 Peer
 * - announce: 宣告自己拥有某个文件
 * 
 * 使用示例：
 * @code
 * asio::io_context io_context;
 * DhtClient dht(io_context);
 * dht.start();
 * 
 * // 加入网络
 * dht.bootstrap([](bool success, size_t nodes) {
 *     if (success) {
 *         std::cout << "Bootstrap 成功\n";
 *     }
 * });
 * 
 * // 查找 Peers
 * InfoHash hash = ...;
 * dht.findPeers(hash,
 *     [](const PeerInfo& peer) {
 *         std::cout << "发现 Peer: " << peer.toString() << "\n";
 *     },
 *     [](bool success, const std::vector<PeerInfo>& peers) {
 *         std::cout << "查找完成，找到 " << peers.size() << " 个 Peers\n";
 *     }
 * );
 * 
 * io_context.run();
 * @endcode
 */
class DhtClient : public std::enable_shared_from_this<DhtClient> {
public:
    /**
     * @brief 构造函数
     * @param io_context 事件循环
     * @param config 配置参数
     */
    DhtClient(asio::io_context& io_context, DhtClientConfig config = {});
    
    /**
     * @brief 析构函数
     */
    ~DhtClient();
    
    // 禁止拷贝和移动
    DhtClient(const DhtClient&) = delete;
    DhtClient& operator=(const DhtClient&) = delete;
    DhtClient(DhtClient&&) = delete;
    DhtClient& operator=(DhtClient&&) = delete;
    
    // ========================================================================
    // 生命周期管理
    // ========================================================================
    
    /**
     * @brief 启动 DHT 客户端
     * 
     * 执行操作：
     * - 启动 UDP 监听
     * - 启动 QueryManager
     * - 启动定时器
     */
    void start();
    
    /**
     * @brief 停止 DHT 客户端
     * 
     * 执行操作：
     * - 取消所有待处理查询
     * - 停止定时器
     * - 关闭 UDP
     */
    void stop();
    
    /**
     * @brief 检查是否正在运行
     */
    bool isRunning() const { return running_.load(); }
    
    // ========================================================================
    // 核心功能
    // ========================================================================
    
    /**
     * @brief 加入 DHT 网络
     * @param callback 完成回调
     * 
     * 向 Bootstrap 节点发送 find_node 查询，填充路由表
     */
    void bootstrap(BootstrapCallback callback = nullptr);
    
    /**
     * @brief 查找拥有指定文件的 Peer
     * @param info_hash 文件的 InfoHash
     * @param on_peer 每找到一个 Peer 时调用
     * @param on_complete 查找完成时调用
     * 
     * 使用迭代查找算法查找 Peer：
     * 1. 从路由表获取最近的 K 个节点
     * 2. 并发向 α 个节点发送 get_peers
     * 3. 收到响应后，继续向更近的节点查询
     * 4. 直到收敛或达到最大轮次
     */
    void findPeers(const InfoHash& info_hash,
                   PeerCallback on_peer,
                   LookupCompleteCallback on_complete = nullptr);
    
    /**
     * @brief 宣告自己拥有某个文件
     * @param info_hash 文件的 InfoHash
     * @param port 提供文件的端口
     * 
     * 向最近的节点发送 announce_peer
     * 注意：需要先调用 findPeers 获取 token
     */
    void announce(const InfoHash& info_hash, uint16_t port);
    
    // ========================================================================
    // 状态查询
    // ========================================================================
    
    /**
     * @brief 获取本地 NodeId
     */
    const NodeId& localId() const { return my_id_; }
    
    /**
     * @brief 获取路由表节点数
     */
    size_t nodeCount() const { return routing_table_.nodeCount(); }
    
    /**
     * @brief 检查是否已 Bootstrap
     */
    bool isBootstrapped() const { return bootstrapped_.load(); }
    
    /**
     * @brief 获取统计信息
     */
    DhtClientStatistics getStatistics() const;
    
    /**
     * @brief 重置统计信息
     */
    void resetStatistics();
    
    /**
     * @brief 获取 UDP 监听端口
     */
    uint16_t localPort() const;

private:
    // ========================================================================
    // 消息处理
    // ========================================================================
    
    /**
     * @brief 处理收到的 UDP 消息
     */
    void onReceive(const network::UdpMessage& message);
    
    /**
     * @brief 处理查询消息
     */
    void handleQuery(const DhtMessage& message, const network::UdpEndpoint& sender);
    
    /**
     * @brief 处理响应消息
     */
    void handleResponse(const DhtMessage& message);
    
    /**
     * @brief 处理错误消息
     */
    void handleError(const DhtMessage& message);
    
    // ========================================================================
    // 查询处理
    // ========================================================================
    
    void handlePing(const DhtMessage& query, const network::UdpEndpoint& sender);
    void handleFindNode(const DhtMessage& query, const network::UdpEndpoint& sender);
    void handleGetPeers(const DhtMessage& query, const network::UdpEndpoint& sender);
    void handleAnnouncePeer(const DhtMessage& query, const network::UdpEndpoint& sender);
    
    // ========================================================================
    // 迭代查找
    // ========================================================================
    
    /**
     * @brief 启动查找
     */
    void startLookup(const InfoHash& target,
                     PeerCallback on_peer,
                     LookupCompleteCallback on_complete);
    
    /**
     * @brief 继续查找
     */
    void continueLookup(const std::string& lookup_id);
    
    /**
     * @brief 完成查找
     */
    void completeLookup(const std::string& lookup_id, bool success);
    
    /**
     * @brief 处理查找响应
     */
    void handleLookupResponse(const std::string& lookup_id,
                              const DhtNode& responder,
                              const DhtMessage& response);
    
    // ========================================================================
    // 维护任务
    // ========================================================================
    
    /**
     * @brief 调度路由表刷新
     */
    void scheduleRefresh();
    
    /**
     * @brief 刷新路由表
     */
    void refreshRoutingTable();
    
    // ========================================================================
    // Token 管理
    // ========================================================================
    
    /**
     * @brief 生成 Token
     */
    std::string generateToken(const network::UdpEndpoint& node);
    
    /**
     * @brief 验证 Token
     */
    bool verifyToken(const network::UdpEndpoint& node, const std::string& token);
    
    // ========================================================================
    // 工具方法
    // ========================================================================
    
    /**
     * @brief 生成唯一的查找 ID
     */
    std::string generateLookupId();
    
    /**
     * @brief 发送响应
     */
    void sendResponse(const network::UdpEndpoint& target, const DhtMessage& response);

private:
    asio::io_context& io_context_;
    DhtClientConfig config_;
    
    // 本地 ID
    NodeId my_id_;
    
    // 子模块
    std::shared_ptr<network::UdpClient> udp_client_;
    std::shared_ptr<QueryManager> query_manager_;
    RoutingTable routing_table_;
    
    // 活动的查找
    std::map<std::string, LookupState> active_lookups_;
    mutable std::mutex lookups_mutex_;
    
    // Peer 存储（info_hash -> peers）
    std::map<std::string, std::vector<PeerInfo>> peer_storage_;
    mutable std::mutex peer_storage_mutex_;
    
    // Token 相关
    std::string token_secret_;
    std::string prev_token_secret_;
    std::chrono::steady_clock::time_point token_rotate_time_;
    
    // 定时器
    asio::steady_timer refresh_timer_;
    
    // 状态
    std::atomic<bool> running_{false};
    std::atomic<bool> bootstrapped_{false};
    
    // 统计
    mutable std::mutex stats_mutex_;
    DhtClientStatistics statistics_;
    
    // 查找 ID 计数器
    std::atomic<uint64_t> lookup_counter_{0};
};

} // namespace magnet::protocols


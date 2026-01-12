#include "magnet/protocols/dht_client.h"
#include "magnet/utils/logger.h"

#include <random>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

namespace magnet::protocols {

// 日志宏定义
#define LOG_DEBUG(msg) magnet::utils::Logger::instance().debug(msg)
#define LOG_INFO(msg) magnet::utils::Logger::instance().info(msg)
#define LOG_WARNING(msg) magnet::utils::Logger::instance().warn(msg)
#define LOG_ERROR(msg) magnet::utils::Logger::instance().error(msg)

// ============================================================================
// 构造函数和析构函数
// ============================================================================

DhtClient::DhtClient(asio::io_context& io_context, DhtClientConfig config)
    : io_context_(io_context)
    , config_(std::move(config))
    , my_id_(NodeId::random())
    , routing_table_(my_id_)
    , refresh_timer_(io_context)
{
    LOG_INFO("DhtClient created with NodeId: " + my_id_.toHex().substr(0, 16) + "...");
    
    // 初始化 Token 密钥
    token_secret_ = DhtMessage::generateTransactionId(16);
    prev_token_secret_ = token_secret_;
    token_rotate_time_ = std::chrono::steady_clock::now();
}

DhtClient::~DhtClient() {
    stop();
    LOG_INFO("DhtClient destroyed");
}

// ============================================================================
// 生命周期管理
// ============================================================================

void DhtClient::start() {
    if (running_.exchange(true)) {
        LOG_WARNING("DhtClient already running");
        return;
    }
    
    LOG_INFO("Starting DhtClient on port " + std::to_string(config_.listen_port));
    
    // 创建 UDP 客户端
    udp_client_ = std::make_shared<network::UdpClient>(io_context_, config_.listen_port);
    
    // 创建 QueryManager
    query_manager_ = std::make_shared<QueryManager>(io_context_, udp_client_, config_.query_config);
    query_manager_->start();
    
    // 启动 UDP 接收
    auto self = shared_from_this();
    udp_client_->startReceive([self](const network::UdpMessage& message) {
        self->onReceive(message);
    });
    
    // 启动路由表刷新定时器
    scheduleRefresh();
    
    LOG_INFO("DhtClient started, listening on port " + std::to_string(udp_client_->localPort()));
}

void DhtClient::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    
    LOG_INFO("Stopping DhtClient...");
    
    // 取消定时器
    asio::error_code ec;
    refresh_timer_.cancel();
    
    // 停止 QueryManager
    if (query_manager_) {
        query_manager_->stop();
    }
    
    // 关闭 UDP
    if (udp_client_) {
        udp_client_->close();
    }
    
    // 取消所有活动的查找
    {
        std::lock_guard<std::mutex> lock(lookups_mutex_);
        for (auto& [id, lookup] : active_lookups_) {
            lookup.completed = true;
            if (lookup.on_complete) {
                lookup.on_complete(false, lookup.found_peers);
            }
        }
        active_lookups_.clear();
    }
    
    bootstrapped_.store(false);
    LOG_INFO("DhtClient stopped");
}

// ============================================================================
// 核心功能
// ============================================================================

void DhtClient::bootstrap(BootstrapCallback callback) {
    if (!running_.load()) {
        LOG_ERROR("DhtClient not running, cannot bootstrap");
        if (callback) {
            callback(false, 0);
        }
        return;
    }
    
    LOG_INFO("Starting bootstrap...");
    
    auto self = shared_from_this();
    size_t pending_count = config_.bootstrap_nodes.size();
    auto success_count = std::make_shared<std::atomic<size_t>>(0);
    auto remaining = std::make_shared<std::atomic<size_t>>(pending_count);
    
    for (const auto& [host, port] : config_.bootstrap_nodes) {
        // 创建一个虚拟的 DhtNode 用于 Bootstrap
        DhtNode bootstrap_node;
        bootstrap_node.ip_ = host;
        bootstrap_node.port_ = port;
        // Bootstrap 节点的 ID 未知，使用随机 ID
        bootstrap_node.id_ = NodeId::random();
        
        // 发送 find_node 查询（查找自己）
        auto msg = DhtMessage::createFindNode(my_id_, my_id_);
        
        query_manager_->sendQuery(bootstrap_node, std::move(msg),
            [self, callback, success_count, remaining, host](QueryResult result) {
                if (result.is_ok()) {
                    const auto& response = result.value();
                    auto nodes = response.getNodes();
                    
                    LOG_INFO("Bootstrap response from " + host + ", got " + 
                             std::to_string(nodes.size()) + " nodes");
                    
                    // 添加节点到路由表
                    for (const auto& node : nodes) {
                        self->routing_table_.addNode(node);
                    }
                    
                    // 也添加响应者
                    DhtNode responder;
                    responder.id_ = response.senderId();
                    responder.ip_ = host;
                    self->routing_table_.addNode(responder);
                    
                    success_count->fetch_add(1);
                } else {
                    LOG_WARNING("Bootstrap query to " + host + " failed");
                }
                
                // 检查是否所有查询都完成
                if (remaining->fetch_sub(1) == 1) {
                    size_t node_count = self->routing_table_.nodeCount();
                    bool success = success_count->load() > 0 && node_count > 0;
                    
                    if (success) {
                        self->bootstrapped_.store(true);
                        LOG_INFO("Bootstrap completed successfully, " + 
                                 std::to_string(node_count) + " nodes in routing table");
                    } else {
                        LOG_ERROR("Bootstrap failed, no nodes in routing table");
                    }
                    
                    if (callback) {
                        callback(success, node_count);
                    }
                }
            }
        );
    }
}

void DhtClient::findPeers(const InfoHash& info_hash,
                          PeerCallback on_peer,
                          LookupCompleteCallback on_complete) {
    if (!running_.load()) {
        LOG_ERROR("DhtClient not running, cannot find peers");
        if (on_complete) {
            on_complete(false, {});
        }
        return;
    }
    
    LOG_INFO("Starting findPeers for " + info_hash.toHex().substr(0, 16) + "...");
    
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        statistics_.lookups_started++;
    }
    
    startLookup(info_hash, std::move(on_peer), std::move(on_complete));
}

void DhtClient::announce(const InfoHash& info_hash, uint16_t port) {
    if (!running_.load()) {
        LOG_ERROR("DhtClient not running, cannot announce");
        return;
    }
    
    LOG_INFO("Announcing " + info_hash.toHex().substr(0, 16) + "... on port " + std::to_string(port));
    
    // 获取最近的节点
    NodeId target_id = NodeId::fromInfoHash(info_hash);
    
    auto closest = routing_table_.findCloset(target_id, config_.k);
    
    for (const auto& node : closest) {
        // 注意：这里需要有从之前 get_peers 获得的 token
        // 简化实现：使用空 token（实际应该缓存 token）
        auto msg = DhtMessage::createAnnouncePeer(my_id_, info_hash, port, "", true);
        
        query_manager_->sendQuery(node, std::move(msg),
            [](QueryResult result) {
                if (result.is_ok()) {
                    LOG_DEBUG("Announce succeeded");
                } else {
                    LOG_DEBUG("Announce failed");
                }
            }
        );
    }
}

// ============================================================================
// 状态查询
// ============================================================================

DhtClientStatistics DhtClient::getStatistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    DhtClientStatistics stats = statistics_;
    stats.bootstrapped = bootstrapped_.load();
    stats.node_count = routing_table_.nodeCount();
    return stats;
}

void DhtClient::resetStatistics() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    statistics_.reset();
}

uint16_t DhtClient::localPort() const {
    return udp_client_ ? udp_client_->localPort() : 0;
}

// ============================================================================
// 消息处理
// ============================================================================

void DhtClient::onReceive(const network::UdpMessage& message) {
    // 解析消息
    auto parsed = DhtMessage::parse(message.data);
    if (!parsed) {
        LOG_DEBUG("Failed to parse DHT message from " + message.remote_endpoint.ip);
        return;
    }
    
    const auto& msg = *parsed;
    
    if (msg.isQuery()) {
        handleQuery(msg, message.remote_endpoint);
    } else if (msg.isResponse()) {
        handleResponse(msg);
    } else if (msg.isError()) {
        handleError(msg);
    }
}

void DhtClient::handleQuery(const DhtMessage& message, const network::UdpEndpoint& sender) {
    LOG_DEBUG("Received query from " + sender.ip + ":" + std::to_string(sender.port));
    
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        statistics_.queries_received++;
    }
    
    // 更新路由表
    DhtNode sender_node;
    sender_node.id_ = message.senderId();
    sender_node.ip_ = sender.ip;
    sender_node.port_ = sender.port;
    routing_table_.addNode(sender_node);
    
    switch (message.queryType()) {
        case DhtQueryType::PING:
            handlePing(message, sender);
            break;
        case DhtQueryType::FIND_NODE:
            handleFindNode(message, sender);
            break;
        case DhtQueryType::GET_PEERS:
            handleGetPeers(message, sender);
            break;
        case DhtQueryType::ANNOUNCE_PEER:
            handleAnnouncePeer(message, sender);
            break;
        default:
            LOG_WARNING("Unknown query type");
            break;
    }
}

void DhtClient::handleResponse(const DhtMessage& message) {
    // 交给 QueryManager 处理
    if (query_manager_) {
        query_manager_->handleResponse(message);
    }
}

void DhtClient::handleError(const DhtMessage& message) {
    const auto& err = message.error();
    LOG_WARNING("Received DHT error: [" + std::to_string(static_cast<int>(err.code)) + 
                "] " + err.message);
}

// ============================================================================
// 查询处理
// ============================================================================

void DhtClient::handlePing(const DhtMessage& query, const network::UdpEndpoint& sender) {
    auto response = DhtMessage::createPingResponse(query.transactionId(), my_id_);
    sendResponse(sender, response);
}

void DhtClient::handleFindNode(const DhtMessage& query, const network::UdpEndpoint& sender) {
    auto closest = routing_table_.findCloset(query.targetId(), config_.k);
    auto response = DhtMessage::createFindNodeResponse(query.transactionId(), my_id_, closest);
    sendResponse(sender, response);
}

void DhtClient::handleGetPeers(const DhtMessage& query, const network::UdpEndpoint& sender) {
    std::string token = generateToken(sender);
    
    // 检查是否有该 InfoHash 的 Peer
    std::vector<PeerInfo> peers;
    {
        std::lock_guard<std::mutex> lock(peer_storage_mutex_);
        auto it = peer_storage_.find(query.infoHash().toHex());
        if (it != peer_storage_.end()) {
            peers = it->second;
        }
    }
    
    if (!peers.empty()) {
        auto response = DhtMessage::createGetPeersResponseWithPeers(
            query.transactionId(), my_id_, token, peers);
        sendResponse(sender, response);
    } else {
        // 返回最近的节点
        NodeId target_id = NodeId::fromInfoHash(query.infoHash());
        auto closest = routing_table_.findCloset(target_id, config_.k);
        auto response = DhtMessage::createGetPeersResponseWithNodes(
            query.transactionId(), my_id_, token, closest);
        sendResponse(sender, response);
    }
}

void DhtClient::handleAnnouncePeer(const DhtMessage& query, const network::UdpEndpoint& sender) {
    // 验证 Token
    if (!verifyToken(sender, query.token())) {
        LOG_WARNING("Invalid token in announce_peer from " + sender.ip);
        auto error = DhtMessage::createError(query.transactionId(), 
                                              DhtErrorCode::PROTOCOL, "Invalid token");
        sendResponse(sender, error);
        return;
    }
    
    // 存储 Peer 信息
    PeerInfo peer;
    peer.ip = sender.ip;
    peer.port = query.impliedPort() ? sender.port : query.port();
    
    {
        std::lock_guard<std::mutex> lock(peer_storage_mutex_);
        auto& peers = peer_storage_[query.infoHash().toHex()];
        
        // 简单去重
        bool exists = false;
        for (const auto& existing : peers) {
            if (existing.ip == peer.ip && existing.port == peer.port) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            peers.push_back(peer);
            // 限制存储数量
            if (peers.size() > 100) {
                peers.erase(peers.begin());
            }
        }
    }
    
    // 发送响应
    auto response = DhtMessage::createPingResponse(query.transactionId(), my_id_);
    sendResponse(sender, response);
}

// ============================================================================
// 迭代查找
// ============================================================================

void DhtClient::startLookup(const InfoHash& target,
                            PeerCallback on_peer,
                            LookupCompleteCallback on_complete) {
    std::string lookup_id = generateLookupId();
    
    LookupState state;
    state.id = lookup_id;
    state.target = target;
    state.target_id = NodeId::fromInfoHash(target);
    state.on_peer = std::move(on_peer);
    state.on_complete = std::move(on_complete);
    state.alpha = config_.alpha;
    state.max_rounds = config_.max_lookup_rounds;
    state.start_time = std::chrono::steady_clock::now();
    
    // 从路由表获取初始节点
    auto initial_nodes = routing_table_.findCloset(state.target_id, config_.k);
    
    if (initial_nodes.empty()) {
        LOG_WARNING("No nodes in routing table, cannot start lookup");
        if (on_complete) {
            on_complete(false, {});
        }
        return;
    }
    
    for (const auto& node : initial_nodes) {
        state.candidates[node.id_] = node;
    }
    
    {
        std::lock_guard<std::mutex> lock(lookups_mutex_);
        active_lookups_[lookup_id] = std::move(state);
    }
    
    // 开始查找
    continueLookup(lookup_id);
}

void DhtClient::continueLookup(const std::string& lookup_id) {
    std::vector<DhtNode> nodes_to_query;
    InfoHash target;
    
    {
        std::lock_guard<std::mutex> lock(lookups_mutex_);
        auto it = active_lookups_.find(lookup_id);
        if (it == active_lookups_.end() || it->second.completed) {
            return;
        }
        
        auto& state = it->second;
        
        if (!state.shouldContinue()) {
            // 查找收敛，完成
            completeLookup(lookup_id, !state.found_peers.empty());
            return;
        }
        
        state.current_round++;
        nodes_to_query = state.getNextNodes(state.alpha);
        target = state.target;
        
        // 标记为 pending
        for (const auto& node : nodes_to_query) {
            state.pending.insert(node.id_);
        }
    }
    
    if (nodes_to_query.empty()) {
        completeLookup(lookup_id, false);
        return;
    }
    
    auto self = shared_from_this();
    
    for (const auto& node : nodes_to_query) {
        auto msg = DhtMessage::createGetPeers(my_id_, target);
        
        query_manager_->sendQuery(node, std::move(msg),
            [self, lookup_id, node](QueryResult result) {
                if (result.is_ok()) {
                    self->handleLookupResponse(lookup_id, node, result.value());
                } else {
                    // 查询失败，从 pending 移除
                    std::lock_guard<std::mutex> lock(self->lookups_mutex_);
                    auto it = self->active_lookups_.find(lookup_id);
                    if (it != self->active_lookups_.end()) {
                        it->second.pending.erase(node.id_);
                        it->second.queried.insert(node.id_);
                        
                        // 标记节点失败
                        self->routing_table_.markNodeFailed(node.id_);
                    }
                }
                
                // 继续查找
                self->continueLookup(lookup_id);
            }
        );
    }
}

void DhtClient::handleLookupResponse(const std::string& lookup_id,
                                     const DhtNode& responder,
                                     const DhtMessage& response) {
    std::lock_guard<std::mutex> lock(lookups_mutex_);
    auto it = active_lookups_.find(lookup_id);
    if (it == active_lookups_.end() || it->second.completed) {
        return;
    }
    
    auto& state = it->second;
    
    // 从 pending 移除，添加到 queried
    state.pending.erase(responder.id_);
    state.queried.insert(responder.id_);
    
    // 更新路由表
    DhtNode updated_responder = responder;
    updated_responder.id_ = response.senderId();
    routing_table_.addNode(updated_responder);
    routing_table_.markNodeResponded(response.senderId());
    
    // 保存 token
    if (!response.token().empty()) {
        state.token = response.token();
    }
    
    // 处理 Peers
    if (response.hasPeers()) {
        auto peers = response.getPeers();
        LOG_INFO("Found " + std::to_string(peers.size()) + " peers");
        
        for (const auto& peer : peers) {
            // 检查是否是新 Peer
            bool is_new = true;
            for (const auto& existing : state.found_peers) {
                if (existing.ip == peer.ip && existing.port == peer.port) {
                    is_new = false;
                    break;
                }
            }
            
            if (is_new) {
                state.found_peers.push_back(peer);
                
                // 回调通知
                if (state.on_peer) {
                    state.on_peer(peer);
                }
                
                {
                    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                    statistics_.peers_found++;
                }
            }
        }
    }
    
    // 处理节点
    if (response.hasNodes()) {
        auto nodes = response.getNodes();
        state.addNodes(nodes);
        
        // 添加到路由表
        for (const auto& node : nodes) {
            routing_table_.addNode(node);
        }
    }
}

void DhtClient::completeLookup(const std::string& lookup_id, bool success) {
    LookupCompleteCallback callback;
    std::vector<PeerInfo> peers;
    
    {
        std::lock_guard<std::mutex> lock(lookups_mutex_);
        auto it = active_lookups_.find(lookup_id);
        if (it == active_lookups_.end() || it->second.completed) {
            return;
        }
        
        auto& state = it->second;
        state.completed = true;
        callback = state.on_complete;
        peers = state.found_peers;
        
        auto elapsed = std::chrono::steady_clock::now() - state.start_time;
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
        
        LOG_INFO("Lookup completed in " + std::to_string(elapsed_ms) + "ms, found " +
                 std::to_string(peers.size()) + " peers");
        
        active_lookups_.erase(it);
    }
    
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        statistics_.lookups_completed++;
        if (success) {
            statistics_.lookups_successful++;
        }
    }
    
    if (callback) {
        callback(success, peers);
    }
}

// ============================================================================
// 维护任务
// ============================================================================

void DhtClient::scheduleRefresh() {
    if (!running_.load()) {
        return;
    }
    
    auto self = shared_from_this();
    refresh_timer_.expires_after(config_.refresh_interval);
    refresh_timer_.async_wait([self](const asio::error_code& ec) {
        if (!ec && self->running_.load()) {
            self->refreshRoutingTable();
            self->scheduleRefresh();
        }
    });
}

void DhtClient::refreshRoutingTable() {
    LOG_DEBUG("Refreshing routing table...");
    
    // 获取需要刷新的桶
    auto stale_buckets = routing_table_.getStaleBuckets();
    
    for (size_t bucket_idx : stale_buckets) {
        // 生成该桶范围内的随机 ID
        NodeId random_id = routing_table_.getRandomIdInBucket(bucket_idx);
        
        // 查找该 ID 附近的节点
        auto closest = routing_table_.findCloset(random_id, config_.alpha);
        
        for (const auto& node : closest) {
            auto msg = DhtMessage::createFindNode(my_id_, random_id);
            query_manager_->sendQuery(node, std::move(msg),
                [this](QueryResult result) {
                    if (result.is_ok()) {
                        auto nodes = result.value().getNodes();
                        for (const auto& node : nodes) {
                            routing_table_.addNode(node);
                        }
                    }
                }
            );
        }
    }
    
    // 更新 Token 密钥
    auto now = std::chrono::steady_clock::now();
    if (now - token_rotate_time_ > std::chrono::minutes(5)) {
        prev_token_secret_ = token_secret_;
        token_secret_ = DhtMessage::generateTransactionId(16);
        token_rotate_time_ = now;
        LOG_DEBUG("Token secret rotated");
    }
}

// ============================================================================
// Token 管理
// ============================================================================

std::string DhtClient::generateToken(const network::UdpEndpoint& node) {
    // 简单实现：hash(secret + ip + port)
    std::string data = token_secret_ + node.ip + std::to_string(node.port);
    
    // 使用简单的哈希
    uint32_t hash = 0;
    for (char c : data) {
        hash = hash * 31 + static_cast<uint8_t>(c);
    }
    
    std::string token(4, '\0');
    token[0] = static_cast<char>((hash >> 24) & 0xFF);
    token[1] = static_cast<char>((hash >> 16) & 0xFF);
    token[2] = static_cast<char>((hash >> 8) & 0xFF);
    token[3] = static_cast<char>(hash & 0xFF);
    
    return token;
}

bool DhtClient::verifyToken(const network::UdpEndpoint& node, const std::string& token) {
    // 检查当前密钥
    if (generateToken(node) == token) {
        return true;
    }
    
    // 检查上一个密钥（Token 轮换过渡期）
    std::string old_data = prev_token_secret_ + node.ip + std::to_string(node.port);
    uint32_t hash = 0;
    for (char c : old_data) {
        hash = hash * 31 + static_cast<uint8_t>(c);
    }
    
    std::string old_token(4, '\0');
    old_token[0] = static_cast<char>((hash >> 24) & 0xFF);
    old_token[1] = static_cast<char>((hash >> 16) & 0xFF);
    old_token[2] = static_cast<char>((hash >> 8) & 0xFF);
    old_token[3] = static_cast<char>(hash & 0xFF);
    
    return old_token == token;
}

// ============================================================================
// 工具方法
// ============================================================================

std::string DhtClient::generateLookupId() {
    uint64_t id = lookup_counter_.fetch_add(1);
    std::ostringstream oss;
    oss << "lookup_" << id;
    return oss.str();
}

void DhtClient::sendResponse(const network::UdpEndpoint& target, const DhtMessage& response) {
    if (!udp_client_) {
        return;
    }
    
    auto data = response.encode();
    udp_client_->send(target, data, [](const asio::error_code& ec, size_t) {
        if (ec) {
            LOG_DEBUG("Failed to send response: " + ec.message());
        }
    });
    
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        statistics_.responses_sent++;
    }
}

} // namespace magnet::protocols

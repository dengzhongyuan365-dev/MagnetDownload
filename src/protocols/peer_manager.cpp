#include "magnet/protocols/peer_manager.h"
#include "magnet/utils/logger.h"

#include <algorithm>
#include <random>

namespace magnet::protocols {

// 日志宏
#define LOG_DEBUG(msg) magnet::utils::Logger::instance().debug(msg)
#define LOG_INFO(msg) magnet::utils::Logger::instance().info(msg)
#define LOG_WARNING(msg) magnet::utils::Logger::instance().warn(msg)
#define LOG_ERROR(msg) magnet::utils::Logger::instance().error(msg)

// ============================================================================
// 构造和析构
// ============================================================================

PeerManager::PeerManager(asio::io_context& io_context,
                         const InfoHash& info_hash,
                         const std::string& my_peer_id,
                         PeerManagerConfig config)
    : io_context_(io_context)
    , info_hash_(info_hash)
    , my_peer_id_(my_peer_id)
    , config_(std::move(config))
    , evaluation_timer_(io_context)
{
    LOG_DEBUG("PeerManager created");
}

PeerManager::~PeerManager() {
    stop();
    LOG_DEBUG("PeerManager destroyed");
}

// ============================================================================
// 生命周期管理
// ============================================================================

void PeerManager::start() {
    if (running_.exchange(true)) {
        return;  // 已经在运行
    }
    
    LOG_INFO("PeerManager started");
    
    // 启动定时器
    startTimers();
    
    // 尝试连接等待中的 Peer
    tryConnectMore();
}

void PeerManager::stop() {
    if (!running_.exchange(false)) {
        return;  // 已经停止
    }
    
    LOG_INFO("PeerManager stopping...");
    
    // 取消定时器
    evaluation_timer_.cancel();
    
    // 断开所有连接
    std::lock_guard<std::mutex> lock(peers_mutex_);
    for (auto& [key, entry] : peers_) {
        if (entry.connection) {
            entry.connection->disconnect();
        }
    }
    
    peers_.clear();
    pending_peers_.clear();
    connecting_peers_.clear();
    connected_peers_.clear();
    
    LOG_INFO("PeerManager stopped");
}

// ============================================================================
// Peer 管理
// ============================================================================

bool PeerManager::addPeer(const network::TcpEndpoint& endpoint) {
    if (!endpoint.isValid()) {
        return false;
    }
    
    std::string key = endpointToKey(endpoint);
    
    std::lock_guard<std::mutex> lock(peers_mutex_);
    
    // 检查是否已存在
    if (peers_.find(key) != peers_.end()) {
        return false;
    }
    
    // 检查等待队列是否已满
    if (pending_peers_.size() >= config_.max_pending) {
        // 找到评分最低的待连接 Peer 并移除
        // 简化处理：直接拒绝
        LOG_DEBUG("Pending queue full, rejecting peer " + endpoint.toString());
        return false;
    }
    
    // 创建条目
    PeerEntry entry(endpoint);
    peers_[key] = std::move(entry);
    pending_peers_.insert(key);
    
    // 更新统计
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        statistics_.total_peers_known++;
        statistics_.peers_pending = pending_peers_.size();
    }
    
    LOG_DEBUG("Added peer " + endpoint.toString());
    
    // 如果正在运行，尝试连接
    if (running_.load()) {
        asio::post(io_context_, [self = shared_from_this()]() {
            self->tryConnectMore();
        });
    }
    
    return true;
}

void PeerManager::addPeers(const std::vector<network::TcpEndpoint>& endpoints) {
    for (const auto& ep : endpoints) {
        addPeer(ep);
    }
}

void PeerManager::removePeer(const network::TcpEndpoint& endpoint) {
    std::string key = endpointToKey(endpoint);
    
    std::lock_guard<std::mutex> lock(peers_mutex_);
    
    auto it = peers_.find(key);
    if (it == peers_.end()) {
        return;
    }
    
    // 断开连接
    if (it->second.connection) {
        it->second.connection->disconnect();
    }
    
    // 从各个集合中移除
    pending_peers_.erase(key);
    connecting_peers_.erase(key);
    connected_peers_.erase(key);
    peers_.erase(it);
    
    // 更新统计
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        statistics_.peers_pending = pending_peers_.size();
        statistics_.peers_connecting = connecting_peers_.size();
        statistics_.peers_connected = connected_peers_.size();
    }
    
    LOG_DEBUG("Removed peer " + endpoint.toString());
}

// ============================================================================
// 数据传输
// ============================================================================

bool PeerManager::requestBlock(const BlockInfo& block) {
    if (!running_.load()) {
        return false;
    }
    
    // 选择最佳 Peer
    std::lock_guard<std::mutex> lock(peers_mutex_);
    
    PeerEntry* best_peer = selectBestPeerForPiece(block.piece_index);
    if (!best_peer) {
        LOG_DEBUG("No peer available for piece " + std::to_string(block.piece_index));
        return false;
    }
    
    // 发送请求
    best_peer->connection->requestBlock(block);
    best_peer->pending_requests++;
    
    return true;
}

void PeerManager::cancelBlock(const BlockInfo& block) {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    
    // 向所有可能有该请求的 Peer 发送取消
    for (auto& [key, entry] : peers_) {
        if (entry.is_connected && entry.connection) {
            entry.connection->cancelBlock(block);
        }
    }
}

void PeerManager::broadcastHave(uint32_t piece_index) {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    
    for (auto& [key, entry] : peers_) {
        if (entry.is_connected && entry.connection) {
            entry.connection->sendHave(piece_index);
        }
    }
}

void PeerManager::updateBitfield(const std::vector<bool>& bitfield) {
    std::lock_guard<std::mutex> lock(bitfield_mutex_);
    my_bitfield_ = bitfield;
}

// ============================================================================
// 查询
// ============================================================================

std::vector<network::TcpEndpoint> PeerManager::getPeersWithPiece(uint32_t piece_index) const {
    std::vector<network::TcpEndpoint> result;
    
    std::lock_guard<std::mutex> lock(peers_mutex_);
    
    for (const auto& [key, entry] : peers_) {
        if (entry.is_connected && entry.connection && 
            entry.connection->hasPiece(piece_index)) {
            result.push_back(entry.endpoint);
        }
    }
    
    return result;
}

std::vector<network::TcpEndpoint> PeerManager::getConnectedPeers() const {
    std::vector<network::TcpEndpoint> result;
    
    std::lock_guard<std::mutex> lock(peers_mutex_);
    
    for (const auto& key : connected_peers_) {
        auto it = peers_.find(key);
        if (it != peers_.end()) {
            result.push_back(it->second.endpoint);
        }
    }
    
    return result;
}

size_t PeerManager::connectedCount() const {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    return connected_peers_.size();
}

PeerManagerStatistics PeerManager::getStatistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return statistics_;
}

// ============================================================================
// 回调设置
// ============================================================================

void PeerManager::setPieceCallback(PieceReceivedCallback callback) {
    piece_callback_ = std::move(callback);
}

void PeerManager::setPeerStatusCallback(PeerStatusCallback callback) {
    peer_status_callback_ = std::move(callback);
}

void PeerManager::setNeedMorePeersCallback(NeedMorePeersCallback callback) {
    need_more_peers_callback_ = std::move(callback);
}

void PeerManager::setNewPeerCallback(NewPeerCallback callback) {
    new_peer_callback_ = std::move(callback);
}

// ============================================================================
// 内部方法
// ============================================================================

void PeerManager::connectToPeer(const network::TcpEndpoint& endpoint) {
    std::string key = endpointToKey(endpoint);
    
    // 更新状态
    {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        
        auto it = peers_.find(key);
        if (it == peers_.end()) {
            return;
        }
        
        // 检查是否已在连接或已连接
        if (it->second.is_connecting || it->second.is_connected) {
            return;
        }
        
        // 检查连接失败次数
        if (it->second.connect_failures >= config_.max_connect_failures) {
            LOG_DEBUG("Too many failures for " + endpoint.toString() + ", removing");
            pending_peers_.erase(key);
            peers_.erase(it);
            return;
        }
        
        // 更新状态
        it->second.is_connecting = true;
        it->second.last_connect_attempt = std::chrono::steady_clock::now();
        
        pending_peers_.erase(key);
        connecting_peers_.insert(key);
        
        // 创建连接
        it->second.connection = std::make_shared<PeerConnection>(
            io_context_, info_hash_, my_peer_id_);
    }
    
    // 更新统计
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        statistics_.peers_pending = pending_peers_.size();
        statistics_.peers_connecting = connecting_peers_.size();
    }
    
    // 获取 PeerConnection
    std::shared_ptr<PeerConnection> conn;
    {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        auto it = peers_.find(key);
        if (it != peers_.end()) {
            conn = it->second.connection;
        }
    }
    
    if (!conn) {
        return;
    }
    
    // 设置回调
    auto self = shared_from_this();
    
    conn->setStateCallback([self, endpoint](PeerConnectionState state) {
        if (state == PeerConnectionState::Disconnected) {
            self->onPeerDisconnected(endpoint, "state changed to disconnected");
        }
    });
    
    conn->setMessageCallback([self, endpoint](const BtMessage& msg) {
        self->onPeerMessage(endpoint, msg);
    });
    
    conn->setPieceCallback([self, endpoint](const PieceBlock& block) {
        self->onPieceReceived(endpoint, block);
    });
    
    conn->setErrorCallback([self, endpoint](const std::string& error) {
        LOG_WARNING("Peer " + endpoint.toString() + " error: " + error);
    });
    
    // 连接
    LOG_INFO("Connecting to peer " + endpoint.toString());
    conn->connect(endpoint, [self, endpoint](bool success) {
        if (success) {
            self->onPeerConnected(endpoint);
        } else {
            self->onPeerDisconnected(endpoint, "connection failed");
        }
    });
}

void PeerManager::onPeerConnected(const network::TcpEndpoint& endpoint) {
    std::string key = endpointToKey(endpoint);
    
    {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        
        auto it = peers_.find(key);
        if (it == peers_.end()) {
            return;
        }
        
        it->second.is_connecting = false;
        it->second.is_connected = true;
        it->second.connect_failures = 0;
        
        connecting_peers_.erase(key);
        connected_peers_.insert(key);
        
        // 发送 Interested
        if (it->second.connection) {
            it->second.connection->sendInterested();
        }
    }
    
    // 更新统计
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        statistics_.peers_connecting = connecting_peers_.size();
        statistics_.peers_connected = connected_peers_.size();
    }
    
    LOG_INFO("Connected to peer " + endpoint.toString());
    
    // 回调通知
    if (peer_status_callback_) {
        peer_status_callback_(endpoint, true);
    }
    
    // 通知新连接的 Peer（用于元数据获取）
    std::shared_ptr<PeerConnection> peer_conn;
    {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        auto it = peers_.find(key);
        if (it != peers_.end()) {
            peer_conn = it->second.connection;
        }
    }
    if (new_peer_callback_ && peer_conn) {
        new_peer_callback_(peer_conn);
    }
    
    // 尝试连接更多
    tryConnectMore();
}

void PeerManager::onPeerDisconnected(const network::TcpEndpoint& endpoint, 
                                      const std::string& reason) {
    std::string key = endpointToKey(endpoint);
    bool was_connected = false;
    
    {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        
        auto it = peers_.find(key);
        if (it == peers_.end()) {
            return;
        }
        
        was_connected = it->second.is_connected;
        
        if (it->second.is_connecting) {
            it->second.connect_failures++;
        }
        
        it->second.is_connecting = false;
        it->second.is_connected = false;
        it->second.connection.reset();
        
        connecting_peers_.erase(key);
        connected_peers_.erase(key);
        
        // 如果失败次数未超限，重新加入等待队列
        if (it->second.connect_failures < config_.max_connect_failures) {
            pending_peers_.insert(key);
        } else {
            peers_.erase(it);
        }
    }
    
    // 更新统计
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        statistics_.peers_pending = pending_peers_.size();
        statistics_.peers_connecting = connecting_peers_.size();
        statistics_.peers_connected = connected_peers_.size();
    }
    
    LOG_INFO("Disconnected from peer " + endpoint.toString() + ": " + reason);
    
    // 回调通知
    if (was_connected && peer_status_callback_) {
        peer_status_callback_(endpoint, false);
    }
    
    // 尝试连接更多
    tryConnectMore();
    
    // 检查是否需要更多 Peer
    checkNeedMorePeers();
}

void PeerManager::onPieceReceived(const network::TcpEndpoint& endpoint, 
                                   const PieceBlock& block) {
    std::string key = endpointToKey(endpoint);
    
    // 更新统计
    {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        auto it = peers_.find(key);
        if (it != peers_.end() && it->second.pending_requests > 0) {
            it->second.pending_requests--;
        }
    }
    
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        statistics_.total_bytes_downloaded += block.data.size();
        statistics_.total_pieces_received++;
    }
    
    // 回调通知
    if (piece_callback_) {
        piece_callback_(block.piece_index, block.begin, block.data);
    }
}

void PeerManager::onPeerMessage(const network::TcpEndpoint& endpoint, 
                                 const BtMessage& msg) {
    std::string key = endpointToKey(endpoint);
    
    // 处理特定消息
    if (msg.type() == BtMessageType::Bitfield) {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        auto it = peers_.find(key);
        if (it != peers_.end()) {
            auto bitfield = msg.bitfield();
            // 检查是否是做种者
            it->second.is_seed = std::all_of(bitfield.begin(), bitfield.end(), 
                                              [](bool b) { return b; });
        }
    }
}

void PeerManager::tryConnectMore() {
    if (!running_.load()) {
        LOG_DEBUG("[PeerManager] tryConnectMore: not running");
        return;
    }
    
    std::vector<network::TcpEndpoint> to_connect;
    
    {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        
        // 检查是否可以连接更多
        size_t current_connecting = connecting_peers_.size();
        size_t current_connected = connected_peers_.size();
        
        LOG_DEBUG("[PeerManager] tryConnectMore: pending=" + std::to_string(pending_peers_.size()) +
                  " connecting=" + std::to_string(current_connecting) +
                  " connected=" + std::to_string(current_connected));
        
        if (current_connecting >= config_.max_connecting) {
            LOG_DEBUG("[PeerManager] tryConnectMore: max_connecting reached");
            return;
        }
        
        if (current_connected + current_connecting >= config_.max_connections) {
            LOG_DEBUG("[PeerManager] tryConnectMore: max_connections reached");
            return;
        }
        
        // 计算可以连接的数量
        size_t can_connect = std::min(
            config_.max_connecting - current_connecting,
            config_.max_connections - current_connected - current_connecting
        );
        
        // 从等待队列中选择 Peer
        for (const auto& key : pending_peers_) {
            if (to_connect.size() >= can_connect) {
                break;
            }
            
            auto it = peers_.find(key);
            if (it != peers_.end() && !it->second.is_connecting && !it->second.is_connected) {
                // 检查重连延迟
                auto now = std::chrono::steady_clock::now();
                if (it->second.connect_failures > 0) {
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                        now - it->second.last_connect_attempt);
                    if (elapsed < config_.reconnect_delay) {
                        LOG_DEBUG("[PeerManager] Skipping " + key + " due to reconnect delay");
                        continue;
                    }
                }
                
                to_connect.push_back(it->second.endpoint);
                LOG_DEBUG("[PeerManager] Selected peer to connect: " + it->second.endpoint.toString());
            }
        }
    }
    
    LOG_DEBUG("[PeerManager] Will connect to " + std::to_string(to_connect.size()) + " peers");
    
    // 连接选中的 Peer
    for (const auto& ep : to_connect) {
        connectToPeer(ep);
    }
}

void PeerManager::startTimers() {
    // Peer 评估定时器
    auto self = shared_from_this();
    
    evaluation_timer_.expires_after(config_.peer_evaluation_interval);
    evaluation_timer_.async_wait([self](const asio::error_code& ec) {
        if (!ec && self->running_.load()) {
            self->evaluatePeers();
            self->startTimers();  // 重新启动定时器
        }
    });
}

void PeerManager::evaluatePeers() {
    if (!running_.load()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(peers_mutex_);
    
    // 收集已连接的 Peer
    std::vector<PeerEntry*> connected;
    for (auto& [key, entry] : peers_) {
        if (entry.is_connected && entry.connection) {
            entry.score = calculatePeerScore(entry);
            connected.push_back(&entry);
        }
    }
    
    if (connected.empty()) {
        return;
    }
    
    // 按评分排序（高分在前）
    std::sort(connected.begin(), connected.end(), [](auto* a, auto* b) {
        return a->score > b->score;
    });
    
    // Unchoke 策略：解阻塞评分最高的 N 个 Peer
    size_t unchoke_count = std::min(config_.unchoke_slots, connected.size());
    
    for (size_t i = 0; i < connected.size(); ++i) {
        if (i < unchoke_count) {
            connected[i]->connection->sendUnchoke();
        } else {
            connected[i]->connection->sendChoke();
        }
    }
    
    // Optimistic Unchoke：每隔一段时间随机解阻塞一个
    optimistic_unchoke_counter_++;
    int optimistic_interval = static_cast<int>(
        config_.optimistic_unchoke_interval.count() / 
        config_.peer_evaluation_interval.count());
    
    if (optimistic_interval > 0 && 
        optimistic_unchoke_counter_ % optimistic_interval == 0 && 
        connected.size() > unchoke_count) {
        
        // 随机选择一个被 choke 的 Peer
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<size_t> dist(unchoke_count, connected.size() - 1);
        
        size_t random_idx = dist(gen);
        connected[random_idx]->connection->sendUnchoke();
        
        LOG_DEBUG("Optimistic unchoke: " + connected[random_idx]->endpoint.toString());
    }
}

int PeerManager::calculatePeerScore(const PeerEntry& entry) const {
    if (!entry.connection) {
        return 0;
    }
    
    int score = 0;
    auto stats = entry.connection->getStatistics();
    
    // 下载速度权重 (40%)
    // 每 KB/s 加 1 分
    if (stats.connectionDuration().count() > 0) {
        double speed = static_cast<double>(stats.bytes_downloaded) / 
                       stats.connectionDuration().count();
        score += static_cast<int>(speed / 1024) * 40;
    }
    
    // 请求队列空闲程度 (30%)
    // 队列越空闲越好
    int max_requests = static_cast<int>(config_.max_requests_per_peer);
    int pending = static_cast<int>(entry.pending_requests);
    score += (max_requests - pending) * 3;
    
    // 是否被对方 unchoke (20%)
    auto peer_state = entry.connection->peerState();
    if (!peer_state.peer_choking) {
        score += 20;
    }
    
    // 是否是 seed (10%)
    if (entry.is_seed) {
        score += 10;
    }
    
    return score;
}

PeerEntry* PeerManager::selectBestPeerForPiece(uint32_t piece_index) {
    // 注意：调用时已持有 peers_mutex_
    
    PeerEntry* best = nullptr;
    int best_score = -1;
    
    for (auto& [key, entry] : peers_) {
        if (!entry.is_connected || !entry.connection) {
            continue;
        }
        
        // 检查是否有该分片
        if (!entry.connection->hasPiece(piece_index)) {
            continue;
        }
        
        // 检查是否可以请求
        auto state = entry.connection->peerState();
        if (!state.canRequest()) {
            continue;
        }
        
        // 检查请求队列是否已满
        if (entry.pending_requests >= config_.max_requests_per_peer) {
            continue;
        }
        
        // 计算评分
        int score = calculatePeerScore(entry);
        if (score > best_score) {
            best_score = score;
            best = &entry;
        }
    }
    
    return best;
}

void PeerManager::checkNeedMorePeers() {
    size_t connected;
    size_t pending;
    
    {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        connected = connected_peers_.size();
        pending = pending_peers_.size();
    }
    
    // 如果连接数和等待数都较少，请求更多 Peer
    if (connected < config_.max_connections / 2 && 
        pending < config_.max_pending / 2) {
        if (need_more_peers_callback_) {
            need_more_peers_callback_();
        }
    }
}

} // namespace magnet::protocols


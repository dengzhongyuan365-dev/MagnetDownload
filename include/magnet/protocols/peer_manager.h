#pragma once

#include "peer_connection.h"
#include "bt_message.h"
#include "magnet_types.h"
#include "../network/network_types.h"

#include <asio.hpp>
#include <functional>
#include <memory>
#include <atomic>
#include <mutex>
#include <map>
#include <set>
#include <vector>
#include <deque>
#include <chrono>

namespace magnet::protocols {

// ============================================================================
// 配置参数
// ============================================================================

/**
 * @struct PeerManagerConfig
 * @brief PeerManager 配置参数
 */
struct PeerManagerConfig {
    size_t max_connections{50};         // 最大连接数
    size_t max_connecting{10};          // 最大同时连接中的数量
    size_t max_pending{200};            // 最大等待连接的 Peer 数
    
    std::chrono::seconds connect_timeout{30};
    std::chrono::seconds reconnect_delay{60};   // 重连间隔
    int max_connect_failures{3};                // 最大连接失败次数
    
    size_t max_requests_per_peer{10};   // 每个 Peer 最大并发请求数
    
    std::chrono::seconds peer_evaluation_interval{10};  // Peer 评估间隔
    std::chrono::seconds optimistic_unchoke_interval{30}; // 乐观解阻塞间隔
    size_t unchoke_slots{4};            // 解阻塞槽位数
};

// ============================================================================
// Peer 条目
// ============================================================================

/**
 * @struct PeerEntry
 * @brief 单个 Peer 的管理信息
 */
struct PeerEntry {
    std::shared_ptr<PeerConnection> connection;
    network::TcpEndpoint endpoint;
    
    // 状态
    bool is_connecting{false};
    bool is_connected{false};
    bool is_seed{false};        // 是否是做种者（拥有所有分片）
    
    // 评分（用于选择策略）
    int score{0};
    
    // 时间戳
    std::chrono::steady_clock::time_point added_time;
    std::chrono::steady_clock::time_point last_connect_attempt;
    int connect_failures{0};    // 连接失败次数
    
    // 请求跟踪
    size_t pending_requests{0}; // 待处理请求数
    
    PeerEntry() : added_time(std::chrono::steady_clock::now()) {}
    explicit PeerEntry(const network::TcpEndpoint& ep) 
        : endpoint(ep), added_time(std::chrono::steady_clock::now()) {}
};

// ============================================================================
// 统计信息
// ============================================================================

/**
 * @struct PeerManagerStatistics
 * @brief PeerManager 统计信息
 */
struct PeerManagerStatistics {
    size_t total_peers_known{0};        // 已知 Peer 总数
    size_t peers_connecting{0};         // 连接中的 Peer 数
    size_t peers_connected{0};          // 已连接的 Peer 数
    size_t peers_pending{0};            // 等待连接的 Peer 数
    
    size_t total_bytes_downloaded{0};   // 总下载字节数
    size_t total_bytes_uploaded{0};     // 总上传字节数
    size_t total_pieces_received{0};    // 总接收分片数
    
    void reset() {
        total_peers_known = 0;
        peers_connecting = 0;
        peers_connected = 0;
        peers_pending = 0;
        total_bytes_downloaded = 0;
        total_bytes_uploaded = 0;
        total_pieces_received = 0;
    }
};

// ============================================================================
// 回调类型
// ============================================================================

/** @brief 收到数据块回调 */
using PieceReceivedCallback = std::function<void(
    uint32_t piece_index, 
    uint32_t begin, 
    const std::vector<uint8_t>& data)>;

/** @brief Peer 连接状态变化回调 */
using PeerStatusCallback = std::function<void(
    const network::TcpEndpoint& endpoint,
    bool connected)>;

/** @brief 需要更多 Peer 回调 */
using NeedMorePeersCallback = std::function<void()>;

/** @brief 新 Peer 连接成功回调 */
using NewPeerCallback = std::function<void(std::shared_ptr<PeerConnection> peer)>;

// ============================================================================
// PeerManager 类
// ============================================================================

/**
 * @class PeerManager
 * @brief Peer 连接池管理器
 * 
 * 管理多个 PeerConnection 的生命周期：
 * - 自动连接和断开 Peer
 * - 智能选择最佳 Peer 请求数据
 * - 实现 choke/unchoke 策略
 * - 统计下载/上传信息
 * 
 * 使用示例：
 * @code
 * auto pm = std::make_shared<PeerManager>(io_context, info_hash, "-MT0001-xxxx");
 * 
 * pm->setPieceCallback([](uint32_t piece, uint32_t begin, const auto& data) {
 *     // 保存数据...
 * });
 * 
 * pm->start();
 * pm->addPeers(peers_from_dht);
 * pm->requestBlock({0, 0, 16384});
 * @endcode
 */
class PeerManager : public std::enable_shared_from_this<PeerManager> {
public:
    // ========================================================================
    // 构造和析构
    // ========================================================================
    
    /**
     * @brief 构造函数
     * @param io_context 事件循环
     * @param info_hash 文件的 info_hash
     * @param my_peer_id 本客户端的 peer_id
     * @param config 配置参数
     */
    PeerManager(asio::io_context& io_context,
                const InfoHash& info_hash,
                const std::string& my_peer_id,
                PeerManagerConfig config = {});
    
    /**
     * @brief 析构函数
     */
    ~PeerManager();
    
    // 禁止拷贝
    PeerManager(const PeerManager&) = delete;
    PeerManager& operator=(const PeerManager&) = delete;
    
    // ========================================================================
    // 生命周期管理
    // ========================================================================
    
    /**
     * @brief 启动 PeerManager
     * 
     * 开始接受 Peer 连接请求，启动定时器
     */
    void start();
    
    /**
     * @brief 停止 PeerManager
     * 
     * 断开所有连接，停止定时器
     */
    void stop();
    
    /**
     * @brief 检查是否正在运行
     */
    bool isRunning() const { return running_.load(); }
    
    // ========================================================================
    // Peer 管理
    // ========================================================================
    
    /**
     * @brief 添加单个 Peer
     * @param endpoint Peer 地址
     * @return true 如果成功添加
     */
    bool addPeer(const network::TcpEndpoint& endpoint);
    
    /**
     * @brief 批量添加 Peer
     * @param endpoints Peer 地址列表
     */
    void addPeers(const std::vector<network::TcpEndpoint>& endpoints);
    
    /**
     * @brief 移除 Peer
     * @param endpoint Peer 地址
     */
    void removePeer(const network::TcpEndpoint& endpoint);
    
    // ========================================================================
    // 数据传输
    // ========================================================================
    
    /**
     * @brief 请求数据块
     * @param block 块信息
     * @return true 如果成功发送请求
     */
    bool requestBlock(const BlockInfo& block);
    
    /**
     * @brief 取消数据块请求
     * @param block 块信息
     */
    void cancelBlock(const BlockInfo& block);
    
    /**
     * @brief 广播 Have 消息
     * @param piece_index 分片索引
     */
    void broadcastHave(uint32_t piece_index);
    
    /**
     * @brief 更新本地位图
     * @param bitfield 分片位图
     */
    void updateBitfield(const std::vector<bool>& bitfield);
    
    // ========================================================================
    // 查询
    // ========================================================================
    
    /**
     * @brief 获取拥有指定分片的 Peer
     * @param piece_index 分片索引
     * @return 拥有该分片的 Peer 地址列表
     */
    std::vector<network::TcpEndpoint> getPeersWithPiece(uint32_t piece_index) const;
    
    /**
     * @brief 获取所有已连接的 Peer
     * @return 已连接 Peer 地址列表
     */
    std::vector<network::TcpEndpoint> getConnectedPeers() const;
    
    /**
     * @brief 获取已连接 Peer 数量
     */
    size_t connectedCount() const;
    
    /**
     * @brief 获取统计信息
     */
    PeerManagerStatistics getStatistics() const;
    
    // ========================================================================
    // 回调设置
    // ========================================================================
    
    /** @brief 设置数据块接收回调 */
    void setPieceCallback(PieceReceivedCallback callback);
    
    /** @brief 设置 Peer 状态变化回调 */
    void setPeerStatusCallback(PeerStatusCallback callback);
    
    /** @brief 设置需要更多 Peer 回调 */
    void setNeedMorePeersCallback(NeedMorePeersCallback callback);
    
    /** @brief 设置新 Peer 连接成功回调 */
    void setNewPeerCallback(NewPeerCallback callback);

private:
    // ========================================================================
    // 内部方法
    // ========================================================================
    
    /**
     * @brief 连接到指定 Peer
     */
    void connectToPeer(const network::TcpEndpoint& endpoint);
    
    /**
     * @brief Peer 连接成功处理
     */
    void onPeerConnected(const network::TcpEndpoint& endpoint);
    
    /**
     * @brief Peer 断开连接处理
     */
    void onPeerDisconnected(const network::TcpEndpoint& endpoint, const std::string& reason);
    
    /**
     * @brief 收到数据块处理
     */
    void onPieceReceived(const network::TcpEndpoint& endpoint, const PieceBlock& block);
    
    /**
     * @brief 收到消息处理
     */
    void onPeerMessage(const network::TcpEndpoint& endpoint, const BtMessage& msg);
    
    /**
     * @brief 尝试连接更多 Peer
     */
    void tryConnectMore();
    
    /**
     * @brief 启动定时器
     */
    void startTimers();
    
    /**
     * @brief 定期评估 Peer
     */
    void evaluatePeers();
    
    /**
     * @brief 计算 Peer 评分
     */
    int calculatePeerScore(const PeerEntry& entry) const;
    
    /**
     * @brief 选择最佳 Peer 请求指定分片
     */
    PeerEntry* selectBestPeerForPiece(uint32_t piece_index);
    
    /**
     * @brief 检查是否需要更多 Peer
     */
    void checkNeedMorePeers();

private:
    asio::io_context& io_context_;
    InfoHash info_hash_;
    std::string my_peer_id_;
    PeerManagerConfig config_;
    
    std::atomic<bool> running_{false};
    
    // Peer 管理
    mutable std::mutex peers_mutex_;
    std::map<std::string, PeerEntry> peers_;  // key: "ip:port"
    std::set<std::string> pending_peers_;     // 等待连接的 Peer
    std::set<std::string> connecting_peers_;  // 连接中的 Peer
    std::set<std::string> connected_peers_;   // 已连接的 Peer
    
    // 本地位图
    mutable std::mutex bitfield_mutex_;
    std::vector<bool> my_bitfield_;
    
    // 统计信息
    mutable std::mutex stats_mutex_;
    PeerManagerStatistics statistics_;
    
    // 定时器
    asio::steady_timer evaluation_timer_;
    int optimistic_unchoke_counter_{0};
    
    // 回调
    PieceReceivedCallback piece_callback_;
    PeerStatusCallback peer_status_callback_;
    NeedMorePeersCallback need_more_peers_callback_;
    NewPeerCallback new_peer_callback_;
    
    // 辅助方法
    static std::string endpointToKey(const network::TcpEndpoint& ep) {
        return ep.ip + ":" + std::to_string(ep.port);
    }
};

} // namespace magnet::protocols


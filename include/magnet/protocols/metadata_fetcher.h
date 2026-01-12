#pragma once

/**
 * @file metadata_fetcher.h
 * @brief 元数据获取器 - 从 peers 获取 torrent 元数据
 * 
 * 协调多个 peers 并行获取元数据块，处理超时和重试，
 * 最终组装并验证完整的 torrent 元数据。
 */

#include "magnet_types.h"
#include "metadata_extension.h"
#include "peer_connection.h"

#include <asio.hpp>
#include <memory>
#include <map>
#include <set>
#include <vector>
#include <functional>
#include <mutex>
#include <atomic>
#include <chrono>

namespace magnet::protocols {

// ============================================================================
// 配置
// ============================================================================

struct MetadataFetcherConfig {
    // 超时设置
    std::chrono::seconds fetch_timeout{120};      // 总超时
    std::chrono::seconds piece_timeout{30};       // 单块超时
    
    // 重试设置
    int max_retries{3};                           // 最大重试次数
    size_t max_peers{5};                          // 最大并行 peers
    
    // 块大小（BEP-9 规定）
    static constexpr size_t kBlockSize = 16384;   // 16KB
    
    // 元数据大小限制
    size_t max_metadata_size{10 * 1024 * 1024};   // 10MB
};

// ============================================================================
// 元数据获取器
// ============================================================================

class MetadataFetcher : public std::enable_shared_from_this<MetadataFetcher> {
public:
    // 回调类型
    using MetadataCallback = std::function<void(const TorrentMetadata*, MetadataError)>;
    
    /**
     * @brief 构造函数
     * @param io_context 事件循环
     * @param info_hash 目标 torrent 的 info_hash
     * @param config 配置参数
     */
    MetadataFetcher(asio::io_context& io_context,
                    const InfoHash& info_hash,
                    MetadataFetcherConfig config = {});
    
    ~MetadataFetcher();
    
    // 禁止拷贝
    MetadataFetcher(const MetadataFetcher&) = delete;
    MetadataFetcher& operator=(const MetadataFetcher&) = delete;
    
    // ========================================================================
    // 生命周期
    // ========================================================================
    
    /**
     * @brief 开始获取元数据
     * @param callback 完成回调（成功时 metadata 非空，error 为 Success）
     */
    void start(MetadataCallback callback);
    
    /**
     * @brief 停止获取
     */
    void stop();
    
    /**
     * @brief 是否正在运行
     */
    bool isRunning() const { return running_.load(); }
    
    // ========================================================================
    // Peer 管理
    // ========================================================================
    
    /**
     * @brief 添加 peer
     * @param peer Peer 连接
     * 
     * 添加后会自动发送扩展握手，并在收到对方握手后开始请求元数据
     */
    void addPeer(std::shared_ptr<PeerConnection> peer);
    
    /**
     * @brief 移除 peer
     * @param peer Peer 连接
     */
    void removePeer(std::shared_ptr<PeerConnection> peer);
    
    // ========================================================================
    // 事件处理（由 PeerConnection 调用）
    // ========================================================================
    
    /**
     * @brief 处理扩展握手
     * @param peer 来源 peer
     * @param handshake 握手数据
     */
    void onExtensionHandshake(PeerConnection* peer, const ExtensionHandshake& handshake);
    
    /**
     * @brief 处理元数据消息
     * @param peer 来源 peer
     * @param message 消息数据
     */
    void onMetadataMessage(PeerConnection* peer, const MetadataMessage& message);
    
    /**
     * @brief 处理 peer 断开
     * @param peer 断开的 peer
     */
    void onPeerDisconnected(PeerConnection* peer);
    
    // ========================================================================
    // 状态查询
    // ========================================================================
    
    /**
     * @brief 是否已完成
     */
    bool isComplete() const { return complete_.load(); }
    
    /**
     * @brief 获取进度 (0.0 - 1.0)
     */
    float progress() const;
    
    /**
     * @brief 获取元数据大小（如果已知）
     */
    std::optional<size_t> metadataSize() const;
    
    /**
     * @brief 获取已连接的 peer 数量
     */
    size_t peerCount() const;
    
private:
    // ========================================================================
    // 内部状态
    // ========================================================================
    
    enum class PieceState {
        Pending,    // 等待请求
        Requested,  // 已请求
        Received    // 已收到
    };
    
    struct PeerState {
        std::shared_ptr<PeerConnection> connection;
        uint8_t their_metadata_id{0};   // 对方的 ut_metadata ID
        size_t their_metadata_size{0};  // 对方报告的元数据大小
        bool handshake_sent{false};     // 是否已发送握手
        bool supports_metadata{false};  // 是否支持元数据扩展
        std::set<uint32_t> requested_pieces;  // 已请求的块
        int failures{0};                // 失败次数
    };
    
    // ========================================================================
    // 内部方法
    // ========================================================================
    
    void initializePieces(size_t metadata_size);
    void requestNextPiece(PeerState& peer_state);
    void requestPieceFromPeer(PeerState& peer_state, uint32_t piece_index);
    void onPieceReceived(uint32_t piece_index, const std::vector<uint8_t>& data);
    void onPieceRejected(PeerState& peer_state, uint32_t piece_index);
    void checkCompletion();
    bool verifyAndParseMetadata();
    void complete(const TorrentMetadata* metadata, MetadataError error);
    void startTimeoutTimer();
    void onTimeout();
    PeerState* findPeerState(PeerConnection* peer);
    uint32_t findNextPiece(const PeerState& peer_state);
    
private:
    asio::io_context& io_context_;
    InfoHash info_hash_;
    MetadataFetcherConfig config_;
    
    std::atomic<bool> running_{false};
    std::atomic<bool> complete_{false};
    
    MetadataCallback callback_;
    
    // 元数据相关
    mutable std::mutex mutex_;
    size_t metadata_size_{0};
    std::vector<uint8_t> metadata_buffer_;
    std::vector<PieceState> piece_states_;
    size_t pieces_received_{0};
    
    // Peer 管理
    std::map<PeerConnection*, PeerState> peers_;
    
    // 超时定时器
    asio::steady_timer timeout_timer_;
    
    // 结果
    std::unique_ptr<TorrentMetadata> result_;
};

} // namespace magnet::protocols


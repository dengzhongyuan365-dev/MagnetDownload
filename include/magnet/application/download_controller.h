#pragma once

#include "../protocols/magnet_uri_parser.h"
#include "../protocols/dht_client.h"
#include "../protocols/peer_manager.h"
#include "../protocols/bt_message.h"
#include "../protocols/metadata_fetcher.h"

#include <asio.hpp>
#include <functional>
#include <memory>
#include <atomic>
#include <mutex>
#include <vector>
#include <map>
#include <chrono>

namespace magnet::application {

// ============================================================================
// 下载状态
// ============================================================================

enum class DownloadState {
    Idle,               // 初始状态
    ResolvingMetadata,  // 正在获取元数据
    Downloading,        // 正在下载
    Paused,             // 已暂停
    Verifying,          // 验证中
    Completed,          // 完成
    Failed,             // 失败
    Stopped             // 已停止
};

/**
 * @brief 状态转字符串
 */
inline const char* downloadStateToString(DownloadState state) {
    switch (state) {
        case DownloadState::Idle: return "Idle";
        case DownloadState::ResolvingMetadata: return "ResolvingMetadata";
        case DownloadState::Downloading: return "Downloading";
        case DownloadState::Paused: return "Paused";
        case DownloadState::Verifying: return "Verifying";
        case DownloadState::Completed: return "Completed";
        case DownloadState::Failed: return "Failed";
        case DownloadState::Stopped: return "Stopped";
        default: return "Unknown";
    }
}

// ============================================================================
// 下载配置
// ============================================================================

/**
 * @struct DownloadConfig
 * @brief 下载任务配置
 */
struct DownloadConfig {
    std::string magnet_uri;             // 磁力链接
    std::string save_path;              // 保存路径
    
    size_t max_connections{50};         // 最大连接数
    size_t max_download_speed{0};       // 最大下载速度 (0=无限制)
    size_t max_upload_speed{0};         // 最大上传速度
    
    bool verify_on_complete{true};      // 完成后验证
    bool auto_start{true};              // 自动开始
    
    std::chrono::seconds metadata_timeout{300}; // 元数据获取超时 (5分钟)
    std::chrono::seconds peer_search_interval{30}; // Peer 搜索间隔
};

// ============================================================================
// 下载进度
// ============================================================================

/**
 * @struct DownloadProgress
 * @brief 下载进度信息
 */
struct DownloadProgress {
    size_t total_size{0};           // 总大小（字节）
    size_t downloaded_size{0};      // 已下载大小
    size_t uploaded_size{0};        // 已上传大小
    
    size_t total_pieces{0};         // 总分片数
    size_t completed_pieces{0};     // 已完成分片数
    size_t pending_pieces{0};       // 正在下载的分片数
    
    double download_speed{0};       // 下载速度 (bytes/s)
    double upload_speed{0};         // 上传速度 (bytes/s)
    
    size_t connected_peers{0};      // 已连接 Peer 数
    size_t total_peers{0};          // 已知 Peer 总数
    
    /**
     * @brief 获取下载进度百分比
     */
    double progressPercent() const {
        return total_size > 0 ? 
            static_cast<double>(downloaded_size) / total_size * 100.0 : 0;
    }
    
    /**
     * @brief 获取预计剩余时间
     */
    std::chrono::seconds eta() const {
        if (download_speed <= 0 || downloaded_size >= total_size) {
            return std::chrono::seconds{0};
        }
        size_t remaining = total_size - downloaded_size;
        return std::chrono::seconds(static_cast<long long>(remaining / download_speed));
    }
};

// ============================================================================
// 种子元数据
// ============================================================================

/**
 * @struct TorrentMetadata
 * @brief 种子元数据（文件信息）
 */
struct TorrentMetadata {
    protocols::InfoHash info_hash;
    std::string name;                   // 文件/目录名
    size_t total_size{0};               // 总大小
    size_t piece_length{0};             // 分片大小
    size_t piece_count{0};              // 分片数量
    std::vector<std::array<uint8_t, 20>> piece_hashes; // 每个分片的 SHA1
    
    /**
     * @struct FileInfo
     * @brief 单个文件信息
     */
    struct FileInfo {
        std::string path;               // 相对路径
        size_t size{0};                 // 文件大小
        size_t start_piece{0};          // 起始分片索引
        size_t end_piece{0};            // 结束分片索引
    };
    std::vector<FileInfo> files;        // 文件列表（单文件时只有一项）
    
    bool isValid() const {
        return !name.empty() && total_size > 0 && piece_length > 0;
    }
    
    bool isSingleFile() const {
        return files.size() == 1;
    }
};

// ============================================================================
// 分片状态
// ============================================================================

enum class PieceState {
    Missing,        // 未下载
    Pending,        // 正在下载
    Downloaded,     // 已下载，待验证
    Verified,       // 已验证
    Failed          // 验证失败
};

/**
 * @struct PieceInfo
 * @brief 分片信息
 */
struct PieceInfo {
    uint32_t index{0};
    PieceState state{PieceState::Missing};
    size_t size{0};
    size_t downloaded{0};
    std::vector<bool> blocks;           // 块下载状态
    std::vector<uint8_t> data;          // 分片数据
    
    bool isComplete() const {
        return downloaded >= size;
    }
    
    double progress() const {
        return size > 0 ? static_cast<double>(downloaded) / size : 0;
    }
};

// ============================================================================
// 回调类型
// ============================================================================

/** @brief 状态变化回调 */
using DownloadStateCallback = std::function<void(DownloadState state)>;

/** @brief 进度更新回调 */
using DownloadProgressCallback = std::function<void(const DownloadProgress& progress)>;

/** @brief 完成回调 */
using DownloadCompletedCallback = std::function<void(bool success, const std::string& error)>;

/** @brief 元数据获取回调 */
using MetadataReceivedCallback = std::function<void(const TorrentMetadata& metadata)>;

// ============================================================================
// DownloadController 类
// ============================================================================

/**
 * @class DownloadController
 * @brief 下载控制器
 * 
 * 协调所有底层模块完成磁力链接下载：
 * - 解析 Magnet URI
 * - 通过 DHT 查找 Peer
 * - 管理 Peer 连接和数据下载
 * - 协调存储
 * 
 * 使用示例：
 * @code
 * auto controller = std::make_shared<DownloadController>(io_context);
 * 
 * controller->setProgressCallback([](const DownloadProgress& p) {
 *     std::cout << "Progress: " << p.progressPercent() << "%\n";
 * });
 * 
 * DownloadConfig config;
 * config.magnet_uri = "magnet:?xt=urn:btih:...";
 * config.save_path = "/downloads/";
 * 
 * controller->start(config);
 * @endcode
 */
class DownloadController : public std::enable_shared_from_this<DownloadController> {
public:
    // 常量
    static constexpr size_t kBlockSize = 16384;  // 16KB 块大小
    
    // ========================================================================
    // 构造和析构
    // ========================================================================
    
    /**
     * @brief 构造函数
     * @param io_context 事件循环
     */
    explicit DownloadController(asio::io_context& io_context);
    
    /**
     * @brief 析构函数
     */
    ~DownloadController();
    
    // 禁止拷贝
    DownloadController(const DownloadController&) = delete;
    DownloadController& operator=(const DownloadController&) = delete;
    
    // ========================================================================
    // 生命周期管理
    // ========================================================================
    
    /**
     * @brief 启动下载
     * @param config 下载配置
     * @return true 如果成功启动
     */
    bool start(const DownloadConfig& config);
    
    /**
     * @brief 暂停下载
     */
    void pause();
    
    /**
     * @brief 恢复下载
     */
    void resume();
    
    /**
     * @brief 停止下载
     */
    void stop();
    
    // ========================================================================
    // 状态查询
    // ========================================================================
    
    /**
     * @brief 获取当前状态
     */
    DownloadState state() const { return state_.load(); }
    
    /**
     * @brief 获取下载进度
     */
    DownloadProgress progress() const;
    
    /**
     * @brief 获取元数据
     */
    TorrentMetadata metadata() const;
    
    /**
     * @brief 获取配置
     */
    const DownloadConfig& config() const { return config_; }
    
    // ========================================================================
    // 回调设置
    // ========================================================================
    
    /** @brief 设置状态变化回调 */
    void setStateCallback(DownloadStateCallback callback);
    
    /** @brief 设置进度更新回调 */
    void setProgressCallback(DownloadProgressCallback callback);
    
    /** @brief 设置完成回调 */
    void setCompletedCallback(DownloadCompletedCallback callback);
    
    /** @brief 设置元数据回调 */
    void setMetadataCallback(MetadataReceivedCallback callback);
    
    // ========================================================================
    // 元数据设置（简化版，跳过 BEP-9）
    // ========================================================================
    
    /**
     * @brief 设置元数据（用于测试或已知元数据的情况）
     * @param metadata 种子元数据
     */
    void setMetadata(const TorrentMetadata& metadata);

private:
    // ========================================================================
    // 内部方法
    // ========================================================================
    
    /**
     * @brief 初始化 DHT
     */
    void initializeDht();
    
    /**
     * @brief 开始查找 Peer
     */
    void findPeers();
    
    /**
     * @brief Peer 发现回调
     */
    void onPeersFound(const std::vector<protocols::PeerInfo>& peers);
    
    /**
     * @brief 收到数据块回调
     */
    void onPieceReceived(uint32_t piece_index, uint32_t begin, 
                         const std::vector<uint8_t>& data);
    
    /**
     * @brief Peer 状态变化回调
     */
    void onPeerStatusChanged(const network::TcpEndpoint& endpoint, bool connected);
    
    /**
     * @brief 初始化元数据获取器
     */
    void initializeMetadataFetcher();
    
    /**
     * @brief 处理新 Peer 连接（用于元数据获取）
     */
    void onNewPeerConnected(std::shared_ptr<protocols::PeerConnection> peer);
    
    /**
     * @brief 元数据获取完成回调
     */
    void onMetadataFetched(const protocols::TorrentMetadata* metadata, 
                           protocols::MetadataError error);
    
    /**
     * @brief 初始化分片状态
     */
    void initializePieces();
    
    /**
     * @brief 选择下一个要下载的分片
     * @return 分片索引，-1 表示没有可下载的
     */
    int32_t selectNextPiece();
    
    /**
     * @brief 请求分片
     */
    void requestPiece(uint32_t piece_index);
    
    /**
     * @brief 请求更多数据块
     */
    void requestMoreBlocks();
    
    /**
     * @brief 验证分片
     */
    bool verifyPiece(uint32_t piece_index);
    
    /**
     * @brief 更新进度
     */
    void updateProgress();
    
    /**
     * @brief 检查是否完成
     */
    void checkCompletion();
    
    /**
     * @brief 设置状态并通知
     */
    void setState(DownloadState new_state);
    
    /**
     * @brief 报告错误并失败
     */
    void fail(const std::string& error);
    
    /**
     * @brief 启动进度更新定时器
     */
    void startProgressTimer();
    
    /**
     * @brief 启动 Peer 搜索定时器
     */
    void startPeerSearchTimer();
    
    /**
     * @brief 计算分片大小
     */
    size_t getPieceSize(uint32_t piece_index) const;
    
    /**
     * @brief 生成 Peer ID
     */
    static std::string generatePeerId();

private:
    asio::io_context& io_context_;
    DownloadConfig config_;
    
    // 状态
    std::atomic<DownloadState> state_{DownloadState::Idle};
    std::string error_message_;
    
    // 元数据
    mutable std::mutex metadata_mutex_;
    TorrentMetadata metadata_;
    bool has_metadata_{false};
    
    // 分片管理
    mutable std::mutex pieces_mutex_;
    std::vector<PieceInfo> pieces_;
    std::vector<bool> bitfield_;
    
    // 组件
    std::shared_ptr<protocols::DhtClient> dht_client_;
    std::shared_ptr<protocols::PeerManager> peer_manager_;
    std::shared_ptr<protocols::MetadataFetcher> metadata_fetcher_;
    std::string my_peer_id_;
    
    // 进度
    mutable std::mutex progress_mutex_;
    DownloadProgress current_progress_;
    std::chrono::steady_clock::time_point start_time_;
    std::chrono::steady_clock::time_point last_progress_update_;
    size_t last_downloaded_size_{0};
    
    // 定时器
    asio::steady_timer progress_timer_;
    asio::steady_timer peer_search_timer_;
    asio::steady_timer metadata_timeout_timer_;
    
    // 回调
    DownloadStateCallback state_callback_;
    DownloadProgressCallback progress_callback_;
    DownloadCompletedCallback completed_callback_;
    MetadataReceivedCallback metadata_callback_;
};

} // namespace magnet::application


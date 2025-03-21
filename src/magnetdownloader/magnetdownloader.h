#pragma once

#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <functional>
#include <filesystem>
#include <map>
#include <set>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <asio.hpp>
#include "../magnetparser/magnetparser.h"

// 前向声明
namespace bt {
    class DHT;
    class Tracker;
    class PeerConnection;
    class PieceManager;
    class TorrentMetadata;
}

// 下载状态枚举
enum class DownloadStatus {
    CONNECTING,    // 正在连接到Tracker/DHT网络
    METADATA,      // 获取元数据中
    DOWNLOADING,   // 正在下载
    SEEDING,       // 做种中
    PAUSED,        // 已暂停
    COMPLETED,     // 已完成
    ERROR          // 出错
};

// 下载进度信息结构
struct DownloadProgress {
    std::string filename;           // 文件名
    int64_t total_size;             // 总大小（字节）
    int64_t downloaded;             // 已下载（字节）
    float progress;                 // 进度（0.0-1.0）
    float download_speed;           // 下载速度（字节/秒）
    float upload_speed;             // 上传速度（字节/秒）
    int seeds;                      // 种子数
    int peers;                      // 连接节点数
    DownloadStatus status;          // 当前状态
    std::string error_message;      // 错误消息（如有）
};

// Peer信息结构
struct PeerInfo {
    std::string ip;
    uint16_t port;
    std::string peer_id;
    bool is_seeder;
    
    // 用于std::set比较
    bool operator<(const PeerInfo& other) const {
        return ip < other.ip || (ip == other.ip && port < other.port);
    }
};

// 进度回调函数类型
using ProgressCallback = std::function<void(const DownloadProgress&)>;

/**
 * 磁力链接下载管理器
 * 
 * 该类负责使用自实现的BitTorrent协议从磁力链接下载内容
 */
class MagnetDownloader {
public:
    /**
     * 构造函数
     * 
     * @param save_path 文件保存路径
     * @param progress_callback 进度更新回调函数
     */
    MagnetDownloader(
        const std::filesystem::path& save_path,
        ProgressCallback progress_callback = nullptr
    );
    
    /**
     * 析构函数
     */
    ~MagnetDownloader();
    
    /**
     * 禁用复制构造和赋值
     */
    MagnetDownloader(const MagnetDownloader&) = delete;
    MagnetDownloader& operator=(const MagnetDownloader&) = delete;
    
    /**
     * 从解析后的磁力链接开始下载
     * 
     * @param parser 已解析的磁力链接解析器
     * @return 是否成功开始下载
     */
    bool start_download(const MagnetParserStateMachine& parser);
    
    /**
     * 从磁力链接URL直接下载
     * 
     * @param magnet_uri 磁力链接URL
     * @return 是否成功开始下载
     */
    bool download(const std::string& magnet_uri);
    
    /**
     * 暂停下载
     */
    void pause();
    
    /**
     * 恢复下载
     */
    void resume();
    
    /**
     * 取消并删除下载
     * 
     * @param delete_files 是否同时删除已下载的文件
     */
    void cancel(bool delete_files = false);
    
    /**
     * 获取当前下载状态
     * 
     * @return 下载状态
     */
    DownloadStatus get_status() const;
    
    /**
     * 获取当前下载进度信息
     * 
     * @return 下载进度信息
     */
    DownloadProgress get_progress() const;
    
    /**
     * 设置最大下载速度限制
     * 
     * @param limit_kbps 限制（KB/s，0表示无限制）
     */
    void set_download_rate_limit(int limit_kbps);
    
    /**
     * 设置最大上传速度限制
     * 
     * @param limit_kbps 限制（KB/s，0表示无限制）
     */
    void set_upload_rate_limit(int limit_kbps);

private:
    // 初始化网络组件
    void initialize();
    
    // 从InfoHash开始下载
    bool start_download_from_hash(const std::string& info_hash, 
                                 const std::vector<std::string>& trackers,
                                 const std::string& display_name);
    
    // 获取元数据（.torrent文件内容）
    bool retrieve_metadata();
    
    // 连接到Tracker和DHT网络
    void connect_to_network();
    
    // 处理Tracker回应
    void handle_tracker_response(const std::vector<PeerInfo>& peers);
    
    // 处理DHT发现的节点
    void handle_dht_peers(const std::vector<PeerInfo>& peers);
    
    // 管理Peer连接
    void manage_peers();
    
    // 连接到新的Peer
    void connect_to_peer(const PeerInfo& peer);
    
    // 处理来自Peer的消息
    void handle_peer_message(const std::shared_ptr<bt::PeerConnection>& peer, 
                            const std::vector<uint8_t>& message);
    
    // 请求新的区块
    void request_pieces();
    
    // 写入数据到文件
    void write_piece_to_disk(int piece_index, const std::vector<uint8_t>& data);
    
    // 更新状态监控线程
    void update_status_thread();
    
    // 更新并通知进度信息
    void update_progress();
    
    // Asio组件
    std::unique_ptr<asio::io_context> io_context_;
    std::shared_ptr<std::thread> io_thread_;
    std::unique_ptr<asio::io_context::work> work_guard_;
    
    // BitTorrent组件
    std::unique_ptr<bt::DHT> dht_;
    std::vector<std::unique_ptr<bt::Tracker>> trackers_;
    std::map<std::string, std::shared_ptr<bt::PeerConnection>> peer_connections_;
    std::unique_ptr<bt::PieceManager> piece_manager_;
    std::shared_ptr<bt::TorrentMetadata> metadata_;
    
    // 下载信息
    std::string info_hash_;
    std::string display_name_;
    std::string peer_id_;
    
    // Peer管理
    std::set<PeerInfo> known_peers_;
    std::set<PeerInfo> connected_peers_;
    std::mutex peers_mutex_;
    
    // 配置参数
    std::filesystem::path save_path_;
    ProgressCallback progress_callback_;
    int max_connections_ = 50;
    int download_rate_limit_ = 0;  // KB/s, 0表示无限制
    int upload_rate_limit_ = 0;    // KB/s, 0表示无限制
    
    // 状态变量
    std::atomic<DownloadStatus> status_;
    std::atomic<bool> should_stop_;
    std::atomic<bool> is_paused_;
    DownloadProgress current_progress_;
    std::mutex progress_mutex_;
    
    // 状态监控线程
    std::unique_ptr<std::thread> status_thread_;
};

namespace bt {
    /**
     * 分布式哈希表(DHT)实现
     * 实现Kademlia DHT协议用于对等节点发现
     */
    class DHT {
    public:
        DHT(asio::io_context& io_context, const std::string& info_hash);
        ~DHT();
        
        // 启动DHT服务
        void start(uint16_t port);
        
        // 停止DHT服务
        void stop();
        
        // 设置新Peer回调
        void set_peer_callback(std::function<void(const std::vector<PeerInfo>&)> callback);
        
        // 从种子节点开始引导
        void bootstrap_from_nodes(const std::vector<std::pair<std::string, uint16_t>>& nodes);
        
    private:
        // DHT节点ID (20字节)
        std::array<uint8_t, 20> node_id_;
        
        // 目标InfoHash
        std::array<uint8_t, 20> target_info_hash_;
        
        // Asio组件
        asio::io_context& io_context_;
        asio::ip::udp::socket socket_;
        
        // 回调函数
        std::function<void(const std::vector<PeerInfo>&)> peer_callback_;
        
        // DHT内部实现...
    };

    /**
     * Tracker通信实现
     * 支持HTTP和UDP Tracker协议
     */
    class Tracker {
    public:
        enum class Type {
            HTTP,
            UDP
        };
        
        Tracker(asio::io_context& io_context, 
                const std::string& url, 
                const std::string& info_hash,
                const std::string& peer_id);
        
        ~Tracker();
        
        // 获取Tracker类型
        Type get_type() const;
        
        // 发送announce请求
        void announce(uint16_t port, int64_t uploaded, int64_t downloaded, 
                     int64_t left, bool seeder);
        
        // 发送scrape请求
        void scrape();
        
        // 设置announce回调
        void set_announce_callback(std::function<void(const std::vector<PeerInfo>&)> callback);
        
        // 设置scrape回调
        void set_scrape_callback(std::function<void(int seeders, int leechers, int complete)> callback);
        
    private:
        // Tracker URL解析
        void parse_url(const std::string& url);
        
        // HTTP Tracker实现
        void http_announce();
        void http_scrape();
        void handle_http_response(const std::string& response);
        
        // UDP Tracker实现
        void udp_announce();
        void udp_scrape();
        void handle_udp_packet(const std::vector<uint8_t>& packet);
        
        // Tracker类型
        Type type_;
        
        // Asio组件
        asio::io_context& io_context_;
        
        // Tracker信息
        std::string host_;
        std::string path_;
        uint16_t port_;
        
        // 下载信息
        std::string info_hash_;
        std::string peer_id_;
        uint16_t local_port_;
        int64_t uploaded_;
        int64_t downloaded_;
        int64_t left_;
        bool seeder_;
        
        // 回调函数
        std::function<void(const std::vector<PeerInfo>&)> announce_callback_;
        std::function<void(int seeders, int leechers, int complete)> scrape_callback_;
    };

    /**
     * Peer连接实现
     * 实现BitTorrent对等协议(Peer Wire Protocol)
     */
    class PeerConnection : public std::enable_shared_from_this<PeerConnection> {
    public:
        PeerConnection(asio::io_context& io_context, 
                      const std::string& ip, 
                      uint16_t port,
                      const std::string& info_hash,
                      const std::string& peer_id);
        
        ~PeerConnection();
        
        // 连接到Peer
        void connect();
        
        // 断开连接
        void disconnect();
        
        // 设置消息回调
        void set_message_callback(std::function<void(const std::shared_ptr<PeerConnection>&, 
                                                    const std::vector<uint8_t>&)> callback);
        
        // 发送消息
        void send_message(const std::vector<uint8_t>& message);
        
        // 发送握手消息
        void send_handshake();
        
        // 发送interested消息
        void send_interested();
        
        // 发送not interested消息
        void send_not_interested();
        
        // 发送choke消息
        void send_choke();
        
        // 发送unchoke消息
        void send_unchoke();
        
        // 发送have消息
        void send_have(int piece_index);
        
        // 发送bitfield消息
        void send_bitfield(const std::vector<bool>& bitfield);
        
        // 发送request消息
        void send_request(int piece_index, int block_offset, int block_length);
        
        // 发送piece消息(数据块)
        void send_piece(int piece_index, int block_offset, const std::vector<uint8_t>& block);
        
        // 发送cancel消息
        void send_cancel(int piece_index, int block_offset, int block_length);
        
        // 获取对方是否choked我们
        bool am_choked() const;
        
        // 获取对方是否interested我们
        bool am_interested() const;
        
        // 获取我们是否choked对方
        bool peer_choked() const;
        
        // 获取我们是否interested对方
        bool peer_interested() const;
        
        // 获取对方的bitfield
        const std::vector<bool>& get_bitfield() const;
        
        // 检查对方是否拥有某个块
        bool has_piece(int piece_index) const;
        
        // 获取IP地址
        const std::string& get_ip() const;
        
        // 获取端口
        uint16_t get_port() const;
        
    private:
        // 读取消息
        void read_message();
        
        // 处理消息
        void handle_message(const std::vector<uint8_t>& message);
        
        // 处理握手响应
        void handle_handshake(const std::vector<uint8_t>& message);
        
        // 处理各种消息类型
        void handle_keep_alive();
        void handle_choke();
        void handle_unchoke();
        void handle_interested();
        void handle_not_interested();
        void handle_have(const std::vector<uint8_t>& message);
        void handle_bitfield(const std::vector<uint8_t>& message);
        void handle_request(const std::vector<uint8_t>& message);
        void handle_piece(const std::vector<uint8_t>& message);
        void handle_cancel(const std::vector<uint8_t>& message);
        
        // Asio组件
        asio::io_context& io_context_;
        asio::ip::tcp::socket socket_;
        asio::ip::tcp::endpoint endpoint_;
        
        // 连接信息
        std::string info_hash_;
        std::string peer_id_;
        std::string remote_peer_id_;
        std::string ip_;
        uint16_t port_;
        
        // 状态标志
        bool connected_;
        bool handshake_completed_;
        bool am_choked_;
        bool am_interested_;
        bool peer_choked_;
        bool peer_interested_;
        
        // Peer数据
        std::vector<bool> bitfield_;
        
        // 接收缓冲区
        std::vector<uint8_t> receive_buffer_;
        
        // 回调函数
        std::function<void(const std::shared_ptr<PeerConnection>&, 
                          const std::vector<uint8_t>&)> message_callback_;
    };

    /**
     * 块管理实现
     * 管理文件块的下载、验证和存储
     */
    class PieceManager {
    public:
        // 块请求结构
        struct BlockRequest {
            int piece_index;
            int block_offset;
            int block_length;
        };
        
        PieceManager(const std::string& info_hash, 
                    const std::filesystem::path& save_path);
        
        ~PieceManager();
        
        // 设置元数据
        void set_metadata(std::shared_ptr<TorrentMetadata> metadata);
        
        // 添加新的块数据
        bool add_piece(int piece_index, int block_offset, 
                      const std::vector<uint8_t>& block_data);
        
        // 获取下一个待请求的块
        std::optional<BlockRequest> get_next_request(const std::shared_ptr<PeerConnection>& peer);
        
        // 获取本地拥有的块位图
        std::vector<bool> get_bitfield() const;
        
        // 检查是否拥有某个块
        bool has_piece(int piece_index) const;
        
        // 获取下载进度
        float get_progress() const;
        
        // 获取下载的总字节数
        int64_t get_downloaded() const;
        
        // 获取总字节数
        int64_t get_total_size() const;
        
        // 检查是否下载完成
        bool is_complete() const;
        
        // 验证数据完整性
        bool verify_data();
        
    private:
        // 初始化已下载的块
        void initialize_pieces();
        
        // 写块数据到磁盘
        bool write_block(int piece_index, int block_offset, 
                        const std::vector<uint8_t>& block_data);
        
        // 验证块的完整性(SHA1校验)
        bool verify_piece(int piece_index);
        
        // 文件信息
        std::string info_hash_;
        std::filesystem::path save_path_;
        
        // 元数据信息
        std::shared_ptr<TorrentMetadata> metadata_;
        
        // 块管理
        struct Piece {
            std::vector<bool> blocks_present;
            std::vector<std::vector<uint8_t>> blocks;
            bool is_complete;
            bool is_verified;
        };
        
        std::vector<Piece> pieces_;
        std::vector<bool> piece_bitfield_;
        
        // 统计信息
        int64_t downloaded_;
        int64_t total_size_;
        
        // 同步
        std::mutex mutex_;
    };

    /**
     * Torrent元数据实现
     * 存储和解析.torrent文件格式
     */
    class TorrentMetadata {
    public:
        struct FileInfo {
            std::string path;
            int64_t length;
        };
        
        TorrentMetadata();
        ~TorrentMetadata();
        
        // 从元数据加载(.torrent文件内容)
        bool load_from_data(const std::vector<uint8_t>& data);
        
        // 从InfoHash获取元数据(BEP 9)
        bool load_from_info_hash(const std::string& info_hash);
        
        // 获取InfoHash
        std::string get_info_hash() const;
        
        // 获取名称
        std::string get_name() const;
        
        // 获取注释
        std::string get_comment() const;
        
        // 获取创建者
        std::string get_created_by() const;
        
        // 获取创建日期
        int64_t get_creation_date() const;
        
        // 获取块大小
        int get_piece_length() const;
        
        // 获取块数量
        int get_piece_count() const;
        
        // 获取总大小
        int64_t get_total_size() const;
        
        // 获取文件列表
        const std::vector<FileInfo>& get_files() const;
        
        // 获取块哈希
        const std::vector<std::string>& get_piece_hashes() const;
        
        // 是否为单文件种子
        bool is_single_file() const;
        
    private:
        // 元数据信息
        std::string info_hash_;
        std::string name_;
        std::string comment_;
        std::string created_by_;
        int64_t creation_date_;
        int piece_length_;
        std::vector<std::string> piece_hashes_;
        bool is_single_file_;
        int64_t total_size_;
        
        // 文件信息
        std::vector<FileInfo> files_;
    };
} 
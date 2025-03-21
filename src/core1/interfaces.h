#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <variant>
#include <chrono>
#include <future>
#include <filesystem>

namespace bt {

// 前置声明
class InfoHash;
class TorrentMetadata;
struct PeerInfo;
struct DownloadProgress;

// 使用C++17的std::byte进行二进制数据处理
using ByteSpan = std::span<const std::byte>;
using ByteBuffer = std::vector<std::byte>;
using FileSize = std::uint64_t;

/**
 * 网络端点类（表示IP和端口）
 */
class Endpoint {
public:
    virtual ~Endpoint() = default;
    virtual std::string getIP() const = 0;
    virtual uint16_t getPort() const = 0;
    virtual bool operator==(const Endpoint& other) const = 0;
};

/**
 * @brief 网络管理接口
 * 负责底层网络通信，包括TCP和UDP
 */
class INetworkManager {
public:
    virtual ~INetworkManager() = default;
    
    // UDP相关功能
    virtual void sendUDP(ByteSpan data, const Endpoint& endpoint) = 0;
    virtual void listenUDP(uint16_t port) = 0;
    virtual void registerUDPDataHandler(std::function<void(ByteSpan, const Endpoint&)> handler) = 0;
    
    // TCP相关功能
    virtual std::shared_ptr<class ITCPConnection> connectTCP(const Endpoint& endpoint) = 0;
    virtual void listenTCP(uint16_t port) = 0;
    virtual void registerTCPConnectionHandler(std::function<void(std::shared_ptr<class ITCPConnection>)> handler) = 0;
};

/**
 * @brief TCP连接接口
 * 代表单个TCP连接
 */
class ITCPConnection : public std::enable_shared_from_this<ITCPConnection> {
public:
    virtual ~ITCPConnection() = default;
    
    virtual void send(ByteSpan data) = 0;
    virtual void registerDataHandler(std::function<void(ByteSpan)> handler) = 0;
    virtual void registerCloseHandler(std::function<void()> handler) = 0;
    virtual void close() = 0;
    virtual Endpoint getRemoteEndpoint() const = 0;
    virtual bool isConnected() const = 0;
};

/**
 * @brief InfoHash类
 * 表示BitTorrent的infohash
 */
class InfoHash {
public:
    static constexpr size_t HASH_SIZE = 20; // SHA-1 哈希长度为20字节
    
    InfoHash() = default;
    explicit InfoHash(const std::array<std::byte, HASH_SIZE>& bytes);
    explicit InfoHash(const std::string& hexString);
    
    // 获取底层字节数组
    const std::array<std::byte, HASH_SIZE>& getBytes() const;
    
    // 获取十六进制字符串表示
    std::string toHexString() const;
    
    // 比较操作符
    bool operator==(const InfoHash& other) const;
    bool operator<(const InfoHash& other) const;
    
private:
    std::array<std::byte, HASH_SIZE> bytes_{};
};

/**
 * @brief 对等体信息
 */
struct PeerInfo {
    std::string ip;
    uint16_t port;
    std::string peerId;
    bool isSeeder;
    
    // 用于比较操作
    bool operator==(const PeerInfo& other) const;
    bool operator<(const PeerInfo& other) const;
};

/**
 * @brief 下载状态枚举
 */
enum class DownloadStatus {
    CONNECTING,    // 正在连接到网络
    METADATA,      // 获取元数据中
    DOWNLOADING,   // 正在下载
    SEEDING,       // 做种中
    PAUSED,        // 已暂停
    COMPLETED,     // 已完成
    ERROR          // 发生错误
};

/**
 * @brief 下载进度信息
 */
struct DownloadProgress {
    std::string filename;           // 文件名
    FileSize totalSize;             // 总大小（字节）
    FileSize downloaded;            // 已下载（字节）
    float progress;                 // 进度（0.0-1.0）
    float downloadSpeed;            // 下载速度（字节/秒）
    float uploadSpeed;              // 上传速度（字节/秒）
    int seeds;                      // 种子数
    int peers;                      // 连接节点数
    DownloadStatus status;          // 当前状态
    std::string errorMessage;       // 错误消息（如有）
};

/**
 * @brief 元数据接口
 * 表示.torrent文件的内容
 */
class IMetadata {
public:
    virtual ~IMetadata() = default;
    
    // 基本信息
    virtual InfoHash getInfoHash() const = 0;
    virtual std::string getName() const = 0;
    virtual std::optional<std::string> getComment() const = 0;
    virtual FileSize getTotalSize() const = 0;
    
    // 块信息
    virtual uint32_t getPieceSize() const = 0;
    virtual uint32_t getPieceCount() const = 0;
    virtual ByteSpan getPieceHash(uint32_t index) const = 0;
    
    // 文件信息
    virtual bool isSingleFile() const = 0;
    virtual uint32_t getFileCount() const = 0;
    virtual struct FileInfo getFileInfo(uint32_t index) const = 0;
    
    // 序列化/反序列化
    virtual ByteBuffer serialize() const = 0;
    virtual bool deserialize(ByteSpan data) = 0;
};

/**
 * @brief 文件信息结构
 */
struct FileInfo {
    std::string path;
    FileSize size;
    std::optional<std::string> md5sum;
};

/**
 * @brief DHT管理接口
 * 负责分布式哈希表操作
 */
class IDHTManager {
public:
    virtual ~IDHTManager() = default;
    
    virtual void start(uint16_t port) = 0;
    virtual void stop() = 0;
    virtual void announce(const InfoHash& infoHash, uint16_t port) = 0;
    virtual void findPeers(const InfoHash& infoHash) = 0;
    virtual void registerPeerFoundCallback(std::function<void(const InfoHash&, const PeerInfo&)> callback) = 0;
};

/**
 * @brief Tracker管理接口
 * 负责与Tracker服务器通信
 */
class ITrackerManager {
public:
    virtual ~ITrackerManager() = default;
    
    virtual void addTracker(std::string_view url) = 0;
    virtual void announce(const InfoHash& infoHash, uint16_t port, 
                        FileSize uploaded, FileSize downloaded, FileSize left,
                        bool seeder) = 0;
    virtual void stop(const InfoHash& infoHash) = 0;
    virtual void registerPeerFoundCallback(std::function<void(const InfoHash&, const std::vector<PeerInfo>&)> callback) = 0;
};

/**
 * @brief 对等体连接管理接口
 * 负责管理连接到的对等体
 */
class IPeerManager {
public:
    virtual ~IPeerManager() = default;
    
    virtual void addPeer(const PeerInfo& peer, const InfoHash& infoHash) = 0;
    virtual void disconnectPeer(const PeerInfo& peer) = 0;
    virtual void disconnectAll() = 0;
    virtual std::vector<PeerInfo> getConnectedPeers(const InfoHash& infoHash) const = 0;
    virtual void registerPieceAvailableCallback(std::function<void(const PeerInfo&, uint32_t pieceIndex)> callback) = 0;
};

/**
 * @brief 块管理接口
 * 负责管理下载块的调度和存储
 */
class IPieceManager {
public:
    virtual ~IPieceManager() = default;
    
    struct BlockRequest {
        uint32_t pieceIndex;
        uint32_t offset;
        uint32_t length;
    };
    
    struct BlockInfo {
        uint32_t pieceIndex;
        uint32_t offset;
        ByteBuffer data;
    };
    
    virtual void initialize(const IMetadata& metadata, const std::filesystem::path& savePath) = 0;
    virtual std::optional<BlockRequest> getNextRequest(const PeerInfo& peer) = 0;
    virtual bool addBlock(const BlockInfo& block, const PeerInfo& peer) = 0;
    virtual bool hasPiece(uint32_t pieceIndex) const = 0;
    virtual std::vector<bool> getBitfield() const = 0;
    virtual FileSize getDownloaded() const = 0;
    virtual FileSize getTotalSize() const = 0;
    virtual bool isComplete() const = 0;
    virtual float getProgress() const = 0;
};

/**
 * @brief 元数据交换接口
 * 负责BEP 9协议实现
 */
class IMetadataExchange {
public:
    virtual ~IMetadataExchange() = default;
    
    virtual void initialize(const InfoHash& infoHash) = 0;
    virtual void addPeer(std::shared_ptr<ITCPConnection> connection) = 0;
    virtual bool isComplete() const = 0;
    virtual std::shared_ptr<IMetadata> getMetadata() const = 0;
    virtual void registerMetadataCompleteCallback(std::function<void(std::shared_ptr<IMetadata>)> callback) = 0;
};

/**
 * @brief 日志接口
 * 提供统一的日志功能
 */
class ILogger {
public:
    enum class Level {
        DEBUG,
        INFO,
        WARNING,
        ERROR,
        CRITICAL
    };
    
    virtual ~ILogger() = default;
    
    virtual void setLevel(Level level) = 0;
    
    virtual void debug(std::string_view message) = 0;
    virtual void info(std::string_view message) = 0;
    virtual void warning(std::string_view message) = 0;
    virtual void error(std::string_view message) = 0;
    virtual void critical(std::string_view message) = 0;
    
    // 使用C++17的折叠表达式支持格式化日志
    template<typename... Args>
    void debugf(std::string_view format, Args&&... args);
    
    template<typename... Args>
    void infof(std::string_view format, Args&&... args);
    
    template<typename... Args>
    void warningf(std::string_view format, Args&&... args);
    
    template<typename... Args>
    void errorf(std::string_view format, Args&&... args);
    
    template<typename... Args>
    void criticalf(std::string_view format, Args&&... args);
};

/**
 * @brief 事件类型
 */
enum class EventType {
    PEER_CONNECTED,
    PEER_DISCONNECTED,
    PIECE_COMPLETED,
    DOWNLOAD_STARTED,
    DOWNLOAD_PAUSED,
    DOWNLOAD_RESUMED,
    DOWNLOAD_COMPLETED,
    DOWNLOAD_FAILED,
    METADATA_RECEIVED,
    TRACKER_RESPONDED,
    DHT_PEER_FOUND,
};

/**
 * @brief 事件数据基类
 */
struct EventData {
    virtual ~EventData() = default;
    virtual EventType getType() const = 0;
};

/**
 * @brief 事件系统接口
 * 提供观察者模式实现
 */
class IEventSystem {
public:
    using EventHandler = std::function<void(const std::shared_ptr<EventData>&)>;
    
    virtual ~IEventSystem() = default;
    
    virtual void registerHandler(EventType type, EventHandler handler) = 0;
    virtual void unregisterHandler(EventType type, size_t handlerId) = 0;
    virtual void fireEvent(const std::shared_ptr<EventData>& eventData) = 0;
};

} // namespace bt 
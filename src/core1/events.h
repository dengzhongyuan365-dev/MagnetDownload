#pragma once

#include "interfaces.h"
#include <map>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <vector>

namespace bt {

/**
 * @brief 对等体连接事件数据
 */
struct PeerConnectedEvent : public EventData {
    PeerInfo peer;
    InfoHash infoHash;
    
    PeerConnectedEvent(const PeerInfo& p, const InfoHash& ih)
        : peer(p), infoHash(ih) {}
    
    EventType getType() const override { return EventType::PEER_CONNECTED; }
};

/**
 * @brief 对等体断开连接事件数据
 */
struct PeerDisconnectedEvent : public EventData {
    PeerInfo peer;
    InfoHash infoHash;
    
    PeerDisconnectedEvent(const PeerInfo& p, const InfoHash& ih)
        : peer(p), infoHash(ih) {}
    
    EventType getType() const override { return EventType::PEER_DISCONNECTED; }
};

/**
 * @brief 块完成事件数据
 */
struct PieceCompletedEvent : public EventData {
    uint32_t pieceIndex;
    InfoHash infoHash;
    
    PieceCompletedEvent(uint32_t idx, const InfoHash& ih)
        : pieceIndex(idx), infoHash(ih) {}
    
    EventType getType() const override { return EventType::PIECE_COMPLETED; }
};

/**
 * @brief 下载开始事件数据
 */
struct DownloadStartedEvent : public EventData {
    InfoHash infoHash;
    std::string displayName;
    
    DownloadStartedEvent(const InfoHash& ih, std::string_view name)
        : infoHash(ih), displayName(name) {}
    
    EventType getType() const override { return EventType::DOWNLOAD_STARTED; }
};

/**
 * @brief 下载暂停事件数据
 */
struct DownloadPausedEvent : public EventData {
    InfoHash infoHash;
    
    explicit DownloadPausedEvent(const InfoHash& ih)
        : infoHash(ih) {}
    
    EventType getType() const override { return EventType::DOWNLOAD_PAUSED; }
};

/**
 * @brief 下载恢复事件数据
 */
struct DownloadResumedEvent : public EventData {
    InfoHash infoHash;
    
    explicit DownloadResumedEvent(const InfoHash& ih)
        : infoHash(ih) {}
    
    EventType getType() const override { return EventType::DOWNLOAD_RESUMED; }
};

/**
 * @brief 下载完成事件数据
 */
struct DownloadCompletedEvent : public EventData {
    InfoHash infoHash;
    std::string displayName;
    FileSize totalSize;
    std::filesystem::path savePath;
    
    DownloadCompletedEvent(
        const InfoHash& ih, 
        std::string_view name,
        FileSize size,
        const std::filesystem::path& path)
        : infoHash(ih), displayName(name), totalSize(size), savePath(path) {}
    
    EventType getType() const override { return EventType::DOWNLOAD_COMPLETED; }
};

/**
 * @brief 下载失败事件数据
 */
struct DownloadFailedEvent : public EventData {
    InfoHash infoHash;
    std::string displayName;
    std::string errorMessage;
    
    DownloadFailedEvent(
        const InfoHash& ih, 
        std::string_view name,
        std::string_view error)
        : infoHash(ih), displayName(name), errorMessage(error) {}
    
    EventType getType() const override { return EventType::DOWNLOAD_FAILED; }
};

/**
 * @brief 元数据接收事件数据
 */
struct MetadataReceivedEvent : public EventData {
    InfoHash infoHash;
    std::shared_ptr<IMetadata> metadata;
    
    MetadataReceivedEvent(
        const InfoHash& ih,
        const std::shared_ptr<IMetadata>& md)
        : infoHash(ih), metadata(md) {}
    
    EventType getType() const override { return EventType::METADATA_RECEIVED; }
};

/**
 * @brief Tracker响应事件数据
 */
struct TrackerRespondedEvent : public EventData {
    InfoHash infoHash;
    std::string trackerUrl;
    int seeders;
    int leechers;
    std::vector<PeerInfo> peers;
    
    TrackerRespondedEvent(
        const InfoHash& ih,
        std::string_view url,
        int s,
        int l,
        const std::vector<PeerInfo>& p)
        : infoHash(ih), trackerUrl(url), seeders(s), leechers(l), peers(p) {}
    
    EventType getType() const override { return EventType::TRACKER_RESPONDED; }
};

/**
 * @brief DHT发现对等体事件数据
 */
struct DHTPeerFoundEvent : public EventData {
    InfoHash infoHash;
    PeerInfo peer;
    
    DHTPeerFoundEvent(const InfoHash& ih, const PeerInfo& p)
        : infoHash(ih), peer(p) {}
    
    EventType getType() const override { return EventType::DHT_PEER_FOUND; }
};

/**
 * @brief 事件系统实现
 */
class EventSystem : public IEventSystem {
public:
    EventSystem() = default;
    ~EventSystem() override = default;
    
    // 禁用拷贝和赋值
    EventSystem(const EventSystem&) = delete;
    EventSystem& operator=(const EventSystem&) = delete;
    
    /**
     * 注册事件处理函数
     * 
     * @param type 事件类型
     * @param handler 处理函数
     * @return 处理函数ID，用于后续取消注册
     */
    void registerHandler(EventType type, EventHandler handler) override;
    
    /**
     * 取消注册事件处理函数
     * 
     * @param type 事件类型
     * @param handlerId 处理函数ID
     */
    void unregisterHandler(EventType type, size_t handlerId) override;
    
    /**
     * 触发事件
     * 
     * @param eventData 事件数据
     */
    void fireEvent(const std::shared_ptr<EventData>& eventData) override;
    
private:
    // 处理函数ID生成器
    size_t nextHandlerId_ = 0;
    
    // 事件处理函数映射
    std::unordered_map<EventType, std::map<size_t, EventHandler>> handlers_;
    
    // 互斥锁保护并发访问
    std::mutex mutex_;
};

} // namespace bt 
#include "magnet/application/download_controller.h"
#include "magnet/utils/logger.h"
#include "magnet/utils/sha1.h"
#include "magnet/storage/file_manager.h"

#include <random>
#include <filesystem>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cstring>

namespace magnet::application {

// 日志宏
#define LOG_DEBUG(msg) magnet::utils::Logger::instance().debug(msg)
#define LOG_INFO(msg) magnet::utils::Logger::instance().info(msg)
#define LOG_WARNING(msg) magnet::utils::Logger::instance().warn(msg)
#define LOG_ERROR(msg) magnet::utils::Logger::instance().error(msg)

// ============================================================================
// 构造和析构
// ============================================================================

DownloadController::DownloadController(asio::io_context& io_context)
    : io_context_(io_context)
    , progress_timer_(io_context)
    , peer_search_timer_(io_context)
    , metadata_timeout_timer_(io_context)
    , download_stall_timer_(io_context)
{
    my_peer_id_ = generatePeerId();
    LOG_DEBUG("DownloadController created, peer_id=" + my_peer_id_);
}

DownloadController::~DownloadController() {
    stop();
    LOG_DEBUG("DownloadController destroyed");
}

// ============================================================================
// 生命周期管理
// ============================================================================

bool DownloadController::start(const DownloadConfig& config) {
    // 检查状态
    DownloadState expected = DownloadState::Idle;
    if (!state_.compare_exchange_strong(expected, DownloadState::ResolvingMetadata)) {
        if (expected == DownloadState::Paused) {
            // 从暂停恢复
            resume();
            return true;
        }
        LOG_WARNING("Cannot start: invalid state " + std::string(downloadStateToString(expected)));
        return false;
    }
    
    config_ = config;
    start_time_ = std::chrono::steady_clock::now();
    
    LOG_INFO("Starting download: " + config.magnet_uri);
    
    // 解析 Magnet URI
    LOG_INFO("Parsing magnet URI: " + config.magnet_uri);
    auto parse_result = protocols::parseMagnetUri(config.magnet_uri);
    if (!parse_result.is_ok()) {
        LOG_ERROR("Parse error code: " + std::to_string(static_cast<int>(parse_result.error())));
        fail("Failed to parse magnet URI");
        return false;
    }
    LOG_INFO("Magnet URI parsed successfully");
    
    auto& magnet_info = parse_result.value();
    
    // 检查是否有有效的 info_hash
    if (!magnet_info.info_hash.has_value()) {
        fail("Invalid magnet URI: no info_hash");
        return false;
    }
    
    // 提取 info_hash
    {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        metadata_.info_hash = magnet_info.info_hash.value();
        metadata_.name = magnet_info.display_name.empty() ? "unknown" : magnet_info.display_name;
    }
    
    // 保存 Tracker URLs
    tracker_urls_ = magnet_info.trackers;
    LOG_INFO("info_hash: " + magnet_info.info_hash->toHex());
    LOG_INFO("Found " + std::to_string(tracker_urls_.size()) + " trackers in magnet link");
    
    // 通知状态变化
    setState(DownloadState::ResolvingMetadata);
    
    // 初始化 TrackerClient（优先使用 Tracker）
    if (!tracker_urls_.empty()) {
        tracker_client_ = std::make_shared<protocols::TrackerClient>(
            io_context_, magnet_info.info_hash.value(), my_peer_id_, 6881);
        
        // 立即向所有 Tracker 发送请求
        auto self = shared_from_this();
        tracker_client_->announceAll(tracker_urls_, 0, 0, 0,
            [self](const protocols::TrackerResponse& response) {
                if (response.success && !response.peers.empty()) {
                    LOG_INFO("Got " + std::to_string(response.peers.size()) + " peers from tracker");
                    
                    // 转换为 PeerInfo
                    std::vector<protocols::PeerInfo> peers;
                    for (const auto& ep : response.peers) {
                        protocols::PeerInfo peer;
                        peer.ip = ep.ip;
                        peer.port = ep.port;
                        peers.push_back(peer);
                    }
                    self->onPeersFound(peers);
                } else if (!response.failure_reason.empty()) {
                    LOG_WARNING("Tracker failed: " + response.failure_reason);
                }
            });
    }
    
    // 初始化 DHT（作为备用）
    initializeDht();
    
    // 启动元数据超时定时器
    metadata_timeout_timer_.expires_after(config_.metadata_timeout);
    auto self = shared_from_this();
    metadata_timeout_timer_.async_wait([self](const asio::error_code& ec) {
        if (!ec && self->state_.load() == DownloadState::ResolvingMetadata) {
            self->fail("Metadata timeout");
        }
    });
    
    // 启动 Peer 搜索定时器
    startPeerSearchTimer();
    
    return true;
}

void DownloadController::pause() {
    DownloadState current = state_.load();
    if (current != DownloadState::Downloading) {
        return;
    }
    
    LOG_INFO("Pausing download");
    setState(DownloadState::Paused);
    
    // 取消定时器
    progress_timer_.cancel();
}

void DownloadController::resume() {
    DownloadState current = state_.load();
    if (current != DownloadState::Paused) {
        return;
    }
    
    LOG_INFO("Resuming download");
    setState(DownloadState::Downloading);
    
    // 重新启动定时器
    startProgressTimer();
    
    // 继续请求数据
    requestMoreBlocks();
}

void DownloadController::stop() {
    DownloadState current = state_.load();
    if (current == DownloadState::Idle || 
        current == DownloadState::Stopped ||
        current == DownloadState::Completed ||
        current == DownloadState::Failed) {
        return;
    }
    
    LOG_INFO("Stopping download");
    
    // 取消所有定时器
    progress_timer_.cancel();
    peer_search_timer_.cancel();
    metadata_timeout_timer_.cancel();
    download_stall_timer_.cancel();
    
    // 停止组件
    if (peer_manager_) {
        peer_manager_->stop();
    }
    if (dht_client_) {
        dht_client_->stop();
    }
    if (tracker_client_) {
        tracker_client_->cancel();
    }
    
    setState(DownloadState::Stopped);
}

// ============================================================================
// 状态查询
// ============================================================================

DownloadProgress DownloadController::progress() const {
    std::lock_guard<std::mutex> lock(progress_mutex_);
    return current_progress_;
}

TorrentMetadata DownloadController::metadata() const {
    std::lock_guard<std::mutex> lock(metadata_mutex_);
    return metadata_;
}

// ============================================================================
// 回调设置
// ============================================================================

void DownloadController::setStateCallback(DownloadStateCallback callback) {
    state_callback_ = std::move(callback);
}

void DownloadController::setProgressCallback(DownloadProgressCallback callback) {
    progress_callback_ = std::move(callback);
}

void DownloadController::setCompletedCallback(DownloadCompletedCallback callback) {
    completed_callback_ = std::move(callback);
}

void DownloadController::setMetadataCallback(MetadataReceivedCallback callback) {
    metadata_callback_ = std::move(callback);
}

// ============================================================================
// 元数据设置
// ============================================================================

void DownloadController::setMetadata(const TorrentMetadata& metadata) {
    {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        metadata_ = metadata;
        has_metadata_ = true;
    }
    
    LOG_INFO("Metadata set: " + metadata.name + 
             ", size=" + std::to_string(metadata.total_size) +
             ", pieces=" + std::to_string(metadata.piece_count));
    
    // 取消元数据超时
    metadata_timeout_timer_.cancel();
    
    // 通知回调
    if (metadata_callback_) {
        metadata_callback_(metadata);
    }
    
    // 初始化文件存储
    initializeFileStorage();
    
    // 初始化分片状态
    initializePieces();
    
    // 转换到下载状态
    setState(DownloadState::Downloading);
    
    // 启动进度更新定时器
    startProgressTimer();
    
    // 启动下载停滞检测定时器
    startDownloadStallTimer();
    
    // 开始请求数据
    LOG_INFO("Starting to request blocks after metadata set");
    requestMoreBlocks();
}

// ============================================================================
// 元数据获取
// ============================================================================

void DownloadController::initializeMetadataFetcher() {
    if (metadata_fetcher_) {
        return;  // 已经初始化
    }
    
    protocols::InfoHash info_hash;
    {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        info_hash = metadata_.info_hash;
    }
    
    protocols::MetadataFetcherConfig config;
    config.fetch_timeout = config_.metadata_timeout;
    
    metadata_fetcher_ = std::make_shared<protocols::MetadataFetcher>(
        io_context_, info_hash, config);
    
    auto self = shared_from_this();
    metadata_fetcher_->start([self](const protocols::TorrentMetadata* metadata, 
                                     protocols::MetadataError error) {
        self->onMetadataFetched(metadata, error);
    });
    
    LOG_INFO("MetadataFetcher initialized");
}

void DownloadController::onNewPeerConnected(std::shared_ptr<protocols::PeerConnection> peer) {
    if (!peer) {
        LOG_DEBUG("onNewPeerConnected: peer is null");
        return;
    }
    
    LOG_INFO("New peer connected: " + peer->peerInfo().toString());
    
    auto current_state = state_.load();
    LOG_DEBUG("onNewPeerConnected: state=" + std::string(downloadStateToString(current_state)) +
              ", metadata_fetcher=" + (metadata_fetcher_ ? "yes" : "no"));
    
    // 如果正在获取元数据，将新连接的 Peer 添加到 MetadataFetcher
    if (current_state == DownloadState::ResolvingMetadata && metadata_fetcher_) {
        LOG_INFO("Adding peer to MetadataFetcher");
        metadata_fetcher_->addPeer(peer);
        
        // 设置回调以处理扩展握手和元数据消息
        auto self = shared_from_this();
        auto* raw_peer = peer.get();
        
        peer->setExtensionHandshakeCallback(
            [self, raw_peer](const protocols::ExtensionHandshake& handshake) {
                if (self->metadata_fetcher_) {
                    self->metadata_fetcher_->onExtensionHandshake(raw_peer, handshake);
                }
            });
        
        peer->setMetadataMessageCallback(
            [self, raw_peer](const protocols::MetadataMessage& message) {
                if (self->metadata_fetcher_) {
                    self->metadata_fetcher_->onMetadataMessage(raw_peer, message);
                }
            });
    }
}

void DownloadController::onMetadataFetched(const protocols::TorrentMetadata* metadata,
                                            protocols::MetadataError error) {
    if (error != protocols::MetadataError::Success || !metadata) {
        LOG_ERROR("Failed to fetch metadata, error: " + std::to_string(static_cast<int>(error)));
        fail("Failed to fetch torrent metadata");
        return;
    }
    
    LOG_INFO("Metadata fetched successfully: " + metadata->name);
    
    // 转换并设置元数据
    TorrentMetadata internal_metadata;
    internal_metadata.name = metadata->name;
    internal_metadata.info_hash = metadata->info_hash;
    internal_metadata.piece_length = metadata->piece_length;
    internal_metadata.total_size = metadata->totalSize();
    internal_metadata.piece_count = metadata->pieceCount();
    
    // 复制 piece hashes
    internal_metadata.piece_hashes.resize(metadata->piece_hashes.size());
    for (size_t i = 0; i < metadata->piece_hashes.size(); ++i) {
        std::memcpy(internal_metadata.piece_hashes[i].data(), 
                    metadata->piece_hashes[i].data(), 20);
    }
    
    // 复制文件信息
    if (metadata->length.has_value()) {
        TorrentMetadata::FileInfo fi;
        fi.path = metadata->name;
        fi.size = metadata->length.value();
        internal_metadata.files.push_back(fi);
    } else {
        for (const auto& f : metadata->files) {
            TorrentMetadata::FileInfo fi;
            fi.path = f.path;
            fi.size = f.length;
            internal_metadata.files.push_back(fi);
        }
    }
    
    // 停止 MetadataFetcher
    metadata_fetcher_->stop();
    metadata_fetcher_.reset();
    
    // 设置元数据（会触发状态转换到 Downloading）
    setMetadata(internal_metadata);
}

// ============================================================================
// 内部方法
// ============================================================================

void DownloadController::initializeDht() {
    protocols::DhtClientConfig dht_config;
    dht_config.listen_port = 6881;  // 固定端口，方便防火墙配置
    
    // 添加引导节点 - 使用多个公共 DHT 节点增加成功率
    dht_config.bootstrap_nodes = {
        {"router.bittorrent.com", 6881},
        {"router.utorrent.com", 6881},
        {"dht.transmissionbt.com", 6881},
        {"dht.libtorrent.org", 25401},
        {"dht.aelitis.com", 6881},
        {"router.bitcomet.com", 6881},
        {"dht.vuze.com", 6881}
    };
    
    // 大幅增加超时时间（国内网络环境需要更长时间）
    dht_config.query_config.default_timeout = std::chrono::milliseconds(10000);
    dht_config.query_config.default_max_retries = 6;
    
    dht_client_ = std::make_shared<protocols::DhtClient>(io_context_, dht_config);
    dht_client_->start();
    
    LOG_INFO("DHT client started on port " + std::to_string(dht_client_->localPort()));
    
    // Bootstrap - 连接引导节点获取初始路由表
    LOG_INFO("Bootstrapping DHT...");
    auto self = shared_from_this();
    dht_client_->bootstrap([self](bool success, size_t node_count) {
        if (success) {
            LOG_INFO("DHT bootstrap successful, " + std::to_string(node_count) + " nodes");
            // 引导成功后开始查找 Peer
            self->findPeers();
        } else {
            LOG_WARNING("DHT bootstrap failed, will retry...");
            // 延迟重试
            self->peer_search_timer_.expires_after(std::chrono::seconds(5));
            self->peer_search_timer_.async_wait([self](const asio::error_code& ec) {
                if (!ec && self->state_.load() != DownloadState::Stopped) {
                    self->dht_client_->bootstrap([self](bool success, size_t) {
                        if (success) {
                            self->findPeers();
                        }
                    });
                }
            });
        }
    });
}

void DownloadController::findPeers() {
    protocols::InfoHash info_hash;
    {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        info_hash = metadata_.info_hash;
    }
    
    LOG_DEBUG("Searching for peers for " + info_hash.toHex());
    
    auto self = shared_from_this();
    
    // 1. 使用 Tracker 获取 peers（优先）
    if (tracker_client_ && !tracker_urls_.empty()) {
        uint64_t downloaded = 0, left = 0;
        {
            std::lock_guard<std::mutex> lock(progress_mutex_);
            downloaded = current_progress_.downloaded_size;
            left = current_progress_.total_size > downloaded ? 
                   current_progress_.total_size - downloaded : 0;
        }
        
        tracker_client_->announceAll(tracker_urls_, downloaded, 0, left,
            [self](const protocols::TrackerResponse& response) {
                if (response.success && !response.peers.empty()) {
                    LOG_INFO("Tracker returned " + std::to_string(response.peers.size()) + " peers");
                    
                    std::vector<protocols::PeerInfo> peers;
                    for (const auto& ep : response.peers) {
                        protocols::PeerInfo peer;
                        peer.ip = ep.ip;
                        peer.port = ep.port;
                        peers.push_back(peer);
                    }
                    self->onPeersFound(peers);
                }
            });
    }
    
    // 2. 同时使用 DHT 获取 peers（备用）
    if (dht_client_) {
        dht_client_->findPeers(info_hash, 
            [self](const protocols::PeerInfo& peer) {
                std::vector<protocols::PeerInfo> peers = {peer};
                self->onPeersFound(peers);
            },
            [self](bool success, const std::vector<protocols::PeerInfo>& all_peers) {
                if (success && !all_peers.empty()) {
                    LOG_INFO("DHT lookup complete, found " + std::to_string(all_peers.size()) + " peers");
                }
            });
    }
}

void DownloadController::onPeersFound(const std::vector<protocols::PeerInfo>& peers) {
    if (peers.empty()) {
        LOG_DEBUG("No peers found");
        return;
    }
    
    LOG_INFO("Found " + std::to_string(peers.size()) + " peers");
    
    // 初始化 PeerManager（如果还没有）
    if (!peer_manager_) {
        protocols::InfoHash info_hash;
        {
            std::lock_guard<std::mutex> lock(metadata_mutex_);
            info_hash = metadata_.info_hash;
        }
        
        protocols::PeerManagerConfig pm_config;
        pm_config.max_connections = config_.max_connections;
        
        peer_manager_ = std::make_shared<protocols::PeerManager>(
            io_context_, info_hash, my_peer_id_, pm_config);
        
        // 设置回调
        auto self = shared_from_this();
        
        peer_manager_->setPieceCallback(
            [self](uint32_t piece, uint32_t begin, const std::vector<uint8_t>& data) {
                self->onPieceReceived(piece, begin, data);
            });
        
        peer_manager_->setPeerStatusCallback(
            [self](const network::TcpEndpoint& ep, bool connected) {
                self->onPeerStatusChanged(ep, connected);
            });
        
        peer_manager_->setNeedMorePeersCallback([self]() {
            self->findPeers();
        });
        
        // 设置新 Peer 连接回调（用于元数据获取）
        peer_manager_->setNewPeerCallback(
            [self](std::shared_ptr<protocols::PeerConnection> peer) {
                self->onNewPeerConnected(peer);
            });
        
        peer_manager_->start();
    }
    
    // 如果还没有元数据，初始化 MetadataFetcher
    if (state_.load() == DownloadState::ResolvingMetadata && !metadata_fetcher_) {
        initializeMetadataFetcher();
    }
    
    // 添加 Peer
    std::vector<network::TcpEndpoint> endpoints;
    for (const auto& peer : peers) {
        endpoints.push_back({peer.ip, peer.port});
    }
    peer_manager_->addPeers(endpoints);
    
    // 更新进度中的 Peer 数量
    {
        std::lock_guard<std::mutex> lock(progress_mutex_);
        current_progress_.total_peers += peers.size();
    }
}

void DownloadController::onPieceReceived(uint32_t piece_index, uint32_t begin, 
                                          const std::vector<uint8_t>& data) {
    if (state_.load() != DownloadState::Downloading) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(pieces_mutex_);
    
    if (piece_index >= pieces_.size()) {
        return;
    }
    
    auto& piece = pieces_[piece_index];
    
    // 计算块索引
    size_t block_index = begin / kBlockSize;
    if (block_index >= piece.blocks.size()) {
        return;
    }
    
    // 检查是否已经收到
    if (piece.blocks[block_index]) {
        return;
    }
    
    // 保存数据
    if (piece.data.size() < begin + data.size()) {
        piece.data.resize(begin + data.size());
    }
    std::copy(data.begin(), data.end(), piece.data.begin() + begin);
    
    piece.blocks[block_index] = true;
    piece.downloaded += data.size();
    
    LOG_DEBUG("Received block: piece=" + std::to_string(piece_index) + 
              " begin=" + std::to_string(begin) + 
              " size=" + std::to_string(data.size()));
    
    // 检查分片是否完整
    if (piece.isComplete()) {
        piece.state = PieceState::Downloaded;
        
        // 异步验证
        auto self = shared_from_this();
        asio::post(io_context_, [self, piece_index]() {
            if (self->verifyPiece(piece_index)) {
                self->checkCompletion();
            }
            self->requestMoreBlocks();
        });
    } else {
        // 每收到一个 block 就尝试请求更多，保持管道满载
        auto self = shared_from_this();
        asio::post(io_context_, [self]() {
            self->requestMoreBlocks();
        });
    }
    
    // 更新进度
    {
        std::lock_guard<std::mutex> progress_lock(progress_mutex_);
        current_progress_.downloaded_size += data.size();
    }
}

void DownloadController::onPeerStatusChanged(const network::TcpEndpoint& endpoint, 
                                              bool connected) {
    std::lock_guard<std::mutex> lock(progress_mutex_);
    
    if (connected) {
        current_progress_.connected_peers++;
        LOG_DEBUG("Peer connected: " + endpoint.toString());
    } else {
        if (current_progress_.connected_peers > 0) {
            current_progress_.connected_peers--;
        }
        LOG_DEBUG("Peer disconnected: " + endpoint.toString());
    }
}

void DownloadController::initializeFileStorage() {
    TorrentMetadata meta;
    {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        meta = metadata_;
    }
    
    // 构建存储路径
    std::string base_path = config_.save_path;
    if (base_path.empty()) {
        base_path = ".";
    }
    
    // 创建目录
    try {
        std::filesystem::create_directories(base_path);
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create directory: " + std::string(e.what()));
    }
    
    // 配置文件存储
    storage::StorageConfig storage_config;
    storage_config.base_path = base_path;
    storage_config.piece_length = meta.piece_length;
    storage_config.total_size = meta.total_size;
    
    // 添加文件信息
    size_t current_offset = 0;
    for (const auto& file : meta.files) {
        storage::FileEntry entry;
        entry.path = file.path;
        entry.size = file.size;
        entry.offset = current_offset;
        storage_config.files.push_back(entry);
        current_offset += file.size;
    }
    
    // 如果没有文件列表，使用种子名称
    if (storage_config.files.empty()) {
        storage::FileEntry entry;
        entry.path = meta.name;
        entry.size = meta.total_size;
        entry.offset = 0;  // 单文件偏移为 0
        storage_config.files.push_back(entry);
    }
    
    // 创建并初始化 FileManager
    file_manager_ = std::make_unique<storage::FileManager>(storage_config);
    if (!file_manager_->initialize()) {
        LOG_ERROR("Failed to initialize file storage");
        file_manager_.reset();
    } else {
        LOG_INFO("File storage initialized at: " + base_path);
    }
}

void DownloadController::initializePieces() {
    std::lock_guard<std::mutex> lock(pieces_mutex_);
    
    TorrentMetadata meta;
    {
        std::lock_guard<std::mutex> meta_lock(metadata_mutex_);
        meta = metadata_;
    }
    
    pieces_.clear();
    pieces_.reserve(meta.piece_count);
    
    for (size_t i = 0; i < meta.piece_count; ++i) {
        PieceInfo piece;
        piece.index = static_cast<uint32_t>(i);
        piece.size = getPieceSize(static_cast<uint32_t>(i));
        
        // 计算块数
        size_t block_count = (piece.size + kBlockSize - 1) / kBlockSize;
        piece.blocks.resize(block_count, false);
        
        pieces_.push_back(std::move(piece));
    }
    
    // 初始化位图
    bitfield_.resize(meta.piece_count, false);
    
    // 更新进度
    {
        std::lock_guard<std::mutex> progress_lock(progress_mutex_);
        current_progress_.total_size = meta.total_size;
        current_progress_.total_pieces = meta.piece_count;
    }
    
    LOG_INFO("Initialized " + std::to_string(meta.piece_count) + " pieces");
}

int32_t DownloadController::selectNextPiece() {
    // 注意：调用时已持有 pieces_mutex_
    
    // 收集所有 Missing 的分片
    std::vector<uint32_t> missing_pieces;
    for (const auto& piece : pieces_) {
        if (piece.state == PieceState::Missing) {
            missing_pieces.push_back(piece.index);
        }
    }
    
    if (missing_pieces.empty()) {
        LOG_DEBUG("selectNextPiece: no missing pieces");
        return -1;
    }
    
    LOG_DEBUG("selectNextPiece: " + std::to_string(missing_pieces.size()) + " missing pieces");
    
    // 稀有优先算法：计算每个分片的稀有度
    // 稀有度 = 拥有该分片的 Peer 数量（越少越稀有）
    
    if (peer_manager_) {
        int32_t best_piece = -1;
        size_t min_availability = SIZE_MAX;
        size_t pieces_with_peers = 0;
        
        for (uint32_t piece_idx : missing_pieces) {
            auto peers = peer_manager_->getPeersWithPiece(piece_idx);
            size_t availability = peers.size();
            
            if (availability > 0) {
                pieces_with_peers++;
                if (availability < min_availability) {
                    min_availability = availability;
                    best_piece = static_cast<int32_t>(piece_idx);
                }
            }
        }
        
        LOG_DEBUG("selectNextPiece: " + std::to_string(pieces_with_peers) + 
                  " pieces have peers available");
        
        if (best_piece >= 0) {
            LOG_DEBUG("selectNextPiece: selected piece " + std::to_string(best_piece) + 
                      " with availability " + std::to_string(min_availability));
            return best_piece;
        }
        
        LOG_DEBUG("selectNextPiece: no peers have any of the missing pieces");
    } else {
        LOG_DEBUG("selectNextPiece: peer_manager is null");
    }
    
    // 如果没有 peer 有这些分片，返回 -1（不要盲目请求）
    return -1;
}

void DownloadController::requestPiece(uint32_t piece_index) {
    // 注意：调用时已持有 pieces_mutex_
    
    if (piece_index >= pieces_.size()) {
        return;
    }
    
    auto& piece = pieces_[piece_index];
    if (piece.state != PieceState::Missing) {
        return;
    }
    
    // 统计成功发送的请求数
    size_t requests_sent = 0;
    
    // 生成块请求
    for (size_t i = 0; i < piece.blocks.size(); ++i) {
        if (piece.blocks[i]) {
            continue;  // 已下载
        }
        
        uint32_t begin = static_cast<uint32_t>(i * kBlockSize);
        uint32_t length = static_cast<uint32_t>(
            std::min(kBlockSize, piece.size - begin));
        
        protocols::BlockInfo block{piece_index, begin, length};
        
        if (peer_manager_ && peer_manager_->requestBlock(block)) {
            requests_sent++;
        }
    }
    
    // 只有当成功发送了请求时，才将 piece 标记为 Pending
    if (requests_sent > 0) {
        piece.state = PieceState::Pending;
        piece.request_time = std::chrono::steady_clock::now();  // 记录请求时间
        LOG_DEBUG("Requested piece " + std::to_string(piece_index) + 
                  " with " + std::to_string(requests_sent) + " blocks");
    } else {
        LOG_DEBUG("No peer available for piece " + std::to_string(piece_index));
    }
}

void DownloadController::requestMoreBlocks() {
    if (state_.load() != DownloadState::Downloading) {
        LOG_DEBUG("requestMoreBlocks: not in Downloading state");
        return;
    }
    
    std::lock_guard<std::mutex> lock(pieces_mutex_);
    
    // 统计正在下载的分片数和各状态数量
    size_t pending_count = 0;
    size_t missing_count = 0;
    size_t verified_count = 0;
    for (const auto& piece : pieces_) {
        if (piece.state == PieceState::Pending) {
            pending_count++;
        } else if (piece.state == PieceState::Missing) {
            missing_count++;
        } else if (piece.state == PieceState::Verified) {
            verified_count++;
        }
    }
    
    LOG_DEBUG("requestMoreBlocks: pieces=" + std::to_string(pieces_.size()) +
              ", pending=" + std::to_string(pending_count) +
              ", missing=" + std::to_string(missing_count) +
              ", verified=" + std::to_string(verified_count));
    
    // 限制并发下载的分片数（增加到 100 以提高下载速度）
    const size_t max_pending = 100;
    size_t requested = 0;
    while (pending_count < max_pending) {
        int32_t next_piece = selectNextPiece();
        if (next_piece < 0) {
            LOG_DEBUG("requestMoreBlocks: no more pieces to request (selectNextPiece returned -1)");
            break;
        }
        
        LOG_DEBUG("requestMoreBlocks: requesting piece " + std::to_string(next_piece));
        requestPiece(static_cast<uint32_t>(next_piece));
        pending_count++;
        requested++;
    }
    
    LOG_DEBUG("requestMoreBlocks: requested " + std::to_string(requested) + " pieces");
    
    // 更新进度
    {
        std::lock_guard<std::mutex> progress_lock(progress_mutex_);
        current_progress_.pending_pieces = pending_count;
    }
}

bool DownloadController::verifyPiece(uint32_t piece_index) {
    std::lock_guard<std::mutex> lock(pieces_mutex_);
    
    if (piece_index >= pieces_.size()) {
        return false;
    }
    
    auto& piece = pieces_[piece_index];
    if (piece.state != PieceState::Downloaded) {
        return false;
    }
    
    TorrentMetadata meta;
    {
        std::lock_guard<std::mutex> meta_lock(metadata_mutex_);
        meta = metadata_;
    }
    
    // 计算 SHA1
    if (piece_index < meta.piece_hashes.size()) {
        auto expected_hash = meta.piece_hashes[piece_index];
        auto actual_hash = utils::sha1(piece.data.data(), piece.data.size());
        
        if (actual_hash != expected_hash) {
            LOG_WARNING("Piece " + std::to_string(piece_index) + " verification failed");
            piece.state = PieceState::Failed;
            // 重置，准备重新下载
            piece.state = PieceState::Missing;
            piece.downloaded = 0;
            std::fill(piece.blocks.begin(), piece.blocks.end(), false);
            piece.data.clear();
            return false;
        }
    }
    
    piece.state = PieceState::Verified;
    bitfield_[piece_index] = true;
    
    // 写入文件
    if (file_manager_) {
        size_t offset = static_cast<size_t>(piece_index) * meta.piece_length;
        if (!file_manager_->write(offset, piece.data)) {
            LOG_ERROR("Failed to write piece " + std::to_string(piece_index) + " to disk");
        } else {
            LOG_DEBUG("Piece " + std::to_string(piece_index) + " written to disk");
            // 写入成功后释放内存
            piece.data.clear();
            piece.data.shrink_to_fit();
        }
    }
    
    // 广播 Have
    if (peer_manager_) {
        peer_manager_->broadcastHave(piece_index);
    }
    
    LOG_DEBUG("Piece " + std::to_string(piece_index) + " verified");
    
    // 更新进度
    {
        std::lock_guard<std::mutex> progress_lock(progress_mutex_);
        current_progress_.completed_pieces++;
    }
    
    return true;
}

void DownloadController::updateProgress() {
    auto now = std::chrono::steady_clock::now();
    
    {
        std::lock_guard<std::mutex> lock(progress_mutex_);
        
        // 计算速度
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_progress_update_).count();
        
        if (elapsed > 0) {
            size_t bytes_diff = current_progress_.downloaded_size - last_downloaded_size_;
            current_progress_.download_speed = 
                static_cast<double>(bytes_diff) * 1000.0 / elapsed;
        }
        
        last_progress_update_ = now;
        last_downloaded_size_ = current_progress_.downloaded_size;
        
        // 获取 Peer 统计
        if (peer_manager_) {
            auto pm_stats = peer_manager_->getStatistics();
            current_progress_.connected_peers = pm_stats.peers_connected;
            current_progress_.total_peers = pm_stats.total_peers_known;
        }
    }
    
    // 通知回调
    if (progress_callback_) {
        std::lock_guard<std::mutex> lock(progress_mutex_);
        progress_callback_(current_progress_);
    }
}

void DownloadController::checkCompletion() {
    bool all_verified = false;
    
    {
        std::lock_guard<std::mutex> lock(pieces_mutex_);
        all_verified = std::all_of(pieces_.begin(), pieces_.end(), 
                                    [](const PieceInfo& p) {
                                        return p.state == PieceState::Verified;
                                    });
    }
    
    if (all_verified) {
        LOG_INFO("All pieces verified, download complete!");
        setState(DownloadState::Completed);
        
        // 停止组件
        progress_timer_.cancel();
        peer_search_timer_.cancel();
        
        if (peer_manager_) {
            peer_manager_->stop();
        }
        
        // 通知完成
        if (completed_callback_) {
            completed_callback_(true, "");
        }
    }
}

void DownloadController::setState(DownloadState new_state) {
    DownloadState old_state = state_.exchange(new_state);
    
    if (old_state != new_state) {
        LOG_INFO("State changed: " + std::string(downloadStateToString(old_state)) +
                 " -> " + std::string(downloadStateToString(new_state)));
        
        if (state_callback_) {
            state_callback_(new_state);
        }
    }
}

void DownloadController::fail(const std::string& error) {
    LOG_ERROR("Download failed: " + error);
    error_message_ = error;
    
    // 停止所有活动
    progress_timer_.cancel();
    peer_search_timer_.cancel();
    metadata_timeout_timer_.cancel();
    
    if (peer_manager_) {
        peer_manager_->stop();
    }
    if (dht_client_) {
        dht_client_->stop();
    }
    
    setState(DownloadState::Failed);
    
    if (completed_callback_) {
        completed_callback_(false, error);
    }
}

void DownloadController::startProgressTimer() {
    auto self = shared_from_this();
    
    progress_timer_.expires_after(std::chrono::seconds(1));
    progress_timer_.async_wait([self](const asio::error_code& ec) {
        if (!ec && self->state_.load() == DownloadState::Downloading) {
            self->updateProgress();
            self->startProgressTimer();  // 重新启动
        }
    });
}

void DownloadController::startPeerSearchTimer() {
    auto self = shared_from_this();
    
    peer_search_timer_.expires_after(config_.peer_search_interval);
    peer_search_timer_.async_wait([self](const asio::error_code& ec) {
        if (!ec) {
            auto state = self->state_.load();
            if (state == DownloadState::ResolvingMetadata || 
                state == DownloadState::Downloading) {
                self->findPeers();
                self->startPeerSearchTimer();  // 重新启动
            }
        }
    });
}

void DownloadController::startDownloadStallTimer() {
    auto self = shared_from_this();
    
    // 初始化停滞检测的起始值
    {
        std::lock_guard<std::mutex> lock(progress_mutex_);
        last_download_progress_ = std::chrono::steady_clock::now();
        stall_check_size_ = current_progress_.downloaded_size;
    }
    
    // 每30秒检查一次下载进度
    download_stall_timer_.expires_after(std::chrono::seconds(30));
    download_stall_timer_.async_wait([self](const asio::error_code& ec) {
        if (ec) {
            return;  // 定时器被取消
        }
        
        if (self->state_.load() != DownloadState::Downloading) {
            return;
        }
        
        size_t current_size;
        size_t check_size;
        std::chrono::steady_clock::time_point last_progress;
        
        {
            std::lock_guard<std::mutex> lock(self->progress_mutex_);
            current_size = self->current_progress_.downloaded_size;
            check_size = self->stall_check_size_;
            last_progress = self->last_download_progress_;
        }
        
        // 检查是否有新的下载
        if (current_size > check_size) {
            // 有进度，更新检查点
            std::lock_guard<std::mutex> lock(self->progress_mutex_);
            self->stall_check_size_ = current_size;
            self->last_download_progress_ = std::chrono::steady_clock::now();
            LOG_DEBUG("Download progress detected: " + std::to_string(current_size) + " bytes");
        } else {
            // 没有进度，检查是否超时（60秒无进度）
            auto now = std::chrono::steady_clock::now();
            auto stall_duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_progress);
            
            if (stall_duration.count() >= 60) {
                LOG_ERROR("Download stalled for " + std::to_string(stall_duration.count()) + 
                          " seconds, stopping download");
                self->fail("Download stalled: no progress for 60 seconds");
                return;
            } else {
                LOG_WARNING("No download progress for " + std::to_string(stall_duration.count()) + 
                            " seconds, will retry...");
                // 重置超时的 pending pieces
                self->resetTimedOutPieces();
                // 尝试请求更多数据
                self->requestMoreBlocks();
            }
        }
        
        // 继续检测
        self->startDownloadStallTimer();
    });
}

void DownloadController::resetTimedOutPieces() {
    std::lock_guard<std::mutex> lock(pieces_mutex_);
    
    auto now = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::seconds(15);  // 15秒超时（缩短以更快重试）
    size_t reset_count = 0;
    
    for (auto& piece : pieces_) {
        if (piece.state == PieceState::Pending) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                now - piece.request_time);
            
            if (elapsed >= timeout) {
                // 重置为 Missing 状态，允许重新请求
                piece.state = PieceState::Missing;
                reset_count++;
                LOG_DEBUG("Reset timed out piece " + std::to_string(piece.index) + 
                          " after " + std::to_string(elapsed.count()) + " seconds");
            }
        }
    }
    
    if (reset_count > 0) {
        LOG_INFO("Reset " + std::to_string(reset_count) + " timed out pieces");
    }
}

size_t DownloadController::getPieceSize(uint32_t piece_index) const {
    std::lock_guard<std::mutex> lock(metadata_mutex_);
    
    if (piece_index >= metadata_.piece_count) {
        return 0;
    }
    
    // 最后一个分片可能较小
    if (piece_index == metadata_.piece_count - 1) {
        size_t remainder = metadata_.total_size % metadata_.piece_length;
        return remainder > 0 ? remainder : metadata_.piece_length;
    }
    
    return metadata_.piece_length;
}

std::string DownloadController::generatePeerId() {
    // 格式: -MT0001-xxxxxxxxxxxx
    // MT = MagnetDownload, 0001 = 版本, 后 12 字符随机
    
    std::string peer_id = "-MT0001-";
    
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dist(0, 35);
    
    const char* chars = "0123456789abcdefghijklmnopqrstuvwxyz";
    
    for (int i = 0; i < 12; ++i) {
        peer_id += chars[dist(gen)];
    }
    
    return peer_id;
}

} // namespace magnet::application


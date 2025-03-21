#include "magnetdownloader.h"
#include <random>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <functional>

// 使用libtorrent库
#include <libtorrent/session.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/error_code.hpp>
#include <chrono>
#include <thread>
#include <iostream>

// 辅助函数：将普通哈希字符串转换为libtorrent格式
libtorrent::sha1_hash to_libtorrent_hash(const std::string& hash) {
    // 验证哈希字符串长度
    if (hash.length() != 40) {
        throw std::invalid_argument("Invalid hash string length");
    }
    
    // 初始化SHA1哈希数组
    std::array<char, 20> bytes;
    
    // 将十六进制字符串转换为字节数组
    for (int i = 0; i < 20; ++i) {
        std::string byte_str = hash.substr(i * 2, 2);
        unsigned int byte_val;
        std::sscanf(byte_str.c_str(), "%02x", &byte_val);
        bytes[i] = static_cast<char>(byte_val);
    }
    
    // 创建libtorrent sha1_hash对象
    return libtorrent::sha1_hash(bytes.data());
}

// 生成随机peer_id
std::string generate_peer_id() {
    // BitTorrent客户端标识前缀 (BT-开头的标准格式)
    std::string prefix = "-CD0001-"; // CD = Custom Downloader, 0001 = 版本号
    
    // 生成随机数
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    
    // 拼接peer_id (总长度20字节)
    std::stringstream ss;
    ss << prefix;
    
    // 添加12个随机字节
    for (int i = 0; i < 12; ++i) {
        ss << std::setw(2) << std::setfill('0') << std::hex << dis(gen);
    }
    
    return ss.str();
}

MagnetDownloader::MagnetDownloader(
    const std::filesystem::path& save_path,
    ProgressCallback progress_callback
) : 
    save_path_(save_path),
    progress_callback_(progress_callback),
    status_(DownloadStatus::CONNECTING),
    should_stop_(false),
    is_paused_(false),
    peer_id_(generate_peer_id())
{
    // 确保保存目录存在
    std::filesystem::create_directories(save_path_);
    
    // 初始化进度信息
    current_progress_.filename = "";
    current_progress_.total_size = 0;
    current_progress_.downloaded = 0;
    current_progress_.progress = 0.0f;
    current_progress_.download_speed = 0.0f;
    current_progress_.upload_speed = 0.0f;
    current_progress_.seeds = 0;
    current_progress_.peers = 0;
    current_progress_.status = DownloadStatus::CONNECTING;
    current_progress_.error_message = "";
    
    // 初始化网络组件
    initialize();
}

MagnetDownloader::~MagnetDownloader() {
    // 停止下载
    cancel(false);
    
    // 停止状态更新线程
    should_stop_ = true;
    if (status_thread_ && status_thread_->joinable()) {
        status_thread_->join();
    }
    
    // 停止IO线程
    if (io_context_) {
        work_guard_.reset(); // 允许io_context在没有更多工作时退出
        io_context_->stop();
    }
    
    if (io_thread_ && io_thread_->joinable()) {
        io_thread_->join();
    }
}

void MagnetDownloader::initialize() {
    // 创建IO上下文
    io_context_ = std::make_unique<asio::io_context>();
    
    // 创建工作守卫，防止IO上下文在没有工作时退出
    work_guard_ = std::make_unique<asio::io_context::work>(*io_context_);
    
    // 启动IO线程
    io_thread_ = std::make_shared<std::thread>([this]() {
        try {
            io_context_->run();
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(progress_mutex_);
            current_progress_.status = DownloadStatus::ERROR;
            current_progress_.error_message = "IO错误: " + std::string(e.what());
            status_ = DownloadStatus::ERROR;
        }
    });
}

bool MagnetDownloader::start_download(const MagnetParserStateMachine& parser) {
    if (!parser.is_valid()) {
        std::lock_guard<std::mutex> lock(progress_mutex_);
        current_progress_.status = DownloadStatus::ERROR;
        current_progress_.error_message = "无效的磁力链接";
        status_ = DownloadStatus::ERROR;
        return false;
    }
    
    // 从解析器获取关键信息
    std::string info_hash = parser.get_info_hash();
    std::vector<std::string> trackers = parser.get_trackers();
    std::string display_name = parser.get_display_name();
    
    // 开始下载
    return start_download_from_hash(info_hash, trackers, display_name);
}

bool MagnetDownloader::download(const std::string& magnet_uri) {
    // 先解析磁力链接
    MagnetParserStateMachine parser;
    if (!parser.parse(magnet_uri)) {
        std::lock_guard<std::mutex> lock(progress_mutex_);
        current_progress_.status = DownloadStatus::ERROR;
        current_progress_.error_message = "无法解析磁力链接";
        status_ = DownloadStatus::ERROR;
        return false;
    }
    
    // 使用解析结果开始下载
    return start_download(parser);
}

bool MagnetDownloader::start_download_from_hash(
    const std::string& info_hash, 
    const std::vector<std::string>& trackers,
    const std::string& display_name
) {
    try {
        // 设置下载信息
        info_hash_ = info_hash;
        display_name_ = display_name;
        
        // 设置初始状态
        status_ = DownloadStatus::METADATA;
        std::lock_guard<std::mutex> lock(progress_mutex_);
        current_progress_.filename = display_name;
        current_progress_.status = DownloadStatus::METADATA;
        
        // 创建块管理器
        piece_manager_ = std::make_unique<bt::PieceManager>(info_hash, save_path_);
        
        // 创建DHT组件
        dht_ = std::make_unique<bt::DHT>(*io_context_, info_hash);
        dht_->set_peer_callback([this](const std::vector<PeerInfo>& peers) {
            handle_dht_peers(peers);
        });
        
        // 创建Tracker通信组件
        for (const std::string& tracker_url : trackers) {
            try {
                auto tracker = std::make_unique<bt::Tracker>(
                    *io_context_, tracker_url, info_hash, peer_id_
                );
                
                tracker->set_announce_callback([this](const std::vector<PeerInfo>& peers) {
                    handle_tracker_response(peers);
                });
                
                trackers_.push_back(std::move(tracker));
            } catch (const std::exception& e) {
                // 忽略单个Tracker的错误，继续尝试其他Tracker
                continue;
            }
        }
        
        // 尝试获取元数据
        asio::post(*io_context_, [this]() {
            retrieve_metadata();
        });
        
        // 连接到网络
        asio::post(*io_context_, [this]() {
            connect_to_network();
        });
        
        // 启动状态更新线程
        if (!status_thread_) {
            status_thread_ = std::make_unique<std::thread>(
                &MagnetDownloader::update_status_thread, this
            );
        }
        
        return true;
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(progress_mutex_);
        current_progress_.status = DownloadStatus::ERROR;
        current_progress_.error_message = e.what();
        status_ = DownloadStatus::ERROR;
        return false;
    }
}

void MagnetDownloader::connect_to_network() {
    // 启动DHT
    uint16_t port = 6881; // 使用标准的BitTorrent端口范围的开始端口
    dht_->start(port);
    
    // 引导DHT从已知的节点开始
    std::vector<std::pair<std::string, uint16_t>> bootstrap_nodes = {
        {"router.bittorrent.com", 6881},
        {"router.utorrent.com", 6881},
        {"dht.transmissionbt.com", 6881}
    };
    dht_->bootstrap_from_nodes(bootstrap_nodes);
    
    // 向所有Tracker发送announce请求
    for (auto& tracker : trackers_) {
        tracker->announce(port, 0, 0, -1, false);
    }
}

void MagnetDownloader::handle_tracker_response(const std::vector<PeerInfo>& peers) {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    for (const auto& peer : peers) {
        // 添加到已知Peer集合
        if (known_peers_.find(peer) == known_peers_.end()) {
            known_peers_.insert(peer);
            
            // 尝试连接新Peer
            asio::post(*io_context_, [this, peer]() {
                connect_to_peer(peer);
            });
        }
    }
}

void MagnetDownloader::handle_dht_peers(const std::vector<PeerInfo>& peers) {
    // 与Tracker响应处理相同
    handle_tracker_response(peers);
}

void MagnetDownloader::connect_to_peer(const PeerInfo& peer) {
    // 限制连接数
    if (peer_connections_.size() >= max_connections_) {
        return;
    }
    
    // 检查是否已连接
    std::string peer_key = peer.ip + ":" + std::to_string(peer.port);
    if (peer_connections_.find(peer_key) != peer_connections_.end()) {
        return;
    }
    
    // 创建新的Peer连接
    try {
        auto connection = std::make_shared<bt::PeerConnection>(
            *io_context_, peer.ip, peer.port, info_hash_, peer_id_
        );
        
        // 设置消息回调
        connection->set_message_callback([this](
            const std::shared_ptr<bt::PeerConnection>& peer, 
            const std::vector<uint8_t>& message)
        {
            handle_peer_message(peer, message);
        });
        
        // 添加到连接集合
        peer_connections_[peer_key] = connection;
        
        // 开始连接
        connection->connect();
        
        // 更新连接的Peer集合
        {
            std::lock_guard<std::mutex> lock(peers_mutex_);
            connected_peers_.insert(peer);
        }
        
    } catch (const std::exception& e) {
        // 忽略单个Peer连接错误
    }
}

void MagnetDownloader::handle_peer_message(
    const std::shared_ptr<bt::PeerConnection>& peer, 
    const std::vector<uint8_t>& message)
{
    // 实际应用中会根据消息类型进行不同处理
    // 此处为简化实现
    
    // 如果我们处于元数据获取阶段，并且获取到了元数据
    if (status_ == DownloadStatus::METADATA && metadata_) {
        // 向块管理器提供元数据
        piece_manager_->set_metadata(metadata_);
        
        // 发送感兴趣消息
        peer->send_interested();
        
        // 更新状态为下载中
        status_ = DownloadStatus::DOWNLOADING;
        {
            std::lock_guard<std::mutex> lock(progress_mutex_);
            current_progress_.status = DownloadStatus::DOWNLOADING;
        }
        
        // 开始请求块
        asio::post(*io_context_, [this]() {
            request_pieces();
        });
    }
    
    // 如果我们处于下载阶段，并且收到了块数据
    if (status_ == DownloadStatus::DOWNLOADING && message[0] == 7) { // 7 = piece消息
        // 提取块信息 (简化版)
        // 实际实现需要从消息中解析出piece_index, block_offset和block_data
        int piece_index = 0; // 应从消息中提取
        int block_offset = 0; // 应从消息中提取
        std::vector<uint8_t> block_data; // 应从消息中提取
        
        // 添加到块管理器
        bool piece_complete = piece_manager_->add_piece(piece_index, block_offset, block_data);
        
        // 如果一个完整的块下载完成，写入磁盘
        if (piece_complete) {
            write_piece_to_disk(piece_index, block_data);
        }
        
        // 继续请求更多块
        asio::post(*io_context_, [this]() {
            request_pieces();
        });
    }
}

bool MagnetDownloader::retrieve_metadata() {
    // 这里应该实现BEP 9 - 元数据交换
    // 由于实现复杂，这里使用简化版
    
    // 在实际实现中，需要:
    // 1. 寻找支持元数据交换的peer
    // 2. 使用extension协议握手
    // 3. 使用ut_metadata扩展请求元数据
    // 4. 接收和组装元数据片段
    // 5. 验证元数据的hash是否与info_hash匹配
    
    // 这里假设我们已经获取了元数据
    metadata_ = std::make_shared<bt::TorrentMetadata>();
    
    // 在实际应用中，这里会通过网络获取
    // 现在我们只模拟元数据的加载
    if (metadata_->load_from_info_hash(info_hash_)) {
        // 更新文件信息
        std::lock_guard<std::mutex> lock(progress_mutex_);
        current_progress_.filename = metadata_->get_name();
        current_progress_.total_size = metadata_->get_total_size();
        
        return true;
    }
    
    return false;
}

void MagnetDownloader::request_pieces() {
    if (status_ != DownloadStatus::DOWNLOADING || is_paused_) {
        return;
    }
    
    // 遍历所有连接的Peer
    for (auto& peer_pair : peer_connections_) {
        auto& peer = peer_pair.second;
        
        // 如果Peer没有choke我们
        if (!peer->am_choked()) {
            // 获取下一个要请求的块
            auto request = piece_manager_->get_next_request(peer);
            
            // 如果有可请求的块
            if (request) {
                // 发送请求
                peer->send_request(
                    request->piece_index, 
                    request->block_offset, 
                    request->block_length
                );
            }
        }
    }
}

void MagnetDownloader::write_piece_to_disk(int piece_index, const std::vector<uint8_t>& data) {
    // 在实际实现中，这应该由PieceManager负责
    // 这里只是示意
}

void MagnetDownloader::update_status_thread() {
    while (!should_stop_) {
        // 更新状态和进度
        update_progress();
        
        // 等待一段时间再次更新（例如每500毫秒）
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void MagnetDownloader::update_progress() {
    if (is_paused_) {
        return;
    }
    
    try {
        // 如果有块管理器和元数据
        if (piece_manager_ && metadata_) {
            // 锁定进度信息
            std::lock_guard<std::mutex> lock(progress_mutex_);
            
            // 更新进度信息
            current_progress_.downloaded = piece_manager_->get_downloaded();
            current_progress_.total_size = metadata_->get_total_size();
            
            // 计算进度
            if (current_progress_.total_size > 0) {
                current_progress_.progress = 
                    static_cast<float>(current_progress_.downloaded) / 
                    static_cast<float>(current_progress_.total_size);
            }
            
            // 更新Peer和Seed数量
            {
                std::lock_guard<std::mutex> peers_lock(peers_mutex_);
                current_progress_.peers = connected_peers_.size();
                current_progress_.seeds = 0; // 应该从Tracker或连接的Peer获取
            }
            
            // 检查是否已完成
            if (piece_manager_->is_complete()) {
                status_ = DownloadStatus::COMPLETED;
                current_progress_.status = DownloadStatus::COMPLETED;
            }
        }
        
        // 调用进度回调
        if (progress_callback_) {
            DownloadProgress progress;
            {
                std::lock_guard<std::mutex> lock(progress_mutex_);
                progress = current_progress_;
            }
            progress_callback_(progress);
        }
        
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(progress_mutex_);
        current_progress_.status = DownloadStatus::ERROR;
        current_progress_.error_message = e.what();
        status_ = DownloadStatus::ERROR;
        
        if (progress_callback_) {
            progress_callback_(current_progress_);
        }
    }
}

void MagnetDownloader::pause() {
    if (status_ == DownloadStatus::DOWNLOADING || 
        status_ == DownloadStatus::SEEDING) {
        is_paused_ = true;
        status_ = DownloadStatus::PAUSED;
        
        std::lock_guard<std::mutex> lock(progress_mutex_);
        current_progress_.status = DownloadStatus::PAUSED;
    }
}

void MagnetDownloader::resume() {
    if (status_ == DownloadStatus::PAUSED) {
        is_paused_ = false;
        
        // 恢复之前的状态
        if (piece_manager_ && piece_manager_->is_complete()) {
            status_ = DownloadStatus::SEEDING;
            std::lock_guard<std::mutex> lock(progress_mutex_);
            current_progress_.status = DownloadStatus::SEEDING;
        } else {
            status_ = DownloadStatus::DOWNLOADING;
            std::lock_guard<std::mutex> lock(progress_mutex_);
            current_progress_.status = DownloadStatus::DOWNLOADING;
        }
        
        // 继续请求块
        asio::post(*io_context_, [this]() {
            request_pieces();
        });
    }
}

void MagnetDownloader::cancel(bool delete_files) {
    // 停止所有活动
    is_paused_ = true;
    
    // 断开所有Peer连接
    for (auto& peer_pair : peer_connections_) {
        try {
            peer_pair.second->disconnect();
        } catch (...) {}
    }
    peer_connections_.clear();
    
    // 停止DHT和Tracker
    if (dht_) {
        dht_->stop();
    }
    
    trackers_.clear();
    
    // 如果需要删除文件
    if (delete_files && piece_manager_) {
        // 实际实现中应该调用piece_manager的方法删除文件
        // 这里简化处理
    }
    
    // 清理资源
    dht_.reset();
    piece_manager_.reset();
    metadata_.reset();
    
    // 清空Peer集合
    {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        known_peers_.clear();
        connected_peers_.clear();
    }
}

DownloadStatus MagnetDownloader::get_status() const {
    return status_;
}

DownloadProgress MagnetDownloader::get_progress() const {
    std::lock_guard<std::mutex> lock(progress_mutex_);
    return current_progress_;
}

void MagnetDownloader::set_download_rate_limit(int limit_kbps) {
    download_rate_limit_ = limit_kbps;
    // 在实际实现中应该应用速度限制
}

void MagnetDownloader::set_upload_rate_limit(int limit_kbps) {
    upload_rate_limit_ = limit_kbps;
    // 在实际实现中应该应用速度限制
}

// bt命名空间下的类实现应该在单独的文件中
// 这里为了示例，展示DHT类的部分实现

bt::DHT::DHT(asio::io_context& io_context, const std::string& info_hash)
    : io_context_(io_context),
      socket_(io_context, asio::ip::udp::endpoint(asio::ip::udp::v4(), 0))
{
    // 生成随机节点ID
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    
    for (int i = 0; i < 20; ++i) {
        node_id_[i] = static_cast<uint8_t>(dis(gen));
    }
    
    // 转换info_hash为二进制格式
    // 实际应用应该解析十六进制字符串
    for (int i = 0; i < 20 && i < info_hash.length() / 2; ++i) {
        std::string byte = info_hash.substr(i * 2, 2);
        target_info_hash_[i] = static_cast<uint8_t>(std::stoi(byte, nullptr, 16));
    }
}

bt::DHT::~DHT() {
    stop();
}

void bt::DHT::start(uint16_t port) {
    try {
        // 关闭现有套接字
        if (socket_.is_open()) {
            socket_.close();
        }
        
        // 重新绑定到指定端口
        socket_.open(asio::ip::udp::v4());
        socket_.bind(asio::ip::udp::endpoint(asio::ip::udp::v4(), port));
        
        // 开始接收数据
        // 在实际实现中，这里应该有接收和处理DHT消息的代码
        
    } catch (const std::exception& e) {
        // 处理错误
    }
}

void bt::DHT::stop() {
    if (socket_.is_open()) {
        try {
            socket_.close();
        } catch (...) {}
    }
}

void bt::DHT::set_peer_callback(std::function<void(const std::vector<PeerInfo>&)> callback) {
    peer_callback_ = std::move(callback);
}

void bt::DHT::bootstrap_from_nodes(const std::vector<std::pair<std::string, uint16_t>>& nodes) {
    // 向引导节点发送find_node请求
    // 实际实现应该发送和处理DHT消息
    
    // 这里简化处理，只是示意
    for (const auto& node : nodes) {
        try {
            // 解析主机名
            asio::ip::udp::resolver resolver(io_context_);
            auto endpoints = resolver.resolve(node.first, std::to_string(node.second));
            
            // 向每个解析出的端点发送请求
            for (const auto& endpoint : endpoints) {
                // 在实际实现中，这里应该构造和发送DHT消息
                // socket_.async_send_to(...);
            }
        } catch (...) {
            // 忽略单个节点的错误
            continue;
        }
    }
} 
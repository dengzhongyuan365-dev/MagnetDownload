# MagnetDownload 调试总结

**日期：** 2026年1月13日

---

## 概述

本次调试会话成功解决了 MagnetDownload 项目中的多个关键问题，使其能够正常下载磁力链接资源。

---

## 问题 1：元数据获取失败（Metadata Timeout）

### 症状
- 程序连接到 peers 后无法获取元数据
- 日志显示 BitTorrent 握手成功，但没有扩展握手相关日志
- 120 秒后报告 "Metadata timeout"

### 根因
在 `peer_connection.cpp` 的 `handleHandshake()` 函数中，扩展握手在连接回调之前发送，导致 `DownloadController` 没有机会设置元数据处理回调。

### 修复
**文件：** `src/protocols/peer_connection.cpp`

```cpp
// 修复前（错误顺序）：
if (peer_supports_extension) {
    sendExtensionHandshake();
}
if (connect_callback_) {
    connect_callback_(true);
    connect_callback_ = nullptr;
}

// 修复后（正确顺序）：
if (connect_callback_) {
    connect_callback_(true);
    connect_callback_ = nullptr;
}
if (peer_supports_extension) {
    sendExtensionHandshake();
}
```

---

## 问题 2：元数据获取死锁

### 症状
- 程序在接收元数据片段时卡住
- 无响应，无法继续

### 根因
在 `MetadataFetcher::onPieceReceived()` 中调用了 `progress()` 方法，而 `progress()` 需要获取 `mutex_`，但此时 `mutex_` 已被 `onMetadataMessage()` 持有，导致递归锁死锁。

### 修复
**文件：** `src/protocols/metadata_fetcher.cpp`

```cpp
// 修复前：
LOG_INFO("Received piece " + std::to_string(piece_index) + "/" +
         std::to_string(piece_states_.size()) +
         " (" + std::to_string(static_cast<int>(progress() * 100)) + "%)");

// 修复后（直接计算进度，避免递归锁）：
float prog = piece_states_.empty() ? 0.0f :
    static_cast<float>(pieces_received_) / static_cast<float>(piece_states_.size());
LOG_INFO("Received piece " + std::to_string(piece_index) + "/" +
         std::to_string(piece_states_.size()) +
         " (" + std::to_string(static_cast<int>(prog * 100)) + "%)");
```

---

## 问题 3：下载卡在 0.1%（无进度）

### 症状
- 元数据获取成功后，下载停在约 0.1%（160KB）
- 虽然有 peers 连接，但没有数据块被下载

### 根因
1. `requestPiece()` 函数将 piece 标记为 `Pending` 状态，但实际上可能没有成功向任何 peer 发送请求
2. `selectNextPiece()` 没有检查是否有 peers 拥有该 piece

### 修复
**文件：** `src/application/download_controller.cpp`

```cpp
// 修复 requestPiece()：只有在成功发送请求时才标记为 Pending
void DownloadController::requestPiece(uint32_t piece_index) {
    // ... 原有代码 ...
    
    bool requested_any_block = false;
    for (size_t i = 0; i < piece.blocks.size(); ++i) {
        if (piece.blocks[i]) continue;
        // ... 构建 block 请求 ...
        if (peer_manager_ && peer_manager_->requestBlock(block)) {
            requested_any_block = true;
        }
    }
    
    if (requested_any_block) {
        piece.state = PieceState::Pending;
    } else {
        piece.state = PieceState::Missing;  // 没有成功发送请求，保持 Missing 状态
    }
}

// 修复 selectNextPiece()：只选择有 peers 可用的 pieces
int32_t DownloadController::selectNextPiece() {
    // ... 原有代码 ...
    
    for (uint32_t piece_idx : missing_pieces) {
        auto peers = peer_manager_->getPeersWithPiece(piece_idx);
        if (peers.size() > 0 && peers.size() < min_availability) {
            min_availability = peers.size();
            best_piece = static_cast<int32_t>(piece_idx);
        }
    }
    
    return best_piece;  // 如果没有 peers 有任何 piece，返回 -1
}
```

---

## 问题 4：缺少下载停滞检测机制

### 症状
- 当下载没有进度时，程序会无限等待
- 用户无法知道下载是否已停滞

### 解决方案
添加了下载停滞检测机制。

**文件：** `include/magnet/application/download_controller.h`

```cpp
// 新增成员变量
asio::steady_timer download_stall_timer_;
std::chrono::steady_clock::time_point last_download_progress_check_;
uint64_t last_downloaded_size_{0};

// 新增方法声明
void startDownloadStallTimer();
void checkDownloadStall();
```

**文件：** `src/application/download_controller.cpp`

```cpp
void DownloadController::startDownloadStallTimer() {
    auto self = shared_from_this();
    download_stall_timer_.expires_after(std::chrono::seconds(30));
    download_stall_timer_.async_wait([self](const asio::error_code& ec) {
        if (!ec && self->state_.load() == DownloadState::Downloading) {
            self->checkDownloadStall();
            self->startDownloadStallTimer();
        }
    });
}

void DownloadController::checkDownloadStall() {
    std::lock_guard<std::mutex> lock(progress_mutex_);
    if (current_progress_.downloaded_size > last_downloaded_size_) {
        // 有进度，重置检测
        last_download_progress_check_ = std::chrono::steady_clock::now();
        last_downloaded_size_ = current_progress_.downloaded_size;
    } else {
        // 检查是否超时
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - last_download_progress_check_).count();
        if (elapsed >= config_.download_stall_timeout.count()) {
            fail("Download stalled: no progress for " + 
                 std::to_string(config_.download_stall_timeout.count()) + " seconds");
        }
    }
}
```

---

## 问题 5：下载速度过慢

### 症状
- 下载速度只有 15-47 KB/s
- 只有 1-3 个 peers 连接成功
- 虽然发现了数百个 peers，但连接成功率很低

### 根因
连接参数配置过于保守：
- 并发下载 piece 数太少
- 最大连接数太低
- 并发连接尝试数太少
- 连接超时时间过长

### 修复
**文件：** `src/application/download_controller.cpp`

```cpp
// 修改前
const size_t max_pending = 10;

// 修改后
const size_t max_pending = 50;
```

**文件：** `include/magnet/protocols/peer_manager.h`

| 参数 | 修改前 | 修改后 | 说明 |
|------|--------|--------|------|
| `max_connections` | 50 | 100 | 最大连接数 |
| `max_connecting` | 25 | 50 | 最大并发连接尝试数 |
| `max_requests_per_peer` | 10 | 50 | 每个 peer 最大并发请求数 |
| `connect_timeout` | 30s | 10s | 连接超时时间 |

**文件：** `include/magnet/application/download_controller.h`

```cpp
// 修改前
size_t max_connections{50};

// 修改后
size_t max_connections{100};
```

---

## 测试结果

### 优化前
```
[>                                       ]   0.1% 15.84 KB/s 256.00 KB/263.64 MB ETA: 04:43:45 Peers: 3/150
```

### 优化后
```
[=======>                                ]  18.1% 411.07 KB/s 47.62 MB/263.64 MB ETA: 00:08:58 Peers: 10/262
```

### 性能提升
| 指标 | 优化前 | 优化后 | 提升 |
|------|--------|--------|------|
| 下载速度 | ~15-47 KB/s | ~400+ KB/s | **约 10 倍** |
| 连接 Peers | 1-3 | 10+ | **约 5 倍** |
| 预计完成时间 | 4+ 小时 | ~10 分钟 | **约 25 倍** |

---

## 修改文件清单

1. `src/protocols/peer_connection.cpp` - 修复扩展握手时序
2. `src/protocols/metadata_fetcher.cpp` - 修复死锁问题
3. `src/application/download_controller.cpp` - 修复 piece 请求逻辑，添加停滞检测
4. `include/magnet/application/download_controller.h` - 添加停滞检测相关成员
5. `include/magnet/protocols/peer_manager.h` - 优化连接参数
6. `src/application/main.cpp` - 更新默认最大连接数

---

## 结论

通过本次调试，MagnetDownload 项目现在能够：

1. ✅ 正确获取元数据
2. ✅ 稳定下载数据
3. ✅ 自动检测下载停滞并报错
4. ✅ 以合理的速度下载（提升约 10 倍）

项目已具备基本的 BitTorrent 客户端功能，可以正常下载磁力链接资源。


# MagnetDownload

一个基于 C++ 和 ASIO 的轻量级 BitTorrent 磁力链接下载器。

---

## 功能特性

- ✅ 支持磁力链接（Magnet URI）下载
- ✅ DHT 网络节点发现
- ✅ BEP 9/10 元数据扩展协议
- ✅ 多 Peer 并发下载
- ✅ 实时进度显示
- ✅ 下载停滞自动检测
- ✅ 跨平台支持（Windows/Linux/macOS）

---

## 编译

### 依赖

- C++17 或更高版本
- CMake 3.15+
- ASIO（已包含在 `3rd/asio` 目录）

### 编译步骤

```bash
# 创建构建目录
mkdir build && cd build

# 生成项目文件
cmake ..

# 编译（Debug 模式）
cmake --build . --config Debug

# 编译（Release 模式）
cmake --build . --config Release
```

---

## 使用方法

### 基本用法

```bash
magnetdownload <magnet_uri> [选项]
```

### 命令行选项

| 选项 | 说明 | 默认值 |
|------|------|--------|
| `-o, --output <path>` | 文件保存路径 | 当前目录 (`.`) |
| `-c, --connections <n>` | 最大连接数 | 100 |
| `-v, --verbose` | 显示详细日志 | 关闭 |
| `-h, --help` | 显示帮助信息 | - |

### 使用示例

#### 基础下载

```bash
magnetdownload "magnet:?xt=urn:btih:dd8255ecdc7ca55fb0bbf81323d87062db1f6d1c&dn=Big+Buck+Bunny"
```

#### 指定保存路径

```bash
magnetdownload "magnet:?xt=urn:btih:..." -o D:\Downloads
```

#### 增加连接数以提高速度

```bash
magnetdownload "magnet:?xt=urn:btih:..." -c 200
```

#### 启用详细日志（调试用）

```bash
magnetdownload "magnet:?xt=urn:btih:..." -v
```

#### 完整示例

```bash
magnetdownload "magnet:?xt=urn:btih:dd8255ecdc7ca55fb0bbf81323d87062db1f6d1c&dn=Big+Buck+Bunny&tr=udp://tracker.openbittorrent.com:80" -o D:\Downloads -c 150 -v
```

---

## 下载界面

程序运行时会显示以下信息：

```
+--------------------------------------------------------------+
|              MagnetDownload - Magnet Link Downloader         |
+--------------------------------------------------------------+

[>] Magnet: magnet:?xt=urn:btih:dd8255ecdc7ca55fb0bbf81323d87...
[>] Output: D:\Downloads
[>] Max connections: 100

[*] Starting download...

[+] Metadata received!
    Name: Big Buck Bunny
    Size: 263.64 MB
    Pieces: 1055

[*] Status: Downloading
[=======>                                ]  18.1% 411.07 KB/s 47.62 MB/263.64 MB ETA: 00:08:58 Peers: 10/262
```

### 进度条说明

| 字段 | 说明 |
|------|------|
| `[====>     ]` | 可视化进度条 |
| `18.1%` | 下载完成百分比 |
| `411.07 KB/s` | 当前下载速度 |
| `47.62 MB/263.64 MB` | 已下载/总大小 |
| `ETA: 00:08:58` | 预计剩余时间 |
| `Peers: 10/262` | 已连接/已发现的 Peer 数 |

---

## 下载状态

| 状态 | 说明 |
|------|------|
| `Idle` | 空闲状态 |
| `Searching for peers...` | 正在搜索 Peer 并获取元数据 |
| `Downloading` | 正在下载 |
| `Paused` | 已暂停 |
| `Verifying` | 正在验证文件完整性 |
| `Download completed!` | 下载完成 |
| `Download failed` | 下载失败 |
| `Stopped` | 用户停止 |

---

## 停止下载

按 `Ctrl+C` 可以安全停止下载。程序会显示最终统计信息后退出。

```
[!] Interrupt received, stopping download...

[*] Statistics:
    Downloaded: 47.62 MB
    Uploaded: 0 B
    Pieces: 190/1055

[*] Goodbye!
```

---

## 磁力链接格式

磁力链接的标准格式：

```
magnet:?xt=urn:btih:<info_hash>&dn=<display_name>&tr=<tracker_url>
```

| 参数 | 说明 | 必需 |
|------|------|------|
| `xt` | 资源标识符，包含 info_hash | ✅ 是 |
| `dn` | 显示名称（文件名） | ❌ 否 |
| `tr` | Tracker 服务器地址 | ❌ 否 |

### 示例

```
magnet:?xt=urn:btih:dd8255ecdc7ca55fb0bbf81323d87062db1f6d1c&dn=Big+Buck+Bunny&tr=udp://tracker.openbittorrent.com:80&tr=udp://tracker.opentrackr.org:1337
```
### 使用的协议

- **BEP 3:** BitTorrent 协议规范
- **BEP 5:** DHT 协议（基于 Kademlia）
- **BEP 9:** 元数据扩展（ut_metadata）
- **BEP 10:** 扩展协议

### 架构

```
┌─────────────────────────────────────────────────────────────┐
│                    DownloadController                        │
├─────────────────────────────────────────────────────────────┤
│  ┌───────────────┐  ┌───────────────┐  ┌───────────────┐   │
│  │  DhtManager   │  │  PeerManager  │  │ PieceManager  │   │
│  │  (DHT 网络)    │  │  (Peer 管理)   │  │  (分片管理)    │   │
│  └───────────────┘  └───────────────┘  └───────────────┘   │
├─────────────────────────────────────────────────────────────┤
│  ┌───────────────┐  ┌───────────────┐  ┌───────────────┐   │
│  │  UdpClient    │  │PeerConnection │  │  FileStorage  │   │
│  │  (UDP 通信)    │  │  (TCP 通信)    │  │  (文件存储)    │   │
│  └───────────────┘  └───────────────┘  └───────────────┘   │
└─────────────────────────────────────────────────────────────┘
                            │
                       ASIO (异步 I/O)
```

---

## 许可证

MIT License




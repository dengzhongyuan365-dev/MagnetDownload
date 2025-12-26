# Magnet URI 协议详解

> **目标**：理解 Magnet 协议的结构、参数含义，以及为什么需要解析这些参数

---

## 1. 什么是 Magnet URI？

Magnet URI（磁力链接）是一种**无需中心服务器**的资源定位方式，通过**文件内容的哈希值**来标识和查找资源。

### 传统下载 vs Magnet 下载

```
传统 HTTP 下载：
http://example.com/movie.mp4
↓
直接从服务器下载
问题：服务器挂了就无法下载

Magnet 下载：
magnet:?xt=urn:btih:HASH...
↓
通过 P2P 网络查找拥有该文件的节点
↓
从多个节点并发下载
优势：去中心化，更快，更可靠
```

---

## 2. Magnet URI 的完整格式

### 基本结构

```
magnet:?参数1=值1&参数2=值2&参数3=值3...
```

### 真实示例

```
magnet:?xt=urn:btih:ABCDEF1234567890ABCDEF1234567890ABCDEF12&dn=example.mp4&tr=udp://tracker.example.com:80&xl=1234567890
```

拆解：
```
协议头:  magnet:?
参数1:   xt=urn:btih:ABCDEF1234567890ABCDEF1234567890ABCDEF12
参数2:   dn=example.mp4
参数3:   tr=udp://tracker.example.com:80
参数4:   xl=1234567890
```

---

## 3. Magnet URI 参数详解

### 3.1 `xt` - eXact Topic（精确主题）**[必需]**

**格式**：`xt=urn:btih:<InfoHash>`

**含义**：
- `xt` = eXact Topic（精确主题）
- `urn` = Uniform Resource Name（统一资源名称）
- `btih` = BitTorrent Info Hash（BitTorrent 信息哈希）
- `<InfoHash>` = 40 个十六进制字符（20 字节的 SHA-1 哈希）

**示例**：
```
xt=urn:btih:ABCDEF1234567890ABCDEF1234567890ABCDEF12
            ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
            这是文件内容的 SHA-1 哈希值（40个十六进制字符）
```

**作用**：
- **唯一标识**一个资源（文件或文件集合）
- 用于在 DHT 网络中查找拥有该资源的 Peer
- 用于与 Peer 握手时验证双方要下载同一个文件

**为什么必需**：
- 没有 InfoHash 就无法在 P2P 网络中查找资源
- 这是整个下载流程的**核心标识**

**InfoHash 的两种编码格式**：
```
1. 十六进制（Hex）：40 个字符
   xt=urn:btih:ABCDEF1234567890ABCDEF1234567890ABCDEF12

2. Base32 编码：32 个字符
   xt=urn:btih:MFRGG2LTMVZGS3LFNZXGK3TUEB2W64TF
```

---

### 3.2 `dn` - Display Name（显示名称）**[可选]**

**格式**：`dn=<文件名>`

**含义**：
- `dn` = Display Name（显示名称）
- 建议的文件名或目录名

**示例**：
```
dn=example.mp4
dn=Ubuntu%2022.04%20Desktop.iso    (URL 编码后)
```

**作用**：
- 在下载开始前给用户显示文件名
- 在获取完整元数据之前提供参考信息
- 用于 UI 显示和默认保存路径

**为什么可选**：
- 真实的文件名会在获取元数据后得到
- 这只是一个**提示信息**，不影响下载逻辑
- 即使没有 `dn`，也可以用 InfoHash 作为临时文件名

**注意**：
- 需要进行 URL 解码（`%20` → 空格）
- 可能包含路径分隔符（`/`）

---

### 3.3 `tr` - TRacker（Tracker 服务器）**[可选，可多个]**

**格式**：`tr=<Tracker URL>`

**含义**：
- `tr` = TRacker（追踪器）
- BitTorrent Tracker 服务器的 URL

**示例**：
```
tr=udp://tracker.example.com:80
tr=http://tracker.example.com:8080/announce
tr=wss://tracker.example.com:443
```

**作用**：
- 提供**备选的 Peer 发现方式**
- 除了 DHT 网络，还可以通过 Tracker 查找 Peer
- 增加 Peer 发现的成功率

**为什么可选**：
- 现代 BitTorrent 主要依赖 **DHT 网络**（去中心化）
- Tracker 是**辅助手段**，不是必需的
- 即使没有 Tracker，也可以通过 DHT 找到 Peer

**可以有多个 `tr` 参数**：
```
magnet:?xt=...&tr=udp://tracker1.com:80&tr=http://tracker2.com:8080&tr=wss://tracker3.com:443
```

**Tracker 协议类型**：
- `udp://` - UDP Tracker（最常见）
- `http://` - HTTP Tracker
- `https://` - HTTPS Tracker
- `wss://` - WebSocket Tracker

---

### 3.4 `xl` - eXact Length（精确长度）**[可选]**

**格式**：`xl=<字节数>`

**含义**：
- `xl` = eXact Length（精确长度）
- 文件的总大小（字节）

**示例**：
```
xl=1234567890    (约 1.15 GB)
xl=104857600     (100 MB)
```

**作用**：
- 提前知道文件大小
- 可以预分配磁盘空间（避免下载到一半磁盘满）
- 可以显示下载进度百分比
- 可以验证下载完整性

**为什么可选**：
- 真实的文件大小会在获取元数据后得到
- 这只是一个**提示信息**
- 即使没有 `xl`，也可以正常下载

---

### 3.5 `as` - Acceptable Source（可接受的源）**[可选]**

**格式**：`as=<URL>`

**含义**：
- `as` = Acceptable Source（可接受的源）
- Web seed URL（HTTP/HTTPS 下载源）

**示例**：
```
as=http://mirror.example.com/file.iso
as=https://cdn.example.com/downloads/file.zip
```

**作用**：
- 提供 **HTTP/HTTPS 直接下载**作为备选
- 如果 P2P 网络速度慢，可以从 Web seed 下载
- 混合 P2P 和 HTTP 下载，提高速度

**为什么可选**：
- 不是所有资源都有 Web seed
- 主要用于官方发布的资源（如 Linux 发行版）

---

### 3.6 `xs` - eXact Source（精确源）**[可选]**

**格式**：`xs=<URL>`

**含义**：
- `xs` = eXact Source（精确源）
- 指向 .torrent 文件的 URL

**示例**：
```
xs=http://example.com/file.torrent
xs=https://example.com/torrents/abc123.torrent
```

**作用**：
- 提供 .torrent 文件的下载地址
- 可以直接下载 .torrent 文件获取完整元数据
- 避免通过 DHT 或 Peer 获取元数据的延迟

**为什么可选**：
- Magnet 的核心优势就是**不需要 .torrent 文件**
- 这个参数主要用于兼容性

---

### 3.7 `kt` - KeyworD Topic（关键词主题）**[可选]**

**格式**：`kt=<关键词1>+<关键词2>+...`

**含义**：
- `kt` = KeyworD Topic（关键词主题）
- 用于搜索的关键词

**示例**：
```
kt=ubuntu+linux+iso
kt=movie+action+2024
```

**作用**：
- 用于搜索引擎索引
- 帮助用户通过关键词找到资源
- 不影响下载逻辑

**为什么可选**：
- 纯粹用于搜索和分类
- 下载器不需要处理这个参数

---

## 4. 参数优先级和必需性

### 必需参数

| 参数 | 名称 | 必需性 | 原因 |
|------|------|--------|------|
| `xt` | InfoHash | ✅ **必需** | 没有它无法查找和下载资源 |

### 可选参数（按重要性排序）

| 参数 | 名称 | 重要性 | 用途 |
|------|------|--------|------|
| `dn` | 显示名称 | 🟡 中等 | UI 显示，用户体验 |
| `tr` | Tracker | 🟡 中等 | 备选 Peer 发现方式 |
| `xl` | 文件大小 | 🟢 低 | 提前知道大小，优化体验 |
| `as` | Web seed | 🟢 低 | 备选下载源 |
| `xs` | .torrent URL | 🟢 低 | 快速获取元数据 |
| `kt` | 关键词 | 🟢 低 | 搜索和分类 |

---

## 5. 解析后的数据结构

### 解析目标

```cpp
struct MagnetInfo {
    // 必需字段
    std::array<uint8_t, 20> info_hash;  // InfoHash（20字节）
    
    // 可选字段
    std::string display_name;            // 显示名称
    std::vector<std::string> trackers;   // Tracker 列表
    std::optional<uint64_t> length;      // 文件大小
    std::vector<std::string> web_seeds;  // Web seed 列表
    std::vector<std::string> exact_sources; // .torrent URL 列表
    std::vector<std::string> keywords;   // 关键词列表
};
```

---

## 6. 解析后如何使用这些参数

### 6.1 使用 InfoHash（核心流程）

```cpp
// 1. 在 DHT 网络中查找 Peer
dht_client.find_peers(magnet_info.info_hash, [](std::vector<PeerInfo> peers) {
    // 得到 Peer 列表
});

// 2. 与 Peer 握手时使用
HandshakeMessage handshake;
std::copy(magnet_info.info_hash.begin(), 
          magnet_info.info_hash.end(), 
          handshake.info_hash);

// 3. 验证下载的数据
bool verify_piece(const std::vector<byte>& data) {
    auto hash = sha1(data);
    return hash == expected_hash;  // expected_hash 来自元数据
}
```

### 6.2 使用 Display Name（UI 显示）

```cpp
// 在获取元数据之前显示文件名
void show_download_info(const MagnetInfo& info) {
    if (!info.display_name.empty()) {
        std::cout << "正在下载: " << info.display_name << std::endl;
    } else {
        std::cout << "正在下载: " << to_hex(info.info_hash) << std::endl;
    }
}

// 设置默认保存路径
std::filesystem::path get_save_path(const MagnetInfo& info) {
    if (!info.display_name.empty()) {
        return "./downloads/" + info.display_name;
    } else {
        return "./downloads/" + to_hex(info.info_hash);
    }
}
```

### 6.3 使用 Tracker（备选 Peer 发现）

```cpp
// 除了 DHT，还可以通过 Tracker 查找 Peer
void find_peers_from_all_sources(const MagnetInfo& info) {
    // 方式 1：DHT 网络（主要方式）
    dht_client.find_peers(info.info_hash, on_peers_found);
    
    // 方式 2：Tracker 服务器（备选方式）
    for (const auto& tracker_url : info.trackers) {
        tracker_client.announce(tracker_url, info.info_hash, on_peers_found);
    }
}
```

### 6.4 使用 File Length（优化体验）

```cpp
// 预分配磁盘空间
void prepare_download(const MagnetInfo& info, const std::string& save_path) {
    if (info.length.has_value()) {
        // 检查磁盘空间
        auto available = std::filesystem::space(save_path).available;
        if (available < info.length.value()) {
            throw std::runtime_error("磁盘空间不足");
        }
        
        // 预分配文件空间（避免碎片）
        std::ofstream file(save_path, std::ios::binary);
        file.seekp(info.length.value() - 1);
        file.write("", 1);
    }
}

// 显示下载进度
void show_progress(size_t downloaded, const MagnetInfo& info) {
    if (info.length.has_value()) {
        double percentage = (downloaded * 100.0) / info.length.value();
        std::cout << "进度: " << percentage << "%" << std::endl;
    } else {
        std::cout << "已下载: " << downloaded << " 字节" << std::endl;
    }
}
```

### 6.5 使用 Web Seeds（混合下载）

```cpp
// 从多个源下载（P2P + HTTP）
void hybrid_download(const MagnetInfo& info) {
    // P2P 下载
    for (const auto& peer : peers) {
        download_from_peer(peer, info.info_hash);
    }
    
    // HTTP 下载（如果有 Web seed）
    for (const auto& web_seed : info.web_seeds) {
        download_from_http(web_seed);
    }
}
```

---

## 7. 完整的解析和使用流程

```
┌─────────────────────────────────────────────────────────┐
│ 1. 用户输入 Magnet URI                                   │
│    magnet:?xt=urn:btih:HASH&dn=file.mp4&tr=...         │
└─────────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────────┐
│ 2. MagnetUriParser 解析                                 │
│    ✓ 提取 InfoHash (必需)                               │
│    ✓ 提取 Display Name (可选)                           │
│    ✓ 提取 Tracker 列表 (可选)                           │
│    ✓ 提取 File Length (可选)                            │
└─────────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────────┐
│ 3. 使用 InfoHash 查找 Peer                              │
│    DHT: dht_client.find_peers(info_hash)                │
│    Tracker: tracker_client.announce(tracker_url)        │
└─────────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────────┐
│ 4. 连接 Peer 并握手                                      │
│    使用 InfoHash 验证双方要下载同一个文件                 │
└─────────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────────┐
│ 5. 获取文件元数据                                        │
│    从 Peer 获取完整的文件信息（文件名、大小、分片列表）    │
└─────────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────────┐
│ 6. 下载文件分片                                          │
│    从多个 Peer 并发下载，使用 Display Name 保存文件       │
└─────────────────────────────────────────────────────────┘
```

---

## 8. 总结

### Magnet URI 的核心思想

**去中心化的资源定位**：
- 不依赖中心服务器
- 通过内容哈希（InfoHash）标识资源
- 通过 P2P 网络查找和下载

### 必须理解的关键点

1. **InfoHash 是核心**：
   - 唯一必需的参数
   - 用于查找、验证、下载

2. **其他参数都是辅助**：
   - Display Name：改善用户体验
   - Tracker：备选 Peer 发现方式
   - File Length：优化下载体验
   - Web Seeds：混合下载源

3. **解析的目的**：
   - 提取 InfoHash 用于 P2P 网络查找
   - 提取辅助信息改善用户体验
   - 为后续下载流程提供必要参数

### 实现优先级

```
第一优先级：解析 InfoHash（没有它无法下载）
第二优先级：解析 Display Name（改善 UI）
第三优先级：解析 Tracker（增加成功率）
第四优先级：解析其他参数
```

---


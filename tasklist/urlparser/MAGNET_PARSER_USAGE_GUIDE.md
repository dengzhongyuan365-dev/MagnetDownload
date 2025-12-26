# Magnet Parser 使用指南

> **目标**：说明如何使用 MagnetParser 的数据结构，以及外部模块如何集成

---

## 1. 数据结构概览

### 1.1 核心类型

```cpp
namespace magnet {
    class InfoHash;              // 20字节 SHA-1 哈希
    struct MagnetInfo;           // 解析结果
    enum class ParseError;       // 错误类型
    struct ParseErrorInfo;       // 错误详情
    template<T, E> class Result; // 结果包装
}
```

### 1.2 类型关系图

```
MagnetUriParser
    ↓ parse()
MagnetParseResult = Result<MagnetInfo, ParseErrorInfo>
    ↓ is_ok()
MagnetInfo
    ├─ InfoHash info_hash          (必需)
    ├─ string display_name         (可选)
    ├─ vector<string> trackers     (可选)
    ├─ uint64_t exact_length       (可选)
    └─ ... 其他字段
```

---

## 2. InfoHash 类型使用

### 2.1 创建 InfoHash

```cpp
#include "magnet/protocols/magnet_types.h"

using namespace magnet;

// 方式 1：从十六进制字符串（40个字符）
auto hash1 = InfoHash::from_hex("ABCDEF1234567890ABCDEF1234567890ABCDEF12");
if (hash1.has_value()) {
    // 使用 hash1.value()
}

// 方式 2：从 Base32 字符串（32个字符）
auto hash2 = InfoHash::from_base32("MFRGG2LTMVZGS3LFNZXGK3TUEB2W64TF");

// 方式 3：从字节数组
InfoHash::ByteArray bytes = { /* 20个字节 */ };
InfoHash hash3(bytes);
```

### 2.2 InfoHash 操作

```cpp
InfoHash hash = /* ... */;

// 转换为十六进制字符串
std::string hex = hash.to_hex();
std::cout << "InfoHash: " << hex << std::endl;

// 获取原始字节（用于网络传输）
const auto& bytes = hash.bytes();
send_to_network(bytes.data(), bytes.size());

// 验证是否有效
if (hash.is_valid()) {
    // InfoHash 不全为 0
}

// 比较
InfoHash hash1, hash2;
if (hash1 == hash2) {
    // 相同
}
```

### 2.3 InfoHash 作为容器的 key

```cpp
// 作为 unordered_map 的 key
std::unordered_map<InfoHash, DownloadTask, InfoHash::Hash> tasks;

tasks[info_hash] = task;
auto it = tasks.find(info_hash);

// 作为 map 的 key（自动排序）
std::map<InfoHash, DownloadTask> sorted_tasks;

// 作为 set 的元素
std::set<InfoHash> unique_hashes;
unique_hashes.insert(info_hash);
```

---

## 3. MagnetInfo 结构使用

### 3.1 访问字段

```cpp
MagnetInfo info = /* 解析结果 */;

// 必需字段（总是存在）
InfoHash hash = info.info_hash;

// 可选字段（需要检查）
if (info.display_name.has_value()) {
    std::string name = *info.display_name;
    std::cout << "文件名: " << name << std::endl;
}

// 或使用 value_or
std::string name = info.display_name.value_or("未知文件");

// 列表字段
for (const auto& tracker : info.trackers) {
    std::cout << "Tracker: " << tracker << std::endl;
}
```

### 3.2 使用辅助方法

```cpp
MagnetInfo info = /* ... */;

// 检查有效性
if (!info.is_valid()) {
    std::cerr << "无效的 MagnetInfo" << std::endl;
    return;
}

// 获取显示名称（自动处理空值）
std::string display = info.get_display_name();
// 如果有 display_name 返回它，否则返回 InfoHash 的十六进制

// 检查是否有 Tracker
if (info.has_trackers()) {
    std::cout << "有 " << info.trackers.size() << " 个 Tracker" << std::endl;
}

// 检查是否知道文件大小
if (info.has_exact_length()) {
    std::cout << "文件大小: " << *info.exact_length << " 字节" << std::endl;
    std::cout << "格式化: " << info.get_size_string() << std::endl;
}
```

### 3.3 元数据字段

```cpp
MagnetInfo info = /* ... */;

// 原始 URI（用于日志）
std::cout << "原始链接: " << info.original_uri << std::endl;

// 解析时间（用于缓存过期检查）
auto now = std::chrono::system_clock::now();
auto age = now - info.parsed_at;
if (age > std::chrono::hours(24)) {
    std::cout << "解析结果已过期，需要重新解析" << std::endl;
}
```

---

## 4. Result 类型使用

### 4.1 基本使用

```cpp
MagnetUriParser parser;
auto result = parser.parse(magnet_uri);

// 方式 1：if-else
if (result.is_ok()) {
    const MagnetInfo& info = result.value();
    // 使用 info
} else {
    const ParseErrorInfo& error = result.error();
    std::cerr << "解析失败: " << error.message << std::endl;
}

// 方式 2：value_or
MagnetInfo info = result.value_or(MagnetInfo{});

// 方式 3：提前返回
if (result.is_err()) {
    log_error(result.error().message);
    return;
}
const auto& info = result.value();
```

### 4.2 函数式操作

```cpp
auto result = parser.parse(magnet_uri);

// map：转换成功值
auto display_name_result = result.map([](const MagnetInfo& info) {
    return info.get_display_name();
});
// Result<std::string, ParseErrorInfo>

// and_then：链式操作
auto final_result = result.and_then([](const MagnetInfo& info) {
    if (info.has_exact_length()) {
        return Result<MagnetInfo, ParseErrorInfo>::ok(info);
    } else {
        return Result<MagnetInfo, ParseErrorInfo>::err(
            ParseErrorInfo{ParseError::MISSING_INFO_HASH, "缺少文件大小"}
        );
    }
});
```

---

## 5. 外部模块使用示例

### 5.1 DownloadManager 使用

```cpp
class DownloadManager {
public:
    TaskId start_download(const std::string& magnet_uri, 
                          const std::string& save_path) {
        // 1. 解析磁力链接
        MagnetUriParser parser;
        auto result = parser.parse(magnet_uri);
        
        if (result.is_err()) {
            throw std::runtime_error("解析失败: " + result.error().message);
        }
        
        const auto& info = result.value();
        
        // 2. 验证
        if (!info.is_valid()) {
            throw std::runtime_error("无效的磁力链接");
        }
        
        // 3. 创建任务
        auto task = create_task(info, save_path);
        
        // 4. 开始查找 Peer
        find_peers(task, info);
        
        return task->id;
    }
    
private:
    std::shared_ptr<DownloadTask> create_task(
        const MagnetInfo& info, 
        const std::string& save_path) {
        
        auto task = std::make_shared<DownloadTask>();
        task->id = generate_id();
        task->info_hash = info.info_hash;  // 直接赋值
        task->display_name = info.get_display_name();
        task->save_path = save_path;
        task->original_uri = info.original_uri;
        
        if (info.has_exact_length()) {
            task->total_size = *info.exact_length;
            task->downloaded_size = 0;
        }
        
        return task;
    }
    
    void find_peers(std::shared_ptr<DownloadTask> task, 
                    const MagnetInfo& info) {
        // DHT 查找
        dht_client_.find_peers(info.info_hash, 
            [this, task](std::vector<PeerInfo> peers) {
                on_peers_found(task, peers);
            });
        
        // Tracker 查找
        if (info.has_trackers()) {
            for (const auto& tracker_url : info.trackers) {
                tracker_client_.announce(tracker_url, info.info_hash,
                    [this, task](std::vector<PeerInfo> peers) {
                        on_peers_found(task, peers);
                    });
            }
        }
    }
};
```

### 5.2 DHTClient 使用

```cpp
class DHTClient {
public:
    void find_peers(const InfoHash& info_hash, PeerListCallback callback) {
        // 直接使用 InfoHash 的二进制数据
        const auto& hash_bytes = info_hash.bytes();
        
        // 构造 DHT 查询消息
        BencodeDict query;
        query["id"] = node_id_;
        query["info_hash"] = std::string(
            reinterpret_cast<const char*>(hash_bytes.data()),
            hash_bytes.size()
        );
        
        // 发送查询
        send_dht_query("get_peers", query, callback);
    }
};
```

### 5.3 PeerManager 使用

```cpp
class PeerManager {
public:
    void connect_to_peer(const PeerInfo& peer, 
                         const InfoHash& info_hash,
                         ConnectCallback callback) {
        auto socket = std::make_shared<asio::ip::tcp::socket>(io_context_);
        
        socket->async_connect(peer.endpoint, 
            [this, socket, info_hash, callback](const asio::error_code& ec) {
                if (!ec) {
                    send_handshake(socket, info_hash, callback);
                } else {
                    callback(nullptr);
                }
            });
    }
    
private:
    void send_handshake(std::shared_ptr<asio::ip::tcp::socket> socket,
                        const InfoHash& info_hash,
                        ConnectCallback callback) {
        // BitTorrent 握手消息：68 字节
        std::array<uint8_t, 68> handshake;
        
        // 协议标识：19 + "BitTorrent protocol"
        handshake[0] = 19;
        std::memcpy(&handshake[1], "BitTorrent protocol", 19);
        
        // 保留字节：8 字节全 0
        std::memset(&handshake[20], 0, 8);
        
        // InfoHash：20 字节（直接使用）
        std::memcpy(&handshake[28], info_hash.bytes().data(), 20);
        
        // Peer ID：20 字节
        std::memcpy(&handshake[48], peer_id_.data(), 20);
        
        // 发送
        asio::async_write(*socket, asio::buffer(handshake),
            [socket, callback](const asio::error_code& ec, size_t) {
                if (!ec) {
                    callback(std::make_shared<PeerConnection>(socket));
                } else {
                    callback(nullptr);
                }
            });
    }
};
```

### 5.4 UI/CLI 使用

```cpp
class CLI {
public:
    void show_download_info(const MagnetInfo& info) {
        std::cout << "========== 下载信息 ==========" << std::endl;
        std::cout << "InfoHash: " << info.info_hash.to_hex() << std::endl;
        std::cout << "文件名: " << info.get_display_name() << std::endl;
        
        if (info.has_exact_length()) {
            std::cout << "大小: " << info.get_size_string() << std::endl;
        } else {
            std::cout << "大小: 未知" << std::endl;
        }
        
        if (info.has_trackers()) {
            std::cout << "Trackers (" << info.trackers.size() << "):" << std::endl;
            for (const auto& tracker : info.trackers) {
                std::cout << "  - " << tracker << std::endl;
            }
        }
        
        if (info.has_web_seeds()) {
            std::cout << "Web Seeds (" << info.web_seeds.size() << "):" << std::endl;
            for (const auto& seed : info.web_seeds) {
                std::cout << "  - " << seed << std::endl;
            }
        }
        
        std::cout << "=============================" << std::endl;
    }
};
```

---

## 6. 错误处理模式

### 6.1 简单错误处理

```cpp
auto result = parser.parse(magnet_uri);

if (result.is_err()) {
    const auto& error = result.error();
    
    switch (error.error) {
        case ParseError::INVALID_SCHEME:
            std::cerr << "不是有效的磁力链接" << std::endl;
            break;
        case ParseError::MISSING_INFO_HASH:
            std::cerr << "缺少 InfoHash" << std::endl;
            break;
        case ParseError::INVALID_INFO_HASH:
            std::cerr << "InfoHash 格式错误" << std::endl;
            break;
        default:
            std::cerr << "解析错误: " << error.message << std::endl;
    }
    
    return;
}
```

### 6.2 详细错误处理

```cpp
auto result = parser.parse(magnet_uri);

if (result.is_err()) {
    const auto& error = result.error();
    
    // 记录详细日志
    logger_.error("磁力链接解析失败");
    logger_.error("  URI: {}", magnet_uri);
    logger_.error("  错误: {}", error.message);
    logger_.error("  位置: {}", error.position);
    logger_.error("  类型: {}", static_cast<int>(error.error));
    
    // 通知用户
    notify_user("解析失败: " + error.message);
    
    // 统计
    metrics_.increment("parse_errors", {
        {"error_type", error_type_to_string(error.error)}
    });
    
    return;
}
```

---

## 7. 完整使用流程示例

```cpp
#include "magnet/protocols/magnet_uri_parser.h"
#include "magnet/protocols/magnet_types.h"

using namespace magnet;

int main() {
    // 1. 创建解析器
    MagnetUriParser parser;
    
    // 2. 解析磁力链接
    std::string uri = "magnet:?xt=urn:btih:ABCD...&dn=file.mp4";
    auto result = parser.parse(uri);
    
    // 3. 检查结果
    if (result.is_err()) {
        std::cerr << "解析失败: " << result.error().message << std::endl;
        return 1;
    }
    
    // 4. 获取解析结果
    const auto& info = result.value();
    
    // 5. 验证
    if (!info.is_valid()) {
        std::cerr << "无效的磁力链接" << std::endl;
        return 1;
    }
    
    // 6. 使用解析结果
    std::cout << "InfoHash: " << info.info_hash.to_hex() << std::endl;
    std::cout << "文件名: " << info.get_display_name() << std::endl;
    
    // 7. 开始下载
    DownloadManager manager;
    auto task_id = manager.start_download(uri, "./downloads/");
    
    std::cout << "下载任务已创建: " << task_id << std::endl;
    
    return 0;
}
```

---

## 8. 最佳实践

### 8.1 总是检查 Result

```cpp
// ✅ 好
auto result = parser.parse(uri);
if (result.is_err()) {
    handle_error(result.error());
    return;
}
use(result.value());

// ❌ 差：不检查直接使用
auto info = parser.parse(uri).value();  // 可能崩溃
```

### 8.2 使用 const 引用

```cpp
// ✅ 好：避免拷贝
const auto& info = result.value();

// ❌ 差：不必要的拷贝
auto info = result.value();
```

### 8.3 使用辅助方法

```cpp
// ✅ 好：使用辅助方法
std::string name = info.get_display_name();

// ❌ 差：手动处理
std::string name = info.display_name.has_value() 
    ? *info.display_name 
    : info.info_hash.to_hex();
```

### 8.4 保存原始 URI

```cpp
// ✅ 好：保存原始 URI 用于日志
task->original_uri = info.original_uri;
logger_.info("开始下载: {}", task->original_uri);

// ❌ 差：丢失原始信息
task->info_hash = info.info_hash;  // 只保存 hash
```

---

## 9. 总结

### 核心要点

1. **InfoHash 是核心类型**：
   - 20 字节二进制数据
   - 支持十六进制和 Base32 转换
   - 可作为容器的 key

2. **MagnetInfo 包含所有解析结果**：
   - 必需字段：info_hash
   - 可选字段：使用 std::optional
   - 辅助方法：简化常见操作

3. **Result 类型处理错误**：
   - 避免异常
   - 提供详细错误信息
   - 支持函数式操作

4. **外部模块直接使用**：
   - 无需二次转换
   - 类型安全
   - 性能高效

### 使用流程

```
解析 → 检查错误 → 验证有效性 → 使用数据 → 传递给其他模块
```

这样的设计既满足功能需求，又保证了代码质量！

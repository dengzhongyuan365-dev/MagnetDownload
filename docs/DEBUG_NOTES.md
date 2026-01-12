# MagnetDownload 调试笔记

本文档记录项目开发过程中遇到的问题及解决方案。

---

## 1. 死锁问题 (2026-01-12)

### 错误信息
```
IO Error: resource deadlock would occur: resource deadlock would occur
```

### 问题根源
在 `src/protocols/dht_client.cpp` 的 `continueLookup` 方法中，存在锁重入问题：

```cpp
void DhtClient::continueLookup(const std::string& lookup_id) {
    {
        std::lock_guard<std::mutex> lock(lookups_mutex_);  // 获取锁
        // ...
        if (!state.shouldContinue()) {
            completeLookup(lookup_id, ...);  // ❌ 在持有锁时调用
            return;
        }
    }
}

void DhtClient::completeLookup(const std::string& lookup_id, bool success) {
    {
        std::lock_guard<std::mutex> lock(lookups_mutex_);  // ❌ 尝试再次获取同一个锁 → 死锁
        // ...
    }
}
```

同样的问题也存在于 `handleLookupResponse` 方法中，在持有锁时调用回调函数。

### 解决方案
使用标志变量，在锁外执行可能导致锁重入的操作：

```cpp
void DhtClient::continueLookup(const std::string& lookup_id) {
    bool should_complete = false;
    bool complete_success = false;
    
    {
        std::lock_guard<std::mutex> lock(lookups_mutex_);
        // ...
        if (!state.shouldContinue()) {
            should_complete = true;  // ✅ 标记需要完成
            complete_success = !state.found_peers.empty();
        }
    }
    
    // ✅ 在锁外调用
    if (should_complete) {
        completeLookup(lookup_id, complete_success);
        return;
    }
}
```

### 修改的文件
- `src/protocols/dht_client.cpp`
  - `continueLookup()` - 在锁外调用 `completeLookup()`
  - `handleLookupResponse()` - 在锁外调用回调函数和更新路由表

---

## 2. 路由表无效节点问题 (2026-01-12)

### 错误信息
```
[WARN] [UdpClient] Invalid endpoint: 87.98.162.88:0
```

### 问题根源
在 `bootstrap()` 方法的回调 lambda 中，没有捕获 `port` 变量：

```cpp
for (const auto& [host, port] : config_.bootstrap_nodes) {
    query_manager_->sendQuery(bootstrap_node, std::move(msg),
        [self, callback, success_count, remaining, host](QueryResult result) {  // ❌ 缺少 port
            // ...
            DhtNode responder;
            responder.id_ = response.senderId();
            responder.ip_ = host;
            // responder.port_ 未设置，默认为 0  ❌
            self->routing_table_.addNode(responder);
        }
    );
}
```

### 解决方案

1. **修复 lambda 捕获**：
```cpp
[self, callback, success_count, remaining, host, port](QueryResult result) {  // ✅ 添加 port
    // ...
    DhtNode responder;
    responder.id_ = response.senderId();
    responder.ip_ = host;
    responder.port_ = port;  // ✅ 设置端口
    self->routing_table_.addNode(responder);
}
```

2. **在路由表中添加验证**（防御性编程）：
```cpp
// src/protocols/routing_table.cpp
bool RoutingTable::addNode(const DhtNode& node) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (node.id_ == local_id_)
        return false;
    
    // ✅ 过滤无效端口和空 IP
    if (node.port_ == 0 || node.ip_.empty())
        return false;
    
    // ...
}
```

3. **过滤从响应中获得的无效节点**：
```cpp
for (const auto& node : nodes) {
    if (node.port_ != 0) {
        self->routing_table_.addNode(node);
    } else {
        LOG_DEBUG("Skipping node with invalid port: " + node.ip_);
    }
}
```

### 修改的文件
- `src/protocols/dht_client.cpp` - 修复 lambda 捕获和节点过滤
- `src/protocols/routing_table.cpp` - 添加节点有效性验证

---

## 3. Bootstrap 节点连接超时 (2026-01-12)

### 问题描述
所有引导节点都超时，导致 DHT 无法启动。

### 解决方案

1. **添加更多备用引导节点**：
```cpp
dht_config.bootstrap_nodes = {
    {"router.bittorrent.com", 6881},
    {"router.utorrent.com", 6881},
    {"dht.transmissionbt.com", 6881},
    {"dht.libtorrent.org", 25401},
    // 备用节点
    {"dht.aelitis.com", 6881},
    {"87.98.162.88", 6881},      // 欧洲节点 IP
    {"82.221.103.244", 6881}     // 备用节点 IP
};
```

2. **增加超时时间**：
```cpp
dht_config.query_config.default_timeout = std::chrono::milliseconds(5000);
dht_config.query_config.default_max_retries = 4;
```

### 修改的文件
- `src/application/download_controller.cpp` - 更新 DHT 配置

---

## 4. Windows 控制台编码问题 (2026-01-12)

### 问题描述
Windows 控制台输出中文时显示乱码（亂碼）。

### 解决方案
在程序启动时设置控制台代码页为 UTF-8：

```cpp
#ifdef _WIN32
#include <windows.h>
#endif

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    // ...
}
```

### 修改的文件
- `src/application/main.cpp` - 添加 Windows 控制台编码设置

---

## 调试技巧

### 1. 添加详细日志
在关键位置添加日志输出，帮助追踪执行流程：

```cpp
LOG_DEBUG("Received UDP packet from " + message.remote_endpoint.ip + 
          ":" + std::to_string(message.remote_endpoint.port) + 
          ", size=" + std::to_string(message.data.size()));

LOG_DEBUG("Response: hasPeers=" + std::string(response.hasPeers() ? "yes" : "no") +
          ", hasNodes=" + std::string(response.hasNodes() ? "yes" : "no"));
```

### 2. 使用 `-v` 参数启用详细输出
```bash
magnetdownload.exe "magnet:?xt=urn:btih:..." -v
```

### 3. 检查锁的使用
- 确保所有回调在锁外调用
- 使用 `std::lock_guard` 确保锁的正确释放
- 避免在持有锁时调用可能获取同一锁的函数

---

## 测试验证

修复后的测试结果：
```
2026-01-12 23:38:26.039 [DEBUG] Response: hasPeers=yes, hasNodes=yes
2026-01-12 23:38:26.039 [INFO ] Found 17 peers from get_peers response
2026-01-12 23:38:26.087 [INFO ] Lookup completed in 368ms, found 17 peers
2026-01-12 23:38:26.087 [INFO ] Peer lookup complete, found 17 peers
```

✅ DHT Bootstrap 成功  
✅ Peer 搜索正常  
✅ 找到多个 peers  
✅ 正在尝试连接 peers


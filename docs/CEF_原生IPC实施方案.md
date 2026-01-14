# CEF 原生 IPC 实施方案（学习浏览器技术）

## 🎯 方案选择

**使用 CefMessageRouter**（CEF 官方推荐的 IPC 方式）

- ✅ CEF 原生技术，深入学习浏览器架构
- ✅ 比 ProcessMessage 更高级，但仍是底层技术
- ✅ 完全不依赖第三方库
- ✅ 理解 Chromium 多进程通信机制

---

## 📚 前置知识：CEF 多进程架构

### 1. CEF 的进程模型

```
┌─────────────────────────────────────────────────────┐
│  主进程（Browser Process）                           │
│  - 窗口管理                                          │
│  - 网络请求                                          │
│  - 文件访问                                          │
│  - 你的 C++ 下载逻辑 ← 运行在这里                    │
└────────────────────┬────────────────────────────────┘
                     │
        ┌────────────┼────────────┐
        │            │            │
        ▼            ▼            ▼
┌──────────┐  ┌──────────┐  ┌──────────┐
│ 渲染进程1 │  │ 渲染进程2 │  │ GPU 进程 │
│ (Tab 1)  │  │ (Tab 2)  │  │          │
│          │  │          │  │          │
│ Vue 3 ←  │  │          │  │          │
│ 运行这里  │  │          │  │          │
└──────────┘  └──────────┘  └──────────┘
```

**关键理解**：
- 前端 JavaScript 运行在**渲染进程**（隔离的沙箱）
- C++ 业务逻辑运行在**主进程**
- 它们是**不同的进程**，需要 IPC 通信

### 2. 为什么需要 IPC？

```cpp
// ❌ 错误理解：前端直接调用 C++ 函数
// JavaScript: window.addTask()  → C++: download_manager->addTask()
// 这是不可能的！它们在不同进程！

// ✅ 正确理解：前端发送消息，主进程接收并处理
// JavaScript: 发送消息 "add_task"
//     ↓ (IPC)
// C++: 接收消息 → 调用 download_manager->addTask()
//     ↓ (IPC)
// JavaScript: 接收响应 "task_id: abc123"
```

---

## 🔧 CefMessageRouter 工作原理

### 1. 整体流程

```
┌────────────────────────────────────────────────┐
│  渲染进程（JavaScript）                         │
│                                                │
│  window.cefQuery({                             │
│    request: JSON.stringify({                   │
│      action: 'addTask',                        │
│      data: { magnet_uri: '...' }               │
│    }),                                         │
│    onSuccess: (response) => { ... },           │
│    onFailure: (code, msg) => { ... }           │
│  });                                           │
│                                                │
└───────────────────┬────────────────────────────┘
                    │
                    │ ① 发送请求
                    ↓
┌───────────────────────────────────────────────┐
│  CefMessageRouter（CEF 框架层）                │
│  - 自动处理进程间通信                          │
│  - 管理请求/响应的路由                         │
│  - 处理异步回调                                │
└───────────────────┬───────────────────────────┘
                    │
                    │ ② 路由到 Handler
                    ↓
┌───────────────────────────────────────────────┐
│  主进程（C++ Handler）                         │
│                                                │
│  bool OnQuery(..., const CefString& request,   │
│               ..., Callback* callback) {       │
│      // 解析请求                               │
│      auto json = parse(request);               │
│                                                │
│      // 调用业务逻辑                           │
│      std::string task_id =                     │
│        download_manager->addTask(...);         │
│                                                │
│      // 返回响应                               │
│      callback->Success(task_id);               │
│  }                                             │
│                                                │
└───────────────────┬───────────────────────────┘
                    │
                    │ ③ 返回响应
                    ↓
            JavaScript onSuccess()
```

### 2. 关键组件

```cpp
// 1. CefMessageRouterConfig - 配置
CefRefPtr<CefMessageRouterConfig> config = CefMessageRouterConfig::Create();
config->js_query_function = "cefQuery";        // JavaScript 函数名
config->js_cancel_function = "cefQueryCancel"; // 取消函数名

// 2. CefMessageRouterBrowserSide - 主进程路由器
CefRefPtr<CefMessageRouterBrowserSide> router_ = 
    CefMessageRouterBrowserSide::Create(config);

// 3. Handler - 处理请求
class MyHandler : public CefMessageRouterBrowserSide::Handler {
    bool OnQuery(...) override {
        // 处理来自前端的请求
    }
};
```

---

## 📁 项目结构

```
MagnetDownload/
├── src/
│   ├── gui/
│   │   ├── CMakeLists.txt
│   │   ├── cef_app.h                    # CefApp 实现
│   │   ├── cef_app.cpp
│   │   ├── cef_client.h                 # CefClient 实现
│   │   ├── cef_client.cpp
│   │   ├── message_handler.h            # 新增：IPC 消息处理
│   │   ├── message_handler.cpp
│   │   ├── api_bridge.h                 # 新增：前端 API 桥接
│   │   ├── api_bridge.cpp
│   │   └── main_win.cpp                 # 主程序入口
│   │
│   └── application/
│       ├── download_manager.h           # 待实现：多任务管理
│       └── download_manager.cpp
│
└── web-ui/
    └── src/
        └── api/
            └── cef_bridge.ts            # 前端：CEF 通信封装
```

---

## 🚀 实施步骤

### Phase 1: 基础 CEF 应用（2-3 天）

#### Step 1.1: 实现基础 CefApp 和 CefClient

参考 `CEF_集成实施指南.md` 中的基础代码。

#### Step 1.2: 集成 CefMessageRouter

**文件：`src/gui/cef_client.h`**

```cpp
#pragma once
#include "include/cef_client.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"
#include "include/wrapper/cef_message_router.h"

class CefClient : public CefClient,
                  public CefLifeSpanHandler,
                  public CefLoadHandler {
public:
    CefClient();
    ~CefClient();
    
    // CefClient methods
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
    
    bool OnProcessMessageReceived(
        CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        CefProcessId source_process,
        CefRefPtr<CefProcessMessage> message) override;
    
    // CefLifeSpanHandler methods
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
    bool DoClose(CefRefPtr<CefBrowser> browser) override;
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
    
    // CefLoadHandler methods
    void OnLoadEnd(CefRefPtr<CefBrowser> browser,
                   CefRefPtr<CefFrame> frame,
                   int httpStatusCode) override;
    
private:
    // CefMessageRouter（核心）
    CefRefPtr<CefMessageRouterBrowserSide> message_router_;
    
    CefRefPtr<CefBrowser> browser_;
    
    IMPLEMENT_REFCOUNTING(CefClient);
};
```

**文件：`src/gui/cef_client.cpp`**

```cpp
#include "cef_client.h"
#include "message_handler.h"
#include "include/wrapper/cef_helpers.h"

CefClient::CefClient() {
    // 创建 MessageRouter 配置
    CefMessageRouterConfig config;
    config.js_query_function = "cefQuery";
    config.js_cancel_function = "cefQueryCancel";
    
    // 创建 MessageRouter
    message_router_ = CefMessageRouterBrowserSide::Create(config);
    
    // 添加自定义 Handler
    message_router_->AddHandler(new MessageHandler(), false);
}

CefClient::~CefClient() {
    message_router_ = nullptr;
}

bool CefClient::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefProcessId source_process,
    CefRefPtr<CefProcessMessage> message) {
    
    CEF_REQUIRE_UI_THREAD();
    
    // 委托给 MessageRouter 处理
    return message_router_->OnProcessMessageReceived(
        browser, frame, source_process, message);
}

void CefClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    browser_ = browser;
    message_router_->OnAfterCreated(browser);
}

bool CefClient::DoClose(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    return false;
}

void CefClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    message_router_->OnBeforeClose(browser);
    browser_ = nullptr;
    CefQuitMessageLoop();
}

void CefClient::OnLoadEnd(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefFrame> frame,
                         int httpStatusCode) {
    CEF_REQUIRE_UI_THREAD();
    
    // 注入 JavaScript API
    if (frame->IsMain()) {
        std::string js_code = R"(
            // CEF Query 是由 CefMessageRouter 自动注入的
            // 这里我们封装一个更友好的 API
            window.MagnetAPI = {
                addTask: function(magnetUri, savePath) {
                    return new Promise((resolve, reject) => {
                        window.cefQuery({
                            request: JSON.stringify({
                                action: 'addTask',
                                magnet_uri: magnetUri,
                                save_path: savePath
                            }),
                            onSuccess: function(response) {
                                resolve(JSON.parse(response));
                            },
                            onFailure: function(error_code, error_message) {
                                reject(new Error(error_message));
                            }
                        });
                    });
                },
                
                getTasks: function() {
                    return new Promise((resolve, reject) => {
                        window.cefQuery({
                            request: JSON.stringify({ action: 'getTasks' }),
                            onSuccess: function(response) {
                                resolve(JSON.parse(response));
                            },
                            onFailure: function(error_code, error_message) {
                                reject(new Error(error_message));
                            }
                        });
                    });
                },
                
                controlTask: function(taskId, action) {
                    return new Promise((resolve, reject) => {
                        window.cefQuery({
                            request: JSON.stringify({
                                action: 'controlTask',
                                task_id: taskId,
                                task_action: action  // 'pause', 'resume', 'remove'
                            }),
                            onSuccess: function(response) {
                                resolve(JSON.parse(response));
                            },
                            onFailure: function(error_code, error_message) {
                                reject(new Error(error_message));
                            }
                        });
                    });
                }
            };
            
            console.log('MagnetAPI initialized');
        )";
        
        frame->ExecuteJavaScript(js_code, frame->GetURL(), 0);
    }
}
```

---

### Phase 2: 实现消息处理器（3-4 天）

#### Step 2.1: 定义 MessageHandler

**文件：`src/gui/message_handler.h`**

```cpp
#pragma once
#include "include/wrapper/cef_message_router.h"
#include <nlohmann/json.hpp>
#include <memory>

// 前向声明
namespace magnet::application {
    class DownloadManager;
}

using json = nlohmann::json;

class MessageHandler : public CefMessageRouterBrowserSide::Handler {
public:
    MessageHandler();
    ~MessageHandler() override;
    
    // 处理来自前端的查询
    bool OnQuery(CefRefPtr<CefBrowser> browser,
                 CefRefPtr<CefFrame> frame,
                 int64_t query_id,
                 const CefString& request,
                 bool persistent,
                 CefRefPtr<Callback> callback) override;
    
    // 查询被取消
    void OnQueryCanceled(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        int64_t query_id) override;
    
    // 设置 DownloadManager
    void SetDownloadManager(std::shared_ptr<magnet::application::DownloadManager> manager);
    
private:
    // 处理不同的 action
    void HandleAddTask(const json& request, CefRefPtr<Callback> callback);
    void HandleGetTasks(const json& request, CefRefPtr<Callback> callback);
    void HandleGetTaskDetail(const json& request, CefRefPtr<Callback> callback);
    void HandleControlTask(const json& request, CefRefPtr<Callback> callback);
    void HandleGetSettings(const json& request, CefRefPtr<Callback> callback);
    void HandleUpdateSettings(const json& request, CefRefPtr<Callback> callback);
    
    // 辅助方法
    void SendSuccess(CefRefPtr<Callback> callback, const json& data);
    void SendError(CefRefPtr<Callback> callback, int code, const std::string& message);
    
private:
    std::shared_ptr<magnet::application::DownloadManager> download_manager_;
    
    IMPLEMENT_REFCOUNTING(MessageHandler);
};
```

**文件：`src/gui/message_handler.cpp`**

```cpp
#include "message_handler.h"
#include "magnet/application/download_manager.h"
#include "include/wrapper/cef_helpers.h"
#include <iostream>

MessageHandler::MessageHandler() {
    // 构造函数
}

MessageHandler::~MessageHandler() {
    // 析构函数
}

void MessageHandler::SetDownloadManager(
    std::shared_ptr<magnet::application::DownloadManager> manager) {
    download_manager_ = manager;
}

bool MessageHandler::OnQuery(CefRefPtr<CefBrowser> browser,
                            CefRefPtr<CefFrame> frame,
                            int64_t query_id,
                            const CefString& request,
                            bool persistent,
                            CefRefPtr<Callback> callback) {
    CEF_REQUIRE_UI_THREAD();
    
    try {
        // 解析 JSON 请求
        std::string request_str = request.ToString();
        json request_json = json::parse(request_str);
        
        // 获取 action
        if (!request_json.contains("action")) {
            SendError(callback, 400, "Missing 'action' field");
            return true;
        }
        
        std::string action = request_json["action"];
        
        // 路由到不同的处理函数
        if (action == "addTask") {
            HandleAddTask(request_json, callback);
        } else if (action == "getTasks") {
            HandleGetTasks(request_json, callback);
        } else if (action == "getTaskDetail") {
            HandleGetTaskDetail(request_json, callback);
        } else if (action == "controlTask") {
            HandleControlTask(request_json, callback);
        } else if (action == "getSettings") {
            HandleGetSettings(request_json, callback);
        } else if (action == "updateSettings") {
            HandleUpdateSettings(request_json, callback);
        } else {
            SendError(callback, 404, "Unknown action: " + action);
        }
        
        return true;
        
    } catch (json::exception& e) {
        SendError(callback, 400, std::string("JSON parse error: ") + e.what());
        return true;
    } catch (std::exception& e) {
        SendError(callback, 500, std::string("Internal error: ") + e.what());
        return true;
    }
}

void MessageHandler::OnQueryCanceled(CefRefPtr<CefBrowser> browser,
                                    CefRefPtr<CefFrame> frame,
                                    int64_t query_id) {
    // 查询被取消（用户刷新页面等）
    std::cout << "Query canceled: " << query_id << std::endl;
}

// ============ 处理不同的 Action ============

void MessageHandler::HandleAddTask(const json& request, CefRefPtr<Callback> callback) {
    if (!download_manager_) {
        SendError(callback, 500, "DownloadManager not initialized");
        return;
    }
    
    // 验证参数
    if (!request.contains("magnet_uri")) {
        SendError(callback, 400, "Missing 'magnet_uri'");
        return;
    }
    
    std::string magnet_uri = request["magnet_uri"];
    std::string save_path = request.value("save_path", "E:\\Downloads");
    std::vector<size_t> selected_files;
    
    if (request.contains("selected_files")) {
        selected_files = request["selected_files"].get<std::vector<size_t>>();
    }
    
    // 调用 DownloadManager
    std::string task_id = download_manager_->addTask(
        magnet_uri, save_path, selected_files);
    
    // 返回响应
    json response = {
        {"success", true},
        {"task_id", task_id}
    };
    SendSuccess(callback, response);
}

void MessageHandler::HandleGetTasks(const json& request, CefRefPtr<Callback> callback) {
    if (!download_manager_) {
        SendError(callback, 500, "DownloadManager not initialized");
        return;
    }
    
    // 获取任务列表
    std::vector<std::string> task_ids = download_manager_->getTaskIds();
    
    json tasks = json::array();
    for (const auto& task_id : task_ids) {
        auto task_info = download_manager_->getTaskInfo(task_id);
        if (task_info) {
            // 转换为 JSON
            json task_json = {
                {"id", task_info->id},
                {"name", task_info->name},
                {"status", taskStatusToString(task_info->status)},
                {"progress", task_info->progress},
                {"download_speed", task_info->download_speed},
                {"upload_speed", task_info->upload_speed},
                {"total_size", task_info->total_size},
                {"downloaded", task_info->downloaded},
                {"eta", task_info->eta},
                {"peers", task_info->peers},
                {"seeds", task_info->seeds}
            };
            tasks.push_back(task_json);
        }
    }
    
    json response = {
        {"success", true},
        {"tasks", tasks}
    };
    SendSuccess(callback, response);
}

void MessageHandler::HandleGetTaskDetail(const json& request, CefRefPtr<Callback> callback) {
    if (!download_manager_) {
        SendError(callback, 500, "DownloadManager not initialized");
        return;
    }
    
    if (!request.contains("task_id")) {
        SendError(callback, 400, "Missing 'task_id'");
        return;
    }
    
    std::string task_id = request["task_id"];
    auto detail = download_manager_->getTaskDetail(task_id);
    
    if (!detail) {
        SendError(callback, 404, "Task not found");
        return;
    }
    
    // 转换为 JSON（省略详细转换代码，类似上面）
    json response = {
        {"success", true},
        {"detail", /* task_detail_json */}
    };
    SendSuccess(callback, response);
}

void MessageHandler::HandleControlTask(const json& request, CefRefPtr<Callback> callback) {
    if (!download_manager_) {
        SendError(callback, 500, "DownloadManager not initialized");
        return;
    }
    
    if (!request.contains("task_id") || !request.contains("task_action")) {
        SendError(callback, 400, "Missing 'task_id' or 'task_action'");
        return;
    }
    
    std::string task_id = request["task_id"];
    std::string action = request["task_action"];
    
    bool success = false;
    if (action == "pause") {
        success = download_manager_->pauseTask(task_id);
    } else if (action == "resume") {
        success = download_manager_->resumeTask(task_id);
    } else if (action == "remove") {
        success = download_manager_->removeTask(task_id);
    } else {
        SendError(callback, 400, "Unknown task_action: " + action);
        return;
    }
    
    json response = {
        {"success", success}
    };
    SendSuccess(callback, response);
}

void MessageHandler::HandleGetSettings(const json& request, CefRefPtr<Callback> callback) {
    // TODO: 实现设置获取
    json response = {
        {"success", true},
        {"settings", {
            {"default_save_path", "E:\\Downloads"},
            {"max_connections", 200}
        }}
    };
    SendSuccess(callback, response);
}

void MessageHandler::HandleUpdateSettings(const json& request, CefRefPtr<Callback> callback) {
    // TODO: 实现设置更新
    json response = {
        {"success", true}
    };
    SendSuccess(callback, response);
}

// ============ 辅助方法 ============

void MessageHandler::SendSuccess(CefRefPtr<Callback> callback, const json& data) {
    callback->Success(data.dump());
}

void MessageHandler::SendError(CefRefPtr<Callback> callback, 
                              int code, 
                              const std::string& message) {
    json error = {
        {"success", false},
        {"error_code", code},
        {"error_message", message}
    };
    callback->Failure(code, message);
}
```

---

### Phase 3: 前端集成（2-3 天）

#### Step 3.1: 封装 CEF Bridge

**文件：`web-ui/src/api/cef_bridge.ts`**

```typescript
/**
 * CEF Bridge - 封装 window.MagnetAPI
 * 提供类型安全的前端 API
 */

// 类型定义
export interface TaskInfo {
  id: string;
  name: string;
  status: string;
  progress: number;
  download_speed: number;
  upload_speed: number;
  total_size: number;
  downloaded: number;
  eta: number;
  peers: number;
  seeds: number;
}

export interface AddTaskRequest {
  magnet_uri: string;
  save_path?: string;
  selected_files?: number[];
}

export interface AddTaskResponse {
  success: boolean;
  task_id: string;
}

// 检查是否在 CEF 环境中
function isCefEnvironment(): boolean {
  return typeof (window as any).MagnetAPI !== 'undefined';
}

// CEF Bridge 类
class CefBridge {
  /**
   * 添加下载任务
   */
  async addTask(request: AddTaskRequest): Promise<string> {
    if (!isCefEnvironment()) {
      throw new Error('Not in CEF environment');
    }
    
    const response = await (window as any).MagnetAPI.addTask(
      request.magnet_uri,
      request.save_path || 'E:\\Downloads'
    );
    
    if (!response.success) {
      throw new Error(response.error_message);
    }
    
    return response.task_id;
  }
  
  /**
   * 获取任务列表
   */
  async getTasks(): Promise<TaskInfo[]> {
    if (!isCefEnvironment()) {
      throw new Error('Not in CEF environment');
    }
    
    const response = await (window as any).MagnetAPI.getTasks();
    
    if (!response.success) {
      throw new Error(response.error_message);
    }
    
    return response.tasks;
  }
  
  /**
   * 控制任务（暂停/恢复/删除）
   */
  async controlTask(taskId: string, action: 'pause' | 'resume' | 'remove'): Promise<void> {
    if (!isCefEnvironment()) {
      throw new Error('Not in CEF environment');
    }
    
    const response = await (window as any).MagnetAPI.controlTask(taskId, action);
    
    if (!response.success) {
      throw new Error(response.error_message);
    }
  }
}

export const cefBridge = new CefBridge();
```

#### Step 3.2: Vue 3 中使用

```typescript
// Vue 3 组件中使用
<script setup lang="ts">
import { ref, onMounted } from 'vue';
import { cefBridge } from '@/api/cef_bridge';

const tasks = ref<TaskInfo[]>([]);

// 添加任务
async function addTask(magnetUri: string) {
  try {
    const taskId = await cefBridge.addTask({
      magnet_uri: magnetUri,
      save_path: 'E:\\Downloads'
    });
    console.log('Task created:', taskId);
    
    // 刷新任务列表
    await refreshTasks();
  } catch (error) {
    console.error('Failed to add task:', error);
  }
}

// 刷新任务列表
async function refreshTasks() {
  try {
    tasks.value = await cefBridge.getTasks();
  } catch (error) {
    console.error('Failed to get tasks:', error);
  }
}

// 控制任务
async function pauseTask(taskId: string) {
  try {
    await cefBridge.controlTask(taskId, 'pause');
    await refreshTasks();
  } catch (error) {
    console.error('Failed to pause task:', error);
  }
}

onMounted(() => {
  refreshTasks();
  
  // 定时刷新（模拟实时更新）
  setInterval(refreshTasks, 2000);
});
</script>
```

---

### Phase 4: 实时更新（WebSocket 替代方案）

由于不用 HTTP/WebSocket，我们需要另一种方式实现实时更新：

#### 方案 A：定时轮询（简单）

```typescript
// 前端：每 2 秒查询一次
setInterval(async () => {
  tasks.value = await cefBridge.getTasks();
}, 2000);
```

#### 方案 B：C++ 主动推送（复杂但优雅）

```cpp
// C++ 端：定时器推送更新
void PushTaskUpdate(CefRefPtr<CefBrowser> browser, const std::string& task_id) {
    auto task_info = download_manager_->getTaskInfo(task_id);
    
    json update = {
        {"type", "task_update"},
        {"task_id", task_id},
        {"progress", task_info->progress},
        {"download_speed", task_info->download_speed}
    };
    
    // 执行 JavaScript 回调
    std::string js_code = "if (window.onTaskUpdate) { window.onTaskUpdate(" 
                        + update.dump() + "); }";
    
    browser->GetMainFrame()->ExecuteJavaScript(js_code, "", 0);
}
```

```javascript
// 前端：监听更新
window.onTaskUpdate = function(update) {
  console.log('Task update:', update);
  // 更新 Vue 组件
};
```

---

## 📚 学习资源

### CEF 官方文档
- **General Usage**: https://bitbucket.org/chromiumembedded/cef/wiki/GeneralUsage
- **MessageRouter**: https://bitbucket.org/chromiumembedded/cef/wiki/GeneralUsage#markdown-header-asynchronous-javascript-bindings

### 示例项目
- **cef/tests/cefclient/**: CEF 官方示例客户端
- 查看 `message_router_handler.h/cpp` 文件

### Chromium 多进程架构
- https://www.chromium.org/developers/design-documents/multi-process-architecture/

---

## 🎯 总结

### 你将学到：
1. ✅ Chromium 多进程架构
2. ✅ 进程间通信（IPC）机制
3. ✅ JavaScript 与 C++ 交互
4. ✅ 浏览器渲染原理
5. ✅ 异步回调处理

### 代码量估算：
- C++ 端：~200 行（MessageHandler）
- 前端封装：~100 行（cef_bridge.ts）
- **总计**：~300 行

### 时间估算：
- Phase 1（基础）：2-3 天
- Phase 2（Handler）：3-4 天
- Phase 3（前端）：2-3 天
- **总计**：7-10 天

---

## 🚀 下一步

准备好开始了吗？我可以：

1. ✅ 立即创建 `message_handler.h/cpp` 完整代码
2. ✅ 修改现有的 `cef_client.cpp` 集成 MessageRouter
3. ✅ 创建前端 `cef_bridge.ts` 封装

你说开始，我就开始写代码！💪

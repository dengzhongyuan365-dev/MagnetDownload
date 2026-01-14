# CEF 集成实施指南（方案 1：纯 CEF 原生窗口）

## 📋 目录

1. [环境准备](#环境准备)
2. [项目结构设计](#项目结构设计)
3. [分步实施计划](#分步实施计划)
4. [关键技术点](#关键技术点)
5. [调试和测试](#调试和测试)
6. [打包部署](#打包部署)
7. [常见问题解决](#常见问题解决)

---

## 1. 环境准备 <a id="环境准备"></a>

### 1.1 下载 CEF 二进制

#### Windows 平台

访问：https://cef-builds.spotifycdn.com/index.html

**推荐版本**：
- **CEF Version**: `120.1.10+g3ce3184+chromium-120.0.6099.129`（或最新稳定版）
- **Distribution**: `Standard Distribution`
- **Platform**: `Windows 64-bit`

下载文件：`cef_binary_120.1.10+g3ce3184+chromium-120.0.6099.129_windows64.tar.bz2`

**解压到**：`E:\proj\MagnetDownload\3rd\cef\`

目录结构应该是：
```
3rd/cef/
├── Release/
│   ├── libcef.dll
│   └── ...
├── Resources/
│   ├── icudtl.dat
│   ├── locales/
│   └── ...
├── include/
│   └── cef_*.h
└── libcef_dll_wrapper/
    └── CMakeLists.txt
```

#### Linux 平台（可选）

下载：`cef_binary_*_linux64.tar.bz2`

#### macOS 平台（可选）

下载：`cef_binary_*_macosx64.tar.bz2`

---

### 1.2 环境依赖检查

```bash
# Windows 需要
- Visual Studio 2019/2022
- CMake >= 3.19
- Windows SDK 10

# Linux 需要
- GCC >= 7.0 或 Clang >= 5.0
- CMake >= 3.19
- GTK 3 开发库

# macOS 需要
- Xcode >= 12
- CMake >= 3.19
```

---

## 2. 项目结构设计 <a id="项目结构设计"></a>

### 2.1 新增目录和文件

```
MagnetDownload/
├── 3rd/
│   └── cef/                      # CEF 二进制（新增）
│       ├── Release/
│       ├── Resources/
│       ├── include/
│       └── libcef_dll_wrapper/
│
├── src/
│   ├── application/              # 现有后端代码
│   │   ├── download_controller.cpp
│   │   ├── download_manager.cpp  # 待实现：多任务管理
│   │   └── web_api_server.cpp    # 待实现：HTTP API 服务器
│   │
│   └── gui/                      # 新增：CEF GUI 层
│       ├── CMakeLists.txt        # CEF 模块构建配置
│       ├── cef_app.h             # CefApp 接口实现
│       ├── cef_app.cpp
│       ├── cef_client.h          # CefClient 接口实现
│       ├── cef_client.cpp
│       ├── main.cpp              # 主程序入口（跨平台）
│       ├── main_win.cpp          # Windows 特定代码（WinMain）
│       ├── main_linux.cpp        # Linux 特定代码
│       └── main_mac.mm           # macOS 特定代码
│
├── web-ui/                       # Vue 3 前端项目（待创建）
│   ├── src/
│   ├── public/
│   ├── package.json
│   └── vite.config.ts
│
├── cmake/
│   ├── FindCEF.cmake             # 新增：CEF 查找脚本
│   └── CopyResources.cmake       # 新增：拷贝 CEF 资源
│
└── CMakeLists.txt                # 根 CMake（需要修改）
```

---

## 3. 分步实施计划 <a id="分步实施计划"></a>

### Phase 1: CEF 基础集成（3-5 天）

#### Step 1.1: 配置 CMake 查找 CEF

**文件**：`cmake/FindCEF.cmake`

**目标**：让 CMake 能够找到 CEF 库和头文件

**任务清单**：
- [ ] 创建 `FindCEF.cmake` 脚本
- [ ] 设置 `CEF_ROOT` 路径
- [ ] 定义 `CEF_INCLUDE_PATH` 和 `CEF_LIBRARIES`
- [ ] 配置 `libcef_dll_wrapper` 子项目

#### Step 1.2: 实现最小 CEF 应用

**文件**：
- `src/gui/cef_app.h/cpp`
- `src/gui/cef_client.h/cpp`
- `src/gui/main_win.cpp`

**目标**：创建一个空白 CEF 窗口

**任务清单**：
- [ ] 实现 `CefApp` 接口（处理进程启动、上下文初始化）
- [ ] 实现 `CefClient` 接口（处理浏览器生命周期）
- [ ] 实现 `WinMain` 入口（Windows）
- [ ] 配置 CMake 构建 GUI 可执行文件

**验证标准**：
- ✅ 运行程序能弹出空白 CEF 窗口
- ✅ 能够加载 `about:blank`
- ✅ 按 F12 能打开 DevTools

#### Step 1.3: 拷贝 CEF 资源文件

**目标**：将 CEF 的 DLL 和资源文件拷贝到输出目录

**任务清单**：
- [ ] 创建 `cmake/CopyResources.cmake` 脚本
- [ ] 在构建后自动拷贝 `libcef.dll`
- [ ] 拷贝 `Resources/` 目录（icudtl.dat、locales 等）
- [ ] 拷贝 `chrome_*.pak` 文件

**验证标准**：
- ✅ `build/bin/Debug/` 目录包含所有必要文件
- ✅ 应用能够独立运行（不依赖源目录）

---

### Phase 2: 后端 API 服务器（5-7 天）

#### Step 2.1: 实现 HTTP API 服务器

**文件**：
- `src/application/web_api_server.h/cpp`

**目标**：提供 RESTful API 供前端调用

**技术选型**：
- **方案 A**：Boost.Beast（性能高，但代码量大）
- **方案 B**：Crow（轻量级，推荐）⭐
- **方案 C**：cpp-httplib（单头文件，最简单）

**推荐**：**Crow**（https://github.com/CrowCpp/Crow）

**任务清单**：
- [ ] 集成 Crow 库（CMake）
- [ ] 实现基础路由框架
- [ ] 实现 CORS 支持（允许 CEF 访问）
- [ ] 实现以下 API：
  - `GET /api/tasks` - 获取任务列表
  - `GET /api/tasks/{id}` - 获取任务详情
  - `POST /api/tasks` - 添加任务
  - `POST /api/tasks/{id}/action` - 控制任务
  - `GET /api/settings` - 获取设置
  - `PUT /api/settings` - 更新设置

**验证标准**：
- ✅ 服务器在 `localhost:8080` 启动
- ✅ 使用 Postman/curl 能够访问 API
- ✅ 返回正确的 JSON 格式

#### Step 2.2: 实现 WebSocket 实时推送

**文件**：`src/application/web_api_server.cpp`

**目标**：实时推送任务进度更新

**任务清单**：
- [ ] 实现 WebSocket 连接管理
- [ ] 实现订阅机制（客户端订阅特定任务）
- [ ] 实现广播机制（推送到所有连接的客户端）
- [ ] 定时推送任务状态（每秒）

**消息格式**：
```json
{
  "type": "task_update",
  "task_id": "abc123",
  "data": {
    "progress": 45.5,
    "download_speed": 716800
  }
}
```

**验证标准**：
- ✅ 前端能够建立 WebSocket 连接
- ✅ 任务进度每秒自动更新

#### Step 2.3: 实现 DownloadManager（多任务管理）

**文件**：`src/application/download_manager.h/cpp`

**目标**：管理多个 `DownloadController` 实例

**任务清单**：
- [ ] 实现任务创建、删除、控制
- [ ] 实现任务列表查询（按状态筛选）
- [ ] 实现任务详情查询（文件、Peers、Pieces）
- [ ] 实现全局统计收集
- [ ] 实现任务状态缓存（减少查询开销）
- [ ] 实现定时器（每秒更新任务状态）

**验证标准**：
- ✅ 能够同时管理多个下载任务
- ✅ API 查询返回正确数据
- ✅ WebSocket 推送正常工作

---

### Phase 3: 前端 Vue 3 项目（7-10 天）

#### Step 3.1: 初始化 Vue 3 项目

**目录**：`web-ui/`

**任务清单**：
- [ ] 使用 `npm create vite@latest` 创建项目
- [ ] 选择 Vue 3 + TypeScript
- [ ] 安装依赖：
  - `element-plus`（UI 组件库）
  - `vue-router`（路由）
  - `pinia`（状态管理）
  - `axios`（HTTP 客户端）
  - `echarts` + `vue-echarts`（图表）
  - `dayjs`（时间处理）
- [ ] 配置 Vite 代理（`/api` -> `http://localhost:8080`）
- [ ] 配置自动导入（`unplugin-auto-import` + `unplugin-vue-components`）

**验证标准**：
- ✅ `npm run dev` 能够启动开发服务器
- ✅ 访问 `http://localhost:5173` 看到 Vue 页面

#### Step 3.2: 实现前端核心功能

**任务清单**：
- [ ] 实现 API Client（`src/api/client.ts`）
- [ ] 实现 WebSocket Client（`src/api/websocket.ts`）
- [ ] 实现 Pinia Store（`src/stores/tasks.ts`、`src/stores/settings.ts`）
- [ ] 实现路由（`src/router/index.ts`）
- [ ] 实现布局（`src/layouts/MainLayout.vue`）
- [ ] 实现组件：
  - `TaskList.vue`（任务列表）
  - `TaskCard.vue`（任务卡片）
  - `AddTaskDialog.vue`（添加任务对话框）
  - `TaskDetail.vue`（任务详情）
  - `Settings.vue`（设置页面）

**验证标准**：
- ✅ 能够添加任务
- ✅ 任务列表实时更新
- ✅ 能够控制任务（暂停/恢复/删除）

#### Step 3.3: 构建生产版本

**任务清单**：
- [ ] 运行 `npm run build` 生成 `dist/` 目录
- [ ] 将 `dist/` 目录拷贝到 C++ 项目的 `resources/web-ui/`
- [ ] 修改 CEF 加载路径（从 `http://localhost:5173` 改为本地文件）

**CEF 加载本地文件**：
```cpp
// 开发模式
std::string url = "http://localhost:5173";

// 生产模式
std::string url = "file:///" + GetExecutablePath() + "/resources/web-ui/index.html";
```

**验证标准**：
- ✅ 生产版本能够离线运行
- ✅ 不依赖 Node.js 开发服务器

---

### Phase 4: 集成和优化（3-5 天）

#### Step 4.1: CEF 自定义协议（可选）

**目的**：更优雅地加载本地资源

**实现**：注册 `magnet://` 协议，将前端资源嵌入到可执行文件中

**任务清单**：
- [ ] 实现 `CefSchemeHandlerFactory`
- [ ] 注册 `magnet://app/` 协议
- [ ] 从内存或资源文件读取前端资源

**好处**：
- ✅ 无需拷贝 `resources/web-ui/` 目录
- ✅ 防止用户修改前端代码
- ✅ 更快的加载速度

#### Step 4.2: 实现系统托盘

**目标**：最小化到系统托盘

**Windows 实现**：
```cpp
// 创建托盘图标
NOTIFYICONDATA nid = {0};
nid.cbSize = sizeof(NOTIFYICONDATA);
nid.hWnd = hwnd;
nid.uID = 1;
nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
nid.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP_ICON));
wcscpy_s(nid.szTip, L"MagnetDownload");
Shell_NotifyIcon(NIM_ADD, &nid);
```

**任务清单**：
- [ ] 实现托盘图标创建
- [ ] 实现右键菜单（显示/退出）
- [ ] 实现双击事件（显示窗口）
- [ ] 实现下载完成通知

#### Step 4.3: 性能优化

**后端优化**：
- [ ] WebSocket 消息合并（减少推送频率）
- [ ] 任务状态缓存（减少 `DownloadController` 查询）
- [ ] 增量更新（只推送变化的字段）

**前端优化**：
- [ ] 虚拟滚动（任务列表超过 100 项）
- [ ] 图表懒加载（只在需要时初始化 ECharts）
- [ ] WebSocket 消息批量处理（100ms 合并一次）

---

## 4. 关键技术点 <a id="关键技术点"></a>

### 4.1 CEF 多进程架构

CEF 使用多进程架构（类似 Chrome）：

```
主进程 (Browser Process)
    ↓ 启动
渲染进程 (Renderer Process) - 运行 JavaScript
GPU 进程 (GPU Process) - 硬件加速
```

**重要**：你的可执行文件会被多次启动！

```cpp
int WINAPI WinMain(HINSTANCE hInstance, ...) {
    CefMainArgs main_args(hInstance);
    
    // 第一步：检查是否是子进程
    int exit_code = CefExecuteProcess(main_args, nullptr, nullptr);
    if (exit_code >= 0) {
        // 这是子进程，直接退出
        return exit_code;
    }
    
    // 第二步：这是主进程，继续初始化
    CefInitialize(main_args, settings, app.get(), nullptr);
    CefRunMessageLoop();
    CefShutdown();
    return 0;
}
```

### 4.2 CEF 与后端通信

**方案 A**：HTTP API + WebSocket（推荐）⭐

```
CEF (Vue 3)  ←→  HTTP/WS (localhost:8080)  ←→  C++ Backend
```

优点：
- ✅ 前后端完全解耦
- ✅ 可以用 Postman 测试 API
- ✅ 前端可以独立开发（`npm run dev`）

**方案 B**：CEF JavaScript Binding

```cpp
// 在 C++ 中注册函数
CefRefPtr<CefV8Value> func = CefV8Value::CreateFunction("addTask", handler);
global->SetValue("addTask", func, V8_PROPERTY_ATTRIBUTE_NONE);

// 在 JavaScript 中调用
window.addTask("magnet:?xt=...");
```

缺点：
- ❌ 需要实现 IPC（进程间通信）
- ❌ 调试困难

**推荐：方案 A**

### 4.3 前端资源加载方式

#### 开发模式

```cpp
// CEF 加载 Vite 开发服务器
CefBrowserHost::CreateBrowser(
    window_info,
    client,
    "http://localhost:5173",  // Vite Dev Server
    browser_settings,
    nullptr, nullptr
);
```

**优点**：热更新、DevTools、快速迭代

#### 生产模式

```cpp
// 方式 1：加载本地文件
std::string url = "file:///" + GetExecutablePath() + "/resources/web-ui/index.html";

// 方式 2：自定义协议（推荐）
std::string url = "magnet://app/index.html";
```

### 4.4 线程模型

CEF 和 ASIO 都使用异步 I/O，需要协调：

```
主线程：CEF UI 线程（CefRunMessageLoop）
后台线程：ASIO I/O 线程（io_context.run）
```

**方案**：
```cpp
// 在单独线程运行 ASIO
std::thread io_thread([&io_context]() {
    io_context.run();
});

// 主线程运行 CEF
CefRunMessageLoop();

// 清理
io_context.stop();
io_thread.join();
```

---

## 5. 调试和测试 <a id="调试和测试"></a>

### 5.1 CEF 调试工具

#### Chrome DevTools

```cpp
// 启用远程调试
CefSettings settings;
settings.remote_debugging_port = 9222;
```

访问：`http://localhost:9222`

#### 日志输出

```cpp
// 启用详细日志
CefSettings settings;
CefString(&settings.log_file).FromASCII("cef.log");
settings.log_severity = LOGSEVERITY_INFO;
```

### 5.2 API 测试

使用 Postman 或 curl：

```bash
# 获取任务列表
curl http://localhost:8080/api/tasks

# 添加任务
curl -X POST http://localhost:8080/api/tasks \
  -H "Content-Type: application/json" \
  -d '{"magnet_uri": "magnet:?xt=...", "save_path": "E:\\Downloads"}'
```

### 5.3 常见调试问题

| 问题 | 原因 | 解决方案 |
|------|------|----------|
| 白屏 | 资源文件缺失 | 检查 `Resources/` 是否拷贝 |
| 崩溃 | 多进程入口错误 | 检查 `CefExecuteProcess` 位置 |
| 加载失败 | CORS 错误 | 后端添加 CORS 响应头 |
| 卡顿 | 主线程阻塞 | 将耗时操作移到后台线程 |

---

## 6. 打包部署 <a id="打包部署"></a>

### 6.1 Windows 安装包（NSIS）

**所需文件**：
```
installer/
├── magnetdownload.exe
├── libcef.dll
├── Resources/
│   ├── icudtl.dat
│   ├── locales/
│   └── *.pak
└── resources/
    └── web-ui/
```

**NSIS 脚本**：
```nsis
; 安装程序脚本
OutFile "MagnetDownload-Setup.exe"
InstallDir "$PROGRAMFILES\MagnetDownload"

Section "MainSection"
    SetOutPath "$INSTDIR"
    File "magnetdownload.exe"
    File "libcef.dll"
    File /r "Resources"
    File /r "resources"
    
    CreateShortcut "$DESKTOP\MagnetDownload.lnk" "$INSTDIR\magnetdownload.exe"
SectionEnd
```

### 6.2 文件大小估算

- CEF 运行时：~120 MB
- 你的可执行文件：~5 MB
- 前端资源：~2 MB
- **总计**：~130 MB

### 6.3 自动更新（可选）

使用 **Sparkle**（macOS）或 **WinSparkle**（Windows）

---

## 7. 常见问题解决 <a id="常见问题解决"></a>

### Q1: CEF 编译报错 "无法找到 include/cef_app.h"

**原因**：CMake 未正确找到 CEF

**解决**：
```cmake
set(CEF_ROOT "${CMAKE_SOURCE_DIR}/3rd/cef")
include_directories(${CEF_ROOT})
```

### Q2: 运行时崩溃 "0xC0000005 访问冲突"

**原因**：未正确调用 `CefExecuteProcess`

**解决**：确保主函数第一行就是：
```cpp
int exit_code = CefExecuteProcess(main_args, nullptr, nullptr);
if (exit_code >= 0) return exit_code;
```

### Q3: CEF 窗口显示空白

**原因**：资源文件未拷贝

**解决**：检查输出目录是否包含：
- `Resources/` 目录
- `*.pak` 文件
- `locales/` 目录

### Q4: 前端无法访问 API（CORS 错误）

**原因**：跨域限制

**解决**：后端添加 CORS 响应头：
```cpp
response.set_header("Access-Control-Allow-Origin", "*");
response.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE");
response.set_header("Access-Control-Allow-Headers", "Content-Type");
```

### Q5: WebSocket 连接失败

**原因**：协议不匹配

**解决**：
- CEF 使用 `ws://localhost:8080/ws`（开发）
- 生产环境需要考虑 HTTPS → `wss://`

---

## 8. 时间估算和里程碑

| 阶段 | 任务 | 预计时间 | 里程碑 |
|------|------|----------|--------|
| Phase 1 | CEF 基础集成 | 3-5 天 | ✅ 能够显示空白 CEF 窗口 |
| Phase 2 | 后端 API 服务器 | 5-7 天 | ✅ API 和 WebSocket 正常工作 |
| Phase 3 | Vue 3 前端 | 7-10 天 | ✅ 能够添加和管理任务 |
| Phase 4 | 集成和优化 | 3-5 天 | ✅ 生产版本可用 |
| **总计** |  | **18-27 天** | **约 1 个月** |

---

## 9. 下一步行动

### 立即执行：

1. **下载 CEF 二进制**
   - 访问 https://cef-builds.spotifycdn.com/index.html
   - 下载 Windows 64-bit Standard Distribution
   - 解压到 `3rd/cef/`

2. **确认开发环境**
   - Visual Studio 2022 已安装
   - CMake >= 3.19
   - Git

3. **开始 Phase 1**
   - 创建 `src/gui/` 目录
   - 实现最小 CEF 应用
   - 验证能够显示窗口

### 需要决策的问题：

1. **HTTP 服务器库选择**：
   - Crow（推荐，轻量级）⭐
   - Boost.Beast（功能强大）
   - cpp-httplib（最简单）

2. **前端组件库**：
   - Element Plus（推荐，组件丰富）⭐
   - Ant Design Vue（企业级）
   - Naive UI（现代化）

3. **图表库**：
   - ECharts（推荐，功能强大）⭐
   - Chart.js（轻量级）

**建议：使用推荐项（⭐），减少选择困难**

---

**准备就绪！现在可以开始实施了吗？** 🚀

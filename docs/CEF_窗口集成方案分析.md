# CEF 窗口集成方案详细分析

## 📋 目录

1. [技术背景](#技术背景)
2. [五大主流方案对比](#五大主流方案对比)
3. [方案详细分析](#方案详细分析)
4. [开源项目实践案例](#开源项目实践案例)
5. [推荐方案](#推荐方案)
6. [实现代码示例](#实现代码示例)

---

## 1. 技术背景 <a id="技术背景"></a>

### 1.1 CEF 的渲染模式

CEF (Chromium Embedded Framework) 提供了两种核心渲染模式：

```
┌─────────────────────────────────────────────────────┐
│           CEF 渲染模式                               │
├─────────────────────────────────────────────────────┤
│  1. 窗口渲染模式 (Windowed Mode)                     │
│     - CEF 直接创建系统窗口                           │
│     - 或者嵌入到现有窗口句柄                          │
│     - GPU 加速渲染                                   │
│                                                      │
│  2. 离屏渲染模式 (Offscreen Rendering / OSR)         │
│     - CEF 渲染到内存缓冲区                           │
│     - 应用自己负责显示                               │
│     - 适合自定义渲染管线                             │
└─────────────────────────────────────────────────────┘
```

### 1.2 你提到的"容器"概念

你说的"类似 Qt 的 widget 容器"，实际上有两种理解：

1. **窗口句柄容器**：提供一个原生窗口句柄（HWND/NSView/XWindow），CEF 在其中创建子窗口
2. **渲染目标容器**：提供一个绘图表面，CEF 将渲染结果输出到这里

---

## 2. 五大主流方案对比 <a id="五大主流方案对比"></a>

| 方案 | 窗口创建 | 复杂度 | 性能 | 跨平台 | 适用场景 | 代表项目 |
|------|----------|--------|------|--------|----------|----------|
| **1. 纯 CEF 原生窗口** | CEF 自己创建 | ⭐ | ⭐⭐⭐⭐⭐ | ✅ | 简单应用 | CEF Simple |
| **2. CEF + Win32/Cocoa/X11** | 应用创建，CEF 嵌入 | ⭐⭐ | ⭐⭐⭐⭐⭐ | ⚠️ | 原生应用 | Spotify Desktop |
| **3. CEF + Qt (QWidget)** | Qt 创建，CEF 嵌入 | ⭐⭐⭐ | ⭐⭐⭐⭐ | ✅ | Qt 应用 | Teamviewer (部分版本) |
| **4. CEF 离屏渲染 (OSR)** | 应用创建，CEF 渲染到内存 | ⭐⭐⭐⭐ | ⭐⭐⭐ | ✅ | 游戏引擎/特殊效果 | OBS Studio |
| **5. Electron (Node.js + CEF)** | Electron 封装 | ⭐ | ⭐⭐⭐⭐ | ✅ | Web 技术栈 | VS Code, Slack |

---

## 3. 方案详细分析 <a id="方案详细分析"></a>

### 方案 1：纯 CEF 原生窗口 ⭐⭐⭐⭐⭐（推荐）

#### 原理
CEF 自己创建并管理系统窗口，应用只需要处理消息循环。

#### 架构图
```
┌──────────────────────────────────────────┐
│        你的 C++ 应用程序                  │
│  ┌────────────────────────────────────┐  │
│  │  CefApp                            │  │
│  │  - OnBeforeCommandLineProcessing   │  │
│  │  - OnContextInitialized            │  │
│  └────────────────────────────────────┘  │
│                                          │
│  ┌────────────────────────────────────┐  │
│  │  CefClient + CefLifeSpanHandler    │  │
│  │  - OnAfterCreated                  │  │
│  │  - DoClose                         │  │
│  └────────────────────────────────────┘  │
│                                          │
│  ┌────────────────────────────────────┐  │
│  │  CefBrowser (CEF 创建的窗口)       │  │
│  │  ┌──────────────────────────────┐  │  │
│  │  │   Chromium 渲染进程           │  │  │
│  │  │   (HTML/CSS/JS)               │  │  │
│  │  └──────────────────────────────┘  │  │
│  └────────────────────────────────────┘  │
└──────────────────────────────────────────┘
         ↓ GPU 加速渲染
   ┌─────────────┐
   │  系统窗口    │ (HWND/NSWindow/XWindow)
   └─────────────┘
```

#### 优点
✅ **最简单**：CEF 官方推荐方式，文档最全  
✅ **性能最好**：GPU 加速，无额外开销  
✅ **跨平台统一**：CEF 抽象了平台差异  
✅ **功能完整**：支持所有 CEF 特性  

#### 缺点
❌ 窗口样式定制受限（需要自定义标题栏）  
❌ 无法与其他 UI 框架深度集成  

#### 代码示例（Windows）

```cpp
// main.cpp
#include "include/cef_app.h"
#include "include/cef_client.h"
#include "include/cef_browser.h"

// 1. 实现 CefApp（应用程序级别）
class MyApp : public CefApp {
public:
    void OnBeforeCommandLineProcessing(
        const CefString& process_type,
        CefRefPtr<CefCommandLine> command_line) override {
        // 配置 CEF 启动参数
        command_line->AppendSwitch("disable-gpu");  // 可选：禁用 GPU
        command_line->AppendSwitch("disable-gpu-compositing");
    }
    
    void OnContextInitialized() override {
        // CEF 初始化完成，创建浏览器窗口
        CefWindowInfo window_info;
        
        // 让 CEF 创建窗口（不需要容器！）
        window_info.SetAsPopup(nullptr, "MagnetDownload");
        window_info.width = 1280;
        window_info.height = 720;
        
        CefBrowserSettings browser_settings;
        
        CefRefPtr<CefClient> client = new MyClient();
        
        // 创建浏览器实例
        CefBrowserHost::CreateBrowser(
            window_info,
            client,
            "http://localhost:5173",  // Vue 3 开发服务器
            browser_settings,
            nullptr,
            nullptr
        );
    }
    
private:
    IMPLEMENT_REFCOUNTING(MyApp);
};

// 2. 实现 CefClient（浏览器级别）
class MyClient : public CefClient,
                 public CefLifeSpanHandler,
                 public CefLoadHandler {
public:
    // 返回生命周期处理器
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override {
        return this;
    }
    
    CefRefPtr<CefLoadHandler> GetLoadHandler() override {
        return this;
    }
    
    // 浏览器创建完成
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
        browser_ = browser;
        LOG_INFO("Browser window created");
    }
    
    // 页面加载完成
    void OnLoadEnd(CefRefPtr<CefBrowser> browser,
                   CefRefPtr<CefFrame> frame,
                   int httpStatusCode) override {
        LOG_INFO("Page loaded successfully");
    }
    
    // 窗口关闭
    bool DoClose(CefRefPtr<CefBrowser> browser) override {
        return false;  // 允许关闭
    }
    
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
        browser_ = nullptr;
        CefQuitMessageLoop();  // 退出消息循环
    }
    
private:
    CefRefPtr<CefBrowser> browser_;
    IMPLEMENT_REFCOUNTING(MyClient);
};

// 3. 主函数
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    CefMainArgs main_args(hInstance);
    
    // 多进程架构：子进程入口
    int exit_code = CefExecuteProcess(main_args, nullptr, nullptr);
    if (exit_code >= 0) {
        return exit_code;
    }
    
    // 主进程：初始化 CEF
    CefSettings settings;
    settings.no_sandbox = true;
    settings.remote_debugging_port = 9222;  // Chrome DevTools
    CefString(&settings.log_file).FromASCII("cef.log");
    
    CefRefPtr<MyApp> app = new MyApp();
    CefInitialize(main_args, settings, app.get(), nullptr);
    
    // 运行消息循环（阻塞）
    CefRunMessageLoop();
    
    // 清理
    CefShutdown();
    return 0;
}
```

#### 使用场景
- ✅ 应用只需要 Web UI（无需原生控件）
- ✅ 想要最简单的实现方式
- ✅ **最推荐给你的项目**

---

### 方案 2：CEF + Win32/Cocoa/X11 原生窗口 ⭐⭐⭐⭐

#### 原理
应用使用原生 API 创建窗口，然后将窗口句柄传递给 CEF，CEF 在其中创建子窗口。

#### 架构图
```
┌───────────────────────────────────────────┐
│   你的 C++ 应用（Win32/Cocoa/X11）         │
│  ┌─────────────────────────────────────┐  │
│  │  原生窗口创建                        │  │
│  │  CreateWindowEx() / [NSWindow ...]  │  │
│  │  ↓ 获取窗口句柄 (HWND/NSView)       │  │
│  └─────────────────────────────────────┘  │
│                 ↓ 传递给 CEF               │
│  ┌─────────────────────────────────────┐  │
│  │  CEF Browser (嵌入模式)             │  │
│  │  window_info.SetAsChild(hwnd, rect) │  │
│  └─────────────────────────────────────┘  │
└───────────────────────────────────────────┘
```

#### 优点
✅ 完全控制窗口样式（标题栏、边框、菜单）  
✅ 可以添加原生控件（如原生按钮、状态栏）  
✅ 可以集成系统托盘、通知  

#### 缺点
❌ 需要编写平台特定代码（Windows/macOS/Linux 不同）  
❌ 窗口消息处理复杂  

#### 代码示例（Windows）

```cpp
// 1. 创建原生窗口
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_SIZE: {
            // 窗口大小改变时，调整 CEF 浏览器大小
            RECT rect;
            GetClientRect(hwnd, &rect);
            
            CefRefPtr<CefBrowser> browser = GetBrowserForWindow(hwnd);
            if (browser) {
                HWND cef_hwnd = browser->GetHost()->GetWindowHandle();
                SetWindowPos(cef_hwnd, nullptr, 
                    rect.left, rect.top, 
                    rect.right - rect.left, 
                    rect.bottom - rect.top,
                    SWP_NOZORDER);
            }
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // 注册窗口类
    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"MagnetDownloadWindow";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassEx(&wc);
    
    // 创建主窗口
    HWND hwnd = CreateWindowEx(
        0,
        L"MagnetDownloadWindow",
        L"MagnetDownload",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,  // 重要：WS_CLIPCHILDREN
        CW_USEDEFAULT, CW_USEDEFAULT,
        1280, 720,
        nullptr, nullptr, hInstance, nullptr
    );
    
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    
    // 初始化 CEF
    CefMainArgs main_args(hInstance);
    CefSettings settings;
    settings.no_sandbox = true;
    CefRefPtr<MyApp> app = new MyApp();
    CefInitialize(main_args, settings, app.get(), nullptr);
    
    // 创建 CEF 浏览器（嵌入到原生窗口）
    CefWindowInfo window_info;
    RECT rect;
    GetClientRect(hwnd, &rect);
    
    // 关键：设置为子窗口模式
    window_info.SetAsChild(hwnd, rect);
    
    CefBrowserSettings browser_settings;
    CefBrowserHost::CreateBrowser(
        window_info,
        new MyClient(),
        "http://localhost:5173",
        browser_settings,
        nullptr,
        nullptr
    );
    
    // 混合消息循环（处理 Win32 和 CEF）
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        CefDoMessageLoopWork();  // CEF 消息处理
    }
    
    CefShutdown();
    return 0;
}
```

#### 使用场景
- ✅ 需要自定义窗口样式（无边框、圆角等）
- ✅ 需要集成原生控件
- ✅ Spotify、Discord 等桌面应用采用此方案

---

### 方案 3：CEF + Qt (QWidget) ⭐⭐⭐

#### 原理
使用 Qt 的 `QWidget` 作为容器，将 CEF 嵌入到 Qt 窗口中。

#### 架构图
```
┌──────────────────────────────────────────┐
│           Qt Application                 │
│  ┌────────────────────────────────────┐  │
│  │  QMainWindow                       │  │
│  │  ┌──────────────────────────────┐  │  │
│  │  │  QWidget (容器)              │  │  │
│  │  │  ↓ 获取 winId() 窗口句柄     │  │  │
│  │  └──────────────────────────────┘  │  │
│  │         ↓ 传递给 CEF                │  │
│  │  ┌──────────────────────────────┐  │  │
│  │  │  CEF Browser (嵌入)          │  │  │
│  │  └──────────────────────────────┘  │  │
│  └────────────────────────────────────┘  │
└──────────────────────────────────────────┘
```

#### 优点
✅ 可以使用 Qt 的原生控件（菜单、工具栏、状态栏）  
✅ 利用 Qt 的跨平台能力  
✅ 可以与现有 Qt 应用集成  

#### 缺点
❌ 需要协调两套消息循环（Qt + CEF）  
❌ 依赖 Qt 框架（体积增加）  
❌ 窗口调整大小时可能闪烁  

#### 代码示例

```cpp
#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include "include/cef_app.h"
#include "include/cef_client.h"

class CefWidget : public QWidget {
public:
    CefWidget(QWidget* parent = nullptr) : QWidget(parent) {
        // 获取 Qt Widget 的原生窗口句柄
        WId window_id = winId();
        
        CefWindowInfo window_info;
        RECT rect = {0, 0, width(), height()};
        
#ifdef _WIN32
        window_info.SetAsChild((HWND)window_id, rect);
#elif defined(__APPLE__)
        window_info.SetAsChild((NSView*)window_id, rect);
#else
        window_info.SetAsChild(window_id, rect);
#endif
        
        CefBrowserSettings browser_settings;
        CefBrowserHost::CreateBrowser(
            window_info,
            new MyClient(),
            "http://localhost:5173",
            browser_settings,
            nullptr,
            nullptr
        );
    }
    
protected:
    void resizeEvent(QResizeEvent* event) override {
        // 调整 CEF 浏览器大小
        if (browser_) {
#ifdef _WIN32
            HWND cef_hwnd = browser_->GetHost()->GetWindowHandle();
            SetWindowPos(cef_hwnd, nullptr, 0, 0, width(), height(), SWP_NOZORDER);
#endif
        }
        QWidget::resizeEvent(event);
    }
    
private:
    CefRefPtr<CefBrowser> browser_;
};

int main(int argc, char* argv[]) {
    // 初始化 CEF
    CefMainArgs main_args(argc, argv);
    CefSettings settings;
    settings.no_sandbox = true;
    CefInitialize(main_args, settings, nullptr, nullptr);
    
    // 初始化 Qt
    QApplication app(argc, argv);
    
    QMainWindow mainWindow;
    mainWindow.setWindowTitle("MagnetDownload");
    mainWindow.resize(1280, 720);
    
    CefWidget* cefWidget = new CefWidget(&mainWindow);
    mainWindow.setCentralWidget(cefWidget);
    
    mainWindow.show();
    
    // 启动 Qt 事件循环（同时处理 CEF）
    int result = app.exec();
    
    CefShutdown();
    return result;
}
```

#### 使用场景
- ✅ 现有 Qt 应用需要嵌入 Web 内容
- ✅ 需要 Qt 的丰富控件（如原生菜单、对话框）

---

### 方案 4：CEF 离屏渲染 (OSR) ⭐⭐

#### 原理
CEF 不创建窗口，而是将渲染结果输出到内存缓冲区，应用自己负责显示。

#### 优点
✅ 完全控制渲染管线  
✅ 可以应用自定义特效（如透明、模糊）  
✅ 适合游戏引擎集成  

#### 缺点
❌ 性能损失（需要 CPU 拷贝）  
❌ 需要手动处理鼠标、键盘事件  
❌ 实现复杂  

#### 使用场景
- ✅ 游戏内嵌浏览器（如虚幻引擎的 Web UI）
- ✅ 需要特殊视觉效果（如 OBS Studio 的浏览器源）

---

### 方案 5：Electron（不推荐给你的项目）

#### 原理
基于 Node.js + Chromium，提供完整的打包方案。

#### 优点
✅ 开发效率最高（纯 JavaScript/TypeScript）  
✅ 社区庞大  

#### 缺点
❌ 与现有 C++ 代码集成困难  
❌ 体积非常大（~150MB）  

---

## 4. 开源项目实践案例 <a id="开源项目实践案例"></a>

### 案例 1：Spotify Desktop（方案 2）

```
架构：CEF + Win32 原生窗口
原因：
- 需要自定义无边框窗口
- 集成系统媒体控制
- 需要原生系统托盘
```

### 案例 2：VS Code（Electron - 方案 5）

```
架构：Electron（封装的 CEF）
原因：
- 开发团队主要是 Web 技术栈
- 快速迭代需求
- 跨平台一致性
```

### 案例 3：OBS Studio 浏览器源（方案 4）

```
架构：CEF 离屏渲染
原因：
- 需要在游戏画面中叠加 Web 内容
- 自定义渲染管线（透明度、色度键）
```

### 案例 4：TeamViewer（方案 3）

```
架构：Qt + CEF（部分模块）
原因：
- 主应用是 Qt 开发
- Web 内容仅占一部分功能
```

---

## 5. 推荐方案 <a id="推荐方案"></a>

### 针对你的 MagnetDownload 项目

**推荐：方案 1（纯 CEF 原生窗口）⭐⭐⭐⭐⭐**

#### 理由

1. **最简单**：
   - 无需学习额外框架（Qt/Win32）
   - CEF 文档最全面
   - 示例代码丰富

2. **性能最好**：
   - GPU 加速
   - 无额外消息循环开销

3. **跨平台统一**：
   - 同一套代码支持 Windows/Linux/macOS
   - CEF 自动处理平台差异

4. **符合你的需求**：
   - 你的 UI 完全是 Vue 3（不需要原生控件）
   - 不需要复杂的窗口定制
   - 专注于 Web UI 开发

#### 项目结构

```
MagnetDownload/
├── cef/
│   ├── cef_app.h/.cpp           # CefApp 实现
│   ├── cef_client.h/.cpp        # CefClient 实现
│   └── main_win.cpp             # Windows 入口
│   └── main_linux.cpp           # Linux 入口
│   └── main_mac.mm              # macOS 入口
├── web-ui/                      # Vue 3 前端
├── src/                         # 现有 C++ 后端
└── CMakeLists.txt
```

---

## 6. 实现代码示例（完整） <a id="实现代码示例"></a>

### 6.1 CMakeLists.txt 配置

```cmake
cmake_minimum_required(VERSION 3.19)
project(MagnetDownload)

# 下载 CEF 二进制（自动化）
include(cmake/DownloadCEF.cmake)
set(CEF_VERSION "120.1.10+g3ce3184+chromium-120.0.6099.129")
download_cef(${CEF_VERSION})

# 查找 CEF
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(CEF REQUIRED)

# 添加 CEF 辅助宏
include_directories(${CEF_INCLUDE_PATH})
add_subdirectory(${CEF_LIBCEF_DLL_WRAPPER_PATH} libcef_dll_wrapper)

# 你的应用程序
add_executable(magnetdownload WIN32 MACOSX_BUNDLE
    cef/cef_app.cpp
    cef/cef_client.cpp
    cef/main_win.cpp
    # 现有后端代码...
)

target_link_libraries(magnetdownload
    libcef_dll_wrapper
    ${CEF_STANDARD_LIBS}
    magnet_core         # 你的后端库
    magnet_protocols
)

# 拷贝 CEF 资源文件
COPY_FILES("magnetdownload" "${CEF_BINARY_FILES}" "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")
COPY_FILES("magnetdownload" "${CEF_RESOURCE_FILES}" "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")
```

### 6.2 完整的 CefApp 实现

```cpp
// cef/cef_app.h
#pragma once
#include "include/cef_app.h"

class MyApp : public CefApp,
              public CefBrowserProcessHandler {
public:
    MyApp();
    
    // CefApp methods
    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
        return this;
    }
    
    void OnBeforeCommandLineProcessing(
        const CefString& process_type,
        CefRefPtr<CefCommandLine> command_line) override;
    
    void OnContextInitialized() override;
    
private:
    IMPLEMENT_REFCOUNTING(MyApp);
};
```

```cpp
// cef/cef_app.cpp
#include "cef_app.h"
#include "cef_client.h"
#include "include/cef_browser.h"
#include "include/cef_command_line.h"
#include "include/wrapper/cef_helpers.h"

MyApp::MyApp() {}

void MyApp::OnBeforeCommandLineProcessing(
    const CefString& process_type,
    CefRefPtr<CefCommandLine> command_line) {
    
    // 禁用沙箱（简化部署）
    command_line->AppendSwitch("no-sandbox");
    
    // 启用远程调试
    command_line->AppendSwitchWithValue("remote-debugging-port", "9222");
    
    // 禁用 GPU（可选，减少崩溃）
    // command_line->AppendSwitch("disable-gpu");
}

void MyApp::OnContextInitialized() {
    CEF_REQUIRE_UI_THREAD();
    
    // 配置浏览器窗口
    CefWindowInfo window_info;
    
#ifdef _WIN32
    // Windows: 让 CEF 创建顶层窗口
    window_info.SetAsPopup(nullptr, "MagnetDownload");
    window_info.width = 1280;
    window_info.height = 720;
#elif defined(__APPLE__)
    // macOS
    window_info.SetAsPopup(nullptr, "MagnetDownload");
    window_info.width = 1280;
    window_info.height = 720;
#else
    // Linux
    window_info.SetAsPopup(nullptr, "MagnetDownload");
    window_info.width = 1280;
    window_info.height = 720;
#endif
    
    // 浏览器设置
    CefBrowserSettings browser_settings;
    browser_settings.file_access_from_file_urls = STATE_ENABLED;  // 允许本地文件访问
    
    // 创建客户端处理器
    CefRefPtr<MyClient> client = new MyClient();
    
    // 创建浏览器
    CefBrowserHost::CreateBrowser(
        window_info,
        client,
        "http://localhost:8080",  // 你的后端 API 地址（生产环境改为本地 HTML）
        browser_settings,
        nullptr,
        nullptr
    );
}
```

### 6.3 完整的 CefClient 实现

```cpp
// cef/cef_client.h
#pragma once
#include "include/cef_client.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"

class MyClient : public CefClient,
                 public CefLifeSpanHandler,
                 public CefLoadHandler {
public:
    MyClient();
    
    // CefClient methods
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
    
    // CefLifeSpanHandler methods
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
    bool DoClose(CefRefPtr<CefBrowser> browser) override;
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
    
    // CefLoadHandler methods
    void OnLoadError(CefRefPtr<CefBrowser> browser,
                     CefRefPtr<CefFrame> frame,
                     ErrorCode errorCode,
                     const CefString& errorText,
                     const CefString& failedUrl) override;
    
private:
    CefRefPtr<CefBrowser> browser_;
    IMPLEMENT_REFCOUNTING(MyClient);
};
```

```cpp
// cef/cef_client.cpp
#include "cef_client.h"
#include "include/wrapper/cef_helpers.h"
#include <iostream>

MyClient::MyClient() {}

void MyClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    browser_ = browser;
    std::cout << "Browser created successfully" << std::endl;
}

bool MyClient::DoClose(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    return false;  // 允许关闭
}

void MyClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    browser_ = nullptr;
    CefQuitMessageLoop();
}

void MyClient::OnLoadError(CefRefPtr<CefBrowser> browser,
                          CefRefPtr<CefFrame> frame,
                          ErrorCode errorCode,
                          const CefString& errorText,
                          const CefString& failedUrl) {
    CEF_REQUIRE_UI_THREAD();
    
    if (errorCode == ERR_ABORTED) return;
    
    std::cerr << "Failed to load URL: " << failedUrl.ToString() 
              << " Error: " << errorText.ToString() << std::endl;
}
```

### 6.4 主程序入口

```cpp
// cef/main_win.cpp
#include "cef_app.h"
#include "include/cef_sandbox_win.h"
#include <windows.h>

int WINAPI WinMain(HINSTANCE hInstance,
                   HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine,
                   int nCmdShow) {
    
    CefMainArgs main_args(hInstance);
    
    // 多进程架构：渲染进程入口
    int exit_code = CefExecuteProcess(main_args, nullptr, nullptr);
    if (exit_code >= 0) {
        return exit_code;
    }
    
    // 主进程：配置 CEF
    CefSettings settings;
    settings.no_sandbox = true;
    settings.remote_debugging_port = 9222;
    CefString(&settings.log_file).FromASCII("cef.log");
    
    // 初始化 CEF
    CefRefPtr<MyApp> app = new MyApp();
    CefInitialize(main_args, settings, app.get(), nullptr);
    
    // 运行消息循环（阻塞直到所有浏览器窗口关闭）
    CefRunMessageLoop();
    
    // 清理
    CefShutdown();
    
    return 0;
}
```

---

## 7. 总结

### 对于你的项目，最佳方案是：

**✅ 方案 1：纯 CEF 原生窗口**

理由：
1. 代码量最少（约 300 行）
2. 性能最好
3. 跨平台最简单
4. 不需要额外学习 Win32/Qt
5. 专注于 Vue 3 UI 开发

### 下一步行动

1. 下载 CEF 二进制（https://cef-builds.spotifycdn.com/index.html）
2. 配置 CMake 构建
3. 实现上面的 3 个文件（cef_app、cef_client、main_win）
4. 运行测试

---

**文档版本**：v1.0  
**创建日期**：2026-01-14  
**作者**：AI Assistant

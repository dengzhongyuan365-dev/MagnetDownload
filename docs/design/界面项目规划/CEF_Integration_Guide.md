# MagnetDownload CEF + 前端集成指南

## 🎯 架构设计

### 整体架构
```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           前端层 (Frontend)                                  │
│  React/Vue.js + TypeScript + Tailwind CSS + Chart.js                       │
├─────────────────────────────────────────────────────────────────────────────┤
│                           CEF 桥接层 (CEF Bridge)                           │
│  JavaScript ↔ C++ 双向通信 | 事件系统 | 状态同步                            │
├─────────────────────────────────────────────────────────────────────────────┤
│                           C++ 后端层 (Backend)                              │
│  DownloadController | DhtClient | PeerManager | FileManager                │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 技术栈选择

#### 前端技术栈
- **框架**: React 18 + TypeScript
- **样式**: Tailwind CSS + Headless UI
- **图表**: Chart.js / Recharts
- **状态管理**: Zustand / Redux Toolkit
- **构建工具**: Vite
- **UI组件**: Ant Design / Material-UI

#### CEF 集成
- **CEF版本**: CEF 118+ (基于 Chromium 118)
- **通信方式**: JavaScript Binding + IPC
- **进程模型**: 多进程架构
- **资源管理**: 自定义资源处理器

## 🏗️ 项目结构

```
MagnetDownload/
├── src/
│   ├── gui/                    # CEF GUI 模块
│   │   ├── cef_app.h/cpp      # CEF 应用程序类
│   │   ├── cef_client.h/cpp   # CEF 客户端处理
│   │   ├── cef_handler.h/cpp  # CEF 事件处理器
│   │   ├── js_bridge.h/cpp    # JavaScript 桥接
│   │   ├── resource_handler.h/cpp # 资源处理器
│   │   └── gui_controller.h/cpp   # GUI 控制器
│   └── main_gui.cpp           # GUI 主程序入口
├── frontend/                  # 前端项目
│   ├── src/
│   │   ├── components/        # React 组件
│   │   ├── pages/            # 页面组件
│   │   ├── hooks/            # 自定义 Hooks
│   │   ├── stores/           # 状态管理
│   │   ├── types/            # TypeScript 类型
│   │   ├── utils/            # 工具函数
│   │   └── api/              # API 接口
│   ├── public/               # 静态资源
│   ├── package.json
│   ├── vite.config.ts
│   └── tsconfig.json
├── resources/                 # CEF 资源文件
│   ├── html/                 # HTML 模板
│   ├── css/                  # 样式文件
│   └── js/                   # JavaScript 文件
└── cmake/
    └── FindCEF.cmake         # CEF 查找脚本
```

## 🔧 CEF 集成实现

### 1. CMake 配置

```cmake
# 在主 CMakeLists.txt 中添加
option(BUILD_CEF_GUI "Build CEF-based GUI" ON)

if(BUILD_CEF_GUI)
    # 查找 CEF
    find_package(CEF REQUIRED)
    
    # 添加 GUI 子目录
    add_subdirectory(src/gui)
    add_subdirectory(frontend)
endif()
```

### 2. CEF 应用程序类

```cpp
// src/gui/cef_app.h
#pragma once

#include "include/cef_app.h"
#include "include/cef_browser_process_handler.h"
#include "include/cef_render_process_handler.h"

namespace magnet::gui {

class CefApp : public CefApp,
               public CefBrowserProcessHandler,
               public CefRenderProcessHandler {
public:
    CefApp();

    // CefApp methods
    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
        return this;
    }
    
    CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override {
        return this;
    }

    // CefBrowserProcessHandler methods
    void OnContextInitialized() override;
    void OnBeforeCommandLineProcessing(
        const CefString& process_type,
        CefRefPtr<CefCommandLine> command_line) override;

    // CefRenderProcessHandler methods
    void OnContextCreated(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefFrame> frame,
                         CefRefPtr<CefV8Context> context) override;

private:
    IMPLEMENT_REFCOUNTING(CefApp);
};

} // namespace magnet::gui
```

### 3. JavaScript 桥接类

```cpp
// src/gui/js_bridge.h
#pragma once

#include "include/cef_v8.h"
#include "../application/download_controller.h"
#include <memory>
#include <functional>

namespace magnet::gui {

/**
 * @brief JavaScript 与 C++ 的桥接类
 * 
 * 提供双向通信能力：
 * - JavaScript 调用 C++ 方法
 * - C++ 向 JavaScript 发送事件
 */
class JavaScriptBridge : public CefV8Handler {
public:
    explicit JavaScriptBridge(
        std::shared_ptr<application::DownloadController> controller);

    // CefV8Handler 接口
    bool Execute(const CefString& name,
                CefRefPtr<CefV8Value> object,
                const CefV8ValueList& arguments,
                CefRefPtr<CefV8Value>& retval,
                CefString& exception) override;

    // 注册 JavaScript 函数
    void RegisterFunctions(CefRefPtr<CefV8Context> context);

    // 向前端发送事件
    void SendEvent(const std::string& event, const CefRefPtr<CefValue>& data);

private:
    // JavaScript 可调用的方法
    bool AddMagnetLink(const CefV8ValueList& arguments, 
                      CefRefPtr<CefV8Value>& retval);
    bool PauseDownload(const CefV8ValueList& arguments, 
                      CefRefPtr<CefV8Value>& retval);
    bool ResumeDownload(const CefV8ValueList& arguments, 
                       CefRefPtr<CefV8Value>& retval);
    bool RemoveDownload(const CefV8ValueList& arguments, 
                       CefRefPtr<CefV8Value>& retval);
    bool GetDownloadList(const CefV8ValueList& arguments, 
                        CefRefPtr<CefV8Value>& retval);
    bool GetDownloadProgress(const CefV8ValueList& arguments, 
                            CefRefPtr<CefV8Value>& retval);

    // 辅助方法
    CefRefPtr<CefValue> ConvertProgressToCefValue(
        const application::DownloadProgress& progress);
    CefRefPtr<CefValue> ConvertMetadataToCefValue(
        const application::TorrentMetadata& metadata);

private:
    std::shared_ptr<application::DownloadController> download_controller_;
    CefRefPtr<CefBrowser> browser_;
    
    IMPLEMENT_REFCOUNTING(JavaScriptBridge);
};

} // namespace magnet::gui
```

### 4. GUI 控制器

```cpp
// src/gui/gui_controller.h
#pragma once

#include "../application/download_controller.h"
#include "js_bridge.h"
#include <memory>
#include <thread>
#include <atomic>

namespace magnet::gui {

/**
 * @brief GUI 控制器
 * 
 * 协调 CEF 界面和后端下载逻辑：
 * - 管理下载任务
 * - 处理用户交互
 * - 同步状态到前端
 */
class GuiController {
public:
    GuiController();
    ~GuiController();

    // 启动 GUI
    bool Initialize();
    void Run();
    void Shutdown();

    // 下载管理
    bool AddMagnetLink(const std::string& magnet_uri, 
                      const std::string& save_path);
    bool PauseDownload(const std::string& download_id);
    bool ResumeDownload(const std::string& download_id);
    bool RemoveDownload(const std::string& download_id);

    // 状态查询
    std::vector<application::DownloadProgress> GetAllProgress() const;
    application::DownloadProgress GetProgress(const std::string& download_id) const;

    // 设置 JavaScript 桥接
    void SetJavaScriptBridge(std::shared_ptr<JavaScriptBridge> bridge);

private:
    // 后台任务线程
    void BackgroundTaskThread();
    
    // 状态同步
    void SyncStateToFrontend();
    
    // 事件处理
    void OnDownloadStateChanged(const std::string& download_id, 
                               application::DownloadState state);
    void OnDownloadProgressUpdated(const std::string& download_id, 
                                  const application::DownloadProgress& progress);

private:
    asio::io_context io_context_;
    std::shared_ptr<application::DownloadController> download_controller_;
    std::shared_ptr<JavaScriptBridge> js_bridge_;
    
    std::thread background_thread_;
    std::atomic<bool> running_{false};
    
    // 下载任务管理
    std::map<std::string, std::shared_ptr<application::DownloadController>> downloads_;
    mutable std::mutex downloads_mutex_;
};

} // namespace magnet::gui
```

## 🎨 前端实现

### 1. 项目初始化

```bash
# 在 MagnetDownload/frontend 目录下
npm create vite@latest . -- --template react-ts
npm install

# 安装依赖
npm install @types/node
npm install tailwindcss @tailwindcss/forms @tailwindcss/typography
npm install @headlessui/react @heroicons/react
npm install chart.js react-chartjs-2
npm install zustand
npm install clsx
```

### 2. TypeScript 类型定义

```typescript
// frontend/src/types/download.ts
export interface DownloadProgress {
  id: string;
  name: string;
  totalSize: number;
  downloadedSize: number;
  uploadedSize: number;
  downloadSpeed: number;
  uploadSpeed: number;
  progress: number;
  state: DownloadState;
  eta: number;
  connectedPeers: number;
  totalPeers: number;
}

export enum DownloadState {
  IDLE = 'idle',
  RESOLVING_METADATA = 'resolving_metadata',
  DOWNLOADING = 'downloading',
  PAUSED = 'paused',
  VERIFYING = 'verifying',
  COMPLETED = 'completed',
  FAILED = 'failed',
  STOPPED = 'stopped'
}

export interface TorrentMetadata {
  infoHash: string;
  name: string;
  totalSize: number;
  pieceLength: number;
  pieceCount: number;
  files: TorrentFile[];
}

export interface TorrentFile {
  path: string;
  size: number;
  startPiece: number;
  endPiece: number;
}

export interface PeerInfo {
  ip: string;
  port: number;
  client: string;
  downloadSpeed: number;
  uploadSpeed: number;
  progress: number;
  country: string;
}
```

### 3. C++ API 接口封装

```typescript
// frontend/src/api/magnetApi.ts
declare global {
  interface Window {
    magnetApi: {
      // 下载管理
      addMagnetLink: (magnetUri: string, savePath: string) => Promise<boolean>;
      pauseDownload: (downloadId: string) => Promise<boolean>;
      resumeDownload: (downloadId: string) => Promise<boolean>;
      removeDownload: (downloadId: string) => Promise<boolean>;
      
      // 状态查询
      getDownloadList: () => Promise<DownloadProgress[]>;
      getDownloadProgress: (downloadId: string) => Promise<DownloadProgress>;
      
      // 事件监听
      onDownloadStateChanged: (callback: (downloadId: string, state: DownloadState) => void) => void;
      onDownloadProgressUpdated: (callback: (downloadId: string, progress: DownloadProgress) => void) => void;
    };
  }
}

export class MagnetApi {
  static async addMagnetLink(magnetUri: string, savePath: string): Promise<boolean> {
    return window.magnetApi.addMagnetLink(magnetUri, savePath);
  }

  static async pauseDownload(downloadId: string): Promise<boolean> {
    return window.magnetApi.pauseDownload(downloadId);
  }

  static async resumeDownload(downloadId: string): Promise<boolean> {
    return window.magnetApi.resumeDownload(downloadId);
  }

  static async removeDownload(downloadId: string): Promise<boolean> {
    return window.magnetApi.removeDownload(downloadId);
  }

  static async getDownloadList(): Promise<DownloadProgress[]> {
    return window.magnetApi.getDownloadList();
  }

  static async getDownloadProgress(downloadId: string): Promise<DownloadProgress> {
    return window.magnetApi.getDownloadProgress(downloadId);
  }

  static onDownloadStateChanged(callback: (downloadId: string, state: DownloadState) => void) {
    window.magnetApi.onDownloadStateChanged(callback);
  }

  static onDownloadProgressUpdated(callback: (downloadId: string, progress: DownloadProgress) => void) {
    window.magnetApi.onDownloadProgressUpdated(callback);
  }
}
```

### 4. 状态管理 (Zustand)

```typescript
// frontend/src/stores/downloadStore.ts
import { create } from 'zustand';
import { DownloadProgress, DownloadState } from '../types/download';
import { MagnetApi } from '../api/magnetApi';

interface DownloadStore {
  downloads: Map<string, DownloadProgress>;
  selectedDownload: string | null;
  
  // Actions
  addDownload: (magnetUri: string, savePath: string) => Promise<boolean>;
  pauseDownload: (downloadId: string) => Promise<boolean>;
  resumeDownload: (downloadId: string) => Promise<boolean>;
  removeDownload: (downloadId: string) => Promise<boolean>;
  selectDownload: (downloadId: string | null) => void;
  updateDownloadProgress: (downloadId: string, progress: DownloadProgress) => void;
  refreshDownloads: () => Promise<void>;
}

export const useDownloadStore = create<DownloadStore>((set, get) => ({
  downloads: new Map(),
  selectedDownload: null,

  addDownload: async (magnetUri: string, savePath: string) => {
    const success = await MagnetApi.addMagnetLink(magnetUri, savePath);
    if (success) {
      get().refreshDownloads();
    }
    return success;
  },

  pauseDownload: async (downloadId: string) => {
    const success = await MagnetApi.pauseDownload(downloadId);
    if (success) {
      get().refreshDownloads();
    }
    return success;
  },

  resumeDownload: async (downloadId: string) => {
    const success = await MagnetApi.resumeDownload(downloadId);
    if (success) {
      get().refreshDownloads();
    }
    return success;
  },

  removeDownload: async (downloadId: string) => {
    const success = await MagnetApi.removeDownload(downloadId);
    if (success) {
      set(state => {
        const newDownloads = new Map(state.downloads);
        newDownloads.delete(downloadId);
        return {
          downloads: newDownloads,
          selectedDownload: state.selectedDownload === downloadId ? null : state.selectedDownload
        };
      });
    }
    return success;
  },

  selectDownload: (downloadId: string | null) => {
    set({ selectedDownload: downloadId });
  },

  updateDownloadProgress: (downloadId: string, progress: DownloadProgress) => {
    set(state => {
      const newDownloads = new Map(state.downloads);
      newDownloads.set(downloadId, progress);
      return { downloads: newDownloads };
    });
  },

  refreshDownloads: async () => {
    const downloadList = await MagnetApi.getDownloadList();
    const downloadsMap = new Map(downloadList.map(d => [d.id, d]));
    set({ downloads: downloadsMap });
  }
}));

// 初始化事件监听
MagnetApi.onDownloadProgressUpdated((downloadId, progress) => {
  useDownloadStore.getState().updateDownloadProgress(downloadId, progress);
});
```

### 5. 主要 React 组件

```tsx
// frontend/src/components/DownloadList.tsx
import React from 'react';
import { useDownloadStore } from '../stores/downloadStore';
import { DownloadState } from '../types/download';
import { formatBytes, formatSpeed, formatTime } from '../utils/format';

export const DownloadList: React.FC = () => {
  const { downloads, selectedDownload, selectDownload, pauseDownload, resumeDownload, removeDownload } = useDownloadStore();

  const getStateIcon = (state: DownloadState) => {
    switch (state) {
      case DownloadState.DOWNLOADING: return '⬇️';
      case DownloadState.PAUSED: return '⏸️';
      case DownloadState.COMPLETED: return '✅';
      case DownloadState.FAILED: return '❌';
      default: return '⏳';
    }
  };

  const getStateColor = (state: DownloadState) => {
    switch (state) {
      case DownloadState.DOWNLOADING: return 'text-blue-600';
      case DownloadState.PAUSED: return 'text-yellow-600';
      case DownloadState.COMPLETED: return 'text-green-600';
      case DownloadState.FAILED: return 'text-red-600';
      default: return 'text-gray-600';
    }
  };

  return (
    <div className="bg-white shadow-sm rounded-lg overflow-hidden">
      <div className="px-4 py-3 border-b border-gray-200">
        <h3 className="text-lg font-medium text-gray-900">下载列表</h3>
      </div>
      
      <div className="divide-y divide-gray-200">
        {Array.from(downloads.values()).map((download) => (
          <div
            key={download.id}
            className={`p-4 hover:bg-gray-50 cursor-pointer ${
              selectedDownload === download.id ? 'bg-blue-50 border-l-4 border-blue-500' : ''
            }`}
            onClick={() => selectDownload(download.id)}
          >
            <div className="flex items-center justify-between">
              <div className="flex-1 min-w-0">
                <div className="flex items-center space-x-3">
                  <span className="text-2xl">{getStateIcon(download.state)}</span>
                  <div className="flex-1">
                    <p className="text-sm font-medium text-gray-900 truncate">
                      {download.name}
                    </p>
                    <p className="text-sm text-gray-500">
                      {formatBytes(download.downloadedSize)} / {formatBytes(download.totalSize)}
                    </p>
                  </div>
                </div>
                
                {/* 进度条 */}
                <div className="mt-2">
                  <div className="flex items-center justify-between text-xs text-gray-600 mb-1">
                    <span>{download.progress.toFixed(1)}%</span>
                    <span>{formatSpeed(download.downloadSpeed)}</span>
                  </div>
                  <div className="w-full bg-gray-200 rounded-full h-2">
                    <div
                      className="bg-blue-600 h-2 rounded-full transition-all duration-300"
                      style={{ width: `${download.progress}%` }}
                    />
                  </div>
                </div>
              </div>
              
              <div className="flex items-center space-x-2 ml-4">
                <span className={`text-sm font-medium ${getStateColor(download.state)}`}>
                  {download.state}
                </span>
                
                {/* 操作按钮 */}
                <div className="flex space-x-1">
                  {download.state === DownloadState.DOWNLOADING ? (
                    <button
                      onClick={(e) => {
                        e.stopPropagation();
                        pauseDownload(download.id);
                      }}
                      className="p-1 text-gray-400 hover:text-gray-600"
                      title="暂停"
                    >
                      ⏸️
                    </button>
                  ) : download.state === DownloadState.PAUSED ? (
                    <button
                      onClick={(e) => {
                        e.stopPropagation();
                        resumeDownload(download.id);
                      }}
                      className="p-1 text-gray-400 hover:text-gray-600"
                      title="恢复"
                    >
                      ▶️
                    </button>
                  ) : null}
                  
                  <button
                    onClick={(e) => {
                      e.stopPropagation();
                      if (confirm('确定要删除这个下载任务吗？')) {
                        removeDownload(download.id);
                      }
                    }}
                    className="p-1 text-gray-400 hover:text-red-600"
                    title="删除"
                  >
                    🗑️
                  </button>
                </div>
              </div>
            </div>
            
            {/* 详细信息 */}
            {download.state === DownloadState.DOWNLOADING && (
              <div className="mt-2 flex items-center space-x-4 text-xs text-gray-500">
                <span>⬇️ {formatSpeed(download.downloadSpeed)}</span>
                <span>⬆️ {formatSpeed(download.uploadSpeed)}</span>
                <span>👥 {download.connectedPeers}/{download.totalPeers}</span>
                {download.eta > 0 && (
                  <span>⏱️ {formatTime(download.eta)}</span>
                )}
              </div>
            )}
          </div>
        ))}
      </div>
      
      {downloads.size === 0 && (
        <div className="p-8 text-center text-gray-500">
          <p>暂无下载任务</p>
          <p className="text-sm mt-1">点击上方的"添加链接"按钮开始下载</p>
        </div>
      )}
    </div>
  );
};
```

## 🚀 构建和部署

### 1. 前端构建脚本

```json
// frontend/package.json
{
  "name": "magnetdownload-frontend",
  "scripts": {
    "dev": "vite",
    "build": "tsc && vite build",
    "build:watch": "tsc && vite build --watch",
    "preview": "vite preview"
  },
  "build": {
    "outDir": "../resources/html",
    "assetsDir": "assets"
  }
}
```

### 2. CMake 集成

```cmake
# frontend/CMakeLists.txt
if(BUILD_CEF_GUI)
    # 检查 Node.js 和 npm
    find_program(NODE_EXECUTABLE node)
    find_program(NPM_EXECUTABLE npm)
    
    if(NOT NODE_EXECUTABLE OR NOT NPM_EXECUTABLE)
        message(FATAL_ERROR "Node.js and npm are required for frontend build")
    endif()
    
    # 安装依赖
    add_custom_target(frontend_install
        COMMAND ${NPM_EXECUTABLE} install
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Installing frontend dependencies"
    )
    
    # 构建前端
    add_custom_target(frontend_build
        COMMAND ${NPM_EXECUTABLE} run build
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        DEPENDS frontend_install
        COMMENT "Building frontend"
    )
    
    # 确保主程序依赖前端构建
    add_dependencies(magnetdownload_gui frontend_build)
endif()
```

## 📋 开发步骤

### 第一阶段：基础框架
1. **CEF 集成** - 配置 CEF 环境和基础应用框架
2. **JavaScript 桥接** - 实现 C++ 和 JavaScript 的双向通信
3. **前端项目初始化** - 创建 React + TypeScript 项目
4. **基础 UI** - 实现主窗口和基本布局

### 第二阶段：核心功能
1. **磁力链接添加** - 实现添加下载的完整流程
2. **下载列表** - 显示和管理下载任务
3. **实时状态同步** - C++ 后端状态实时同步到前端
4. **基本操作** - 暂停、恢复、删除等操作

### 第三阶段：高级功能
1. **详细信息面板** - 显示下载详情、Peers 信息等
2. **设置界面** - 完整的配置管理
3. **图表可视化** - 速度图表、进度可视化
4. **主题系统** - 深色/浅色主题切换

这个方案结合了现代前端技术的灵活性和 C++ 后端的高性能，能够创建出功能强大、界面美观的桌面应用。你想从哪个部分开始实现？
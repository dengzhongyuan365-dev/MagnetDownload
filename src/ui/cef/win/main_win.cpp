// Copyright (c) 2026 MagnetDownload Project. All rights reserved.

#include <windows.h>

#include "include/cef_app.h"
#include "include/cef_sandbox_win.h"

#include "../common/cef_app.h"

// Windows 主函数
int APIENTRY wWinMain(HINSTANCE hInstance,
                      HINSTANCE hPrevInstance,
                      LPTSTR lpCmdLine,
                      int nCmdShow) {
  UNREFERENCED_PARAMETER(hPrevInstance);
  UNREFERENCED_PARAMETER(lpCmdLine);

  // CEF 初始化配置
  CefMainArgs main_args(hInstance);

  // 创建应用
  CefRefPtr<magnet::ui::MagnetCefApp> app(new magnet::ui::MagnetCefApp());

  // 执行子进程逻辑（如果是子进程）
  int exit_code = CefExecuteProcess(main_args, app, nullptr);
  if (exit_code >= 0) {
    // 子进程退出
    return exit_code;
  }

  // CEF 设置
  CefSettings settings;

  // 设置日志文件路径和日志级别
  settings.log_severity = LOGSEVERITY_WARNING;
  CefString(&settings.log_file).FromASCII("magnetdownload_debug.log");

  // 设置资源路径
  // CEF 会在可执行文件同目录查找资源文件

  // 禁用沙箱（简化开发，生产环境建议启用）
  settings.no_sandbox = true;

  // 初始化 CEF
  CefInitialize(main_args, settings, app, nullptr);

  // 运行 CEF 消息循环
  CefRunMessageLoop();

  // 关闭 CEF
  CefShutdown();

  return 0;
}

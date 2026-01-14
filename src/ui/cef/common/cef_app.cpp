// Copyright (c) 2026 MagnetDownload Project. All rights reserved.

#include "cef_app.h"

#include "include/cef_browser.h"
#include "include/cef_command_line.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_helpers.h"

#include "cef_client.h"

namespace magnet {
namespace ui {

MagnetCefApp::MagnetCefApp() {}

void MagnetCefApp::OnContextInitialized() {
  CEF_REQUIRE_UI_THREAD();

  CefRefPtr<CefCommandLine> command_line =
      CefCommandLine::GetGlobalCommandLine();

  // 创建浏览器窗口
  CefRefPtr<MagnetCefClient> client(new MagnetCefClient());

  // 窗口信息
  CefWindowInfo window_info;

#if defined(OS_WIN)
  // Windows: 创建一个弹出窗口
  window_info.SetAsPopup(nullptr, "MagnetDownload");
#endif

  // 浏览器设置
  CefBrowserSettings browser_settings;

  // 初始 URL（这里先加载一个占位页面，稍后可以改为加载本地前端文件）
  std::string url = "data:text/html,<html><body><h1>MagnetDownload</h1><p>CEF Integration Successful!</p></body></html>";

  // 创建浏览器
  CefBrowserHost::CreateBrowser(window_info, client, url, browser_settings,
                                 nullptr, nullptr);
}

}  // namespace ui
}  // namespace magnet

// Copyright (c) 2026 MagnetDownload Project. All rights reserved.

#include "cef_client.h"

#include <sstream>
#include <string>

#include "include/base/cef_callback.h"
#include "include/cef_app.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

#include "message_handler.h"

namespace magnet {
namespace ui {

MagnetCefClient::MagnetCefClient() : is_closing_(false) {
  // 创建消息路由器
  CefMessageRouterConfig config;
  message_router_ = CefMessageRouterBrowserSide::Create(config);

  // 添加消息处理器
  message_router_->AddHandler(new MagnetMessageHandler(), false);
}

MagnetCefClient::~MagnetCefClient() {}

bool MagnetCefClient::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefProcessId source_process,
    CefRefPtr<CefProcessMessage> message) {
  CEF_REQUIRE_UI_THREAD();

  return message_router_->OnProcessMessageReceived(browser, frame,
                                                     source_process, message);
}

void MagnetCefClient::OnTitleChange(CefRefPtr<CefBrowser> browser,
                                     const CefString& title) {
  CEF_REQUIRE_UI_THREAD();

  // 设置窗口标题（仅适用于原生窗口）
  CefWindowHandle hwnd = browser->GetHost()->GetWindowHandle();
  if (hwnd) {
#if defined(OS_WIN)
    SetWindowTextW(hwnd, std::wstring(title).c_str());
#endif
  }
}

void MagnetCefClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();

  // 添加到浏览器列表
  browser_list_.push_back(browser);
}

bool MagnetCefClient::DoClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();

  // 允许关闭浏览器窗口
  return false;
}

void MagnetCefClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();

  // 从列表中移除
  for (auto it = browser_list_.begin(); it != browser_list_.end(); ++it) {
    if ((*it)->IsSame(browser)) {
      browser_list_.erase(it);
      break;
    }
  }

  if (browser_list_.empty()) {
    // 所有浏览器窗口都已关闭，退出消息循环
    CefQuitMessageLoop();
  }
}

void MagnetCefClient::OnLoadError(CefRefPtr<CefBrowser> browser,
                                   CefRefPtr<CefFrame> frame,
                                   ErrorCode errorCode,
                                   const CefString& errorText,
                                   const CefString& failedUrl) {
  CEF_REQUIRE_UI_THREAD();

  // 不显示已取消请求的错误
  if (errorCode == ERR_ABORTED)
    return;

  // 显示错误页面
  std::stringstream ss;
  ss << "<html><body bgcolor=\"white\">"
        "<h2>Failed to load URL "
     << std::string(failedUrl) << " with error " << std::string(errorText)
     << " (" << errorCode << ").</h2></body></html>";

  std::string data_uri = "data:text/html," + ss.str();
  frame->LoadURL(data_uri);
}

void MagnetCefClient::CloseAllBrowsers(bool force_close) {
  if (!CefCurrentlyOn(TID_UI)) {
    // 在 UI 线程上执行
    CefPostTask(TID_UI, base::BindOnce(&MagnetCefClient::CloseAllBrowsers, this,
                                        force_close));
    return;
  }

  is_closing_ = true;

  if (browser_list_.empty())
    return;

  for (auto it = browser_list_.begin(); it != browser_list_.end(); ++it) {
    (*it)->GetHost()->CloseBrowser(force_close);
  }
}

}  // namespace ui
}  // namespace magnet

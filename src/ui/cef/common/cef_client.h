// Copyright (c) 2026 MagnetDownload Project. All rights reserved.

#ifndef MAGNET_UI_CEF_CLIENT_H_
#define MAGNET_UI_CEF_CLIENT_H_

#include "include/cef_client.h"
#include "include/wrapper/cef_message_router.h"

#include <list>

namespace magnet {
namespace ui {

// CEF 客户端类
// 负责处理浏览器事件和消息路由
class MagnetCefClient : public CefClient,
                         public CefDisplayHandler,
                         public CefLifeSpanHandler,
                         public CefLoadHandler {
 public:
  MagnetCefClient();
  ~MagnetCefClient();

  // CefClient 方法
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                 CefRefPtr<CefFrame> frame,
                                 CefProcessId source_process,
                                 CefRefPtr<CefProcessMessage> message) override;

  // CefDisplayHandler 方法
  void OnTitleChange(CefRefPtr<CefBrowser> browser,
                     const CefString& title) override;

  // CefLifeSpanHandler 方法
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  bool DoClose(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

  // CefLoadHandler 方法
  void OnLoadError(CefRefPtr<CefBrowser> browser,
                   CefRefPtr<CefFrame> frame,
                   ErrorCode errorCode,
                   const CefString& errorText,
                   const CefString& failedUrl) override;

  // 关闭所有浏览器
  void CloseAllBrowsers(bool force_close);

  bool IsClosing() const { return is_closing_; }

 private:
  // 浏览器列表
  typedef std::list<CefRefPtr<CefBrowser>> BrowserList;
  BrowserList browser_list_;

  bool is_closing_;

  // 消息路由器（用于 JS <-> C++ 通信）
  CefRefPtr<CefMessageRouterBrowserSide> message_router_;

  IMPLEMENT_REFCOUNTING(MagnetCefClient);
  DISALLOW_COPY_AND_ASSIGN(MagnetCefClient);
};

}  // namespace ui
}  // namespace magnet

#endif  // MAGNET_UI_CEF_CLIENT_H_

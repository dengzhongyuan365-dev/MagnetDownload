// Copyright (c) 2026 MagnetDownload Project. All rights reserved.

#ifndef MAGNET_UI_MESSAGE_HANDLER_H_
#define MAGNET_UI_MESSAGE_HANDLER_H_

#include "include/wrapper/cef_message_router.h"

namespace magnet {
namespace ui {

// 消息处理器
// 负责处理前端（JavaScript）发送的查询请求
class MagnetMessageHandler : public CefMessageRouterBrowserSide::Handler {
 public:
  MagnetMessageHandler();
  ~MagnetMessageHandler() override;

  // CefMessageRouterBrowserSide::Handler 方法
  bool OnQuery(CefRefPtr<CefBrowser> browser,
               CefRefPtr<CefFrame> frame,
               int64_t query_id,
               const CefString& request,
               bool persistent,
               CefRefPtr<Callback> callback) override;

  void OnQueryCanceled(CefRefPtr<CefBrowser> browser,
                       CefRefPtr<CefFrame> frame,
                       int64_t query_id) override;

 private:
  DISALLOW_COPY_AND_ASSIGN(MagnetMessageHandler);
};

}  // namespace ui
}  // namespace magnet

#endif  // MAGNET_UI_MESSAGE_HANDLER_H_

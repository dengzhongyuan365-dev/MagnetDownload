// Copyright (c) 2026 MagnetDownload Project. All rights reserved.

#ifndef MAGNET_UI_CEF_APP_H_
#define MAGNET_UI_CEF_APP_H_

#include "include/cef_app.h"

namespace magnet {
namespace ui {

// CEF 应用类
// 负责处理进程级别的回调和配置
class MagnetCefApp : public CefApp,
                      public CefBrowserProcessHandler {
 public:
  MagnetCefApp();

  // CefApp 方法
  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }

  // CefBrowserProcessHandler 方法
  void OnContextInitialized() override;

 private:
  IMPLEMENT_REFCOUNTING(MagnetCefApp);
  DISALLOW_COPY_AND_ASSIGN(MagnetCefApp);
};

}  // namespace ui
}  // namespace magnet

#endif  // MAGNET_UI_CEF_APP_H_

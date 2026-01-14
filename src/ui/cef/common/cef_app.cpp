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

namespace {

class MagnetWindowDelegate : public CefWindowDelegate {
 public:
  explicit MagnetWindowDelegate(CefRefPtr<CefBrowserView> browser_view)
      : browser_view_(browser_view) {}

  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    window->AddChildView(browser_view_);
    window->Show();
    browser_view_->RequestFocus();
  }

  void OnWindowDestroyed(CefRefPtr<CefWindow> window) override {
    browser_view_ = nullptr;
  }

  bool CanClose(CefRefPtr<CefWindow> window) override {
    CefRefPtr<CefBrowser> browser = browser_view_->GetBrowser();
    if (browser)
      return browser->GetHost()->TryCloseBrowser();
    return true;
  }

  CefSize GetPreferredSize(CefRefPtr<CefView> view) override {
    return CefSize(1280, 800);
  }

 private:
  CefRefPtr<CefBrowserView> browser_view_;

  IMPLEMENT_REFCOUNTING(MagnetWindowDelegate);
  DISALLOW_COPY_AND_ASSIGN(MagnetWindowDelegate);
};

class MagnetBrowserViewDelegate : public CefBrowserViewDelegate {
 public:
  MagnetBrowserViewDelegate() {}

  bool OnPopupBrowserViewCreated(CefRefPtr<CefBrowserView> browser_view,
                                  CefRefPtr<CefBrowserView> popup_browser_view,
                                  bool is_devtools) override {
    CefWindow::CreateTopLevelWindow(
        new MagnetWindowDelegate(popup_browser_view));
    return true;
  }

 private:
  IMPLEMENT_REFCOUNTING(MagnetBrowserViewDelegate);
  DISALLOW_COPY_AND_ASSIGN(MagnetBrowserViewDelegate);
};

}  // namespace

MagnetCefApp::MagnetCefApp() {}

void MagnetCefApp::OnContextInitialized() {
  CEF_REQUIRE_UI_THREAD();

  CefRefPtr<MagnetCefClient> client(new MagnetCefClient());

  std::string url = "data:text/html,<html><body style='font-family:sans-serif;padding:20px;'>"
                    "<h1>MagnetDownload</h1>"
                    "<p>CEF Integration Successful!</p>"
                    "</body></html>";

  CefBrowserSettings browser_settings;

  CefRefPtr<CefBrowserView> browser_view = CefBrowserView::CreateBrowserView(
      client, url, browser_settings, nullptr, nullptr,
      new MagnetBrowserViewDelegate());

  CefWindow::CreateTopLevelWindow(new MagnetWindowDelegate(browser_view));
}

}  // namespace ui
}  // namespace magnet

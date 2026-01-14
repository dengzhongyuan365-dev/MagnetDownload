// Copyright (c) 2026 MagnetDownload Project. All rights reserved.

#include "message_handler.h"

#include "include/wrapper/cef_helpers.h"
#include "json.hpp"

using json = nlohmann::json;

namespace magnet {
namespace ui {

MagnetMessageHandler::MagnetMessageHandler() {}

MagnetMessageHandler::~MagnetMessageHandler() {}

bool MagnetMessageHandler::OnQuery(CefRefPtr<CefBrowser> browser,
                                    CefRefPtr<CefFrame> frame,
                                    int64_t query_id,
                                    const CefString& request,
                                    bool persistent,
                                    CefRefPtr<Callback> callback) {
  CEF_REQUIRE_UI_THREAD();

  // 解析 JSON 请求
  std::string request_str = request.ToString();
  
  try {
    json request_json = json::parse(request_str);
    std::string action = request_json["action"];

    // TODO: 根据 action 调用相应的业务逻辑
    // 目前只是返回一个简单的响应
    
    if (action == "test") {
      json response = {
        {"success", true},
        {"message", "CEF IPC is working!"},
        {"data", request_json["data"]}
      };
      callback->Success(response.dump());
      return true;
    }
    
    // 未知的 action
    json error_response = {
      {"success", false},
      {"error", "Unknown action: " + action}
    };
    callback->Failure(0, error_response.dump());
    return true;

  } catch (const std::exception& e) {
    // JSON 解析失败
    json error_response = {
      {"success", false},
      {"error", std::string("JSON parse error: ") + e.what()}
    };
    callback->Failure(0, error_response.dump());
    return true;
  }
}

void MagnetMessageHandler::OnQueryCanceled(CefRefPtr<CefBrowser> browser,
                                            CefRefPtr<CefFrame> frame,
                                            int64_t query_id) {
  CEF_REQUIRE_UI_THREAD();
  
  // 查询被取消，清理资源（如果有）
}

}  // namespace ui
}  // namespace magnet

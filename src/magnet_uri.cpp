#include "magnet_uri.h"
#include <regex>

namespace magnet {

MagnetURI::MagnetURI(const std::string& uri) {
    parse(uri);
}

bool MagnetURI::parse(const std::string& uri) {
    original_uri_ = uri;
    is_valid_ = false;
    
    // 验证是否是磁力链接
    if (uri.substr(0, 8) != "magnet:?") {
        return false;
    }
    
    // 解析查询参数
    std::string query = uri.substr(8);
    auto params = parseQueryParams(query);
    
    // 提取InfoHash (xt参数，格式为urn:btih:HASH)
    if (params.find("xt") != params.end() && !params["xt"].empty()) {
        for (const auto& xt : params["xt"]) {
            static const std::regex btih_regex("urn:btih:([a-fA-F0-9]{40})", std::regex::icase);
            std::smatch match;
            if (std::regex_search(xt, match, btih_regex) && match.size() > 1) {
                info_hash_ = match[1];
                std::transform(info_hash_.begin(), info_hash_.end(), info_hash_.begin(), 
                              [](unsigned char c){ return std::toupper(c); });
                is_valid_ = true;
                break;
            }
        }
    }
    
    // 如果没有有效的InfoHash，则返回
    if (!is_valid_) {
        return false;
    }
    
    // 提取显示名称 (dn参数)
    if (params.find("dn") != params.end() && !params["dn"].empty()) {
        display_name_ = urlDecode(params["dn"][0]);
    }
    
    // 提取Tracker URL (tr参数)
    if (params.find("tr") != params.end()) {
        for (const auto& tr : params["tr"]) {
            trackers_.push_back(urlDecode(tr));
        }
    }
    
    return true;
}

std::string MagnetURI::urlDecode(const std::string& input) {
    std::string result;
    result.reserve(input.size());
    
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '%' && i + 2 < input.size()) {
            int value;
            std::istringstream is(input.substr(i + 1, 2));
            if (is >> std::hex >> value) {
                result += static_cast<char>(value);
                i += 2;
            } else {
                result += input[i];
            }
        } else if (input[i] == '+') {
            result += ' ';
        } else {
            result += input[i];
        }
    }
    
    return result;
}

std::map<std::string, std::vector<std::string>> MagnetURI::parseQueryParams(const std::string& query) {
    std::map<std::string, std::vector<std::string>> result;
    
    std::istringstream stream(query);
    std::string pair;
    
    while (std::getline(stream, pair, '&')) {
        size_t pos = pair.find('=');
        if (pos != std::string::npos) {
            std::string key = pair.substr(0, pos);
            std::string value = pair.substr(pos + 1);
            result[key].push_back(value);
        }
    }
    
    return result;
}

} // namespace magnet 
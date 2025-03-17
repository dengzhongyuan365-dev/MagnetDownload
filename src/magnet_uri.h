#pragma once

#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <sstream>
#include <iostream>

namespace magnet {

class MagnetURI {
public:
    MagnetURI() = default;
    
    explicit MagnetURI(const std::string& uri);
    
    // 解析URI
    bool parse(const std::string& uri);
    
    // 获取InfoHash (Base16编码)
    std::string getInfoHash() const { return info_hash_; }
    
    // 获取显示名称
    std::string getDisplayName() const { return display_name_; }
    
    // 获取Tracker URL列表
    const std::vector<std::string>& getTrackers() const { return trackers_; }
    
    // 获取原始URI
    std::string getOriginalURI() const { return original_uri_; }
    
    // 检查URI是否有效
    bool isValid() const { return is_valid_; }
    
private:
    std::string original_uri_;
    std::string info_hash_;      // Base16编码的InfoHash
    std::string display_name_;
    std::vector<std::string> trackers_;
    bool is_valid_ = false;
    
    // URL解码函数
    std::string urlDecode(const std::string& input);
    
    // 解析查询参数
    std::map<std::string, std::vector<std::string>> parseQueryParams(const std::string& query);
};

} // namespace magnet 
#pragma once

#include "magnet_types.h"
#include <string_view>
#include <vector>
#include <utility>

namespace magnet::protocols {

/**
 * @brief 磁力链接解析器
 * 
 * 负责解析标准的 Magnet URI 格式：
 * magnet:?xt=urn:btih:<InfoHash>&dn=<name>&tr=<tracker>&...
 * 
 * 使用示例：
 * @code
 * auto result = MagnetUriParser::parse("magnet:?xt=urn:btih:...");
 * if (result.is_ok()) {
 *     const auto& info = result.value();
 *     // 使用解析结果
 * }
 * @endcode
 */
class MagnetUriParser {
public:
    /**
     * @brief 解析磁力链接（静态方法）
     * @param uri 磁力链接字符串（例如：magnet:?xt=urn:btih:...）
     * @return Result<MagnetInfo, ParseError> 
     *         成功返回 MagnetInfo，失败返回错误类型
     * 
     * 解析流程：
     * 1. 验证协议头（magnet:?）
     * 2. 分割查询参数（key=value&key=value）
     * 3. 提取必需字段（xt - InfoHash）
     * 4. 提取可选字段（dn, tr, xl, as, xs, kt）
     * 5. 验证和构造 MagnetInfo
     */
    static Result<MagnetInfo, ParseError> parse(std::string_view uri);
    
    /**
     * @brief 验证 URI 协议头
     * @param uri 完整的 URI 字符串
     * @return true 如果以 "magnet:?" 开头
     */
    static bool validateScheme(std::string_view uri);
    
private:
    // ========== 解析方法 ==========
    
    /**
     * @brief 分割查询参数
     * @param query 查询字符串（不包含 "magnet:?" 前缀）
     * @return 参数键值对列表 [(key, value), ...]
     */
    static std::vector<std::pair<std::string, std::string>> splitParameters(std::string_view query);
    
    // ========== 字段提取方法 ==========
    
    /**
     * @brief 提取 InfoHash（必需字段）
     */
    static std::optional<InfoHash> extractInfoHash(
        const std::vector<std::pair<std::string, std::string>>& params);
    
    /**
     * @brief 提取显示名称（可选字段）
     */
    static std::string extractDisplayName(
        const std::vector<std::pair<std::string, std::string>>& params);
    
    /**
     * @brief 提取 Tracker 列表（可选字段，可多个）
     */
    static std::vector<std::string> extractTrackers(
        const std::vector<std::pair<std::string, std::string>>& params);
    
    /**
     * @brief 提取文件大小（可选字段）
     */
    static std::optional<uint64_t> extractExactLength(
        const std::vector<std::pair<std::string, std::string>>& params);
    
    /**
     * @brief 提取 Web Seed 列表（可选字段，可多个）
     */
    static std::vector<std::string> extractWebSeeds(
        const std::vector<std::pair<std::string, std::string>>& params);
    
    /**
     * @brief 提取 .torrent 文件源列表（可选字段，可多个）
     */
    static std::vector<std::string> extractExactSources(
        const std::vector<std::pair<std::string, std::string>>& params);
    
    /**
     * @brief 提取关键词列表（可选字段）
     */
    static std::vector<std::string> extractKeywords(
        const std::vector<std::pair<std::string, std::string>>& params);
    
    // ========== 辅助方法 ==========
    
    /**
     * @brief URL 解码
     */
    static std::string urlDecode(std::string_view encoded);
    
    /**
     * @brief 解析 InfoHash 值
     */
    static std::optional<InfoHash> parseInfoHashValue(std::string_view value);
    
    /**
     * @brief 从参数列表中查找指定 key 的值
     */
    static std::optional<std::string> findParameter(
        const std::vector<std::pair<std::string, std::string>>& params,
        std::string_view key);
    
    /**
     * @brief 从参数列表中查找指定 key 的所有值（支持多个相同 key）
     */
    static std::vector<std::string> findAllParameters(
        const std::vector<std::pair<std::string, std::string>>& params,
        std::string_view key);
};

} // namespace magnet::protocols


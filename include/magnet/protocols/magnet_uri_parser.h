#pragma once

#include "magnet_types.h"
#include <string_view>
#include <vector>
#include <utility>

namespace magnet {

/**
 * @brief 磁力链接解析器
 * 
 * 负责解析标准的 Magnet URI 格式：
 * magnet:?xt=urn:btih:<InfoHash>&dn=<name>&tr=<tracker>&...
 * 
 * 使用示例：
 * @code
 * MagnetUriParser parser;
 * auto result = parser.parse("magnet:?xt=urn:btih:...");
 * if (result.is_ok()) {
 *     const auto& info = result.value();
 *     // 使用解析结果
 * }
 * @endcode
 */
class MagnetUriParser {
public:
    MagnetUriParser() = default;
    ~MagnetUriParser() = default;
    
    // 禁止拷贝和移动
    MagnetUriParser(const MagnetUriParser&) = delete;
    MagnetUriParser& operator=(const MagnetUriParser&) = delete;
    MagnetUriParser(MagnetUriParser&&) = default;
    MagnetUriParser& operator=(MagnetUriParser&&) = default;
    
    /**
     * @brief 解析磁力链接
     * @param uri 磁力链接字符串（例如：magnet:?xt=urn:btih:...）
     * @return ParseResult<MagnetInfo, ENUM_PARSE_ERROR> 
     *         成功返回 MagnetInfo，失败返回错误类型
     * 
     * 解析流程：
     * 1. 验证协议头（magnet:?）
     * 2. 分割查询参数（key=value&key=value）
     * 3. 提取必需字段（xt - InfoHash）
     * 4. 提取可选字段（dn, tr, xl, as, xs, kt）
     * 5. 验证和构造 MagnetInfo
     */
    ParseResult<MagnetInfo, ENUM_PARSE_ERROR> parse(std::string_view uri);
    
private:
    // ========== 验证方法 ==========
    
    /**
     * @brief 验证 URI 协议头
     * @param uri 完整的 URI 字符串
     * @return true 如果以 "magnet:?" 开头
     */
    bool validateScheme(std::string_view uri);
    
    // ========== 解析方法 ==========
    
    /**
     * @brief 分割查询参数
     * @param query 查询字符串（不包含 "magnet:?" 前缀）
     * @return 参数键值对列表 [(key, value), ...]
     * 
     * 例如：
     * 输入："xt=urn:btih:ABC&dn=file.mp4&tr=udp://tracker.com:80"
     * 输出：[("xt", "urn:btih:ABC"), ("dn", "file.mp4"), ("tr", "udp://tracker.com:80")]
     */
    std::vector<std::pair<std::string, std::string>> splitParameters(std::string_view query);
    
    // ========== 字段提取方法 ==========
    
    /**
     * @brief 提取 InfoHash（必需字段）
     * @param params 参数列表
     * @return std::optional<InfoHash> 成功返回 InfoHash，失败返回 nullopt
     * 
     * 查找 "xt" 参数，格式：xt=urn:btih:<hash>
     * 支持十六进制（40字符）和 Base32（32字符）编码
     */
    std::optional<InfoHash> extractInfoHash(
        const std::vector<std::pair<std::string, std::string>>& params);
    
    /**
     * @brief 提取显示名称（可选字段）
     * @param params 参数列表
     * @return std::optional<std::string> 成功返回名称，失败返回 nullopt
     * 
     * 查找 "dn" 参数，需要进行 URL 解码
     */
    std::optional<std::string> extractDisplayName(
        const std::vector<std::pair<std::string, std::string>>& params);
    
    /**
     * @brief 提取 Tracker 列表（可选字段，可多个）
     * @param params 参数列表
     * @return std::vector<std::string> Tracker URL 列表
     * 
     * 查找所有 "tr" 参数，需要进行 URL 解码
     */
    std::vector<std::string> extractTrackers(
        const std::vector<std::pair<std::string, std::string>>& params);
    
    /**
     * @brief 提取文件大小（可选字段）
     * @param params 参数列表
     * @return std::optional<uint64_t> 成功返回字节数，失败返回 nullopt
     * 
     * 查找 "xl" 参数，解析为 uint64_t
     */
    std::optional<uint64_t> extractExactLength(
        const std::vector<std::pair<std::string, std::string>>& params);
    
    /**
     * @brief 提取 Web Seed 列表（可选字段，可多个）
     * @param params 参数列表
     * @return std::vector<std::string> Web Seed URL 列表
     * 
     * 查找所有 "as" 参数（Acceptable Source）
     */
    std::vector<std::string> extractWebSeeds(
        const std::vector<std::pair<std::string, std::string>>& params);
    
    /**
     * @brief 提取 .torrent 文件源列表（可选字段，可多个）
     * @param params 参数列表
     * @return std::vector<std::string> .torrent URL 列表
     * 
     * 查找所有 "xs" 参数（eXact Source）
     */
    std::vector<std::string> extractExactSources(
        const std::vector<std::pair<std::string, std::string>>& params);
    
    /**
     * @brief 提取关键词列表（可选字段）
     * @param params 参数列表
     * @return std::vector<std::string> 关键词列表
     * 
     * 查找 "kt" 参数，按 '+' 分割
     * 例如："kt=ubuntu+linux+iso" -> ["ubuntu", "linux", "iso"]
     */
    std::vector<std::string> extractKeywords(
        const std::vector<std::pair<std::string, std::string>>& params);
    
    // ========== 辅助方法 ==========
    
    /**
     * @brief URL 解码
     * @param encoded URL 编码的字符串
     * @return std::string 解码后的字符串
     * 
     * 例如：
     * "Ubuntu%2022.04.iso" -> "Ubuntu 22.04.iso"
     * "%E4%B8%AD%E6%96%87" -> "中文"
     */
    std::string urlDecode(std::string_view encoded);
    
    /**
     * @brief 解析 InfoHash 值
     * @param value xt 参数的值（例如："urn:btih:ABC123..."）
     * @return std::optional<InfoHash> 成功返回 InfoHash，失败返回 nullopt
     * 
     * 处理流程：
     * 1. 验证前缀 "urn:btih:"
     * 2. 提取哈希值部分
     * 3. 判断是十六进制（40字符）还是 Base32（32字符）
     * 4. 调用相应的转换方法
     */
    std::optional<InfoHash> parseInfoHashValue(std::string_view value);
    
    /**
     * @brief 从参数列表中查找指定 key 的值
     * @param params 参数列表
     * @param key 要查找的键
     * @return std::optional<std::string> 找到返回值，否则返回 nullopt
     */
    std::optional<std::string> findParameter(
        const std::vector<std::pair<std::string, std::string>>& params,
        std::string_view key);
    
    /**
     * @brief 从参数列表中查找指定 key 的所有值（支持多个相同 key）
     * @param params 参数列表
     * @param key 要查找的键
     * @return std::vector<std::string> 所有匹配的值
     */
    std::vector<std::string> findAllParameters(
        const std::vector<std::pair<std::string, std::string>>& params,
        std::string_view key);
};

} // namespace magnet


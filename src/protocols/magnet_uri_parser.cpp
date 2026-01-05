// MagnetDownload - Magnet URI Parser Implementation
// 磁力链接解析器实现

#include "../../include/magnet/protocols/magnet_uri_parser.h"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace magnet::protocols {

// ============================================================================
// 常量定义
// ============================================================================

namespace {

/// SHA-1 哈希的十六进制字符串长度（40 字符）
constexpr size_t kHexHashLength = 40;

/// SHA-1 哈希的 Base32 字符串长度（32 字符）
constexpr size_t kBase32HashLength = 32;

/// URL 协议前缀长度
constexpr size_t kUdpPrefixLength = 6;    // "udp://"
constexpr size_t kHttpPrefixLength = 7;   // "http://"
constexpr size_t kHttpsPrefixLength = 8;  // "https://"
constexpr size_t kWssPrefixLength = 6;    // "wss://"

/// 磁力链接前缀
const std::string kMagnetPrefix = "magnet:?";

/// BitTorrent InfoHash URN 前缀
const std::string kBtihPrefix = "urn:btih:";

/// Base32 解码表（RFC 4648）
/// 索引为 ASCII 码，值为对应的 5-bit 数值，0xFF 表示无效字符
constexpr uint8_t kBase32DecodeTable[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0x00-0x07
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0x08-0x0F
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0x10-0x17
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0x18-0x1F
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0x20-0x27 (空格-')
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0x28-0x2F
    0xFF, 0xFF, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,  // 0x30-0x37 ('0'-'7', 2-7有效)
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0x38-0x3F
    0xFF, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,  // 0x40-0x47 (A-G)
    0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E,  // 0x48-0x4F (H-O)
    0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,  // 0x50-0x57 (P-W)
    0x17, 0x18, 0x19, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0x58-0x5F (X-Z)
    0xFF, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,  // 0x60-0x67 (a-g)
    0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E,  // 0x68-0x6F (h-o)
    0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,  // 0x70-0x77 (p-w)
    0x17, 0x18, 0x19, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF   // 0x78-0x7F (x-z)
};

// ============================================================================
// 内部辅助函数
// ============================================================================

/**
 * @brief 十六进制字符转换为字节值
 * @param c 十六进制字符（0-9, a-f, A-F）
 * @return 字节值（0-15），无效字符返回 0xFF
 */
uint8_t hexCharToByte(char c) {
    if (c >= '0' && c <= '9') {
        return static_cast<uint8_t>(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return static_cast<uint8_t>(c - 'a' + 10);
    }
    if (c >= 'A' && c <= 'F') {
        return static_cast<uint8_t>(c - 'A' + 10);
    }
    return 0xFF;
}

/**
 * @brief URL 解码函数
 * @param str URL 编码的字符串
 * @return 解码后的字符串
 *
 * 将 %XX 格式的编码转换为字符，+ 转换为空格
 */
std::string urlDecode(const std::string& str) {
    std::string result;
    result.reserve(str.size());

    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '%' && i + 2 < str.size()) {
            const char hex_high = str[i + 1];
            const char hex_low = str[i + 2];

            if (std::isxdigit(hex_high) && std::isxdigit(hex_low)) {
                const uint8_t byte = (hexCharToByte(hex_high) << 4) 
                                   | hexCharToByte(hex_low);
                result.push_back(static_cast<char>(byte));
                i += 2;
            } else {
                // 无效的 %XX 编码，保持原样
                result.push_back(str[i]);
            }
        } else if (str[i] == '+') {
            result.push_back(' ');
        } else {
            result.push_back(str[i]);
        }
    }

    return result;
}

/**
 * @brief URL 编码函数
 * @param str 原始字符串
 * @return URL 编码后的字符串
 *
 * 保留字母数字和 -_.~ ，空格转为 +，其他字符转为 %XX
 */
std::string urlEncode(const std::string& str) {
    static const char kHexChars[] = "0123456789ABCDEF";
    
    std::string encoded;
    encoded.reserve(str.size() * 3);  // 预分配，最坏情况每字符变 3 字符

    for (unsigned char c : str) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += c;
        } else if (c == ' ') {
            encoded += '+';
        } else {
            encoded += '%';
            encoded += kHexChars[c >> 4];
            encoded += kHexChars[c & 0x0F];
        }
    }

    return encoded;
}

/**
 * @brief 字符串分割函数
 * @param str 要分割的字符串
 * @param delimiter 分隔符
 * @return 分割后的字符串向量（跳过空字符串）
 */
std::vector<std::string> split(const std::string& str, char delimiter) {
    std::vector<std::string> result;
    size_t start = 0;
    size_t end = 0;

    while ((end = str.find(delimiter, start)) != std::string::npos) {
        if (end > start) {
            result.push_back(str.substr(start, end - start));
        }
        start = end + 1;
    }

    // 处理最后一个元素
    if (start < str.size()) {
        result.push_back(str.substr(start));
    }

    return result;
}

/**
 * @brief 验证 Tracker URL 格式
 * @param url Tracker URL
 * @return 格式有效返回 true
 */
bool validateTrackerUrl(const std::string& url) {
    if (url.empty()) {
        return false;
    }

    return url.compare(0, kUdpPrefixLength, "udp://") == 0 ||
           url.compare(0, kHttpPrefixLength, "http://") == 0 ||
           url.compare(0, kHttpsPrefixLength, "https://") == 0 ||
           url.compare(0, kWssPrefixLength, "wss://") == 0;
}

/**
 * @brief 验证 Web Seed URL 格式
 * @param url Web Seed URL
 * @return 格式有效返回 true
 */
bool validateWebSeedUrl(const std::string& url) {
    if (url.empty()) {
        return false;
    }

    return url.compare(0, kHttpPrefixLength, "http://") == 0 ||
           url.compare(0, kHttpsPrefixLength, "https://") == 0;
}

/**
 * @brief 解析单个参数键值对
 * @param param "key=value" 格式的字符串
 * @return 解析结果，失败返回错误
 */
Result<std::pair<std::string, std::string>, ParseError>
parseParameter(const std::string& param) {
    const size_t eq_pos = param.find('=');
    if (eq_pos == std::string::npos || eq_pos == 0) {
        return Result<std::pair<std::string, std::string>, ParseError>::err(
            ParseError::INVALID_PARAMETER);
    }

    const std::string key = urlDecode(param.substr(0, eq_pos));
    const std::string value = urlDecode(param.substr(eq_pos + 1));

    return Result<std::pair<std::string, std::string>, ParseError>::ok({key, value});
}

/**
 * @brief 解析 xt 参数值
 * @param xt_value "urn:btih:哈希值" 格式的字符串
 * @return 解析结果，失败返回错误
 */
Result<InfoHash, ParseError> parseXtValue(const std::string& xt_value) {
    // 验证前缀
    if (xt_value.size() < kBtihPrefix.size() ||
        xt_value.substr(0, kBtihPrefix.size()) != kBtihPrefix) {
        return Result<InfoHash, ParseError>::err(ParseError::INVALID_INFO_HASH);
    }

    const std::string hash_str = xt_value.substr(kBtihPrefix.size());

    // 40 字符十六进制编码
    if (hash_str.size() == kHexHashLength) {
        auto hash = InfoHash::fromHex(hash_str);
        if (hash) {
            return Result<InfoHash, ParseError>::ok(*hash);
        }
        return Result<InfoHash, ParseError>::err(ParseError::INVALID_HEX_ENCODING);
    }

    // 32 字符 Base32 编码
    if (hash_str.size() == kBase32HashLength) {
        auto hash = InfoHash::fromBase32(hash_str);
        if (hash) {
            return Result<InfoHash, ParseError>::ok(*hash);
        }
        return Result<InfoHash, ParseError>::err(ParseError::INVALID_BASE32_ENCODING);
    }

    // 不支持的哈希长度
    return Result<InfoHash, ParseError>::err(ParseError::INVALID_INFO_HASH);
}

/**
 * @brief 安全解析 uint64_t
 * @param str 数字字符串
 * @return 解析结果，失败返回 std::nullopt
 */
std::optional<uint64_t> parseUint64(const std::string& str) {
    if (str.empty()) {
        return std::nullopt;
    }

    uint64_t value = 0;
    auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value);

    if (ec == std::errc() && ptr == str.data() + str.size()) {
        return value;
    }

    return std::nullopt;
}

/**
 * @brief 处理单个解析参数
 * @param key 参数键
 * @param value 参数值（已 URL 解码）
 * @param info 输出的 MagnetInfo 结构
 * @return 解析成功返回 true，遇到致命错误返回 false
 */
std::optional<ParseError> processParameter(const std::string& key,
                                            const std::string& value,
                                            MagnetInfo& info) {
    if (key == "xt") {
        // InfoHash 参数（必需）
        auto hash_result = parseXtValue(value);
        if (hash_result.is_err()) {
            return hash_result.error();
        }
        info.info_hash = hash_result.value();
    } else if (key == "dn") {
        // 显示名称
        info.display_name = value;
    } else if (key == "tr") {
        // Tracker URL
        if (validateTrackerUrl(value)) {
            info.trackers.push_back(value);
        }
    } else if (key == "xl") {
        // 文件大小
        info.exact_length = parseUint64(value);
    } else if (key == "ws") {
        // Web Seed URL
        if (validateWebSeedUrl(value)) {
            info.web_seeds.push_back(value);
        }
    } else if (key == "as" || key == "mt") {
        // 备用源 / Manifest Topic
        info.exact_sources.push_back(value);
    } else if (key == "kt") {
        // 关键字参数
        // 格式: kt=keyword1+keyword2+keyword3
        // 注意：必须从原始值分割，因为 '+' 是关键字分隔符
        // 但此时 value 已经被 URL 解码，'+' 已变为空格，所以按空格分割
        std::vector<std::string> keywords = split(value, ' ');
        info.keywords.insert(info.keywords.end(), keywords.begin(), keywords.end());
    }
    // 忽略未知参数

    return std::nullopt;
}

}  // anonymous namespace

// ============================================================================
// InfoHash 类方法实现
// ============================================================================

std::optional<InfoHash> InfoHash::fromHex(const std::string& hex) {
    if (hex.size() != kHexHashLength) {
        return std::nullopt;
    }

    ByteArray bytes{};

    for (size_t i = 0; i < kHexHashLength; i += 2) {
        const uint8_t high = hexCharToByte(hex[i]);
        const uint8_t low = hexCharToByte(hex[i + 1]);

        if (high == 0xFF || low == 0xFF) {
            return std::nullopt;
        }

        bytes[i / 2] = (high << 4) | low;
    }

    return InfoHash(bytes);
}

std::optional<InfoHash> InfoHash::fromBase32(const std::string& base32) {
    if (base32.size() != kBase32HashLength) {
        return std::nullopt;
    }

    ByteArray bytes{};
    size_t buffer = 0;
    size_t bits = 0;
    size_t byte_index = 0;

    for (char c : base32) {
        // 检查字符范围，防止数组越界
        const auto uc = static_cast<unsigned char>(c);
        if (uc >= sizeof(kBase32DecodeTable)) {
            return std::nullopt;
        }

        const uint8_t value = kBase32DecodeTable[uc];
        if (value == 0xFF) {
            return std::nullopt;
        }

        buffer = (buffer << 5) | value;
        bits += 5;

        if (bits >= 8) {
            bytes[byte_index++] = static_cast<uint8_t>((buffer >> (bits - 8)) & 0xFF);
            bits -= 8;
        }
    }

    // 32 个 Base32 字符应该刚好解码为 20 字节，不应该有剩余位
    if (bits != 0 || byte_index != HASHSIZE) {
        return std::nullopt;
    }

    return InfoHash(bytes);
}

std::string InfoHash::toHex() const {
    static const char kHexChars[] = "0123456789abcdef";
    
    std::string result;
    result.reserve(HASHSIZE * 2);

    for (uint8_t byte : data_) {
        result += kHexChars[byte >> 4];
        result += kHexChars[byte & 0x0F];
    }

    return result;
}

// ============================================================================
// 主解析函数实现
// ============================================================================

Result<MagnetInfo, ParseError> parseMagnetUri(const std::string& uri) {
    // 1. 空值检查
    if (uri.empty()) {
        return Result<MagnetInfo, ParseError>::err(ParseError::EMPTY_URI);
    }

    // 2. 验证协议头
    if (uri.size() < kMagnetPrefix.size() ||
        uri.compare(0, kMagnetPrefix.size(), kMagnetPrefix) != 0) {
        return Result<MagnetInfo, ParseError>::err(ParseError::INVALID_SCHEME);
    }

    // 3. 初始化结果结构
    MagnetInfo info;
    info.original_uri = uri;

    // 4. 提取并解析查询参数
    const std::string query = uri.substr(kMagnetPrefix.size());
    const std::vector<std::string> params = split(query, '&');

    for (const auto& param : params) {
        auto param_result = parseParameter(param);
        if (param_result.is_err()) {
            // 跳过无效参数，继续解析
            continue;
        }

        const auto& [key, value] = param_result.value();
        auto error = processParameter(key, value, info);
        if (error.has_value()) {
            return Result<MagnetInfo, ParseError>::err(error.value());
        }
    }

    // 5. 最终验证：必须有有效的 info_hash
    if (!info.info_hash.has_value() || !info.info_hash->is_valid()) {
        return Result<MagnetInfo, ParseError>::err(ParseError::MISSING_INFO_HASH);
    }

    return Result<MagnetInfo, ParseError>::ok(info);
}

// ============================================================================
// 构建磁力链接函数实现
// ============================================================================

std::string buildMagnetUri(const MagnetInfo& info) {
    if (!info.is_valid()) {
        return "";
    }

    std::string result = kMagnetPrefix;
    bool has_param = false;

    // 辅助函数：添加参数分隔符
    auto addSeparator = [&result, &has_param]() {
        if (has_param) {
            result += '&';
        }
        has_param = true;
    };

    // xt 参数（必需）
    if (info.info_hash) {
        addSeparator();
        result += "xt=";
        result += kBtihPrefix;
        result += info.info_hash->toHex();
    }

    // dn 参数（显示名称）
    if (!info.display_name.empty()) {
        addSeparator();
        result += "dn=";
        result += urlEncode(info.display_name);
    }

    // tr 参数（Tracker，可多个）
    for (const auto& tracker : info.trackers) {
        addSeparator();
        result += "tr=";
        result += urlEncode(tracker);
    }

    // xl 参数（文件大小）
    if (info.exact_length.has_value()) {
        addSeparator();
        result += "xl=";
        result += std::to_string(info.exact_length.value());
    }

    // ws 参数（Web Seed，可多个）
    for (const auto& web_seed : info.web_seeds) {
        addSeparator();
        result += "ws=";
        result += urlEncode(web_seed);
    }

    // kt 参数（关键字）
    if (!info.keywords.empty()) {
        addSeparator();
        result += "kt=";

        bool first_keyword = true;
        for (const auto& keyword : info.keywords) {
            if (!first_keyword) {
                result += '+';
            }
            result += urlEncode(keyword);
            first_keyword = false;
        }
    }

    return result;
}

// ============================================================================
// 工具函数实现
// ============================================================================

std::optional<InfoHash> extractInfoHash(const std::string& uri) {
    auto result = parseMagnetUri(uri);
    if (result.is_ok() && result.value().info_hash.has_value()) {
        return result.value().info_hash;
    }
    return std::nullopt;
}

bool isValidMagnetUri(const std::string& uri) {
    auto result = parseMagnetUri(uri);
    return result.is_ok() && result.value().is_valid();
}

std::string toString(const MagnetInfo& info) {
    std::string result;
    result.reserve(512);  // 预估大小

    result += "=== Magnet Info ===\n";
    result += "Valid: ";
    result += (info.is_valid() ? "Yes" : "No");
    result += "\n";
    result += "Original URI: ";
    result += info.original_uri;
    result += "\n";

    if (info.info_hash) {
        result += "Info Hash: ";
        result += info.info_hash->toHex();
        result += "\n";
    }

    if (!info.display_name.empty()) {
        result += "Display Name: ";
        result += info.display_name;
        result += "\n";
    }

    if (info.exact_length.has_value()) {
        const double size_mb = info.exact_length.value() / (1024.0 * 1024.0);
        result += "Exact Length: ";
        result += std::to_string(info.exact_length.value());
        result += " bytes (";
        result += std::to_string(size_mb);
        result += " MB)\n";
    }

    if (!info.trackers.empty()) {
        result += "Trackers (";
        result += std::to_string(info.trackers.size());
        result += "):\n";
        for (size_t i = 0; i < info.trackers.size(); ++i) {
            result += "  [";
            result += std::to_string(i + 1);
            result += "] ";
            result += info.trackers[i];
            result += "\n";
        }
    }

    if (!info.web_seeds.empty()) {
        result += "Web Seeds (";
        result += std::to_string(info.web_seeds.size());
        result += "):\n";
        for (size_t i = 0; i < info.web_seeds.size(); ++i) {
            result += "  [";
            result += std::to_string(i + 1);
            result += "] ";
            result += info.web_seeds[i];
            result += "\n";
        }
    }

    if (!info.keywords.empty()) {
        result += "Keywords (";
        result += std::to_string(info.keywords.size());
        result += "): ";
        for (size_t i = 0; i < info.keywords.size(); ++i) {
            if (i > 0) {
                result += ", ";
            }
            result += info.keywords[i];
        }
        result += "\n";
    }

    result += "===================\n";

    return result;
}

}  // namespace magnet::protocols

// MagnetDownload - Magnet URI Parser Implementation
// 占位文件 - 你将要实现磁力链接解析器

#include "../../include/magnet/protocols/magnet_uri_parser.h"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <cassert>

namespace magnet::protocols {

    // ============================================================================
    // 内部辅助函数和常量
    // ============================================================================

    namespace {

        // Base32解码表（RFC 4648）
        static constexpr uint8_t BASE32_REVERSE[] = {
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E,
            0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E,
            0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
        };

        /**
         * @brief 十六进制字符转换为字节值
         * @param c 十六进制字符（0-9, a-f, A-F）
         * @return 字节值（0-15），无效字符返回0xFF
         */
        static uint8_t hexCharToByte(char c) {
            if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
            if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
            if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
            return 0xFF; // 无效字符标记
        }

        /** jh
         * @brief URL解码函数
         * @param str URL编码的字符串
         * @return 解码后的字符串
         *
         * 将 %XX 格式的编码转换为字符，+ 转换为空格
         */
        static std::string urlDecode(const std::string& str) {
            std::string result;
            result.reserve(str.size());

            for (size_t i = 0; i < str.size(); ++i) {
                if (str[i] == '%' && i + 2 < str.size()) {
                    char hex1 = str[i + 1];
                    char hex2 = str[i + 2];

                    if (std::isxdigit(hex1) && std::isxdigit(hex2)) {
                        uint8_t byte = (hexCharToByte(hex1) << 4) | hexCharToByte(hex2);
                        result.push_back(static_cast<char>(byte));
                        i += 2;
                    }
                    else {
                        // 无效的%编码，保持原样
                        result.push_back(str[i]);
                    }
                }
                else if (str[i] == '+') {
                    result.push_back(' ');
                }
                else {
                    result.push_back(str[i]);
                }
            }

            return result;
        }

        /**
         * @brief 字符串分割函数
         * @param str 要分割的字符串
         * @param delimiter 分隔符
         * @return 分割后的字符串向量
         */
        static std::vector<std::string> split(const std::string& str, char delimiter) {
            std::vector<std::string> result;
            std::stringstream ss(str);
            std::string item;

            while (std::getline(ss, item, delimiter)) {
                if (!item.empty()) {
                    result.push_back(item);
                }
            }

            return result;
        }

        /**
         * @brief 验证Tracker URL格式
         * @param url Tracker URL
         * @return 格式有效返回true
         */
        static bool validateTrackerUrl(const std::string& url) {
            if (url.empty()) return false;

            // 支持常见的Tracker协议
            return (url.compare(0, 6, "udp://") == 0 ||
                url.compare(0, 7, "http://") == 0 ||
                url.compare(0, 8, "https://") == 0 ||
                url.compare(0, 3, "wss://") == 0);
        }

        /**
         * @brief 验证Web Seed URL格式
         * @param url Web Seed URL
         * @return 格式有效返回true
         */
        static bool validateWebSeedUrl(const std::string& url) {
            if (url.empty()) return false;

            return (url.compare(0, 7, "http://") == 0 ||
                url.compare(0, 8, "https://") == 0);
        }

        /**
         * @brief 解析单个参数键值对
         * @param param "key=value"格式的字符串
         * @return 解析结果，失败返回错误
         */
        static Result<std::pair<std::string, std::string>, ParseError>
            parseParameter(const std::string& param) {
            size_t eq_pos = param.find('=');
            if (eq_pos == std::string::npos || eq_pos == 0) {
                return Result<std::pair<std::string, std::string>, ParseError>::err(
                    ParseError::INVALID_PARAMETER);
            }

            std::string key = param.substr(0, eq_pos);
            std::string value = param.substr(eq_pos + 1);

            // URL解码键和值
            key = urlDecode(key);
            value = urlDecode(value);

            return Result<std::pair<std::string, std::string>, ParseError>::ok({ key, value });
        }

        /**
         * @brief 解析xt参数值
         * @param xtValue "urn:btih:哈希值"格式的字符串
         * @return 解析结果，失败返回错误
         */
        static Result<InfoHash, ParseError> parseXtValue(const std::string& xtValue) {
            const std::string prefix = "urn:btih:";
            if (xtValue.size() < prefix.size() || xtValue.substr(0, prefix.size()) != prefix) {
                return Result<InfoHash, ParseError>::err(ParseError::INVALID_INFO_HASH);
            }

            std::string hashStr = xtValue.substr(prefix.size());

            // 情况1：40个字符的十六进制编码
            if (hashStr.size() == 40) {
                auto hash = InfoHash::fromHex(hashStr);
                if (hash) {
                    return Result<InfoHash, ParseError>::ok(*hash);
                }
                else {
                    return Result<InfoHash, ParseError>::err(ParseError::INVALID_HEX_ENCODING);
                }
            }
            // 情况2：32个字符的Base32编码
            else if (hashStr.size() == 32) {
                auto hash = InfoHash::fromBase32(hashStr);
                if (hash) {
                    return Result<InfoHash, ParseError>::ok(*hash);
                }
                else {
                    return Result<InfoHash, ParseError>::err(ParseError::INVALID_BASE32_ENCODING);
                }
            }
            // 情况3：不支持的哈希长度
            else {
                return Result<InfoHash, ParseError>::err(ParseError::INVALID_INFO_HASH);
            }
        }

        /**
         * @brief 安全解析uint64_t
         * @param str 数字字符串
         * @return 解析结果，失败返回std::nullopt
         */
        static std::optional<uint64_t> parseUint64(const std::string& str) {
            if (str.empty()) {
                return std::nullopt;
            }

            uint64_t value;
            auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value);

            if (ec == std::errc() && ptr == str.data() + str.size()) {
                return value;
            }

            return std::nullopt;
        }

    } // 匿名命名空间结束

    // ============================================================================
    // InfoHash 类方法实现
    // ============================================================================

    std::optional<InfoHash> InfoHash::fromHex(const std::string& hex) {
        if (hex.size() != 40) {
            return std::nullopt;
        }

        ByteArray bytes{};

        for (size_t i = 0; i < 40; i += 2) {
            uint8_t high = hexCharToByte(hex[i]);
            uint8_t low = hexCharToByte(hex[i + 1]);

            if (high == 0xFF || low == 0xFF) {
                return std::nullopt;
            }

            bytes[i / 2] = (high << 4) | low;
        }

        return InfoHash(bytes);
    }

    std::optional<InfoHash> InfoHash::fromBase32(const std::string& base32) {
        if (base32.size() != 32) {
            return std::nullopt;
        }

        ByteArray bytes{};
        size_t buffer = 0;
        size_t bits = 0;
        size_t byteIndex = 0;

        for (char c : base32) {
            uint8_t value = BASE32_REVERSE[static_cast<unsigned char>(c)];
            if (value == 0xFF) {
                return std::nullopt;
            }

            buffer = (buffer << 5) | value;
            bits += 5;

            if (bits >= 8) {
                bytes[byteIndex++] = static_cast<uint8_t>((buffer >> (bits - 8)) & 0xFF);
                bits -= 8;
            }
        }

        // 32个Base32字符应该刚好解码为20字节，不应该有剩余位
        if (bits != 0 || byteIndex != HASHSIZE) {
            return std::nullopt;
        }

        return InfoHash(bytes);
    }

    std::string InfoHash::toHex() const {
        std::stringstream ss;
        ss << std::hex << std::setfill('0') << std::nouppercase;

        for (uint8_t byte : data_) {
            ss << std::setw(2) << static_cast<int>(byte);
        }

        return ss.str();
    }

    // ============================================================================
    // 主解析函数实现
    // ============================================================================

    Result<MagnetInfo, ParseError> parseMagnetUri(const std::string& uri) {
        // 1. 基本验证
        if (uri.empty()) {
            return Result<MagnetInfo, ParseError>::err(ParseError::EMPTY_URI);
        }

        // 2. 验证协议头
        const std::string magnet_prefix = "magnet:?";
        if (uri.size() < magnet_prefix.size() ||
            uri.substr(0, magnet_prefix.size()) != magnet_prefix) {
            return Result<MagnetInfo, ParseError>::err(ParseError::INVALID_SCHEME);
        }

        // 3. 初始化结果结构
        MagnetInfo info;
        info.original_uri = uri;

        // 4. 提取查询参数
        std::string query = uri.substr(magnet_prefix.size());
        std::vector<std::string> params = split(query, '&');

        // 5. 遍历并解析所有参数
        for (const auto& param : params) {
            auto paramResult = parseParameter(param);
            if (paramResult.is_err()) {
                // 跳过无效参数，不中断解析
                continue;
            }

            auto [key, value] = paramResult.value();

            // 根据参数类型处理
            if (key == "xt") {
                auto hashResult = parseXtValue(value);
                if (hashResult.is_err()) {
                    return Result<MagnetInfo, ParseError>::err(hashResult.error());
                }
                info.info_hash = hashResult.value();
            }
            else if (key == "dn") {
                info.display_name = value;
            }
            else if (key == "tr") {
                if (validateTrackerUrl(value)) {
                    info.trackers.push_back(value);
                }
            }
            else if (key == "xl") {
                info.exact_length = parseUint64(value);
            }
            else if (key == "ws") {
                if (validateWebSeedUrl(value)) {
                    info.web_seeds.push_back(value);
                }
            }
            else if (key == "as" || key == "mt") {
                info.exact_sources.push_back(value);
            }
            else if (key == "kt") {
                std::string decoded = urlDecode(value);
                std::vector<std::string> keywords = split(decoded, ' ');

                if (keywords.size() == 1 && !decoded.empty()) {
                    keywords = split(decoded, '+');
                }

                info.keywords.insert(info.keywords.end(), keywords.begin(), keywords.end());
            }
            // 忽略未知参数
        }

        // 6. 最终验证：必须要有有效的 info_hash
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

        std::stringstream ss;
        ss << "magnet:?";

        bool first_param = true;

        // 添加 xt 参数（必需）
        if (info.info_hash) {
            if (!first_param) ss << "&";
            ss << "xt=urn:btih:" << info.info_hash->toHex();
            first_param = false;
        }

        // 添加 dn 参数
        if (!info.display_name.empty()) {
            if (!first_param) ss << "&";
            ss << "dn=";

            // URL编码显示名称
            for (char c : info.display_name) {
                if (std::isalnum(static_cast<unsigned char>(c)) ||
                    c == '-' || c == '_' || c == '.' || c == '~') {
                    ss << c;
                }
                else if (c == ' ') {
                    ss << '+';
                }
                else {
                    ss << '%' << std::hex << std::setw(2) << std::setfill('0')
                        << static_cast<int>(static_cast<unsigned char>(c)) << std::dec;
                }
            }
            first_param = false;
        }

        // 添加 tr 参数（可多个）
        for (const auto& tracker : info.trackers) {
            if (!first_param) ss << "&";
            ss << "tr=" << tracker;
            first_param = false;
        }

        // 添加 xl 参数
        if (info.exact_length.has_value()) {
            if (!first_param) ss << "&";
            ss << "xl=" << info.exact_length.value();
            first_param = false;
        }

        // 添加 ws 参数（可多个）
        for (const auto& web_seed : info.web_seeds) {
            if (!first_param) ss << "&";
            ss << "ws=" << web_seed;
            first_param = false;
        }

        // 添加 kt 参数
        if (!info.keywords.empty()) {
            if (!first_param) ss << "&";
            ss << "kt=";

            bool first_keyword = true;
            for (const auto& keyword : info.keywords) {
                if (!first_keyword) ss << "+";

                for (char c : keyword) {
                    if (std::isalnum(static_cast<unsigned char>(c)) ||
                        c == '-' || c == '_' || c == '.' || c == '~') {
                        ss << c;
                    }
                    else if (c == ' ') {
                        ss << '+';
                    }
                    else {
                        ss << '%' << std::hex << std::setw(2) << std::setfill('0')
                            << static_cast<int>(static_cast<unsigned char>(c)) << std::dec;
                    }
                }
                first_keyword = false;
            }
        }

        return ss.str();
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
        std::stringstream ss;

        ss << "=== Magnet Info ===\n";
        ss << "Valid: " << (info.is_valid() ? "Yes" : "No") << "\n";
        ss << "Original URI: " << info.original_uri << "\n";

        if (info.info_hash) {
            ss << "Info Hash: " << info.info_hash->toHex() << "\n";
        }

        if (!info.display_name.empty()) {
            ss << "Display Name: " << info.display_name << "\n";
        }

        if (info.exact_length.has_value()) {
            ss << "Exact Length: " << info.exact_length.value()
                << " bytes (" << (info.exact_length.value() / (1024.0 * 1024.0)) << " MB)\n";
        }

        if (!info.trackers.empty()) {
            ss << "Trackers (" << info.trackers.size() << "):\n";
            for (size_t i = 0; i < info.trackers.size(); ++i) {
                ss << "  [" << i + 1 << "] " << info.trackers[i] << "\n";
            }
        }

        if (!info.web_seeds.empty()) {
            ss << "Web Seeds (" << info.web_seeds.size() << "):\n";
            for (size_t i = 0; i < info.web_seeds.size(); ++i) {
                ss << "  [" << i + 1 << "] " << info.web_seeds[i] << "\n";
            }
        }

        if (!info.keywords.empty()) {
            ss << "Keywords (" << info.keywords.size() << "): ";
            for (size_t i = 0; i < info.keywords.size(); ++i) {
                if (i > 0) ss << ", ";
                ss << info.keywords[i];
            }
            ss << "\n";
        }

        ss << "===================\n";

        return ss.str();
    }

} // namespace magnet::protocols

#ifndef MAGNET_TYPES_H
#define MAGNET_TYPES_H

#pragma once

#include <string>
#include <optional>
#include <vector>
#include <array>
#include <variant>
#include <cstdint>

namespace magnet::protocols {

    // ============================================================================
    // InfoHash 类 - 表示磁力链接的哈希值
    // ============================================================================

    /**
     * @class InfoHash
     * @brief 表示磁力链接的20字节哈希值
     *
     * 支持两种编码格式：
     * 1. 十六进制：40个字符（0-9, a-f）
     * 2. Base32：32个字符（A-Z, 2-7）
     */
    class InfoHash {
    public:
        static constexpr size_t HASHSIZE = 20;  ///< SHA-1哈希的标准长度（20字节）
        using ByteArray = std::array<uint8_t, HASHSIZE>;  ///< 字节数组类型

        InfoHash() = default;  ///< 默认构造函数

        /**
         * @brief 从字节数组构造InfoHash
         * @param bytes 20字节的数组
         */
        explicit InfoHash(const ByteArray& bytes) : data_(bytes) {}

        /**
         * @brief 从十六进制字符串构造InfoHash
         * @param hex 40个字符的十六进制字符串
         * @return 如果解析成功返回InfoHash，否则返回std::nullopt
         */
        static std::optional<InfoHash> fromHex(const std::string& hex);

        /**
         * @brief 从Base32字符串构造InfoHash
         * @param base32 32个字符的Base32字符串
         * @return 如果解析成功返回InfoHash，否则返回std::nullopt
         */
        static std::optional<InfoHash> fromBase32(const std::string& base32);

        /**
         * @brief 转换为十六进制字符串表示
         * @return 40个字符的小写十六进制字符串
         */
        std::string toHex() const;

        /**
         * @brief 获取原始字节数据（const版本）
         * @return 20字节的数组引用
         */
        const ByteArray& bytes() const { return data_; }

        /**
         * @brief 获取原始字节数据（非const版本）
         * @return 20字节的数组引用
         */
        ByteArray& bytes() { return data_; }

        /**
         * @brief 验证哈希值是否有效
         * @return 如果哈希值不全为0则返回true
         */
        bool is_valid() const {
            return data_ != ByteArray{};
        }

        // 比较运算符
        bool operator==(const InfoHash& other) const { return data_ == other.data_; }
        bool operator!=(const InfoHash& other) const { return data_ != other.data_; }
        bool operator<(const InfoHash& other) const { return data_ < other.data_; }

    private:
        ByteArray data_{};  ///< 存储20字节哈希值的内部数组
    };

    // ============================================================================
    // MagnetInfo 结构体 - 存储解析后的磁力链接信息
    // ============================================================================

    /**
     * @struct MagnetInfo
     * @brief 存储解析后的磁力链接所有信息
     */
    struct MagnetInfo {
        std::optional<InfoHash> info_hash;      ///< 必需：信息哈希（通过xt参数）
        std::string display_name;               ///< 可选：显示名称（dn参数）
        std::vector<std::string> trackers;      ///< 可选：Tracker服务器列表（tr参数）
        std::optional<uint64_t> exact_length;   ///< 可选：文件精确大小（xl参数）
        std::vector<std::string> web_seeds;     ///< 可选：Web种子列表（ws参数）
        std::vector<std::string> exact_sources; ///< 可选：.torrent文件来源（as/mt参数）
        std::vector<std::string> keywords;      ///< 可选：关键词列表（kt参数）
        std::string original_uri;               ///< 原始磁力链接字符串

        /**
         * @brief 检查MagnetInfo是否有效
         * @return 如果有有效的info_hash则返回true
         */
        bool is_valid() const {
            return info_hash.has_value() && info_hash->is_valid();
        }

        /**
         * @brief 检查是否有Tracker服务器
         * @return 如果有Tracker则返回true
         */
        bool has_trackers() const {
            return !trackers.empty();
        }
    };

    // ============================================================================
    // ParseError 枚举 - 解析错误类型
    // ============================================================================

    /**
     * @enum ParseError
     * @brief 磁力链接解析过程中可能发生的错误类型
     */
    enum class ParseError {
        INVALID_SCHEME,          ///< 不是 "magnet:?" 协议头
        MISSING_INFO_HASH,       ///< 缺少必需的 xt 参数
        INVALID_INFO_HASH,       ///< InfoHash 格式错误
        INVALID_HEX_ENCODING,    ///< 十六进制编码错误
        INVALID_BASE32_ENCODING, ///< Base32 编码错误
        INVALID_URL_ENCODING,    ///< URL 编码错误
        INVALID_TRACKER_URL,     ///< Tracker URL 格式错误
        INVALID_PARAMETER,       ///< 参数格式错误（缺少=号等）
        EMPTY_URI                ///< 空的 URI 字符串
    };

    // ============================================================================
    // Result 模板类 - 函数式错误处理
    // ============================================================================

    /**
     * @class Result
     * @brief 函数式错误处理的结果类型
     *
     * @tparam T 成功值的类型
     * @tparam E 错误值的类型
     *
     * 使用示例：
     * @code
     * Result<int, string> result = some_function();
     * if (result.is_ok()) {
     *     int value = result.value();
     * } else {
     *     string error = result.error();
     * }
     * @endcode
     */
    template<typename T, typename E>
    class Result {
    public:
        /**
         * @brief 创建成功结果
         * @param value 成功值
         * @return Result对象
         */
        static Result ok(T value) {
            return Result(std::move(value), true);
        }

        /**
         * @brief 创建错误结果
         * @param error 错误值
         * @return Result对象
         */
        static Result err(E error) {
            return Result(std::move(error), false);
        }

        /**
         * @brief 检查是否成功
         * @return 成功返回true，失败返回false
         */
        bool is_ok() const { return std::holds_alternative<T>(data_); }

        /**
         * @brief 检查是否失败
         * @return 失败返回true，成功返回false
         */
        bool is_err() const { return std::holds_alternative<E>(data_); }

        // 获取成功值的各种版本
        T& value()& { return std::get<T>(data_); }
        const T& value() const& { return std::get<T>(data_); }
        T&& value()&& { return std::move(std::get<T>(data_)); }

        // 获取错误值的各种版本
        E& error()& { return std::get<E>(data_); }
        const E& error() const& { return std::get<E>(data_); }
        E&& error()&& { return std::move(std::get<E>(data_)); }

        /**
         * @brief 获取成功值或默认值
         * @param default_value 默认值
         * @return 成功则返回值，失败则返回默认值
         */
        T value_or(T default_value) const& {
            return is_ok() ? value() : std::move(default_value);
        }

        T value_or(T default_value)&& {
            return is_ok() ? std::move(value()) : std::move(default_value);
        }

        /**
         * @brief 函数式map操作
         * @tparam F 转换函数类型
         * @param f 转换函数
         * @return 新的Result对象
         */
        template<typename F>
        auto map(F&& f) -> Result<decltype(f(std::declval<T>())), E> {
            using U = decltype(f(std::declval<T>()));
            if (is_ok()) {
                return Result<U, E>::ok(f(value()));
            }
            else {
                return Result<U, E>::err(error());
            }
        }

        /**
         * @brief 函数式and_then操作（Monadic bind）
         * @tparam F 返回Result的函数
         * @param f 转换函数
         * @return 新的Result对象
         */
        template<typename F>
        auto and_then(F&& f) -> decltype(f(std::declval<T>())) {
            if (is_ok()) {
                return f(value());
            }
            else {
                using ReturnType = decltype(f(std::declval<T>()));
                return ReturnType::err(error());
            }
        }

    private:
        // 私有构造函数
        explicit Result(T value, bool) : data_(std::move(value)) {}
        explicit Result(E error, bool) : data_(std::move(error)) {}

        std::variant<T, E> data_;
    };

} // namespace magnet::protocols

#endif // MAGNET_TYPES_H
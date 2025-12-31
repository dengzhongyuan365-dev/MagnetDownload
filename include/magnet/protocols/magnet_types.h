#pragma once

#include <string>
#include <optional>
#include <vector>
#include <array>
#include <variant>
#include <cstdint>

namespace magnet::protocols {

class InfoHash {
public:
    static constexpr size_t HASHSIZE = 20;
    using ByteArray = std::array<uint8_t, HASHSIZE>;

    InfoHash() = default;

    explicit InfoHash(const ByteArray& bytes) : data_(bytes) {}

    // 从十六进制字符串构造（40个字符）
    static std::optional<InfoHash> fromHex(const std::string& hex);

    // 从base32字符串构造（32个字符）
    static std::optional<InfoHash> fromBase32(const std::string& base32);
    
    // 转换为十六进制字符串
    std::string toHex() const;
    
    // 获取原始字节
    const ByteArray& bytes() const { return data_; }
    ByteArray& bytes() { return data_; }
    
    // 验证是否有效（不全为0）
    bool is_valid() const {
        return data_ != ByteArray{};
    }

private:
    ByteArray data_{};
};

struct MagnetInfo {
    std::optional<InfoHash> info_hash;

    // 显示名称
    std::string display_name;
    
    // Trackers 列表
    std::vector<std::string> trackers;

    // 文件字节大小
    std::optional<uint64_t> exact_length;

    // Web seed 列表
    std::vector<std::string> web_seeds;

    // .torrent 文件来源列表
    std::vector<std::string> exact_sources;

    // 关键词列表
    std::vector<std::string> keywords;

    // 原始的 URI
    std::string original_uri;

    bool is_valid() const {
        return info_hash.has_value() && info_hash->is_valid();
    }

    bool has_trackers() const {
        return !trackers.empty();
    }
};

enum class ParseError {
    INVALID_SCHEME,         // 不是 magnet: 协议
    MISSING_INFO_HASH,      // 缺少 xt 参数
    INVALID_INFO_HASH,      // InfoHash 格式错误
    INVALID_HEX_ENCODING,   // 十六进制编码错误
    INVALID_BASE32_ENCODING,// Base32 编码错误
    INVALID_URL_ENCODING,   // URL 编码错误
    INVALID_TRACKER_URL,    // Tracker URL 格式错误
    INVALID_PARAMETER,      // 参数格式错误
    EMPTY_URI               // 空的 URI
};

template<typename T, typename E>
class Result {
public:
    // 创建成功结果
    static Result ok(T value) {
        return Result(std::move(value), true);
    }

    // 创建错误结果
    static Result err(E error) {
        return Result(std::move(error), false);
    }

    // 检查是否成功
    bool is_ok() const { return std::holds_alternative<T>(data_); }
    bool is_err() const { return std::holds_alternative<E>(data_); }
    
    // 获取成功值（调用前应检查 is_ok()）
    T& value() & { return std::get<T>(data_); }
    const T& value() const & { return std::get<T>(data_); }
    T&& value() && { return std::move(std::get<T>(data_)); }

    // 获取错误值（调用前应检查 is_err()）
    E& error() & { return std::get<E>(data_); }
    const E& error() const & { return std::get<E>(data_); }
    E&& error() && { return std::move(std::get<E>(data_)); }

    // 获取默认值
    T value_or(T default_value) const & {
        return is_ok() ? value() : std::move(default_value);
    }
    
    T value_or(T default_value) && {
        return is_ok() ? std::move(value()) : std::move(default_value);
    }

    // 函数式操作：map
    template<typename F>
    auto map(F&& f) -> Result<decltype(f(std::declval<T>())), E> {
        using U = decltype(f(std::declval<T>()));
        if (is_ok()) {
            return Result<U, E>::ok(f(value()));
        } else {
            return Result<U, E>::err(error());
        }
    }

    // 函数式操作：and_then
    template<typename F>
    auto and_then(F&& f) -> decltype(f(std::declval<T>())) {
        if (is_ok()) {
            return f(value());
        } else {
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
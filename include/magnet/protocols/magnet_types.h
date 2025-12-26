#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <variant>
#include <cstdint>
#include <cstring>
#include <chrono>

namespace magnet {

// ========== InfoHash 类型 ==========

class InfoHash {
public:
    static constexpr size_t SIZE = 20;
    using ByteArray = std::array<uint8_t, SIZE>;
    
    InfoHash() = default;
    explicit InfoHash(const ByteArray& bytes) : data_(bytes) {}
    
    // 从十六进制字符串构造（40个字符）
    static std::optional<InfoHash> from_hex(std::string_view hex);
    
    // 从 Base32 字符串构造（32个字符）
    static std::optional<InfoHash> from_base32(std::string_view base32);
    
    // 转换为十六进制字符串
    std::string to_hex() const;
    
    // 获取原始字节
    const ByteArray& bytes() const { return data_; }
    ByteArray& bytes() { return data_; }
    
    // 验证是否有效（不全为0）
    bool is_valid() const {
        return data_ != ByteArray{};
    }
    
    // 比较操作
    bool operator==(const InfoHash& other) const {
        return data_ == other.data_;
    }
    
    bool operator!=(const InfoHash& other) const {
        return data_ != other.data_;
    }
    
    bool operator<(const InfoHash& other) const {
        return data_ < other.data_;
    }
    
    // 支持作为 unordered_map 的 key
    struct Hash {
        size_t operator()(const InfoHash& hash) const {
            size_t result = 0;
            std::memcpy(&result, hash.data_.data(), 
                       std::min(sizeof(size_t), hash.data_.size()));
            return result;
        }
    };
    
private:
    ByteArray data_{};
};


// ========== MagnetInfo 结构 ==========

struct MagnetInfo {
    // ========== 必需字段 ==========
    InfoHash info_hash;
    
    // ========== 可选字段 ==========
    std::optional<std::string> display_name;
    std::vector<std::string> trackers;
    std::optional<uint64_t> exact_length;
    std::vector<std::string> web_seeds;
    std::vector<std::string> exact_sources;
    std::vector<std::string> keywords;
    
    // ========== 元数据字段 ==========
    std::string original_uri;  // 原始 URI（用于日志、调试）
    std::chrono::system_clock::time_point parsed_at;  // 解析时间
    
    // ========== 辅助方法 ==========
    
    // 检查是否有效
    bool is_valid() const {
        return info_hash.is_valid();
    }
    
    // 获取显示名称（如果没有则返回 InfoHash）
    std::string get_display_name() const {
        if (display_name.has_value() && !display_name->empty()) {
            return *display_name;
        }
        return info_hash.to_hex();
    }
    
    // 是否有 Tracker
    bool has_trackers() const {
        return !trackers.empty();
    }
    
    // 是否有 Web seeds
    bool has_web_seeds() const {
        return !web_seeds.empty();
    }
    
    // 是否知道文件大小
    bool has_exact_length() const {
        return exact_length.has_value();
    }
    
    // 获取文件大小（带单位的字符串）
    std::string get_size_string() const;
};


// ========== 解析错误类型 ==========

enum class ParseError {
    INVALID_SCHEME,          // 不是 magnet: 协议
    MISSING_INFO_HASH,       // 缺少 xt 参数
    INVALID_INFO_HASH,       // InfoHash 格式错误
    INVALID_HEX_ENCODING,    // 十六进制编码错误
    INVALID_BASE32_ENCODING, // Base32 编码错误
    INVALID_URL_ENCODING,    // URL 编码错误
    INVALID_TRACKER_URL,     // Tracker URL 格式错误
    INVALID_LENGTH_VALUE,    // 文件大小格式错误
    EMPTY_URI                // 空的 URI
};

struct ParseErrorInfo {
    ParseError error;
    std::string message;
    size_t position;  // 错误发生的位置
    
    ParseErrorInfo(ParseError err, std::string msg, size_t pos = 0)
        : error(err), message(std::move(msg)), position(pos) {}
};


// ========== Result 类型 ==========

template<typename T, typename E>
class Result {
public:
    // 成功构造
    static Result ok(T value) {
        return Result(std::move(value));
    }
    
    // 失败构造
    static Result err(E error) {
        return Result(std::move(error));
    }
    
    // 检查是否成功
    bool is_ok() const { return std::holds_alternative<T>(data_); }
    bool is_err() const { return std::holds_alternative<E>(data_); }
    
    // 获取值（如果成功）
    T& value() & { return std::get<T>(data_); }
    const T& value() const & { return std::get<T>(data_); }
    T&& value() && { return std::get<T>(std::move(data_)); }
    
    // 获取错误（如果失败）
    E& error() & { return std::get<E>(data_); }
    const E& error() const & { return std::get<E>(data_); }
    E&& error() && { return std::get<E>(std::move(data_)); }
    
    // 获取值或默认值
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
    explicit Result(T value) : data_(std::move(value)) {}
    explicit Result(E error) : data_(std::move(error)) {}
    
    std::variant<T, E> data_;
};

// 使用别名
using MagnetParseResult = Result<MagnetInfo, ParseErrorInfo>;

} // namespace magnet

#include <string>
#include <optional>
#include <vector>
#include <array>
namespace magnet {

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
        private:
            ByteArray data_ {};
    };

    struct MagnetInfo {
        InfoHash info_hash;

        // 显示名称
        std::optional<std::string> display_name;
        // Trackers 列表
        std::vector<std::string> trackers_vec;

        // 文件字节大小

        std::optional<uint64_t> exact_length;

        // 文件sed列表
        std::vector<std::string> file_seeds_vec;

        //.torrent 文件来源列表
        std::vector<std::string> exact_sources_vec;

        // 关键词列表
        std::vector<std::string> keywords_vec;

        // 原始的 URI
        std::string original_uri;

        bool is_valid() const;

        bool has_trackers() const {
            return !trackers_vec.empty();
        }


    };

    enum class ENUM_PARSE_ERROR {
        INVALID_SCHEME, // 不是 magnet: 协议
        MISSING_INFO_HASH, // 缺少 xt 参数
        INVALID_INFO_HASH, // InfoHash 格式错误
        INVALID_HEX_ENCODING, // 十六进制编码错误
        INVALID_BASE32_ENCODING, // Base32 编码错误
        INVALID_URL_ENCODING, // URL 编码错误
        INVALID_TRACKER_URL, // Tracker URL 格式错误
        INVALID_LENGTH_VALUE, // 文件大小格式错误
        EMPTY_URI // 空的 URI
    };

    template<typename T, typename E>
    class ParseResult {
        public:
        // 创建成功结果
        static ParseResult ok(T value) {
            return ParseResult(std::move(value));
        }

        // 创建错误结果
        static ParseResult err(E error) {
            return ParseResult(std::move(error));
        }

        // 检查是否成功
        bool is_ok() const { return std::holds_alternative<T>(data_); }
        bool is_err() const { return std::holds_alternative<E>(data_); }    
        // 获取成功值（调用前应检查 is_ok()）
        T& value() &{return std::get<T>(data_); }
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
    explicit Result(T value) : data_(std::move(value)) {}
    explicit Result(E error) : data_(std::move(error)) {}
    
    std::variant<T, E> data_;
    };
};
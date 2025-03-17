
// magnet:?xt=urn:btih:6B46C2FCDD1AE65626FF9650065ADBE09A1D7CA8&dn=Example+File&tr=http://tracker.example.com/announce

// 磁力链接解析器
// 使用状态机的方式来实现

// 状态机的核心组件
/*
    1. 状态，解析器当前所处的状态
    2. 输入当前正在处理的字符
    3. 转换规则：基于当前的状态和输入字符确定下一个状态
    4. 动作：在状态转换的时候执行的操作
*/

enum class MagnetParserState 
{
    ParserStateBegin = -1,
    InitialState = 0,
    SchemeState = 1,
    QueryState = 2,
    KeyState = 3,
    EqualsState = 4,
    ValueState = 5,
    AmpState = 6,
    ErrorState = 7,
    ParserStateBegin,
};

#include <string>
#include <map>
#include <vector>
#include <functional>
#include <optional>
#include <variant>
#include <regex>
#include <algorithm>
#include <iostream>
#include <string_view>  // C++17特性，高效的字符串视图

/**
 * @brief 磁力链接解析器的状态枚举
 */
enum class ParserState {
    INIT,           // 初始状态
    SCHEME,         // 解析 "magnet:" 部分
    QUERY_START,    // 解析 "?" 字符
    KEY,            // 解析参数名
    EQUALS,         // 解析 "=" 字符
    VALUE,          // 解析参数值
    AMPERSAND,      // 解析 "&" 字符
    ERROR,          // 错误状态
    DONE            // 解析完成
};

/**
 * @brief 磁力链接解析器使用的状态机
 * 
 * 该类使用模板方法模式结合状态模式设计，将状态逻辑与解析器的具体实现分离
 */
class MagnetURIStateMachine {
public:
    /**
     * @brief 构造函数
     */
    MagnetURIStateMachine() : 
        state_(ParserState::INIT),
        is_valid_(false) {
        // 初始化状态转换表 - 使用C++11引入的lambda表达式结合C++17的std::invoke
        initialize_state_handlers();
    }
    
    /**
     * @brief 解析磁力链接
     * @param uri 要解析的磁力链接
     * @return 解析是否成功
     */
    bool parse(std::string_view uri) {
        // 使用string_view避免拷贝，提高性能
        reset();
        original_uri_ = std::string(uri);
        
        // 使用结构化绑定（C++17特性）简化错误处理模式
        for (const char c : uri) {
            auto [success, next_state] = handle_char(c);
            if (!success) {
                state_ = ParserState::ERROR;
                return false;
            }
            
            if (next_state.has_value()) {
                state_ = *next_state;
            }
            
            if (state_ == ParserState::ERROR || state_ == ParserState::DONE) {
                break;
            }
        }
        
        // 处理最后一个值
        if (state_ == ParserState::VALUE && !current_key_.empty()) {
            params_[current_key_].push_back(current_value_);
        }
        
        // 提取关键信息
        extract_info_hash();
        extract_display_name();
        extract_trackers();
        
        return is_valid_;
    }
    
    // 获取解析结果的访问器方法
    [[nodiscard]] std::string get_info_hash() const { return info_hash_; }
    [[nodiscard]] std::string get_display_name() const { return display_name_; }
    [[nodiscard]] const std::vector<std::string>& get_trackers() const { return trackers_; }
    [[nodiscard]] bool is_valid() const { return is_valid_; }
    
    // 用于调试和测试
    [[nodiscard]] ParserState get_current_state() const { return state_; }
    
private:
    // 当前状态
    ParserState state_;
    
    // 解析器上下文
    std::string original_uri_;
    std::map<std::string, std::vector<std::string>> params_;
    std::string current_key_;
    std::string current_value_;
    
    // 解析结果
    std::string info_hash_;
    std::string display_name_;
    std::vector<std::string> trackers_;
    bool is_valid_;
    
    // 用于状态转换的处理函数类型
    // 返回值为 <成功标志, 可选的下一个状态>
    using StateHandler = std::function<std::pair<bool, std::optional<ParserState>>(char)>;
    
    // 状态处理函数映射表
    std::map<ParserState, StateHandler> state_handlers_;
    
    /**
     * @brief 初始化状态处理函数
     * 使用函数映射表代替传统的switch语句，提高代码可维护性
     */
    void initialize_state_handlers() {
        // INIT 状态处理函数 - 期望 'm'
        state_handlers_[ParserState::INIT] = [this](char c) -> std::pair<bool, std::optional<ParserState>> {
            if (c == 'm') {
                return {true, ParserState::SCHEME};
            }
            return {false, std::nullopt};
        };
        
        // SCHEME 状态处理函数 - 期望匹配 "agnet:"
        state_handlers_[ParserState::SCHEME] = [this](char c) -> std::pair<bool, std::optional<ParserState>> {
            // 使用静态变量跟踪进度（作用域限定在lambda中）
            static const std::string_view expected = "agnet:";
            static size_t pos = 0;
            
            if (c == expected[pos]) {
                pos++;
                if (pos == expected.length()) {
                    pos = 0; // 重置静态变量
                    return {true, ParserState::QUERY_START};
                }
                return {true, std::nullopt};
            }
            
            // 匹配失败
            pos = 0; // 重置静态变量
            return {false, std::nullopt};
        };
        
        // QUERY_START 状态处理函数 - 期望 '?'
        state_handlers_[ParserState::QUERY_START] = [this](char c) -> std::pair<bool, std::optional<ParserState>> {
            if (c == '?') {
                return {true, ParserState::KEY};
            }
            return {false, std::nullopt};
        };
        
        // KEY 状态处理函数 - 收集参数名直到 '='
        state_handlers_[ParserState::KEY] = [this](char c) -> std::pair<bool, std::optional<ParserState>> {
            if (c == '=') {
                return {true, ParserState::VALUE};
            } else if (c == '&') {
                // 空值参数，如 "key&"
                params_[current_key_].push_back("");
                current_key_ = "";
                return {true, ParserState::KEY};
            } else {
                current_key_ += c;
                return {true, std::nullopt};
            }
        };
        
        // VALUE 状态处理函数 - 收集参数值直到 '&'
        state_handlers_[ParserState::VALUE] = [this](char c) -> std::pair<bool, std::optional<ParserState>> {
            if (c == '&') {
                params_[current_key_].push_back(current_value_);
                current_key_ = "";
                current_value_ = "";
                return {true, ParserState::KEY};
            } else {
                current_value_ += c;
                return {true, std::nullopt};
            }
        };
        
        // 为其他状态设置默认处理函数，返回错误
        state_handlers_[ParserState::ERROR] = [](char) -> std::pair<bool, std::optional<ParserState>> {
            return {false, std::nullopt};
        };
        
        state_handlers_[ParserState::DONE] = [](char) -> std::pair<bool, std::optional<ParserState>> {
            return {false, std::nullopt};
        };
    }
    
    /**
     * @brief 处理输入字符，根据当前状态执行相应逻辑
     * @param c 输入字符
     * @return <成功标志, 可选的下一个状态>
     */
    std::pair<bool, std::optional<ParserState>> handle_char(char c) {
        auto it = state_handlers_.find(state_);
        if (it != state_handlers_.end()) {
            return it->second(c);
        }
        return {false, std::nullopt};  // 未找到处理函数，返回错误
    }
    
    /**
     * @brief 重置解析器状态
     */
    void reset() {
        state_ = ParserState::INIT;
        params_.clear();
        current_key_.clear();
        current_value_.clear();
        info_hash_.clear();
        display_name_.clear();
        trackers_.clear();
        is_valid_ = false;
    }
    
    /**
     * @brief 从解析的参数中提取InfoHash
     * 使用正则表达式匹配urn:btih:格式的infohash
     */
    void extract_info_hash() {
        if (params_.find("xt") != params_.end() && !params_["xt"].empty()) {
            // 使用C++11引入的原始字符串字面量(raw string literal)，提高正则表达式可读性
            static const std::regex btih_regex(R"(urn:btih:([a-fA-F0-9]{40}))", std::regex::icase);
            
            for (const auto& xt : params_["xt"]) {
                std::smatch match;
                if (std::regex_search(xt, match, btih_regex) && match.size() > 1) {
                    // 提取并转换为大写
                    info_hash_ = match[1].str();
                    // 使用C++17引入的字符串视图和范围for循环，结合lambda表达式
                    std::transform(info_hash_.begin(), info_hash_.end(), info_hash_.begin(), 
                                 [](unsigned char c){ return std::toupper(c); });
                    is_valid_ = true;
                    break;
                }
            }
        }
    }
    
    /**
     * @brief 从解析的参数中提取显示名称
     */
    void extract_display_name() {
        if (params_.find("dn") != params_.end() && !params_["dn"].empty()) {
            display_name_ = url_decode(params_["dn"][0]);
        }
    }
    
    /**
     * @brief 从解析的参数中提取Tracker列表
     */
    void extract_trackers() {
        if (params_.find("tr") != params_.end()) {
            // 使用C++17的结构化绑定和范围for循环简化代码
            for (const auto& tr : params_["tr"]) {
                trackers_.push_back(url_decode(tr));
            }
        }
    }
    
    /**
     * @brief URL解码
     * @param input 要解码的字符串
     * @return 解码后的字符串
     */
    static std::string url_decode(const std::string& input) {
        // 使用C++17的[[nodiscard]]属性标记函数，表明返回值不应被忽略
        std::string result;
        result.reserve(input.size());  // 预分配内存，避免多次重新分配
        
        for (size_t i = 0; i < input.size(); ++i) {
            if (input[i] == '%' && i + 2 < input.size()) {
                // 使用std::from_chars（C++17特性）替代流操作，但这里示例仍使用传统方式
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
};
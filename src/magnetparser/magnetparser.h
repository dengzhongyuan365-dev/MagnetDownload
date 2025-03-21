// magnet:?xt=urn:btih:6B46C2FCDD1AE65626FF9650065ADBE09A1D7CA8&dn=Example+File&tr=http://tracker.example.com/announce
// 磁力连接解析器
// 对磁力连接进行解析，使用状态机的方式来进行借些各个参数
// 状态机枚举的状态转换
#include <string> 
#include <map>
#include <vector>
#include <functional>
#include <optional>
#include <charconv>  // 用于std::from_chars
#include <string_view>  // 用于高效字符串处理

#define private_function private
#define public_function public
#define private_variable private
#define public_variable public

enum class MagnetParserState {
    INIT,   // 初始
    SCHEME, // 协议
    QUERY,  // ？
    KEY,    // 参数名
    EQUALS, // =
    VALUE,  // 参数值
    AMPERSAND, // &
    ERROR,    // 错误
    DONE,     // 完成
};



class MagnetParserStateMachine 
{

public_function:

    explicit MagnetParserStateMachine();

    bool parse(std::string_view uri);

    [[nodiscard]] std::string get_info_hash() const {return info_hash_;}
    [[nodiscard]] std::string get_display_name() const {return display_name_;}
    [[nodiscard]] const std::vector<std::string>& get_trackers() const {return trackers_;}
    [[nodiscard]] bool is_valid() const {return is_valid_;}
    [[nodiscard]] MagnetParserState get_current_state() const {return current_state_;} 

private_function:

    void initializeStateHandlers();
    std::pair<bool, std::optional<MagnetParserState>> handle_char(char c);
    void reset();
    void extract_info_hash();
    void extract_display_name();
    void extract_trackers();

    [[nodiscard]] static std::string url_decode(const std::string& input);

private_variable:
    MagnetParserState current_state_;
    // 解析器上下文
    std::string original_uri_;
    std::map<std::string, std::vector<std::string>> params_;
    std::string current_key_;
    std::string current_value_;


    //解析结果
    std::string info_hash_;
    std::string display_name_;
    std::vector<std::string> trackers_;
    bool is_valid_; 


    // 用于状态转换的处理函数类型
    // 返回值为 <成功标志, 可选的下一个状态>
    using StateHandler = std::function<std::pair<bool, std::optional<MagnetParserState>>(char)>;
    // 状态处理函数映射表
    std::map<MagnetParserState, StateHandler> state_handlers_;

};



#include "magnetparser.h"
#include <regex>

MagnetParserStateMachine::MagnetParserStateMachine()
: current_state_(MagnetParserState::INIT),
  is_valid_(false)
{
    initializeStateHandlers();
}

bool MagnetParserStateMachine::parse(std::string_view uri)
{
    reset();
    original_uri_ = std::string(uri);

    for(const char c : uri)
    {
        // 结构化绑定
        auto [success, next_state] = handle_char(c);

        if(!success)
        {
            current_state_ = MagnetParserState::ERROR;
            return false;
        }

        if(next_state.has_value())
        {
            current_state_ = *next_state;
        }

        if(current_state_ == MagnetParserState::ERROR || current_state_ == MagnetParserState::DONE  )
        {
            break;
        }
    }

    if(current_state_ == MagnetParserState::VALUE && !current_key_.empty())
    {
        params_[current_key_].push_back(current_value_);
    }

    extract_info_hash();
    extract_display_name();
    extract_trackers();

    return is_valid_;
}

std::pair<bool, std::optional<MagnetParserState>> MagnetParserStateMachine::handle_char(char c)
{
    auto it = state_handlers_.find(current_state_);
    if(it != state_handlers_.end())
    {
        return it->second(c);
    }
    return {false, std::nullopt};
}


void MagnetParserStateMachine::initializeStateHandlers() 
{
    // 初始化的时候的函数
    auto initHandler = [this](char c)->std::pair<bool, std::optional<MagnetParserState>>
    {
       if(c == 'm')
       {
           return {true, MagnetParserState::SCHEME};
       }
        return {false, std::nullopt};
    };
    

    auto schemeHander = [this](char c)->std::pair<bool, std::optional<MagnetParserState>>
    {
        static const std::string_view expected = "agnet:";
        static size_t pos = 0;

        if(c == expected[pos])
        {
            pos++;
            if(pos == expected.length())
            {
                pos = 0;
                return {true, MagnetParserState::QUERY};
            }
            return {true, std::nullopt};
        }

        pos = 0;
        return {false, std::nullopt};
    };

    auto queryHandler = [this](char c)->std::pair<bool,std::optional<MagnetParserState>>
    {
        if(c=='?')
        {
            return {true, MagnetParserState::KEY};
        }
        return {false, std::nullopt};
    };
    
    // 参数匹配
    auto keyHandler = [this](char c)->std::pair<bool,std::optional<MagnetParserState>>
    {
        // 收集参数名直到 =
        if (c == '=')
        {
            return {true, MagnetParserState::VALUE};
        }
        else if(c=='&')
        {
            params_[current_key_].push_back("");
            current_key_ = "";
            return {true, MagnetParserState::KEY};
        }
        else
        {
            current_key_ += c;
            return {true, std::nullopt};
        }
    };

    // 收集参数值直到 &
    auto valueHandler = [this](char c)->std::pair<bool,std::optional<MagnetParserState>>
    {
        if(c=='&')
        {
            params_[current_key_].push_back(current_value_);
            current_key_ = "";
            current_value_ = "";
            return {true, MagnetParserState::KEY};
        }
        else
        {
            current_value_ += c;
            return {true, std::nullopt};
        }
    };

    auto errorHandler = [](char)->std::pair<bool,std::optional<MagnetParserState>>
    {
        return {false, std::nullopt};
    };

    auto doneHandler = [](char)->std::pair<bool,std::optional<MagnetParserState>>
    {
        return {false, std::nullopt};
    };

    state_handlers_[MagnetParserState::INIT] = {initHandler};
    state_handlers_[MagnetParserState::SCHEME] = {schemeHander};
    state_handlers_[MagnetParserState::QUERY] = {queryHandler};
    state_handlers_[MagnetParserState::KEY] = {keyHandler};
    state_handlers_[MagnetParserState::VALUE] = {valueHandler};
    state_handlers_[MagnetParserState::ERROR] = {errorHandler};
    state_handlers_[MagnetParserState::DONE] = {doneHandler};
}

 void MagnetParserStateMachine::reset()
 {
    current_state_ = MagnetParserState::INIT;
    params_.clear();
    current_key_.clear();
    current_value_.clear();
    info_hash_.clear();
    display_name_.clear();
    trackers_.clear();
    is_valid_ = false;
 }

 void MagnetParserStateMachine::extract_info_hash()
 {

    if(params_.find("xt") != params_.end() && !params_["xt"].empty())
    {
        static const std::regex btih_regex(R"(urn:btih:([a-fA-F0-9]{40}))", std::regex::icase);

        for(const auto& xt : params_["xt"])
        {
            // 用于存储匹配结果
            std::smatch match;

            // 在字符串中搜索匹配模式
            if(std::regex_search(xt, match, btih_regex) && match.size() > 1)
            {
                info_hash_ = match[1].str();
                std::transform(info_hash_.begin(), info_hash_.end(), info_hash_.begin(), 
                [](unsigned char c){return std::toupper(c);});
                is_valid_ = true;
                break;
            }

        }

    }

 }
 
 void MagnetParserStateMachine::extract_display_name()
 {

    if(params_.find("dn") != params_.end() && !params_["dn"].empty())
    {
        display_name_ = url_decode(params_["dn"][0]);
    }

 }

 void MagnetParserStateMachine::extract_trackers()
 {
    if(params_.find("tr") != params_.end())
    {
        for(const auto& tr : params_["tr"])
        {
            trackers_.push_back(url_decode(tr));
        }
    }
 }

static std::string url_decode(const std::string& input)
{
    std::string result;
    result.reserve(input.size());
    
    for(size_t i = 0; i < input.size(); ++i)
    {
        if(input[i] == '%' && i + 2 < input.size())
        {
            int value;
            std::istringstream is(input.substr(i + 1, 2));
            if(is >> std::hex >> value)
            {
                result += static_cast<char>(value);
                i += 2;
            }
            else        
            {
                result += input[i];
            }
        }
        else if(input[i] == '+')
        {
            result += ' ';
        }
        else
        {
            result += input[i];
        }
    }
    return result;
}   

 // 改进版的URL解码函数
static std::string improved_url_decode(const std::string& input) {
    std::string result;
    result.reserve(input.size()); // 预分配空间以提高性能
    
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '%' && i + 2 < input.size()) {
            // 使用C++17的std::from_chars替代流操作
            int value = 0;
            const char* begin = input.data() + i + 1;
            const char* end = begin + 2;
            auto [ptr, ec] = std::from_chars(begin, end, value, 16);
            
            if (ec == std::errc() && ptr == end) {
                // 解码成功
                
                // 检查是否是UTF-8多字节字符的开始字节
                if ((value & 0xE0) == 0xE0 && i + 5 < input.size() && 
                    input[i+3] == '%' && input[i+6] == '%') {
                    // 可能是UTF-8三字节序列 (1110xxxx 10xxxxxx 10xxxxxx)
                    std::array<unsigned char, 3> bytes;
                    bytes[0] = static_cast<unsigned char>(value);
                    
                    // 解析第二个字节
                    int value2 = 0;
                    begin = input.data() + i + 4;
                    end = begin + 2;
                    auto [ptr2, ec2] = std::from_chars(begin, end, value2, 16);
                    
                    // 解析第三个字节
                    int value3 = 0;
                    begin = input.data() + i + 7;
                    end = begin + 2;
                    auto [ptr3, ec3] = std::from_chars(begin, end, value3, 16);
                    
                    if (ec2 == std::errc() && ptr2 == end && 
                        ec3 == std::errc() && ptr3 == end &&
                        (value2 & 0xC0) == 0x80 && (value3 & 0xC0) == 0x80) {
                        // 确认是UTF-8三字节序列 (第2和第3字节都是10xxxxxx形式)
                        bytes[1] = static_cast<unsigned char>(value2);
                        bytes[2] = static_cast<unsigned char>(value3);
                        
                        // 添加整个UTF-8序列
                        result.push_back(static_cast<char>(bytes[0]));
                        result.push_back(static_cast<char>(bytes[1]));
                        result.push_back(static_cast<char>(bytes[2]));
                        
                        // 跳过已处理的字节
                        i += 8; // %XX%XX%XX的总长度是9，循环中会再+1
                        continue;
                    }
                }
                // 检查是否是UTF-8二字节字符的开始
                else if ((value & 0xC0) == 0xC0 && i + 2 < input.size() && input[i+3] == '%') {
                    // 可能是UTF-8二字节序列 (110xxxxx 10xxxxxx)
                    std::array<unsigned char, 2> bytes;
                    bytes[0] = static_cast<unsigned char>(value);
                    
                    // 解析第二个字节
                    int value2 = 0;
                    begin = input.data() + i + 4;
                    end = begin + 2;
                    auto [ptr2, ec2] = std::from_chars(begin, end, value2, 16);
                    
                    if (ec2 == std::errc() && ptr2 == end && (value2 & 0xC0) == 0x80) {
                        // 确认是UTF-8二字节序列 (第2字节是10xxxxxx形式)
                        bytes[1] = static_cast<unsigned char>(value2);
                        
                        // 添加整个UTF-8序列
                        result.push_back(static_cast<char>(bytes[0]));
                        result.push_back(static_cast<char>(bytes[1]));
                        
                        // 跳过已处理的字节
                        i += 5; // %XX%XX的总长度是6，循环中会再+1
                        continue;
                    }
                }
                
                // 单个字节情况
                result.push_back(static_cast<char>(value));
                i += 2;
            } else {
                // 解码失败，保留原始字符
                result.push_back(input[i]);
            }
        } else if (input[i] == '+') {
            result.push_back(' ');
        } else {
            result.push_back(input[i]);
        }
    }
    
    return result;
}
 
 
 
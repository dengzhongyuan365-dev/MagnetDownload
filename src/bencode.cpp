#include "bencode.h"
#include <sstream>
#include <stdexcept>

namespace bencode {

std::shared_ptr<BencodeValue> BencodeParser::parse(const std::string& data) {
    if (data.empty()) {
        throw std::runtime_error("Empty data");
    }
    
    size_t pos = 0;
    char type = data[pos];
    
    if (type == 'i') {
        return parseInteger(data, pos);
    } else if (type >= '0' && type <= '9') {
        return parseString(data, pos);
    } else if (type == 'l') {
        return parseList(data, pos);
    } else if (type == 'd') {
        return parseDictionary(data, pos);
    } else {
        throw std::runtime_error("Invalid bencode data");
    }
}

std::shared_ptr<BencodeValue> BencodeParser::parseInteger(const std::string& data, size_t& pos) {
    // 跳过'i'
    pos++;
    
    // 查找结束标记'e'
    size_t end = data.find('e', pos);
    if (end == std::string::npos) {
        throw std::runtime_error("Invalid integer format");
    }
    
    // 提取整数部分
    std::string intStr = data.substr(pos, end - pos);
    pos = end + 1; // 更新位置，跳过'e'
    
    try {
        Integer value = std::stoll(intStr);
        return std::make_shared<BencodeValue>(value);
    } catch (const std::exception& e) {
        throw std::runtime_error("Invalid integer: " + intStr);
    }
}

std::shared_ptr<BencodeValue> BencodeParser::parseString(const std::string& data, size_t& pos) {
    // 查找':'
    size_t colon = data.find(':', pos);
    if (colon == std::string::npos) {
        throw std::runtime_error("Invalid string format");
    }
    
    // 提取长度
    std::string lenStr = data.substr(pos, colon - pos);
    pos = colon + 1; // 更新位置，跳过':'
    
    try {
        size_t len = std::stoull(lenStr);
        
        // 检查是否有足够的数据
        if (pos + len > data.size()) {
            throw std::runtime_error("String length exceeds data size");
        }
        
        // 提取字符串
        std::string value = data.substr(pos, len);
        pos += len; // 更新位置
        
        return std::make_shared<BencodeValue>(value);
    } catch (const std::exception& e) {
        throw std::runtime_error("Invalid string length: " + lenStr);
    }
}

std::shared_ptr<BencodeValue> BencodeParser::parseList(const std::string& data, size_t& pos) {
    // 跳过'l'
    pos++;
    
    List list;
    
    // 解析列表元素直到遇到'e'
    while (pos < data.size() && data[pos] != 'e') {
        auto item = parse(data.substr(pos));
        
        // 计算处理后的新位置
        size_t advance = 0;
        if (data[pos] == 'i') {
            // 整数: i[数字]e
            size_t end = data.find('e', pos + 1);
            if (end == std::string::npos) {
                throw std::runtime_error("Invalid integer in list");
            }
            advance = end + 1 - pos;
        } else if (data[pos] >= '0' && data[pos] <= '9') {
            // 字符串: [长度]:[内容]
            size_t colon = data.find(':', pos);
            if (colon == std::string::npos) {
                throw std::runtime_error("Invalid string in list");
            }
            std::string lenStr = data.substr(pos, colon - pos);
            size_t len = std::stoull(lenStr);
            advance = (colon - pos) + 1 + len;
        } else if (data[pos] == 'l') {
            // 列表: l[内容]e
            size_t depth = 1;
            size_t i = pos + 1;
            while (depth > 0 && i < data.size()) {
                if (data[i] == 'l') depth++;
                else if (data[i] == 'e') depth--;
                i++;
            }
            if (depth != 0) {
                throw std::runtime_error("Unterminated list");
            }
            advance = i - pos;
        } else if (data[pos] == 'd') {
            // 字典: d[内容]e
            size_t depth = 1;
            size_t i = pos + 1;
            while (depth > 0 && i < data.size()) {
                if (data[i] == 'd') depth++;
                else if (data[i] == 'e') depth--;
                i++;
            }
            if (depth != 0) {
                throw std::runtime_error("Unterminated dictionary");
            }
            advance = i - pos;
        } else {
            throw std::runtime_error("Invalid list item");
        }
        
        pos += advance;
        list.push_back(item);
    }
    
    // 跳过'e'
    if (pos < data.size() && data[pos] == 'e') {
        pos++;
    } else {
        throw std::runtime_error("Unterminated list");
    }
    
    return std::make_shared<BencodeValue>(list);
}

std::shared_ptr<BencodeValue> BencodeParser::parseDictionary(const std::string& data, size_t& pos) {
    // 跳过'd'
    pos++;
    
    Dictionary dict;
    
    // 解析字典元素直到遇到'e'
    while (pos < data.size() && data[pos] != 'e') {
        // 键必须是字符串
        if (!(data[pos] >= '0' && data[pos] <= '9')) {
            throw std::runtime_error("Dictionary key must be a string");
        }
        
        // 解析键
        auto keyValue = parseString(data, pos);
        std::string key = keyValue->getString();
        
        // 解析值
        auto value = parse(data.substr(pos));
        
        // 计算处理后的新位置
        size_t advance = 0;
        if (data[pos] == 'i') {
            // 整数: i[数字]e
            size_t end = data.find('e', pos + 1);
            if (end == std::string::npos) {
                throw std::runtime_error("Invalid integer in dictionary");
            }
            advance = end + 1 - pos;
        } else if (data[pos] >= '0' && data[pos] <= '9') {
            // 字符串: [长度]:[内容]
            size_t colon = data.find(':', pos);
            if (colon == std::string::npos) {
                throw std::runtime_error("Invalid string in dictionary");
            }
            std::string lenStr = data.substr(pos, colon - pos);
            size_t len = std::stoull(lenStr);
            advance = (colon - pos) + 1 + len;
        } else if (data[pos] == 'l') {
            // 列表: l[内容]e
            size_t depth = 1;
            size_t i = pos + 1;
            while (depth > 0 && i < data.size()) {
                if (data[i] == 'l') depth++;
                else if (data[i] == 'e') depth--;
                i++;
            }
            if (depth != 0) {
                throw std::runtime_error("Unterminated list");
            }
            advance = i - pos;
        } else if (data[pos] == 'd') {
            // 字典: d[内容]e
            size_t depth = 1;
            size_t i = pos + 1;
            while (depth > 0 && i < data.size()) {
                if (data[i] == 'd') depth++;
                else if (data[i] == 'e') depth--;
                i++;
            }
            if (depth != 0) {
                throw std::runtime_error("Unterminated dictionary");
            }
            advance = i - pos;
        } else {
            throw std::runtime_error("Invalid dictionary value");
        }
        
        pos += advance;
        dict[key] = value;
    }
    
    // 跳过'e'
    if (pos < data.size() && data[pos] == 'e') {
        pos++;
    } else {
        throw std::runtime_error("Unterminated dictionary");
    }
    
    return std::make_shared<BencodeValue>(dict);
}

std::string BencodeParser::encode(const std::shared_ptr<BencodeValue>& value) {
    if (!value) {
        throw std::runtime_error("Null value");
    }
    
    switch (value->getType()) {
        case BencodeValue::Type::INTEGER:
            return encodeInteger(value->getInteger());
        case BencodeValue::Type::STRING:
            return encodeString(value->getString());
        case BencodeValue::Type::LIST:
            return encodeList(value->getList());
        case BencodeValue::Type::DICTIONARY:
            return encodeDictionary(value->getDictionary());
        default:
            throw std::runtime_error("Unknown type");
    }
}

std::string BencodeParser::encodeInteger(Integer value) {
    return "i" + std::to_string(value) + "e";
}

std::string BencodeParser::encodeString(const String& value) {
    return std::to_string(value.size()) + ":" + value;
}

std::string BencodeParser::encodeList(const List& value) {
    std::string result = "l";
    for (const auto& item : value) {
        result += encode(item);
    }
    result += "e";
    return result;
}

std::string BencodeParser::encodeDictionary(const Dictionary& value) {
    std::string result = "d";
    for (const auto& [key, val] : value) {
        result += encodeString(key);
        result += encode(val);
    }
    result += "e";
    return result;
}

} // namespace bencode 
#pragma once

#include <string>
#include <map>
#include <vector>
#include <variant>
#include <memory>
#include <stdexcept>

namespace bencode {

// 前向声明
class BencodeValue;

// Bencode值类型
using Integer = int64_t;
using String = std::string;
using List = std::vector<std::shared_ptr<BencodeValue>>;
using Dictionary = std::map<std::string, std::shared_ptr<BencodeValue>>;

// Bencode值类
class BencodeValue {
public:
    enum class Type {
        INTEGER,
        STRING,
        LIST,
        DICTIONARY
    };

    BencodeValue() : type_(Type::STRING), value_(String()) {}
    explicit BencodeValue(Integer value) : type_(Type::INTEGER), value_(value) {}
    explicit BencodeValue(const String& value) : type_(Type::STRING), value_(value) {}
    explicit BencodeValue(List value) : type_(Type::LIST), value_(std::move(value)) {}
    explicit BencodeValue(Dictionary value) : type_(Type::DICTIONARY), value_(std::move(value)) {}

    Type getType() const { return type_; }

    // 获取Integer值
    Integer getInteger() const {
        if (type_ != Type::INTEGER) throw std::runtime_error("Not an integer");
        return std::get<Integer>(value_);
    }

    // 获取String值
    const String& getString() const {
        if (type_ != Type::STRING) throw std::runtime_error("Not a string");
        return std::get<String>(value_);
    }

    // 获取List值
    const List& getList() const {
        if (type_ != Type::LIST) throw std::runtime_error("Not a list");
        return std::get<List>(value_);
    }

    // 获取Dictionary值
    const Dictionary& getDictionary() const {
        if (type_ != Type::DICTIONARY) throw std::runtime_error("Not a dictionary");
        return std::get<Dictionary>(value_);
    }

private:
    Type type_;
    std::variant<Integer, String, List, Dictionary> value_;
};

// Bencode解析器类
class BencodeParser {
public:
    BencodeParser() = default;

    // 解析Bencode数据
    std::shared_ptr<BencodeValue> parse(const std::string& data);
    
    // 编码Bencode数据
    std::string encode(const std::shared_ptr<BencodeValue>& value);

private:
    // 解析Integer
    std::shared_ptr<BencodeValue> parseInteger(const std::string& data, size_t& pos);
    
    // 解析String
    std::shared_ptr<BencodeValue> parseString(const std::string& data, size_t& pos);
    
    // 解析List
    std::shared_ptr<BencodeValue> parseList(const std::string& data, size_t& pos);
    
    // 解析Dictionary
    std::shared_ptr<BencodeValue> parseDictionary(const std::string& data, size_t& pos);
    
    // 编码Integer
    std::string encodeInteger(Integer value);
    
    // 编码String
    std::string encodeString(const String& value);
    
    // 编码List
    std::string encodeList(const List& value);
    
    // 编码Dictionary
    std::string encodeDictionary(const Dictionary& value);
};

} // namespace bencode 
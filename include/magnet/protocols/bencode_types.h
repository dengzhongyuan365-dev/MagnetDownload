#pragma once

#include <string>
#include <vector>
#include <map>
#include <variant>
#include <cstdint>
#include <memory>
#include <optional>

namespace magnet::protocols {

// 前向声明
class BencodeValue;

/**
 * @brief Bencode 整数类型
 */
using BencodeInt = int64_t;

/**
 * @brief Bencode 字符串类型
 * 
 * 注意：可能包含二进制数据，不一定是有效的 UTF-8
 */
using BencodeString = std::string;

/**
 * @brief Bencode 列表类型
 */
using BencodeList = std::vector<BencodeValue>;

/**
 * @brief Bencode 字典类型
 * 
 * 注意：key 必须按字典序排序
 */
using BencodeDict = std::map<std::string, BencodeValue>;

/**
 * @brief Bencode 值类型
 * 
 * 可以是以下四种类型之一：
 * - BencodeInt（整数）
 * - BencodeString（字符串）
 * - BencodeList（列表）
 * - BencodeDict（字典）
 * 
 * 使用示例：
 * @code
 * // 创建整数
 * BencodeValue value1(42);
 * 
 * // 创建字符串
 * BencodeValue value2("hello");
 * 
 * // 创建列表
 * BencodeList list;
 * list.push_back(BencodeValue(1));
 * list.push_back(BencodeValue("test"));
 * BencodeValue value3(list);
 * 
 * // 创建字典
 * BencodeDict dict;
 * dict["key"] = BencodeValue("value");
 * BencodeValue value4(dict);
 * @endcode
 */

 /*
 * @brief Bencode 值类型
 * 
 */

 class BencodeValue
 {
    public:
        BencodeValue() = default;
        BencodeValue(BencodeInt i);
        BencodeValue(const BencodeString& s);
        BencodeValue(const char* s);
        BencodeValue(const BencodeList& list);
        BencodeValue(const BencodeDict& dict);

        bool isInt() const;
        bool isString() const;
        bool isList() const;
        bool isDict() const;

        // 值访问
        BencodeInt asInt() const;
        const BencodeString& asString() const;
        const BencodeList& asList() const;
        const BencodeDict& asDict() const;

        // 可变访问
        BencodeString& asString();
        BencodeDict& asDict();
        BencodeList& asList();

        // 字典便捷访问
        BencodeValue& operator[](const std::string& key);
        const BencodeValue& operator[](const std::string& key) const;
        
        // 安全访问
        std::optional<BencodeInt> getInt() const;
        std::optional<std::string> getString() const;

    private:
        std::variant<std::monostate, BencodeInt, BencodeString, BencodeList, BencodeDict> data_;
 };

} // namespace magnet::protocols

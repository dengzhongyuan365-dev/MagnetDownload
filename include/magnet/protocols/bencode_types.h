#pragma once

#include <string>
#include <vector>
#include <map>
#include <variant>
#include <cstdint>
#include <memory>

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
class BencodeValue {
public:
    /**
     * @brief Bencode 值的类型枚举
     */
    enum class Type {
        INT,        // 整数
        STRING,     // 字符串
        LIST,       // 列表
        DICT        // 字典
    };
    
    // 构造函数
    BencodeValue();
    BencodeValue(BencodeInt value);
    BencodeValue(const char* value);
    BencodeValue(BencodeString value);
    BencodeValue(BencodeList value);
    BencodeValue(BencodeDict value);
    
    // 拷贝和移动
    BencodeValue(const BencodeValue& other);
    BencodeValue(BencodeValue&& other) noexcept;
    BencodeValue& operator=(const BencodeValue& other);
    BencodeValue& operator=(BencodeValue&& other) noexcept;
    
    ~BencodeValue() = default;
    
    /**
     * @brief 获取值的类型
     * @return 类型枚举
     */
    Type type() const;
    
    /**
     * @brief 检查是否是整数类型
     */
    bool is_int() const { return type() == Type::INT; }
    
    /**
     * @brief 检查是否是字符串类型
     */
    bool is_string() const { return type() == Type::STRING; }
    
    /**
     * @brief 检查是否是列表类型
     */
    bool is_list() const { return type() == Type::LIST; }
    
    /**
     * @brief 检查是否是字典类型
     */
    bool is_dict() const { return type() == Type::DICT; }
    
    /**
     * @brief 获取整数值
     * @return 整数值
     * @throw std::bad_variant_access 如果类型不是整数
     */
    BencodeInt as_int() const;
    
    /**
     * @brief 获取字符串值
     * @return 字符串引用
     * @throw std::bad_variant_access 如果类型不是字符串
     */
    const BencodeString& as_string() const;
    
    /**
     * @brief 获取列表值
     * @return 列表引用
     * @throw std::bad_variant_access 如果类型不是列表
     */
    const BencodeList& as_list() const;
    BencodeList& as_list();
    
    /**
     * @brief 获取字典值
     * @return 字典引用
     * @throw std::bad_variant_access 如果类型不是字典
     */
    const BencodeDict& as_dict() const;
    BencodeDict& as_dict();

private:
    std::variant<BencodeInt, BencodeString, BencodeList, BencodeDict> data_;
};

} // namespace magnet::protocols

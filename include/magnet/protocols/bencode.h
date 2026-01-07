#pragma once

#include "bencode_types.h"
#include <string_view>
#include <optional>

namespace magnet::protocols {

/**
 * @brief Bencode 编解码器
 * 
 * 提供 Bencode 格式的编码和解码功能
 * 
 * Bencode 是 BitTorrent 协议使用的编码格式，支持：
 * - 整数（Integer）：i42e
 * - 字符串（String）：4:spam
 * - 列表（List）：l4:spam4:eggse
 * - 字典（Dictionary）：d3:cow3:mooe
 * 
 * 使用示例：
 * @code
 * // 编码
 * BencodeDict dict;
 * dict["name"] = BencodeValue("test");
 * dict["size"] = BencodeValue(100);
 * std::string encoded = Bencode::encode(BencodeValue(dict));
 * // 结果：d4:name4:test4:sizei100ee
 * 
 * // 解码
 * auto result = Bencode::decode(encoded);
 * if (result) {
 *     const auto& value = result.value();
 *     if (value.is_dict()) {
 *         const auto& dict = value.as_dict();
 *         std::cout << dict.at("name").as_string() << std::endl;
 *     }
 * }
 * @endcode
 */
class Bencode {
public:
    /**
     * @brief 编码 Bencode 值为字符串
     * @param value 要编码的值
     * @return 编码后的字符串
     * 
     * 注意：
     * - 字典的 key 会自动按字典序排序
     * - 支持嵌套的复杂结构
     * - 返回的字符串可能包含二进制数据
     */
    static std::string encode(const BencodeValue& value);
    
    /**
     * @brief 解码 Bencode 字符串为值
     * @param data 要解码的数据
     * @return 解码后的值，失败返回 nullopt
     * 
     * 注意：
     * - 如果格式错误会返回 nullopt
     * - 支持解析部分数据（会忽略末尾多余的数据）
     */
    static std::optional<BencodeValue> decode(std::string_view data);
    
    /**
     * @brief 解码 Bencode 字符串为值（带位置信息）
     * @param data 要解码的数据
     * @param pos 输出参数，解码结束的位置
     * @return 解码后的值，失败返回 nullopt
     * 
     * 用途：
     * - 解析多个连续的 Bencode 值
     * - 获取解析位置用于错误报告
     */
    static std::optional<BencodeValue> decode(std::string_view data, size_t& pos);

    // 便捷编码方法
    static std::string encode(BencodeInt i);
    static std::string encode(const std::string& s);

private:
   static void encodeValue(const BencodeValue& value, std::string&out);
   static void encodeInt(BencodeInt i, std::string& out);
   static void encodeString(const std::string& s, std::string& out);
   static void encodeList(const BencodeList& list, std::string& out);
   static void encodeDict(const BencodeDict& dict, std::string& out);

   class Decoder
   {
        public: 
            explicit Decoder(std::string_view data);
            std::optional<BencodeValue> decode();

        private:
            std::optional<BencodeValue> decodeValue();
            std::optional<BencodeInt> decodeInt();
            std::optional<BencodeString> decodeString();
            std::optional<BencodeList> decodeList();
            std::optional<BencodeDict> decodeDict();

            bool hasMore() const;
            char peek() const;
            char consume();
            bool expect(char c);

            std::string_view data_;
            size_t pos_ = 0;
   };
};

} // namespace magnet::protocols

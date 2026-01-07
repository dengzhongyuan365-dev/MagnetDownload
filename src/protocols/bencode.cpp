#include "../../include/magnet/protocols/bencode.h"

namespace magnet::protocols {

namespace {
    // Bencode 格式常量
    constexpr char kIntStart    = 'i';  // 整数开始标记
    constexpr char kListStart   = 'l';  // 列表开始标记
    constexpr char kDictStart   = 'd';  // 字典开始标记
    constexpr char kEnd         = 'e';  // 结束标记（整数、列表、字典）
    constexpr char kStringSep   = ':';  // 字符串长度与内容分隔符
    constexpr char kNegative    = '-';  // 负号
    constexpr char kZero        = '0';  // 零字符
} // anonymous namespace

    BencodeValue::BencodeValue(BencodeInt i) : data_(i) {}
    BencodeValue::BencodeValue(const BencodeString& s): data_(s) {}
    BencodeValue::BencodeValue(const BencodeList& list): data_(list) {}
    BencodeValue::BencodeValue(const BencodeDict& dict): data_(dict) {}


    bool BencodeValue::isInt() const {
        return std::holds_alternative<BencodeInt>(data_);
    }

    bool BencodeValue::isString() const {
        return std::holds_alternative<BencodeString> (data_);
    }

    bool BencodeValue::isList() const {
        return std::holds_alternative<BencodeList>(data_);
    }

    bool BencodeValue::isDict() const {
        return std::holds_alternative<BencodeDict> (data_);
    }

    BencodeInt BencodeValue::asInt() const {
        return std::get<BencodeInt>(data_);
    }

    const  BencodeString& BencodeValue::asString() const {
        return std::get<BencodeString> (data_);
    }

    const BencodeList& BencodeValue::asList() const {
        return std::get<BencodeList> (data_);
    }

    const BencodeDict& BencodeValue::asDict() const {
        return std::get<BencodeDict> (data_);
    }

    BencodeString& BencodeValue::asString() {
        return std::get<BencodeString> (data_);
    }

    BencodeList& BencodeValue::asList() {
        return std::get<BencodeList> (data_);
    }

    BencodeDict& BencodeValue::asDict() {
        return std::get<BencodeDict> (data_);
    }

    BencodeValue& BencodeValue::operator[](const std::string& key) {
        if (!isDict())
            data_ = BencodeDict{};
        return std::get<BencodeDict>(data_)[key];
    }

    const BencodeValue& BencodeValue::operator[](const std::string& key) const {
        return std::get<BencodeDict>(data_).at(key);
    }

    std::optional<BencodeInt> BencodeValue::getInt() const {
        if (isInt())
            return asInt();
        return std::nullopt;
    }

    std::optional<std::string> BencodeValue::getString() const {
        if (isString())
            return asString();
        return std::nullopt;
    }

    std::string Bencode::encode(const BencodeValue& value) {
        std::string result;
        result.reserve(256);  // 预分配减少重新分配
        encodeValue(value, result);
        return result;
    }

    std::string Bencode::encode(BencodeInt i) {
        return encode(BencodeValue(i));
    }

    std::string Bencode::encode(const std::string& s) {
        return encode(BencodeValue(s));
    }


    void Bencode::encodeValue(const BencodeValue& value, std::string& out) {
        if (value.isInt()) {
            encodeInt(value.asInt(), out);
        } else if (value.isString()) {
            encodeString(value.asString(), out);
        } else if (value.isList()) {
            encodeList(value.asList(), out);
        } else if (value.isDict()) {
            encodeDict(value.asDict(), out);
        }
    }

    void Bencode::encodeInt(BencodeInt i, std::string& out) {
        out += kIntStart;
        out += std::to_string(i);
        out += kEnd;
    }

    void Bencode::encodeString(const std::string& s, std::string& out) {
        out += std::to_string(s.size());
        out += kStringSep;
        out += s;
    }

    void Bencode::encodeList(const BencodeList& list, std::string& out) {
        out += kListStart;
        for (const auto& item : list) {
            encodeValue(item, out);
        }
        out += kEnd;
    }

    void Bencode::encodeDict(const BencodeDict& dict, std::string& out) {
        out += kDictStart;

        for (const auto& [key, value] : dict) {
            encodeString(key, out);
            encodeValue(value, out);
        }
        out += kEnd;
    }


    // 解码

    std::optional<BencodeValue> Bencode::decode(std::string_view data) {
        Decoder decoder(data);
        return decoder.decode();
    }

    Bencode::Decoder::Decoder(std::string_view data) : data_(data) {}

    std::optional<BencodeValue> Bencode::Decoder::decode() {
        auto result = decodeValue();
        if (result && pos_ != data_.size()) {

        }
        return result;
    }

    std::optional<BencodeValue> Bencode::Decoder::decodeValue() {
        if (!hasMore()) {
            return std::nullopt;
        }

        char c = peek();

        if (c == kIntStart) {
            auto i = decodeInt();
            if (i) {
                return BencodeValue(*i);
            }
        } else if (c == kListStart) {
            auto l = decodeList();
            if (l) {
                return BencodeValue(*l);
            }
        } else if (c == kDictStart) {
            auto d = decodeDict();
            if (d) {
                return BencodeValue(*d);
            }
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            auto s = decodeString();
            if (s) {
                return BencodeValue(*s);
            }
        }

        return std::nullopt;
    }

    std::optional<BencodeInt> Bencode::Decoder::decodeInt() {
        if (!expect(kIntStart)) {
            return std::nullopt;
        }

        bool negative = false;

        if (hasMore() && peek() == kNegative) {
            negative = true;
            consume();
        }

        if (!hasMore() || !std::isdigit(static_cast<unsigned char>(peek()))) {
            return std::nullopt;
        }

        // 检查前导0
        if (peek() == kZero) {
            consume();
            if (hasMore() && peek() != kEnd) {
                // 前导0错误 (除非是i0e)
                return std::nullopt;
            }

            if (!expect(kEnd)) {
                return std::nullopt;
            }

            return negative ? std::nullopt : std::optional<BencodeInt>(0);
        }

        BencodeInt value = 0;

        while (hasMore() && std::isdigit(static_cast<unsigned char>(peek()))) {
            value = value * 10 + (consume() - kZero);
        }

        if (!expect(kEnd)) {
            return std::nullopt;
        }

        return negative ? -value : value;
    }

    std::optional<BencodeString> Bencode::Decoder::decodeString() {
        if (!hasMore() || !std::isdigit(static_cast<unsigned char>(peek()))) {
            return std::nullopt;
        }

        size_t length = 0;

        while (hasMore() && std::isdigit(static_cast<unsigned char>(peek()))) {
            length = length * 10 + static_cast<size_t>(consume() - kZero);
        }

        if (!expect(kStringSep)) {
            return std::nullopt;
        }

        // 检查剩余数据是否足够
        if (pos_ + length > data_.size()) {
            return std::nullopt;
        }

        BencodeString result(data_.substr(pos_, length));
        pos_ += length;
        return result;
    }

    std::optional<BencodeList> Bencode::Decoder::decodeList() {
        if (!expect(kListStart)) {
            return std::nullopt;
        }

        BencodeList result;

        while (hasMore() && peek() != kEnd) {
            auto item = decodeValue();
            if (!item) {
                return std::nullopt;
            }
            result.push_back(std::move(*item));
        }

        if (!expect(kEnd)) {
            return std::nullopt;
        }

        return result;
    }

    std::optional<BencodeDict> Bencode::Decoder::decodeDict() {
        if (!expect(kDictStart)) {
            return std::nullopt;
        }

        BencodeDict result;
        std::string lastKey;

        while (hasMore() && peek() != kEnd) {
            auto key = decodeString();
            if (!key) {
                return std::nullopt;
            }

            // 检查键是否按字典序排列（为了兼容性，不强制要求）
            if (!lastKey.empty() && *key <= lastKey) {
                // 键未按字典序排列，但仍然接受
            }

            lastKey = *key;

            auto value = decodeValue();
            if (!value) {
                return std::nullopt;
            }

            result[std::move(*key)] = std::move(*value);
        }

        if (!expect(kEnd)) {
            return std::nullopt;
        }

        return result;
    }

    bool Bencode::Decoder::hasMore() const {
        return pos_ < data_.size();
    }

    char Bencode::Decoder::peek() const {
        return data_[pos_];
    }

    char Bencode::Decoder::consume() {
        return data_[pos_++];
    }

    bool Bencode::Decoder::expect(char c) {
        if (hasMore() && peek() == c) {
            consume();
            return true;
        }

        return false;
    }

} // namespace magnet::protocols
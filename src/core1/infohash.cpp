#include "interfaces.h"
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <algorithm>

namespace bt {

InfoHash::InfoHash(const std::array<std::byte, HASH_SIZE>& bytes)
    : bytes_(bytes) {
}

InfoHash::InfoHash(const std::string& hexString) {
    if (hexString.length() != HASH_SIZE * 2) {
        throw std::invalid_argument("Invalid hash string length. Expected " + 
                                   std::to_string(HASH_SIZE * 2) + 
                                   " characters, got " + 
                                   std::to_string(hexString.length()));
    }
    
    // 将十六进制字符串转换为字节数组
    for (size_t i = 0; i < HASH_SIZE; ++i) {
        std::string byteStr = hexString.substr(i * 2, 2);
        
        // 使用std::from_chars将更高效，但为了兼容性使用std::stoi
        try {
            unsigned int byteVal = std::stoi(byteStr, nullptr, 16);
            bytes_[i] = static_cast<std::byte>(byteVal);
        } catch (const std::exception& e) {
            throw std::invalid_argument("Failed to parse hex string at position " + 
                                       std::to_string(i * 2) + ": " + e.what());
        }
    }
}

const std::array<std::byte, InfoHash::HASH_SIZE>& InfoHash::getBytes() const {
    return bytes_;
}

std::string InfoHash::toHexString() const {
    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    
    for (const auto& byte : bytes_) {
        ss << std::setw(2) << static_cast<int>(byte);
    }
    
    // 将字符串转换为大写
    std::string result = ss.str();
    std::transform(result.begin(), result.end(), result.begin(), ::toupper);
    
    return result;
}

bool InfoHash::operator==(const InfoHash& other) const {
    return bytes_ == other.bytes_;
}

bool InfoHash::operator<(const InfoHash& other) const {
    return bytes_ < other.bytes_;
}

} // namespace bt 
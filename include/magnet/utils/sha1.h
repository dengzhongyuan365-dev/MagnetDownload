#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace magnet::utils {

/**
 * @brief 简单的 SHA1 实现
 * 
 * 用于验证 BitTorrent 分片数据的完整性
 */
class SHA1 {
public:
    static constexpr size_t kDigestSize = 20;
    using Digest = std::array<uint8_t, kDigestSize>;
    
private:
    // SHA1 初始哈希值
    static constexpr uint32_t kH0 = 0x67452301;
    static constexpr uint32_t kH1 = 0xEFCDAB89;
    static constexpr uint32_t kH2 = 0x98BADCFE;
    static constexpr uint32_t kH3 = 0x10325476;
    static constexpr uint32_t kH4 = 0xC3D2E1F0;
    
    // SHA1 轮常量
    static constexpr uint32_t kK0 = 0x5A827999;  // 0-19 轮
    static constexpr uint32_t kK1 = 0x6ED9EBA1;  // 20-39 轮
    static constexpr uint32_t kK2 = 0x8F1BBCDC;  // 40-59 轮
    static constexpr uint32_t kK3 = 0xCA62C1D6;  // 60-79 轮

public:
    
    SHA1() { reset(); }
    
    void reset() {
        state_[0] = kH0;
        state_[1] = kH1;
        state_[2] = kH2;
        state_[3] = kH3;
        state_[4] = kH4;
        count_ = 0;
        buffer_len_ = 0;
    }
    
    void update(const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            buffer_[buffer_len_++] = data[i];
            count_++;
            if (buffer_len_ == 64) {
                processBlock(buffer_);
                buffer_len_ = 0;
            }
        }
    }
    
    Digest finalize() {
        uint64_t bit_count = count_ * 8;
        
        // 填充
        buffer_[buffer_len_++] = 0x80;
        
        while (buffer_len_ != 56) {
            if (buffer_len_ == 64) {
                processBlock(buffer_);
                buffer_len_ = 0;
            }
            buffer_[buffer_len_++] = 0x00;
        }
        
        // 追加长度（大端）
        for (int i = 7; i >= 0; --i) {
            buffer_[buffer_len_++] = static_cast<uint8_t>(bit_count >> (i * 8));
        }
        processBlock(buffer_);
        
        // 输出摘要
        Digest digest;
        for (int i = 0; i < 5; ++i) {
            digest[i * 4 + 0] = static_cast<uint8_t>(state_[i] >> 24);
            digest[i * 4 + 1] = static_cast<uint8_t>(state_[i] >> 16);
            digest[i * 4 + 2] = static_cast<uint8_t>(state_[i] >> 8);
            digest[i * 4 + 3] = static_cast<uint8_t>(state_[i]);
        }
        
        return digest;
    }

private:
    static uint32_t rotl(uint32_t x, int n) {
        return (x << n) | (x >> (32 - n));
    }
    
    void processBlock(const uint8_t* block) {
        uint32_t w[80];
        
        // 扩展消息
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(block[i * 4 + 0]) << 24) |
                   (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
                   (static_cast<uint32_t>(block[i * 4 + 3]));
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = rotl(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
        }
        
        uint32_t a = state_[0];
        uint32_t b = state_[1];
        uint32_t c = state_[2];
        uint32_t d = state_[3];
        uint32_t e = state_[4];
        
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = kK0;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = kK1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = kK2;
            } else {
                f = b ^ c ^ d;
                k = kK3;
            }
            
            uint32_t temp = rotl(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rotl(b, 30);
            b = a;
            a = temp;
        }
        
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
    }
    
    uint32_t state_[5];
    uint64_t count_;
    uint8_t buffer_[64];
    size_t buffer_len_;
};

/**
 * @brief 计算 SHA1 哈希
 * @param data 数据指针
 * @param len 数据长度
 * @return 20 字节的 SHA1 摘要
 */
inline std::array<uint8_t, 20> sha1(const uint8_t* data, size_t len) {
    SHA1 hasher;
    hasher.update(data, len);
    return hasher.finalize();
}

/**
 * @brief 计算 SHA1 哈希
 * @param data 数据向量
 * @return 20 字节的 SHA1 摘要
 */
inline std::array<uint8_t, 20> sha1(const std::vector<uint8_t>& data) {
    return sha1(data.data(), data.size());
}

} // namespace magnet::utils


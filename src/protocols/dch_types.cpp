#include "../../include/magnet/protocols/dch_types.h"
#include <cstring>
#include <random>

namespace magnet::protocols {

    NodeId::NodeId():data_{}{}

    NodeId::NodeId(const ByteArray& bytes):data_(bytes){}

    NodeId NodeId::fromInfoHash(const InfoHash& hash) {
        ByteArray bytes;
        std::memcpy(bytes.data(), hash.bytes().data(), s_KNodeSize);
        return NodeId(bytes);
    }

    NodeId NodeId::random() {
        ByteArray bytes;
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint16_t> dist(0,255);
        for(auto& b: bytes) {
            b = static_cast<uint8_t>(dist(gen));
        }

        return NodeId(bytes);
    }

    NodeId NodeId::distance(const NodeId& other) const {
        ByteArray result;

        for(size_t i = 0;i<s_KNodeSize;++i) {
            result[i] = data_[i] ^ other.data_[i];
        }

        return NodeId(result);
    }

    size_t NodeId::bucketIndex() const {
        for (size_t i = 0; i < s_KNodeSize; ++i) {
            if (data_[i] != 0) {
                uint8_t byte = data_[i];
                size_t bit = 7;
                while ((byte & 0x80) == 0) {
                    byte <<= 1;
                    --bit;
                }
                return (s_KNodeSize - 1 - i) * 8 + bit;
            }
        }
        return 0;
    }
};
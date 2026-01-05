#pragma once

#include <array>
#include <string>
#include <optional>
#include "magnet_types.h"

namespace magnet::protocols {
    class NodeId {
        public:
            // 标识长度为20
            static constexpr size_t s_KNodeSize =20;
            using ByteArray = std::array<uint8_t, s_KNodeSize>;

            NodeId();

            explicit NodeId(const ByteArray& bytes);

            static NodeId random();

            // static std::optional<NodeId> fromHex(const std::string& hex);

            // 文件hash
            static NodeId fromInfoHash(const InfoHash& hash);

            std::string toHex() const;
            // 原始20字节
            std::string toString() const;
            const ByteArray& bytes() const;

            // 距离计算(XOR)
            NodeId distance(const NodeId& other) const;
            // 比较谁更接近
            int compareDisance(const NodeId& a, const NodeId& b) const;
            // 前导零的位数
            int leadingZeroBits() const;

            /*
            * @brief 计算距离对应的通索引（0-159）
            * 返回最高有效的位置，用于确定放到哪一个 k-bucket
            * 距离越小，索引越大（越近的节点放在高索引的桶里）
            */
            size_t bucketIndex() const;

            bool isZero() const;

            bool operator==(const NodeId& other) const;
            bool operator!=(const NodeId& other) const;
            bool operator<(const NodeId& other) const;

        private:
            ByteArray data_;

    };


};
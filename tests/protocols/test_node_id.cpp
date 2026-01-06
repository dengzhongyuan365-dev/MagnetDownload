/**
 * @file test_node_id.cpp
 * @brief NodeId 单元测试
 */

#include <gtest/gtest.h>
#include <magnet/protocols/dch_types.h>

using namespace magnet::protocols;

// ========== 构造函数测试 ==========

TEST(NodeIdTest, DefaultConstructorIsZero) {
    NodeId id;
    EXPECT_TRUE(id.isZero());
}

TEST(NodeIdTest, ByteArrayConstructor) {
    NodeId::ByteArray bytes{};
    bytes[0] = 0x12;
    bytes[19] = 0x34;

    NodeId id(bytes);
    EXPECT_FALSE(id.isZero());
    EXPECT_EQ(id.bytes()[0], 0x12);
    EXPECT_EQ(id.bytes()[19], 0x34);
}

// ========== random() 测试 ==========

TEST(NodeIdTest, RandomNotZero) {
    NodeId id = NodeId::random();
    EXPECT_FALSE(id.isZero());
}

TEST(NodeIdTest, RandomGeneratesDifferentIds) {
    NodeId id1 = NodeId::random();
    NodeId id2 = NodeId::random();
    // 两次随机生成应该不同（极小概率相同）
    EXPECT_NE(id1, id2);
}

// ========== fromInfoHash() 测试 ==========

TEST(NodeIdTest, FromInfoHash) {
    auto hash = InfoHash::fromHex("1234567890abcdef1234567890abcdef12345678");
    ASSERT_TRUE(hash.has_value());

    NodeId id = NodeId::fromInfoHash(*hash);
    EXPECT_EQ(id.toHex(), "1234567890abcdef1234567890abcdef12345678");
}

// ========== toHex() 测试 ==========

TEST(NodeIdTest, ToHexAllZeros) {
    NodeId id;
    EXPECT_EQ(id.toHex(), "0000000000000000000000000000000000000000");
}

TEST(NodeIdTest, ToHexNonZero) {
    NodeId::ByteArray bytes{};
    bytes[0] = 0xAB;
    bytes[1] = 0xCD;

    NodeId id(bytes);
    std::string hex = id.toHex();
    EXPECT_EQ(hex.substr(0, 4), "abcd");
}

// ========== distance() 测试 ==========

TEST(NodeIdTest, DistanceToSelfIsZero) {
    NodeId id = NodeId::random();
    NodeId dist = id.distance(id);
    EXPECT_TRUE(dist.isZero());
}

TEST(NodeIdTest, DistanceIsSymmetric) {
    NodeId a = NodeId::random();
    NodeId b = NodeId::random();

    NodeId dist_ab = a.distance(b);
    NodeId dist_ba = b.distance(a);

    EXPECT_EQ(dist_ab, dist_ba);
}

TEST(NodeIdTest, DistanceXorCorrect) {
    NodeId::ByteArray bytes_a{};
    NodeId::ByteArray bytes_b{};
    bytes_a[0] = 0xFF;  // 1111 1111
    bytes_b[0] = 0x0F;  // 0000 1111

    NodeId a(bytes_a);
    NodeId b(bytes_b);
    NodeId dist = a.distance(b);

    // XOR: 1111 1111 ^ 0000 1111 = 1111 0000 = 0xF0
    EXPECT_EQ(dist.bytes()[0], 0xF0);
}

// ========== compareDistance() 测试 ==========

TEST(NodeIdTest, CompareDistanceCloser) {
    NodeId::ByteArray target_bytes{};
    NodeId::ByteArray near_bytes{};
    NodeId::ByteArray far_bytes{};

    target_bytes[0] = 0x00;
    near_bytes[0] = 0x01;    // 距离 = 0x01
    far_bytes[0] = 0x80;     // 距离 = 0x80

    NodeId target(target_bytes);
    NodeId near(near_bytes);
    NodeId far(far_bytes);

    // near 比 far 更接近 target
    EXPECT_EQ(target.compareDistance(near, far), -1);
    // far 比 near 更远
    EXPECT_EQ(target.compareDistance(far, near), 1);
    // 相同距离
    EXPECT_EQ(target.compareDistance(near, near), 0);
}

// ========== leadingZeroBits() 测试 ==========

TEST(NodeIdTest, LeadingZeroBitsAllZero) {
    NodeId id;
    EXPECT_EQ(id.leadingZeroBits(), 160);
}

TEST(NodeIdTest, LeadingZeroBitsHighBit) {
    NodeId::ByteArray bytes{};
    bytes[0] = 0x80;  // 1000 0000

    NodeId id(bytes);
    EXPECT_EQ(id.leadingZeroBits(), 0);
}

TEST(NodeIdTest, LeadingZeroBitsLowBit) {
    NodeId::ByteArray bytes{};
    bytes[19] = 0x01;  // 最后一个字节的最低位

    NodeId id(bytes);
    EXPECT_EQ(id.leadingZeroBits(), 159);
}

TEST(NodeIdTest, LeadingZeroBitsMiddle) {
    NodeId::ByteArray bytes{};
    bytes[5] = 0x08;  // 0000 1000，第 5 字节的第 3 位

    NodeId id(bytes);
    // 前 5 字节 = 40 位，加上 0000 1xxx 的 4 个前导零
    EXPECT_EQ(id.leadingZeroBits(), 44);
}

// ========== bucketIndex() 测试 ==========

TEST(NodeIdTest, BucketIndexHighBit) {
    NodeId::ByteArray bytes{};
    bytes[0] = 0x80;  // 最高位是 1

    NodeId id(bytes);
    // bucketIndex 应该是 159（最远的桶）
    EXPECT_EQ(id.bucketIndex(), 159);
}

TEST(NodeIdTest, BucketIndexLowBit) {
    NodeId::ByteArray bytes{};
    bytes[19] = 0x01;  // 最低位是 1

    NodeId id(bytes);
    // bucketIndex 应该是 0（最近的桶）
    EXPECT_EQ(id.bucketIndex(), 0);
}

// ========== isZero() 测试 ==========

TEST(NodeIdTest, IsZeroTrue) {
    NodeId id;
    EXPECT_TRUE(id.isZero());
}

TEST(NodeIdTest, IsZeroFalse) {
    NodeId::ByteArray bytes{};
    bytes[10] = 0x01;

    NodeId id(bytes);
    EXPECT_FALSE(id.isZero());
}

// ========== 运算符测试 ==========

TEST(NodeIdTest, EqualityOperator) {
    NodeId::ByteArray bytes{};
    bytes[0] = 0x12;

    NodeId a(bytes);
    NodeId b(bytes);
    NodeId c;

    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
}

TEST(NodeIdTest, InequalityOperator) {
    NodeId a = NodeId::random();
    NodeId b = NodeId::random();

    EXPECT_TRUE(a != b);
}

TEST(NodeIdTest, LessThanOperator) {
    NodeId::ByteArray bytes_a{};
    NodeId::ByteArray bytes_b{};
    bytes_a[0] = 0x10;
    bytes_b[0] = 0x20;

    NodeId a(bytes_a);
    NodeId b(bytes_b);

    EXPECT_TRUE(a < b);
    EXPECT_FALSE(b < a);
}

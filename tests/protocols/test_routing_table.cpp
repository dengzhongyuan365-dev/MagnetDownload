/**
 * @file test_routing_table.cpp
 * @brief RoutingTable 单元测试
 */

#include <gtest/gtest.h>
#include <magnet/protocols/routing_table.h>
#include <thread>

using namespace magnet::protocols;

// ========== 辅助函数 ==========

// 创建一个指定字节的 NodeId
NodeId createNodeId(uint8_t first_byte) {
    NodeId::ByteArray bytes{};
    bytes[0] = first_byte;
    return NodeId(bytes);
}

// 创建一个 DhtNode
DhtNode createDhtNode(uint8_t first_byte, const std::string& ip = "192.168.1.1", uint16_t port = 6881) {
    return DhtNode(createNodeId(first_byte), ip, port);
}

// ========== 构造函数测试 ==========

TEST(RoutingTableTest, ConstructorInitializesEmpty) {
    NodeId local_id = NodeId::random();
    RoutingTable rt(local_id);

    EXPECT_EQ(rt.nodeCount(), 0);
    EXPECT_EQ(rt.goodNodeCount(), 0);
    EXPECT_EQ(rt.localId(), local_id);
}

// ========== addNode 测试 ==========

TEST(RoutingTableTest, AddNodeSuccess) {
    NodeId local_id = createNodeId(0x00);
    RoutingTable rt(local_id);

    DhtNode node = createDhtNode(0x80);  // 与 local_id 距离较远
    EXPECT_TRUE(rt.addNode(node));
    EXPECT_EQ(rt.nodeCount(), 1);
}

TEST(RoutingTableTest, AddSelfFails) {
    NodeId local_id = createNodeId(0x12);
    RoutingTable rt(local_id);

    DhtNode self(local_id, "127.0.0.1", 6881);
    EXPECT_FALSE(rt.addNode(self));
    EXPECT_EQ(rt.nodeCount(), 0);
}

TEST(RoutingTableTest, AddDuplicateUpdates) {
    NodeId local_id = createNodeId(0x00);
    RoutingTable rt(local_id);

    DhtNode node = createDhtNode(0x80, "192.168.1.1", 6881);
    EXPECT_TRUE(rt.addNode(node));

    // 添加相同 ID 但不同端口
    DhtNode updated = createDhtNode(0x80, "192.168.1.1", 7777);
    EXPECT_TRUE(rt.addNode(updated));

    // 数量不变
    EXPECT_EQ(rt.nodeCount(), 1);
}

TEST(RoutingTableTest, AddMultipleNodes) {
    NodeId local_id = createNodeId(0x00);
    RoutingTable rt(local_id);

    for (int i = 1; i <= 10; ++i) {
        DhtNode node = createDhtNode(static_cast<uint8_t>(i * 16));
        rt.addNode(node);
    }

    EXPECT_EQ(rt.nodeCount(), 10);
}

TEST(RoutingTableTest, BucketFullDiscards) {
    // 创建一个 local_id，使得所有测试节点都落在同一个桶
    NodeId local_id = createNodeId(0x00);
    RoutingTable rt(local_id);

    // 添加 8 个节点到同一个桶（都以 0x8x 开头，距离都是 0x8x）
    for (int i = 0; i < 8; ++i) {
        NodeId::ByteArray bytes{};
        bytes[0] = 0x80;
        bytes[1] = static_cast<uint8_t>(i);
        DhtNode node(NodeId(bytes), "192.168.1." + std::to_string(i), 6881);
        EXPECT_TRUE(rt.addNode(node));
    }

    EXPECT_EQ(rt.nodeCount(), 8);

    // 第 9 个节点应该被丢弃（桶已满）
    NodeId::ByteArray bytes{};
    bytes[0] = 0x80;
    bytes[1] = 0xFF;
    DhtNode extra(NodeId(bytes), "192.168.1.100", 6881);
    EXPECT_FALSE(rt.addNode(extra));

    EXPECT_EQ(rt.nodeCount(), 8);
}

// ========== findClosest 测试 ==========

TEST(RoutingTableTest, FindClosestEmpty) {
    NodeId local_id = NodeId::random();
    RoutingTable rt(local_id);

    NodeId target = NodeId::random();
    auto closest = rt.findCloset(target);

    EXPECT_TRUE(closest.empty());
}

TEST(RoutingTableTest, FindClosestReturnsNodes) {
    NodeId local_id = createNodeId(0x00);
    RoutingTable rt(local_id);

    // 添加一些节点
    for (int i = 1; i <= 5; ++i) {
        DhtNode node = createDhtNode(static_cast<uint8_t>(i * 32));
        rt.addNode(node);
    }

    NodeId target = createNodeId(0x40);
    auto closest = rt.findCloset(target, 3);

    EXPECT_LE(closest.size(), 3u);
    EXPECT_GE(closest.size(), 1u);
}

TEST(RoutingTableTest, FindClosestSortedByDistance) {
    NodeId local_id = createNodeId(0x00);
    RoutingTable rt(local_id);

    // 添加节点
    rt.addNode(createDhtNode(0x10));  // 距离 target 0x30
    rt.addNode(createDhtNode(0x20));  // 距离 target 0x00 (最近)
    rt.addNode(createDhtNode(0x30));  // 距离 target 0x10
    rt.addNode(createDhtNode(0x40));  // 距离 target 0x60

    NodeId target = createNodeId(0x20);
    auto closest = rt.findCloset(target, 4);

    ASSERT_EQ(closest.size(), 4u);

    // 验证按距离排序
    for (size_t i = 1; i < closest.size(); ++i) {
        NodeId dist_prev = target.distance(closest[i - 1].id_);
        NodeId dist_curr = target.distance(closest[i].id_);
        EXPECT_TRUE(dist_prev < dist_curr || dist_prev == dist_curr);
    }
}

TEST(RoutingTableTest, FindClosestExcludesBadNodes) {
    NodeId local_id = createNodeId(0x00);
    RoutingTable rt(local_id);

    DhtNode good_node = createDhtNode(0x10);
    DhtNode bad_node = createDhtNode(0x20);

    rt.addNode(good_node);
    rt.addNode(bad_node);

    // 标记 bad_node 为坏节点
    rt.markNodeFailed(bad_node.id_);
    rt.markNodeFailed(bad_node.id_);
    rt.markNodeFailed(bad_node.id_);

    NodeId target = createNodeId(0x20);  // 更接近 bad_node
    auto closest = rt.findCloset(target);

    // 应该只返回 good_node
    EXPECT_EQ(closest.size(), 1u);
    EXPECT_EQ(closest[0].id_, good_node.id_);
}

// ========== markNodeResponded 测试 ==========

TEST(RoutingTableTest, MarkNodeRespondedUpdatesState) {
    NodeId local_id = createNodeId(0x00);
    RoutingTable rt(local_id);

    DhtNode node = createDhtNode(0x80);
    rt.addNode(node);

    // 等待一小段时间
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    rt.markNodeResponded(node.id_);

    // 节点应该仍然是 good
    EXPECT_EQ(rt.goodNodeCount(), 1u);
}

// ========== markNodeFailed 测试 ==========

TEST(RoutingTableTest, MarkNodeFailedIncrementsCounter) {
    NodeId local_id = createNodeId(0x00);
    RoutingTable rt(local_id);

    DhtNode node = createDhtNode(0x80);
    rt.addNode(node);

    // 初始状态是 good
    EXPECT_EQ(rt.goodNodeCount(), 1u);

    // 失败 3 次变成 bad
    rt.markNodeFailed(node.id_);
    rt.markNodeFailed(node.id_);
    rt.markNodeFailed(node.id_);

    auto stats = rt.getStatistics();
    EXPECT_EQ(stats.bad_nodes, 1u);
    EXPECT_EQ(stats.good_nodes, 0u);
}

TEST(RoutingTableTest, BadNodeCanBeReplaced) {
    NodeId local_id = createNodeId(0x00);
    RoutingTable rt(local_id);

    // 填满一个桶
    for (int i = 0; i < 8; ++i) {
        NodeId::ByteArray bytes{};
        bytes[0] = 0x80;
        bytes[1] = static_cast<uint8_t>(i);
        DhtNode node(NodeId(bytes), "192.168.1." + std::to_string(i), 6881);
        rt.addNode(node);
    }

    // 标记第一个节点为坏
    NodeId::ByteArray bad_bytes{};
    bad_bytes[0] = 0x80;
    bad_bytes[1] = 0x00;
    NodeId bad_id(bad_bytes);

    rt.markNodeFailed(bad_id);
    rt.markNodeFailed(bad_id);
    rt.markNodeFailed(bad_id);

    // 新节点应该能替换坏节点
    NodeId::ByteArray new_bytes{};
    new_bytes[0] = 0x80;
    new_bytes[1] = 0xFF;
    DhtNode new_node(NodeId(new_bytes), "192.168.1.100", 6881);

    EXPECT_TRUE(rt.addNode(new_node));
    EXPECT_EQ(rt.nodeCount(), 8u);
}

// ========== getStaleBuckets 测试 ==========

TEST(RoutingTableTest, GetStaleBucketsInitiallyEmpty) {
    NodeId local_id = NodeId::random();
    RoutingTable rt(local_id);

    // 刚创建的路由表没有过期桶
    auto stale = rt.getStaleBuckets();
    EXPECT_TRUE(stale.empty());
}

// ========== getRandomIdInBucket 测试 ==========

TEST(RoutingTableTest, GetRandomIdInBucketReturnsValidId) {
    NodeId local_id = NodeId::random();
    RoutingTable rt(local_id);

    NodeId random_id = rt.getRandomIdInBucket(100);
    EXPECT_FALSE(random_id.isZero());
}

// ========== getStatistics 测试 ==========

TEST(RoutingTableTest, GetStatisticsAccurate) {
    NodeId local_id = createNodeId(0x00);
    RoutingTable rt(local_id);

    // 添加 5 个节点
    for (int i = 1; i <= 5; ++i) {
        DhtNode node = createDhtNode(static_cast<uint8_t>(i * 32));
        rt.addNode(node);
    }

    auto stats = rt.getStatistics();
    EXPECT_EQ(stats.total_nodes, 5u);
    EXPECT_EQ(stats.good_nodes, 5u);
    EXPECT_EQ(stats.bad_nodes, 0u);
    EXPECT_GE(stats.non_empty_buckets, 1u);
}

// ========== 线程安全测试 ==========

TEST(RoutingTableTest, ThreadSafeAddAndFind) {
    NodeId local_id = createNodeId(0x00);
    RoutingTable rt(local_id);

    // 多线程添加节点
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&rt, t]() {
            for (int i = 0; i < 10; ++i) {
                NodeId::ByteArray bytes{};
                bytes[0] = static_cast<uint8_t>(t * 64 + i);
                DhtNode node(NodeId(bytes), "192.168.1.1", 6881);
                rt.addNode(node);
            }
        });
    }

    // 同时进行查找
    std::thread finder([&rt]() {
        for (int i = 0; i < 20; ++i) {
            NodeId target = NodeId::random();
            rt.findCloset(target);
        }
    });

    for (auto& t : threads) {
        t.join();
    }
    finder.join();

    // 不崩溃就算通过
    EXPECT_GE(rt.nodeCount(), 1u);
}

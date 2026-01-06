#pragma once
#include <cstddef>
#include "dch_types.h"

#include <chrono>
#include <mutex>

namespace magnet::protocols {

    class RoutingTable
    {
    private:
        /* data */
    public:
        static constexpr size_t s_kBucketSize = 8;
        static constexpr size_t s_kBucketCount = 160;

        explicit RoutingTable(const NodeId& local_id);

        /*
        * @brief 添加或者更新节点
        * @param node要添加的节点
        * @return true 如果节点被添加或者更新
        * 规则： 不添加自己的
        * 已存在的节点会更新信息并移动到桶的末尾（LRU）
        * 桶未满的时候直接添加
        * 桶已满的时候，如果有坏节点则替换，否则丢弃新的节点
        */
        bool addNode(const DhtNode& node);

        /*
        * @brief 查找最近的节点
        * @param target 目标id
        * @param count 返回最大节点数 default 8
        * @return  按距离排序的节点列表 小在前
        * 
        */

        std::vector<DhtNode> findCloset(const NodeId& target, size_t count = s_kBucketSize) const;

        // 标记响应成功的节点

        void markNodeResponded(const NodeId& id);

        void markNodeFailed(const NodeId& id);

        // 获取需要刷新桶的索引

        std::vector<size_t> getStaleBuckets() const;

        // 获取指定范围内随机的NOdeId
        NodeId getRandomIdInBucket(size_t bucket_idx) const;

        size_t nodeCount() const;

        size_t goodNodeCount() const;

        const NodeId& localId() const;

        struct Statistics
        {
            size_t total_nodes = 0;
            size_t good_nodes = 0;
            size_t questionable_nodes = 0;
            size_t bad_nodes = 0;
            size_t non_empty_buckets = 0;
        };
        
        
        Statistics getStatistics() const;

    private:
        
        struct Bucket
        {
            /* data */
            std::vector<DhtNode> nodes;
            std::chrono::steady_clock::time_point last_changed;
            Bucket() : last_changed(std::chrono::steady_clock::now()) {}
        };
        
        size_t getBucketIndex(const NodeId& node_id) const;

        NodeId local_id_;
        std::array<Bucket, s_kBucketCount> buckets_;
        
        mutable std::mutex mutex_;

    };
};
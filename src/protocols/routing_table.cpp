#include "../../include/magnet/protocols/routing_table.h"
#include <algorithm>
#include <random>

namespace magnet::protocols {

    RoutingTable::RoutingTable(const NodeId& local_id) : local_id_(local_id) {

    }

    size_t RoutingTable::getBucketIndex(const NodeId& node_id) const {
        NodeId dist = local_id_.distance(node_id);
        size_t idx = dist.bucketIndex();

        if (idx >= s_kBucketCount)
            return s_kBucketCount - 1;
        
        return idx;
    }

    bool RoutingTable::addNode(const DhtNode& node) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (node.id_ == local_id_)
            return false;

        size_t bucket_idx = getBucketIndex(node.id_);
        auto& bucket = buckets_[bucket_idx];

        auto it = std::find_if(bucket.nodes.begin(), bucket.nodes.end(),
        [&node](const DhtNode&n) {return n.id_ == node.id_;} );

        if (it != bucket.nodes.end()) {
            it->ip_ = node.ip_;
            it->port_ = node.port_;
            it->markResponded();

            DhtNode updated = *it;
            bucket.nodes.erase(it);
            bucket.nodes.push_back(updated);
            bucket.last_changed = std::chrono::steady_clock::now();

            return true;
        }

        // 节点不存在

        if (bucket.nodes.size() < s_kBucketSize) {
            bucket.nodes.push_back(node);
            bucket.last_changed = std::chrono::steady_clock::now();
            return true;
        }

        // 桶满，检查节点是否有坏节点可以替换

        auto bad_it = std::find_if(bucket.nodes.begin(), bucket.nodes.end(),
            [](const DhtNode& n) { return n.isBad();});

        if (bad_it != bucket.nodes.end()) {
            *bad_it = node;
            bucket.last_changed = std::chrono::steady_clock::now();
            return true;
        }

        return  false;
    } 

    void RoutingTable::markNodeResponded(const NodeId& id){

        std::lock_guard<std::mutex> lock(mutex_);

        size_t bucket_idx = getBucketIndex(id);

        auto& bucket = buckets_[bucket_idx];

        auto it = std::find_if(bucket.nodes.begin(), bucket.nodes.end(), 
                [&id](const DhtNode& n) { return n.id_ == id;});
        if (it != bucket.nodes.end()) {
            it->markResponded();
            bucket.last_changed = std::chrono::steady_clock::now();
        }
    }

    void RoutingTable::markNodeFailed(const NodeId& id) {
        std::lock_guard<std::mutex> lock(mutex_);

        size_t bucket_idx = getBucketIndex(id);
        auto& bucket = buckets_[bucket_idx];

        auto it = std::find_if(bucket.nodes.begin(), bucket.nodes.end(), 
            [&id](const DhtNode& n) { return n.id_ == id;});
        
        if (it != bucket.nodes.end()) {
            it->markFailed();
        }
    }

    std::vector<DhtNode> RoutingTable::findCloset(const NodeId& target, size_t count) const {
        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<std::pair<NodeId, DhtNode>> candidates;

        for (const auto& bucket : buckets_)
        {
            for(const auto& node: bucket.nodes) {
                // 过滤掉坏节点
                if (node.isBad()) {
                    continue;
                }
                NodeId dist = target.distance(node.id_);
                candidates.emplace_back(dist, node);
            }
        }

        std::sort(candidates.begin(), candidates.end(),
            [](const auto& a, const auto& b) {
                return a.first < b.first;
            });
        
        std::vector<DhtNode> result;

        result.reserve(std::min(count, candidates.size()));

        for (size_t i = 0; i < std::min(count, candidates.size()); ++i) {
            result.push_back(candidates[i].second);
        }

        return result;
        
    }

    std::vector<size_t> RoutingTable::getStaleBuckets() const {
        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<size_t> stale;

        auto now = std::chrono::steady_clock::now();

        for (size_t i = 0; i < s_kBucketCount; ++i) {
            if (!buckets_[i].nodes.empty()) {
                auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(
                now - buckets_[i].last_changed);
                if (elapsed.count() >= 15) {
                    stale.push_back(i);
                }
            }
        }

        return stale;
    }

    NodeId RoutingTable::getRandomIdInBucket(size_t bucket_idx) const {
        // 生成一个随机 NodeId，使其与 local_id_ 的距离落在指定桶范围内
        // 简化实现：生成随机 ID，然后设置正确的前缀位

        NodeId::ByteArray bytes;
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint16_t> dist(0, 255);

        // 先填充随机数据
        for (auto& b : bytes) {
            b = static_cast<uint8_t>(dist(gen));
        }

        // 计算需要与 local_id_ 相同的位数
        // bucket_idx 表示距离的最高位位置
        // 例如 bucket_idx = 159 表示距离的最高位在第 0 位（最高字节的最高位）
        // 我们需要让前 (159 - bucket_idx) 位与 local_id_ 相同

        size_t same_bits = 159 - bucket_idx;
        size_t same_bytes = same_bits / 8;
        size_t remaining_bits = same_bits % 8;

        // 复制相同的字节
        for (size_t i = 0; i < same_bytes && i < NodeId::s_KNodeSize; ++i) {
            bytes[i] = local_id_.bytes()[i];
        }

        // 处理部分相同的字节
        if (same_bytes < NodeId::s_KNodeSize && remaining_bits > 0) {
            uint8_t mask = static_cast<uint8_t>(0xFF << (8 - remaining_bits));
            bytes[same_bytes] = (local_id_.bytes()[same_bytes] & mask) |
                            (bytes[same_bytes] & ~mask);

            // 确保第一个不同的位是 1（使距离落在正确的桶）
            uint8_t flip_bit = static_cast<uint8_t>(0x80 >> remaining_bits);
            bytes[same_bytes] ^= flip_bit;
        }

        return NodeId(bytes);
    }


    size_t RoutingTable::nodeCount() const {
        std::lock_guard<std::mutex> lock(mutex_);

        size_t count = 0;
        for (const auto& bucket : buckets_) {
            count += bucket.nodes.size();
        }
        return count;
    }

    size_t RoutingTable::goodNodeCount() const {
        std::lock_guard<std::mutex> lock(mutex_);

        size_t count = 0;
        for (const auto& bucket : buckets_) {
            for (const auto& node : bucket.nodes) {
                if (node.isGood()) {
                    ++count;
                }
            }
        }
        return count;
    }

    RoutingTable::Statistics RoutingTable::getStatistics() const {
        std::lock_guard<std::mutex> lock(mutex_);

        Statistics stats;
        for (const auto& bucket : buckets_) {
            if (!bucket.nodes.empty()) {
                ++stats.non_empty_buckets;
            }
            for (const auto& node : bucket.nodes) {
                ++stats.total_nodes;
                if (node.isGood()) {
                    ++stats.good_nodes;
                } else if (node.isQuestionable()) {
                    ++stats.questionable_nodes;
                } else if (node.isBad()) {
                    ++stats.bad_nodes;
                }
            }
        }
        return stats;
    }

    const NodeId& RoutingTable::localId() const {
        return local_id_;
    }
};
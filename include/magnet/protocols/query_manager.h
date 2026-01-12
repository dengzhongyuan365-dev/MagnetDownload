#pragma once

#include "dht_message.h"
#include "dch_types.h"
#include "magnet_types.h"
#include "../network/udp_client.h"

#include <asio.hpp>
#include <functional>
#include <memory>
#include <map>
#include <mutex>
#include <chrono>
#include <atomic>

namespace magnet::protocols {

// ============================================================================
// 查询错误类型
// ============================================================================

enum class QueryError {
    Timeout,        // 超时且重试耗尽
    SendFailed,     // 发送失败
    Cancelled,      // 被取消
    ShuttingDown,   // 管理器正在关闭
    QueueFull       // 待处理队列已满
};

/**
 * @brief 将 QueryError 转换为字符串
 */
inline const char* queryErrorToString(QueryError error) {
    switch (error) {
        case QueryError::Timeout: return "Timeout";
        case QueryError::SendFailed: return "SendFailed";
        case QueryError::Cancelled: return "Cancelled";
        case QueryError::ShuttingDown: return "ShuttingDown";
        case QueryError::QueueFull: return "QueueFull";
        default: return "Unknown";
    }
}

// ============================================================================
// 查询结果类型
// ============================================================================

/**
 * @brief 查询结果（成功返回 DhtMessage，失败返回 QueryError）
 */
using QueryResult = Result<DhtMessage, QueryError>;

/**
 * @brief 查询完成回调
 */
using QueryCallback = std::function<void(QueryResult)>;

// ============================================================================
// QueryManager 配置
// ============================================================================

struct QueryManagerConfig {
    std::chrono::milliseconds default_timeout{2000};   // 默认超时 2 秒
    int default_max_retries{2};                         // 默认重试 2 次
    std::chrono::milliseconds check_interval{500};     // 检查间隔 500ms
    size_t max_pending_queries{1000};                  // 最大待处理数
};

// ============================================================================
// QueryManager 统计信息
// ============================================================================

struct QueryManagerStatistics {
    size_t queries_sent{0};         // 发送的查询数
    size_t queries_succeeded{0};    // 成功的查询数
    size_t queries_failed{0};       // 失败的查询数
    size_t queries_timeout{0};      // 超时的查询数
    size_t retries_total{0};        // 总重试次数
    size_t current_pending{0};      // 当前待处理数
    
    double total_latency_ms{0};     // 总延迟（用于计算平均值）
    
    /**
     * @brief 获取平均延迟（毫秒）
     */
    double avgLatencyMs() const {
        return queries_succeeded > 0 ? total_latency_ms / queries_succeeded : 0;
    }
    
    /**
     * @brief 获取成功率
     */
    double successRate() const {
        size_t total = queries_succeeded + queries_failed;
        return total > 0 ? static_cast<double>(queries_succeeded) / total : 0;
    }
    
    /**
     * @brief 重置统计信息
     */
    void reset() {
        queries_sent = 0;
        queries_succeeded = 0;
        queries_failed = 0;
        queries_timeout = 0;
        retries_total = 0;
        total_latency_ms = 0;
    }
};

// ============================================================================
// QueryManager 类
// ============================================================================

/**
 * @class QueryManager
 * @brief DHT 查询管理器
 * 
 * 管理 DHT 查询的生命周期，包括：
 * - 请求-响应匹配（通过 transaction_id）
 * - 超时检测和重试
 * - 并发查询管理
 * - 统计信息收集
 * 
 * 使用示例：
 * @code
 * auto udp_client = std::make_shared<UdpClient>(io_context, 6881);
 * QueryManager qm(io_context, udp_client);
 * qm.start();
 * 
 * auto msg = DhtMessage::createGetPeers(my_id, info_hash);
 * qm.sendQuery(target_node, std::move(msg), [](QueryResult result) {
 *     if (result.is_ok()) {
 *         // 处理响应
 *     }
 * });
 * @endcode
 */
class QueryManager : public std::enable_shared_from_this<QueryManager> {
public:
    /**
     * @brief 构造函数
     * @param io_context 事件循环
     * @param udp_client UDP 客户端（用于发送查询）
     * @param config 配置参数
     */
    QueryManager(asio::io_context& io_context,
                 std::shared_ptr<network::UdpClient> udp_client,
                 QueryManagerConfig config = {});
    
    /**
     * @brief 析构函数
     */
    ~QueryManager();
    
    // 禁止拷贝和移动
    QueryManager(const QueryManager&) = delete;
    QueryManager& operator=(const QueryManager&) = delete;
    QueryManager(QueryManager&&) = delete;
    QueryManager& operator=(QueryManager&&) = delete;
    
    // ========================================================================
    // 核心方法
    // ========================================================================
    
    /**
     * @brief 发送查询
     * @param target 目标节点
     * @param message 要发送的消息
     * @param callback 结果回调
     * @param timeout 超时时间（0 表示使用默认值）
     * @param max_retries 最大重试次数（-1 表示使用默认值）
     * 
     * 回调会在以下情况被调用：
     * - 收到响应：callback(Result::ok(response))
     * - 超时：callback(Result::err(QueryError::Timeout))
     * - 发送失败：callback(Result::err(QueryError::SendFailed))
     */
    void sendQuery(const DhtNode& target,
                   DhtMessage message,
                   QueryCallback callback,
                   std::chrono::milliseconds timeout = std::chrono::milliseconds{0},
                   int max_retries = -1);
    
    /**
     * @brief 处理收到的响应
     * @param response 响应消息
     * @return true 如果找到匹配的待处理查询
     * 
     * DhtClient 收到响应后应调用此方法
     */
    bool handleResponse(const DhtMessage& response);
    
    /**
     * @brief 取消特定查询
     * @param transaction_id 事务 ID
     * @return true 如果找到并取消了查询
     */
    bool cancelQuery(const std::string& transaction_id);
    
    /**
     * @brief 取消所有待处理查询
     */
    void cancelAll();
    
    // ========================================================================
    // 生命周期管理
    // ========================================================================
    
    /**
     * @brief 启动查询管理器
     * 
     * 启动超时检查定时器
     */
    void start();
    
    /**
     * @brief 停止查询管理器
     * 
     * 停止定时器，取消所有待处理查询
     */
    void stop();
    
    /**
     * @brief 检查是否正在运行
     */
    bool isRunning() const { return running_.load(); }
    
    // ========================================================================
    // 状态查询
    // ========================================================================
    
    /**
     * @brief 获取当前待处理查询数
     */
    size_t pendingCount() const;
    
    /**
     * @brief 获取统计信息
     */
    QueryManagerStatistics getStatistics() const;
    
    /**
     * @brief 重置统计信息
     */
    void resetStatistics();

private:
    // ========================================================================
    // 内部数据结构
    // ========================================================================
    
    /**
     * @brief 待处理查询
     */
    struct PendingQuery {
        std::string transaction_id;
        DhtNode target;
        DhtMessage message;
        QueryCallback callback;
        
        std::chrono::steady_clock::time_point sent_time;
        int retry_count{0};
        int max_retries;
        std::chrono::milliseconds timeout;
        
        /**
         * @brief 检查是否已超时
         */
        bool isExpired() const {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - sent_time);
            return elapsed >= timeout;
        }
        
        /**
         * @brief 检查是否还能重试
         */
        bool canRetry() const {
            return retry_count < max_retries;
        }
    };
    
    // ========================================================================
    // 内部方法
    // ========================================================================
    
    /**
     * @brief 调度下一次超时检查
     */
    void scheduleTimeoutCheck();
    
    /**
     * @brief 检查超时的查询
     */
    void checkTimeouts();
    
    /**
     * @brief 执行发送
     */
    void doSend(PendingQuery& query);
    
    /**
     * @brief 完成查询（成功或失败）
     * @param tid 事务 ID
     * @param result 结果
     * @note 调用前必须持有锁
     */
    void completeQueryLocked(const std::string& tid, QueryResult result);

private:
    asio::io_context& io_context_;
    std::shared_ptr<network::UdpClient> udp_client_;
    QueryManagerConfig config_;
    
    // 待处理查询
    std::map<std::string, PendingQuery> pending_queries_;
    mutable std::mutex mutex_;
    
    // 超时检查定时器
    asio::steady_timer timeout_timer_;
    std::atomic<bool> running_{false};
    
    // 统计信息
    mutable std::mutex stats_mutex_;
    QueryManagerStatistics statistics_;
};

} // namespace magnet::protocols


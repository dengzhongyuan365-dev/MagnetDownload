// MagnetDownload - Query Manager Implementation
// Manages DHT query lifecycle: timeout, retry, and response matching

#include "magnet/protocols/query_manager.h"
#include "magnet/utils/logger.h"

#include <sstream>

namespace magnet::protocols {

// Helper macros for logging
#define LOG_DEBUG(msg) magnet::utils::Logger::instance().debug(std::string("[QueryManager] ") + msg)
#define LOG_INFO(msg) magnet::utils::Logger::instance().info(std::string("[QueryManager] ") + msg)
#define LOG_WARN(msg) magnet::utils::Logger::instance().warn(std::string("[QueryManager] ") + msg)

// ============================================================================
// Constructor / Destructor
// ============================================================================

QueryManager::QueryManager(asio::io_context& io_context,
                           std::shared_ptr<network::UdpClient> udp_client,
                           QueryManagerConfig config)
    : io_context_(io_context)
    , udp_client_(std::move(udp_client))
    , config_(config)
    , timeout_timer_(io_context)
    , running_(false)
{
    LOG_INFO("QueryManager created");
}

QueryManager::~QueryManager() {
    LOG_DEBUG("QueryManager destructor called");
    stop();
}

// ============================================================================
// Lifecycle Management
// ============================================================================

void QueryManager::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        LOG_WARN("QueryManager already running");
        return;
    }
    
    LOG_INFO("QueryManager started");
    scheduleTimeoutCheck();
}

void QueryManager::stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        return;  // Already stopped
    }
    
    LOG_INFO("QueryManager stopping");
    
    // Cancel timer
    timeout_timer_.cancel();
    
    // Cancel all pending queries
    cancelAll();
    
    LOG_INFO("QueryManager stopped");
}

// ============================================================================
// Core Methods
// ============================================================================

void QueryManager::sendQuery(const DhtNode& target,
                              DhtMessage message,
                              QueryCallback callback,
                              std::chrono::milliseconds timeout,
                              int max_retries) {
    if (!running_) {
        LOG_WARN("QueryManager not running, rejecting query");
        if (callback) {
            asio::post(io_context_, [callback]() {
                callback(QueryResult::err(QueryError::ShuttingDown));
            });
        }
        return;
    }
    
    if (!callback) {
        LOG_WARN("Query callback is null, ignoring");
        return;
    }
    
    // Use defaults if not specified
    if (timeout.count() == 0) {
        timeout = config_.default_timeout;
    }
    if (max_retries < 0) {
        max_retries = config_.default_max_retries;
    }
    
    std::string tid = message.transactionId();
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Check queue limit
        if (pending_queries_.size() >= config_.max_pending_queries) {
            LOG_WARN("Pending query queue full, rejecting query");
            asio::post(io_context_, [callback]() {
                callback(QueryResult::err(QueryError::QueueFull));
            });
            return;
        }
        
        // Check for duplicate transaction ID (very unlikely with random IDs)
        if (pending_queries_.find(tid) != pending_queries_.end()) {
            LOG_WARN("Duplicate transaction ID: " + tid);
            // Generate new ID
            message.setTransactionId(DhtMessage::generateTransactionId());
            tid = message.transactionId();
        }
        
        // Create pending query
        PendingQuery query;
        query.transaction_id = tid;
        query.target = target;
        query.message = std::move(message);
        query.callback = std::move(callback);
        query.sent_time = std::chrono::steady_clock::now();
        query.retry_count = 0;
        query.max_retries = max_retries;
        query.timeout = timeout;
        
        // Send the query
        doSend(query);
        
        // Store in pending map
        pending_queries_[tid] = std::move(query);
    }
    
    // Update statistics
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        statistics_.queries_sent++;
        statistics_.current_pending = pending_queries_.size();
    }
    
    std::ostringstream oss;
    oss << "Query sent to " << target.ip_ << ":" << target.port_ 
        << ", tid=" << tid.size() << " bytes";
    LOG_DEBUG(oss.str());
}

bool QueryManager::handleResponse(const DhtMessage& response) {
    const std::string& tid = response.transactionId();
    
    QueryCallback callback;
    std::chrono::steady_clock::time_point sent_time;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = pending_queries_.find(tid);
        if (it == pending_queries_.end()) {
            // No matching query - could be expired or unsolicited
            LOG_DEBUG("No pending query for transaction ID");
            return false;
        }
        
        // Extract callback and timing info
        callback = std::move(it->second.callback);
        sent_time = it->second.sent_time;
        
        // Remove from pending
        pending_queries_.erase(it);
    }
    
    // Calculate latency
    auto now = std::chrono::steady_clock::now();
    auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(now - sent_time);
    
    // Update statistics
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        statistics_.queries_succeeded++;
        statistics_.total_latency_ms += latency.count();
        statistics_.current_pending = pending_queries_.size();
    }
    
    std::ostringstream oss;
    oss << "Query succeeded, latency=" << latency.count() << "ms";
    LOG_DEBUG(oss.str());
    
    // Call callback (outside lock to avoid deadlock)
    if (callback) {
        callback(QueryResult::ok(response));
    }
    
    return true;
}

bool QueryManager::cancelQuery(const std::string& transaction_id) {
    QueryCallback callback;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = pending_queries_.find(transaction_id);
        if (it == pending_queries_.end()) {
            return false;
        }
        
        callback = std::move(it->second.callback);
        pending_queries_.erase(it);
    }
    
    // Update statistics
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        statistics_.queries_failed++;
        statistics_.current_pending = pending_queries_.size();
    }
    
    LOG_DEBUG("Query cancelled");
    
    // Notify callback
    if (callback) {
        callback(QueryResult::err(QueryError::Cancelled));
    }
    
    return true;
}

void QueryManager::cancelAll() {
    std::map<std::string, PendingQuery> queries_to_cancel;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queries_to_cancel = std::move(pending_queries_);
        pending_queries_.clear();
    }
    
    size_t count = queries_to_cancel.size();
    
    // Update statistics
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        statistics_.queries_failed += count;
        statistics_.current_pending = 0;
    }
    
    if (count > 0) {
        std::ostringstream oss;
        oss << "Cancelling " << count << " pending queries";
        LOG_INFO(oss.str());
    }
    
    // Notify all callbacks
    QueryError error = running_ ? QueryError::Cancelled : QueryError::ShuttingDown;
    for (auto& [tid, query] : queries_to_cancel) {
        if (query.callback) {
            query.callback(QueryResult::err(error));
        }
    }
}

// ============================================================================
// Status Query
// ============================================================================

size_t QueryManager::pendingCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_queries_.size();
}

QueryManagerStatistics QueryManager::getStatistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    QueryManagerStatistics stats = statistics_;
    stats.current_pending = pending_queries_.size();
    return stats;
}

void QueryManager::resetStatistics() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    statistics_.reset();
    LOG_DEBUG("Statistics reset");
}

// ============================================================================
// Internal Methods
// ============================================================================

void QueryManager::scheduleTimeoutCheck() {
    if (!running_) {
        return;
    }
    
    timeout_timer_.expires_after(config_.check_interval);
    
    auto self = shared_from_this();
    timeout_timer_.async_wait([this, self](const asio::error_code& ec) {
        if (ec == asio::error::operation_aborted) {
            return;  // Timer cancelled
        }
        
        if (!running_) {
            return;
        }
        
        checkTimeouts();
        scheduleTimeoutCheck();
    });
}

void QueryManager::checkTimeouts() {
    std::vector<std::pair<std::string, QueryCallback>> expired_queries;
    std::vector<std::string> queries_to_retry;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        for (auto& [tid, query] : pending_queries_) {
            if (query.isExpired()) {
                if (query.canRetry()) {
                    // Mark for retry
                    queries_to_retry.push_back(tid);
                } else {
                    // Mark for failure
                    expired_queries.emplace_back(tid, std::move(query.callback));
                }
            }
        }
        
        // Process retries
        for (const auto& tid : queries_to_retry) {
            auto it = pending_queries_.find(tid);
            if (it != pending_queries_.end()) {
                it->second.retry_count++;
                it->second.sent_time = std::chrono::steady_clock::now();
                doSend(it->second);
                
                std::ostringstream oss;
                oss << "Retrying query (attempt " << it->second.retry_count + 1 
                    << "/" << it->second.max_retries + 1 << ")";
                LOG_DEBUG(oss.str());
            }
        }
        
        // Remove expired queries
        for (const auto& [tid, _] : expired_queries) {
            pending_queries_.erase(tid);
        }
    }
    
    // Update statistics
    if (!queries_to_retry.empty() || !expired_queries.empty()) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        statistics_.retries_total += queries_to_retry.size();
        statistics_.queries_timeout += expired_queries.size();
        statistics_.queries_failed += expired_queries.size();
        statistics_.current_pending = pending_queries_.size();
    }
    
    // Notify failed callbacks (outside lock)
    for (auto& [tid, callback] : expired_queries) {
        if (callback) {
            LOG_DEBUG("Query timeout, no more retries");
            callback(QueryResult::err(QueryError::Timeout));
        }
    }
}

void QueryManager::doSend(PendingQuery& query) {
    if (!udp_client_) {
        LOG_WARN("UDP client not available");
        return;
    }
    
    // Encode message
    std::vector<uint8_t> data = query.message.encode();
    
    // Create endpoint
    network::UdpEndpoint endpoint(query.target.ip_, query.target.port_);
    
    // Send (fire and forget for now - UDP is unreliable anyway)
    udp_client_->send(endpoint, data, nullptr);
}

void QueryManager::completeQueryLocked(const std::string& tid, QueryResult result) {
    auto it = pending_queries_.find(tid);
    if (it == pending_queries_.end()) {
        return;
    }
    
    QueryCallback callback = std::move(it->second.callback);
    pending_queries_.erase(it);
    
    // Note: callback should be called outside the lock
    // This method is kept for potential future use
    if (callback) {
        callback(std::move(result));
    }
}

} // namespace magnet::protocols


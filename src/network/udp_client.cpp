// MagnetDownload - UDP Client Implementation
// Asynchronous UDP communication for DHT protocol

#include "magnet/network/udp_client.h"
#include "magnet/utils/logger.h"

#include <stdexcept>
#include <sstream>

namespace magnet::network {

// Helper macro for logging
#define LOG_DEBUG(msg) magnet::utils::Logger::instance().debug(std::string("[UdpClient] ") + msg)
#define LOG_INFO(msg) magnet::utils::Logger::instance().info(std::string("[UdpClient] ") + msg)
#define LOG_WARN(msg) magnet::utils::Logger::instance().warn(std::string("[UdpClient] ") + msg)
#define LOG_ERROR(msg) magnet::utils::Logger::instance().error(std::string("[UdpClient] ") + msg)

// ============================================================================
// Constructor / Destructor
// ============================================================================

UdpClient::UdpClient(asio::io_context& io_context, uint16_t local_port)
    : io_context_(io_context)
    , socket_(io_context)
    , receiving_(false)
    , receive_callback_(nullptr)
    , receive_buffer_()
    , remote_endpoint_()
    , statistics_()
{
    asio::error_code ec;
    
    // Open socket for IPv4 UDP
    socket_.open(asio::ip::udp::v4(), ec);
    if (ec) {
        LOG_ERROR("Failed to open UDP socket: " + ec.message());
        throw std::runtime_error("Failed to open UDP socket: " + ec.message());
    }
    
    // Set socket options
    // Allow address reuse (useful for quick restarts)
    socket_.set_option(asio::socket_base::reuse_address(true), ec);
    if (ec) {
        LOG_WARN("Failed to set reuse_address option: " + ec.message());
        // Non-fatal, continue
    }
    
    // Bind to local port (0 = system assigns a free port)
    asio::ip::udp::endpoint local_endpoint(asio::ip::udp::v4(), local_port);
    socket_.bind(local_endpoint, ec);
    if (ec) {
        std::ostringstream oss;
        oss << "Failed to bind UDP socket to port " << local_port << ": " << ec.message();
        LOG_ERROR(oss.str());
        throw std::runtime_error("Failed to bind UDP socket: " + ec.message());
    }
    
    std::ostringstream oss;
    oss << "UdpClient created, listening on port " << socket_.local_endpoint().port();
    LOG_INFO(oss.str());
}

UdpClient::~UdpClient() {
    LOG_DEBUG("UdpClient destructor called");
    close();
}

// ============================================================================
// Send
// ============================================================================

void UdpClient::send(const UdpEndpoint& endpoint, 
                     const std::vector<uint8_t>& data, 
                     SendCallback callback) {
    // Validate input
    if (!endpoint.isValid()) {
        LOG_WARN("Invalid endpoint: " + endpoint.toString());
        if (callback) {
            // Post callback to io_context to ensure it's called asynchronously
            asio::post(io_context_, [callback]() {
                callback(asio::error::invalid_argument, 0);
            });
        }
        updateSendStats(0, false);
        return;
    }
    
    if (data.empty()) {
        LOG_DEBUG("Sending empty data to " + endpoint.toString());
    }
    
    if (!socket_.is_open()) {
        LOG_WARN("Socket is closed, cannot send");
        if (callback) {
            asio::post(io_context_, [callback]() {
                callback(asio::error::bad_descriptor, 0);
            });
        }
        updateSendStats(0, false);
        return;
    }
    
    // Resolve endpoint (may involve DNS lookup for domain names)
    asio::ip::udp::endpoint target;
    try {
        target = resolveEndpoint(endpoint);
    } catch (const std::exception& e) {
        LOG_WARN("Failed to resolve endpoint " + endpoint.ip + ": " + e.what());
        if (callback) {
            asio::post(io_context_, [callback]() {
                callback(asio::error::host_not_found, 0);
            });
        }
        updateSendStats(0, false);
        return;
    }
    
    // Copy data for async operation (original may go out of scope)
    auto data_copy = std::make_shared<std::vector<uint8_t>>(data);
    
    // Capture self to extend lifetime during async operation
    auto self = shared_from_this();
    
    std::ostringstream oss;
    oss << "Sending " << data.size() << " bytes to " << endpoint.toString();
    LOG_DEBUG(oss.str());
    
    socket_.async_send_to(
        asio::buffer(*data_copy),
        target,
        [this, self, data_copy, callback](const asio::error_code& ec, size_t bytes_sent) {
            if (ec) {
                LOG_WARN("Send failed: " + ec.message());
                updateSendStats(0, false);
            } else {
                std::ostringstream oss;
                oss << "Sent " << bytes_sent << " bytes successfully";
                LOG_DEBUG(oss.str());
                updateSendStats(bytes_sent, true);
            }
            
            if (callback) {
                callback(ec, bytes_sent);
            }
        }
    );
}

// ============================================================================
// Receive
// ============================================================================

void UdpClient::startReceive(ReceiveCallback callback) {
    // Check if already receiving
    bool expected = false;
    if (!receiving_.compare_exchange_strong(expected, true)) {
        LOG_WARN("Already receiving, call stopReceive() first");
        throw std::runtime_error("Already receiving, call stopReceive() first");
    }
    
    if (!callback) {
        receiving_ = false;
        throw std::invalid_argument("Receive callback cannot be null");
    }
    
    receive_callback_ = callback;
    
    std::ostringstream oss;
    oss << "Started receiving on port " << localPort();
    LOG_INFO(oss.str());
    
    // Start async receive loop
    doReceive();
}

void UdpClient::stopReceive() {
    bool expected = true;
    if (receiving_.compare_exchange_strong(expected, false)) {
        LOG_INFO("Stopping receive");
        
        // Cancel pending receive operation
        asio::error_code ec;
        socket_.cancel(ec);
        if (ec) {
            LOG_DEBUG("Cancel returned: " + ec.message());
            // This is usually fine, might just mean no pending operations
        }
        
        receive_callback_ = nullptr;
    }
}

void UdpClient::doReceive() {
    if (!receiving_ || !socket_.is_open()) {
        return;
    }
    
    // Capture self to extend lifetime during async operation
    auto self = shared_from_this();
    
    socket_.async_receive_from(
        asio::buffer(receive_buffer_),
        remote_endpoint_,
        [this, self](const asio::error_code& ec, size_t bytes_received) {
            handleReceive(ec, bytes_received);
        }
    );
}

void UdpClient::handleReceive(const asio::error_code& ec, size_t bytes_received) {
    // Check if we should continue receiving
    if (!receiving_) {
        LOG_DEBUG("Receive stopped, not processing");
        return;
    }
    
    if (ec) {
        if (ec == asio::error::operation_aborted) {
            // Normal cancellation, don't log as error
            LOG_DEBUG("Receive operation cancelled");
            return;
        }
        
        LOG_WARN("Receive error: " + ec.message());
        updateReceiveStats(0, false);
        
        // Continue receiving despite error (network errors are often transient)
        if (receiving_ && socket_.is_open()) {
            doReceive();
        }
        return;
    }
    
    std::ostringstream oss;
    oss << "Received " << bytes_received << " bytes from " 
        << remote_endpoint_.address().to_string() << ":" << remote_endpoint_.port();
    LOG_DEBUG(oss.str());
    
    updateReceiveStats(bytes_received, true);
    
    // Construct UdpMessage
    UdpMessage message;
    message.data.assign(receive_buffer_.begin(), receive_buffer_.begin() + bytes_received);
    message.remote_endpoint.ip = remote_endpoint_.address().to_string();
    message.remote_endpoint.port = remote_endpoint_.port();
    
    // Call user callback
    if (receive_callback_) {
        try {
            receive_callback_(message);
        } catch (const std::exception& e) {
            LOG_WARN("Exception in receive callback: " + std::string(e.what()));
        }
    }
    
    // Continue receiving
    if (receiving_ && socket_.is_open()) {
        doReceive();
    }
}

// ============================================================================
// Close
// ============================================================================

void UdpClient::close() {
    LOG_DEBUG("Closing UdpClient");
    
    // Stop receiving first
    stopReceive();
    
    // Close socket
    if (socket_.is_open()) {
        asio::error_code ec;
        socket_.shutdown(asio::socket_base::shutdown_both, ec);
        // Ignore shutdown errors, socket might not be connected
        
        socket_.close(ec);
        if (ec) {
            LOG_WARN("Error closing socket: " + ec.message());
        }
    }
    
    LOG_INFO("UdpClient closed");
}

// ============================================================================
// Status & Statistics
// ============================================================================

bool UdpClient::isReceiving() const {
    return receiving_.load();
}

UdpClient::Statistics UdpClient::getStatistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return statistics_;
}

void UdpClient::resetStatistics() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    statistics_.reset();
    LOG_DEBUG("Statistics reset");
}

void UdpClient::updateSendStats(size_t bytes, bool success) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    if (success) {
        statistics_.bytes_sent += bytes;
        statistics_.messages_sent++;
    } else {
        statistics_.send_errors++;
    }
}

void UdpClient::updateReceiveStats(size_t bytes, bool success) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    if (success) {
        statistics_.bytes_received += bytes;
        statistics_.messages_received++;
    } else {
        statistics_.receive_errors++;
    }
}

// ============================================================================
// Endpoint Resolution
// ============================================================================

asio::ip::udp::endpoint UdpClient::resolveEndpoint(const UdpEndpoint& endpoint) {
    asio::error_code ec;
    
    // First, try to parse as IP address directly
    auto address = asio::ip::make_address(endpoint.ip, ec);
    if (!ec) {
        // Successfully parsed as IP address
        return asio::ip::udp::endpoint(address, endpoint.port);
    }
    
    // If not an IP, try DNS resolution
    LOG_DEBUG("Resolving hostname: " + endpoint.ip);
    
    asio::ip::udp::resolver resolver(io_context_);
    auto results = resolver.resolve(
        asio::ip::udp::v4(),
        endpoint.ip,
        std::to_string(endpoint.port),
        ec
    );
    
    if (ec) {
        LOG_WARN("DNS resolution failed for " + endpoint.ip + ": " + ec.message());
        throw std::runtime_error("DNS resolution failed: " + ec.message());
    }
    
    if (results.empty()) {
        throw std::runtime_error("No addresses found for hostname: " + endpoint.ip);
    }
    
    // Return first result
    auto resolved = results.begin()->endpoint();
    LOG_DEBUG("Resolved " + endpoint.ip + " to " + resolved.address().to_string());
    
    return resolved;
}

} // namespace magnet::network

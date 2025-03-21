#include "network.h"
#include "logger.h"
#include <chrono>
#include <functional>

namespace bt {

// Constants for network operations
constexpr size_t BUFFER_SIZE = 8192;
constexpr auto CONNECT_TIMEOUT = std::chrono::seconds(10);

//
// AsioEndpoint Implementation
//

AsioEndpoint::AsioEndpoint(std::string ip, uint16_t port) 
    : ip_(std::move(ip)), port_(port) {
}

AsioEndpoint::AsioEndpoint(const asio::ip::tcp::endpoint& endpoint)
    : ip_(endpoint.address().to_string()), port_(endpoint.port()) {
}

AsioEndpoint::AsioEndpoint(const asio::ip::udp::endpoint& endpoint)
    : ip_(endpoint.address().to_string()), port_(endpoint.port()) {
}

std::string AsioEndpoint::getIP() const {
    return ip_;
}

uint16_t AsioEndpoint::getPort() const {
    return port_;
}

bool AsioEndpoint::operator==(const Endpoint& other) const {
    if (const auto* asioEndpoint = dynamic_cast<const AsioEndpoint*>(&other)) {
        return ip_ == asioEndpoint->ip_ && port_ == asioEndpoint->port_;
    }
    return false;
}

asio::ip::tcp::endpoint AsioEndpoint::getTCPEndpoint() const {
    asio::error_code ec;
    auto address = asio::ip::make_address(ip_, ec);
    if (ec) {
        // If it's not an IP address, try to resolve hostname
        asio::io_context io_context;
        asio::ip::tcp::resolver resolver(io_context);
        auto results = resolver.resolve(ip_, std::to_string(port_), ec);
        if (ec || results.empty()) {
            throw std::runtime_error("Failed to resolve hostname: " + ip_);
        }
        return *results.begin();
    }
    return asio::ip::tcp::endpoint(address, port_);
}

asio::ip::udp::endpoint AsioEndpoint::getUDPEndpoint() const {
    asio::error_code ec;
    auto address = asio::ip::make_address(ip_, ec);
    if (ec) {
        // If it's not an IP address, try to resolve hostname
        asio::io_context io_context;
        asio::ip::udp::resolver resolver(io_context);
        auto results = resolver.resolve(ip_, std::to_string(port_), ec);
        if (ec || results.empty()) {
            throw std::runtime_error("Failed to resolve hostname: " + ip_);
        }
        return *results.begin();
    }
    return asio::ip::udp::endpoint(address, port_);
}

//
// AsioTCPConnection Implementation
//

AsioTCPConnection::AsioTCPConnection(asio::ip::tcp::socket socket)
    : socket_(std::move(socket)), 
      connected_(true), 
      readBuffer_(BUFFER_SIZE),
      writing_(false) {
    
    // Start reading data as soon as the connection is established
    startRead();
}

AsioTCPConnection::~AsioTCPConnection() {
    close();
}

void AsioTCPConnection::send(ByteSpan data) {
    if (!connected_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Create a copy of the data to be sent
    std::vector<std::byte> dataCopy(data.begin(), data.end());
    
    // Queue the data for sending
    writeQueue_.push(std::move(dataCopy));
    
    // If we're not currently writing, start the write operation
    if (!writing_) {
        writing_ = true;
        
        // Get a shared pointer to this to prevent destruction during async operation
        auto self = shared_from_this();
        
        // Post the write operation to the socket's io_context to ensure thread safety
        asio::post(socket_.get_executor(), [this, self]() {
            const auto& data = writeQueue_.front();
            
            socket_.async_write_some(
                asio::buffer(data.data(), data.size()),
                [this, self](const asio::error_code& error, std::size_t bytesTransferred) {
                    handleWrite(error, bytesTransferred);
                }
            );
        });
    }
}

void AsioTCPConnection::registerDataHandler(std::function<void(ByteSpan)> handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    dataHandler_ = std::move(handler);
}

void AsioTCPConnection::registerCloseHandler(std::function<void()> handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    closeHandler_ = std::move(handler);
}

void AsioTCPConnection::close() {
    if (!connected_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    connected_ = false;
    
    // Close the socket
    asio::error_code ec;
    socket_.close(ec);
    
    // Call close handler if registered
    if (closeHandler_) {
        auto handler = closeHandler_;
        // Release the lock before calling the handler to avoid deadlocks
        mutex_.unlock();
        handler();
        mutex_.lock();
    }
}

Endpoint AsioTCPConnection::getRemoteEndpoint() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!connected_) {
        throw std::runtime_error("Connection is closed");
    }
    
    asio::error_code ec;
    auto endpoint = socket_.remote_endpoint(ec);
    if (ec) {
        throw std::runtime_error("Failed to get remote endpoint: " + ec.message());
    }
    
    return AsioEndpoint(endpoint);
}

bool AsioTCPConnection::isConnected() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return connected_;
}

void AsioTCPConnection::startRead() {
    // Get a shared pointer to this to prevent destruction during async operation
    auto self = shared_from_this();
    
    socket_.async_read_some(
        asio::buffer(readBuffer_.data(), readBuffer_.size()),
        [this, self](const asio::error_code& error, std::size_t bytesTransferred) {
            handleRead(error, bytesTransferred);
        }
    );
}

void AsioTCPConnection::handleRead(const asio::error_code& error, std::size_t bytesTransferred) {
    if (error) {
        // Connection closed or error occurred
        close();
        return;
    }
    
    // Call the data handler if registered
    if (dataHandler_) {
        ByteSpan dataSpan(readBuffer_.data(), bytesTransferred);
        
        // Create a copy of the handler to avoid issues if it changes during the callback
        std::function<void(ByteSpan)> handler;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            handler = dataHandler_;
        }
        
        if (handler) {
            handler(dataSpan);
        }
    }
    
    // Continue reading if still connected
    if (connected_) {
        startRead();
    }
}

void AsioTCPConnection::handleWrite(const asio::error_code& error, std::size_t bytesTransferred) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (error) {
        // Error occurred, close the connection
        writing_ = false;
        connected_ = false;
        
        asio::error_code ec;
        socket_.close(ec);
        
        if (closeHandler_) {
            auto handler = closeHandler_;
            // Release the lock before calling the handler to avoid deadlocks
            mutex_.unlock();
            handler();
            mutex_.lock();
        }
        
        return;
    }
    
    auto& currentData = writeQueue_.front();
    
    // If we sent less than the full buffer, adjust the remaining data
    if (bytesTransferred < currentData.size()) {
        // Create a new buffer with the remaining data
        std::vector<std::byte> remainingData(
            currentData.begin() + static_cast<ptrdiff_t>(bytesTransferred),
            currentData.end()
        );
        
        // Replace the current buffer with the remaining data
        writeQueue_.pop();
        writeQueue_.push(std::move(remainingData));
    } else {
        // All data sent, remove from queue
        writeQueue_.pop();
    }
    
    // If there's more data to send, continue sending
    if (!writeQueue_.empty()) {
        // Get a shared pointer to this to prevent destruction during async operation
        auto self = shared_from_this();
        
        const auto& nextData = writeQueue_.front();
        socket_.async_write_some(
            asio::buffer(nextData.data(), nextData.size()),
            [this, self](const asio::error_code& error, std::size_t bytesTransferred) {
                handleWrite(error, bytesTransferred);
            }
        );
    } else {
        // No more data to send
        writing_ = false;
    }
}

//
// NetworkManager Implementation
//

NetworkManager::NetworkManager()
    : workGuard_(std::make_unique<asio::io_context::work>(ioContext_)),
      udpReceiveBuffer_(BUFFER_SIZE) {
    
    // Start the IO thread
    ioThread_ = std::thread([this]() { runIOContext(); });
}

NetworkManager::~NetworkManager() {
    stop();
}

void NetworkManager::stop() {
    // Cancel all pending operations and stop the IO thread
    if (workGuard_) {
        workGuard_.reset();
        
        if (udpSocket_) {
            asio::error_code ec;
            udpSocket_->close(ec);
        }
        
        if (tcpAcceptor_) {
            asio::error_code ec;
            tcpAcceptor_->close(ec);
        }
        
        if (ioThread_.joinable()) {
            ioThread_.join();
        }
    }
}

void NetworkManager::sendUDP(ByteSpan data, const Endpoint& endpoint) {
    std::lock_guard<std::mutex> lock(udpMutex_);
    
    if (!udpSocket_ || !udpSocket_->is_open()) {
        throw std::runtime_error("UDP socket not open");
    }
    
    // Cast to AsioEndpoint to get the native endpoint
    const auto* asioEndpoint = dynamic_cast<const AsioEndpoint*>(&endpoint);
    if (!asioEndpoint) {
        throw std::invalid_argument("Invalid endpoint type");
    }
    
    udpSocket_->async_send_to(
        asio::buffer(data.data(), data.size()),
        asioEndpoint->getUDPEndpoint(),
        [](const asio::error_code& error, std::size_t /*bytesTransferred*/) {
            if (error) {
                // Log the error but don't throw from an async callback
                // A proper logger would be used here
                std::cerr << "UDP send error: " << error.message() << std::endl;
            }
        }
    );
}

void NetworkManager::listenUDP(uint16_t port) {
    std::lock_guard<std::mutex> lock(udpMutex_);
    
    if (udpSocket_ && udpSocket_->is_open()) {
        // Close existing socket
        asio::error_code ec;
        udpSocket_->close(ec);
    }
    
    // Create a new UDP socket
    udpSocket_ = std::make_unique<asio::ip::udp::socket>(ioContext_);
    
    asio::error_code ec;
    udpSocket_->open(asio::ip::udp::v4(), ec);
    if (ec) {
        throw std::runtime_error("Failed to open UDP socket: " + ec.message());
    }
    
    // Enable broadcast
    udpSocket_->set_option(asio::socket_base::broadcast(true), ec);
    if (ec) {
        throw std::runtime_error("Failed to set UDP broadcast option: " + ec.message());
    }
    
    // Allow address reuse
    udpSocket_->set_option(asio::socket_base::reuse_address(true), ec);
    if (ec) {
        throw std::runtime_error("Failed to set UDP reuse_address option: " + ec.message());
    }
    
    // Bind to the specified port
    udpSocket_->bind(asio::ip::udp::endpoint(asio::ip::address_v4::any(), port), ec);
    if (ec) {
        throw std::runtime_error("Failed to bind UDP socket: " + ec.message());
    }
    
    // Start receiving UDP data
    startReceiveUDP();
}

void NetworkManager::registerUDPDataHandler(std::function<void(ByteSpan, const Endpoint&)> handler) {
    std::lock_guard<std::mutex> lock(udpMutex_);
    udpDataHandler_ = std::move(handler);
}

std::shared_ptr<ITCPConnection> NetworkManager::connectTCP(const Endpoint& endpoint) {
    // Cast to AsioEndpoint to get the native endpoint
    const auto* asioEndpoint = dynamic_cast<const AsioEndpoint*>(&endpoint);
    if (!asioEndpoint) {
        throw std::invalid_argument("Invalid endpoint type");
    }
    
    // Create a new socket
    asio::ip::tcp::socket socket(ioContext_);
    
    // Connect to the endpoint with timeout
    asio::error_code ec;
    socket.open(asio::ip::tcp::v4(), ec);
    if (ec) {
        throw std::runtime_error("Failed to open TCP socket: " + ec.message());
    }
    
    // Set non-blocking mode for timeout support
    socket.non_blocking(true, ec);
    if (ec) {
        throw std::runtime_error("Failed to set non-blocking mode: " + ec.message());
    }
    
    // Start async connect
    socket.async_connect(
        asioEndpoint->getTCPEndpoint(),
        [](const asio::error_code& /*error*/) {
            // This callback is just for the timeout mechanism
        }
    );
    
    // Wait for connect with timeout
    asio::io_context connectContext;
    asio::steady_timer timer(connectContext, CONNECT_TIMEOUT);
    
    bool connectCompleted = false;
    bool timedOut = false;
    
    // Set callbacks for async operations
    timer.async_wait([&](const asio::error_code& error) {
        if (!error && !connectCompleted) {
            timedOut = true;
            socket.close();
        }
    });
    
    // Poll until either connected or timed out
    while (!connectCompleted && !timedOut) {
        connectContext.poll();
        
        // Check if connected
        ec.clear();
        socket.non_blocking(false, ec);
        if (!ec) {
            asio::ip::tcp::endpoint unused;
            socket.remote_endpoint(unused, ec);
            if (!ec) {
                connectCompleted = true;
                break;
            }
        }
        
        // Sleep a bit to avoid busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    if (timedOut) {
        throw std::runtime_error("Connection timed out");
    }
    
    if (!connectCompleted) {
        throw std::runtime_error("Connection failed");
    }
    
    // Create and return the connection object
    return std::make_shared<AsioTCPConnection>(std::move(socket));
}

void NetworkManager::listenTCP(uint16_t port) {
    std::lock_guard<std::mutex> lock(tcpMutex_);
    
    if (tcpAcceptor_ && tcpAcceptor_->is_open()) {
        // Close existing acceptor
        asio::error_code ec;
        tcpAcceptor_->close(ec);
    }
    
    // Create a new TCP acceptor
    tcpAcceptor_ = std::make_unique<asio::ip::tcp::acceptor>(
        ioContext_,
        asio::ip::tcp::endpoint(asio::ip::address_v4::any(), port)
    );
    
    // Start accepting connections
    startAcceptTCP();
}

void NetworkManager::registerTCPConnectionHandler(std::function<void(std::shared_ptr<ITCPConnection>)> handler) {
    std::lock_guard<std::mutex> lock(tcpMutex_);
    tcpConnectionHandler_ = std::move(handler);
}

void NetworkManager::startReceiveUDP() {
    if (!udpSocket_ || !udpSocket_->is_open()) {
        return;
    }
    
    udpSocket_->async_receive_from(
        asio::buffer(udpReceiveBuffer_.data(), udpReceiveBuffer_.size()),
        udpSenderEndpoint_,
        [this](const asio::error_code& error, std::size_t bytesTransferred) {
            handleReceiveUDP(error, bytesTransferred);
        }
    );
}

void NetworkManager::handleReceiveUDP(const asio::error_code& error, std::size_t bytesTransferred) {
    if (!error && bytesTransferred > 0) {
        // Call the data handler if registered
        if (udpDataHandler_) {
            ByteSpan dataSpan(udpReceiveBuffer_.data(), bytesTransferred);
            AsioEndpoint senderEndpoint(udpSenderEndpoint_);
            
            // Create a copy of the handler to avoid issues if it changes during the callback
            std::function<void(ByteSpan, const Endpoint&)> handler;
            {
                std::lock_guard<std::mutex> lock(udpMutex_);
                handler = udpDataHandler_;
            }
            
            if (handler) {
                handler(dataSpan, senderEndpoint);
            }
        }
    }
    
    // Continue receiving UDP data
    startReceiveUDP();
}

void NetworkManager::startAcceptTCP() {
    if (!tcpAcceptor_ || !tcpAcceptor_->is_open()) {
        return;
    }
    
    // Create a new socket for the incoming connection
    auto socket = std::make_shared<asio::ip::tcp::socket>(ioContext_);
    
    tcpAcceptor_->async_accept(
        *socket,
        [this, socket](const asio::error_code& error) {
            if (!error) {
                // Create a connection object for the accepted socket
                auto connection = std::make_shared<AsioTCPConnection>(std::move(*socket));
                
                // Call the connection handler if registered
                if (tcpConnectionHandler_) {
                    // Create a copy of the handler to avoid issues if it changes during the callback
                    std::function<void(std::shared_ptr<ITCPConnection>)> handler;
                    {
                        std::lock_guard<std::mutex> lock(tcpMutex_);
                        handler = tcpConnectionHandler_;
                    }
                    
                    if (handler) {
                        handler(connection);
                    }
                }
            }
            
            // Continue accepting connections
            startAcceptTCP();
        }
    );
}

void NetworkManager::runIOContext() {
    try {
        ioContext_.run();
    } catch (const std::exception& e) {
        // Log the error
        std::cerr << "Exception in IO thread: " << e.what() << std::endl;
    }
}

} // namespace bt 
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/steady_timer.hpp>

#include <iostream>

int main() {
    asio::io_context io;
    asio::steady_timer timer(io, asio::chrono::seconds(1));
    
    timer.async_wait([](const std::error_code& error) {
        if (!error) {
            std::cout << "Timer expired!" << std::endl;
        }
    });
    
    io.run();
}
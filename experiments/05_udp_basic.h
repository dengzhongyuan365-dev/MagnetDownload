#pragma once
#include <asio.hpp>
#include <iostream>
#include <array>
#include <string>


class UdpClient {
private:

    asio::io_context& io_context_;
    asio::ip::udp::socket socket_;
    std::array<char, 1024> receive_buffer_;
    asio::ip::udp::endpoint sender_endpoint_;

public:
    UdpClient(asio::io_context& io_context)
        : io_context_(io_context)
        , socket_(io_context, asio::ip::udp::v4())
    {

    }

    void send_message(const std::string& host, unsigned short port, const std::string& message)
    {
        asio::ip::udp::resolver resolver(io_context_);
        auto endpoints = resolver.resolve(host, std::to_string(port));
        auto target_endpoint = *endpoints.begin();

        socket_.async_send_to(asio::buffer(message), target_endpoint,[this](const asio::error_code& ec, std::size_t bytes_sent)
                              {
            handle_send(ec,bytes_sent);
        }
                              );
    }


    void start_receive(){
        socket_.async_receive_from(asio::buffer(receive_buffer_),sender_endpoint_,
                                                                      [this](const asio::error_code& ec, std::size_t bytes_received)
                                   {
            handle_receive(ec, bytes_received);
        }
                                   );
    }


private:
    void handle_send(const asio::error_code& ec, std::size_t bytes_sendt)
    {
        if(!ec) {
            std::cout<<"发送成功： "<< bytes_sendt  <<" 字节"<<std::endl;
        } else {
            std::cout<<"发送失败: "<<ec.message() <<std::endl;
        }
    }

    void handle_receive(const asio::error_code& ec, std::size_t bytes_received)
    {
        if (!ec)
        {
            std::string message(receive_buffer_.data(), bytes_received);
            std::cout<<"收到来自： "<<sender_endpoint_<<" 的消息"<<message << std::endl;

            start_receive();
        }
        else
        {
            std::cout<<"接受失败 "<<ec.message()<<std::endl;
        }
    }
};

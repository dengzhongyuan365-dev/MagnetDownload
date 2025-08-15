#pragma once
#include <asio.hpp>
#include <iostream>
#include <chrono>
#include <memory>


class Timerdemo: public std::enable_shared_from_this<Timerdemo> {

private:
    asio::io_context& io_context_;
    asio::steady_timer timer_;
    int counter_;

public:
    Timerdemo(asio::io_context& io_context):io_context_(io_context), timer_(io_context), counter_(0){}

    ~Timerdemo(){
        std::cout<<__FUNCTION__<<std::endl;
    }

    void start()
    {
        schedule_next_timer();
    }
private:
    void schedule_next_timer() {
        ++counter_;
        timer_.expires_after(std::chrono::seconds(1));
        timer_.async_wait([self=shared_from_this()](const asio::error_code& ec){
            self->handle_timer(ec);
        });
    }

    void handle_timer(const asio::error_code& ec) {
        if(!ec) {
            if(counter_ < 3) {
                schedule_next_timer();
            }
        }
    }
};

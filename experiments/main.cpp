/**
 * @file main.cpp
 * @brief Asio学习实验主程序
 * 
 * 使用方法:
 *   ./asio_experiments 01    # 运行实验1
 *   ./asio_experiments 02    # 运行实验2
 *   ...
 */

#include <iostream>
#include <string>
#include <asio.hpp>

// 实验函数声明 - 你需要实现这些函数
int experiment_01_hello_asio();
int experiment_02_work_guard();
int experiment_03_timer_basic();
int experiment_04_lambda_lifetime();
int experiment_05_udp_basic();
int experiment_06_multithreading();

void show_help() {
    std::cout << "=== Asio学习实验程序 ===" << std::endl;
    std::cout << std::endl;
    std::cout << "用法: ./asio_experiments <实验编号>" << std::endl;
    std::cout << std::endl;
    std::cout << "可用的实验:" << std::endl;
    std::cout << "  01 - Hello Asio (io_context基础)" << std::endl;
    std::cout << "  02 - Work Guard (控制io_context生命周期)" << std::endl;
    std::cout << "  03 - Timer Basic (异步定时器)" << std::endl;
    std::cout << "  04 - Lambda Lifetime (对象生命周期管理)" << std::endl;
    std::cout << "  05 - UDP Basic (UDP网络编程基础)" << std::endl;
    std::cout << "  06 - Multithreading (多线程io_context)" << std::endl;
    std::cout << std::endl;
    std::cout << "示例:" << std::endl;
    std::cout << "  ./asio_experiments 01" << std::endl;
    std::cout << "  ./asio_experiments 02" << std::endl;
    std::cout << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        show_help();
        return 1;
    }
    
    std::string experiment = argv[1];
    
    try {
        if (experiment == "01") {
            return experiment_01_hello_asio();
        }
        else if (experiment == "02") {
            return experiment_02_work_guard();
        }
        else if (experiment == "03") {
            return experiment_03_timer_basic();
        }
        else if (experiment == "04") {
            return experiment_04_lambda_lifetime();
        }
        else if (experiment == "05") {
            return experiment_05_udp_basic();
        }
        else if (experiment == "06") {
            return experiment_06_multithreading();
        }
        else {
            std::cout << "错误: 未知的实验编号 '" << experiment << "'" << std::endl;
            std::cout << std::endl;
            show_help();
            return 1;
        }
    }
    catch (const std::exception& e) {
        std::cerr << "实验运行出错: " << e.what() << std::endl;
        return 1;
    }
}

// ============================================================================
// 实验实现区域 - 你需要在这里实现各个实验
// ============================================================================

int experiment_01_hello_asio() {
    std::cout << "=== 实验1: Hello Asio ===" << std::endl;
    std::cout << std::endl;
    
    asio::io_context io_context;

    std::size_t handlers_run = io_context.run();
    
    std::cout << "handlers_run: " << handlers_run << std::endl;
    return 0;
}

#include <thread>
#include <chrono>
int experiment_02_work_guard() {
    std::cout << "=== 实验2: Work Guard ===" << std::endl;
    std::cout << std::endl;
    
    asio::io_context io_context;

    auto work_guard = asio::make_work_guard(io_context);
    

    std::thread io_thread([&io_context](){
        io_context.run();
        std::cout<<"thread runing : "<<std::endl;
    });

    std::this_thread::sleep_for(std::chrono::seconds(10));

    // work_guard.reset();

    io_thread.join();

    return 0;
}



void on_timer_expired(const asio::error_code& ec)
{
    if(!ec) {
            std::cout<<"work thread: "<<std::this_thread::get_id()<<std::endl;
        std::cout<<"success: "<<std::endl;
    } else {
        std::cout<< "failed : "<<std::endl;
    }

}

int experiment_03_timer_basic() {
    std::cout << "=== 实验3: Timer Basic ===" << std::endl;
    std::cout << std::endl;
    
    asio::io_context io_context;
    asio::steady_timer timer(io_context, std::chrono::seconds(5));
    timer.async_wait(on_timer_expired);
    std::cout<<"main thread: "<<std::this_thread::get_id()<<std::endl;;
    std::size_t handlers_run = io_context.run();
    
    return 0;
}
#include "04_lambda_lifetime.h"
int experiment_04_lambda_lifetime() {
    std::cout << "=== 实验4: Lambda Lifetime ===" << std::endl;
    std::cout << std::endl;
    
    asio::io_context io_context;

    auto demo = std::make_shared<Timerdemo>(io_context);

    demo->start();
    io_context.run();

    return 0;
}

#include "05_udp_basic.h"
int experiment_05_udp_basic() {
    std::cout << "=== 实验5: UDP Basic ===" << std::endl;
    std::cout << std::endl;
    
    asio::io_context io_context;
    UdpClient client(io_context);
    client.start_receive();
    client.send_message("127.0.0.1", 8888, "Hello UDP!");
    io_context.run();
    
    return 0;
}

int experiment_06_multithreading() {
    std::cout << "=== 实验6: Multithreading ===" << std::endl;
    std::cout << std::endl;
    
    // TODO: 在这里实现实验6的代码
    
    std::cout << "⚠️  实验6尚未实现，请根据指导文档编写代码" << std::endl;
    std::cout << "📖 请查看 ASIO_EXPERIMENTS.md 获取实现指导" << std::endl;
    
    return 0;
}

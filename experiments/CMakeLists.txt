# Asio学习实验 - 简化版本
# 所有实验代码在一个可执行文件中，通过命令行参数选择不同实验

message(STATUS "Configuring Asio Learning Experiments...")

# 实验主程序
add_executable(asio_experiments
    main.cpp
    04_lambda_lifetime.h
    04_lambda_lifetime.cpp
    05_udp_basic.h
    05_udp_basic.cpp
    06_multithreading.h

    # 当你创建实验源文件时，在这里添加
    # 01_hello_asio.cpp
    # 02_work_guard.cpp  
    # 03_timer_basic.cpp
    # 04_lambda_lifetime.cpp
    # 05_udp_basic.cpp
    # 06_multithreading.cpp
)

# 链接Asio和线程库
target_link_libraries(asio_experiments PRIVATE 
    asio
    Threads::Threads
)

# 设置输出目录
set_target_properties(asio_experiments PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

message(STATUS "Asio experiments configured!")
message(STATUS "")
message(STATUS "构建: cmake --build . --target asio_experiments")
message(STATUS "运行: ./bin/asio_experiments [实验编号]")
message(STATUS "  例如: ./bin/asio_experiments 01")
message(STATUS "        ./bin/asio_experiments 02")
message(STATUS "")

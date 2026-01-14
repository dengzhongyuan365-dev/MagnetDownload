# MagnetDownload 异步编程基础设施
# 提供事件循环、任务调度、定时器等异步功能

message(STATUS "Configuring Async Library...")

add_library(magnet_async STATIC
    event_loop_manager.cpp          # 事件循环管理器
    task_scheduler.cpp              # 增强型任务调度器
    # timer.cpp                     # 你将要实现的
)

target_include_directories(magnet_async
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# 依赖Utils层和Asio
target_link_libraries(magnet_async
    PUBLIC
        magnet_utils                # 需要Logger和Config
        asio                        # 需要Asio异步框架
    PRIVATE
        Threads::Threads            # 线程支持
)

# 设置编译特性
target_compile_features(magnet_async PUBLIC cxx_std_17)

# 编译选项
target_compile_options(magnet_async PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# 导出符号定义
target_compile_definitions(magnet_async
    PUBLIC MAGNET_ASYNC_AVAILABLE
)

message(STATUS "Async library configured!")

# MagnetDownload 网络层
# 提供UDP/TCP通信、DHT客户端、Peer连接等网络功能

message(STATUS "Configuring Network Library...")

add_library(magnet_network STATIC
    udp_client.cpp
    tcp_client.cpp
    # peer_connection.cpp           # 待实现
)

target_include_directories(magnet_network
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# 依赖Utils层和Async层
target_link_libraries(magnet_network
    PUBLIC
        magnet_utils                # 需要Logger、字符串工具
        magnet_async                # 需要事件循环和任务调度
)

# 设置编译特性
target_compile_features(magnet_network PUBLIC cxx_std_17)

# 编译选项
target_compile_options(magnet_network PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# 导出符号定义
target_compile_definitions(magnet_network
    PUBLIC MAGNET_NETWORK_AVAILABLE
)

message(STATUS "Network library configured!")

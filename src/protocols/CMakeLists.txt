# MagnetDownload 协议层
# 提供磁力链接解析、BitTorrent协议、协议适配器等功能

message(STATUS "Configuring Protocols Library...")

add_library(magnet_protocols STATIC
    magnet_uri_parser.cpp
    dch_types.cpp                   
    routing_table.cpp  
)

target_include_directories(magnet_protocols
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# 依赖Utils层和Network层
target_link_libraries(magnet_protocols
    PUBLIC
        magnet_utils                # 需要Logger、字符串工具、哈希工具
        magnet_network              # 需要网络通信能力
        # 注意：这里会自动传递magnet_async依赖
)

# 设置编译特性
target_compile_features(magnet_protocols PUBLIC cxx_std_17)

# 编译选项
target_compile_options(magnet_protocols PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# 导出符号定义
target_compile_definitions(magnet_protocols
    PUBLIC MAGNET_PROTOCOLS_AVAILABLE
)

message(STATUS "Protocols library configured!")

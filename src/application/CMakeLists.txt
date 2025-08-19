# MagnetDownload 主应用程序
# 集成所有模块，提供完整的下载器功能

message(STATUS "Configuring Main Application...")

add_executable(magnetdownload
    main.cpp                        # 主程序入口
    # application.cpp               # 你将要实现的
    # download_manager.cpp          # 你将要实现的
)

target_include_directories(magnetdownload
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# 链接所有静态库（业务核心）
target_link_libraries(magnetdownload
    PRIVATE
        magnet_utils        # 基础工具
        magnet_async        # 异步基础设施
        magnet_network      # 网络功能
        magnet_protocols    # 协议处理
        magnet_storage      # 存储管理
        # UI模块通过动态加载，不在这里静态链接
)

# 设置编译特性
target_compile_features(magnetdownload PRIVATE cxx_std_17)

# 编译选项
target_compile_options(magnetdownload PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# 运行时可选功能的编译定义
if(BUILD_CONSOLE_UI)
    target_compile_definitions(magnetdownload 
        PRIVATE ENABLE_CONSOLE_UI)
endif()

if(BUILD_QT_UI)
    target_compile_definitions(magnetdownload 
        PRIVATE ENABLE_QT_UI)
endif()

# 设置输出目录
set_target_properties(magnetdownload PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

message(STATUS "Main application configured!")
message(STATUS "Output: ${CMAKE_BINARY_DIR}/bin/magnetdownload")

# MagnetDownload 应用层
# 提供下载控制器等业务逻辑

message(STATUS "Configuring Main Application...")

# 1. 创建应用层静态库（供 GUI 使用）
add_library(magnet_application STATIC
    download_controller.cpp
)

target_include_directories(magnet_application
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
)

target_link_libraries(magnet_application
    PUBLIC
        magnet_utils
        magnet_async
        magnet_network
        magnet_protocols
        magnet_storage
)

target_compile_features(magnet_application PUBLIC cxx_std_17)

# 2. 创建命令行可执行文件
add_executable(magnetdownload
    main.cpp
)

target_link_libraries(magnetdownload
    PRIVATE
        magnet_application
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

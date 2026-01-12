# MagnetDownload 存储层
# 提供文件管理、数据块管理、磁盘缓存等存储功能

message(STATUS "Configuring Storage Library...")

add_library(magnet_storage STATIC
    file_manager.cpp
    piece_manager.cpp
)

target_include_directories(magnet_storage
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# 只依赖Utils层（不依赖网络层，保持存储层的独立性）
target_link_libraries(magnet_storage
    PUBLIC
        magnet_utils                # 需要Logger、哈希工具、文件工具
        magnet_async                # 需要异步任务调度（文件IO）
)

# 设置编译特性
target_compile_features(magnet_storage PUBLIC cxx_std_17)

# 编译选项
target_compile_options(magnet_storage PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# 导出符号定义
target_compile_definitions(magnet_storage
    PUBLIC MAGNET_STORAGE_AVAILABLE
)

message(STATUS "Storage library configured!")

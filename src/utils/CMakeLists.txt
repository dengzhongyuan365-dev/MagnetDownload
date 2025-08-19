# MagnetDownload 通用工具库
# 提供日志、配置、字符串处理等基础工具

message(STATUS "Configuring Utils Library...")

add_library(magnet_utils STATIC
    logger.cpp
    # config.cpp                    # 你将要实现的
    # string_utils.cpp              # 你将要实现的  
    # hash_utils.cpp                # 你将要实现的
)

target_include_directories(magnet_utils
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# 只依赖系统库，零业务逻辑
target_link_libraries(magnet_utils
    PRIVATE  
        Threads::Threads            # Logger需要线程支持
)

# 设置编译特性
target_compile_features(magnet_utils PUBLIC cxx_std_17)

# 编译选项
target_compile_options(magnet_utils PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# 导出符号定义
target_compile_definitions(magnet_utils
    PUBLIC MAGNET_UTILS_AVAILABLE
)

message(STATUS "Utils library configured!")

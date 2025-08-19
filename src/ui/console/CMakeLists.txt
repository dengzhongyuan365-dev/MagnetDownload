# MagnetDownload 控制台界面
# 提供命令行界面和控制台交互功能

message(STATUS "Configuring Console UI Library...")

add_library(magnet_ui_console SHARED  # 动态库，支持可选加载
    console_interface.cpp           # 占位实现，你将要完善
    # command_parser.cpp            # 你将要实现的
    # progress_display.cpp          # 你将要实现的
)

target_include_directories(magnet_ui_console
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../../include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# 依赖所有业务层
target_link_libraries(magnet_ui_console
    PRIVATE  # 动态库通常使用PRIVATE链接
        magnet_utils
        magnet_async
        magnet_network
        magnet_protocols
        magnet_storage
)

# 设置编译特性
target_compile_features(magnet_ui_console PUBLIC cxx_std_17)

# 编译选项
target_compile_options(magnet_ui_console PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# 导出符号定义
target_compile_definitions(magnet_ui_console
    PUBLIC MAGNET_UI_CONSOLE_AVAILABLE
    PRIVATE MAGNET_UI_CONSOLE_EXPORTS  # 用于符号导出
)

message(STATUS "Console UI library configured!")

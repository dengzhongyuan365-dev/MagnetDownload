# MagnetDownload Qt图形界面
# 提供现代图形用户界面（可选模块）

if(NOT BUILD_QT_UI)
    message(STATUS "Qt UI disabled, skipping...")
    return()
endif()

message(STATUS "Configuring Qt UI Library...")

# 查找Qt6
find_package(Qt6 COMPONENTS Core Widgets QUIET)
if(NOT Qt6_FOUND)
    message(WARNING "Qt6 not found, skipping Qt UI module")
    return()
endif()

add_library(magnet_ui_qt SHARED  # 动态库，支持可选加载
    # qt_main_window.cpp            # 你将要实现的
    # qt_download_widget.cpp        # 你将要实现的
    # qt_settings_dialog.cpp        # 你将要实现的
)

target_include_directories(magnet_ui_qt
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../../include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# 依赖所有业务层和Qt
target_link_libraries(magnet_ui_qt
    PRIVATE  # 动态库通常使用PRIVATE链接
        magnet_utils
        magnet_async
        magnet_network
        magnet_protocols
        magnet_storage
        Qt6::Core
        Qt6::Widgets
)

# 设置编译特性
target_compile_features(magnet_ui_qt PUBLIC cxx_std_17)

# 编译选项
target_compile_options(magnet_ui_qt PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# 导出符号定义
target_compile_definitions(magnet_ui_qt
    PUBLIC MAGNET_UI_QT_AVAILABLE
    PRIVATE MAGNET_UI_QT_EXPORTS  # 用于符号导出
)

message(STATUS "Qt UI library configured!")

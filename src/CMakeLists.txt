# MagnetDownload 主项目源码
# 模块化架构：分层静态库 + 可选动态UI插件

message(STATUS "Configuring MagnetDownload modular architecture...")

# 构建选项
option(BUILD_CONSOLE_UI "Build console user interface" ON)
option(BUILD_QT_UI "Build Qt graphical user interface" OFF)

# 1. 基础层：通用工具库（零依赖）
add_subdirectory(utils)

# 2. 异步基础设施层（依赖utils）
add_subdirectory(async)

# 3. 网络层（依赖utils + async）
add_subdirectory(network)

# 4. 协议层（依赖utils + async + network）
add_subdirectory(protocols)

# 5. 存储层（依赖utils + async，独立于network）
add_subdirectory(storage)

# 6. 界面层（动态库，可选）
if(BUILD_CONSOLE_UI)
    add_subdirectory(ui/console)
endif()

if(BUILD_QT_UI)
    add_subdirectory(ui/qt)
endif()

# 7. 主应用程序（集成所有静态库）
add_subdirectory(application)

# 显示构建配置
message(STATUS "")
message(STATUS "=== MagnetDownload Build Configuration ===")
message(STATUS "Libraries:")
message(STATUS "  ✓ magnet_utils     - 通用工具库")
message(STATUS "  ✓ magnet_async     - 异步基础设施")
message(STATUS "  ✓ magnet_network   - 网络层")
message(STATUS "  ✓ magnet_protocols - 协议层") 
message(STATUS "  ✓ magnet_storage   - 存储层")
message(STATUS "")
message(STATUS "User Interfaces:")
message(STATUS "  ${BUILD_CONSOLE_UI} Console UI (magnet_ui_console)")
message(STATUS "  ${BUILD_QT_UI} Qt GUI (magnet_ui_qt)")
message(STATUS "")
message(STATUS "Main Application:")
message(STATUS "  ✓ magnetdownload   - 主可执行文件")
message(STATUS "===========================================")
message(STATUS "")

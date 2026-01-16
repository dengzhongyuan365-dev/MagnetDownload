cmake_minimum_required(VERSION 3.16)

# 版本信息
set(MAGNETDOWNLOAD_VERSION_MAJOR 1)
set(MAGNETDOWNLOAD_VERSION_MINOR 0)
set(MAGNETDOWNLOAD_VERSION_PATCH 0)
set(MAGNETDOWNLOAD_VERSION_SUFFIX "single-task")
set(MAGNETDOWNLOAD_VERSION "${MAGNETDOWNLOAD_VERSION_MAJOR}.${MAGNETDOWNLOAD_VERSION_MINOR}.${MAGNETDOWNLOAD_VERSION_PATCH}")
set(MAGNETDOWNLOAD_VERSION_FULL "${MAGNETDOWNLOAD_VERSION}-${MAGNETDOWNLOAD_VERSION_SUFFIX}")

project(MagnetDownload 
    VERSION ${MAGNETDOWNLOAD_VERSION}
    LANGUAGES CXX
    DESCRIPTION "High-performance magnet link downloader"
    HOMEPAGE_URL "https://github.com/yourusername/MagnetDownload"
)

# vcpkg 支持（可选）
if(DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file")
    message(STATUS "Using vcpkg from: $ENV{VCPKG_ROOT}")
endif()

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -DDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")
elseif(MSVC)
    # MSVC 特定编译选项
    add_compile_definitions(
        NOMINMAX                # 禁用 Windows.h 中的 min/max 宏，避免与 std::min/std::max 冲突
        WIN32_LEAN_AND_MEAN     # 减少 Windows.h 包含的头文件
        _WIN32_WINNT=0x0601     # 设置最低支持的 Windows 版本 (Windows 7)
    )
    # MSVC 编译警告级别和 UTF-8 支持
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /utf-8")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /Od /Zi")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2")
endif()

# 输出目录设置
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 查找系统依赖
find_package(Threads REQUIRED)

# 配置Asio库（独立版）
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/3rd/asio/include")
    # 创建Asio接口库
    add_library(asio INTERFACE)
    
    # 设置包含目录
    target_include_directories(asio INTERFACE 
        ${CMAKE_CURRENT_SOURCE_DIR}/3rd/asio/include)
    
    # 设置编译定义
    target_compile_definitions(asio INTERFACE 
        ASIO_STANDALONE
        ASIO_NO_DEPRECATED)
    
    # 链接线程库
    target_link_libraries(asio INTERFACE Threads::Threads)
    
    # Linux平台特定设置
    if(UNIX AND NOT APPLE)
        target_link_libraries(asio INTERFACE rt)
    endif()
    
    message(STATUS "Found Asio at: ${CMAKE_CURRENT_SOURCE_DIR}/3rd/asio/include")
else()
    message(FATAL_ERROR "Asio not found! Please ensure Asio source is available in 3rd/asio/include")
endif()

# ========== 添加磁力链接解析器库 ==========
add_library(magnet_parser STATIC
    src/protocols/magnet_uri_parser.cpp
)

# 设置包含目录
target_include_directories(magnet_parser PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 链接依赖（当前不需要额外依赖）
# target_link_libraries(magnet_parser PRIVATE asio)
# ==========================================

# 项目选项
option(BUILD_EXPERIMENTS "Build Asio learning experiments" ON)
option(BUILD_TESTS "Build unit tests" ON)
option(BUILD_MAIN_PROJECT "Build main MagnetDownload project" ON)
option(BUILD_CONSOLE_UI "Build console user interface" ON)
option(BUILD_QT_UI "Build Qt graphical user interface" OFF)
option(ENABLE_PACKAGING "Enable packaging support" ON)

# 包含版本管理
include(cmake/version.cmake)

# 添加生成的头文件目录到包含路径
include_directories(${CMAKE_BINARY_DIR}/include)

# 配置 Google Test 库（本地源码版）
if(BUILD_TESTS)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/3rd/googletest/CMakeLists.txt")
        # 设置 gtest 选项
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        
        # 添加 googletest 子目录
        add_subdirectory(3rd/googletest EXCLUDE_FROM_ALL)
        
        message(STATUS "Found Google Test at: ${CMAKE_CURRENT_SOURCE_DIR}/3rd/googletest")
    else()
        message(FATAL_ERROR "Google Test not found! Please ensure GTest source is available in 3rd/googletest")
        set(BUILD_TESTS OFF)
    endif()
endif()

# 添加子目录
# if(BUILD_EXPERIMENTS)
#     add_subdirectory(experiments)
# endif()

if(BUILD_MAIN_PROJECT)
    add_subdirectory(src)
endif()

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# 安装配置和打包
if(BUILD_MAIN_PROJECT AND ENABLE_PACKAGING)
    include(cmake/packaging.cmake)
endif()

# 显示构建信息
message(STATUS "")
message(STATUS "MagnetDownload Configuration:")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Build Experiments: ${BUILD_EXPERIMENTS}")
message(STATUS "  Build Tests: ${BUILD_TESTS}")
message(STATUS "  Build Main Project: ${BUILD_MAIN_PROJECT}")
message(STATUS "  Enable Packaging: ${ENABLE_PACKAGING}")
message(STATUS "")
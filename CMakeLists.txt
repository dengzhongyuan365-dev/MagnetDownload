cmake_minimum_required(VERSION 3.16)
project(MagnetDownload VERSION 1.0.0 LANGUAGES CXX)

# 新增启用vcpkg
if(DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file")
    message(STATUS "Using vcpkg from: $ENV{VCPKG_ROOT}")
else()
    message(WARNING "VCPKG_ROOT not set, vcpkg will not be used")
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

# Google Test 配置
if(BUILD_TESTS)
    message(STATUS "Looking for Google Test...")
    
    # 优先使用vcpkg安装的GTest
    find_package(GTest QUIET)
    
    if(GTest_FOUND)
        message(STATUS "Found GTest via vcpkg (v${GTest_VERSION})")
        message(STATUS "  Include: ${GTEST_INCLUDE_DIRS}")
        message(STATUS "  Libraries: ${GTEST_LIBRARIES}")
    else()
        # 备用方案：使用本地源码
        message(STATUS "GTest not found in vcpkg, checking local source...")
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/3rd/googletest/CMakeLists.txt")
            set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
            add_subdirectory(3rd/googletest EXCLUDE_FROM_ALL)
            message(STATUS "Using local Google Test from 3rd/googletest")
        else()
            message(WARNING 
                "Google Test not found! Tests will be disabled.\n"
                "Options:\n"
                "  1. Ensure vcpkg is installed and GTest is available\n"
                "  2. Download to 3rd/googletest: git clone https://github.com/google/googletest.git\n"
            )
            set(BUILD_TESTS OFF)
        endif()
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

# 安装配置（用于将来的主项目）
if(BUILD_MAIN_PROJECT)
    include(GNUInstallDirs)
    # 安装目标将在src/CMakeLists.txt中定义
endif()

# 显示构建信息
message(STATUS "")
message(STATUS "MagnetDownload Configuration:")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Build Experiments: ${BUILD_EXPERIMENTS}")
message(STATUS "  Build Tests: ${BUILD_TESTS}")
message(STATUS "  Build Main Project: ${BUILD_MAIN_PROJECT}")
message(STATUS "")
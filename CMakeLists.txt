cmake_minimum_required(VERSION 3.10)
project(MagnetDownload VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 添加Asio
add_subdirectory(external/asio EXCLUDE_FROM_ALL)
include_directories(external/asio/asio/include)

# 项目源码目录
add_subdirectory(src)

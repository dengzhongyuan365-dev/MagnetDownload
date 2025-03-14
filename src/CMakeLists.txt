CMake_minimum_required(VERSION 3.27)

project(MagnetDownloader)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../external/asio/asio/include)

# 定义ASIO_STANDALONE宏
add_definitions(-DASIO_STANDALONE)

# 添加源文件
set(SOURCES
    main.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

# 添加平台特定依赖
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE wsock32 ws2_32)
endif()

if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)
endif()


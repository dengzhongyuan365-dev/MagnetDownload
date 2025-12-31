# MagnetDownload 单元测试配置

message(STATUS "Configuring tests...")

# 查找 Google Test
find_package(GTest REQUIRED)

# 创建测试主程序
add_executable(magnet_tests
    test_main.cpp
    protocols/test_magnet_uri_parser.cpp
)

# 链接必要的库
target_link_libraries(magnet_tests
    PRIVATE
        GTest::gtest
        GTest::gmock
        asio
        Threads::Threads
)

# 设置包含目录
target_include_directories(magnet_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

# 自动发现测试用例
include(GoogleTest)
gtest_discover_tests(magnet_tests)

message(STATUS "Tests configured successfully!")

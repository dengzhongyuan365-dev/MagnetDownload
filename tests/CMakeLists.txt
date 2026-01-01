# MagnetDownload 单元测试配置
message(STATUS "Configuring tests...")

# 创建测试可执行文件（只包含测试文件）
add_executable(magnet_tests
    test_main.cpp
    protocols/test_magnet_uri_parser.cpp
)

# 链接库
target_link_libraries(magnet_tests
    PRIVATE
        GTest::gtest
        GTest::gtest_main
        magnet_parser  # 链接库，不包含源文件
)

# 包含目录
target_include_directories(magnet_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

include(GoogleTest)
gtest_discover_tests(magnet_tests)

message(STATUS "Tests configured successfully!")
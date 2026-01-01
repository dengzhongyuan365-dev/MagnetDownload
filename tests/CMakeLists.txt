# MagnetDownload 单元测试配置
message(STATUS "Configuring tests...")

# 创建测试可执行文件（只包含测试文件）
add_executable(magnet_tests
    test_main.cpp
    protocols/test_magnet_uri_parser.cpp
    # 需要添加实现文件，因为magnet_parser库可能没有单独定义
    ../src/protocols/magnet_uri_parser.cpp
)

# 链接库
target_link_libraries(magnet_tests
    PRIVATE
        GTest::gtest
        GTest::gtest_main
)

# 包含目录
target_include_directories(magnet_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include  # 指向 include 目录
        ${CMAKE_SOURCE_DIR}/src  # 可能需要这个
)

include(GoogleTest)
gtest_discover_tests(magnet_tests)

message(STATUS "Tests configured successfully!")
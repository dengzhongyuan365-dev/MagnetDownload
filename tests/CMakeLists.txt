# MagnetDownload 单元测试配置
message(STATUS "Configuring tests...")

add_executable(magnet_tests
    test_main.cpp
    protocols/test_magnet_uri_parser.cpp
    protocols/test_node_id.cpp
    protocols/test_routing_table.cpp
    ../src/protocols/magnet_uri_parser.cpp
    ../src/protocols/dch_types.cpp
    ../src/protocols/routing_table.cpp
)

# 链接库
target_link_libraries(magnet_tests
    PRIVATE
        GTest::gtest
        GTest::gtest_main
        pthread  
)

# 包含目录
target_include_directories(magnet_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
)

include(GoogleTest)
gtest_discover_tests(magnet_tests)

message(STATUS "Tests configured successfully!")
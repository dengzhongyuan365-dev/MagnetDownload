/**
 * @file test_main.cpp
 * @brief Google Test 主入口文件
 * 
 * 这个文件提供了所有单元测试的统一入口点。
 * Google Test 会自动发现并运行所有测试用例。
 */

#include <gtest/gtest.h>

int main(int argc, char** argv) {
    // 初始化 Google Test 框架
    ::testing::InitGoogleTest(&argc, argv);
    
    // 运行所有测试
    return RUN_ALL_TESTS();
}

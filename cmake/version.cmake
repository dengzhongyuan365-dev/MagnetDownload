# MagnetDownload Version Management
# 版本管理和构建信息生成

# 获取Git信息
find_package(Git QUIET)
if(GIT_FOUND)
    # 获取Git提交哈希
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_COMMIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    
    # 获取Git分支名
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_BRANCH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    
    # 检查是否有未提交的更改
    execute_process(
        COMMAND ${GIT_EXECUTABLE} diff --quiet
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        RESULT_VARIABLE GIT_DIRTY
        ERROR_QUIET
    )
    
    if(GIT_DIRTY)
        set(GIT_COMMIT_HASH "${GIT_COMMIT_HASH}-dirty")
    endif()
else()
    set(GIT_COMMIT_HASH "unknown")
    set(GIT_BRANCH "unknown")
endif()

# 构建时间戳
string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S UTC" UTC)

# 构建类型
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif()

# 平台信息
if(WIN32)
    set(BUILD_PLATFORM "Windows")
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(BUILD_ARCH "x64")
    else()
        set(BUILD_ARCH "x86")
    endif()
elseif(UNIX AND NOT APPLE)
    set(BUILD_PLATFORM "Linux")
    execute_process(
        COMMAND uname -m
        OUTPUT_VARIABLE BUILD_ARCH
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
elseif(APPLE)
    set(BUILD_PLATFORM "macOS")
    execute_process(
        COMMAND uname -m
        OUTPUT_VARIABLE BUILD_ARCH
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
else()
    set(BUILD_PLATFORM "Unknown")
    set(BUILD_ARCH "Unknown")
endif()

# 编译器信息
set(BUILD_COMPILER "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")

# 生成版本头文件
configure_file(
    ${CMAKE_SOURCE_DIR}/cmake/version.h.in
    ${CMAKE_BINARY_DIR}/include/magnet/version.h
    @ONLY
)

# 显示版本信息
message(STATUS "")
message(STATUS "MagnetDownload Version Information:")
message(STATUS "  Version: ${MAGNETDOWNLOAD_VERSION_FULL}")
message(STATUS "  Git Commit: ${GIT_COMMIT_HASH}")
message(STATUS "  Git Branch: ${GIT_BRANCH}")
message(STATUS "  Build Time: ${BUILD_TIMESTAMP}")
message(STATUS "  Platform: ${BUILD_PLATFORM} ${BUILD_ARCH}")
message(STATUS "  Compiler: ${BUILD_COMPILER}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "")
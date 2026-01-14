#!/bin/bash
# MagnetDownload Build Script
# 跨平台构建脚本

set -e

# 默认参数
BUILD_TYPE="Release"
BUILD_TESTS="ON"
ENABLE_PACKAGING="ON"
PARALLEL_JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
CLEAN_BUILD=false
INSTALL_PREFIX=""

# 帮助信息
show_help() {
    cat << EOF
MagnetDownload Build Script

Usage: $0 [OPTIONS]

Options:
    -t, --type TYPE         Build type (Debug|Release|RelWithDebInfo) [default: Release]
    -j, --jobs N            Number of parallel jobs [default: auto-detect]
    --no-tests              Disable building tests
    --no-packaging          Disable packaging support
    --clean                 Clean build directory before building
    --prefix PATH           Installation prefix
    -h, --help              Show this help message

Examples:
    $0                      # Build with default settings
    $0 -t Debug --clean     # Clean debug build
    $0 --no-tests -j 8      # Release build without tests, 8 parallel jobs
EOF
}

# 解析命令行参数
while [[ $# -gt 0 ]]; do
    case $1 in
        -t|--type)
            BUILD_TYPE="$2"
            shift 2
            ;;
        -j|--jobs)
            PARALLEL_JOBS="$2"
            shift 2
            ;;
        --no-tests)
            BUILD_TESTS="OFF"
            shift
            ;;
        --no-packaging)
            ENABLE_PACKAGING="OFF"
            shift
            ;;
        --clean)
            CLEAN_BUILD=true
            shift
            ;;
        --prefix)
            INSTALL_PREFIX="$2"
            shift 2
            ;;
        -h|--help)
            show_help
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            show_help
            exit 1
            ;;
    esac
done

# 验证构建类型
case $BUILD_TYPE in
    Debug|Release|RelWithDebInfo|MinSizeRel)
        ;;
    *)
        echo "Error: Invalid build type '$BUILD_TYPE'"
        echo "Valid types: Debug, Release, RelWithDebInfo, MinSizeRel"
        exit 1
        ;;
esac

# 获取脚本目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"

echo "MagnetDownload Build Script"
echo "=========================="
echo "Project Root: $PROJECT_ROOT"
echo "Build Type: $BUILD_TYPE"
echo "Parallel Jobs: $PARALLEL_JOBS"
echo "Build Tests: $BUILD_TESTS"
echo "Enable Packaging: $ENABLE_PACKAGING"
echo "Clean Build: $CLEAN_BUILD"
if [[ -n "$INSTALL_PREFIX" ]]; then
    echo "Install Prefix: $INSTALL_PREFIX"
fi
echo ""

# 清理构建目录
if [[ "$CLEAN_BUILD" == true ]]; then
    echo "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
fi

# 创建构建目录
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# 配置CMake参数
CMAKE_ARGS=(
    "-DCMAKE_BUILD_TYPE=$BUILD_TYPE"
    "-DBUILD_TESTS=$BUILD_TESTS"
    "-DENABLE_PACKAGING=$ENABLE_PACKAGING"
)

if [[ -n "$INSTALL_PREFIX" ]]; then
    CMAKE_ARGS+=("-DCMAKE_INSTALL_PREFIX=$INSTALL_PREFIX")
fi

# 检测平台特定配置
if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS
    echo "Detected macOS"
    CMAKE_ARGS+=("-G" "Ninja")
elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
    # Linux
    echo "Detected Linux"
    CMAKE_ARGS+=("-G" "Ninja")
elif [[ "$OSTYPE" == "msys" ]] || [[ "$OSTYPE" == "cygwin" ]]; then
    # Windows (MSYS2/Cygwin)
    echo "Detected Windows"
    # 使用默认生成器
fi

# 配置项目
echo "Configuring project..."
cmake "$PROJECT_ROOT" "${CMAKE_ARGS[@]}"

# 构建项目
echo "Building project..."
cmake --build . --config "$BUILD_TYPE" --parallel "$PARALLEL_JOBS"

# 运行测试
if [[ "$BUILD_TESTS" == "ON" ]]; then
    echo "Running tests..."
    ctest --build-config "$BUILD_TYPE" --output-on-failure --parallel "$PARALLEL_JOBS"
fi

# 创建包
if [[ "$ENABLE_PACKAGING" == "ON" ]]; then
    echo "Creating packages..."
    cpack -C "$BUILD_TYPE"
fi

echo ""
echo "Build completed successfully!"
echo "Executable: $BUILD_DIR/bin/magnetdownload"

if [[ "$ENABLE_PACKAGING" == "ON" ]]; then
    echo "Packages created in: $BUILD_DIR"
    ls -la "$BUILD_DIR"/*.{deb,rpm,tar.gz,zip,dmg,exe,msi} 2>/dev/null || true
fi
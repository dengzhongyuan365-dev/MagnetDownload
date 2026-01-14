# MagnetDownload 构建和打包指南

本文档介绍如何在不同平台上构建和打包MagnetDownload。

## 📋 目录

- [系统要求](#系统要求)
- [快速开始](#快速开始)
- [详细构建说明](#详细构建说明)
- [打包说明](#打包说明)
- [CI/CD](#cicd)
- [版本管理](#版本管理)

## 🔧 系统要求

### 通用要求
- **CMake** >= 3.16
- **C++17** 兼容编译器
- **Git** (用于版本信息)

### Linux
```bash
# Ubuntu/Debian
sudo apt-get install build-essential cmake ninja-build libssl-dev pkg-config

# CentOS/RHEL/Fedora
sudo yum install gcc-c++ cmake ninja-build openssl-devel pkgconfig
# 或者 (Fedora)
sudo dnf install gcc-c++ cmake ninja-build openssl-devel pkgconfig
```

### Windows
- **Visual Studio 2019/2022** 或 **Build Tools for Visual Studio**
- **vcpkg** (推荐，用于依赖管理)
- **NSIS** (可选，用于创建安装程序)

### macOS
```bash
# 使用 Homebrew
brew install cmake ninja
```

## 🚀 快速开始

### 使用构建脚本 (推荐)

**Linux/macOS:**
```bash
# 默认构建 (Release模式)
./scripts/build.sh

# Debug构建
./scripts/build.sh -t Debug

# 清理构建
./scripts/build.sh --clean

# 不构建测试
./scripts/build.sh --no-tests
```

**Windows:**
```cmd
REM 默认构建 (Release模式, x64)
scripts\build.bat

REM Debug构建
scripts\build.bat -t Debug

REM x86构建
scripts\build.bat --arch x86

REM 清理构建
scripts\build.bat --clean
```

### 手动构建

```bash
# 创建构建目录
mkdir build && cd build

# 配置项目
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON

# 构建
cmake --build . --parallel

# 运行测试
ctest --output-on-failure

# 创建包
cpack
```

## 📦 详细构建说明

### CMake 选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `CMAKE_BUILD_TYPE` | Release | 构建类型 (Debug/Release/RelWithDebInfo) |
| `BUILD_TESTS` | ON | 是否构建单元测试 |
| `BUILD_MAIN_PROJECT` | ON | 是否构建主项目 |
| `ENABLE_PACKAGING` | ON | 是否启用打包支持 |
| `BUILD_CONSOLE_UI` | ON | 是否构建控制台界面 |
| `BUILD_QT_UI` | OFF | 是否构建Qt图形界面 |

### 构建类型说明

- **Debug**: 包含调试信息，未优化，适合开发调试
- **Release**: 完全优化，无调试信息，适合发布
- **RelWithDebInfo**: 优化 + 调试信息，适合性能分析
- **MinSizeRel**: 最小体积优化

### 平台特定配置

#### Windows (Visual Studio)
```bash
# 配置 x64
cmake .. -A x64 -DCMAKE_BUILD_TYPE=Release

# 配置 x86
cmake .. -A Win32 -DCMAKE_BUILD_TYPE=Release

# 使用 vcpkg
cmake .. -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
```

#### Linux (Ninja)
```bash
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja
```

#### macOS (Universal Binary)
```bash
# Intel x64
cmake .. -DCMAKE_OSX_ARCHITECTURES=x86_64

# Apple Silicon (ARM64)
cmake .. -DCMAKE_OSX_ARCHITECTURES=arm64

# Universal Binary
cmake .. -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64"
```

## 📦 打包说明

### 支持的包格式

| 平台 | 格式 | 说明 |
|------|------|------|
| Windows | NSIS (.exe) | 安装程序 |
| Windows | ZIP | 便携版 |
| Linux | DEB | Debian/Ubuntu包 |
| Linux | RPM | RedHat/CentOS/Fedora包 |
| Linux | TGZ | 通用tar.gz包 |
| macOS | DMG | 磁盘镜像 |
| macOS | TGZ | 通用tar.gz包 |

### 创建包

```bash
# 在构建目录中
cd build

# 创建所有支持的包格式
cpack

# 创建特定格式的包
cpack -G DEB      # 仅创建DEB包
cpack -G NSIS     # 仅创建NSIS安装程序
cpack -G ZIP      # 仅创建ZIP包
```

### 包内容

所有包都包含：
- `magnetdownload` 可执行文件
- `README.md` 和 `LICENSE` 文档
- 示例配置文件 (如果存在)

Linux包额外包含：
- Desktop文件 (用于应用程序菜单)
- 图标文件
- Man page文档

## 🔄 CI/CD

项目使用GitHub Actions进行自动化构建和发布。

### 触发条件
- **Push到main分支**: 构建所有平台
- **Push到feature分支**: 构建所有平台
- **创建tag (v*)**: 构建并发布到GitHub Releases
- **Pull Request**: 构建验证

### 构建矩阵
- **Windows**: x64, x86
- **Linux**: GCC, Clang
- **macOS**: x64, ARM64

### 自动发布
当推送以 `v` 开头的tag时，会自动：
1. 构建所有平台的包
2. 创建GitHub Release
3. 上传所有构建产物

## 📋 版本管理

### 版本信息

版本信息自动从以下来源生成：
- **CMakeLists.txt**: 主版本号
- **Git**: 提交哈希、分支名
- **构建时间**: 时间戳
- **平台信息**: 操作系统、架构、编译器

### 查看版本信息

```bash
# 查看完整版本信息
./magnetdownload --version

# 输出示例:
# MagnetDownload 1.0.0-single-task
# Git: a1b2c3d (main)
# Built: 2026-01-14 10:30:00 UTC
# Platform: Linux x86_64
# Compiler: GCC 11.4.0 (Release)
```

### 发布新版本

使用发布脚本：
```bash
# 创建本地tag
./scripts/release.sh 1.0.1

# 创建并推送tag (触发自动发布)
./scripts/release.sh 1.0.1 --push

# 预览操作 (不执行)
./scripts/release.sh 1.0.1 --dry-run
```

手动发布：
```bash
# 1. 更新版本号 (在CMakeLists.txt中)
# 2. 提交更改
git add CMakeLists.txt
git commit -m "chore: bump version to 1.0.1"

# 3. 创建tag
git tag -a v1.0.1 -m "Release version 1.0.1"

# 4. 推送
git push origin main
git push origin v1.0.1
```

## 🐛 故障排除

### 常见问题

**1. CMake找不到依赖**
```bash
# 确保安装了所需依赖
# Linux: 检查pkg-config路径
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH

# Windows: 使用vcpkg
set VCPKG_ROOT=C:\vcpkg
```

**2. 编译错误**
```bash
# 检查C++标准支持
cmake .. -DCMAKE_CXX_STANDARD=17

# 启用详细输出
cmake --build . --verbose
```

**3. 测试失败**
```bash
# 运行特定测试
ctest -R test_name --verbose

# 跳过测试
cmake .. -DBUILD_TESTS=OFF
```

**4. 打包失败**
```bash
# 检查CPack配置
cpack --config CPackConfig.cmake --verbose

# 禁用打包
cmake .. -DENABLE_PACKAGING=OFF
```

### 获取帮助

- **构建脚本帮助**: `./scripts/build.sh --help`
- **发布脚本帮助**: `./scripts/release.sh --help`
- **CMake选项**: `cmake -LH` (列出所有选项)
- **CPack生成器**: `cpack --help` (查看支持的包格式)

## 📚 相关文档

- [README.md](README.md) - 项目概述和使用说明
- [CONTRIBUTING.md](CONTRIBUTING.md) - 贡献指南
- [docs/](docs/) - 详细技术文档
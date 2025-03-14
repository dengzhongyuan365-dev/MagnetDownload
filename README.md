# MagnetDownload

一个基于CMake和vcpkg的工程化项目示例，展示如何有效地管理和维护forked的第三方库。

## 项目结构

```
MagnetDownload/
├── CMakeLists.txt              # 主CMake配置文件
├── vcpkg.json                  # vcpkg依赖配置
├── include/                    # 头文件目录
│   └── magnet_download/        
│       └── config.h            # 配置头文件
├── src/                        # 源代码目录
│   ├── CMakeLists.txt          # 源代码CMake配置
│   └── main.cpp                # 主程序入口
├── tests/                      # 测试目录
│   ├── CMakeLists.txt          # 测试CMake配置
│   └── asio_test.cpp           # Boost.Asio测试
├── cmake/                      # CMake模块目录
│   └── VcpkgHelpers.cmake      # vcpkg集成辅助函数
├── scripts/                    # Python脚本目录
│   ├── setup_vcpkg.py          # vcpkg设置脚本
│   ├── build.py                # 构建脚本
│   └── make_scripts_executable.py # 脚本权限设置工具
├── vcpkg-ports/                # 自定义vcpkg ports
│   └── boost-asio/             # 自定义Boost.Asio port
│       └── portfile.cmake      # 自定义port文件
└── external/                   # 外部依赖（fork的仓库）
```

## 技术栈

- C++17
- CMake 3.15+
- vcpkg（包管理）
- Python 3.6+（自动化脚本）
- Boost.Asio（网络库示例）

## 特性

- 使用vcpkg进行包管理
- 支持自定义forked的第三方库
- 模块化CMake结构
- 集成测试支持
- Python自动化脚本

## 快速开始

### 环境要求

- C++17兼容的编译器（GCC 7+, Clang 5+, MSVC 2019+）
- CMake 3.15 或更高版本
- Python 3.6 或更高版本
- Git

### 构建项目

1. 克隆本仓库：

```bash
git clone https://github.com/yourusername/MagnetDownload.git
cd MagnetDownload
```

2. 设置脚本可执行权限（仅Linux/macOS）：

```bash
python scripts/make_scripts_executable.py
```

3. 运行构建脚本（自动安装vcpkg并构建项目）：

```bash
python scripts/build.py
```

构建选项：

```bash
# 使用Debug模式构建
python scripts/build.py --build-type Debug

# 使用fork的Boost库构建
python scripts/build.py --use-forked

# 清理构建目录后重新构建
python scripts/build.py --clean
```

或者手动构建：

```bash
# 安装vcpkg
python scripts/setup_vcpkg.py

# 创建构建目录
mkdir -p build/Release && cd build/Release

# 配置项目
cmake ../.. -DCMAKE_TOOLCHAIN_FILE=../../vcpkg/scripts/buildsystems/vcpkg.cmake

# 构建项目
cmake --build .
```

### 使用自定义（forked）的依赖库

1. 将Boost库fork到您自己的GitHub账户
2. 运行setup脚本并按提示操作：
   ```bash
   python scripts/setup_vcpkg.py
   ```
3. 构建时指定使用fork的库：
   ```bash
   python scripts/build.py --use-forked
   ```

## 管理forked的第三方库

本项目展示了如何使用vcpkg的overlay ports功能来管理和维护forked的第三方库。步骤如下：

1. Fork需要定制的第三方库到您自己的仓库
2. 在 `vcpkg-ports/<库名>` 目录下创建自定义的portfile
3. 修改portfile指向您的fork仓库
4. 使用vcpkg的overlay ports功能构建项目

这种方式允许您：
- 对第三方库进行定制修改
- 控制依赖库的版本
- 在不影响上游的情况下添加自定义功能
- 长期维护您的定制版本

## 项目维护

### 添加新的依赖

要添加新的依赖库，编辑 `vcpkg.json` 文件：

```json
"dependencies": [
  {
    "name": "新库名",
    "version>=": "版本号"
  }
]
```

### 自定义依赖库

1. 将库fork到您的GitHub账户
2. 运行setup脚本或手动创建overlay port：

```bash
mkdir -p vcpkg-ports/库名
```

3. 创建portfile.cmake，参考 `vcpkg-ports/boost-asio/portfile.cmake`

### 添加新的源文件

将新的.cpp文件添加到src目录，CMake会自动包含它们。

## 许可证

[MIT License](LICENSE)

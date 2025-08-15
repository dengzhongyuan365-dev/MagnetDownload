# MagnetDownload 构建指南

## 项目结构

```
MagnetDownload/
├── CMakeLists.txt              # 顶层CMake配置
├── experiments/                # Asio学习实验
│   ├── CMakeLists.txt         # 实验专用CMake配置
│   ├── 01_hello_asio.cpp      # 你要创建的实验文件
│   ├── 02_work_guard.cpp      # 你要创建的实验文件
│   └── ...                    # 其他实验文件
├── src/                       # 主项目源码（待实现）
│   └── CMakeLists.txt         # 主项目CMake配置
├── tests/                     # 单元测试（待实现）
│   └── CMakeLists.txt         # 测试CMake配置
└── 3rd/                       # 第三方依赖
    └── asio/                  # Asio库源码
```

## 构建步骤

### 1. 确保Asio源码可用
Asio源码应该已经在 `3rd/asio/` 目录下。如果没有，请确保你有asio源码。

### 2. 创建构建目录
```bash
mkdir build
cd build
```

### 3. 配置CMake
```bash
# 只构建实验（推荐开始时使用）
cmake .. -DBUILD_EXPERIMENTS=ON -DBUILD_MAIN_PROJECT=OFF

# 或者，构建所有组件
cmake .. -DBUILD_EXPERIMENTS=ON -DBUILD_MAIN_PROJECT=ON -DBUILD_TESTS=ON
```

### 4. 构建项目
```bash
# 构建实验程序
cmake --build . --target asio_experiments

# 或构建所有目标
cmake --build .
```

## 实验工作流程

### 1. 编写实验代码
在 `experiments/main.cpp` 中的对应函数里编写你的实验代码，例如：
- `experiment_01_hello_asio()` - 实验1的代码
- `experiment_02_work_guard()` - 实验2的代码

### 2. 构建实验
```bash
cd build
cmake --build . --target asio_experiments
```

### 3. 运行实验
```bash
# 运行指定的实验
./bin/asio_experiments 01    # 运行实验1
./bin/asio_experiments 02    # 运行实验2
./bin/asio_experiments 03    # 运行实验3

# 不带参数显示帮助
./bin/asio_experiments
```

## CMake选项说明

- `BUILD_EXPERIMENTS=ON/OFF` - 是否构建Asio学习实验
- `BUILD_MAIN_PROJECT=ON/OFF` - 是否构建主MagnetDownload项目
- `BUILD_TESTS=ON/OFF` - 是否构建单元测试

## 实验编号说明

实验通过命令行参数选择：
- `01` - Hello Asio基础
- `02` - Work Guard使用  
- `03` - 异步定时器
- `04` - 对象生命周期
- `05` - UDP网络编程
- `06` - 多线程协作

## 构建目标说明

### 实验目标
- `asio_experiments` - 构建实验程序

### 输出目录
- 实验可执行文件：`build/bin/asio_experiments`
- 库文件：`build/lib/`
- 主项目可执行文件：`build/bin/`

## 常见问题

### Q: Asio找不到？
A: 确保 `3rd/asio/include` 目录存在且包含asio头文件

### Q: 编译错误：找不到头文件？
A: 确保使用了正确的CMake配置，Asio库应该自动配置

### Q: 如何调试CMake配置？
A: 使用 `cmake .. -DCMAKE_VERBOSE_MAKEFILE=ON` 查看详细构建信息

### Q: 如何清理构建？
A: 删除 `build` 目录重新开始，或使用 `cmake --build . --target clean`

## 开发建议

1. **先完成实验**：专注于Asio学习实验，掌握基础概念
2. **逐个实现**：按照实验顺序，逐个完成和测试
3. **记录问题**：遇到问题时记录解决方案
4. **后续开发**：实验完成后再开始主项目开发

## 下一步

1. 确保Asio下载完成
2. 创建第一个实验文件 `experiments/01_hello_asio.cpp`
3. 根据 `ASIO_EXPERIMENTS.md` 的指导开始编码
4. 构建和运行你的第一个实验！

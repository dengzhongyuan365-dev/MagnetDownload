# CEF (Chromium Embedded Framework)

## 当前版本信息

### Windows 64-bit
- **目录**: `win64/`
- **CEF 版本**: 143.0.14+gdd46a37+chromium-143.0.7499.193
- **Chromium 版本**: 143.0.7499.193
- **下载日期**: 2026-01-14
- **下载地址**: https://cef-builds.spotifycdn.com/index.html

### Linux 64-bit（待添加）
- **目录**: `linux64/`
- **状态**: 未下载

### macOS（待添加）
- **目录**: `macos/`
- **状态**: 未下载

---

## 目录结构

```
cef/
├── win64/              # Windows 64-bit 预编译二进制
│   ├── Release/        # Release 版本
│   │   ├── libcef.dll
│   │   ├── libcef.lib
│   │   └── ...
│   ├── Debug/          # Debug 版本
│   │   ├── libcef.dll
│   │   ├── libcef.lib
│   │   └── ...
│   ├── Resources/      # 资源文件
│   │   ├── locales/
│   │   ├── *.pak
│   │   └── icudtl.dat
│   ├── include/        # CEF 头文件（所有平台共享）
│   └── libcef_dll/     # libcef_dll_wrapper 源码
├── linux64/            # Linux 版本（待添加）
├── macos/              # macOS 版本（待添加）
└── README.md           # 本文件
```

---

## 获取 CEF

### 方式 1：自动下载（推荐）⭐

首次构建时，CMake 会自动下载并解压 CEF：

```bash
mkdir build && cd build
cmake ..  # 自动检测并下载 CEF
```

**工作原理**：
1. CMake 检查 `3rd/cef/win64/` 是否存在
2. 如果不存在，自动从 Spotify CDN 下载
3. 自动解压到 `3rd/cef/`
4. **自动重命名**为标准目录名（`win64`/`linux64`/`macos`）
5. 清理临时文件

**优点**：
- ✅ 无需手动下载
- ✅ 自动重命名（解决长目录名问题）
- ✅ 适合 CI/CD
- ✅ 跨平台支持（Windows/Linux/macOS）

**配置**：见 `cmake/DownloadCEF.cmake`

---

### 方式 2：手动下载

如果网络问题或想手动控制：

#### Windows
1. 访问：https://cef-builds.spotifycdn.com/index.html
2. 选择：**Windows 64-bit, Standard Distribution**
3. 下载：`cef_binary_*_windows64.tar.bz2`（约 100 MB）
4. 解压（会得到类似 `cef_binary_143.0.14+...+chromium-143.0.7499.193_windows64` 的目录）
5. **重命名为：`win64`**
6. 移动到：`3rd/cef/win64/`

#### Linux
1. 下载：**Linux 64-bit, Standard Distribution**
2. 解压后**重命名为：`linux64`**
3. 放置到：`3rd/cef/linux64/`

#### macOS
1. 下载：**macOS 64-bit, Standard Distribution**
2. 解压后**重命名为：`macos`**
3. 放置到：`3rd/cef/macos/`

**PowerShell 快捷脚本（Windows）**：
```powershell
# 下载并解压 CEF（Windows）
$url = "https://cef-builds.spotifycdn.com/cef_binary_143.0.14+gdd46a37+chromium-143.0.7499.193_windows64.tar.bz2"
$output = "cef.tar.bz2"

# 下载
Invoke-WebRequest -Uri $url -OutFile $output

# 解压（需要 7-Zip 或 tar）
tar -xjf $output

# 重命名
Rename-Item "cef_binary_*_windows64" "win64"

# 清理
Remove-Item $output
```

---

### 方式 3：强制重新下载/更新版本

如果需要更新 CEF 版本或重新下载：

```bash
# 1. 删除现有目录
rm -rf 3rd/cef/win64

# 2. （可选）修改版本号
# 编辑 cmake/DownloadCEF.cmake，修改 CEF_VERSION

# 3. 重新运行 CMake（会自动下载新版本）
cd build
cmake ..
```

---

## CMake 集成

项目使用 `cmake/FindCEF.cmake` 自动检测平台并选择对应的 CEF 目录。

```cmake
# 自动下载 CEF（如果不存在）
include(cmake/DownloadCEF.cmake)
download_cef_if_not_exists()

# 查找 CEF
find_package(CEF REQUIRED)

# 使用 CEF
target_include_directories(myapp PRIVATE ${CEF_INCLUDE_PATH})
target_link_libraries(myapp libcef_dll_wrapper ${CEF_STANDARD_LIBS})
```

---

## 常见问题

### Q: 为什么下载的目录名很长？
A: CEF 官方压缩包解压后的目录名包含完整版本信息，例如：
```
cef_binary_143.0.14+gdd46a37+chromium-143.0.7499.193_windows64
```

我们的 CMake 脚本会**自动重命名**为简短的标准名称：
- Windows → `win64`
- Linux → `linux64`
- macOS → `macos`

### Q: 如何验证 CEF 已正确安装？
A: 检查以下文件是否存在：
```
3rd/cef/win64/Release/libcef.dll       # Windows
3rd/cef/linux64/Release/libcef.so      # Linux
3rd/cef/macos/Release/Chromium Embedded Framework.framework  # macOS
```

### Q: 下载失败怎么办？
A: 
1. 检查网络连接
2. 尝试手动下载（方式 2）
3. 使用 VPN（Spotify CDN 可能被墙）
4. 从其他镜像下载

### Q: 可以使用其他版本的 CEF 吗？
A: 可以！修改 `cmake/DownloadCEF.cmake` 中的版本号：
```cmake
set(CEF_VERSION "你的版本号")
```

### Q: CEF 占用空间太大？
A: 是的，CEF 大约 100-150 MB。这是 Chromium 的正常大小。你可以：
- ✅ 添加到 `.gitignore`（已配置）
- ✅ 使用自动下载脚本（不提交到 Git）
- ✅ CI/CD 使用缓存

---

## 许可证

CEF 使用 BSD 许可证，详见各平台目录下的 `LICENSE.txt`。

---

## 相关资源

- **CEF 官网**: https://bitbucket.org/chromiumembedded/cef
- **CEF 论坛**: https://www.magpcss.org/ceforum/
- **构建版本索引**: https://cef-builds.spotifycdn.com/index.html
- **API 文档**: https://cef-builds.spotifycdn.com/docs/

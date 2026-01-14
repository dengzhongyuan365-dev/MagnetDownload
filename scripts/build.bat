@echo off
REM MagnetDownload Windows Build Script

setlocal enabledelayedexpansion

REM 默认参数
set BUILD_TYPE=Release
set BUILD_TESTS=ON
set ENABLE_PACKAGING=ON
set PARALLEL_JOBS=%NUMBER_OF_PROCESSORS%
set CLEAN_BUILD=false
set INSTALL_PREFIX=
set ARCH=x64

REM 解析命令行参数
:parse_args
if "%~1"=="" goto :args_done
if "%~1"=="-t" (
    set BUILD_TYPE=%~2
    shift
    shift
    goto :parse_args
)
if "%~1"=="--type" (
    set BUILD_TYPE=%~2
    shift
    shift
    goto :parse_args
)
if "%~1"=="-j" (
    set PARALLEL_JOBS=%~2
    shift
    shift
    goto :parse_args
)
if "%~1"=="--jobs" (
    set PARALLEL_JOBS=%~2
    shift
    shift
    goto :parse_args
)
if "%~1"=="--no-tests" (
    set BUILD_TESTS=OFF
    shift
    goto :parse_args
)
if "%~1"=="--no-packaging" (
    set ENABLE_PACKAGING=OFF
    shift
    goto :parse_args
)
if "%~1"=="--clean" (
    set CLEAN_BUILD=true
    shift
    goto :parse_args
)
if "%~1"=="--prefix" (
    set INSTALL_PREFIX=%~2
    shift
    shift
    goto :parse_args
)
if "%~1"=="--arch" (
    set ARCH=%~2
    shift
    shift
    goto :parse_args
)
if "%~1"=="-h" goto :show_help
if "%~1"=="--help" goto :show_help
echo Unknown option: %~1
goto :show_help

:show_help
echo MagnetDownload Windows Build Script
echo.
echo Usage: %~nx0 [OPTIONS]
echo.
echo Options:
echo     -t, --type TYPE         Build type (Debug^|Release^|RelWithDebInfo) [default: Release]
echo     -j, --jobs N            Number of parallel jobs [default: %NUMBER_OF_PROCESSORS%]
echo     --arch ARCH             Architecture (x64^|x86) [default: x64]
echo     --no-tests              Disable building tests
echo     --no-packaging          Disable packaging support
echo     --clean                 Clean build directory before building
echo     --prefix PATH           Installation prefix
echo     -h, --help              Show this help message
echo.
echo Examples:
echo     %~nx0                      # Build with default settings
echo     %~nx0 -t Debug --clean     # Clean debug build
echo     %~nx0 --no-tests -j 8      # Release build without tests, 8 parallel jobs
exit /b 0

:args_done

REM 验证构建类型
if not "%BUILD_TYPE%"=="Debug" if not "%BUILD_TYPE%"=="Release" if not "%BUILD_TYPE%"=="RelWithDebInfo" if not "%BUILD_TYPE%"=="MinSizeRel" (
    echo Error: Invalid build type '%BUILD_TYPE%'
    echo Valid types: Debug, Release, RelWithDebInfo, MinSizeRel
    exit /b 1
)

REM 验证架构
if not "%ARCH%"=="x64" if not "%ARCH%"=="x86" (
    echo Error: Invalid architecture '%ARCH%'
    echo Valid architectures: x64, x86
    exit /b 1
)

REM 获取脚本目录
set SCRIPT_DIR=%~dp0
set PROJECT_ROOT=%SCRIPT_DIR%..
set BUILD_DIR=%PROJECT_ROOT%\build

echo MagnetDownload Windows Build Script
echo ===================================
echo Project Root: %PROJECT_ROOT%
echo Build Type: %BUILD_TYPE%
echo Architecture: %ARCH%
echo Parallel Jobs: %PARALLEL_JOBS%
echo Build Tests: %BUILD_TESTS%
echo Enable Packaging: %ENABLE_PACKAGING%
echo Clean Build: %CLEAN_BUILD%
if not "%INSTALL_PREFIX%"=="" echo Install Prefix: %INSTALL_PREFIX%
echo.

REM 清理构建目录
if "%CLEAN_BUILD%"=="true" (
    echo Cleaning build directory...
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
)

REM 创建构建目录
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"

REM 检查vcpkg
if defined VCPKG_ROOT (
    echo Using vcpkg from: %VCPKG_ROOT%
    set CMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
) else (
    echo Warning: VCPKG_ROOT not set, vcpkg will not be used
    set CMAKE_TOOLCHAIN_FILE=
)

REM 配置CMake参数
set CMAKE_ARGS=-DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DBUILD_TESTS=%BUILD_TESTS% -DENABLE_PACKAGING=%ENABLE_PACKAGING%

if not "%CMAKE_TOOLCHAIN_FILE%"=="" (
    set CMAKE_ARGS=%CMAKE_ARGS% -DCMAKE_TOOLCHAIN_FILE=%CMAKE_TOOLCHAIN_FILE%
)

if not "%INSTALL_PREFIX%"=="" (
    set CMAKE_ARGS=%CMAKE_ARGS% -DCMAKE_INSTALL_PREFIX=%INSTALL_PREFIX%
)

REM 设置架构
if "%ARCH%"=="x64" (
    set CMAKE_ARGS=%CMAKE_ARGS% -A x64
) else (
    set CMAKE_ARGS=%CMAKE_ARGS% -A Win32
)

REM 配置项目
echo Configuring project...
cmake "%PROJECT_ROOT%" %CMAKE_ARGS%
if errorlevel 1 (
    echo Configuration failed!
    exit /b 1
)

REM 构建项目
echo Building project...
cmake --build . --config %BUILD_TYPE% --parallel %PARALLEL_JOBS%
if errorlevel 1 (
    echo Build failed!
    exit /b 1
)

REM 运行测试
if "%BUILD_TESTS%"=="ON" (
    echo Running tests...
    ctest --build-config %BUILD_TYPE% --output-on-failure --parallel %PARALLEL_JOBS%
    if errorlevel 1 (
        echo Tests failed!
        exit /b 1
    )
)

REM 创建包
if "%ENABLE_PACKAGING%"=="ON" (
    echo Creating packages...
    cpack -C %BUILD_TYPE%
    if errorlevel 1 (
        echo Packaging failed!
        exit /b 1
    )
)

echo.
echo Build completed successfully!
echo Executable: %BUILD_DIR%\bin\%BUILD_TYPE%\magnetdownload.exe

if "%ENABLE_PACKAGING%"=="ON" (
    echo Packages created in: %BUILD_DIR%
    dir /b *.zip *.exe *.msi 2>nul
)

endlocal
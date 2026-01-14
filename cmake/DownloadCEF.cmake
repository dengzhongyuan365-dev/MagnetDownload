# DownloadCEF.cmake - 自动下载 CEF 并设置路径
#
# Usage:
#   include(cmake/DownloadCEF.cmake)
#   setup_cef()
#   find_package(CEF REQUIRED)

# CEF 版本配置
set(CEF_VERSION "143.0.14+gdd46a37+chromium-143.0.7499.193")
set(CEF_CHROMIUM_VERSION "143.0.7499.193")

# 检测平台
if(WIN32)
    set(CEF_PLATFORM "windows64")
    set(CEF_PLATFORM_SHORT "win64")
    set(CEF_ARCHIVE_EXT "tar.bz2")
elseif(UNIX AND NOT APPLE)
    set(CEF_PLATFORM "linux64")
    set(CEF_PLATFORM_SHORT "linux64")
    set(CEF_ARCHIVE_EXT "tar.bz2")
elseif(APPLE)
    set(CEF_PLATFORM "macosx64")
    set(CEF_PLATFORM_SHORT "macos")
    set(CEF_ARCHIVE_EXT "tar.bz2")
else()
    message(FATAL_ERROR "Unsupported platform")
endif()

# CEF 下载 URL（Spotify CDN）
set(CEF_DOWNLOAD_URL 
    "https://cef-builds.spotifycdn.com/cef_binary_${CEF_VERSION}_${CEF_PLATFORM}.${CEF_ARCHIVE_EXT}")

# CEF 安装目录
set(CEF_ROOT_DIR "${CMAKE_SOURCE_DIR}/3rd/cef")
set(CEF_TARGET_DIR "${CEF_ROOT_DIR}/${CEF_PLATFORM_SHORT}")
set(CEF_TEMP_ARCHIVE "${CEF_ROOT_DIR}/cef_temp.${CEF_ARCHIVE_EXT}")

function(download_cef_if_not_exists)
    # 检查目标目录是否已存在
    if(EXISTS "${CEF_TARGET_DIR}" AND EXISTS "${CEF_TARGET_DIR}/Release/libcef.dll")
        message(STATUS "CEF already exists at: ${CEF_TARGET_DIR}")
        message(STATUS "  Version: ${CEF_VERSION}")
        message(STATUS "  Platform: ${CEF_PLATFORM_SHORT}")
        return()
    endif()
    
    message(STATUS "========================================")
    message(STATUS "CEF not found, starting download...")
    message(STATUS "========================================")
    message(STATUS "Version: ${CEF_VERSION}")
    message(STATUS "Platform: ${CEF_PLATFORM}")
    message(STATUS "URL: ${CEF_DOWNLOAD_URL}")
    message(STATUS "Target: ${CEF_TARGET_DIR}")
    message(STATUS "")
    message(STATUS "This may take several minutes (~100 MB download)...")
    message(STATUS "")
    
    # 创建 CEF 根目录
    file(MAKE_DIRECTORY "${CEF_ROOT_DIR}")
    
    # 步骤 1: 下载 CEF
    message(STATUS "[1/4] Downloading CEF...")
    file(DOWNLOAD 
        "${CEF_DOWNLOAD_URL}"
        "${CEF_TEMP_ARCHIVE}"
        SHOW_PROGRESS
        STATUS download_status
        TIMEOUT 600  # 10 分钟超时
    )
    
    # 检查下载是否成功
    list(GET download_status 0 status_code)
    list(GET download_status 1 status_message)
    
    if(NOT status_code EQUAL 0)
        file(REMOVE "${CEF_TEMP_ARCHIVE}")
        message(FATAL_ERROR 
            "Failed to download CEF: ${status_message}\n"
            "Please check your network connection or download manually from:\n"
            "  ${CEF_DOWNLOAD_URL}\n"
            "And extract to: ${CEF_TARGET_DIR}")
    endif()
    
    message(STATUS "[1/4] Download completed!")
    
    # 步骤 2: 解压 CEF
    message(STATUS "[2/4] Extracting CEF archive...")
    file(ARCHIVE_EXTRACT 
        INPUT "${CEF_TEMP_ARCHIVE}"
        DESTINATION "${CEF_ROOT_DIR}"
    )
    message(STATUS "[2/4] Extraction completed!")
    
    # 步骤 3: 查找解压后的目录并重命名
    message(STATUS "[3/4] Renaming to standard directory...")
    
    # 查找解压后的目录（以 cef_binary_ 开头）
    file(GLOB extracted_dirs "${CEF_ROOT_DIR}/cef_binary_*")
    
    if(NOT extracted_dirs)
        file(REMOVE "${CEF_TEMP_ARCHIVE}")
        message(FATAL_ERROR "Failed to find extracted CEF directory")
    endif()
    
    list(GET extracted_dirs 0 extracted_dir)
    
    # 重命名为标准名称（win64/linux64/macos）
    if(EXISTS "${CEF_TARGET_DIR}")
        file(REMOVE_RECURSE "${CEF_TARGET_DIR}")
    endif()
    
    file(RENAME "${extracted_dir}" "${CEF_TARGET_DIR}")
    
    message(STATUS "[3/4] Renamed to: ${CEF_PLATFORM_SHORT}")
    
    # 步骤 4: 清理临时文件
    message(STATUS "[4/4] Cleaning up...")
    file(REMOVE "${CEF_TEMP_ARCHIVE}")
    message(STATUS "[4/4] Cleanup completed!")
    
    # 验证关键文件是否存在
    if(WIN32)
        set(CEF_LIB_FILE "${CEF_TARGET_DIR}/Release/libcef.dll")
    elseif(UNIX AND NOT APPLE)
        set(CEF_LIB_FILE "${CEF_TARGET_DIR}/Release/libcef.so")
    elseif(APPLE)
        set(CEF_LIB_FILE "${CEF_TARGET_DIR}/Release/Chromium Embedded Framework.framework")
    endif()
    
    if(NOT EXISTS "${CEF_LIB_FILE}")
        message(WARNING "CEF library not found at expected location: ${CEF_LIB_FILE}")
    endif()
    
    message(STATUS "")
    message(STATUS "========================================")
    message(STATUS "CEF download and setup completed!")
    message(STATUS "========================================")
    message(STATUS "Location: ${CEF_TARGET_DIR}")
    message(STATUS "")
    message(STATUS "You can now continue building the project.")
    message(STATUS "")
endfunction()

function(download_cef_force)
    if(EXISTS "${CEF_TARGET_DIR}")
        message(STATUS "Removing existing CEF at: ${CEF_TARGET_DIR}")
        file(REMOVE_RECURSE "${CEF_TARGET_DIR}")
    endif()
    
    download_cef_if_not_exists()
endfunction()

function(show_cef_info)
    message(STATUS "========================================")
    message(STATUS "CEF Configuration")
    message(STATUS "========================================")
    message(STATUS "Version: ${CEF_VERSION}")
    message(STATUS "Platform: ${CEF_PLATFORM} -> ${CEF_PLATFORM_SHORT}")
    message(STATUS "Target Dir: ${CEF_TARGET_DIR}")
    message(STATUS "Download URL: ${CEF_DOWNLOAD_URL}")
    
    if(EXISTS "${CEF_TARGET_DIR}")
        message(STATUS "Status: Installed")
    else()
        message(STATUS "Status: Not installed")
        message(STATUS "")
        message(STATUS "Run cmake again to auto-download CEF,")
        message(STATUS "or download manually from:")
        message(STATUS "  ${CEF_DOWNLOAD_URL}")
    endif()
    message(STATUS "========================================")
endfunction()

function(setup_cef)
    download_cef_if_not_exists()
    
    set(CEF_ROOT "${CEF_TARGET_DIR}" CACHE PATH "CEF root directory" FORCE)
    
    list(APPEND CMAKE_MODULE_PATH "${CEF_TARGET_DIR}/cmake")
    set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} PARENT_SCOPE)
    
    message(STATUS "CEF setup completed: ${CEF_ROOT}")
endfunction()

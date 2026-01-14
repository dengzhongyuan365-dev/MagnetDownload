# MagnetDownload Packaging Configuration
# 跨平台打包配置

include(GNUInstallDirs)

# 设置包信息
set(CPACK_PACKAGE_NAME "MagnetDownload")
set(CPACK_PACKAGE_VENDOR "MagnetDownload Team")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "High-performance magnet link downloader")
set(CPACK_PACKAGE_DESCRIPTION "A modern, high-performance BitTorrent magnet link downloader with DHT support")
set(CPACK_PACKAGE_VERSION_MAJOR ${MAGNETDOWNLOAD_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${MAGNETDOWNLOAD_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${MAGNETDOWNLOAD_VERSION_PATCH})
set(CPACK_PACKAGE_VERSION ${MAGNETDOWNLOAD_VERSION_FULL})
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/yourusername/MagnetDownload")
set(CPACK_PACKAGE_CONTACT "your.email@example.com")

# 资源文件
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")

# 平台特定配置
if(WIN32)
    # Windows 配置
    set(CPACK_GENERATOR "NSIS;ZIP")
    
    # NSIS 安装程序配置
    set(CPACK_NSIS_DISPLAY_NAME "MagnetDownload")
    set(CPACK_NSIS_PACKAGE_NAME "MagnetDownload")
    set(CPACK_NSIS_CONTACT "your.email@example.com")
    set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/yourusername/MagnetDownload")
    set(CPACK_NSIS_HELP_LINK "https://github.com/yourusername/MagnetDownload/wiki")
    set(CPACK_NSIS_MODIFY_PATH ON)
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    
    # 创建开始菜单快捷方式
    set(CPACK_NSIS_MENU_LINKS
        "bin/magnetdownload.exe" "MagnetDownload"
        "https://github.com/yourusername/MagnetDownload" "MagnetDownload Website"
    )
    
    # 文件关联（可选）
    # set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS
    #     "WriteRegStr HKCR '.magnet' '' 'MagnetDownload.magnet'"
    # )
    
    # 包文件名
    set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-Windows-${BUILD_ARCH}")
    
elseif(UNIX AND NOT APPLE)
    # Linux 配置
    set(CPACK_GENERATOR "DEB;RPM;TGZ")
    
    # DEB 包配置
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "MagnetDownload Team <your.email@example.com>")
    set(CPACK_DEBIAN_PACKAGE_SECTION "net")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.17)")
    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "")
    set(CPACK_DEBIAN_PACKAGE_SUGGESTS "")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    
    # RPM 包配置
    set(CPACK_RPM_PACKAGE_GROUP "Applications/Internet")
    set(CPACK_RPM_PACKAGE_LICENSE "MIT")
    set(CPACK_RPM_PACKAGE_REQUIRES "glibc >= 2.17")
    set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
    
    # 包文件名
    set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-Linux-${BUILD_ARCH}")
    
elseif(APPLE)
    # macOS 配置
    set(CPACK_GENERATOR "DragNDrop;TGZ")
    
    # DMG 配置
    set(CPACK_DMG_VOLUME_NAME "MagnetDownload")
    set(CPACK_DMG_FORMAT "UDZO")
    
    # 包文件名
    set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-macOS-${BUILD_ARCH}")
endif()

# 组件配置
set(CPACK_COMPONENTS_ALL Runtime Development)

# Runtime 组件
set(CPACK_COMPONENT_RUNTIME_DISPLAY_NAME "MagnetDownload Application")
set(CPACK_COMPONENT_RUNTIME_DESCRIPTION "Main application executable and required libraries")
set(CPACK_COMPONENT_RUNTIME_REQUIRED TRUE)

# Development 组件
set(CPACK_COMPONENT_DEVELOPMENT_DISPLAY_NAME "Development Files")
set(CPACK_COMPONENT_DEVELOPMENT_DESCRIPTION "Header files and libraries for development")
set(CPACK_COMPONENT_DEVELOPMENT_DEPENDS Runtime)

# 安装目标
install(TARGETS magnetdownload
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT Runtime
)

# 安装文档
install(FILES 
    ${CMAKE_SOURCE_DIR}/README.md
    ${CMAKE_SOURCE_DIR}/LICENSE
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
    COMPONENT Runtime
)

# 安装示例配置文件（如果有）
if(EXISTS ${CMAKE_SOURCE_DIR}/config/magnetdownload.conf.example)
    install(FILES ${CMAKE_SOURCE_DIR}/config/magnetdownload.conf.example
        DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/magnetdownload
        COMPONENT Runtime
    )
endif()

# Linux 特定安装
if(UNIX AND NOT APPLE)
    # 安装 desktop 文件
    if(EXISTS ${CMAKE_SOURCE_DIR}/packaging/linux/magnetdownload.desktop)
        install(FILES ${CMAKE_SOURCE_DIR}/packaging/linux/magnetdownload.desktop
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
            COMPONENT Runtime
        )
    endif()
    
    # 安装图标
    if(EXISTS ${CMAKE_SOURCE_DIR}/packaging/linux/magnetdownload.png)
        install(FILES ${CMAKE_SOURCE_DIR}/packaging/linux/magnetdownload.png
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/pixmaps
            COMPONENT Runtime
        )
    endif()
    
    # 安装 man page
    if(EXISTS ${CMAKE_SOURCE_DIR}/docs/magnetdownload.1)
        install(FILES ${CMAKE_SOURCE_DIR}/docs/magnetdownload.1
            DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
            COMPONENT Runtime
        )
    endif()
endif()

# 包含 CPack
include(CPack)
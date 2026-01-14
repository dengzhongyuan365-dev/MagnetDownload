# CEF GUI 模块

find_package(CEF REQUIRED)

ADD_LOGICAL_TARGET("libcef_lib" "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")

set(CEF_TARGET_OUT_DIR "${CMAKE_BINARY_DIR}/bin/$<CONFIGURATION>")

add_subdirectory(${CEF_LIBCEF_DLL_WRAPPER_PATH} libcef_dll_wrapper)

# 跨平台公共源文件
set(CEF_COMMON_SOURCES
    common/cef_app.h
    common/cef_app.cpp
    common/cef_client.h
    common/cef_client.cpp
    common/message_handler.h
    common/message_handler.cpp
)

# 平台特定源文件
if(WIN32)
    set(CEF_PLATFORM_SOURCES
        win/main_win.cpp
    )
elseif(UNIX AND NOT APPLE)
    set(CEF_PLATFORM_SOURCES
        linux/main_linux.cpp
    )
elseif(APPLE)
    set(CEF_PLATFORM_SOURCES
        mac/main_mac.mm
    )
endif()

add_executable(magnetdownload_gui WIN32
    ${CEF_COMMON_SOURCES}
    ${CEF_PLATFORM_SOURCES}
)

SET_EXECUTABLE_TARGET_PROPERTIES(magnetdownload_gui)
add_dependencies(magnetdownload_gui libcef_dll_wrapper)

set_target_properties(magnetdownload_gui PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

target_include_directories(magnetdownload_gui PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/3rd/json
)

target_link_libraries(magnetdownload_gui
    libcef_lib
    libcef_dll_wrapper
    ${CEF_STANDARD_LIBS}
    magnet_application
    magnet_protocols
    magnet_network
    magnet_storage
    magnet_async
    magnet_utils
)

COPY_FILES("magnetdownload_gui" "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR}" "${CEF_TARGET_OUT_DIR}")
COPY_FILES("magnetdownload_gui" "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}" "${CEF_TARGET_OUT_DIR}")

# 安装配置
install(TARGETS magnetdownload_gui
    RUNTIME DESTINATION bin
)

# 输出信息
message(STATUS "Configured CEF GUI application")
message(STATUS "  Target: magnetdownload_gui")
message(STATUS "  CEF Platform: ${CEF_PLATFORM}")

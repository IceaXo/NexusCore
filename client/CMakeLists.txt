cmake_minimum_required(VERSION 3.15)
project(nexus_client LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options("/utf-8")   # 所有源文件统一 UTF-8 编码

# ==============================================================
# WebView2 SDK
#
# Install via NuGet:
#   nuget install Microsoft.Web.WebView2 -Version 1.0.2903.40
#       -OutputDirectory packages/
# ==============================================================
set(WEBVIEW2_PKG_DIR "${CMAKE_SOURCE_DIR}/packages/Microsoft.Web.WebView2.1.0.2903.40"
    CACHE PATH "Path to WebView2 NuGet package")

# Auto-detect other versions if the default doesn't exist
if(NOT EXISTS "${WEBVIEW2_PKG_DIR}")
    file(GLOB WEBVIEW2_CANDIDATES "${CMAKE_SOURCE_DIR}/packages/Microsoft.Web.WebView2.*")
    list(LENGTH WEBVIEW2_CANDIDATES N_CANDIDATES)
    if(N_CANDIDATES GREATER 0)
        list(GET WEBVIEW2_CANDIDATES 0 WEBVIEW2_PKG_DIR)
    endif()
endif()

if(NOT EXISTS "${WEBVIEW2_PKG_DIR}")
    message(FATAL_ERROR
        "WebView2 SDK not found!\n\n"
        "Install with NuGet:\n"
        "  nuget install Microsoft.Web.WebView2 -Version 1.0.2903.40 -OutputDirectory ${CMAKE_SOURCE_DIR}/packages\n\n"
        "Or download from: https://www.nuget.org/packages/Microsoft.Web.WebView2\n"
        "Then configure with: -DWEBVIEW2_PKG_DIR=<path>"
    )
endif()

set(WEBVIEW2_INCLUDE "${WEBVIEW2_PKG_DIR}/build/native/include")
set(WEBVIEW2_LIB "${WEBVIEW2_PKG_DIR}/build/native/x64/WebView2LoaderStatic.lib")

# ==============================================================
# Sources
# ==============================================================
file(GLOB NETWORK_SOURCES network/*.cpp)
file(GLOB UI_SOURCES ui/*.cpp)

add_executable(nexus_client
    main_client.cpp
    ${NETWORK_SOURCES}
    ${UI_SOURCES}
)

target_include_directories(nexus_client PRIVATE
    ${WEBVIEW2_INCLUDE}
    network
    ui
)

target_link_libraries(nexus_client PRIVATE
    ws2_32      # WinSock2
    ole32       # COM base
    oleaut32    # COM automation
    ${WEBVIEW2_LIB}
)

# Keep console window for log output during development.
# To hide the console in release builds, uncomment:
# set_target_properties(nexus_client PROPERTIES WIN32_EXECUTABLE TRUE)

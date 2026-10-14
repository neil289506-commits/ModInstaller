cmake_minimum_required(VERSION 3.21)
project(ModInstaller VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Svg LinguistTools)
# libwebp：Qt 不一定附 WebP 外掛，但 Modrinth 的模組圖示多為 webp，所以自行解碼
include(FetchContent)
set(WEBP_BUILD_ANIM_UTILS OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_CWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_DWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_GIF2WEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_IMG2WEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_VWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_WEBPINFO OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_WEBPMUX OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_LIBWEBPMUX OFF CACHE BOOL "" FORCE)
FetchContent_Declare(libwebp
    GIT_REPOSITORY https://github.com/webmproject/libwebp.git
    GIT_TAG v1.4.0)
FetchContent_MakeAvailable(libwebp)

# libdatachannel：WebRTC 資料通道（模組包傳輸用）。優先使用已安裝的版本（例如 vcpkg），否則由 FetchContent 取得
find_package(OpenSSL QUIET)
find_package(LibDataChannel CONFIG QUIET)
if(LibDataChannel_FOUND)
    set(MI_DC_TARGET LibDataChannel::LibDataChannel)
else()
    message(STATUS "找不到已安裝的 libdatachannel，改用 FetchContent 取得（需要 OpenSSL）")
    set(NO_MEDIA ON CACHE BOOL "" FORCE)
    set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
    set(NO_TESTS ON CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(libdatachannel
        GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
        GIT_TAG v0.24.6
        GIT_SUBMODULES_RECURSE TRUE)
    FetchContent_MakeAvailable(libdatachannel)
    set(MI_DC_TARGET LibDataChannel::LibDataChannelStatic)
endif()

set(SOURCES
    src/main.cpp
    src/core/Util.h
    src/core/Models.h
    src/core/Settings.h
    src/core/Settings.cpp
    src/core/Net.h
    src/core/Net.cpp
    src/core/Downloader.h
    src/core/Downloader.cpp
    src/core/ModApi.h
    src/core/ModApi.cpp
    src/core/VersionData.h
    src/core/VersionData.cpp
    src/core/DependencyResolver.h
    src/core/DependencyResolver.cpp
    src/core/ModPak.h
    src/core/ModPak.cpp
    src/core/PackJob.h
    src/core/PackJob.cpp
    src/core/DeviceCode.h
    src/core/DeviceCode.cpp
    src/core/PeerService.h
    src/core/PeerService.cpp
    src/ui/IconCache.h
    src/ui/IconCache.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/SettingsDialog.h
    src/ui/SettingsDialog.cpp
    src/ui/LoaderVersionDialog.h
    src/ui/LoaderVersionDialog.cpp
    src/ui/ModBrowserDialog.h
    src/ui/ModBrowserDialog.cpp
    src/ui/DependencyReviewDialog.h
    src/ui/DependencyReviewDialog.cpp
    src/ui/FileTargetDialog.h
    src/ui/FileTargetDialog.cpp
    src/ui/BlockedModsDialog.h
    src/ui/BlockedModsDialog.cpp
    src/ui/ProgressDialog.h
    src/ui/ProgressDialog.cpp
    src/ui/Flows.h
    src/ui/Flows.cpp
    src/ui/UpdateModsDialog.h
    src/ui/UpdateModsDialog.cpp
    src/ui/PakEditorDialog.h
    src/ui/PakEditorDialog.cpp
    src/ui/TransferPanel.h
    src/ui/TransferPanel.cpp
    src/ui/RequestToastHost.h
    src/ui/RequestToastHost.cpp
    resources/resources.qrc
)

# Windows：app.rc 內嵌 exe 圖示（resources/icons/app.ico）與版本資訊
if(WIN32)
    enable_language(RC)
    list(APPEND SOURCES resources/app.rc)
    set_source_files_properties(resources/app.rc PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/app.ico")
endif()

add_executable(ModInstaller WIN32 ${SOURCES})
target_include_directories(ModInstaller PRIVATE src)
target_link_libraries(ModInstaller PRIVATE Qt6::Widgets Qt6::Network Qt6::Svg Qt6::GuiPrivate webp ${MI_DC_TARGET})
target_compile_definitions(ModInstaller PRIVATE APP_VERSION="${PROJECT_VERSION}")

if(MSVC)
    target_compile_options(ModInstaller PRIVATE /utf-8 /W3)
endif()

qt_add_translations(ModInstaller
    TS_FILES i18n/ModInstaller_zh_TW.ts i18n/ModInstaller_en.ts
    RESOURCE_PREFIX /i18n)

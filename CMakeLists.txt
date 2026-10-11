cmake_minimum_required(VERSION 3.21)
project(ModInstaller VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Svg LinguistTools)
# QuaZip：優先使用已安裝的版本（例如 vcpkg）；找不到時自動用 FetchContent 下載並一起編譯
find_package(QuaZip-Qt6 QUIET)
if(NOT QuaZip-Qt6_FOUND)
    message(STATUS "找不到已安裝的 QuaZip，改用 FetchContent 取得")
    include(FetchContent)
    set(QUAZIP_QT_MAJOR_VERSION 6 CACHE STRING "" FORCE)
    set(QUAZIP_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
    set(QUAZIP_INSTALL OFF CACHE BOOL "" FORCE)
    set(QUAZIP_BZIP2 OFF CACHE BOOL "" FORCE)
    set(QUAZIP_FETCH_LIBS ON CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(QuaZip
        GIT_REPOSITORY https://github.com/stachenov/quazip.git
        GIT_TAG v1.4
        SOURCE_DIR ${CMAKE_BINARY_DIR}/ext/quazip/quazip)
    FetchContent_MakeAvailable(QuaZip)
    target_include_directories(QuaZip PUBLIC ${CMAKE_BINARY_DIR}/ext/quazip)
endif()

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
    resources/resources.qrc
)

if(WIN32)
    list(APPEND SOURCES resources/app.rc)
endif()

add_executable(ModInstaller WIN32 ${SOURCES})
target_include_directories(ModInstaller PRIVATE src)
target_link_libraries(ModInstaller PRIVATE Qt6::Widgets Qt6::Network Qt6::Svg QuaZip::QuaZip webp)
target_compile_definitions(ModInstaller PRIVATE APP_VERSION="${PROJECT_VERSION}")

if(MSVC)
    target_compile_options(ModInstaller PRIVATE /utf-8 /W3)
endif()

qt_add_translations(ModInstaller
    TS_FILES i18n/ModInstaller_zh_TW.ts i18n/ModInstaller_en.ts
    RESOURCE_PREFIX /i18n)

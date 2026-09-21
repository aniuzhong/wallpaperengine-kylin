cmake_minimum_required(VERSION 3.10)
project(wallpaperengine-kylin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Qt is a build-time-only concern now: the tests use the QtTest framework
# and the shim compiles against Qt headers (its symbols resolve from the
# host peony process at load time). The shipped binaries link no Qt at all.
find_package(Qt5 REQUIRED COMPONENTS Core Gui)
find_package(X11 REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(XCB REQUIRED xcb)
pkg_check_modules(XCB_RANDR REQUIRED xcb-randr)
pkg_check_modules(LIBSYSTEMD REQUIRED libsystemd)

# JSON for the config file: fetched at configure time, pinned to a release
# tag — git over HTTPS, the same path the engine fetch uses.
include(FetchContent)
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(nlohmann_json)

# The browser frontend's HTTP server: header-only, single file, no package to
# install on Kylin. Every optional backend is forced off — this server speaks
# plain HTTP on loopback only, and leaving them on would pull OpenSSL and the
# compression libraries into the deb's dependency list for nothing.
#
# The non-blocking resolver is off for the same reason: it exists to bound a
# DNS lookup, and on Linux it drags in libanl. This server only ever binds
# and connects to numeric loopback addresses, so there is nothing to resolve.
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZSTD_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_NON_BLOCKING_GETADDRINFO OFF CACHE BOOL "" FORCE)
set(HTTPLIB_INSTALL OFF CACHE BOOL "" FORCE)
set(HTTPLIB_TEST OFF CACHE BOOL "" FORCE)
FetchContent_Declare(cpp_httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.56.0
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(cpp_httplib)

# ---- wallpaper_service
# Control core (frontend-free): the generic systemd D-Bus client plus the
# wallpaper domain logic (config persistence, argv mapping, library scan,
# peony integration, engine-unit facade). Qt-free by design — sd-bus for the
# buses, nlohmann for JSON, xcb-randr where the platform needs them. Frontends link this and nothing else — they must never talk to
# systemctl or craft unit files themselves.
add_library(wallpaper_service STATIC
    src/systemdunit.cpp
    src/unitbuilder.cpp
    src/config.cpp
    src/argvbuilder.cpp
    src/library.cpp
    src/report.cpp
    src/peonybuilder.cpp
    src/integration/marker.cpp
    src/integration/peony.cpp
    src/engineunit.cpp
    src/engineprocess.cpp
)
target_include_directories(wallpaper_service PUBLIC src ${XCB_RANDR_INCLUDE_DIRS})
target_link_libraries(wallpaper_service PUBLIC nlohmann_json::nlohmann_json ${XCB_LIBRARIES} ${XCB_RANDR_LIBRARIES} ${LIBSYSTEMD_LIBRARIES})
target_compile_options(wallpaper_service PRIVATE -Wall -Wextra)

# ---- wallpaper-engine
# Frontend binary: the headless CLI plus the browser frontend it serves on
# loopback. Qt-free like the service layer — it links this and nothing else,
# so a Qt include anywhere in src/ (outside hook/) breaks the link.
#
# The single-page app is embedded: no asset directory to install, no path to
# resolve at run time. The sources stay real .html/.css/.js files and are
# read here at configure time.
file(READ ${CMAKE_SOURCE_DIR}/src/ui/index.html UI_INDEX_HTML)
file(READ ${CMAKE_SOURCE_DIR}/src/ui/style.css UI_STYLE_CSS)
file(READ ${CMAKE_SOURCE_DIR}/src/ui/app.js UI_APP_JS)
configure_file(${CMAKE_SOURCE_DIR}/cmake/uiassets.cpp.in ${CMAKE_BINARY_DIR}/uiassets.cpp @ONLY)

# The app icon doubles as the page's favicon: launched with --app= the window
# has no other place to get an icon from, and Chromium takes it from here.
# Embedded as bytes rather than as a raw string literal — PNG data contains
# NULs, and a string literal would stop at the first one.
file(READ ${CMAKE_SOURCE_DIR}/icon/wallpaper-engine-256.png UI_ICON_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," UI_ICON_BYTES "${UI_ICON_HEX}")
file(WRITE ${CMAKE_BINARY_DIR}/uiicon.h
"// Generated from icon/wallpaper-engine-256.png at configure time; do not edit.
#pragma once

#include <cstddef>

namespace Ui {

static const unsigned char kIconPng[] = {${UI_ICON_BYTES}};
static const std::size_t kIconPngSize = sizeof(kIconPng);

} // namespace Ui
")

# configure_file does not watch its inputs: without this, editing the page
# and running `cmake --build` links the previous assets in and the change
# appears to do nothing.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/ui/index.html
    ${CMAKE_SOURCE_DIR}/src/ui/style.css
    ${CMAKE_SOURCE_DIR}/src/ui/app.js
    ${CMAKE_SOURCE_DIR}/icon/wallpaper-engine-256.png)

add_executable(wallpaper-engine
    src/main.cpp
    src/cli.cpp
    src/ui.cpp
    src/uivalidate.cpp
    src/uilaunch.cpp
    ${CMAKE_BINARY_DIR}/uiassets.cpp
)
target_link_libraries(wallpaper-engine PRIVATE wallpaper_service httplib::httplib Threads::Threads)
target_include_directories(wallpaper-engine PRIVATE src ${CMAKE_BINARY_DIR})
target_compile_options(wallpaper-engine PRIVATE -Wall -Wextra)

# ---- peony-alpha
# LD_PRELOAD interposition library: makes peony-qt-desktop's background
# transparent and rewrites its window type for ukui-kwin. Exported symbols
# carry explicit visibility attributes in the source; Qt symbols resolve
# from the host process at load time, hence they are not linked here.
add_library(peony-alpha SHARED
    src/shim/peony-alpha.cpp
)
target_compile_options(peony-alpha PRIVATE -O2 -Wall -fPIC -fvisibility=hidden)
target_include_directories(peony-alpha PRIVATE ${Qt5Gui_INCLUDE_DIRS} ${X11_INCLUDE_DIR} ${XCB_INCLUDE_DIRS})
target_link_libraries(peony-alpha PRIVATE ${X11_LIBRARIES} ${XCB_LIBRARIES} dl)

# ---- engine (optional)
# Two equivalent acquisition paths, everything stays inside build/:
#   - third_party/linux-wallpaperengine (gitignored) holds a patched upstream
#     checkout copied in by hand: zero network, used when present
#   - otherwise cmake fetches upstream at LWPE_REF and applies patches/ —
#     this is what CI does, so a patch that no longer applies fails there
# Requires cmake >= 3.22 for the glslang submodule; Kylin's 3.16 builds only
# the controller (leave BUILD_ENGINE off).
option(BUILD_ENGINE "Build the engine from third_party or fetched upstream" OFF)
if(BUILD_ENGINE)
    if(CMAKE_VERSION VERSION_LESS 3.22)
        message(FATAL_ERROR "BUILD_ENGINE needs cmake >= 3.22; run this build with a newer cmake (see Dockerfile deps stage)")
    endif()
    set(LWPE_REF b016d7d1fdcf4e5fd2f9c9fa420a8aaa07fee02d CACHE STRING "upstream anchor")
    include(ExternalProject)
    file(GLOB KYLIN_PATCHES ${CMAKE_SOURCE_DIR}/patches/*.patch)
    set(_ENGINE_ARGS
        BINARY_DIR ${CMAKE_BINARY_DIR}/engine
        CMAKE_ARGS -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
                   -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/payload
                   -DBUILD_TESTING=OFF
    )
    if(EXISTS ${CMAKE_SOURCE_DIR}/third_party/linux-wallpaperengine/CMakeLists.txt)
        ExternalProject_Add(linux-wallpaperengine
            SOURCE_DIR ${CMAKE_SOURCE_DIR}/third_party/linux-wallpaperengine
            ${_ENGINE_ARGS}
        )
    else()
        ExternalProject_Add(linux-wallpaperengine
            GIT_REPOSITORY https://github.com/Almamu/linux-wallpaperengine.git
            GIT_TAG ${LWPE_REF}
            GIT_SUBMODULES_RECURSE ON
            PATCH_COMMAND git apply --verbose ${KYLIN_PATCHES}
            ${_ENGINE_ARGS}
        )
    endif()
endif()

# ---- install
install(TARGETS wallpaper-engine peony-alpha
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib)

# ---- tests (off for the deb integration build stage, which has no tests/)
option (BUILD_TESTING "Build the test tree" ON)
if (BUILD_TESTING)
    enable_testing ()
    add_subdirectory (tests)
endif ()

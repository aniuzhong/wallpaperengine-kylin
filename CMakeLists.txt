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
# the marker's text rendering (src/integration/marker.cpp)
pkg_check_modules(FREETYPE REQUIRED freetype2)

# JSON for the config file: fetched at configure time, pinned to a release
# tag — git over HTTPS, the same path the engine fetch uses.
include(FetchContent)
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(nlohmann_json)

# Result<T, E> for the systemd module (src/systemd_unit.h): std::expected
# backported to C++17, single header. Errors travel as values; see the
# module header for the convention.
#
# Populated, not MakeAvailable: tl's own CMakeLists carries unconditional
# install() rules, and MakeAvailable would teach `cmake --install` to ship
# its headers and package config into our payload — the deb is not tl's
# distribution channel. A header-only library needs an imported interface
# target and nothing else.
FetchContent_Declare(tl_expected
    GIT_REPOSITORY https://github.com/TartanLlama/expected.git
    GIT_TAG v1.1.0
    GIT_SHALLOW TRUE)
FetchContent_GetProperties(tl_expected)
if(NOT tl_expected_POPULATED)
    FetchContent_Populate(tl_expected)
    add_library(tl_expected_headers INTERFACE)
    target_include_directories(tl_expected_headers INTERFACE "${tl_expected_SOURCE_DIR}/include")
    add_library(tl::expected ALIAS tl_expected_headers)
endif()

# fmt for the shim's log formatting (src/shim/peony-alpha.cpp): the shim's
# one deliberate third-party payload, header-only through an interface
# target — same Populate-not-MakeAvailable reasoning as tl::expected, fmt's
# install() rules must not reach the deb. FMT_HEADER_ONLY compiles it into
# the single shim TU, where -fvisibility=hidden keeps every emitted symbol
# out of the interposition ABI.
FetchContent_Declare(fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 11.1.4
    GIT_SHALLOW TRUE)
FetchContent_GetProperties(fmt)
if(NOT fmt_POPULATED)
    FetchContent_Populate(fmt)
    add_library(fmt_headers INTERFACE)
    target_include_directories(fmt_headers INTERFACE "${fmt_SOURCE_DIR}/include")
    target_compile_definitions(fmt_headers INTERFACE FMT_HEADER_ONLY=1)
    add_library(fmt::fmt ALIAS fmt_headers)
endif()

# reproc for the service layer's child processes (src/process.cpp): start +
# drain + deadline + stop replace the hand-rolled fork/exec/poll loop. This
# is a MakeAvailable, unlike tl::expected above, because reproc guards all
# of its install rules behind REPROC_INSTALL — with it off, nothing of
# reproc's reaches the deb payload. The C++ wrapper (reproc++) carries the
# drain/sink API the runners use.
set(REPROC_INSTALL OFF)
set(REPROC++ ON)
FetchContent_Declare(reproc
    GIT_REPOSITORY https://github.com/DaanDeMeyer/reproc.git
    GIT_TAG v14.2.7
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(reproc)

# stb_image_write for the marker's PNG encoding (src/integration/marker.cpp).
# nothings/stb has no release tags at all — pin a commit and name the header
# version it carries; and it has no CMakeLists, so Populate + INTERFACE (the
# tl::expected pattern) is the only integration there is. Only the write
# half is used: stb_image's decoder has no consumer in this tree.
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20) # stb_image_write 1.16
FetchContent_GetProperties(stb)
if(NOT stb_POPULATED)
    FetchContent_Populate(stb)
    add_library(stb_headers INTERFACE)
    target_include_directories(stb_headers INTERFACE "${stb_SOURCE_DIR}")
    add_library(stb::stb ALIAS stb_headers)
endif()

# ---- wallpaper_service
# Control core (frontend-free): the generic systemd D-Bus client plus the
# wallpaper domain logic (config persistence, argv mapping, library scan,
# peony integration, engine-unit facade). Qt-free by design — sd-bus for the
# buses, nlohmann for JSON, xcb-randr where the platform needs them. Frontends link this and nothing else — they must never talk to
# systemctl or craft unit files themselves.
add_library(wallpaper_service STATIC
    src/systemd_unit.cpp
    src/exec_args.cpp
    src/config.cpp
    src/projection.cpp
    src/library.cpp
    src/report.cpp
    src/peonybuilder.cpp
    src/integration/marker.cpp
    src/integration/peony.cpp
    src/utils/xcbutils.cpp
    src/engine_unit.cpp
    src/process.cpp
    src/display.cpp
    src/unit_file.cpp
    src/lwe/toargv.cpp
    src/lwe/fromargv.cpp
    src/lwe/validate.cpp
)
target_include_directories(wallpaper_service PUBLIC src ${XCB_RANDR_INCLUDE_DIRS} ${FREETYPE_INCLUDE_DIRS})
target_link_libraries(wallpaper_service PUBLIC nlohmann_json::nlohmann_json tl::expected reproc++ stb::stb ${FREETYPE_LIBRARIES} ${XCB_LIBRARIES} ${XCB_RANDR_LIBRARIES} ${LIBSYSTEMD_LIBRARIES})
target_compile_options(wallpaper_service PRIVATE -Wall -Wextra)

# ---- wallpaper-engine
# The headless CLI frontend. Qt-free like the service layer — it links this
# and nothing else, so a Qt include anywhere in src/ (outside hook/) breaks
# the link.
add_executable(wallpaper-engine
    src/main.cpp
    src/cli.cpp
)
target_link_libraries(wallpaper-engine PRIVATE wallpaper_service Threads::Threads)
target_include_directories(wallpaper-engine PRIVATE src)
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
target_link_libraries(peony-alpha PRIVATE ${X11_LIBRARIES} ${XCB_LIBRARIES} fmt::fmt dl)

# ---- engine (optional)
# Two equivalent acquisition paths, everything stays inside build/:
#   - third_party/linux-wallpaperengine (gitignored) holds a patched upstream
#     checkout copied in by hand: zero network, used when present
#   - otherwise cmake fetches upstream at LWE_REF and applies patches/ —
#     this is what CI does, so a patch that no longer applies fails there
# Requires cmake >= 3.22 for the glslang submodule; Kylin's 3.16 builds only
# the controller (leave BUILD_ENGINE off).
option(BUILD_ENGINE "Build the engine from third_party or fetched upstream" OFF)
if(BUILD_ENGINE)
    if(CMAKE_VERSION VERSION_LESS 3.22)
        message(FATAL_ERROR "BUILD_ENGINE needs cmake >= 3.22; run this build with a newer cmake (see Dockerfile deps stage)")
    endif()
    set(LWE_REF b016d7d1fdcf4e5fd2f9c9fa420a8aaa07fee02d CACHE STRING "upstream anchor")
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
            GIT_TAG ${LWE_REF}
            GIT_SUBMODULES_RECURSE ON
            PATCH_COMMAND git apply --verbose ${KYLIN_PATCHES}
            ${_ENGINE_ARGS}
        )
    endif()
    # The engine installs into its own payload prefix at build time; fold it
    # into this project's install rules so one `cmake --install` (and the
    # cpack run in scripts/build-deb.sh) stages the complete deb payload.
    install(DIRECTORY ${CMAKE_BINARY_DIR}/payload/
            DESTINATION . USE_SOURCE_PERMISSIONS)
endif()

# ---- install
install(TARGETS wallpaper-engine peony-alpha
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib)
install(FILES deb/copyright DESTINATION share/doc/wallpaper-engine-kylin)

# ---- packaging
# The deb is CPack's DEB generator: scripts/build-deb.sh stages this
# project's install rules into a scratch prefix, runs the payload preflight
# against it, then hands the version to `cpack -R`. deb/ keeps only what
# CPack cannot generate — the maintainer scripts and the copyright file;
# deb/control was retired, its fields live in this block.
#
# Depends is the audited list from the retired control file. shlibdeps stays
# off: it would probe the build container's library set, not Kylin's.
set(CPACK_PACKAGE_NAME "wallpaper-engine-kylin")
set(CPACK_PACKAGE_VERSION "0.1.0-dev") # overridden per build with cpack -R
set(CPACK_PACKAGE_VENDOR "Aniuzhong")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/aniuzhong/wallpaperengine-kylin")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Wallpaper Engine wallpapers on Kylin V10 SP1 (linux-wallpaperengine)")
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION
    "Repackaging of linux-wallpaperengine for Kylin V10 SP1 with UKUI desktop
 integration: renders Steam Workshop wallpapers as the desktop background.
 Includes the peony interposition shim for transparent desktop icon support.
 The engine binary and libraries install under /opt/wallpaper-engine;
 the wallpaper runs under the user's own systemd session (no root service).")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER
    "Aniuzhong <101186392+aniuzhong@users.noreply.github.com>")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/aniuzhong/wallpaperengine-kylin")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "libavcodec58, libavformat58, libavutil56, libdbus-1-3, libfreetype6, libglew2.1, libglfw3, libgl1, liblz4-1, libmpv1, libpng16-16, libpulse0, libsdl2-2.0-0, libswresample3, libsystemd0, libx11-6, libxcb-randr1, libxcb1, libxrandr2")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
# <pkg>_<version>_<arch>.deb, assembled at packaging time from the version
# cpack -R passes in (cmake 3.16 has no default here — without it the name
# falls back to the generic dash style)
set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_SOURCE_DIR}/deb/postinst;${CMAKE_SOURCE_DIR}/deb/prerm")
# 0755 on the maintainer scripts regardless of how git checked them out
set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION YES)
# every relative install() destination hangs off the deb prefix; the
# copyright rides along under it
set(CPACK_PACKAGING_INSTALL_PREFIX "/opt/wallpaper-engine")
set(CPACK_STRIP_FILES YES)
include(CPack)

# ---- tests (off for the deb integration build stage, which has no tests/)
option (BUILD_TESTING "Build the test tree" ON)
if (BUILD_TESTING)
    enable_testing ()
    add_subdirectory (tests)
endif ()

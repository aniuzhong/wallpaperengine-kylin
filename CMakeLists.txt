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

# Result<T, E> for the systemd module (src/systemd_unit.h): std::expected
# backported to C++17, single header. Errors travel as values; see the
# module header for the convention.
FetchContent_Declare(tl_expected
    GIT_REPOSITORY https://github.com/TartanLlama/expected.git
    GIT_TAG v1.1.0
    GIT_SHALLOW TRUE)
set(EXPECTED_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(EXPECTED_BUILD_PACKAGE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(tl_expected)

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
    src/argvbuilder.cpp
    src/library.cpp
    src/report.cpp
    src/peonybuilder.cpp
    src/integration/marker.cpp
    src/integration/peony.cpp
    src/engine_unit.cpp
    src/process.cpp
    src/display.cpp
    src/unit_file.cpp
)
target_include_directories(wallpaper_service PUBLIC src ${XCB_RANDR_INCLUDE_DIRS})
target_link_libraries(wallpaper_service PUBLIC nlohmann_json::nlohmann_json tl::expected ${XCB_LIBRARIES} ${XCB_RANDR_LIBRARIES} ${LIBSYSTEMD_LIBRARIES})
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

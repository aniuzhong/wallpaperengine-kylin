cmake_minimum_required(VERSION 3.10)
project(wallpaperengine-kylin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets DBus)
find_package(X11 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(XCB REQUIRED xcb)
pkg_check_modules(XCB_RANDR REQUIRED xcb-randr)
pkg_check_modules(PNG REQUIRED libpng)

# ---- wallpaper_service
# Control core (frontend-free): the generic systemd D-Bus client plus the
# wallpaper domain logic (config persistence, argv mapping, library scan,
# peony integration, engine-unit facade). Frontends link this and nothing
# else — they must never talk to systemctl or craft unit files themselves.
add_library(wallpaper_service STATIC
    src/service/systemd/systemdunit.cpp
    src/service/systemd/unitbuilder.cpp
    src/service/config.cpp
    src/service/argvbuilder.cpp
    src/service/library.cpp
    src/service/integration.cpp
    src/service/engineunit.cpp
)
target_include_directories(wallpaper_service PUBLIC src/service src/service/systemd ${XCB_RANDR_INCLUDE_DIRS} ${PNG_INCLUDE_DIRS})
target_link_libraries(wallpaper_service PUBLIC Qt5::Core Qt5::DBus ${XCB_LIBRARIES} ${XCB_RANDR_LIBRARIES} ${PNG_LIBRARIES})
target_compile_options(wallpaper_service PRIVATE -Wall -Wextra)

# ---- wallpaper-engine
# Frontend binary: headless CLI plus the Wallpaper UI window (the only GUI;
# the legacy minimal UI was removed at the start of M1).
add_executable(wallpaper-engine
    src/ui/main.cpp
    src/ui/cli.cpp
    src/ui/wallpaperui/wallpaperui.cpp
    src/ui/wallpaperui/detailpanel.cpp
    src/ui/wallpaperui/griddelegate.cpp
    src/ui/wallpaperui/placeholder.cpp
    src/ui/wallpaperui/preview.cpp
)
target_link_libraries(wallpaper-engine PRIVATE Qt5::Core Qt5::Widgets wallpaper_service)
target_include_directories(wallpaper-engine PRIVATE src/ui)
target_compile_options(wallpaper-engine PRIVATE -Wall -Wextra)

# ---- peony-alpha-shim
# LD_PRELOAD interposition library: makes peony-qt-desktop's background
# transparent and rewrites its window type for ukui-kwin. Exported symbols
# carry explicit visibility attributes in the source; Qt symbols resolve
# from the host process at load time, hence they are not linked here.
add_library(peony-alpha-shim SHARED
    src/hook/peony-alpha-shim.cpp
)
target_compile_options(peony-alpha-shim PRIVATE -O2 -Wall -fPIC -fvisibility=hidden)
target_include_directories(peony-alpha-shim PRIVATE ${Qt5Gui_INCLUDE_DIRS} ${X11_INCLUDE_DIR} ${XCB_INCLUDE_DIRS})
target_link_libraries(peony-alpha-shim PRIVATE ${X11_LIBRARIES} ${XCB_LIBRARIES} dl)

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
install(TARGETS wallpaper-engine peony-alpha-shim
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib)

# ---- tests (off for the deb integration build stage, which has no tests/)
option (BUILD_TESTING "Build the test tree" ON)
if (BUILD_TESTING)
    enable_testing ()
    add_subdirectory (tests)
endif ()

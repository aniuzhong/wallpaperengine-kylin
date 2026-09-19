cmake_minimum_required(VERSION 3.10)
project(wallpaperengine-kylin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets DBus)
find_package(X11 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(XCB REQUIRED xcb)

# ---- wallpaper-engine
# UI / CLI control surface for the integration(scaffold: config + selftest)
add_executable(wallpaper-engine
    src/ui/main.cpp
    src/ui/cli.cpp
    src/ui/config.cpp
    src/ui/argvbuilder.cpp
    src/ui/library.cpp
    src/ui/systemd.cpp
    src/ui/mainwindow.cpp
    src/ui/integration.cpp
)
target_link_libraries(wallpaper-engine PRIVATE Qt5::Core Qt5::Widgets systemd_layer) 
target_include_directories(wallpaper-engine PRIVATE src/ui)
target_compile_options(wallpaper-engine PRIVATE -Wall -Wextra)

# ---- peony-alpha-shim
# LD_PRELOAD interposition library: makes peony-qt-desktop's background
# transparent and rewrites its window type for ukui-kwin. Exported symbols
# carry explicit visibility attributes in the source; Qt symbols resolve
# from the host process at load time, hence they are not linked here.
# systemd control layer (used by the integration sources; also unit-tested)
add_library(systemd_layer STATIC
    src/systemd/systemdunit.cpp
    src/systemd/unitbuilder.cpp
)
target_include_directories(systemd_layer PUBLIC src/systemd)
target_link_libraries(systemd_layer PUBLIC Qt5::Core Qt5::DBus)
target_compile_options(systemd_layer PRIVATE -Wall -Wextra)

add_library(peony-alpha-shim SHARED
    src/shim/peony-alpha-shim.cpp
)
target_compile_options(peony-alpha-shim PRIVATE -O2 -Wall -fPIC -fvisibility=hidden)
target_include_directories(peony-alpha-shim PRIVATE ${Qt5Gui_INCLUDE_DIRS} ${X11_INCLUDE_DIR} ${XCB_INCLUDE_DIRS})
target_link_libraries(peony-alpha-shim PRIVATE ${X11_LIBRARIES} ${XCB_LIBRARIES} dl)

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

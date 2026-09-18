cmake_minimum_required(VERSION 3.10)
project(wallpaperengine-kylin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt5 REQUIRED COMPONENTS Core Gui)
find_package(X11 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(XCB REQUIRED xcb)

# ---- wallpaper-engine
# UI / CLI control surface for the integration(scaffold: config + selftest)
add_executable(wallpaper-engine
    src/ui/main.cpp
    src/ui/config.cpp
)
target_link_libraries(wallpaper-engine PRIVATE Qt5::Core)
target_include_directories(wallpaper-engine PRIVATE src/ui)
target_compile_options(wallpaper-engine PRIVATE -Wall -Wextra)

# ---- peony-alpha-shim
# LD_PRELOAD interposition library: makes peony-qt-desktop's background
# transparent and rewrites its window type for ukui-kwin. Exported symbols
# carry explicit visibility attributes in the source; Qt symbols resolve
# from the host process at load time, hence they are not linked here.
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

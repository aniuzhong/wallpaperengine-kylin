cmake_minimum_required (VERSION 3.10)
project (peony-alpha-shim LANGUAGES CXX)

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)

find_package (Qt5 REQUIRED COMPONENTS Gui)
find_package (X11 REQUIRED)
find_package (PkgConfig REQUIRED)
pkg_check_modules (XCB REQUIRED xcb)

# LD_PRELOAD interposition library that makes peony-qt-desktop's background
# transparent and rewrites its window type for ukui-kwin. Exported symbols
# are declared individually with visibility attributes in the source.
add_library (peony-alpha-shim SHARED peony-alpha-shim.cpp)

target_compile_options (peony-alpha-shim PRIVATE -O2 -Wall -fPIC -fvisibility=hidden)
target_include_directories (peony-alpha-shim PRIVATE ${Qt5Gui_INCLUDE_DIRS} ${X11_INCLUDE_DIR} ${XCB_INCLUDE_DIRS})
target_link_libraries (peony-alpha-shim PRIVATE ${X11_LIBRARIES} ${XCB_LIBRARIES} dl)

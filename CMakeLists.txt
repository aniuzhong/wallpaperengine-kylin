cmake_minimum_required(VERSION 3.10)
project(wallpaperengine-kylin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets DBus)
find_package(X11 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(XCB REQUIRED xcb)

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
target_include_directories(wallpaper_service PUBLIC src/service src/service/systemd)
target_link_libraries(wallpaper_service PUBLIC Qt5::Core Qt5::Gui Qt5::DBus)
target_compile_options(wallpaper_service PRIVATE -Wall -Wextra)

# ---- wallpaper-engine
# Frontend binary: headless CLI plus the UI windows. Default entry is the
# legacy minimal UI; `--wallpaper-ui` opens the new Wallpaper UI scaffold.
add_executable(wallpaper-engine
    src/ui/main.cpp
    src/ui/cli.cpp
    src/ui/mainwindow.cpp
    src/ui/wallpaperui/wallpaperui.cpp
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

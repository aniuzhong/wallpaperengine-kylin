# systemd layer tests: pure logic always runs; integration tests need a
# systemd user session bus and skip automatically when it is absent.
find_package (Qt5 REQUIRED COMPONENTS Core DBus Test)
pkg_check_modules (LIBSYSTEMD REQUIRED libsystemd)

add_executable (exec_args_test exec_args_test.cpp ../src/exec_args.cpp)
target_link_libraries (exec_args_test PRIVATE Qt5::Core Qt5::Test)
add_test (NAME exec_args_test COMMAND exec_args_test)

# T0: Config -> argv mapping (pure)
add_executable (argvbuilder_test argvbuilder_test.cpp ../src/argvbuilder.cpp ../src/config.cpp)
target_link_libraries (argvbuilder_test PRIVATE Qt5::Core Qt5::Test nlohmann_json::nlohmann_json tl::expected)
add_test (NAME argvbuilder_test COMMAND argvbuilder_test)

# T0: Config persistence roundtrip
add_executable (config_test config_test.cpp ../src/config.cpp)
target_link_libraries (config_test PRIVATE Qt5::Core Qt5::Test nlohmann_json::nlohmann_json tl::expected)
add_test (NAME config_test COMMAND config_test)

# T0: library metadata scan (pure; previews are paths, no decoding here)
add_executable (library_test library_test.cpp ../src/library.cpp)
target_link_libraries (library_test PRIVATE Qt5::Core Qt5::Test nlohmann_json::nlohmann_json)
add_test (NAME library_test COMMAND library_test)

# T0: the integration marker's rendering half (PNG structure, contrast,
# determinism)
add_executable (marker_test marker_test.cpp ../src/integration/marker.cpp)
target_link_libraries (marker_test PRIVATE Qt5::Core Qt5::Gui Qt5::Test tl::expected stb::stb ${FREETYPE_LIBRARIES})
target_include_directories (marker_test PRIVATE ${FREETYPE_INCLUDE_DIRS})
add_test (NAME marker_test COMMAND marker_test)

# T0: JSON projections of service data (pure) — the wire contract the CLI
# publishes with --json, shared with any other frontend
add_executable (report_test report_test.cpp ../src/report.cpp)
target_link_libraries (report_test PRIVATE Qt5::Core Qt5::Test nlohmann_json::nlohmann_json)
add_test (NAME report_test COMMAND report_test)

# T0: the peony integration's pure half (wallpaper candidate list, shim
# environment) — integration/peony.cpp keeps only the syscall sequence
add_executable (peonybuilder_test peonybuilder_test.cpp ../src/peonybuilder.cpp)
target_link_libraries (peonybuilder_test PRIVATE Qt5::Core Qt5::Test)
add_test (NAME peonybuilder_test COMMAND peonybuilder_test)

# smoke: the CLI frontend and the service archive must link and run WITHOUT
# Qt at all — guards the Qt-free boundary (any Qt include in src/ outside
# hook/ fails this target's link, since no Qt library is on its link line)
add_executable (service_smoke_test service_smoke_test.cpp ../src/cli.cpp)
target_link_libraries (service_smoke_test PRIVATE wallpaper_service)
add_test (NAME service_smoke_test COMMAND service_smoke_test)

# T3: headless CLI end-to-end against the real user manager (isolated by
# the dedicated wallpaper-engine-e2e-test unit name; config stays the user's own and is
# restored by cleanup)
add_executable (cli_e2e_test cli_e2e_test.cpp ../src/config.cpp ../src/engine_unit.cpp ../src/argvbuilder.cpp)
target_link_libraries (cli_e2e_test PRIVATE Qt5::Core Qt5::DBus Qt5::Test wallpaper_service)
target_compile_definitions (cli_e2e_test PRIVATE
    WALLPAPER_ENGINE_BIN="${CMAKE_BINARY_DIR}/wallpaper-engine")
add_test (NAME cli_e2e_test COMMAND cli_e2e_test)

add_executable (systemd_unit_test systemd_unit_test.cpp ../src/systemd_unit.cpp ../src/exec_args.cpp)
target_link_libraries (systemd_unit_test PRIVATE Qt5::Core Qt5::DBus Qt5::Test tl::expected ${LIBSYSTEMD_LIBRARIES})
add_test (NAME systemd_unit_test COMMAND systemd_unit_test)

# ---- shim interposition coverage ------------------------------------------
# probe: tiny Qt binary that exercises one shim hook per mode
pkg_check_modules (XCB_PROBE REQUIRED xcb)
add_executable (shim_probe shim_probe.cpp)
target_link_libraries (shim_probe PRIVATE Qt5::Gui ${XCB_PROBE_LIBRARIES})
target_include_directories (shim_probe PRIVATE ${XCB_PROBE_INCLUDE_DIRS})
set_target_properties (shim_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# hook assertions run the probe with LD_PRELOAD set to the shim
add_executable (shim_hook_test shim_hook_test.cpp)
target_link_libraries (shim_hook_test PRIVATE Qt5::Core Qt5::Gui Qt5::Test)
target_compile_definitions (shim_hook_test PRIVATE
    SHIM_PATH="${CMAKE_BINARY_DIR}/libpeony-alpha.so"
    PROBE_PATH="${CMAKE_BINARY_DIR}/shim_probe"
    MARKER_PATH="${CMAKE_BINARY_DIR}/test-marker.png")
add_test (NAME shim_hook_test COMMAND shim_hook_test)
set_tests_properties (shim_hook_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

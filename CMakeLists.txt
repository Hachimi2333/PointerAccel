cmake_minimum_required(VERSION 3.25)
project(
  PointerAccel
  VERSION 0.1.0
  LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(CTest)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(Qt6 REQUIRED COMPONENTS Core DBus Gui Widgets)
find_package(KWin 6.7 REQUIRED)
if(KWin_VERSION VERSION_GREATER_EQUAL 6.8)
  message(FATAL_ERROR "KWin 6.8 or newer needs an adapter review. Supported series: 6.7.")
endif()
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBINPUT REQUIRED IMPORTED_TARGET libinput)
pkg_check_modules(UDEV REQUIRED IMPORTED_TARGET libudev)
get_target_property(PA_KWIN_LIBRARY KWin::kwin IMPORTED_LOCATION_RELEASE)
if(NOT PA_KWIN_LIBRARY)
  get_target_property(PA_KWIN_LIBRARY KWin::kwin IMPORTED_LOCATION)
endif()
file(REAL_PATH "${PA_KWIN_LIBRARY}" PA_KWIN_LIBRARY)
file(SHA256 "${PA_KWIN_LIBRARY}" PA_KWIN_SHA256)
set(PA_KWIN_EXECUTABLE /usr/bin/kwin_wayland)
file(SHA256 "${PA_KWIN_EXECUTABLE}" PA_EXECUTABLE_SHA256)

foreach(PA_IMAGE KWIN EXECUTABLE)
  if(PA_IMAGE STREQUAL "KWIN")
    set(PA_IMAGE_PATH "${PA_KWIN_LIBRARY}")
  else()
    set(PA_IMAGE_PATH "${PA_KWIN_EXECUTABLE}")
  endif()
  execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/maintenance.py" elf-id "${PA_IMAGE_PATH}"
    OUTPUT_VARIABLE PA_${PA_IMAGE}_BUILD_ID
    OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
endforeach()

configure_file(src/platform/build_info.hpp.in generated/build_info.hpp @ONLY)

add_compile_options(-Wall -Wextra -Wpedantic)

# Internal build units only; headers are private application sources under src.
add_library(
  pa_core STATIC
  src/core/pointer_engine.cpp
  src/config/config.cpp
  src/platform/paths.cpp
  src/platform/json_io.cpp
  src/platform/build_metadata.cpp
  src/platform/runtime_identity.cpp
  src/input/validation.cpp)
target_include_directories(pa_core PUBLIC src "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(pa_core PUBLIC Qt6::Core ${CMAKE_DL_LIBS})

set(PA_CAPTURE_SOURCES src/input/capture.cpp src/input/import_hooks.cpp)
add_library(pa_capture STATIC ${PA_CAPTURE_SOURCES})
target_include_directories(pa_capture PUBLIC src)
target_link_libraries(pa_capture PUBLIC Qt6::Core PkgConfig::LIBINPUT ${CMAKE_DL_LIBS})

set(PA_KWIN_SOURCES src/kwin/plugin_control.hpp src/kwin/plugin.cpp src/kwin/runtime.cpp src/kwin/devices.cpp
                    src/kwin/diagnostics.cpp)
function(pa_kwin_target target capture stage)
  add_library(${target} MODULE ${PA_KWIN_SOURCES} ${ARGN})
  target_link_libraries(${target} PRIVATE pa_core ${capture} KWin::kwin Qt6::DBus PkgConfig::UDEV)
  set_target_properties(
    ${target}
    PROPERTIES PREFIX ""
               OUTPUT_NAME pointeraccel_kwin
               LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${stage}/qt6/plugins/kwin/plugins")
endfunction()
pa_kwin_target(pointeraccel_kwin pa_capture stage)

find_package(KF6Config REQUIRED)
set(PA_CONTROL_SOURCES
    src/control/client.cpp
    src/maintenance/context.cpp
    src/maintenance/installation.cpp
    src/maintenance/startup.cpp
    src/maintenance/session_watch.cpp
    src/maintenance/watch_paths.cpp
    src/maintenance/repair.cpp)
add_library(pa_control STATIC ${PA_CONTROL_SOURCES})
target_link_libraries(pa_control PUBLIC pa_core Qt6::DBus KF6::ConfigCore)

set(PA_CLI_SOURCES src/cli/main.cpp src/cli/plugin_config.cpp)
add_executable(pointeraccel ${PA_CLI_SOURCES})
target_link_libraries(pointeraccel PRIVATE pa_control)

add_library(
  pa_gui STATIC
  src/gui/controller.cpp
  src/gui/worker.cpp
  src/gui/main_window.cpp
  src/gui/settings_page.cpp
  src/gui/devices_page.cpp
  src/gui/device_model.cpp
  src/gui/diagnostics_page.cpp
  src/gui/facts.cpp)
target_include_directories(pa_gui PUBLIC src/gui)
target_link_libraries(pa_gui PUBLIC pa_control Qt6::Widgets)
add_executable(pointeraccel-gui src/gui/main.cpp)
target_link_libraries(pointeraccel-gui PRIVATE pa_gui)
qt_add_resources(
  pointeraccel-gui
  "pointeraccel_icons"
  PREFIX
  "/icons"
  BASE
  "src/gui/icons"
  FILES
  src/gui/icons/pointeraccel-16.png
  src/gui/icons/pointeraccel-24.png
  src/gui/icons/pointeraccel-32.png
  src/gui/icons/pointeraccel-48.png
  src/gui/icons/pointeraccel-64.png
  src/gui/icons/pointeraccel-128.png
  src/gui/icons/pointeraccel-256.png
  src/gui/icons/pointeraccel-512.png)

pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(XINPUT REQUIRED IMPORTED_TARGET xi x11)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
foreach(PROTOCOL xdg-shell relative-pointer)
  if(PROTOCOL STREQUAL "xdg-shell")
    set(XML "${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml")
  else()
    set(XML "${WAYLAND_PROTOCOLS_DIR}/unstable/relative-pointer/relative-pointer-unstable-v1.xml")
  endif()
  add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/${PROTOCOL}-client-protocol.h"
    COMMAND ${WAYLAND_SCANNER} client-header "${XML}"
            "${CMAKE_CURRENT_BINARY_DIR}/generated/${PROTOCOL}-client-protocol.h"
    DEPENDS "${XML}")
  add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/${PROTOCOL}-protocol.c"
    COMMAND ${WAYLAND_SCANNER} private-code "${XML}" "${CMAKE_CURRENT_BINARY_DIR}/generated/${PROTOCOL}-protocol.c"
    DEPENDS "${XML}")
endforeach()

add_executable(
  pointeraccel-raw-wayland
  src/probes/raw_wayland.c
  "${CMAKE_CURRENT_BINARY_DIR}/generated/xdg-shell-client-protocol.h"
  "${CMAKE_CURRENT_BINARY_DIR}/generated/xdg-shell-protocol.c"
  "${CMAKE_CURRENT_BINARY_DIR}/generated/relative-pointer-client-protocol.h"
  "${CMAKE_CURRENT_BINARY_DIR}/generated/relative-pointer-protocol.c")
target_include_directories(pointeraccel-raw-wayland PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(pointeraccel-raw-wayland PRIVATE PkgConfig::WAYLAND_CLIENT)

add_executable(pointeraccel-raw-x11 src/probes/raw_x11.cpp)
target_link_libraries(pointeraccel-raw-x11 PRIVATE PkgConfig::XINPUT)

if(BUILD_TESTING)

  add_library(pa_control_test_support STATIC ${PA_CONTROL_SOURCES})
  target_compile_definitions(pa_control_test_support PRIVATE PA_TESTING)
  target_link_libraries(pa_control_test_support PUBLIC pa_core Qt6::DBus KF6::ConfigCore)

  add_executable(pointeraccel-test ${PA_CLI_SOURCES})
  target_link_libraries(pointeraccel-test PRIVATE pa_control_test_support)
  find_package(Qt6 REQUIRED COMPONENTS Test)

  add_executable(pa_control_peer tests/control_peer.cpp)
  target_link_libraries(pa_control_peer PRIVATE pa_control)

  add_executable(pa_gui_test tests/gui_test.cpp)
  target_link_libraries(pa_gui_test PRIVATE pa_gui Qt6::Widgets Qt6::Test)
  add_dependencies(pa_gui_test pa_control_peer)
  add_test(NAME gui_control COMMAND dbus-run-session -- $<TARGET_FILE:pa_gui_test>)
  set_tests_properties(gui_control PROPERTIES TIMEOUT 90 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  foreach(PA_SCALE 1.25 2)
    add_test(NAME gui_scale_${PA_SCALE} COMMAND dbus-run-session -- $<TARGET_FILE:pa_gui_test> widgetFlow)
    set_tests_properties(gui_scale_${PA_SCALE} PROPERTIES TIMEOUT 40 ENVIRONMENT
                                                          "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=${PA_SCALE}")
  endforeach()

  add_executable(pa_gui_driver tests/gui_driver.cpp)
  target_link_libraries(pa_gui_driver PRIVATE pa_gui Qt6::Widgets)
  find_package(GTest REQUIRED)

  add_executable(pa_core_test tests/core_test.cpp)
  target_link_libraries(pa_core_test PRIVATE pa_core GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(pa_core_test)

  add_executable(pa_engine_test tests/engine/engine_test.cpp tests/engine/model_test.cpp tests/engine/scenario_test.cpp
                                tests/engine/state_test.cpp)
  target_link_libraries(pa_engine_test PRIVATE pa_core GTest::gtest_main)
  gtest_discover_tests(pa_engine_test)

  add_library(pa_fake_libinput SHARED tests/fake_libinput.cpp)

  add_library(pa_fake_consumer SHARED tests/fake_consumer.cpp)
  target_link_libraries(pa_fake_consumer PRIVATE pa_fake_libinput)
  target_link_options(pa_fake_consumer PRIVATE -Wl,-z,relro,-z,now)

  add_executable(pa_capture_test tests/capture_test.cpp)
  target_include_directories(pa_capture_test PRIVATE src)
  target_link_libraries(pa_capture_test PRIVATE pa_capture pa_fake_consumer pa_fake_libinput)
  add_test(NAME capture_import_hooks COMMAND pa_capture_test)

  add_library(pa_capture_test_support STATIC ${PA_CAPTURE_SOURCES})
  target_include_directories(pa_capture_test_support PUBLIC src)
  target_compile_definitions(pa_capture_test_support PUBLIC PA_TESTING)
  target_link_libraries(pa_capture_test_support PUBLIC Qt6::Core PkgConfig::LIBINPUT ${CMAKE_DL_LIBS})

  pa_kwin_target(pointeraccel_kwin_test pa_capture_test_support teststage tests/kwin_test_support.cpp)
  target_include_directories(pointeraccel_kwin_test PRIVATE tests)
  pa_kwin_target(pointeraccel_kwin_mismatch pa_capture mismatchstage)
  target_compile_definitions(pointeraccel_kwin_mismatch PRIVATE PA_TEST_RUNTIME_MISMATCH)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(NAME nested_kwin COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tests/nested_test.py"
                                    "${CMAKE_CURRENT_BINARY_DIR}")
  set_tests_properties(nested_kwin PROPERTIES TIMEOUT 45)
  add_test(NAME install_roundtrip COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tests/install_test.py"
                                          "${CMAKE_CURRENT_BINARY_DIR}")
  set_tests_properties(install_roundtrip PROPERTIES TIMEOUT 60)
  add_test(NAME upgrade_guard COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tests/upgrade_test.py"
                                      "${CMAKE_CURRENT_BINARY_DIR}")
  set_tests_properties(upgrade_guard PROPERTIES TIMEOUT 90)
endif()

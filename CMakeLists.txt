cmake_minimum_required(VERSION 3.25)
project(
  PointerAccel
  VERSION 0.1.0
  LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(CTest)
option(PA_BUILD_DESKTOP "Build KWin plugin, CLI, GUI and diagnostic probes" ON)
option(PA_BUILD_BENCHMARKS "Build optional synthetic benchmarks" OFF)
add_compile_options(-Wall -Wextra -Wpedantic)

# Private, pure C++ engine target. The source bytes and arithmetic are frozen.
add_library(pa_engine STATIC src/core/pointer_engine.cpp)
target_include_directories(pa_engine PUBLIC src)
if(NOT PA_BUILD_DESKTOP)
  if(BUILD_TESTING)
    add_subdirectory(tests)
  endif()
  return()
endif()

set(CMAKE_AUTOMOC ON)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(Qt6 REQUIRED COMPONENTS Core DBus Gui Widgets)
find_package(KWin 6.7 REQUIRED)
if(KWin_VERSION VERSION_GREATER_EQUAL 6.9)
  message(FATAL_ERROR "KWin 6.9 or newer needs an adapter review. Supported series: 6.7, 6.8.")
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

add_library(pa_platform STATIC src/platform/paths.cpp src/platform/json_io.cpp src/platform/runtime_identity.cpp)
target_include_directories(pa_platform PUBLIC src)
target_link_libraries(pa_platform PUBLIC Qt6::Core ${CMAKE_DL_LIBS})

add_library(pa_config STATIC src/config/config.cpp)
target_link_libraries(pa_config PUBLIC pa_engine pa_platform)

add_library(pa_runtime_info STATIC src/platform/build_metadata.cpp)
target_include_directories(pa_runtime_info PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(pa_runtime_info PUBLIC pa_platform)

add_library(pa_input_validation STATIC src/input/validation.cpp)
target_link_libraries(pa_input_validation PUBLIC pa_engine)

set(PA_CAPTURE_SOURCES src/input/capture.cpp src/input/import_hooks.cpp)
add_library(pa_capture STATIC ${PA_CAPTURE_SOURCES})
target_include_directories(pa_capture PUBLIC src)
target_link_libraries(pa_capture PUBLIC Qt6::Core PkgConfig::LIBINPUT ${CMAKE_DL_LIBS})

set(PA_KWIN_SOURCES src/kwin/plugin_control.hpp src/kwin/plugin.cpp src/kwin/runtime.cpp src/kwin/devices.cpp
                    src/kwin/diagnostics.cpp)
function(pa_kwin_target target capture stage)
  add_library(${target} MODULE ${PA_KWIN_SOURCES} ${ARGN})
  target_link_libraries(
    ${target}
    PRIVATE pa_config
            pa_runtime_info
            pa_input_validation
            ${capture}
            KWin::kwin
            Qt6::DBus
            PkgConfig::UDEV)
  set_target_properties(
    ${target}
    PROPERTIES PREFIX ""
               OUTPUT_NAME pointeraccel_kwin
               LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${stage}/qt6/plugins/kwin/plugins")
endfunction()
pa_kwin_target(pointeraccel_kwin pa_capture stage)

find_package(KF6Config REQUIRED)
set(PA_MAINTENANCE_SOURCES src/maintenance/context.cpp src/maintenance/installation.cpp src/maintenance/startup.cpp
                           src/maintenance/session_watch.cpp src/maintenance/watch_paths.cpp src/maintenance/repair.cpp)
add_library(pa_maintenance STATIC ${PA_MAINTENANCE_SOURCES})
target_link_libraries(pa_maintenance PUBLIC pa_platform Qt6::DBus KF6::ConfigCore)
set(PA_CONTROL_SOURCES src/control/client.cpp)
add_library(pa_control STATIC ${PA_CONTROL_SOURCES})
target_link_libraries(pa_control PUBLIC pa_config pa_maintenance)

set(PA_CLI_SOURCES src/cli/main.cpp src/cli/plugin_config.cpp)
add_executable(pointeraccel ${PA_CLI_SOURCES})
target_link_libraries(pointeraccel PRIVATE pa_control pa_runtime_info)

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
  add_subdirectory(tests)
endif()

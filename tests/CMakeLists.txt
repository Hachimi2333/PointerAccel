set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
find_package(GTest REQUIRED)
include(GoogleTest)
add_test(NAME frozen_sources COMMAND ${CMAKE_COMMAND} -DPA_SOURCE_DIR=${PROJECT_SOURCE_DIR} -P
                                     ${PROJECT_SOURCE_DIR}/cmake/VerifyFrozen.cmake)
set_tests_properties(frozen_sources PROPERTIES LABELS "contract;unit" TIMEOUT 10)
add_executable(pa_engine_test engine/engine_test.cpp engine/model_test.cpp engine/scenario_test.cpp
                              engine/state_test.cpp)
target_link_libraries(pa_engine_test PRIVATE pa_engine GTest::gtest_main)
set_target_properties(pa_engine_test PROPERTIES AUTOMOC OFF)
gtest_discover_tests(
  pa_engine_test
  PROPERTIES LABELS unit
  TIMEOUT 20)
if(NOT PA_BUILD_DESKTOP)
  return()
endif()

foreach(source_list PA_MAINTENANCE_SOURCES PA_CONTROL_SOURCES PA_CLI_SOURCES PA_CAPTURE_SOURCES PA_KWIN_SOURCES)
  list(TRANSFORM ${source_list} PREPEND "${PROJECT_SOURCE_DIR}/")
endforeach()

add_library(pa_maintenance_test_support STATIC ${PA_MAINTENANCE_SOURCES})
target_compile_definitions(pa_maintenance_test_support PRIVATE PA_TESTING)
target_link_libraries(pa_maintenance_test_support PUBLIC pa_platform Qt6::DBus KF6::ConfigCore)
add_library(pa_control_test_support STATIC ${PA_CONTROL_SOURCES})
target_link_libraries(pa_control_test_support PUBLIC pa_config pa_maintenance_test_support)

add_executable(pointeraccel-test ${PA_CLI_SOURCES})
target_link_libraries(pointeraccel-test PRIVATE pa_control_test_support pa_runtime_info)
find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(pa_control_peer support/control_peer.cpp)
target_link_libraries(pa_control_peer PRIVATE pa_control)

add_executable(pa_control_test integration/control_test.cpp)
target_link_libraries(pa_control_test PRIVATE pa_control Qt6::Test)
target_include_directories(pa_control_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_dependencies(pa_control_test pa_control_peer)
add_test(NAME control_contract COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/support/qt_runner.py
                                       ${PROJECT_BINARY_DIR} $<TARGET_FILE:pa_control_test>)
set_tests_properties(control_contract PROPERTIES LABELS "contract;integration" TIMEOUT 40)

add_executable(pa_gui_test integration/gui_test.cpp)
target_link_libraries(pa_gui_test PRIVATE pa_gui Qt6::Widgets Qt6::Test)
target_include_directories(pa_gui_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_dependencies(pa_gui_test pa_control_peer)
add_test(NAME gui_control COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/support/qt_runner.py
                                  ${PROJECT_BINARY_DIR} $<TARGET_FILE:pa_gui_test>)
set_tests_properties(gui_control PROPERTIES TIMEOUT 90 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
foreach(PA_SCALE 1.25 2)
  add_test(NAME gui_scale_${PA_SCALE} COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/support/qt_runner.py
                                              ${PROJECT_BINARY_DIR} $<TARGET_FILE:pa_gui_test> widgetFlow)
  set_tests_properties(gui_scale_${PA_SCALE} PROPERTIES TIMEOUT 40 ENVIRONMENT
                                                        "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=${PA_SCALE}")
endforeach()

add_executable(pa_gui_driver support/gui_driver.cpp)
target_link_libraries(pa_gui_driver PRIVATE pa_gui Qt6::Widgets)
add_executable(pa_core_test unit/adapter_test.cpp)
target_link_libraries(pa_core_test PRIVATE pa_engine pa_platform pa_input_validation pa_config GTest::gtest_main)
gtest_discover_tests(
  pa_core_test
  PROPERTIES LABELS unit
  TIMEOUT 20)

add_library(pa_fake_libinput SHARED support/fake_libinput.cpp)

add_library(pa_fake_consumer SHARED support/fake_consumer.cpp)
target_link_libraries(pa_fake_consumer PRIVATE pa_fake_libinput)
target_link_options(pa_fake_consumer PRIVATE -Wl,-z,relro,-z,now)

add_executable(pa_capture_test integration/capture_test.cpp)
target_include_directories(pa_capture_test PRIVATE ${PROJECT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pa_capture_test PRIVATE pa_capture pa_fake_consumer pa_fake_libinput GTest::gtest_main)
gtest_discover_tests(
  pa_capture_test
  PROPERTIES LABELS integration
  TIMEOUT 20)

add_library(pa_capture_test_support STATIC ${PA_CAPTURE_SOURCES})
target_include_directories(pa_capture_test_support PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(pa_capture_test_support PUBLIC PA_TESTING)
target_link_libraries(pa_capture_test_support PUBLIC Qt6::Core PkgConfig::LIBINPUT ${CMAKE_DL_LIBS})

pa_kwin_target(pointeraccel_kwin_test pa_capture_test_support teststage support/kwin_test_support.cpp)
target_include_directories(pointeraccel_kwin_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
pa_kwin_target(pointeraccel_kwin_mismatch pa_capture mismatchstage)
target_compile_definitions(pointeraccel_kwin_mismatch PRIVATE PA_TEST_RUNTIME_MISMATCH)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME compatibility_preflight COMMAND Python3::Interpreter
                                              "${CMAKE_CURRENT_SOURCE_DIR}/unit/maintenance_test.py")
add_test(NAME nested_kwin COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/system/nested_test.py"
                                  "${PROJECT_BINARY_DIR}")
set_tests_properties(nested_kwin PROPERTIES TIMEOUT 45)
add_test(NAME install_roundtrip
         COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/system/install_test.py" "${PROJECT_BINARY_DIR}"
                 InstallationTest.test_recovery_upgrade_and_legacy_migration)
set_tests_properties(install_roundtrip PROPERTIES TIMEOUT 60)
add_test(NAME upgrade_guard COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/system/upgrade_test.py"
                                    "${PROJECT_BINARY_DIR}")
set_tests_properties(upgrade_guard PROPERTIES TIMEOUT 90)

add_test(NAME input_equivalence
         COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/system/input_equivalence_test.py"
                 "${PROJECT_BINARY_DIR}")
set_tests_properties(input_equivalence PROPERTIES LABELS "integration;contract" TIMEOUT 60)
set_tests_properties(gui_control gui_scale_1.25 gui_scale_2 PROPERTIES LABELS integration)
set_tests_properties(compatibility_preflight PROPERTIES LABELS "unit;contract" TIMEOUT 20)
set_tests_properties(nested_kwin PROPERTIES LABELS integration)
set_tests_properties(install_roundtrip upgrade_guard PROPERTIES LABELS system)
set_tests_properties(nested_kwin install_roundtrip upgrade_guard
                     PROPERTIES ENVIRONMENT "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/support")

if(PA_BUILD_BENCHMARKS)
  add_executable(pa_capture_benchmark benchmarks/capture_benchmark.cpp)
  target_include_directories(pa_capture_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(pa_capture_benchmark PRIVATE pa_capture pa_fake_consumer pa_fake_libinput)
endif()

foreach(scenario preserves_preexisting_configuration modified_file_blocks_uninstall)
  add_test(NAME install_${scenario} COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/system/install_test.py
                                            ${PROJECT_BINARY_DIR} InstallationTest.test_${scenario})
  set_tests_properties(install_${scenario} PROPERTIES LABELS system TIMEOUT 40)
endforeach()

# Current e73f4c6 bytes, distinct from the historical upstream-import hashes.
set(frozen_files
    "src/core/pointer_engine.cpp|dea3278a4de6f9c38d5a82c70182e6a028c9d545d333cfaa6841dbb49cf5b526"
    "src/core/pointer_engine.hpp|8ebb75ec1df4cb22b06f113575f0be821710f31db9f2707c326cbf41562e5e80"
    "tests/engine/state_baseline.hpp|3933832248ef13b422d9ff9802ab4d8cd0fa4117b9b4118fc8b99436cce0a362"
    "tests/fixtures/continuous_input.json|5ddf09b567d13200b3a979c91511f1f36cb118caba2032db53c1b0bb7396560e")
foreach(entry IN LISTS frozen_files)
  string(REPLACE "|" ";" pair "${entry}")
  list(GET pair 0 path)
  list(GET pair 1 expected)
  file(SHA256 "${PA_SOURCE_DIR}/${path}" actual)
  if(NOT actual STREQUAL expected)
    message(FATAL_ERROR "Frozen engine/baseline changed: ${path}. Do not regenerate the expected digest.")
  endif()
endforeach()

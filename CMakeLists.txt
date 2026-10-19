cmake_minimum_required(VERSION 3.28)

project(ShapeLogic
  VERSION 1.0.0
  DESCRIPTION "ShapeLogic core: puzzle model, solvers and the engine the web front end talks to"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BT_BUILD_TESTS "Build the test suite" ON)
option(BT_WERROR "Treat compiler warnings as errors" OFF)
set(BT_SANITIZE "" CACHE STRING "Sanitizers to build with: address, undefined, thread (comma separated)")
# The Python module (python/, deferred part 7), as the original's `python`
# option: ON requires it, AUTO builds it when Python's headers are found,
# except in sanitizer builds (Python cannot load an instrumented module
# unaided) and builds for another architecture (the original skipped
# cross builds).
set(BT_PYTHON "AUTO" CACHE STRING "Build the Python module: ON, OFF or AUTO")
set_property(CACHE BT_PYTHON PROPERTY STRINGS AUTO ON OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Python)
include(Warnings)
include(Sanitizers)
include(Dependencies)

add_subdirectory(core)
add_subdirectory(engine)
if(NOT EMSCRIPTEN)
  # the browser build (the wasm preset) is the engine alone
  add_subdirectory(cli)
endif()
if(BT_PYTHON_MODULE)
  add_subdirectory(python)
endif()

if(BT_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

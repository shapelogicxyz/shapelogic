# libbtcore: the puzzle model, file format, solvers and application layer.
# No UI, no stdout/stderr output, no global state: everything reaches it
# through its API (docs/ARCHITECTURE.md).
add_library(btcore STATIC
  bt/base/assert.cpp
  bt/base/error.cpp
  bt/base/solve_state.cpp)

target_include_directories(btcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(btcore PUBLIC BT_VERSION_STRING="${PROJECT_VERSION}")
target_compile_features(btcore PUBLIC cxx_std_23)
bt_set_warnings(btcore)

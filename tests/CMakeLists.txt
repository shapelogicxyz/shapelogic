# Unit tests: one Catch2 binary for the whole core. Tag conventions are in
# AGENTS.md ([slow] cases are skipped by `just test`, [parallel] cases are
# what the ThreadSanitizer build runs).
add_executable(bt_tests
  unit/base/test_error.cpp
  unit/base/test_solve_state.cpp
  unit/base/test_stop.cpp
  unit/grid/test_grid_dump.cpp
  unit/grid/test_grid_invariants.cpp
  unit/grid/test_grid_port.cpp
  unit/grid/test_symmetries.cpp
  unit/grid/test_voxel.cpp
  unit/grid/test_voxel_connect.cpp)

target_link_libraries(bt_tests PRIVATE btcore Catch2::Catch2WithMain)
bt_set_warnings(bt_tests)

# Run from the source root so fixtures resolve as tests/data/...
add_test(NAME unit COMMAND bt_tests "~[slow]" WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
add_test(NAME unit-slow COMMAND bt_tests "[slow]" --allow-running-no-tests
         WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
set_tests_properties(unit-slow PROPERTIES LABELS slow)

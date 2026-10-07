# Unit tests: one Catch2 binary for the whole core. Tag conventions are in
# AGENTS.md ([slow] cases are skipped by `just test`, [parallel] cases are
# what the ThreadSanitizer build runs).
add_executable(bt_tests
  unit/base/test_error.cpp
  unit/base/test_solve_state.cpp
  unit/base/test_stop.cpp
  unit/base/test_thread_pool.cpp
  unit/grid/test_grid_dump.cpp
  unit/grid/test_grid_invariants.cpp
  unit/grid/test_grid_port.cpp
  unit/grid/test_symmetries.cpp
  unit/grid/test_voxel.cpp
  unit/grid/test_voxel_connect.cpp
  unit/io/test_puzzle_file.cpp
  unit/io/test_roundtrip.cpp
  unit/io/test_xml.cpp
  unit/model/test_model_dump.cpp
  unit/model/test_puzzle_model.cpp
  unit/solve/test_assembler.cpp
  unit/solve/test_assembler_1.cpp
  unit/solve/test_assembler_bt2.cpp
  unit/solve/test_disassembly.cpp
  unit/solve/test_grouping.cpp
  unit/solve/test_positions.cpp
  unit/solve/test_rotation_solvers.cpp
  unit/solve/test_simd_exact_cover.cpp
  unit/solve/test_solve_session.cpp
  unit/solve/test_work_stealing.cpp)

target_link_libraries(bt_tests PRIVATE btcore Catch2::Catch2WithMain)
bt_set_warnings(bt_tests)

# Run from the source root so fixtures resolve as tests/data/...
add_test(NAME unit COMMAND bt_tests "~[slow]" WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
add_test(NAME unit-slow COMMAND bt_tests "[slow]" --allow-running-no-tests
         WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
set_tests_properties(unit-slow PROPERTIES LABELS slow)

# bt_roundtrip: load and save one puzzle file, for `oracle.py roundtrip`.
add_executable(bt_roundtrip tools/roundtrip.cpp)
target_link_libraries(bt_roundtrip PRIVATE btcore)
bt_set_warnings(bt_roundtrip)

# bt_resume: pause and continue assembler searches, for `oracle.py resume-check`.
add_executable(bt_resume tools/resume.cpp)
target_link_libraries(bt_resume PRIVATE btcore)
bt_set_warnings(bt_resume)

# bt_solve_file: solve a problem and write the solved file, as burrTxt2 does,
# and bt_solutions: what a file holds of solving, for `oracle.py compare-files`.
add_executable(bt_solve_file tools/solve_file.cpp)
target_link_libraries(bt_solve_file PRIVATE btcore)
bt_set_warnings(bt_solve_file)

add_executable(bt_solutions tools/solutions.cpp)
target_link_libraries(bt_solutions PRIVATE btcore)
bt_set_warnings(bt_solutions)

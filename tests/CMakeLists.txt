# Unit tests: one Catch2 binary for the whole core. Tag conventions are in
# AGENTS.md ([slow] cases are skipped by `just test`, [parallel] cases are
# what the ThreadSanitizer build runs).
add_executable(bt_tests
  unit/app/test_analysis_files.cpp
  unit/app/test_app_dump.cpp
  unit/app/test_document.cpp
  unit/app/test_document_solving.cpp
  unit/app/test_layer_geometry.cpp
  unit/app/test_playback_explorers.cpp
  unit/app/test_tools_queries.cpp
  unit/base/test_error.cpp
  unit/engine/test_engine.cpp
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
  unit/mesh/test_meshes.cpp
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

target_link_libraries(bt_tests PRIVATE btengine Catch2::Catch2WithMain)
bt_set_warnings(bt_tests)

# Run from the source root so fixtures resolve as tests/data/...
add_test(NAME unit COMMAND bt_tests "~[slow]" WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
add_test(NAME unit-slow COMMAND bt_tests "[slow]" --allow-running-no-tests
         WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
set_tests_properties(unit-slow PROPERTIES LABELS slow)

# The engine's contract (decision 0012): the schema checks itself, docs/API.md
# is what api/gen_api_md.py makes of it, and bt-engine, driven as a
# subprocess, answers as the schema says.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME api-schema COMMAND Python3::Interpreter api/apischema.py
         WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
add_test(NAME api-docs COMMAND Python3::Interpreter api/gen_api_md.py --check
         WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
add_test(NAME contract COMMAND Python3::Interpreter tests/contract/test_engine.py
         --engine $<TARGET_FILE:bt-engine> WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

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

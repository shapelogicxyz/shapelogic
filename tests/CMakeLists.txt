# Unit tests: one Catch2 binary for the whole core. Tag conventions are in
# AGENTS.md ([slow] cases are skipped by `just test`, [parallel] cases are
# what the ThreadSanitizer build runs).
add_executable(bt_tests
  unit/app/test_analysis_files.cpp
  unit/app/test_app_dump.cpp
  unit/app/test_document.cpp
  unit/app/test_document_sliding.cpp
  unit/app/test_document_stacking.cpp
  unit/app/test_document_solving.cpp
  unit/app/test_layer_geometry.cpp
  unit/app/test_playback_explorers.cpp
  unit/app/test_stl_export.cpp
  unit/app/test_tools_queries.cpp
  unit/base/test_error.cpp
  unit/engine/test_engine.cpp
  unit/base/test_solve_state.cpp
  unit/base/test_stop.cpp
  unit/base/test_system.cpp
  unit/base/test_thread_pool.cpp
  unit/grid/test_grid_dump.cpp
  unit/grid/test_grid_invariants.cpp
  unit/grid/test_grid_port.cpp
  unit/grid/test_symmetries.cpp
  unit/grid/test_voxel.cpp
  unit/grid/test_voxel_connect.cpp
  unit/io/test_imports.cpp
  unit/io/test_puzzle_file.cpp
  unit/io/test_roundtrip.cpp
  unit/io/test_xml.cpp
  unit/mesh/test_meshes.cpp
  unit/mesh/test_stl.cpp
  unit/mesh/test_stl_dump.cpp
  unit/model/test_model_dump.cpp
  unit/model/test_puzzle_model.cpp
  unit/solve/test_assembler.cpp
  unit/solve/test_assembler_1.cpp
  unit/solve/test_disassembly.cpp
  unit/solve/test_grouping.cpp
  unit/solve/test_positions.cpp
  unit/solve/test_rotation_solvers.cpp
  unit/solve/test_simd_exact_cover.cpp
  unit/solve/test_slide_routes.cpp
  unit/solve/test_sliding.cpp
  unit/solve/test_sliding_session.cpp
  unit/solve/test_solve_session.cpp
  unit/solve/test_stacking.cpp
  unit/solve/test_stacking_session.cpp)

target_link_libraries(bt_tests PRIVATE btengine Catch2::Catch2WithMain manifold::manifold)
bt_set_warnings(bt_tests)

# Run from the source root so fixtures resolve as tests/data/...
add_test(NAME unit COMMAND bt_tests "~[slow]" WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
add_test(NAME unit-slow COMMAND bt_tests "[slow]" --allow-running-no-tests
         WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
set_tests_properties(unit-slow PROPERTIES LABELS slow)

# The engine's contract (decision 0012): the schema checks itself, docs/API.md
# is what api/gen_api_md.py makes of it, and shapelogic_engine, driven as a
# subprocess, answers as the schema says.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME api-schema COMMAND Python3::Interpreter api/apischema.py
         WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
add_test(NAME api-docs COMMAND Python3::Interpreter api/gen_api_md.py --check
         WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
add_test(NAME contract COMMAND Python3::Interpreter tests/contract/test_engine.py
         --engine $<TARGET_FILE:shapelogic_engine> WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

# The shapelogic_cli command line: burrTxt's option rules, the refusals, --save, -a, -x.
add_test(NAME cli COMMAND Python3::Interpreter tests/cli/test_cli.py
         --bt $<TARGET_FILE:shapelogic_cli> WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

# The Python module (deferred part 7), when it is built: the original's
# tests and the new ones in python/tests, on this build's module.
if(TARGET shapelogic)
  add_test(NAME python COMMAND Python::Interpreter -m unittest discover -s python/tests -v
           WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
  set_tests_properties(python PROPERTIES
    ENVIRONMENT "PYTHONPATH=$<TARGET_FILE_DIR:shapelogic>" LABELS python TIMEOUT 600)
endif()

# bt_roundtrip: load and save one puzzle file, for `oracle.py roundtrip`.
add_executable(bt_roundtrip tools/roundtrip.cpp)
target_link_libraries(bt_roundtrip PRIVATE btcore)
bt_set_warnings(bt_roundtrip)

# bt_resume: pause and continue assembler searches, for `oracle.py resume-check`.
add_executable(bt_resume tools/resume.cpp)
target_link_libraries(bt_resume PRIVATE btcore)
bt_set_warnings(bt_resume)

# bt_panex_resume: stop and carry on Panex searches, for `oracle.py panex-resume`.
add_executable(bt_panex_resume tools/panex_resume.cpp)
target_link_libraries(bt_panex_resume PRIVATE btcore)
bt_set_warnings(bt_panex_resume)

# bt_blockpack: pack and unpack files, for `oracle.py blockpack-check`.
add_executable(bt_blockpack tools/blockpack.cpp)
target_link_libraries(bt_blockpack PRIVATE btcore)
bt_set_warnings(bt_blockpack)

# bt_imports: the imports, the Puzzlecad export and grid conversion, for
# `oracle.py imports`.
add_executable(bt_imports tools/imports.cpp)
target_link_libraries(bt_imports PRIVATE btcore)
bt_set_warnings(bt_imports)

# bt_solutions: what a file holds of solving, for `oracle.py compare-files`
# (which solves with `shapelogic_cli --save`, burrTxt2's mode).
add_executable(bt_solutions tools/solutions.cpp)
target_link_libraries(bt_solutions PRIVATE btcore)
bt_set_warnings(bt_solutions)

# Unit tests: one Catch2 binary for the whole core. Tag conventions are in
# AGENTS.md ([slow] cases are skipped by `just test`, [parallel] cases are
# what the ThreadSanitizer build runs).
# Every .cpp under unit/ is a test file (the helpers are headers), so the
# list is a glob: a new test file cannot be left out (cleanup item 8.6).
file(GLOB_RECURSE sl_test_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cpp)
add_executable(sl_tests ${sl_test_sources})

# manifold for its headers only (test_stl.cpp, test_stl_dump.cpp): the
# library itself comes with sl_core, and named again here the linker warns
# of a duplicate.
target_link_libraries(sl_tests PRIVATE sl_engine Catch2::Catch2WithMain
                      $<COMPILE_ONLY:manifold::manifold>)
sl_set_warnings(sl_tests)

# Run from the source root so fixtures resolve as tests/data/...
# The quick cases are split into shards that ctest runs side by side
# (`ctest -j`). A shard is a run of the case list, so the list is shuffled
# first, the same way in every shard (a fixed seed; no test uses Catch2's
# random numbers), which spreads the slow cases of one file over the
# shards. More shards than cores: a shard that drew long cases holds one
# core while the others take the rest.
set(SL_UNIT_SHARDS 24 CACHE STRING "How many shards the quick unit tests are split into")

# Every test has a time limit, so a deadlock fails its test instead of
# stalling the run for good. Each limit is at least three times the longest
# time of its kind (the Debug build side by side, on 2026-11-25:
# build/dev/Testing/Temporary/CTestCostData.txt), at least 120 s; the
# sanitizer presets multiply them by SL_TEST_TIMEOUT_SCALE (6).
set(SL_TEST_TIMEOUT_SCALE 1 CACHE STRING "Multiplies every test's time limit (the sanitizer presets: 6)")
function(sl_test_timeout seconds)
  math(EXPR scaled "${seconds} * ${SL_TEST_TIMEOUT_SCALE}")
  set_tests_properties(${ARGN} PROPERTIES TIMEOUT ${scaled})
endfunction()

# --durations yes: each case's time as it ends, so the log of a shard that
# ran out of time shows the last case that finished.
math(EXPR last_shard "${SL_UNIT_SHARDS} - 1")
foreach(i RANGE ${last_shard})
  add_test(NAME unit-${i} COMMAND sl_tests "~[slow]" --order rand --rng-seed 1
           --shard-count ${SL_UNIT_SHARDS} --shard-index ${i} --durations yes
           WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
  # seen: the quick shards of 8 up to 140 s
  sl_test_timeout(600 unit-${i})
endforeach()

# Each [slow] case is a ctest test of its own ("slow: <case>", label slow):
# a few of them take minutes, and in shards the shard that drew them set
# the length of the whole run. ctest's cost data starts the longest first.
# Listed when ctest runs (PRE_TEST), so a build needs no run of sl_tests.
if(catch2_SOURCE_DIR)
  list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
else()
  # an installed Catch2 (FIND_PACKAGE_ARGS) has Catch.cmake beside its config
  list(APPEND CMAKE_MODULE_PATH "${Catch2_DIR}")
endif()
include(Catch)
# seen: a case up to 235 s beside the others (120 s alone)
math(EXPR slow_timeout "1200 * ${SL_TEST_TIMEOUT_SCALE}")
catch_discover_tests(sl_tests TEST_SPEC "[slow]" TEST_PREFIX "slow: "
                     WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} DISCOVERY_MODE PRE_TEST
                     PROPERTIES LABELS slow TIMEOUT ${slow_timeout})

# The ThreadSanitizer build: each [parallel] case a test of its own as well
# ("parallel: <case>", label parallel), so `just test-tsan` runs them side
# by side (`ctest -L parallel`) instead of in one process.
if(SL_SANITIZE MATCHES "thread")
  # seen: a case up to 246 s beside the others (the tsan preset's scale is 6)
  math(EXPR parallel_timeout "300 * ${SL_TEST_TIMEOUT_SCALE}")
  catch_discover_tests(sl_tests TEST_SPEC "[parallel]" TEST_PREFIX "parallel: "
                       WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} DISCOVERY_MODE PRE_TEST
                       PROPERTIES LABELS parallel TIMEOUT ${parallel_timeout})
endif()

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
# docs/ARCHITECTURE.md's map: a line for every source file, every line a
# file that exists, the prose capped (cleanup item 3.2)
add_test(NAME architecture-map COMMAND Python3::Interpreter tests/docs/test_architecture_map.py
         WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
# recorded: the schema and docs checks under a second; the contract suite
# 4 s, but 41 s beside the unit shards
sl_test_timeout(120 api-schema api-docs architecture-map)
sl_test_timeout(300 contract)

# The shapelogic_cli command line: burrTxt's option rules, the refusals, --save, -a, -x.
add_test(NAME cli COMMAND Python3::Interpreter tests/cli/test_cli.py
         --cli $<TARGET_FILE:shapelogic_cli> WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
sl_test_timeout(120 cli)

# The regression suite's fast tier (tests/regression): this build's CLI
# against the recorded answers. The other tiers and the text runs are the
# gate's (`just gate regression`).
add_test(NAME regression-fast COMMAND Python3::Interpreter tests/regression/regress.py
         --cli $<TARGET_FILE:shapelogic_cli> summary --tier fast WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
sl_test_timeout(300 regression-fast)

# The Python module (deferred part 7), when it is built: the original's
# tests and the new ones in python/tests, on this build's module.
if(TARGET shapelogic)
  add_test(NAME python COMMAND Python::Interpreter -m unittest discover -s python/tests -v
           WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
  set_tests_properties(python PROPERTIES
    ENVIRONMENT "PYTHONPATH=$<TARGET_FILE_DIR:shapelogic>" LABELS python)
  sl_test_timeout(600 python)
endif()

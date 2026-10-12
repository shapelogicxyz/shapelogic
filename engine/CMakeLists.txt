# btengine: the engine's protocol and methods (decision 0012), a library so
# that the unit tests drive it in process; bt-engine: the program.
add_library(btengine STATIC
  convert.cpp
  engine.cpp
  events.cpp
  methods_colour.cpp
  methods_document.cpp
  methods_engine.cpp
  methods_explore.cpp
  methods_export.cpp
  methods_playback.cpp
  methods_problem.cpp
  methods_shape.cpp
  methods_sliding.cpp
  methods_stacking.cpp
  methods_solution.cpp
  methods_solve.cpp
  output.cpp
  rpc.cpp)
target_include_directories(btengine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(btengine PUBLIC btcore nlohmann_json::nlohmann_json)
bt_set_warnings(btengine)

add_executable(bt-engine main.cpp)
target_link_libraries(bt-engine PRIVATE btengine)
bt_set_warnings(bt-engine)

# The packaged app carries bt-engine alone (web/tools/dist.ts): on Windows
# the MinGW runtime (libstdc++, libgcc, winpthread) and zlib are linked in,
# so no DLL has to sit beside it; on Linux libstdc++ and libgcc are, since
# a C++23 libstdc++ is newer than many systems have.
if(MINGW)
  target_link_options(bt-engine PRIVATE -static)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_link_options(bt-engine PRIVATE -static-libstdc++ -static-libgcc)
endif()

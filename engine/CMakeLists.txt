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
  methods_playback.cpp
  methods_problem.cpp
  methods_shape.cpp
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

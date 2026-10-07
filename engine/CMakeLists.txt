add_executable(bt-engine main.cpp)
target_link_libraries(bt-engine PRIVATE btcore)
bt_set_warnings(bt-engine)

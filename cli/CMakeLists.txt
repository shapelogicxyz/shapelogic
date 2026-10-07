add_executable(bt main.cpp)
target_link_libraries(bt PRIVATE btcore)
bt_set_warnings(bt)

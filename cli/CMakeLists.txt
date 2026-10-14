add_executable(shapelogic_cli main.cpp options.cpp text_print.cpp)
target_link_libraries(shapelogic_cli PRIVATE btcore)
bt_set_warnings(shapelogic_cli)

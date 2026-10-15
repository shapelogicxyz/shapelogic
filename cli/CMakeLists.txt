add_executable(shapelogic_cli main.cpp options.cpp text_print.cpp)
target_link_libraries(shapelogic_cli PRIVATE btcore)
bt_set_warnings(shapelogic_cli)

# Run from the packaged app and from a plain shell: no MinGW runtime DLLs
# beside it (the same as shapelogic_engine, engine/CMakeLists.txt).
if(MINGW)
  target_link_options(shapelogic_cli PRIVATE -static)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_link_options(shapelogic_cli PRIVATE -static-libstdc++ -static-libgcc)
endif()

# The `burrtools` Python module (deferred part 7): the original's
# src/python bindings over the new core. Built when BT_PYTHON_MODULE is on
# (cmake/Python.cmake); its tests are tests/CMakeLists.txt's `python`.
pybind11_add_module(burrtools
  src/module.cpp
  src/py_problem.cpp
  src/py_puzzle.cpp
  src/py_shape.cpp
  src/solution_iterator.cpp)
target_link_libraries(burrtools PRIVATE btcore)
bt_set_warnings(burrtools)

# a wheel (pyproject.toml, scikit-build-core) installs the module at its top
if(SKBUILD)
  install(TARGETS burrtools LIBRARY DESTINATION . COMPONENT python)
endif()


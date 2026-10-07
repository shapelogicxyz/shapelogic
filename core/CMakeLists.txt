# libbtcore: the puzzle model, file format, solvers and application layer.
# No UI, no stdout/stderr output, no global state: everything reaches it
# through its API (docs/ARCHITECTURE.md).
add_library(btcore STATIC
  bt/base/assert.cpp
  bt/base/error.cpp
  bt/base/solve_state.cpp
  bt/grid/grid_type.cpp
  bt/grid/symmetries_0.cpp
  bt/grid/symmetries_1.cpp
  bt/grid/symmetries_2.cpp
  bt/grid/voxel.cpp
  bt/grid/voxel_bricks.cpp
  bt/grid/voxel_prisms.cpp
  bt/grid/voxel_rhombic.cpp
  bt/grid/voxel_spheres.cpp
  bt/grid/voxel_table.cpp
  bt/grid/voxel_tetra_octa.cpp)

target_include_directories(btcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(btcore PUBLIC BT_VERSION_STRING="${PROJECT_VERSION}")
target_compile_features(btcore PUBLIC cxx_std_23)
bt_set_warnings(btcore)

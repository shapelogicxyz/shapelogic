# libbtcore: the puzzle model, file format, solvers and application layer.
# No UI, no stdout/stderr output, no global state: everything reaches it
# through its API (docs/ARCHITECTURE.md).
add_library(btcore STATIC
  shapelogic/app/analysis.cpp
  shapelogic/app/document.cpp
  shapelogic/app/document_commands.cpp
  shapelogic/app/document_sliding.cpp
  shapelogic/app/document_stacking.cpp
  shapelogic/app/explorers.cpp
  shapelogic/app/layer_geometry.cpp
  shapelogic/app/playback.cpp
  shapelogic/app/queries.cpp
  shapelogic/app/shape_tools.cpp
  shapelogic/base/assert.cpp
  shapelogic/base/error.cpp
  shapelogic/base/solve_state.cpp
  shapelogic/base/system.cpp
  shapelogic/base/thread_pool.cpp
  shapelogic/grid/grid_type.cpp
  shapelogic/grid/symmetries_0.cpp
  shapelogic/grid/symmetries_1.cpp
  shapelogic/grid/symmetries_2.cpp
  shapelogic/grid/voxel.cpp
  shapelogic/grid/voxel_bricks.cpp
  shapelogic/grid/voxel_prisms.cpp
  shapelogic/grid/voxel_rhombic.cpp
  shapelogic/grid/voxel_spheres.cpp
  shapelogic/grid/voxel_table.cpp
  shapelogic/grid/voxel_tetra_octa.cpp
  shapelogic/io/blockpack.cpp
  shapelogic/io/gzip_file.cpp
  shapelogic/io/paused_solve.cpp
  shapelogic/io/ps3d_file.cpp
  shapelogic/io/puzzle_file.cpp
  shapelogic/io/scad_file.cpp
  shapelogic/io/xml_document.cpp
  shapelogic/io/xml_pull_parser.cpp
  shapelogic/io/xml_writer.cpp
  shapelogic/mesh/cube_mesh.cpp
  shapelogic/mesh/draw_mesh.cpp
  shapelogic/mesh/grid_geometry.cpp
  shapelogic/mesh/grid_geometry_0.cpp
  shapelogic/mesh/grid_geometry_1.cpp
  shapelogic/mesh/grid_geometry_3.cpp
  shapelogic/mesh/grid_geometry_4.cpp
  shapelogic/mesh/mink_mesh.cpp
  shapelogic/mesh/modifiers.cpp
  shapelogic/mesh/polyhedron.cpp
  shapelogic/mesh/sphere_mesh.cpp
  shapelogic/mesh/stl.cpp
  shapelogic/mesh/triangulate.cpp
  shapelogic/mesh/vec3.cpp
  shapelogic/mesh/voxel_mesh.cpp
  shapelogic/model/assembly.cpp
  shapelogic/model/disassembly.cpp
  shapelogic/model/grid_convert.cpp
  shapelogic/model/problem.cpp
  shapelogic/model/puzzle.cpp
  shapelogic/model/sliding.cpp
  shapelogic/model/solution.cpp
  shapelogic/model/stacking.cpp
  shapelogic/solve/assembler.cpp
  shapelogic/solve/assembler_0.cpp
  shapelogic/solve/assembler_1.cpp
  shapelogic/solve/disasm_to_moves.cpp
  shapelogic/solve/disassembler.cpp
  shapelogic/solve/disassembler_node.cpp
  shapelogic/solve/dlx_matrix.cpp
  shapelogic/solve/grouping.cpp
  shapelogic/solve/movement_analysator.cpp
  shapelogic/solve/movement_cache.cpp
  shapelogic/solve/movement_cache_bricks.cpp
  shapelogic/solve/movement_cache_prisms.cpp
  shapelogic/solve/node_hash.cpp
  shapelogic/solve/panex_search.cpp
  shapelogic/solve/placement_finder.cpp
  shapelogic/solve/rotation_moves_classic.cpp
  shapelogic/solve/rotation_moves_crowell.cpp
  shapelogic/solve/rotation_rules.cpp
  shapelogic/solve/search_run.cpp
  shapelogic/solve/simd_exact_cover.cpp
  shapelogic/solve/simd_huang_cover.cpp
  shapelogic/solve/sliding_search.cpp
  shapelogic/solve/solve_progress.cpp
  shapelogic/solve/solve_session.cpp
  shapelogic/solve/solver_type.cpp
  shapelogic/solve/stack_search.cpp)

target_include_directories(btcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(btcore PUBLIC BT_VERSION_STRING="${PROJECT_VERSION}")
target_compile_features(btcore PUBLIC cxx_std_23)
target_link_libraries(btcore PRIVATE ZLIB::ZLIB manifold::manifold)
bt_set_warnings(btcore)

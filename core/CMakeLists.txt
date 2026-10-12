# libbtcore: the puzzle model, file format, solvers and application layer.
# No UI, no stdout/stderr output, no global state: everything reaches it
# through its API (docs/ARCHITECTURE.md).
add_library(btcore STATIC
  bt/app/analysis.cpp
  bt/app/document.cpp
  bt/app/document_commands.cpp
  bt/app/document_sliding.cpp
  bt/app/document_stacking.cpp
  bt/app/explorers.cpp
  bt/app/layer_geometry.cpp
  bt/app/playback.cpp
  bt/app/queries.cpp
  bt/app/shape_tools.cpp
  bt/base/assert.cpp
  bt/base/error.cpp
  bt/base/solve_state.cpp
  bt/base/system.cpp
  bt/base/thread_pool.cpp
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
  bt/grid/voxel_tetra_octa.cpp
  bt/io/blockpack.cpp
  bt/io/gzip_file.cpp
  bt/io/paused_solve.cpp
  bt/io/puzzle_file.cpp
  bt/io/xml_document.cpp
  bt/io/xml_pull_parser.cpp
  bt/io/xml_writer.cpp
  bt/mesh/cube_mesh.cpp
  bt/mesh/draw_mesh.cpp
  bt/mesh/grid_geometry.cpp
  bt/mesh/grid_geometry_0.cpp
  bt/mesh/grid_geometry_1.cpp
  bt/mesh/grid_geometry_3.cpp
  bt/mesh/grid_geometry_4.cpp
  bt/mesh/mink_mesh.cpp
  bt/mesh/modifiers.cpp
  bt/mesh/polyhedron.cpp
  bt/mesh/sphere_mesh.cpp
  bt/mesh/stl.cpp
  bt/mesh/triangulate.cpp
  bt/mesh/vec3.cpp
  bt/mesh/voxel_mesh.cpp
  bt/model/assembly.cpp
  bt/model/disassembly.cpp
  bt/model/problem.cpp
  bt/model/puzzle.cpp
  bt/model/sliding.cpp
  bt/model/solution.cpp
  bt/model/stacking.cpp
  bt/solve/assembler.cpp
  bt/solve/assembler_0.cpp
  bt/solve/assembler_1.cpp
  bt/solve/assembler_bt2.cpp
  bt/solve/dancing_cells.cpp
  bt/solve/disasm_to_moves.cpp
  bt/solve/disassembler.cpp
  bt/solve/disassembler_node.cpp
  bt/solve/dlx_matrix.cpp
  bt/solve/grouping.cpp
  bt/solve/movement_analysator.cpp
  bt/solve/movement_cache.cpp
  bt/solve/movement_cache_bricks.cpp
  bt/solve/movement_cache_prisms.cpp
  bt/solve/node_hash.cpp
  bt/solve/panex_search.cpp
  bt/solve/placement_finder.cpp
  bt/solve/rotation_moves_classic.cpp
  bt/solve/rotation_moves_crowell.cpp
  bt/solve/rotation_rules.cpp
  bt/solve/search_run.cpp
  bt/solve/simd_exact_cover.cpp
  bt/solve/simd_huang_cover.cpp
  bt/solve/sliding_search.cpp
  bt/solve/solve_progress.cpp
  bt/solve/solve_session.cpp
  bt/solve/solver_type.cpp
  bt/solve/stack_search.cpp)

target_include_directories(btcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(btcore PUBLIC BT_VERSION_STRING="${PROJECT_VERSION}")
target_compile_features(btcore PUBLIC cxx_std_23)
target_link_libraries(btcore PRIVATE ZLIB::ZLIB manifold::manifold)
bt_set_warnings(btcore)

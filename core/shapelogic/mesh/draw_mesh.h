// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// A mesh as plain arrays, ready to be sent to a front end as a binary
/// frame and drawn: triangles with flat normals and, per triangle, the
/// cell and side it belongs to, its flags and colour.
/// Ported from the face loop of voxelFrame_c::drawShape
/// (src/gui/voxelframe.cpp: the triangle fan of each face, its normal,
/// the edge lines of the "edges" style) and buildMesh of
/// src/lib/gltfexport.cpp (vertices shared by triangles with the same
/// normal).

#include "shapelogic/mesh/polyhedron.h"
#include "shapelogic/mesh/voxel_mesh.h"

#include <cstdint>
#include <vector>

namespace bt {
class Voxel;
}

namespace bt::mesh {

/// Triangles, flat shaded: a vertex is shared only by triangles with the
/// same normal. Coordinates are mesh units (grid_geometry.h).
struct DrawMesh {
  std::vector<float> positions;   ///< x, y, z per vertex
  std::vector<float> normals;     ///< x, y, z per vertex
  std::vector<std::uint32_t> indices;  ///< three per triangle
  /// Per triangle: the cell (voxel index) and its side (neighbour number;
  /// -1 for bevel, gap and filling faces), what a click on it picks.
  std::vector<std::uint32_t> voxel;
  std::vector<std::int32_t> voxelFace;
  /// Per triangle: FaceFlag bits (the checker colour, the variable mark,
  /// the wire frame, inside faces) and the constraint colour (0: none).
  std::vector<std::uint32_t> flags;
  std::vector<std::uint8_t> colour;
  /// Per triangle: the polygon it is part of (numbered from 0), so the
  /// variable mark can be drawn on the whole polygon.
  std::vector<std::uint32_t> polygon;
  /// Line segments (two points, x, y, z each) where two faces meet at an
  /// angle: the edge lines of the "edges" style. Only for closed meshes.
  std::vector<float> edgeLines;
  /// The bounding box of the positions (all 0 when empty).
  float lo[3] = {0, 0, 0};
  float hi[3] = {0, 0, 0};
  /// The hotspot of the voxel space the mesh was made from, in grid
  /// coordinates (Voxel::getHx...): an assembly puts a piece so that the
  /// hotspot of its orientation lands on the placement's cell, so the mesh
  /// is drawn moved by the cell minus the hotspot (in mesh units). Zero for
  /// a mesh not made from a space.
  int hotspot[3] = {0, 0, 0};

  [[nodiscard]] std::size_t triangleCount() const { return indices.size() / 3; }
  [[nodiscard]] std::size_t vertexCount() const { return positions.size() / 3; }
};

/// The triangles of a polyhedron's faces (holes left out), as the view
/// drew them; with `edgeLines` the lines between faces at an angle.
[[nodiscard]] DrawMesh toDrawMesh(const Polyhedron& poly, bool edgeLines);

/// The mesh of a shape in a style (shapeMesh) as a DrawMesh, with the
/// shape's hotspot; the flat style has its edge lines.
[[nodiscard]] DrawMesh shapeDrawMesh(const Voxel& v, MeshStyle style);

}  // namespace bt::mesh

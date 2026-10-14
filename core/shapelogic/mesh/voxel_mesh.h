// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The meshes of a shape, as the original's 3D view drew them.
/// Ported from voxel_c::getMeshInternal, getDrawingMesh, getWireframeMesh
/// and getFlatMesh (src/lib/voxel.cpp) and from the sphere meshes of
/// src/lib/voxel_2_mesh.cpp, and getSTLMesh (voxel.cpp, voxel_0.cpp,
/// voxel_2_mesh.cpp: the view's "Draw pieces like STL export").

#include "shapelogic/mesh/polyhedron.h"

namespace bt {
class Voxel;
}

namespace bt::mesh {

/// Which mesh of a shape.
enum class MeshStyle : unsigned char {
  /// The view's normal one: cells with bevelled edges and a small gap
  /// between neighbours, only the cells on the surface (getDrawingMesh).
  /// Not closed: the faces are drawn as they are.
  Drawing,
  /// The same, closed, with the gap and bevel strips merged into the cell
  /// faces where they lie flat; the faces flagged kFaceWireframe are the
  /// wire frame (getWireframeMesh). Spheres: the drawing mesh.
  Wireframe,
  /// Plain cells without bevel or gap, closed (getFlatMesh): the view's
  /// "edges" style.
  Flat,
  /// The STL export's mesh at the view's own parameters (getSTLMesh: gap
  /// 0.02, bevel 0.05 with interior chamfers on the cube, prism, rhombic
  /// and tetra-octa grids; the sphere grid's balls at recursion 2), closed:
  /// the view's "Draw pieces like STL export". Where the mesher fails,
  /// the flat mesh.
  Stl,
};

/// The mesh of a shape in its own coordinates (mesh units, see
/// grid_geometry.h). Each face carries its cell and side (what a click
/// picks), its colour and its FaceFlag bits.
[[nodiscard]] Polyhedron shapeMesh(const Voxel& v, MeshStyle style);

/// The mesh of cells with bevel and gap (getMeshInternal): with `fast`
/// only the cells with an empty neighbour, no gap faces, not closed.
[[nodiscard]] Polyhedron cellMesh(const Voxel& v, double bevel, double offset, bool fast);

/// The mesh of a sphere shape (voxel_2_c::getMeshInternal): balls of
/// radius `sphereRad` joined by round connections, `recursion` levels of
/// subdivision; hollow with `innerRad` > 0, with holes of `holeDiam`
/// (negative: square). With `fast` the mesh is not closed.
[[nodiscard]] Polyhedron sphereMesh(const Voxel& v, float sphereRad, float connectionRad,
                                    float round, float offset, int recursion, float innerRad,
                                    float holeDiam, bool fast);

}  // namespace bt::mesh

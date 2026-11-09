// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The uniform mesher: bevels of one width on every edge of every grid,
/// a gap between neighbouring cells, and the "bumpy" look, from the
/// cells' face polygons alone. The bevel and gap directions come from the
/// planes that meet at each corner, the cells around an edge are found by
/// walking from face to face, and the corners are closed by chaining the
/// loose edges of the bevel strips into polygons.
/// After the mesher of BurrTools V2 (createMeshShape of
/// src/lib/polygen.cpp, calculateBevelOffsetVertices of src/lib/grid.cpp),
/// written anew on this core's cell geometry: V2 reads a grid's polyhedra
/// and edge loops from its registration; here they are found per cell from
/// connectionFace and getNeighbor (grid_geometry.h, voxel.h).

#include "shapelogic/base/error.h"
#include "shapelogic/mesh/polyhedron.h"

namespace bt {
class Voxel;
}

namespace bt::mesh {

/// What the uniform mesher makes.
struct UniformMeshOptions {
  /// How deep the bevel cuts into each face at a right-angle edge, in
  /// cell units (as the ported meshers' bevel: the chamfer there is
  /// bevel * sqrt 2 wide), and a chamfer of that same width at every
  /// other edge whatever its angle. 0: no bevel.
  double bevel = 0;
  /// How far every face is moved into its cell, in cell units: the gap
  /// between neighbouring cells is twice this. 0: no gap.
  double offset = 0;
  /// Bevel only the edges where the solid is convex (the STL export's
  /// "Interior chamfers" off); otherwise every edge, concave ones too.
  bool convexBevelOnly = false;
  /// Each cell a rounded body of its own, joined to its filled neighbours
  /// by a neck (bevels on every edge of every cell, also inside the
  /// solid).
  bool bumpy = false;
  /// The cells stretched along the axes before the bevels are worked out
  /// (the STL export's unit sizes), so the bevels keep their width.
  float stretch[3] = {1, 1, 1};
};

/// The mesh of a shape by the uniform mesher: closed (finalized), each
/// face carrying its cell, its side (-1 for bevel, gap and corner faces),
/// its colour and its FaceFlag bits (the checker, the variable mark on
/// cell faces, kFaceBevel on bent strips and corners, kFaceOffset on flat
/// ones; both kFaceWireframe). Refused (Unsupported) on a grid whose cells
/// have no face polygons, with an InternalError when the cell geometry
/// does not meet the mesher's assumptions (convex cells whose shared
/// faces coincide).
[[nodiscard]] Result<Polyhedron> uniformMesh(const Voxel& v, const UniformMeshOptions& options);

}  // namespace bt::mesh

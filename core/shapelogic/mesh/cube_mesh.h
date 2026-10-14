// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The STL mesh of a shape on the cube grids (bricks, sliding, stacking):
/// a chamfered surface with a gap between neighbouring pieces, built from
/// a table of the surface inside the dual cell around each cell corner.
/// Ported from src/lib/cubemesh.{h,cpp} (cubeMesh::generate, with the
/// tables cubetable.h and cubetable_nofill.h copied unchanged into
/// tabs_cube/) and src/lib/cubepoly.{h,cpp} (cubePolyhedron).

#include "shapelogic/base/error.h"
#include "shapelogic/mesh/polyhedron.h"

#include <vector>

namespace bt {
class Voxel;
}

namespace bt::mesh {

/// A point of the cube mesher, in double precision.
struct DVec3 {
  double x = 0;
  double y = 0;
  double z = 0;
};

/// The cube mesher's output: welded vertices and outward-wound triangles
/// (index triples into `verts`), in cell units.
struct CubeSurface {
  std::vector<DVec3> verts;
  std::vector<int> tris;
};

/// The chamfered surface of nx*ny*nz cells, `filled[x + nx*(y + ny*z)]`
/// non-zero for a filled cell (cell (x,y,z) occupies [x,x+1]x[y,y+1]x
/// [z,z+1]). `g` is the gap per body (bodies end up 2g apart), `r` the
/// bevel's leg; `fills` with the interior (concave) chamfers, or without.
/// Refused (InvalidArgument, with the original's message) when g + r is
/// beyond what the table covers or the construction fails.
[[nodiscard]] Result<CubeSurface> cubeSurface(int nx, int ny, int nz,
                                              const std::vector<char>& filled, double g, double r,
                                              bool fills);

/// The cube mesher's surface of a shape as a finalized polyhedron in cell
/// units (cubePolyhedron): triangles tagged with the cell they belong to
/// and its side (what a click in the view picks), bevels and fills
/// flagged kFaceBevel, the checker colour on the cell faces.
[[nodiscard]] Result<Polyhedron> cubePolyhedron(const Voxel& v, double g, double r, bool fills);

}  // namespace bt::mesh

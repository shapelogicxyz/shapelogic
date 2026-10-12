// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Ear clipping of a planar polygon with holes, for the cube grid's STL
/// mesher (cube_mesh.cpp).
/// Ported from src/lib/triangulate.h (cubeMesh::triangulateLoops, a
/// template there, used with one point type).

#include <vector>

namespace bt::mesh {

/// A point of a face projected onto its plane.
struct Point2 {
  double x = 0;
  double y = 0;
};

/// Triangulates a polygon with holes. `loops[0]` is the outline (counter
/// clockwise), the others are holes (clockwise), each a list of indices
/// into `pts`. Holes are bridged into the outline (a keyhole per hole,
/// from its rightmost vertex to a visible vertex, Eberly's rule), then the
/// polygon is ear clipped; the keyhole polygon is weakly simple, so every
/// test is by position, with a tolerance of 1e-6 in the points' units.
/// Appends index triples into `pts` to `tris` (cleared first). Returns
/// false with the reason in `why` when a hole is not inside the outline or
/// the clipping gets stuck.
bool triangulateLoops(const std::vector<Point2>& pts, const std::vector<std::vector<int>>& loops,
                      std::vector<int>& tris, const char*& why);

}  // namespace bt::mesh

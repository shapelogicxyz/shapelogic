// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The STL mesh of a shape on the prism, rhombic and tetra-octa grids: the
/// shape shrunk and chamfered by Minkowski sums and differences with small
/// convex bits, through the Manifold library's hulls and booleans, every
/// step put back onto its planes.
/// Ported from src/lib/minkmesh.{h,cpp}.

#include "shapelogic/base/error.h"
#include "shapelogic/mesh/polyhedron.h"

namespace bt {
class Voxel;
}

namespace bt::mesh {

/// Whether the shape's grid is one this mesher handles (prism, rhombic,
/// tetra-octa).
[[nodiscard]] bool minkHandles(const Voxel& v);

/// The chamfered surface of the shape as a finalized polyhedron in mesh
/// units: gap `g`, bevel legs `r` (cell units), with or without the
/// interior chamfers (`fills`). Faces on a facet direction are plain,
/// the others flagged kFaceBevel; each is tagged with the cell holding
/// its centroid and, when it lies on one of that cell's faces, its side.
/// Refused (InvalidArgument, with the original's message) when the
/// construction fails or nothing is left of the shape.
[[nodiscard]] Result<Polyhedron> minkPolyhedron(const Voxel& v, double g, double r, bool fills);

}  // namespace bt::mesh

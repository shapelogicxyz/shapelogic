// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Which cells a cell passes through when it is moved: the sweep test of
/// docs/V2-IDEAS-PLAN.md's idea 4 (decision 0025), on the cells as convex
/// polyhedra (cell_geometry.h). BurrTools V2 answers the same question in
/// its grid registration (src/lib/grid.cpp, calculateMovementChecks): it
/// shrinks the cell a little, sweeps it along the move (a Minkowski sum)
/// and asks a convex-intersection routine about every cell nearby. Here
/// the answer is exact instead: a moved convex cell and another convex
/// cell share inner points exactly when no plane separates them, and the
/// planes to try are few (the separating axis theorem), so cells that
/// only touch the sweep need no shrinking to be told apart.
/// The movement checks of the take-apart are derived from this
/// (solve/movement_checks.h).

#include "shapelogic/base/error.h"
#include "shapelogic/mesh/cell_geometry.h"
#include "shapelogic/mesh/vec3.h"

#include <vector>

namespace bt::mesh {

/// Whether a cell moved in a straight line by `move` passes through
/// `other`: at some point of the way the two share more than a face, an
/// edge or a corner. Cells that touch the swept cell do not count.
[[nodiscard]] bool sweepEnters(const CellPolyhedron& cell, const Vec3& move,
                               const CellPolyhedron& other);

/// The cells the cell `c` passes through when it moves by `step` cell
/// coordinates, as offsets from `c` in coordinate order: the cell it ends
/// in is one of them, `c` itself is not. Candidates are the cells whose
/// coordinates lie within `reach` of the box from `c` to `c + step`; every
/// coordinate asked about must be one the grid's validCoordinate takes
/// (not negative on every grid). InvalidArgument when `c` or `c + step`
/// is no cell, or when the cell at `c + step` is not the cell at `c`
/// moved (the step is no move of the grid onto itself).
[[nodiscard]] Result<std::vector<CellAt>> sweptCells(CellGeometry& g, CellAt c, CellAt step,
                                                     CellAt reach);

}  // namespace bt::mesh

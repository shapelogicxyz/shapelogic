// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The movement checks of a grid, derived from the cells' geometry: the
/// directions pieces move in, and for each direction and each kind of
/// cell the cells that must be empty for the cell to move one step
/// (docs/V2-IDEAS-PLAN.md idea 4, decision 0025). The original made these
/// tables by hand for cubes and triangular prisms (movementcache_0.cpp,
/// movementcache_1.cpp) and has none for the other grids. BurrTools V2
/// declares the movement vectors per grid and computes the cells
/// (src/lib/grid.cpp, calculateMovementChecks); so does this, with the
/// sweep test of mesh/cell_sweep.h. The rhombic, tetra-octa, hexagonal
/// and cairo grids take apart with these tables (MovementCacheDerived);
/// for cubes and triangular prisms the derivation reproduces the ported
/// tables, which is its test.

#include "shapelogic/base/error.h"
#include "shapelogic/grid/grid_type.h"

#include <array>
#include <cstddef>
#include <optional>
#include <vector>

namespace bt {

/// The directions pieces move in on a grid: one step each, a move of the
/// grid onto itself; and the box after which the grid repeats, so that a
/// cell's position within it says what kind of cell it is.
struct MovementDirections {
  std::array<int, 3> period{1, 1, 1};
  std::vector<std::array<int, 3>> directions;
};

/// The directions of a grid kind, or none for a grid without cells that
/// move this way (spheres, and the sliding and stacking grids, which have
/// searches of their own).
[[nodiscard]] std::optional<MovementDirections> movementDirections(GridKind kind);

/// The movement checks of a grid.
struct MovementChecks {
  /// The box after which the grid repeats.
  std::array<int, 3> period{1, 1, 1};
  /// One step in each direction.
  std::vector<std::array<int, 3>> directions;
  /// Per direction and position in the period box (kindOf), the offsets
  /// of the cells that must be empty for a cell there to move one step
  /// that way: every cell it passes through, the one it ends in among
  /// them. Empty for positions that are no cell.
  std::vector<std::vector<std::array<int, 3>>> checks;
  /// The largest offset in `checks` along each axis: a cell this far
  /// outside a shape's box and moving away cannot run into the shape.
  std::array<int, 3> extent{0, 0, 0};

  /// The number of positions in the period box.
  [[nodiscard]] std::size_t kinds() const {
    return static_cast<std::size_t>(period[0]) * static_cast<std::size_t>(period[1]) *
           static_cast<std::size_t>(period[2]);
  }
  /// The position in the period box of a cell (coordinates not negative).
  [[nodiscard]] std::size_t kindOf(unsigned int x, unsigned int y, unsigned int z) const {
    const auto px = static_cast<unsigned int>(period[0]);
    const auto py = static_cast<unsigned int>(period[1]);
    const auto pz = static_cast<unsigned int>(period[2]);
    return (x % px) + std::size_t{px} * ((y % py) + std::size_t{py} * (z % pz));
  }
  /// The checks of a direction for a kind of cell.
  [[nodiscard]] const std::vector<std::array<int, 3>>& at(std::size_t dir,
                                                          std::size_t kind) const {
    return checks[dir * kinds() + kind];
  }
};

/// Derives the checks of a grid for the given directions: each kind of
/// cell is swept one step along each direction and the cells it passes
/// through are found among those within `reachPeriods` period boxes
/// (at least two cells) around the move. Fails when a direction is no
/// move of the grid onto itself or the grid's cells have no polyhedra.
[[nodiscard]] Result<MovementChecks> deriveMovementChecks(GridKind kind,
                                                          const MovementDirections& moves,
                                                          int reachPeriods = 1);

/// The checks of a grid kind for its movementDirections, derived the
/// first time they are asked for and kept (immutable, shared between
/// threads); null for a grid without directions.
[[nodiscard]] const MovementChecks* movementChecks(GridKind kind);

}  // namespace bt

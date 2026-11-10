// SPDX-License-Identifier: GPL-3.0-or-later
// After the movement vectors of BurrTools V2's grids (data/scripts/
// grid_rhombic.lua, grid_octahedral.lua: `movementVectors`) and its
// calculateMovementChecks (src/lib/grid.cpp); the directions of cubes and
// triangular prisms are the original's (movementcache_0.cpp,
// movementcache_1.cpp). New.
#include "shapelogic/solve/movement_checks.h"

#include "shapelogic/base/assert.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/mesh/cell_geometry.h"
#include "shapelogic/mesh/cell_sweep.h"

#include <algorithm>
#include <cstdlib>
#include <memory>

namespace bt {

// Pieces that lie face to face can only slide along the faces they share,
// so the directions worth trying are those of the cells' edges (where two
// face planes meet): the axes for cubes, the triangle's sides for the
// prisms. A step is the shortest move along the direction that puts the
// grid onto itself. The original's and V2's lists are exactly these; the
// hexagonal and cairo grids have none in V2 and get theirs here.
std::optional<MovementDirections> movementDirections(GridKind kind) {
  switch (kind) {
    case GridKind::Bricks:
      return MovementDirections{{1, 1, 1}, {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};

    case GridKind::TriangularPrism:
      // the triangle's kind is the parity of x + y; the original's order
      return MovementDirections{{2, 2, 1}, {{0, 0, 1}, {2, 0, 0}, {1, 1, 0}, {-1, 1, 0}}};

    case GridKind::Rhombic:
      // A cube of 5x5x5 coordinates holds 24 cells and the grid repeats
      // cube by cube. The cells' edges run along the cube's edges, the
      // diagonals of its faces and its space diagonals: V2's 13 vectors.
      // (V2 steps half a space diagonal: its cells are halves of these
      // and repeat that way; these do not.)
      return MovementDirections{{5, 5, 5},
                                {{5, 0, 0},
                                 {0, 5, 0},
                                 {0, 0, 5},
                                 {5, 5, 0},
                                 {5, -5, 0},
                                 {5, 0, 5},
                                 {5, 0, -5},
                                 {0, 5, 5},
                                 {0, 5, -5},
                                 {5, 5, 5},
                                 {5, 5, -5},
                                 {5, -5, 5},
                                 {5, -5, -5}}};

    case GridKind::TetraOcta:
      // A cube of 3x3x3 coordinates holds 5 cells; neighbouring cubes are
      // mirror images, so the grid repeats along the face diagonals of
      // the cubes (the edges of the tetrahedra and octahedra) and every
      // two cubes along the axes (the edges of the octahedra's eighths):
      // V2's 9 vectors, in the same coordinates.
      return MovementDirections{{6, 6, 6},
                                {{6, 0, 0},
                                 {0, 6, 0},
                                 {0, 0, 6},
                                 {3, 3, 0},
                                 {3, -3, 0},
                                 {3, 0, 3},
                                 {3, 0, -3},
                                 {0, 3, 3},
                                 {0, 3, -3}}};

    case GridKind::Hexagonal:
      // Up, across the three sides of the hexagon to the neighbour, and
      // along the three sides to the next cell but one. The move to a
      // neighbour is no edge direction, but it is the shortest there is,
      // and a prism with nothing beside the way can make it.
      return MovementDirections{
          {1, 1, 1},
          {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}, {-1, 1, 0}, {1, 1, 0}, {-1, 2, 0}, {2, -1, 0}}};

    case GridKind::Cairo:
      // Up, and the grid's shortest moves in the plane: the two that span
      // its lattice and the two along the axes. A pentagon's sides run in
      // no direction the grid repeats in, so in the plane a piece moves
      // only where it has room; the checks say where that is.
      return MovementDirections{{4, 4, 1},
                                {{0, 0, 1}, {2, 2, 0}, {2, -2, 0}, {4, 0, 0}, {0, 4, 0}}};

    case GridKind::Spheres:
    case GridKind::Sliding:
    case GridKind::Stacking:
      break;
  }
  return std::nullopt;
}

Result<MovementChecks> deriveMovementChecks(GridKind kind, const MovementDirections& moves,
                                            int reachPeriods) {
  BT_ASSERT(reachPeriods >= 1);
  BT_ASSERT(moves.period[0] > 0 && moves.period[1] > 0 && moves.period[2] > 0);

  MovementChecks out;
  out.period = moves.period;
  out.directions = moves.directions;
  out.checks.resize(out.directions.size() * out.kinds());

  const std::unique_ptr<Voxel> v =
      GridType(kind).makeVoxel(static_cast<unsigned int>(moves.period[0]),
                               static_cast<unsigned int>(moves.period[1]),
                               static_cast<unsigned int>(moves.period[2]));
  mesh::CellGeometry geometry(*v);

  // A cell lies within its period box, so the cells a moved cell can meet
  // are within one box of the move (two cells where the box is one cell).
  // The cell is taken whole boxes away from the origin, far enough that
  // no coordinate looked at is negative (validCoordinate of some grids
  // takes none).
  mesh::CellAt reach;
  mesh::CellAt shift;
  const auto axis = [&](std::size_t a, int& reachOut, int& shiftOut) {
    reachOut = std::max(2, moves.period[a] * reachPeriods);
    int longest = 0;
    for (const std::array<int, 3>& d : moves.directions) {
      longest = std::max(longest, std::abs(d[a]));
    }
    const int need = reachOut + longest;
    shiftOut = moves.period[a] * ((need + moves.period[a] - 1) / moves.period[a]);
  };
  axis(0, reach.x, shift.x);
  axis(1, reach.y, shift.y);
  axis(2, reach.z, shift.z);

  for (int kz = 0; kz < moves.period[2]; kz++) {
    for (int ky = 0; ky < moves.period[1]; ky++) {
      for (int kx = 0; kx < moves.period[0]; kx++) {
        if (!v->validCoordinate(kx, ky, kz)) {
          continue;
        }
        const std::size_t cellKind =
            out.kindOf(static_cast<unsigned int>(kx), static_cast<unsigned int>(ky),
                       static_cast<unsigned int>(kz));
        const mesh::CellAt c{kx + shift.x, ky + shift.y, kz + shift.z};

        for (std::size_t dir = 0; dir < out.directions.size(); dir++) {
          const std::array<int, 3>& d = out.directions[dir];
          Result<std::vector<mesh::CellAt>> cells =
              mesh::sweptCells(geometry, c, {d[0], d[1], d[2]}, reach);
          if (!cells) {
            return std::unexpected(cells.error());
          }
          std::vector<std::array<int, 3>>& list = out.checks[dir * out.kinds() + cellKind];
          for (const mesh::CellAt& q : *cells) {
            list.push_back({q.x, q.y, q.z});
            out.extent[0] = std::max(out.extent[0], std::abs(q.x));
            out.extent[1] = std::max(out.extent[1], std::abs(q.y));
            out.extent[2] = std::max(out.extent[2], std::abs(q.z));
          }
        }
      }
    }
  }
  return out;
}

namespace {

// The checks of a grid that must have them: a failure here is a fault of
// the grid's tables, not of a puzzle.
MovementChecks derived(GridKind kind) {
  const std::optional<MovementDirections> moves = movementDirections(kind);
  BT_ASSERT(moves.has_value());
  Result<MovementChecks> checks = deriveMovementChecks(kind, *moves);
  BT_ASSERT_MSG(checks.has_value(), "the movement checks of a grid cannot be derived");
  return std::move(*checks);
}

}  // namespace

// One table per grid kind, each made on first use: the statics' own
// guards make that safe from the threads of a take-apart.
const MovementChecks* movementChecks(GridKind kind) {
  switch (kind) {
    case GridKind::Bricks: {
      static const MovementChecks checks = derived(GridKind::Bricks);
      return &checks;
    }
    case GridKind::TriangularPrism: {
      static const MovementChecks checks = derived(GridKind::TriangularPrism);
      return &checks;
    }
    case GridKind::Rhombic: {
      static const MovementChecks checks = derived(GridKind::Rhombic);
      return &checks;
    }
    case GridKind::TetraOcta: {
      static const MovementChecks checks = derived(GridKind::TetraOcta);
      return &checks;
    }
    case GridKind::Hexagonal: {
      static const MovementChecks checks = derived(GridKind::Hexagonal);
      return &checks;
    }
    case GridKind::Cairo: {
      static const MovementChecks checks = derived(GridKind::Cairo);
      return &checks;
    }
    case GridKind::Spheres:
    case GridKind::Sliding:
    case GridKind::Stacking:
      break;
  }
  return nullptr;
}

}  // namespace bt

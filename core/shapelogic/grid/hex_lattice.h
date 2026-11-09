// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The hexagonal prism grid's lattice (decision 0023): where a cell is in
/// space and which cell a point is in. Cells are hexagonal prisms with
/// flat sides left and right ("pointy top"), 1 across the flats and 1
/// high, in rows along x; each row sits half a cell to the right of the
/// one below it ("axial" coordinates: (x, y) is the cell x along row y,
/// so a space is a parallelogram of cells). Every integer coordinate is
/// a cell and every move by whole coordinates is a symmetry of the grid.
/// Shared by the voxel class, the meshes and the editor's layer geometry.

#include <cmath>

namespace bt::hex {

/// Rows are this far apart (sqrt 3 / 2).
inline constexpr double kRow = 0.8660254037844386;
/// A corner is this far from the centre (1 / sqrt 3).
inline constexpr double kRadius = 0.5773502691896258;

/// The centre of cell (x, y, z): the first cell of row 0 spans x from 0
/// to 1, row 0 has its lowest corner at y = 0, each row starts half a
/// cell further right, and a cell is 1 high from z.
[[nodiscard]] inline double centreX(int x, int y) noexcept { return x + 0.5 + 0.5 * y; }
[[nodiscard]] inline double centreY(int y) noexcept { return kRadius + y * kRow; }
[[nodiscard]] inline double centreZ(int z) noexcept { return z + 0.5; }

/// The cell whose centre is at (or nearest to) a point: the row first,
/// then the cell within it.
struct HexCell {
  int x;
  int y;
  int z;
};
[[nodiscard]] inline HexCell cellAt(double px, double py, double pz) noexcept {
  const int y = static_cast<int>(std::lround((py - kRadius) / kRow));
  const int x = static_cast<int>(std::lround(px - 0.5 - 0.5 * y));
  const int z = static_cast<int>(std::floor(pz));
  return {x, y, z};
}

/// The six corners of a cell around its centre, counter clockwise from
/// the lower right: x and y offsets.
inline constexpr double kCornerX[6] = {0.5, 0.5, 0.0, -0.5, -0.5, 0.0};
inline constexpr double kCornerY[6] = {-kRadius / 2, kRadius / 2, kRadius,
                                       kRadius / 2,  -kRadius / 2, -kRadius};

/// The six neighbours in the plane: east, north-east, north-west, west,
/// south-west, south-east (side k lies between corners k and k + 1).
inline constexpr int kAround[6][2] = {{1, 0}, {0, 1}, {-1, 1}, {-1, 0}, {0, -1}, {1, -1}};

}  // namespace bt::hex

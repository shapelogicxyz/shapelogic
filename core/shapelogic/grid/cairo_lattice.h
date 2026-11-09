// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The cairo prism grid's lattice (decision 0023): the Cairo pentagonal
/// tiling, stacked. After the cairo grid of BurrTools V2
/// (data/scripts/grid_cairo.lua), whose coordinates these are: cells are
/// the integer coordinates with x + y odd; four kinds of cell, one for
/// each way the pentagon turns, repeating with the lattice spanned by
/// (2, -2) and (2, 2), that is by the moves (u, v) with u and v even and
/// u - v a multiple of 4. The pentagons have five sides of one length
/// (the offset `kOfs` makes them so) and meet four at the lattice points,
/// which are the grid's 4-fold centres; (0, 0) is one. In mesh units the
/// plane is scaled by `kScale` and moved by one so that a space's cells
/// lie in x, y >= 0; a cell is 1 high from z.
/// Shared by the voxel class, the meshes and the editor's layer geometry.

#include <array>
#include <vector>

namespace bt::cairo {

/// The offset that gives all five sides one length: 4/3 - sqrt(7)/3.
inline constexpr double kOfs = 0.4514162296451364;
/// Mesh units per lattice unit: a cell has area 2 in lattice units, 1 in
/// mesh units (1 / sqrt 2).
inline constexpr double kScale = 0.7071067811865476;

/// A coordinate of a pentagon's corner: `a + o * kOfs`, exactly.
struct Coord {
  int a = 0;
  int o = 0;
  [[nodiscard]] double value() const { return a + o * kOfs; }
  bool operator==(const Coord&) const = default;
  auto operator<=>(const Coord&) const = default;
};

/// A corner of a pentagon in the plane.
struct Point {
  Coord x;
  Coord y;
  bool operator==(const Point&) const = default;
  auto operator<=>(const Point&) const = default;
};

/// Whether (x, y) is a cell.
[[nodiscard]] constexpr bool isCell(int x, int y) noexcept { return ((x + y) & 1) != 0; }

/// Whether a move by (u, v) keeps every cell its kind (a lattice vector).
[[nodiscard]] constexpr bool keepsKinds(int u, int v) noexcept {
  return (u & 1) == 0 && (v & 1) == 0 && ((u - v) & 3) == 0;
}

/// The kind of cell (x, y) is, 0 to 3, or -1 when it is no cell.
[[nodiscard]] int kindOf(int x, int y) noexcept;

/// The base cell of each kind.
inline constexpr int kBase[4][2] = {{0, -1}, {1, 0}, {-1, 0}, {0, 1}};

/// The pentagon of cell (x, y) in lattice units, counter clockwise.
[[nodiscard]] std::array<Point, 5> pentagon(int x, int y) noexcept;

/// The smallest a, b (a + b least, 0 to 3 each) that make (u + a, v + b)
/// a lattice vector.
struct Fix {
  int a;
  int b;
};
[[nodiscard]] Fix fixToLattice(int u, int v) noexcept;

/// The cells of the plane that share a side with a cell of each kind, in
/// the order of the pentagon's sides, as (dx, dy); and those that share a
/// corner but no side.
struct Around {
  std::array<std::array<int, 2>, 5> sides;
  std::vector<std::array<int, 2>> corners;
};
[[nodiscard]] const Around& around(int kind) noexcept;

}  // namespace bt::cairo

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Basic types shared by the grid module.
/// Ported from src/lib/types.h and src/lib/symmetries.h (the symmetry macros).

#include <cstdint>

namespace bt {

/// One cell of a voxel space: the low 2 bits are the state (Voxel::kEmpty,
/// kFilled, kVariable), the upper 6 bits a colour (0 = neutral, 1..63).
/// Assembly-result spaces use the whole byte as a piece number instead.
using VoxelType = std::uint8_t;

/// The number of a symmetry group: an index into the grid's table of all
/// possible sets of transformations that map a shape onto itself (see
/// Symmetries). The brick and prism grids have a fixed table; the sphere
/// grid starts from a generated one of 241 and appends the list of any
/// shape it has not met before (grid/symmetries_2.h), so the number leaves
/// room for more than 256.
using Symmetry = std::uint16_t;

/// The symmetry value meaning "not calculated yet".
inline constexpr Symmetry kSymmetryInvalid = 0xFFFF;

/// Transformation result meaning "this combination does not exist" (TND in
/// the original's tables).
inline constexpr unsigned char kTransformUndefined = 0xFF;

/// True for the group that holds only the identity: the shape has no symmetry.
[[nodiscard]] constexpr bool unSymmetric(Symmetry s) noexcept { return s == 0; }

/// True for kSymmetryInvalid.
[[nodiscard]] constexpr bool isSymmetryInvalid(Symmetry s) noexcept {
  return s == kSymmetryInvalid;
}

/// Integer division rounding towards minus infinity (as in Python).
/// Ported from src/tools/intdiv.h.
template <class T>
[[nodiscard]] constexpr T intDivFloor(T x, T y) noexcept {
  return x / y - ((x % y && (x % y ^ y) < 0) ? 1 : 0);
}

}  // namespace bt

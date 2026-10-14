// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Edit ▸ Convert brick grid type to other: a cube-grid puzzle's shapes on
/// a grid that is a cut cube grid, each cube becoming the block of cells
/// that fills it (5x5x5 rhombic cells, 3x3x3 tetra-octa cells).
/// Ported from src/lib/converter.{h,cpp}.

#include "shapelogic/base/error.h"
#include "shapelogic/grid/grid_type.h"
#include "shapelogic/model/puzzle.h"

#include <cstdint>
#include <vector>

namespace bt {

/// True when a puzzle on grid `src` can be converted to `dst`
/// (canConvert): bricks to rhombic and to tetra-octa.
[[nodiscard]] bool canConvertGrid(GridKind src, GridKind dst) noexcept;

/// The grids a puzzle on `src` converts to, in the grids' order (what the
/// original's dialog offered).
[[nodiscard]] std::vector<GridKind> convertTargets(GridKind src);

/// The most cells the converted shapes may hold together: a cube becomes
/// 125 rhombic cells, so a large brick shape asked for gigabytes (the
/// original had no limit).
inline constexpr std::uint64_t kMaxConvertedCells = std::uint64_t{1} << 27;

/// A new puzzle on grid `target` with the shapes converted (doConvert):
/// their names, the colours, the notes and a copy of each problem's
/// definition with its name (no solutions: unsolved). Unsupported when
/// canConvertGrid() says no; InvalidArgument when the shapes would pass
/// `maxCells` together.
[[nodiscard]] Result<Puzzle> convertGrid(const Puzzle& p, GridKind target,
                                         std::uint64_t maxCells = kMaxConvertedCells);

}  // namespace bt

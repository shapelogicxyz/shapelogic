// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The Document's rules before a solve, for every program that solves
/// (cleanup item 11.5): why a sliding problem cannot be solved as it
/// stands, and the shapes as a run leaves them. The Document, the Python
/// module and the CLI's --save call them, so the three refuse alike.

#include "shapelogic/base/error.h"

namespace sl {
class Problem;
class Puzzle;
}  // namespace sl

namespace sl::app {

/// Why `problem` of a sliding puzzle cannot be solved as it stands
/// (InvalidArgument; what the original's Solve checked): a start/goal shape
/// whose marks make no puzzle, a stamp larger than its piece, no result
/// shape, no pieces. Nothing for the other grids: the solve session refuses
/// what they lack, in the same words.
[[nodiscard]] Result<void> solveRefusal(const Puzzle& puzzle, const Problem& problem);

/// The shapes as a run leaves them, so that a solve only reads them: the
/// hotspots where a run puts them (a stacking puzzle's discs at their
/// centers) and the caches warm.
void prepareShapesForSolving(Puzzle& puzzle);

}  // namespace sl::app

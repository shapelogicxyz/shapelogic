// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The text drawings of burrTxt: a shape, the puzzle, an assembly and a
/// take-apart, layer by layer side by side (z to the right, y down).
/// Ported from src/lib/print.cpp; the output is the original's byte for
/// byte, except where the original drew the wrong shapes (see
/// printSeparation).

#include <cstdio>

namespace bt {
class Assembly;
class Problem;
class Puzzle;
class Separation;
class Voxel;
}  // namespace bt

namespace bt::cli {

/// A shape: its layers framed, cells as letters from `base` (the cell's
/// whole value: state and colour), then its bounds and hotspot.
void printShape(std::FILE* out, const Voxel& v, char base = 'a');

/// Every shape, then every problem with its result and parts.
void printPuzzle(std::FILE* out, const Puzzle& puzzle);

/// An assembly inside the result's box, one letter per placed piece.
void printAssembly(std::FILE* out, const Puzzle& puzzle, const Problem& problem,
                   const Assembly& assembly);

/// Every state of a take-apart, the removed part's tree, then the part's
/// that stays, in a box half the result's size bigger on each side.
///
/// The original took the shape of the n-th piece of the problem for the
/// n-th piece of each node (and named it by the node's place in its
/// parent), which is right only at the root: below it the wrong shapes
/// are drawn. `originalDrawings` draws as it did (for the comparison with
/// burrTxt); otherwise each piece is drawn as itself.
void printSeparation(std::FILE* out, const Puzzle& puzzle, const Problem& problem,
                     const Separation& separation, const Assembly& assembly,
                     bool originalDrawings);

}  // namespace bt::cli

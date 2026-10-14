// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Stacking puzzles: discs on vertical rods, moved one top disc at a time
/// from rod to rod. A puzzle has boards (rod sets, Puzzle::getRodSet); a
/// problem chooses one and holds the discs on each rod at the start and at
/// the goal (Problem::startStacks, goalStacks). Here are the rules of a
/// board (the height of its rods, the size rule, the distance rule, Panex
/// columns with their pocket and outside channel), the discs (shapes one
/// cell thick whose size is their radius), the editing of the stacks, the
/// check that a problem is ready to solve, and where the rods and discs
/// stand for drawing and for a solution's frames. The searches are
/// bt/solve/stack_search.h and bt/solve/panex_search.h.
///
/// Ported from src/lib/stacking.{h,cpp} (all but findStackPath and its
/// breadth-first search, and the file part, which is bt/io).

#include "shapelogic/model/assembly.h"
#include "shapelogic/model/disassembly.h"
#include "shapelogic/model/stacking_types.h"

#include <string>
#include <vector>

namespace bt {
class Problem;
class Puzzle;
class Voxel;
}  // namespace bt

namespace bt::stacking {

/// True when the puzzle uses the stacking grid.
[[nodiscard]] bool isStacking(const Puzzle& puzzle);

/// Rods on the board: the rod count, plus the pocket column when it is on.
[[nodiscard]] unsigned int totalRods(const RodSet& board);

/// True for the pocket column. It is the last rod in a stack map, so turning
/// it on or off never renumbers the other rods, but it stands beside rod 1.
[[nodiscard]] bool isPocket(const RodSet& board, unsigned int rod);

/// Where a rod stands along the board: rod r at r, the pocket at -1.
[[nodiscard]] int rodPosition(const RodSet& board, unsigned int rod);

/// The outside channel is in use: it is set, with Panex columns and three
/// rods or more.
[[nodiscard]] bool hasOutsideChannel(const RodSet& board);

/// A move between these rods can go through the outside channel.
[[nodiscard]] bool viaOutsideChannel(const RodSet& board, unsigned int from, unsigned int to);

/// "rod 2" or "the pocket column", for messages.
[[nodiscard]] std::string rodName(const RodSet& board, unsigned int rod);

/// Height of the rods: the defined height, or the disc count when they grow.
[[nodiscard]] unsigned int rodHeight(const RodSet& board, unsigned int discCount);

/// Discs one rod holds: a Panex column one more than its height, the pocket
/// its own height.
[[nodiscard]] unsigned int rodCapacity(const RodSet& board, unsigned int rod,
                                       unsigned int discCount);

// ---- discs ----------------------------------------------------------------------

/// Rebuilds a flat disc. Size 1 is a 3x3 square, one cell thick, with the
/// centre cell empty. Each larger size adds one ring, so size n is
/// (2n+1)x(2n+1) with only the centre empty. Keeps the current colour.
void generateDisk(Voxel& v, unsigned int size);

/// Puts the hotspot at the disc centre, so a rod position is the disc's
/// middle; nothing for a shape that is not a disc.
void ensureHotspot(Voxel& v);

/// Puts a shape's hotspot where a stacking solve has it (the GUI's Solve:
/// initHotspot, then ensureHotspot): a disc's at its centre, another
/// shape's where initHotspot puts it. Writes only a hotspot that is
/// elsewhere (a write drops the shape's caches, which a run of another
/// problem may be reading); true when one was written.
bool placeSolvingHotspot(Voxel& v);

/// Adds a disc shape of `size` (at least 1); returns its shape index.
unsigned int addDisk(Puzzle& puzzle, unsigned int size = 1);

/// A disc of a problem as the searches number them: parts, then copies.
struct Disc {
  unsigned int shapeId = 0;
  unsigned int instance = 0;
  /// The disc's size (a shape that is no disc counts as size 1).
  unsigned int size = 1;
};

/// The problem's discs, numbered as the searches and the frames number
/// them (the problem's pieces).
[[nodiscard]] std::vector<Disc> discsOf(const Puzzle& puzzle, const Problem& problem);

// ---- the stacks -----------------------------------------------------------------

/// Gives the start and goal maps at least the rod set's rod count. Rods
/// past the count are kept while they still hold discs, so no disc is lost.
/// Nothing when the problem has no rod set.
void syncMaps(const Puzzle& puzzle, Problem& problem);

/// Empty when every disc is on the start and the goal exactly once and both
/// stackings obey the rod set's rod count, height and size rules. Otherwise
/// one sentence naming the first problem, rule breaks before missing discs.
[[nodiscard]] std::string setupError(const Puzzle& puzzle, const Problem& problem);

/// Places the next unused copy of shapeId on top of rod. Empty on success,
/// else why not. With enforceRules false the height and size rules are not
/// checked, so the editor can build a stacking setupError then reports.
std::string placeDisk(const Puzzle& puzzle, Problem& problem, bool goal, unsigned int shapeId,
                      unsigned int rod, bool enforceRules = true);

/// Takes the top disc off rod. Empty on success, else why not.
std::string liftTop(const Puzzle& puzzle, Problem& problem, bool goal, unsigned int rod);

/// Removes one disc copy from both stackings and renumbers the later copies.
void dropInstance(Problem& problem, unsigned int shapeId, unsigned int instance);

/// Swaps a disc with the one delta places away; +1 moves it up the rod.
/// False when there is no such place.
bool moveDisk(const Puzzle& puzzle, Problem& problem, bool goal, unsigned int rod,
              unsigned int index, int delta);

/// Hotspot position of one disc; placed false when the disc is on no rod.
struct Hotspot {
  int x = 0;
  int y = 0;
  int z = 0;
  bool placed = false;
};

/// Where the rods and the discs of one stacking stand, for the 3D view and
/// a solution's frames.
struct BoardLayout {
  int spacing = 4;
  unsigned int rodCount = 0;
  unsigned int rodHeight = 1;
  /// x of each rod, the pocket column included.
  std::vector<int> rodX;
  /// Drawn height of each rod; the pocket column is shorter.
  std::vector<unsigned int> rodHeights;
  /// Rods are Panex columns: drawn with a V that narrows to the bottom.
  bool panex = false;
  /// The last rod is the pocket column, drawn as a plain peg.
  bool pocket = false;
  /// Largest disc size, so the V stays inside the rod spacing.
  unsigned int maxSize = 1;
  /// Parallel to the problem's pieces (parts, then copies).
  std::vector<Hotspot> disks;
};

/// The board and the start (or goal) stacking of a problem as it stands.
[[nodiscard]] BoardLayout layoutBoard(const Puzzle& puzzle, const Problem& problem, bool goal);

// ---- what the searches read and make ----------------------------------------------

/// A stacking as a search sees it: for each rod, its discs bottom to top,
/// numbered as discsOf numbers them.
using Stacking = std::vector<std::vector<unsigned int>>;

/// True when the top disc of rod `from` may go onto rod `to` under the
/// board's rules (the Stacking Solver's move rule).
[[nodiscard]] bool moveAllowed(const RodSet& board, const std::vector<Disc>& discs,
                               const Stacking& stacking, unsigned int from, unsigned int to);

/// Start, goal and each disc's size, for a search. False when setupError is
/// not empty.
[[nodiscard]] bool searchInput(const Puzzle& puzzle, const Problem& problem, Stacking& start,
                               Stacking& goal, std::vector<unsigned int>& sizes);

/// Placements per transfer in a path: lift, cross, drop.
inline constexpr unsigned int kStepsPerMove = 3;

/// The frames of a path of stackings, start to goal: a separation of one
/// node whose states put each disc where it stands (the rod's x and the
/// height on it, not shifts), three a transfer (lift above the rods, cross,
/// drop), so the move slider can scrub that motion.
[[nodiscard]] Separation pathSeparation(const Puzzle& puzzle, const Problem& problem,
                                        const std::vector<Stacking>& path);

/// Transfers, not the three frames of each transfer.
[[nodiscard]] unsigned int logicalMoves(const Separation& path);

/// The assembly whose placements are the start stacking: each disc at its
/// hotspot place (a solution's assembly).
[[nodiscard]] Assembly startAssembly(const Puzzle& puzzle, const Problem& problem);

/// The width of the board, the animation's unit of distance.
[[nodiscard]] unsigned int boardSpan(const Puzzle& puzzle, const Problem& problem);

}  // namespace bt::stacking

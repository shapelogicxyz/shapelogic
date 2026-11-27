// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// What a stacking puzzle's file holds besides its shapes: the boards (rod
/// sets) of the puzzle, and for each problem the board it uses and the
/// discs on each rod at the start and at the goal. Plain values; the rules
/// and the editing live in shapelogic/model/stacking.h.
/// Ported from src/lib/stacking.h (rodSet_c, diskRef_c, stackMap_c).

#include <string>
#include <vector>

namespace sl::stacking {

/// "No rod set": the value of a problem that has none.
inline constexpr unsigned int kNoRodSet = 0xFFFFFFFF;

/// The GUI's bounds, which the document and the engine refuse past (and
/// the schema and the web take: cleanup item 11.2): the rod count's input
/// (STACK_ROD_BUTTONS), the heights' inputs, and the disc list's size
/// (onSize's message).
inline constexpr unsigned int kMaxRods = 16;
inline constexpr unsigned int kMaxHeight = 64;
inline constexpr unsigned int kMaxDiscSize = 100;

/// One board: a row of rods and the rules that govern moves (rodSet_c).
struct RodSet {
  std::string name;
  unsigned int rodCount = 3;
  /// When true, a rod can hold every disc in the problem.
  bool growHeight = true;
  /// Discs a rod can hold when growHeight is false.
  unsigned int definedHeight = 8;
  /// A larger size number may not sit on a smaller one. Equal sizes may stack.
  bool sizeMatters = true;
  /// When true, a disc may move only to a neighboring rod.
  bool distanceMatters = false;
  /// Kept so older files still load. The editor no longer uses it.
  bool canMoveOver = false;
  /// Panex columns: a disc of size s may sit at most s places below the top
  /// of its rod, and discs stack in any order (size matters is ignored). One
  /// disc more than the height may wait at the top, raised into the bridge
  /// that joins the rods; a raised disc blocks every move that passes over
  /// it.
  bool panexColumns = false;
  /// With Panex columns: an extra rod beside rod 1 that holds any disc.
  bool pocketColumn = false;
  /// Discs the pocket column holds.
  unsigned int pocketHeight = 1;
  /// With Panex columns and three rods or more: a channel outside the
  /// bridge joins the first and last rods, so a move between them is not
  /// blocked by a disc raised on a rod in between.
  bool outsideChannel = false;

  bool operator==(const RodSet&) const = default;
};

/// One disc occurrence: the shape and which copy of it (0 .. count-1).
struct DiskRef {
  unsigned int shapeId = 0;
  unsigned int instance = 0;

  bool operator==(const DiskRef&) const = default;
};

/// The discs of each rod, bottom to top.
struct StackMap {
  std::vector<std::vector<DiskRef>> rods;

  bool operator==(const StackMap&) const = default;
};

}  // namespace sl::stacking

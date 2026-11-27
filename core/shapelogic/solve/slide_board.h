// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The slide search's board rules (internal to the slide search and the
/// play, cleanup item 21.1): the steps a piece takes, its cells and
/// outline, the goal check, the stop grid of optimized paths, the shifts a
/// group can reach and the route with the fewest turns, nested groups.

#include "shapelogic/base/assert.h"
#include "shapelogic/base/int_cast.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/model/slide_marks.h"
#include "shapelogic/model/sliding.h"
#include "shapelogic/solve/sliding_search.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace bt {
class SearchSample;
}  // namespace bt

namespace bt::sliding::detail {

// The six steps a piece can take: in its layer first, then down and up.
// Down and up are taken only with vertical moves (SlideSearch); on a tray
// one layer deep they never fit, so a flat puzzle is searched exactly as
// the original searched it either way.
inline constexpr int kDirs = 6;
inline constexpr int kLayerDirs = 4;
inline constexpr int kDx[kDirs] = {0, 0, -1, 1, 0, 0};
inline constexpr int kDy[kDirs] = {-1, 1, 0, 0, 0, 0};
inline constexpr int kDz[kDirs] = {0, 0, 0, 0, -1, 1};

inline std::size_t idx(int v) { return static_cast<std::size_t>(v); }

// The steps of kDx/kDy/kDz a piece may take: all six with vertical moves,
// else the four in its layer.
inline int dirsFor(bool vertical) { return vertical ? kDirs : kLayerDirs; }

// Whether (x, y, z) is in the tray and not a wall (an empty cell).
inline bool isFloor(const Voxel& tray, int x, int y, int z) {
  if (x < 0 || y < 0 || z < 0 || asUnsigned(x) >= tray.getX() || asUnsigned(y) >= tray.getY() ||
      asUnsigned(z) >= tray.getZ()) {
    return false;
  }
  return tray.getState(asUnsigned(x), asUnsigned(y), asUnsigned(z)) != Voxel::kEmpty;
}

// Where a piece is: its hotspot and its orientation.
struct Place {
  int x = 0;
  int y = 0;
  unsigned char trans = 0;
  // The layer the hotspot sits on: a piece may change layers on a tray
  // more than one layer deep, such as lifting a block out of a box.
  int z = 0;
};

// An arrangement: every piece's place, in the assembly's order.
struct SlideState {
  std::vector<Place> places;
};

// An arrangement as a live preview's picture, when one is asked for: the
// pieces are in the assembly's order.
void putSample(SearchSample* sample, const SlideState& st);

// An arrangement as the text key of legacySearch.
std::string stateKey(const SlideState& s);

// Whether each piece sits where the goal wants it. Every piece is judged on
// its own, so a position search can work the answer out once per position
// (at: 0 it cannot end there, 1 it can, 2 it fills one of its goals); a
// piece with goals for only some of its copies reaches the goal when as
// many copies fill its goals as it has goals (together they hold exactly
// the goals' cells), the others ending anywhere.
class GoalCheck {
 public:
  GoalCheck(const Puzzle& puzzle, const Problem& problem, const std::vector<const Voxel*>& pieces)
      : pieces_(pieces) {
    tray_ = problem.resultValid() ? &problem.resultShape(puzzle) : nullptr;
    marks_ = &problem.slideMarks();
    startGoal_ = tray_ != nullptr && isStartGoalShape(tray_);
    goal_ = (!startGoal_ && problem.goalValid()) ? &puzzle.getShape(problem.getGoalId()) : nullptr;

    // each placement's part, to look up the colors
    std::vector<unsigned int> partOf(pieces.size());
    {
      unsigned int pc = 0;
      for (unsigned int part = 0; part < problem.getNumberOfParts(); part++) {
        for (unsigned int j = 0; j < problem.getPartMaximum(part); j++) {
          partOf[pc++] = part;
        }
      }
    }

    color_.assign(pieces.size(), 0);
    group_.assign(pieces.size(), -1);
    // each part's goals counted once: those with fewer than its copies
    std::vector<int> partGroup(problem.getNumberOfParts(), -1);
    for (std::size_t i = 0; i < pieces.size(); i++) {
      const unsigned int shapeId = problem.getShapeIdOfPart(partOf[i]);
      if (startGoal_) {
        const unsigned int col = shapeId + 1;
        unsigned int marked = 0;
        for (unsigned int gi = 0; gi < tray_->getXYZ(); gi++) {
          if (marks_->goal(gi) == col) {
            color_[i] = col;
            marked++;
          }
        }
        const unsigned int part = partOf[i];
        const unsigned int copies = problem.getPartMaximum(part);
        const unsigned int size = cellCount(problem.partShape(puzzle, part));
        const unsigned int goals = size == 0 ? 0 : marked / size;
        if (marked > 0 && goals < copies) {
          if (partGroup[part] < 0) {
            partGroup[part] = static_cast<int>(need_.size());
            need_.push_back(goals);
          }
          group_[i] = partGroup[part];
        }
      } else if (goal_ != nullptr) {
        unsigned int col = pieceColor(puzzle, shapeId);
        if (col == 0) {
          col = partOf[i] + 1;
        }
        for (unsigned int gi = 0; gi < goal_->getXYZ(); gi++) {
          if (goal_->getColor(gi) == col) {
            color_[i] = col;
            break;
          }
        }
      }
    }
    // reached() counts the groups in an array of that size
    BT_ASSERT(need_.size() <= kMaxGroups);
  }

  // Whether a piece has goals for only some of its copies (then the
  // arrangement's count decides, not each piece).
  [[nodiscard]] bool counts() const { return !need_.empty(); }
  // The group of piece i's part among those (-1: none), and how many of
  // its copies must fill its goals.
  [[nodiscard]] int group(std::size_t i) const { return group_[i]; }
  [[nodiscard]] const std::vector<unsigned int>& need() const { return need_; }

  // Piece i, placed at place, is where the goal wants it.
  [[nodiscard]] bool piece(std::size_t i, const Place& place) const { return at(i, place) != 0; }

  // Piece i, placed at place, fills one of its goals (false for a piece
  // with none, which ends anywhere).
  [[nodiscard]] bool fillsGoal(std::size_t i, const Place& place) const {
    return color_[i] != 0 && at(i, place) == (group_[i] >= 0 ? 2 : 1);
  }

  // 0: piece i cannot end at place; 1: it can; 2: it can, and fills one of
  // its goals (only for a piece of a group).
  [[nodiscard]] char at(std::size_t i, const Place& place) const {
    const Voxel* p = pieces_[i];
    if (startGoal_) {
      if (p == nullptr) {
        return 0;
      }
      // Empty cells are walls. A piece may slide across a variable cell,
      // and the finished position must leave every variable cell empty.
      // Only normal cells are places to stop, a goal stamp included.
      bool ok = true;
      bool onGoal = color_[i] != 0;
      forCells(*p, place, [&](int tx, int ty, int tz) {
        if (!isFloor(*tray_, tx, ty, tz) ||
            tray_->getState(asUnsigned(tx), asUnsigned(ty), asUnsigned(tz)) == Voxel::kVariable) {
          ok = false;
        } else if (color_[i] != 0 &&
                   marks_->goal(tray_->getIndex(asUnsigned(tx), asUnsigned(ty),
                                                asUnsigned(tz))) != color_[i]) {
          onGoal = false;
        }
      });
      if (!ok) {
        return 0;
      }
      if (group_[i] >= 0) {
        return onGoal ? 2 : 1;
      }
      return color_[i] == 0 || onGoal ? 1 : 0;
    }
    if (goal_ == nullptr || color_[i] == 0) {
      return 1;
    }
    if (p == nullptr) {
      return 0;
    }
    bool ok = true;
    forCells(*p, place, [&](int tx, int ty, int tz) {
      if (!isFloor(*goal_, tx, ty, tz) ||
          goal_->getColor(asUnsigned(tx), asUnsigned(ty), asUnsigned(tz)) != color_[i]) {
        ok = false;
      }
    });
    return ok ? 1 : 0;
  }

  // Whether pieces whose `at` answers are given reach the goal.
  template <class At>
  [[nodiscard]] bool reached(At atOf) const {
    // Only the groups in use are zeroed: this runs for every arrangement
    // the search emits.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init): the first need_.size() entries are zeroed here and no others are read
    std::array<unsigned int, kMaxGroups> filled;
    std::fill_n(filled.begin(), need_.size(), 0U);
    for (std::size_t i = 0; i < pieces_.size(); i++) {
      const char a = atOf(i);
      if (a == 0) {
        return false;
      }
      if (a == 2) {
        filled[idx(group_[i])]++;
      }
    }
    for (std::size_t g = 0; g < need_.size(); g++) {
      if (filled[g] != need_[g]) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] bool all(const SlideState& s) const {
    return reached([&](std::size_t i) { return at(i, s.places[i]); });
  }

 private:
  template <class F>
  static void forCells(const Voxel& p, const Place& place, F f) {
    const int hx = p.getHx();
    const int hy = p.getHy();
    const int hz = p.getHz();
    for (unsigned int z = 0; z < p.getZ(); z++) {
      for (unsigned int y = 0; y < p.getY(); y++) {
        for (unsigned int x = 0; x < p.getX(); x++) {
          if (p.getState(x, y, z) != Voxel::kEmpty) {
            f(place.x + asInt(x) - hx, place.y + asInt(y) - hy, place.z + asInt(z) - hz);
          }
        }
      }
    }
  }

  const std::vector<const Voxel*>& pieces_;
  const Voxel* tray_ = nullptr;
  const SlideMarks* marks_ = nullptr;  ///< the problem's goals on the tray
  const Voxel* goal_ = nullptr;
  bool startGoal_ = false;
  // The goal color of each piece; 0 when the goal does not place it.
  std::vector<unsigned int> color_;
  // A group is a part's whose shape has marks, and only the shapes up to
  // the highest mark have them: so at most this many groups.
  static constexpr std::size_t kMaxGroups = SlideMarks::kMaxMark + 1;
  // Each piece's group (-1: none) and each group's count of goals.
  std::vector<int> group_;
  std::vector<unsigned int> need_;

  static unsigned int cellCount(const Voxel& v) {
    unsigned int n = 0;
    for (unsigned int i = 0; i < v.getXYZ(); i++) {
      n += v.getState(i) != Voxel::kEmpty ? 1 : 0;
    }
    return n;
  }
};

// One cell of a piece, relative to its hotspot.
struct Cell3 {
  int x;
  int y;
  int z;
};
using Cell3List = std::vector<Cell3>;
// Shifts of a piece or a group, (dx, dy, dz).
using ShiftList = std::vector<SlideShift>;

// The filled cells of a piece, every layer, relative to its hotspot. Pieces
// only ever translate in a sliding search, so these are worked out once.
Cell3List cellsOf(const Voxel& p);

// A piece's outline: its own cells plus every empty cell that has cells of
// the piece on both sides of it in the same row or the same column of its
// layer, such as the inside of a pocket. Relative to the hotspot.
Cell3List outlineOf(const Voxel& p);

// Whether two pieces have the same cells, in the same order.
bool sameCells(const Cell3List& a, const Cell3List& b);

// The most pieces nested in one piece at once for which the moves are made
// backwards (SlideMover::movesBack tries every part of what a piece holds,
// twice as many with each piece more).
inline constexpr unsigned int kMaxBackNested = 6;

// The most pieces that could ever be nested in one piece at once, those
// nested in the nested ones too: a bound, never too low. A piece nests in
// another when its cells lie in the other's outline, so in its holes (the
// outline less the cells), which have room for only so many of the smallest
// piece that fits them at all.
unsigned int nestedBound(const std::vector<Cell3List>& cells,
                         const std::vector<Cell3List>& outlines);

// SlideSearch::grid: the offsets, x and y modulo the grid's size, at which
// each piece may stop. A piece with none stops anywhere, so a piece the
// grid knows nothing of is never held still by it.
struct StopGrid {
  int size = 0;
  // Each piece's offsets, sorted.
  std::vector<std::vector<std::array<int, 2>>> offsets;

  [[nodiscard]] int mod(int v) const { return ((v % size) + size) % size; }

  [[nodiscard]] bool allows(std::size_t i, int x, int y) const {
    if (size <= 1 || offsets[i].empty()) {
      return true;
    }
    const std::array<int, 2> o{mod(x), mod(y)};
    return std::binary_search(offsets[i].begin(), offsets[i].end(), o);
  }
};

// The stop grid of `size` for pieces of these cells and outlines whose
// starts and goals are `known`: a piece's offsets are those of the known
// places of every piece with the same cells (the copies of a block share
// them, and so do the pieces that swap holes), and those at which it lies
// wholly inside another piece's outline, off its cells, while that piece
// is at one of those offsets (a hole no piece starts in).
StopGrid stopGridFor(int size, const std::vector<Cell3List>& cells,
                     const std::vector<Cell3List>& outlines,
                     const std::vector<std::vector<Place>>& known);

// Each piece's start and every place on the tray where it fills one of its
// goals: where its grid offsets come from.
std::vector<std::vector<Place>> knownPlaces(const Voxel& tray, const std::vector<Cell3List>& cells,
                                            const SlideState& initial, const GoalCheck& goals);

// Every shift the movers can reach as one group while the others stay put.
// One move of the solver is a jump to any of them, whatever the route.
ShiftList reachableShifts(const Voxel& tray, const std::vector<Cell3List>& cells,
                          const SlideState& s, const std::vector<unsigned int>& movers,
                          bool vertical);

// The corner points, as shifts from the start, of a route with the fewest
// straight runs that takes the movers to shift `to`, start and end
// included, in their layer only unless `vertical`. Empty when unreachable.
ShiftList fewestTurns(const Voxel& tray, const std::vector<Cell3List>& cells, const SlideState& s,
                      const std::vector<unsigned int>& movers, const SlideShift& to,
                      bool vertical);

// `outer` and everything nested inside it: pieces whose cells all lie in
// the outline of a piece already in the group, or with `partial`, pieces
// with any cell there; repeated until no more join. Pieces that merely
// touch are not nested.
std::vector<unsigned int> nestedGroup(const std::vector<Cell3List>& cells,
                                      const std::vector<Cell3List>& outlines, const SlideState& s,
                                      unsigned int outer, bool partial);

}  // namespace bt::sliding::detail

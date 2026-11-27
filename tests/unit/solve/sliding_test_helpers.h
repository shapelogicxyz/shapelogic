// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// What the slide search's tests share (test_sliding.cpp,
/// test_sliding_saved.cpp, model/test_slide_marks.cpp; cleanup item 21.2):
/// the cell states, puzzles built from rectangles (SlideFixture), Klotski
/// and the eight puzzle, the grid boards of optimized paths and their
/// checks, the searches from both ends.

#include "shapelogic/grid/grid_type.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/model/assembly.h"
#include "shapelogic/model/disassembly.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/model/sliding.h"
#include "shapelogic/solve/sliding_search.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace bt::test {

// The helpers are written in the slide search's names, as the cases are.
using namespace bt::sliding;

inline constexpr int kFilled = Voxel::kFilled;

inline constexpr int kVariable = Voxel::kVariable;

inline constexpr int kEmpty = Voxel::kEmpty;

// A sliding puzzle built from rectangles: each piece is its own shape, w x h,
// with its top-left cell at (x, y); goal says where it must end, or is
// negative when it may end anywhere.
struct SlideBlock {
  unsigned int w;
  unsigned int h;
  int x;
  int y;
  int goalX;
  int goalY;
};

struct SlideFixture {
  Puzzle puz{GridType(GridKind::Sliding)};
  std::unique_ptr<Assembly> start;
  [[nodiscard]] const Problem& pr() const { return puz.getProblem(0); }
};

inline SlideFixture slideFixture(unsigned int trayW, unsigned int trayH,
                          const std::vector<SlideBlock>& blocks) {
  SlideFixture f;
  const unsigned int trayId = addStartGoalShape(f.puz, trayW, trayH);
  f.start = std::make_unique<Assembly>(f.puz.getGridType());
  for (const SlideBlock& b : blocks) {
    const unsigned int id = f.puz.addShape(b.w, b.h, 1);
    for (unsigned int y = 0; y < b.h; y++) {
      for (unsigned int x = 0; x < b.w; x++) {
        f.puz.getShape(id).setState(x, y, 0, kFilled);
        Voxel& tray = f.puz.getShape(trayId);
        tray.setStartPiece(static_cast<unsigned int>(b.x) + x, static_cast<unsigned int>(b.y) + y, 0,
                      id + 1);
        if (b.goalX >= 0) {
          tray.setGoalPiece(tray.getIndex(static_cast<unsigned int>(b.goalX) + x,
                                          static_cast<unsigned int>(b.goalY) + y, 0),
                            id + 1);
        }
      }
    }
    f.start->addPlacement(0, b.x, b.y, 0);
  }
  syncSlidingProblems(f.puz);
  return f;
}

// Every step of a slide path moves exactly one piece.
inline bool oneMoverPerStep(const Separation& path) {
  for (unsigned int step = 0; step < path.getMoves(); step++) {
    unsigned int moved = 0;
    for (unsigned int i = 0; i < path.getPieceNumber(); i++) {
      if (path.getState(step).getX(i) != path.getState(step + 1).getX(i) ||
          path.getState(step).getY(i) != path.getState(step + 1).getY(i)) {
        moved++;
      }
    }
    if (moved != 1) {
      return false;
    }
  }
  return true;
}

// Klotski, the classic "Heng Dao Li Ma" start: a 4x5 tray, the 2x2 block to
// bring to the bottom center past four upright 1x2, one flat 2x1 and four
// unit pieces.
inline const std::vector<SlideBlock> kKlotski = {
    {2, 2, 1, 0, 1, 3},   {1, 2, 0, 0, -1, -1}, {1, 2, 3, 0, -1, -1}, {1, 2, 0, 2, -1, -1},
    {1, 2, 3, 2, -1, -1}, {2, 1, 1, 2, -1, -1}, {1, 1, 1, 3, -1, -1}, {1, 1, 2, 3, -1, -1},
    {1, 1, 0, 4, -1, -1}, {1, 1, 3, 4, -1, -1},
};

// ---- the streamed level search: saved searches, both ends, thinned levels

// The 8-puzzle: a 3x3 tray of eight unit pieces, each with its own goal,
// so the goal is one arrangement. The start is the goal with the pieces
// slid round the hole eight times.
inline SlideFixture eightPuzzle() {
  const int gx[8] = {0, 1, 2, 0, 1, 2, 0, 1};
  const int gy[8] = {0, 0, 0, 1, 1, 1, 2, 2};
  const int sx[8] = {0, 0, 1, 1, 1, 2, 0, 2};
  const int sy[8] = {1, 0, 0, 1, 2, 0, 2, 1};
  std::vector<SlideBlock> blocks;
  for (int i = 0; i < 8; i++) {
    blocks.push_back({1, 1, sx[i], sy[i], gx[i], gy[i]});
  }
  return slideFixture(3, 3, blocks);
}

// The places of every piece along a path, for comparing paths.
inline std::vector<int> placesAlong(const Separation& path) {
  std::vector<int> places;
  for (unsigned int s = 0; s <= path.getMoves(); s++) {
    for (unsigned int i = 0; i < path.getPieceNumber(); i++) {
      places.push_back(path.getState(s).getX(i));
      places.push_back(path.getState(s).getY(i));
    }
  }
  return places;
}

// Loose stops (decision 0031): a board of 3x3 blocks on a grid of 3, some
// of them cups, with small pieces in their pockets and in the empty cells.
// Every piece has a start; `places` says where the pieces with a goal must
// end.
struct GridBoard {
  Puzzle puz{GridType(GridKind::Sliding)};
  std::unique_ptr<Assembly> start;
  struct Place {
    int x;
    int y;
    int goalX;  // negative: it may end anywhere
    int goalY;
    bool block;
  };
  std::vector<Place> places;
  // What it was made of (gridBoardOf), for a board like it with other goals.
  int W = 0;
  int H = 0;
  std::vector<std::vector<std::pair<int, int>>> shapes;
  // For each piece the first of the copies it is one of: itself, but for
  // the copies of a piece.
  std::vector<std::size_t> copyOf;
  [[nodiscard]] const Problem& pr() const { return puz.getProblem(0); }
};

using BoardCells = std::vector<std::pair<int, int>>;

// A 3x3 block without the cells of `hole`.
inline BoardCells blockWithout(const BoardCells& hole) {
  BoardCells cells;
  for (int cy = 0; cy < 3; cy++) {
    for (int cx = 0; cx < 3; cx++) {
      if (std::find(hole.begin(), hole.end(), std::pair<int, int>{cx, cy}) == hole.end()) {
        cells.emplace_back(cx, cy);
      }
    }
  }
  return cells;
}

// The board W x H of pieces of these cells, each at its place. With
// `copies` the whole blocks are the copies of one piece and the unit pieces
// of another, each kind after the first of it: its starts and its goals are
// any copy's.
inline GridBoard gridBoardOf(int W, int H, const std::vector<BoardCells>& shapes,
                      const std::vector<GridBoard::Place>& places, bool copies = false) {
  GridBoard g;
  const unsigned int trayId =
      addStartGoalShape(g.puz, static_cast<unsigned int>(W), static_cast<unsigned int>(H));
  g.start = std::make_unique<Assembly>(g.puz.getGridType());
  g.W = W;
  g.H = H;
  std::vector<char> taken(shapes.size(), 0);
  for (std::size_t i = 0; i < shapes.size(); i++) {
    if (taken[i] != 0) {
      continue;
    }
    int w = 3;
    int h = 3;
    for (const std::pair<int, int> c : shapes[i]) {
      w = std::max(w, c.first + 1);
      h = std::max(h, c.second + 1);
    }
    const unsigned int id =
        g.puz.addShape(static_cast<unsigned int>(w), static_cast<unsigned int>(h), 1);
    for (const std::pair<int, int> c : shapes[i]) {
      g.puz.getShape(id).setState(static_cast<unsigned int>(c.first),
                                  static_cast<unsigned int>(c.second), 0, kFilled);
    }
    const bool alike = copies && (shapes[i].size() == 1 || shapes[i].size() == 9);
    const std::size_t first = g.places.size();
    for (std::size_t j = i; j < shapes.size(); j++) {
      if (taken[j] != 0 || (j != i && !(alike && shapes[j] == shapes[i]))) {
        continue;
      }
      taken[j] = 1;
      const GridBoard::Place& at = places[j];
      for (const std::pair<int, int> c : shapes[j]) {
        Voxel& tray = g.puz.getShape(trayId);
        tray.setStartPiece(static_cast<unsigned int>(at.x + c.first),
                           static_cast<unsigned int>(at.y + c.second), 0, id + 1);
        if (at.goalX >= 0) {
          tray.setGoalPiece(tray.getIndex(static_cast<unsigned int>(at.goalX + c.first),
                                          static_cast<unsigned int>(at.goalY + c.second), 0),
                            id + 1);
        }
      }
      g.start->addPlacement(0, at.x, at.y, 0);
      g.places.push_back(at);
      g.shapes.push_back(shapes[j]);
      g.copyOf.push_back(first);
    }
  }
  syncSlidingProblems(g.puz);
  return g;
}

// A 9x6 board, three cells by two. U is a cup open at the top and L one
// open to the left; p, q and r are unit pieces, which stop at three places
// in a cell of the grid (where a pocket of U or L would hold them):
//   .p. .q. .r.
//   ... ... ...
//   ... ... ...
//   U.U ... LLL
//   U.U ... ..L
//   UUU ... LLL
// U must end top right with p in its pocket and L bottom left with q in
// its own; r may end anywhere. With `reachable` off U's goal takes in a
// cell where no piece may stop, so the search goes through everything
// (a smaller everything without r, `units` 2).
inline GridBoard cupsAndUnits(bool reachable, int units = 3) {
  const BoardCells unit = {{0, 0}};
  std::vector<BoardCells> shapes = {blockWithout({{1, 0}, {1, 1}}), blockWithout({{0, 1}, {1, 1}}),
                                    unit, unit};
  std::vector<GridBoard::Place> places = {
      {0, 3, 6, 0, true}, {6, 3, 0, 3, true}, {1, 0, 7, 1, false}, {4, 0, 1, 4, false}};
  if (units > 2) {
    shapes.push_back(unit);
    places.push_back({7, 0, -1, -1, false});
  }
  GridBoard g = gridBoardOf(9, 6, shapes, places);
  if (!reachable) {
    g.puz.getShape(0).setState(8, 2, 0, kVariable);
    syncSlidingProblems(g.puz);
  }
  return g;
}

// A board made from `seed`: blocks of several kinds in cells of the grid,
// one to three small pieces anywhere they fit, and goals for some of the
// pieces from a second layout made the same way.
inline GridBoard gridBoard(std::uint64_t seed) {
  std::uint64_t x = seed * 0x9e3779b97f4a7c15ULL + 88172645463325252ULL;
  auto rnd = [&x](int n) {
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return static_cast<int>((x >> 11) % static_cast<std::uint64_t>(n));
  };
  // the cells a block leaves empty: none, the cups open to each side, a
  // ring, a corner cut out, a notch
  const std::vector<BoardCells> holes = {
      {},
      {{1, 0}, {1, 1}},
      {{0, 1}, {1, 1}},
      {{1, 1}, {2, 1}},
      {{1, 1}, {1, 2}},
      {{1, 1}},
      {{0, 0}, {1, 0}, {0, 1}, {1, 1}},
      {{1, 0}},
  };
  const std::vector<BoardCells> smalls = {{{0, 0}}, {{0, 0}, {1, 0}}, {{0, 0}, {0, 1}}};

  const int cw = 2 + rnd(2);
  const int ch = cw == 3 ? 2 + rnd(2) : 2;
  const int W = 3 * cw;
  const int H = 3 * ch;
  const int blocks = cw * ch - 1 - rnd(cw * ch > 4 ? 3 : 2);
  const int small = 1 + rnd(3);

  std::vector<BoardCells> shapes;
  for (int b = 0; b < blocks; b++) {
    shapes.push_back(blockWithout(holes[static_cast<std::size_t>(rnd(2) == 0 ? rnd(5) : rnd(8))]));
  }
  for (int s = 0; s < small; s++) {
    shapes.push_back(smalls[static_cast<std::size_t>(rnd(2) == 0 ? 0 : rnd(3))]);
  }

  // a layout: the blocks in cells of the grid, then the small pieces where
  // they fit (negative: nowhere)
  auto layout = [&](std::vector<std::pair<int, int>>& at) {
    std::vector<int> cell(static_cast<std::size_t>(cw * ch));
    for (std::size_t i = 0; i < cell.size(); i++) {
      cell[i] = static_cast<int>(i);
    }
    for (std::size_t i = cell.size(); i > 1; i--) {
      std::swap(cell[i - 1], cell[static_cast<std::size_t>(rnd(static_cast<int>(i)))]);
    }
    std::vector<int> occ(static_cast<std::size_t>(W * H), 0);
    at.assign(shapes.size(), {-1, -1});
    for (std::size_t i = 0; i < shapes.size(); i++) {
      for (int tries = 0; tries < 50 && at[i].first < 0; tries++) {
        const bool block = i < static_cast<std::size_t>(blocks);
        const int px = block ? 3 * (cell[i] % cw) : rnd(W);
        const int py = block ? 3 * (cell[i] / cw) : rnd(H);
        bool fits = true;
        for (const std::pair<int, int> c : shapes[i]) {
          if (px + c.first >= W || py + c.second >= H ||
              occ[static_cast<std::size_t>((py + c.second) * W + px + c.first)] != 0) {
            fits = false;
          }
        }
        if (!fits) {
          continue;
        }
        for (const std::pair<int, int> c : shapes[i]) {
          occ[static_cast<std::size_t>((py + c.second) * W + px + c.first)] = 1;
        }
        at[i] = {px, py};
      }
    }
  };
  std::vector<std::pair<int, int>> from;
  std::vector<std::pair<int, int>> to;
  layout(from);
  layout(to);

  std::vector<BoardCells> placed;
  std::vector<GridBoard::Place> places;
  for (std::size_t i = 0; i < shapes.size(); i++) {
    if (from[i].first < 0) {
      continue;
    }
    // the first piece always has a goal, the others half the time
    const bool goal = to[i].first >= 0 && (places.empty() || rnd(2) == 0);
    placed.push_back(shapes[i]);
    places.push_back({from[i].first, from[i].second, goal ? to[i].first : -1,
                      goal ? to[i].second : -1, i < static_cast<std::size_t>(blocks)});
  }
  return gridBoardOf(W, H, placed, places);
}

// A path of the board: from its start to its goals, every step a slide,
// the blocks stopping only on the grid's lines (not when the board was
// searched without the grid).
inline void checkGridPath(const GridBoard& g, const Separation& path, bool onGrid = true) {
  REQUIRE(path.getPieceNumber() == g.places.size());
  const State& first = path.getState(0);
  const State& last = path.getState(path.getMoves());
  // the copies of a piece take its starts and its goals in any order
  for (unsigned int i = 0; i < g.places.size(); i++) {
    if (g.copyOf[i] != i) {
      continue;
    }
    std::vector<std::pair<int, int>> starts;
    std::vector<std::pair<int, int>> began;
    std::vector<std::pair<int, int>> goals;
    std::vector<std::pair<int, int>> ended;
    for (unsigned int j = i; j < g.places.size() && g.copyOf[j] == i; j++) {
      const GridBoard::Place& p = g.places[j];
      starts.emplace_back(p.x, p.y);
      began.emplace_back(first.getX(j), first.getY(j));
      ended.emplace_back(last.getX(j), last.getY(j));
      if (p.goalX >= 0) {
        goals.emplace_back(p.goalX, p.goalY);
      }
    }
    std::sort(starts.begin(), starts.end());
    std::sort(began.begin(), began.end());
    std::sort(goals.begin(), goals.end());
    std::sort(ended.begin(), ended.end());
    CHECK(began == starts);
    if (!goals.empty()) {
      CHECK(ended == goals);
    }
  }
  for (unsigned int step = 0; step < path.getMoves(); step++) {
    INFO("step " << step);
    std::vector<unsigned int> movers;
    CHECK(slideRoute(g.puz, g.pr(), path, step, &movers).size() >= 2);
    for (unsigned int i = 0; i < g.places.size(); i++) {
      if (onGrid && g.places[i].block) {
        CHECK(path.getState(step + 1).getX(i) % 3 == 0);
        CHECK(path.getState(step + 1).getY(i) % 3 == 0);
      }
    }
  }
}

// What a search's levels came to: "start level 3: 120 arrangements" each.
struct LevelSizes {
  std::vector<std::string> lines;
  void watch(SlideSearch& s) {
    s.onLevel = [this](const std::string& line) {
      lines.push_back(line.substr(0, line.find(", one-way")));
    };
  }
  // Whether the search went from the goal as well.
  [[nodiscard]] bool fromGoal() const {
    return std::any_of(lines.begin(), lines.end(),
                       [](const std::string& line) { return line.starts_with("goal"); });
  }
};

// The board with every piece's goal where a path left it: a goal that is
// one arrangement, and one the board comes to.
inline GridBoard endAsGoal(const GridBoard& g, const Separation& path) {
  std::vector<GridBoard::Place> places = g.places;
  const State& last = path.getState(path.getMoves());
  for (unsigned int i = 0; i < places.size(); i++) {
    places[i].goalX = last.getX(i);
    places[i].goalY = last.getY(i);
  }
  return gridBoardOf(g.W, g.H, g.shapes, places);
}

// The board of `seed` with its goal made one arrangement: where the fewest
// moves from the start alone leave every piece.
inline GridBoard settledBoard(std::uint64_t seed) {
  const GridBoard g = gridBoard(seed);
  SlideSearch one;
  one.nested = true;
  one.grid = 3;
  one.oneEnd = true;
  one.maxMemoryStates = 30000;
  const std::optional<Separation> path = findSlidePath(g.puz, g.pr(), *g.start, one);
  REQUIRE(path.has_value());
  return endAsGoal(g, *path);
}

// Three blocks in the four cells of a 6x6 board, the first a cup with a
// unit piece in its pocket. The goal has the cup and the block beside it
// swapped: no sliding does that.
inline GridBoard swappedBlocks() {
  return gridBoardOf(
      6, 6, {blockWithout({{1, 0}, {1, 1}}), blockWithout({}), blockWithout({}), {{0, 0}}},
      {{0, 0, 3, 0, true}, {3, 0, 0, 0, true}, {0, 3, 0, 3, true}, {1, 1, 4, 1, false}});
}

// The board searched, and whether the search went from the goal as well.
inline std::optional<Separation> searched(const GridBoard& g, SlideSearch& s, bool& fromGoal) {
  LevelSizes levels;
  levels.watch(s);
  std::optional<Separation> path = findSlidePath(g.puz, g.pr(), *g.start, s);
  s.onLevel = nullptr;
  fromGoal = levels.fromGoal();
  return path;
}

// A search with nested slides (grid 0: every position a stop).
inline SlideSearch nestedSearch(int grid) {
  SlideSearch s;
  s.nested = true;
  s.grid = grid;
  return s;
}

}  // namespace bt::test

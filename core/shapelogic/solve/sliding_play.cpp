// SPDX-License-Identifier: GPL-3.0-or-later
// Playing a sliding puzzle by hand (sliding_play.h, decision 0039): the
// slide search's own pieces, free cells, reach, routes and goal check
// (slide_board.h), asked of one position at a time. Moved here from
// sliding_search.cpp (cleanup item 21.1).
#include "shapelogic/solve/sliding_play.h"

#include "shapelogic/solve/slide_board.h"

#include <algorithm>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace bt::sliding {

using namespace detail;

// Playing by hand (sliding_play.h): the search's own pieces, free cells,
// reach, routes and goal check, asked of one position at a time.

namespace {

// What the play queries work on: the problem's pieces as drawn, at the
// places a layout gives. Filled in place and never moved: a GoalCheck looks
// at the pieces through `pieces`.
struct PlayBoard {
  PlayBoard() = default;
  PlayBoard(const PlayBoard&) = delete;
  PlayBoard& operator=(const PlayBoard&) = delete;

  const Voxel* tray = nullptr;
  std::vector<const Voxel*> pieces;
  std::vector<Cell3List> cells;
  // with nested slides only
  std::vector<Cell3List> outlines;
  SlideState state;
  bool hasGoal = false;

  // Empty, or why `places` is no position of the problem.
  std::string place(const Puzzle& puzzle, const Problem& problem,
                    std::span<const TrayCell> places, bool nested) {
    if (std::string why = layoutRefusal(puzzle, problem, false, places); !why.empty()) {
      return why;
    }
    tray = &problem.resultShape(puzzle);
    hasGoal = problem.slideMarks().any(true);
    const std::vector<LayoutPiece> list = layoutPieces(problem);
    for (std::size_t i = 0; i < list.size(); i++) {
      const Voxel& v = problem.partShape(puzzle, list[i].part);
      pieces.push_back(&v);
      cells.push_back(cellsOf(v));
      outlines.push_back(nested ? outlineOf(v) : Cell3List());
      // the hotspot's cell: the lowest corner less the lowest cell's offset
      Cell3 low = cells.back().front();
      for (const Cell3& c : cells.back()) {
        low = {std::min(low.x, c.x), std::min(low.y, c.y), std::min(low.z, c.z)};
      }
      state.places.push_back(
          Place{places[i][0] - low.x, places[i][1] - low.y, 0, places[i][2] - low.z});
    }
    return std::string();
  }
};

// The position is the goal: never without goals, which every position
// would meet.
bool playSolved(const PlayBoard& b, const GoalCheck& goals, const SlideState& s) {
  return b.hasGoal && goals.all(s);
}

// The groups piece `pi` may move as, as the search has them (legacySearch):
// alone; with nested slides also with what is nested in it; with partial
// nesting also the wider group.
std::vector<std::vector<unsigned int>> playMovers(const PlayBoard& b, const SlideState& s,
                                                  unsigned int pi, const PlayRules& rules) {
  std::vector<std::vector<unsigned int>> sets{{pi}};
  if (rules.nested || rules.partialNested) {
    std::vector<unsigned int> group = nestedGroup(b.cells, b.outlines, s, pi, false);
    const std::size_t whole = group.size();
    if (whole > 1) {
      sets.push_back(std::move(group));
    }
    if (rules.partialNested) {
      std::vector<unsigned int> wider = nestedGroup(b.cells, b.outlines, s, pi, true);
      if (wider.size() > whole) {
        sets.push_back(std::move(wider));
      }
    }
  }
  return sets;
}

// The position with the movers shifted.
SlideState shifted(const SlideState& s, const std::vector<unsigned int>& movers,
                   const SlideShift& by) {
  SlideState next = s;
  for (const unsigned int m : movers) {
    next.places[m].x += by[0];
    next.places[m].y += by[1];
    next.places[m].z += by[2];
  }
  return next;
}

// Whether the pieces (sorted) are a group that moves as one: a group of one
// of them.
bool movesAsOne(const PlayBoard& b, const SlideState& s, const std::vector<unsigned int>& sorted,
                const PlayRules& rules) {
  for (const unsigned int outer : sorted) {
    for (std::vector<unsigned int> set : playMovers(b, s, outer, rules)) {
      std::ranges::sort(set);
      if (set == sorted) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace

PlayStart playStart(const Puzzle& puzzle, const Problem& problem) {
  PlayStart out;
  const std::optional<std::vector<TrayCell>> at = startLayout(puzzle, problem, &out.error);
  if (!at) {
    return out;
  }
  PlayBoard b;
  if (std::string why = b.place(puzzle, problem, *at, false); !why.empty()) {
    out.error = std::move(why);
    return out;
  }
  const std::vector<LayoutPiece> list = layoutPieces(problem);
  for (std::size_t i = 0; i < list.size(); i++) {
    // the lowest corner in the shape's own grid: its cells are from the hotspot
    const Voxel& v = *b.pieces[i];
    Cell3 low = b.cells[i].front();
    for (const Cell3& c : b.cells[i]) {
      low = {std::min(low.x, c.x), std::min(low.y, c.y), std::min(low.z, c.z)};
    }
    out.pieces.push_back({list[i].shapeId,
                          (*at)[i],
                          layoutCells(v, TrayCell{0, 0, 0}),
                          TrayCell{low.x + v.getHx(), low.y + v.getHy(), low.z + v.getHz()}});
  }
  const GoalCheck goals(puzzle, problem, b.pieces);
  out.hasGoal = b.hasGoal;
  out.solved = playSolved(b, goals, b.state);
  return out;
}

Result<std::vector<PlayGroup>> playReach(const Puzzle& puzzle, const Problem& problem,
                                         std::span<const TrayCell> places, unsigned int piece,
                                         const PlayRules& rules) {
  PlayBoard b;
  if (std::string why = b.place(puzzle, problem, places, rules.nested || rules.partialNested);
      !why.empty()) {
    return fail(Errc::InvalidArgument, std::move(why));
  }
  if (piece >= b.pieces.size()) {
    return fail(Errc::InvalidArgument, "The problem has no piece " + std::to_string(piece) + ".");
  }
  const GoalCheck goals(puzzle, problem, b.pieces);
  std::vector<std::vector<unsigned int>> sets = playMovers(b, b.state, piece, rules);
  // a piece nested in another may also move it: pushed into the walls it
  // sits in, it takes them along. The move is the outer piece's (its group,
  // the outer piece first); the innermost of them first, each set once
  if (rules.nested || rules.partialNested) {
    std::vector<std::vector<unsigned int>> seen;
    for (std::vector<unsigned int> set : sets) {
      std::ranges::sort(set);
      seen.push_back(std::move(set));
    }
    std::vector<std::vector<unsigned int>> outer;
    for (unsigned int q = 0; q < b.pieces.size(); q++) {
      if (q == piece) {
        continue;
      }
      for (std::vector<unsigned int>& set : playMovers(b, b.state, q, rules)) {
        std::vector<unsigned int> sorted = set;
        std::ranges::sort(sorted);
        if (std::ranges::binary_search(sorted, piece) &&
            std::ranges::find(seen, sorted) == seen.end()) {
          seen.push_back(std::move(sorted));
          outer.push_back(std::move(set));
        }
      }
    }
    std::ranges::stable_sort(outer, {}, &std::vector<unsigned int>::size);
    for (std::vector<unsigned int>& set : outer) {
      sets.push_back(std::move(set));
    }
  }
  std::vector<PlayGroup> out;
  for (std::vector<unsigned int>& movers : sets) {
    PlayGroup g;
    for (const SlideShift& by :
         reachableShifts(*b.tray, b.cells, b.state, movers, rules.verticalMoves)) {
      g.stops.push_back({by, playSolved(b, goals, shifted(b.state, movers, by))});
    }
    g.movers = std::move(movers);
    out.push_back(std::move(g));
  }
  return out;
}

Result<PlayReplay> playReplay(const Puzzle& puzzle, const Problem& problem,
                              std::span<const TrayCell> places, std::span<const PlayMove> moves,
                              const PlayRules& rules) {
  PlayBoard b;
  if (std::string why = b.place(puzzle, problem, places, rules.nested || rules.partialNested);
      !why.empty()) {
    return fail(Errc::InvalidArgument, std::move(why));
  }
  const GoalCheck goals(puzzle, problem, b.pieces);
  PlayReplay out;
  SlideState s = b.state;
  out.solved = playSolved(b, goals, s);
  for (const PlayMove& m : moves) {
    std::vector<unsigned int> sorted = m.movers;
    std::ranges::sort(sorted);
    ShiftList route;
    if (sorted.empty() || sorted.back() >= b.pieces.size() ||
        std::ranges::adjacent_find(sorted) != sorted.end()) {
      out.error = "The move names pieces the problem does not have.";
    } else if (m.by == SlideShift{0, 0, 0}) {
      out.error = "The move goes nowhere.";
    } else if (!movesAsOne(b, s, sorted, rules)) {
      out.error = "Those pieces do not move as one.";
    } else {
      // in its layer when it can be, as a solution's route is drawn
      if (m.by[2] == 0) {
        route = fewestTurns(*b.tray, b.cells, s, sorted, m.by, false);
      }
      if (route.empty() && rules.verticalMoves) {
        route = fewestTurns(*b.tray, b.cells, s, sorted, m.by, true);
      }
      if (route.empty()) {
        out.error = sorted.size() == 1 ? "The piece cannot get there."
                                       : "The pieces cannot get there.";
      }
    }
    if (!out.error.empty()) {
      break;
    }
    s = shifted(s, sorted, m.by);
    out.steps.push_back({std::move(route), playSolved(b, goals, s)});
  }
  return out;
}

}  // namespace bt::sliding

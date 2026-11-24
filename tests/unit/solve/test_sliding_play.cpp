// SPDX-License-Identifier: GPL-3.0-or-later
// A sliding puzzle played by hand (sliding_play.h, and the layouts of
// model/sliding.h it stands on): where a game starts, where one move can
// take a piece, a list of moves made and its routes, the goal as the search
// judges it, and a position made the problem's starts or goals. The rules
// are the search's, so its own paths are replayed as the check.
#include "../model/model_test_helpers.h"

#include "shapelogic/grid/grid_type.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/model/assembly.h"
#include "shapelogic/model/disassembly.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/model/slide_marks.h"
#include "shapelogic/model/sliding.h"
#include "shapelogic/solve/sliding_play.h"
#include "shapelogic/solve/sliding_search.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace bt;
using namespace bt::sliding;

namespace {

constexpr int kFilled = Voxel::kFilled;
constexpr int kVariable = Voxel::kVariable;
constexpr int kEmpty = Voxel::kEmpty;

// A piece of a board built from rectangles: w by h, a shape of its own,
// its lowest corner at (x, y); (goalX, goalY) is where it must end, or
// negative when it may end anywhere.
struct Block {
  unsigned int w;
  unsigned int h;
  int x;
  int y;
  int goalX;
  int goalY;
};

struct Game {
  Puzzle puz{GridType(GridKind::Sliding)};
  [[nodiscard]] const Problem& pr() const { return puz.getProblem(0); }
  [[nodiscard]] Problem& pr() { return puz.getProblem(0); }
  [[nodiscard]] Voxel& board() { return puz.getShape(0); }
};

// A board of w by h cells, all floor, with the blocks at their starts.
Game game(unsigned int w, unsigned int h, const std::vector<Block>& blocks) {
  Game g;
  const unsigned int trayId = addStartGoalShape(g.puz, w, h);
  for (const Block& b : blocks) {
    const unsigned int id = g.puz.addShape(b.w, b.h, 1);
    for (unsigned int y = 0; y < b.h; y++) {
      for (unsigned int x = 0; x < b.w; x++) {
        g.puz.getShape(id).setState(x, y, 0, kFilled);
        Voxel& tray = g.puz.getShape(trayId);
        tray.setStartPiece(static_cast<unsigned int>(b.x) + x, static_cast<unsigned int>(b.y) + y,
                           0, id + 1);
        if (b.goalX >= 0) {
          tray.setGoalPiece(tray.getIndex(static_cast<unsigned int>(b.goalX) + x,
                                          static_cast<unsigned int>(b.goalY) + y, 0),
                            id + 1);
        }
      }
    }
  }
  syncSlidingProblems(g.puz);
  return g;
}

// A unit piece with `copies` copies in problem 0, its starts and goals at
// the cells given (y = 0).
unsigned int unitCopies(Game& g, unsigned int copies, const std::vector<int>& starts,
                        const std::vector<int>& goals) {
  const unsigned int id = g.puz.addShape(1, 1, 1);
  g.puz.getShape(id).setState(0, 0, 0, kFilled);
  g.pr().setShapeMaximum(g.puz, id, copies);
  for (const bool goal : {false, true}) {
    for (const int x : goal ? goals : starts) {
      REQUIRE(stampPiece(&g.board(), &g.pr().editSlideMarks(), &g.puz.getShape(id), id, x, 0, 0,
                         goal, copies)
                  .empty());
    }
  }
  return id;
}

// The start layout, which the problem must have.
std::vector<TrayCell> startOf(const Game& g) {
  std::string why;
  const std::optional<std::vector<TrayCell>> at = startLayout(g.puz, g.pr(), &why);
  INFO(why);
  REQUIRE(at.has_value());
  return *at;
}

// The layout as an assembly, for the search: each hotspot's cell.
Assembly assemblyOf(const Puzzle& puz, const Problem& pr, const std::vector<TrayCell>& at) {
  Assembly a(puz.getGridType());
  const std::vector<LayoutPiece> pieces = layoutPieces(pr);
  for (std::size_t i = 0; i < pieces.size(); i++) {
    const Voxel& v = puz.getShape(pieces[i].shapeId);
    // the lowest corner of the piece in its own grid
    const TrayCell low = layoutCells(v, {0, 0, 0}).empty() ? TrayCell{0, 0, 0} : [&v] {
      TrayCell m{static_cast<int>(v.getX()), static_cast<int>(v.getY()),
                 static_cast<int>(v.getZ())};
      for (unsigned int z = 0; z < v.getZ(); z++) {
        for (unsigned int y = 0; y < v.getY(); y++) {
          for (unsigned int x = 0; x < v.getX(); x++) {
            if (v.getState(x, y, z) != kEmpty) {
              m = {std::min(m[0], static_cast<int>(x)), std::min(m[1], static_cast<int>(y)),
                   std::min(m[2], static_cast<int>(z))};
            }
          }
        }
      }
      return m;
    }();
    a.addPlacement(0, at[i][0] - low[0] + v.getHx(), at[i][1] - low[1] + v.getHy(),
                   at[i][2] - low[2] + v.getHz());
  }
  return a;
}

// A search's path as the moves of a game: each step's movers and shift.
std::vector<PlayMove> movesOf(const Separation& path) {
  std::vector<PlayMove> moves;
  for (unsigned int step = 0; step < path.getMoves(); step++) {
    const State& a = path.getState(step);
    const State& b = path.getState(step + 1);
    PlayMove m;
    for (unsigned int i = 0; i < path.getPieceNumber(); i++) {
      const SlideShift d{b.getX(i) - a.getX(i), b.getY(i) - a.getY(i), b.getZ(i) - a.getZ(i)};
      if (d != SlideShift{0, 0, 0}) {
        m.movers.push_back(i);
        m.by = d;
      }
    }
    moves.push_back(std::move(m));
  }
  return moves;
}

// The shifts a group can reach, as a set.
std::set<SlideShift> shifts(const PlayGroup& g) {
  std::set<SlideShift> out;
  for (const PlayStop& s : g.stops) {
    out.insert(s.by);
  }
  return out;
}

// The stops of a group that give the goal.
std::set<SlideShift> solvedStops(const PlayGroup& g) {
  std::set<SlideShift> out;
  for (const PlayStop& s : g.stops) {
    if (s.solved) {
      out.insert(s.by);
    }
  }
  return out;
}

// Klotski, as test_sliding.cpp has it: the 2x2 block to the bottom center.
const std::vector<Block> kKlotski = {
    {2, 2, 1, 0, 1, 3},   {1, 2, 0, 0, -1, -1}, {1, 2, 3, 0, -1, -1}, {1, 2, 0, 2, -1, -1},
    {1, 2, 3, 2, -1, -1}, {2, 1, 1, 2, -1, -1}, {1, 1, 1, 3, -1, -1}, {1, 1, 2, 3, -1, -1},
    {1, 1, 0, 4, -1, -1}, {1, 1, 3, 4, -1, -1},
};

}  // namespace

TEST_CASE("sliding play: a game starts where the starts put the pieces", "[sliding][play]") {
  // the 81-move puzzle's file: ten pieces, the 2x2 block's goal
  const Puzzle puz = test::loadFixture("test_sliding_solver.xmpuzzle");
  const Problem& pr = puz.getProblem(0);
  const PlayStart start = playStart(puz, pr);
  INFO(start.error);
  REQUIRE(start.error.empty());
  REQUIRE(start.pieces.size() == 10);
  CHECK(start.hasGoal);
  CHECK_FALSE(start.solved);

  // each piece covers its own start marks, and together all of them
  const Voxel& tray = pr.resultShape(puz);
  const SlideMarks& marks = pr.slideMarks();
  const std::vector<LayoutPiece> list = layoutPieces(pr);
  REQUIRE(list.size() == start.pieces.size());
  std::size_t covered = 0;
  for (std::size_t i = 0; i < start.pieces.size(); i++) {
    const PlayPiece& p = start.pieces[i];
    CHECK(p.shapeId == list[i].shapeId);
    REQUIRE_FALSE(p.cells.empty());
    for (const TrayCell& c : p.cells) {
      // from the lowest corner: no cell before it
      CHECK((c[0] >= 0 && c[1] >= 0 && c[2] >= 0));
      const unsigned int at =
          tray.getIndex(static_cast<unsigned int>(p.at[0] + c[0]),
                        static_cast<unsigned int>(p.at[1] + c[1]),
                        static_cast<unsigned int>(p.at[2] + c[2]));
      CHECK(marks.start(at) == p.shapeId + 1);
      covered++;
    }
    CHECK(layoutCells(puz.getShape(p.shapeId), p.at).size() == p.cells.size());
    // the corner in the shape's own grid: its cells there are the piece's
    const Voxel& shape = puz.getShape(p.shapeId);
    for (const TrayCell& c : p.cells) {
      CHECK(shape.getState(static_cast<unsigned int>(p.origin[0] + c[0]),
                           static_cast<unsigned int>(p.origin[1] + c[1]),
                           static_cast<unsigned int>(p.origin[2] + c[2])) != kEmpty);
    }
  }
  std::size_t marked = 0;
  for (unsigned int i = 0; i < tray.getXYZ(); i++) {
    marked += marks.start(i) != 0 ? 1 : 0;
  }
  CHECK(covered == marked);
}

TEST_CASE("sliding play: a turn is one move, and its route has the corner", "[sliding][play]") {
  // a unit in the corner of a 2x2 board, its goal the opposite corner
  const Game g = game(2, 2, {{1, 1, 0, 0, 1, 1}});
  const std::vector<TrayCell> at = startOf(g);
  CHECK(at == std::vector<TrayCell>{{0, 0, 0}});

  const auto reach = playReach(g.puz, g.pr(), at, 0, {});
  REQUIRE(reach.has_value());
  REQUIRE(reach->size() == 1);
  const PlayGroup& alone = reach->front();
  CHECK(alone.movers == std::vector<unsigned int>{0});
  CHECK(shifts(alone) == std::set<SlideShift>{{1, 0, 0}, {0, 1, 0}, {1, 1, 0}});
  CHECK(solvedStops(alone) == std::set<SlideShift>{{1, 1, 0}});
  // nearest first: the corner's far side is last
  CHECK(alone.stops.back().by == SlideShift{1, 1, 0});

  const std::vector<PlayMove> moves{{{0}, {1, 1, 0}}};
  const auto replay = playReplay(g.puz, g.pr(), at, moves, {});
  REQUIRE(replay.has_value());
  CHECK(replay->error.empty());
  CHECK_FALSE(replay->solved);
  REQUIRE(replay->steps.size() == 1);
  const std::vector<SlideShift>& route = replay->steps[0].route;
  REQUIRE(route.size() == 3);
  CHECK(route.front() == SlideShift{0, 0, 0});
  CHECK(route.back() == SlideShift{1, 1, 0});
  CHECK(replay->steps[0].solved);

  // no moves: only whether the position is the goal
  const auto none = playReplay(g.puz, g.pr(), std::vector<TrayCell>{{1, 1, 0}}, {}, {});
  REQUIRE(none.has_value());
  CHECK(none->solved);
  CHECK(none->steps.empty());
  CHECK(none->error.empty());
}

TEST_CASE("sliding play: a piece cannot pass another", "[sliding][play]") {
  // two units side by side at the left of a row of three
  const Game g = game(3, 1, {{1, 1, 0, 0, 2, 0}, {1, 1, 1, 0, -1, -1}});
  const std::vector<TrayCell> at = startOf(g);

  const auto left = playReach(g.puz, g.pr(), at, 0, {});
  REQUIRE(left.has_value());
  REQUIRE(left->size() == 1);
  CHECK(left->front().stops.empty());
  const auto right = playReach(g.puz, g.pr(), at, 1, {});
  REQUIRE(right.has_value());
  CHECK(shifts(right->front()) == std::set<SlideShift>{{1, 0, 0}});

  // the second move cannot be made: the first is kept
  const std::vector<PlayMove> moves{{{1}, {1, 0, 0}}, {{0}, {2, 0, 0}}, {{0}, {1, 0, 0}}};
  const auto replay = playReplay(g.puz, g.pr(), at, moves, {});
  REQUIRE(replay.has_value());
  CHECK(replay->steps.size() == 1);
  CHECK(replay->error.find("cannot get there") != std::string::npos);

  // moves that are none
  for (const PlayMove& bad : std::vector<PlayMove>{
           {{0}, {0, 0, 0}}, {{}, {1, 0, 0}}, {{7}, {1, 0, 0}}, {{1, 1}, {1, 0, 0}},
           {{0, 1}, {1, 0, 0}}, {{1}, {0, 0, 1}}}) {
    const std::vector<PlayMove> one{bad};
    const auto r = playReplay(g.puz, g.pr(), at, one, {});
    REQUIRE(r.has_value());
    CHECK(r->steps.empty());
    CHECK_FALSE(r->error.empty());
  }
}

TEST_CASE("sliding play: the search's Klotski path is 81 moves of a game", "[sliding][play]") {
  Game g = game(4, 5, kKlotski);
  const std::vector<TrayCell> at = startOf(g);
  REQUIRE(at.size() == kKlotski.size());
  for (std::size_t i = 0; i < at.size(); i++) {
    CHECK(at[i] == TrayCell{kKlotski[i].x, kKlotski[i].y, 0});
  }
  const Assembly start = assemblyOf(g.puz, g.pr(), at);
  const std::optional<Separation> path = findSlidePath(g.puz, g.pr(), start);
  REQUIRE(path.has_value());
  REQUIRE(path->getMoves() == 81);

  const std::vector<PlayMove> moves = movesOf(*path);
  const auto replay = playReplay(g.puz, g.pr(), at, moves, {});
  REQUIRE(replay.has_value());
  INFO(replay->error);
  REQUIRE(replay->steps.size() == 81);
  CHECK(replay->error.empty());
  for (unsigned int k = 0; k < 81; k++) {
    INFO("move " << k);
    // only the last position is the goal, and the routes are a solution's
    CHECK(replay->steps[k].solved == (k == 80));
    CHECK(replay->steps[k].route == slideRoute(g.puz, g.pr(), *path, k, nullptr));
    CHECK(moves[k].movers.size() == 1);
  }

  // every move is one a piece's reach offers, with the same verdict
  std::vector<TrayCell> now = at;
  for (unsigned int k = 0; k < 81; k++) {
    const unsigned int piece = moves[k].movers.front();
    const auto reach = playReach(g.puz, g.pr(), now, piece, {});
    REQUIRE(reach.has_value());
    INFO("move " << k);
    CHECK(shifts(reach->front()).contains(moves[k].by));
    CHECK(solvedStops(reach->front()).contains(moves[k].by) == (k == 80));
    for (std::size_t a = 0; a < 3; a++) {
      now[piece][a] += moves[k].by[a];
    }
  }

  // a place is the piece's corner on the board: where the piece is drawn
  // in its grid, and its hotspot, play no part
  const auto before = playReach(g.puz, g.pr(), at, 0, {});
  REQUIRE(before.has_value());
  g.puz.getShape(1).setHotspot(1, 1, 0);
  CHECK(startOf(g) == at);
  const auto after = playReach(g.puz, g.pr(), at, 0, {});
  REQUIRE(after.has_value());
  CHECK(shifts(after->front()) == shifts(before->front()));
  const auto again = playReplay(g.puz, g.pr(), at, moves, {});
  REQUIRE(again.has_value());
  CHECK(again->steps.size() == 81);
}

TEST_CASE("sliding play: any copy of a piece fills any of its goals", "[sliding][play]") {
  // two copies of a unit at the left of a row of three, their goals at its right
  Game g = game(3, 1, {});
  unitCopies(g, 2, {0, 1}, {1, 2});
  const std::vector<TrayCell> at = startOf(g);
  CHECK(at == std::vector<TrayCell>{{0, 0, 0}, {1, 0, 0}});
  const PlayStart start = playStart(g.puz, g.pr());
  REQUIRE(start.error.empty());
  CHECK(start.pieces.size() == 2);
  CHECK_FALSE(start.solved);

  // the second copy takes the far goal, the first the one the second started on
  const std::vector<PlayMove> moves{{{1}, {1, 0, 0}}, {{0}, {1, 0, 0}}};
  const auto replay = playReplay(g.puz, g.pr(), at, moves, {});
  REQUIRE(replay.has_value());
  REQUIRE(replay->steps.size() == 2);
  CHECK_FALSE(replay->steps[0].solved);
  CHECK(replay->steps[1].solved);

  // one goal for two copies: one fills it, the other ends anywhere
  Game one = game(3, 1, {});
  unitCopies(one, 2, {0, 1}, {2});
  const std::vector<PlayMove> single{{{1}, {1, 0, 0}}};
  const auto partial = playReplay(one.puz, one.pr(), startOf(one), single, {});
  REQUIRE(partial.has_value());
  REQUIRE(partial->steps.size() == 1);
  CHECK(partial->steps[0].solved);
}

TEST_CASE("sliding play: a piece may rest on a corridor, where nothing is solved",
          "[sliding][play]") {
  // a row of four, its second cell a corridor: A on its goal at the right
  // end, B with no goal at the left
  Game g = game(4, 1, {{1, 1, 3, 0, 3, 0}, {1, 1, 0, 0, -1, -1}});
  g.board().setState(1, 0, 0, kVariable);
  const std::vector<TrayCell> at = startOf(g);
  const PlayStart start = playStart(g.puz, g.pr());
  REQUIRE(start.error.empty());
  CHECK(start.solved);

  const auto reach = playReach(g.puz, g.pr(), at, 1, {});
  REQUIRE(reach.has_value());
  // B crosses the corridor and may stop on it, but that is no goal
  CHECK(shifts(reach->front()) == std::set<SlideShift>{{1, 0, 0}, {2, 0, 0}});
  CHECK(solvedStops(reach->front()) == std::set<SlideShift>{{2, 0, 0}});

  const std::vector<PlayMove> moves{{{1}, {1, 0, 0}}, {{1}, {1, 0, 0}}};
  const auto replay = playReplay(g.puz, g.pr(), at, moves, {});
  REQUIRE(replay.has_value());
  CHECK(replay->solved);
  REQUIRE(replay->steps.size() == 2);
  CHECK_FALSE(replay->steps[0].solved);
  CHECK(replay->steps[1].solved);
}

TEST_CASE("sliding play: without goals nothing is solved", "[sliding][play]") {
  const Game g = game(3, 1, {{1, 1, 0, 0, -1, -1}});
  const PlayStart start = playStart(g.puz, g.pr());
  REQUIRE(start.error.empty());
  CHECK_FALSE(start.hasGoal);
  CHECK_FALSE(start.solved);
  const auto reach = playReach(g.puz, g.pr(), startOf(g), 0, {});
  REQUIRE(reach.has_value());
  CHECK(reach->front().stops.size() == 2);
  CHECK(solvedStops(reach->front()).empty());
}

TEST_CASE("sliding play: a piece carries what is nested in it, and moves what it is nested "
          "in, only with nested slides",
          "[sliding][play]") {
  // the Cave and the Smile in its pocket (test_sliding.cpp's nested puzzle)
  const Puzzle puz = test::loadFixture("sliding/test_sliding_nested.xmpuzzle");
  const Problem& pr = puz.getProblem(0);
  const PlayStart start = playStart(puz, pr);
  INFO(start.error);
  REQUIRE(start.error.empty());
  REQUIRE(start.pieces.size() == 6);
  std::vector<TrayCell> at;
  for (const PlayPiece& p : start.pieces) {
    at.push_back(p.at);
  }

  PlayRules nested;
  nested.nested = true;
  unsigned int pairs = 0;
  for (unsigned int piece = 0; piece < 6; piece++) {
    const auto alone = playReach(puz, pr, at, piece, {});
    REQUIRE(alone.has_value());
    CHECK(alone->size() == 1);
    const auto with = playReach(puz, pr, at, piece, nested);
    REQUIRE(with.has_value());
    CHECK(with->front().movers == std::vector<unsigned int>{piece});
    if (with->size() > 1) {
      pairs++;
      // the Cave carrying the Smile, also when the Smile is dragged: pushed
      // into the Cave's walls it takes the Cave along, the Cave's move
      CHECK(with->size() == 2);
      std::vector<unsigned int> group = (*with)[1].movers;
      CHECK(group.front() == 4);
      std::ranges::sort(group);
      CHECK(group == std::vector<unsigned int>{4, 5});
      const auto cave = playReach(puz, pr, at, 4, nested);
      REQUIRE(cave.has_value());
      REQUIRE(cave->size() == 2);
      CHECK((*with)[1].movers == (*cave)[1].movers);
      CHECK(shifts((*with)[1]) == shifts((*cave)[1]));
    }
  }
  CHECK(pairs == 2);

  // the search's path with nested slides is a game with them, and none without
  SlideSearch search;
  search.nested = true;
  const std::optional<Separation> path =
      findSlidePath(puz, pr, assemblyOf(puz, pr, at), search);
  REQUIRE(path.has_value());
  const std::vector<PlayMove> moves = movesOf(*path);
  const auto replay = playReplay(puz, pr, at, moves, nested);
  REQUIRE(replay.has_value());
  INFO(replay->error);
  REQUIRE(replay->steps.size() == moves.size());
  CHECK(replay->steps.back().solved);
  for (unsigned int k = 0; k < moves.size(); k++) {
    INFO("move " << k);
    CHECK(replay->steps[k].route == slideRoute(puz, pr, *path, k, nullptr));
  }
  const auto plain = playReplay(puz, pr, at, moves, {});
  REQUIRE(plain.has_value());
  CHECK(plain->steps.size() < moves.size());
  CHECK(plain->error.find("do not move as one") != std::string::npos);
  CHECK(moves[plain->steps.size()].movers.size() == 2);
}

TEST_CASE("sliding play: a piece stays in its layer unless vertical moves are allowed",
          "[sliding][play]") {
  // two units in a row of three, two layers deep: the left one's goal is
  // past the other
  Game g = game(3, 1, {{1, 1, 0, 0, 2, 0}, {1, 1, 1, 0, 1, 0}});
  g.board().resize(3, 1, 2, kFilled);
  const std::vector<TrayCell> at = startOf(g);
  CHECK(at == std::vector<TrayCell>{{0, 0, 0}, {1, 0, 0}});

  const auto flat = playReach(g.puz, g.pr(), at, 0, {});
  REQUIRE(flat.has_value());
  CHECK(flat->front().stops.empty());
  const std::vector<PlayMove> over{{{0}, {2, 0, 0}}};
  const auto refused = playReplay(g.puz, g.pr(), at, over, {});
  REQUIRE(refused.has_value());
  CHECK(refused->steps.empty());

  PlayRules vertical;
  vertical.verticalMoves = true;
  const auto lifted = playReach(g.puz, g.pr(), at, 0, vertical);
  REQUIRE(lifted.has_value());
  CHECK(shifts(lifted->front()) ==
        std::set<SlideShift>{{0, 0, 1}, {1, 0, 1}, {2, 0, 1}, {2, 0, 0}});
  CHECK(solvedStops(lifted->front()) == std::set<SlideShift>{{2, 0, 0}});
  const auto replay = playReplay(g.puz, g.pr(), at, over, vertical);
  REQUIRE(replay.has_value());
  REQUIRE(replay->steps.size() == 1);
  // up, across, down
  CHECK(replay->steps[0].route ==
        std::vector<SlideShift>{{0, 0, 0}, {0, 0, 1}, {2, 0, 1}, {2, 0, 0}});
  CHECK(replay->steps[0].solved);
}

TEST_CASE("sliding play: a problem whose starts place no layout is not played",
          "[sliding][play]") {
  // the older model, a tray painted in piece colors: no board
  const Puzzle old = test::loadFixture("sliding/slide_turn.xmpuzzle");
  const PlayStart painted = playStart(old, old.getProblem(0));
  CHECK(painted.error.find("no board") != std::string::npos);
  CHECK(painted.pieces.empty());
  CHECK_FALSE(painted.solved);
  // a file of the original's, its starts kept in the board's colors, is
  // converted as it loads and is played
  const Puzzle converted = test::loadFixture("sliding/slide_klotski.xmpuzzle");
  const PlayStart klotski = playStart(converted, converted.getProblem(0));
  CHECK(klotski.error.empty());
  CHECK(klotski.pieces.size() == 10);

  // no pieces
  const Game bare = game(3, 1, {});
  CHECK(playStart(bare.puz, bare.pr()).error.find("no pieces") != std::string::npos);

  // a copy without a start
  Game two = game(3, 1, {});
  const unsigned int unit = unitCopies(two, 2, {0}, {2});
  std::string why;
  CHECK_FALSE(startLayout(two.puz, two.pr(), &why).has_value());
  CHECK(why.find("1 start for its 2 copies") != std::string::npos);
  CHECK(playStart(two.puz, two.pr()).error == why);

  // no start at all
  REQUIRE(clearStamp(&two.pr().editSlideMarks(), unit, false));
  CHECK_FALSE(startLayout(two.puz, two.pr(), &why).has_value());
  CHECK(why.find("has no start") != std::string::npos);

  // a start that is not the whole piece
  Game part = game(3, 1, {{2, 1, 0, 0, -1, -1}});
  part.pr().editSlideMarks().set(1, false, 0);
  CHECK_FALSE(startLayout(part.puz, part.pr(), &why).has_value());
  CHECK(why.find("not the whole piece") != std::string::npos);
}

TEST_CASE("sliding play: places that are no position are refused", "[sliding][play]") {
  const Game g = game(3, 2, {{2, 1, 0, 0, -1, -1}, {1, 1, 0, 1, -1, -1}});
  const std::vector<TrayCell> at = startOf(g);
  REQUIRE(playReach(g.puz, g.pr(), at, 1, {}).has_value());

  const auto refusedWith = [&g](const std::vector<TrayCell>& places, const std::string& what) {
    const auto reach = playReach(g.puz, g.pr(), places, 0, {});
    REQUIRE_FALSE(reach.has_value());
    CHECK(reach.error().code == Errc::InvalidArgument);
    INFO(reach.error().message);
    CHECK(reach.error().message.find(what) != std::string::npos);
    const auto replay = playReplay(g.puz, g.pr(), places, {}, {});
    REQUIRE_FALSE(replay.has_value());
    CHECK(replay.error().code == Errc::InvalidArgument);
    CHECK_FALSE(layoutRefusal(g.puz, g.pr(), false, places).empty());
  };
  refusedWith({{0, 0, 0}}, "places 1 piece");
  refusedWith({{2, 0, 0}, {0, 1, 0}}, "off the floor");
  refusedWith({{0, 0, 0}, {0, 0, 1}}, "off the floor");
  refusedWith({{0, 0, 0}, {1, 0, 0}}, "cover another piece");
  refusedWith({{-1, 0, 0}, {0, 1, 0}}, "off the floor");

  // a wall is no floor
  Game walled = game(3, 2, {{2, 1, 0, 0, -1, -1}, {1, 1, 0, 1, -1, -1}});
  walled.board().setState(2, 1, 0, kEmpty);
  CHECK_FALSE(layoutRefusal(walled.puz, walled.pr(), false,
                            std::vector<TrayCell>{{0, 0, 0}, {2, 1, 0}})
                  .empty());

  // no such piece
  const auto none = playReach(g.puz, g.pr(), at, 2, {});
  REQUIRE_FALSE(none.has_value());
  CHECK(none.error().code == Errc::InvalidArgument);
}

TEST_CASE("sliding play: a position becomes the problem's starts or goals", "[sliding][play]") {
  Game g = game(4, 5, kKlotski);
  const std::vector<TrayCell> at = startOf(g);
  const SlideMarks before = g.pr().slideMarks();

  // the start as the goal: every piece's goal where it starts
  REQUIRE(setLayout(g.puz, g.pr(), true, at).empty());
  const SlideMarks& marks = g.pr().slideMarks();
  for (unsigned int i = 0; i < g.board().getXYZ(); i++) {
    CHECK(marks.goal(i) == marks.start(i));
  }
  CHECK(playStart(g.puz, g.pr()).solved);

  // another position as the start: read back as it was given
  std::vector<TrayCell> moved = at;
  moved[8] = {1, 4, 0};
  moved[9] = {2, 4, 0};
  CHECK(layoutMarks(g.puz, g.pr(), false, at) == g.pr().slideMarks());
  REQUIRE(setLayout(g.puz, g.pr(), false, moved).empty());
  CHECK(startOf(g) == moved);
  CHECK_FALSE(playStart(g.puz, g.pr()).solved);
  // the goals stayed
  for (unsigned int i = 0; i < g.board().getXYZ(); i++) {
    CHECK(g.pr().slideMarks().goal(i) == marks.goal(i));
  }

  // refused, and nothing changes
  const SlideMarks kept = g.pr().slideMarks();
  std::vector<TrayCell> bad = moved;
  bad[9] = {1, 4, 0};
  CHECK(setLayout(g.puz, g.pr(), false, bad).find("cover another piece") != std::string::npos);
  bad[9] = {4, 4, 0};
  CHECK(setLayout(g.puz, g.pr(), false, bad).find("off the floor") != std::string::npos);
  bad.pop_back();
  CHECK(setLayout(g.puz, g.pr(), true, bad).find("places 9 pieces") != std::string::npos);
  CHECK(g.pr().slideMarks() == kept);
  CHECK_FALSE(before == kept);

  // a goal cannot be on a corridor; a start can
  Game c = game(3, 1, {{1, 1, 0, 0, 2, 0}});
  c.board().setState(1, 0, 0, kVariable);
  const std::vector<TrayCell> onCorridor{{1, 0, 0}};
  CHECK(setLayout(c.puz, c.pr(), true, onCorridor).find("variable cell") != std::string::npos);
  CHECK(setLayout(c.puz, c.pr(), false, onCorridor).empty());
  CHECK(startOf(c) == onCorridor);

  // like copies side by side are read back as the pieces they were: two
  // flat dominoes one above the other, not two upright ones
  Game d = game(2, 2, {});
  const unsigned int domino = d.puz.addShape(2, 1, 1);
  d.puz.getShape(domino).setState(0, 0, 0, kFilled);
  d.puz.getShape(domino).setState(1, 0, 0, kFilled);
  d.pr().setShapeMaximum(d.puz, domino, 2);
  const std::vector<TrayCell> stacked{{0, 0, 0}, {0, 1, 0}};
  REQUIRE(setLayout(d.puz, d.pr(), false, stacked).empty());
  std::vector<TrayCell> read = startOf(d);
  std::ranges::sort(read);
  CHECK(read == stacked);
}

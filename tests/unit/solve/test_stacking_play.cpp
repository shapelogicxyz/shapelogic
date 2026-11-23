// SPDX-License-Identifier: GPL-3.0-or-later
// A stacking puzzle played by hand (bt/model/stacking_play.h, decision
// 0042): how a game starts, the transfers a position allows, a list of
// transfers made, and a position checked before it becomes a start or a
// goal. The rules are the Stacking Solver's, so every position a search
// can reach is walked here and the play calls compared with moveAllowed,
// and a path the solver found is played as a game.
#include "shapelogic/grid/grid_type.h"
#include "shapelogic/model/disassembly.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/model/stacking.h"
#include "shapelogic/model/stacking_play.h"
#include "shapelogic/solve/stack_search.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <queue>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace bt;
using namespace bt::stacking;

namespace {

Puzzle makeBoard() { return Puzzle(GridType(GridKind::Stacking)); }

void addSized(Puzzle& puz, Problem& prob, unsigned int size) {
  const unsigned int id = addDisk(puz, size);
  prob.setShapeMaximum(puz, id, 1);
  prob.setShapeMinimum(puz, id, 1);
}

// The Tower of Hanoi with n discs: the tower on rod 1, its goal on rod 3.
Problem& makeHanoi(Puzzle& puz, unsigned int n) {
  Problem& pr = puz.getProblem(puz.addProblem());
  for (unsigned int size = n; size >= 1; size--) {
    addSized(puz, pr, size);
  }
  pr.setRodSetId(0);
  syncMaps(puz, pr);
  for (unsigned int id = 0; id < n; id++) {
    REQUIRE(placeDisk(puz, pr, false, id, 0).empty());
    REQUIRE(placeDisk(puz, pr, true, id, 2).empty());
  }
  REQUIRE(setupError(puz, pr).empty());
  return pr;
}

// The classic Panex swap (test_stacking.cpp's): three Panex columns n deep,
// a tower on each outer column, the goal the towers swapped.
Problem& makePanexSwap(Puzzle& puz, unsigned int n) {
  RodSet& board = puz.getRodSet(0);
  board.rodCount = 3;
  board.growHeight = false;
  board.definedHeight = n;
  board.sizeMatters = false;
  board.panexColumns = true;
  Problem& pr = puz.getProblem(puz.addProblem());
  for (unsigned int side = 0; side < 2; side++) {
    for (unsigned int size = n; size >= 1; size--) {
      addSized(puz, pr, size);
    }
  }
  pr.setRodSetId(0);
  syncMaps(puz, pr);
  for (unsigned int i = 0; i < n; i++) {
    REQUIRE(placeDisk(puz, pr, false, i, 0).empty());
    REQUIRE(placeDisk(puz, pr, false, n + i, 2).empty());
    REQUIRE(placeDisk(puz, pr, true, n + i, 0).empty());
    REQUIRE(placeDisk(puz, pr, true, i, 2).empty());
  }
  REQUIRE(setupError(puz, pr).empty());
  return pr;
}

// Panex Jr (test_stacking.cpp's): two Panex columns six deep and a pocket
// column, one tower moved across.
Problem& makePanexJr(Puzzle& puz) {
  RodSet& board = puz.getRodSet(0);
  board.rodCount = 2;
  board.growHeight = false;
  board.definedHeight = 6;
  board.sizeMatters = false;
  board.panexColumns = true;
  board.pocketColumn = true;
  board.pocketHeight = 1;
  Problem& pr = puz.getProblem(puz.addProblem());
  for (unsigned int size = 6; size >= 1; size--) {
    addSized(puz, pr, size);
  }
  pr.setRodSetId(0);
  syncMaps(puz, pr);
  for (unsigned int id = 0; id < 6; id++) {
    REQUIRE(placeDisk(puz, pr, false, id, 0).empty());
    REQUIRE(placeDisk(puz, pr, true, id, 1).empty());
  }
  REQUIRE(setupError(puz, pr).empty());
  return pr;
}

std::vector<std::pair<unsigned int, unsigned int>> pairsOf(
    const std::vector<PlayTransfer>& moves) {
  std::vector<std::pair<unsigned int, unsigned int>> out;
  out.reserve(moves.size());
  for (const PlayTransfer& m : moves) {
    out.emplace_back(m.from, m.to);
  }
  return out;
}

// Walks the positions a game can reach from the start (at most `most`) and
// checks, at each one, that it is a position, that playMoves gives exactly
// the transfers moveAllowed allows with the heights the frames draw, that
// moveRefusal has a sentence for every other pair and none for those, and
// that the goal is told where it is. Returns how many positions it saw.
std::size_t walkGame(const Puzzle& puz, const Problem& pr, std::size_t most) {
  const PlayStart start = playStart(puz, pr);
  REQUIRE(start.error.empty());
  const RodSet& board = puz.getRodSet(pr.getRodSetId());
  const std::vector<Disc>& discs = start.discs;
  const StackingPlacer placer(puz, pr);
  const auto rods = static_cast<unsigned int>(start.stacks.size());
  std::set<Stacking> seen{start.stacks};
  std::queue<Stacking> todo;
  todo.push(start.stacks);
  bool goalSeen = false;
  while (!todo.empty() && seen.size() < most) {
    const Stacking cfg = todo.front();
    todo.pop();
    CHECK(positionRefusal(puz, pr, cfg).empty());
    // where the discs rest is where the frames place them
    const Heights heights = restingHeights(puz, pr, cfg);
    const std::vector<Placement> placed = placer.place(cfg);
    for (std::size_t r = 0; r < cfg.size(); r++) {
      REQUIRE(heights[r].size() == cfg[r].size());
      for (std::size_t h = 0; h < cfg[r].size(); h++) {
        CHECK(heights[r][h] == placed[cfg[r][h]].getZ());
      }
    }
    const Result<std::vector<PlayTransfer>> allowed = playMoves(puz, pr, cfg);
    REQUIRE(allowed.has_value());
    std::vector<std::pair<unsigned int, unsigned int>> want;
    for (unsigned int from = 0; from < rods; from++) {
      for (unsigned int to = 0; to < rods; to++) {
        const bool ok = moveAllowed(board, discs, cfg, from, to);
        CHECK(moveRefusal(board, discs, cfg, from, to).empty() == ok);
        if (ok) {
          want.emplace_back(from, to);
        }
      }
    }
    CHECK(pairsOf(*allowed) == want);
    for (const PlayTransfer& m : *allowed) {
      Stacking next = cfg;
      next[m.to].push_back(next[m.from].back());
      next[m.from].pop_back();
      CHECK(m.z == restingHeights(puz, pr, next)[m.to].back());
      CHECK(m.solved == (start.hasGoal && next == start.goal));
      goalSeen = goalSeen || m.solved;
      if (seen.insert(next).second) {
        todo.push(std::move(next));
      }
    }
  }
  if (todo.empty() && start.hasGoal && !start.solved) {
    // the whole game was walked: a goal the solver reaches was met
    CHECK(goalSeen == findStackPath(puz, pr).has_value());
  }
  return seen.size();
}

// The transfers of a solver's path, read from its frames: the disc whose
// rod changed between a transfer's first and last frame, and the rods.
std::vector<PlayMove> transfersOf(const Puzzle& puz, const Problem& pr, const Separation& path,
                                  std::vector<PlayStep>* steps) {
  const BoardLayout lay = layoutBoard(puz, pr, false);
  const auto rodAt = [&lay](int x) {
    const auto it = std::find(lay.rodX.begin(), lay.rodX.end(), x);
    REQUIRE(it != lay.rodX.end());
    return static_cast<unsigned int>(it - lay.rodX.begin());
  };
  std::vector<PlayMove> out;
  for (unsigned int m = 0; m < logicalMoves(path); m++) {
    const State& a = path.getState(m * kStepsPerMove);
    const State& b = path.getState((m + 1) * kStepsPerMove);
    for (unsigned int i = 0; i < path.getPieceNumber(); i++) {
      if (a.getX(i) != b.getX(i)) {
        out.push_back(PlayMove{rodAt(a.getX(i)), rodAt(b.getX(i))});
        steps->push_back(PlayStep{i, b.getZ(i), false});
      }
    }
  }
  return out;
}

}  // namespace

TEST_CASE("stacking play: a game of the Tower of Hanoi", "[stacking][play]") {
  Puzzle puz = makeBoard();
  const Problem& pr = makeHanoi(puz, 3);

  const PlayStart start = playStart(puz, pr);
  REQUIRE(start.error.empty());
  REQUIRE(start.discs.size() == 3);
  CHECK(start.discs[0].size == 3);
  CHECK(start.discs[2].size == 1);
  CHECK(start.stacks == Stacking{{0, 1, 2}, {}, {}});
  CHECK(start.heights == Heights{{0, 1, 2}, {}, {}});
  REQUIRE(start.hasGoal);
  CHECK(start.goal == Stacking{{}, {}, {0, 1, 2}});
  CHECK(start.goalHeights == Heights{{}, {}, {0, 1, 2}});
  CHECK(start.goalError.empty());
  CHECK_FALSE(start.solved);

  // the smallest disc goes to either empty rod, and nothing else moves
  const Result<std::vector<PlayTransfer>> first = playMoves(puz, pr, start.stacks);
  REQUIRE(first.has_value());
  REQUIRE(first->size() == 2);
  CHECK((*first)[0].from == 0);
  CHECK((*first)[0].to == 1);
  CHECK((*first)[1].to == 2);
  CHECK((*first)[1].z == 0);
  CHECK_FALSE((*first)[1].solved);

  // the seven transfers: only the last one gives the goal
  const std::vector<PlayMove> seven{{0, 2}, {0, 1}, {2, 1}, {0, 2}, {1, 0}, {1, 2}, {0, 2}};
  const Result<PlayReplay> game = playReplay(puz, pr, start.stacks, seven);
  REQUIRE(game.has_value());
  CHECK(game->error.empty());
  CHECK_FALSE(game->solved);
  REQUIRE(game->steps.size() == 7);
  const std::vector<unsigned int> discs{2, 1, 2, 0, 2, 1, 2};
  const std::vector<int> rests{0, 0, 1, 0, 0, 1, 2};
  for (std::size_t i = 0; i < 7; i++) {
    CHECK(game->steps[i].disc == discs[i]);
    CHECK(game->steps[i].z == rests[i]);
    CHECK(game->steps[i].solved == (i == 6));
  }

  // the last transfer alone, from the position before it, is told solved
  const Result<std::vector<PlayTransfer>> last =
      playMoves(puz, pr, Stacking{{2}, {}, {0, 1}});
  REQUIRE(last.has_value());
  CHECK(std::count_if(last->begin(), last->end(),
                      [](const PlayTransfer& m) { return m.solved; }) == 1);

  // no moves: whether the position is the goal
  CHECK(playReplay(puz, pr, start.goal, {})->solved);
  CHECK_FALSE(playReplay(puz, pr, start.stacks, {})->solved);
}

TEST_CASE("stacking play: a replay stops at the first transfer the rules refuse",
          "[stacking][play]") {
  Puzzle puz = makeBoard();
  const Problem& pr = makeHanoi(puz, 3);
  const Stacking start = playStart(puz, pr).stacks;
  const auto refused = [&](const std::vector<PlayMove>& moves, std::size_t made) {
    const Result<PlayReplay> r = playReplay(puz, pr, start, moves);
    REQUIRE(r.has_value());
    CHECK(r->steps.size() == made);
    return r->error;
  };
  CHECK(refused({{0, 2}, {0, 2}}, 1) == "A larger disc cannot sit on a smaller one.");
  CHECK(refused({{1, 2}}, 0) == "Rod 2 is empty.");
  CHECK(refused({{0, 0}}, 0) == "A disc moves from one rod to another.");
  CHECK(refused({{0, 1}, {0, 7}}, 1) == "That rod is not on this board.");
  CHECK(refused({{9, 1}}, 0) == "That rod is not on this board.");

  // rods two high: the third disc does not fit
  puz.getRodSet(0).growHeight = false;
  puz.getRodSet(0).definedHeight = 3;
  puz.getRodSet(0).sizeMatters = false;
  const Result<PlayReplay> tall = playReplay(puz, pr, Stacking{{0}, {}, {1, 2}}, {{0, 2}});
  REQUIRE(tall.has_value());
  CHECK(tall->error.empty());
  puz.getRodSet(0).definedHeight = 2;
  const Result<PlayReplay> full = playReplay(puz, pr, Stacking{{0}, {}, {1, 2}}, {{0, 2}});
  REQUIRE(full.has_value());
  CHECK(full->error == "Rod 3 is full.");

  // only to a neighboring rod
  puz.getRodSet(0).distanceMatters = true;
  const Result<PlayReplay> far = playReplay(puz, pr, Stacking{{0}, {}, {1, 2}}, {{0, 1}, {2, 0}});
  REQUIRE(far.has_value());
  CHECK(far->steps.size() == 1);
  CHECK(far->error == "A disc can only move to a neighboring rod.");
}

TEST_CASE("stacking play: the play calls keep the solver's rules at every position",
          "[stacking][play]") {
  SECTION("the Tower of Hanoi") {
    Puzzle puz = makeBoard();
    const Problem& pr = makeHanoi(puz, 4);
    CHECK(walkGame(puz, pr, 100000) == 81);
  }
  SECTION("only to a neighboring rod") {
    Puzzle puz = makeBoard();
    puz.getRodSet(0).distanceMatters = true;
    const Problem& pr = makeHanoi(puz, 3);
    CHECK(walkGame(puz, pr, 100000) == 27);
  }
  SECTION("rods of a defined height, any size on any") {
    Puzzle puz = makeBoard();
    puz.getRodSet(0).growHeight = false;
    puz.getRodSet(0).definedHeight = 3;
    puz.getRodSet(0).sizeMatters = false;
    const Problem& pr = makeHanoi(puz, 3);
    CHECK(walkGame(puz, pr, 100000) > 27);
  }
  SECTION("the Panex swap: discs raised into the bridge block") {
    Puzzle puz = makeBoard();
    const Problem& pr = makePanexSwap(puz, 3);
    CHECK(walkGame(puz, pr, 100000) > 100);
  }
  SECTION("the Panex swap with the outside channel") {
    Puzzle puz = makeBoard();
    const Problem& pr = makePanexSwap(puz, 3);
    puz.getRodSet(0).outsideChannel = true;
    CHECK(walkGame(puz, pr, 100000) > 100);
  }
  SECTION("Panex Jr: a pocket column") {
    Puzzle puz = makeBoard();
    const Problem& pr = makePanexJr(puz);
    CHECK(walkGame(puz, pr, 4000) > 143);
  }
}

TEST_CASE("stacking play: a path the solver found is a legal game", "[stacking][play]") {
  for (const bool jr : {false, true}) {
    INFO((jr ? "Panex Jr" : "the Panex swap"));
    Puzzle puz = makeBoard();
    const Problem& pr = jr ? makePanexJr(puz) : makePanexSwap(puz, 3);
    const std::optional<Separation> path = findStackPath(puz, pr);
    REQUIRE(path);
    std::vector<PlayStep> frames;
    const std::vector<PlayMove> moves = transfersOf(puz, pr, *path, &frames);
    REQUIRE(moves.size() == logicalMoves(*path));

    const PlayStart start = playStart(puz, pr);
    REQUIRE(start.error.empty());
    const Result<PlayReplay> game = playReplay(puz, pr, start.stacks, moves);
    REQUIRE(game.has_value());
    CHECK(game->error.empty());
    REQUIRE(game->steps.size() == moves.size());
    for (std::size_t i = 0; i < moves.size(); i++) {
      CHECK(game->steps[i].disc == frames[i].disc);
      CHECK(game->steps[i].z == frames[i].z);
      // a shortest path meets the goal at its end only
      CHECK(game->steps[i].solved == (i + 1 == moves.size()));
    }
  }
}

TEST_CASE("stacking play: a problem is played once its start has every disc",
          "[stacking][play]") {
  Puzzle puz = makeBoard();
  Problem& pr = puz.getProblem(puz.addProblem());
  CHECK(playStart(puz, pr).error == "Choose a rod set with Set Start/Goal Rods.");
  pr.setRodSetId(0);
  syncMaps(puz, pr);
  CHECK(playStart(puz, pr).error == "Add the discs that move on the rods.");

  addSized(puz, pr, 2);
  addSized(puz, pr, 1);
  REQUIRE(placeDisk(puz, pr, false, 0, 0).empty());
  PlayStart start = playStart(puz, pr);
  CHECK(start.error == "S2 is not on the start.");
  CHECK(start.stacks.empty());
  CHECK(playMoves(puz, pr, Stacking{{0}, {}, {}}).error().code == Errc::InvalidArgument);

  // every disc on the start, no goal: played, never solved
  REQUIRE(placeDisk(puz, pr, false, 1, 0).empty());
  start = playStart(puz, pr);
  CHECK(start.error.empty());
  CHECK(start.stacks == Stacking{{0, 1}, {}, {}});
  CHECK_FALSE(start.hasGoal);
  CHECK(start.goal.empty());
  CHECK(start.goalError.empty());
  const Result<std::vector<PlayTransfer>> free = playMoves(puz, pr, start.stacks);
  REQUIRE(free.has_value());
  CHECK(std::none_of(free->begin(), free->end(), [](const PlayTransfer& m) { return m.solved; }));
  CHECK_FALSE(playReplay(puz, pr, start.stacks, {{0, 1}})->steps[0].solved);

  // half a goal is no goal, and says why
  REQUIRE(placeDisk(puz, pr, true, 0, 2).empty());
  start = playStart(puz, pr);
  CHECK(start.error.empty());
  CHECK_FALSE(start.hasGoal);
  CHECK(start.goalError == setupError(puz, pr));
  CHECK_FALSE(start.goalError.empty());

  // the whole goal: played to it
  REQUIRE(placeDisk(puz, pr, true, 1, 2).empty());
  start = playStart(puz, pr);
  CHECK(start.hasGoal);
  CHECK(start.goal == Stacking{{}, {}, {0, 1}});

  // a start that breaks the size rule is not played
  std::swap(pr.editStart().rods[0][0], pr.editStart().rods[0][1]);
  start = playStart(puz, pr);
  CHECK(start.error.find("sits on the smaller") != std::string::npos);
  CHECK(start.error.find("the start") != std::string::npos);

  // a start that is the goal already
  pr.editStart() = pr.goalStacks();
  start = playStart(puz, pr);
  CHECK(start.error.empty());
  CHECK(start.solved);
}

TEST_CASE("stacking play: what is no position is refused with its reason", "[stacking][play]") {
  Puzzle puz = makeBoard();
  const Problem& pr = makeHanoi(puz, 3);
  CHECK(positionRefusal(puz, pr, Stacking{{0, 1, 2}, {}, {}}).empty());
  CHECK(positionRefusal(puz, pr, Stacking{{0}, {1}, {2}}).empty());
  CHECK(positionRefusal(puz, pr, Stacking{{0, 1, 2}, {}}) ==
        "The position does not match the number of rods.");
  CHECK(positionRefusal(puz, pr, Stacking{{0, 1, 2}, {}, {}, {}}) ==
        "The position does not match the number of rods.");
  CHECK(positionRefusal(puz, pr, Stacking{{0, 1, 3}, {}, {}}) ==
        "The position names a disc that is not in the problem.");
  CHECK(positionRefusal(puz, pr, Stacking{{0, 1}, {}, {}}) == "S3 is not on the position.");
  CHECK(positionRefusal(puz, pr, Stacking{{0, 1, 2}, {2}, {}}) ==
        "S3 is on the position more than once.");
  CHECK(positionRefusal(puz, pr, Stacking{{1, 0, 2}, {}, {}}).find("sits on the smaller") !=
        std::string::npos);
  for (const Stacking& bad : {Stacking{{0, 1}, {}, {}}, Stacking{{2, 0, 1}, {}, {}}}) {
    CHECK(playMoves(puz, pr, bad).error().code == Errc::InvalidArgument);
    CHECK(playReplay(puz, pr, bad, {}).error().code == Errc::InvalidArgument);
    CHECK(playMoves(puz, pr, bad).error().message == positionRefusal(puz, pr, bad));
  }

  // a position as a stack map names the discs by shape and copy
  const StackMap map = positionMap(puz, pr, Stacking{{0}, {2}, {1}});
  REQUIRE(map.rods.size() == 3);
  CHECK(map.rods[0] == std::vector<DiskRef>{DiskRef{0, 0}});
  CHECK(map.rods[1] == std::vector<DiskRef>{DiskRef{2, 0}});
  CHECK(map.rods[2] == std::vector<DiskRef>{DiskRef{1, 0}});

  // a Panex column: a small disc cannot lie under a tower
  Puzzle panex = makeBoard();
  const Problem& swap = makePanexSwap(panex, 3);
  CHECK(positionRefusal(panex, swap, playStart(panex, swap).stacks).empty());
  CHECK(positionRefusal(panex, swap, Stacking{{2, 1, 0, 5}, {}, {3, 4}})
            .find("in a Panex column") != std::string::npos);
}

// SPDX-License-Identifier: GPL-3.0-or-later
// sliding_puzzles_old <dir>: builds the sliding puzzles of the original's
// test/test_sliding.cpp with the original's own code (sliding.cpp's
// ensureSetup, placeStart, placeGoal, addStartGoalShape, stampPiece,
// syncSlidingProblems), a few more of the same kinds, and trays from its
// fuzz case, and saves each into <dir> as <name>.xmpuzzle. Built by
// `oracle.py sliding-puzzles` against the original's build/libburr_core.a;
// the files give the sliding tier of the corpus its breadth
// (tests/oracle/corpus.txt).

#include "lib/gridtype.h"
#include "lib/problem.h"
#include "lib/puzzle.h"
#include "lib/sliding.h"
#include "lib/voxel.h"
#include "tools/xml.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

std::filesystem::path outDir;

void save(puzzle_c& puz, const std::string& name) {
  puz.save(outDir / (name + ".xmpuzzle"));
  std::printf("%s\n", name.c_str());
}

std::unique_ptr<puzzle_c> newPuzzle() {
  return std::make_unique<puzzle_c>(new gridType_c(gridType_c::GT_SLIDING));
}

unsigned int unitPiece(puzzle_c& puz) {
  const unsigned int id = puz.addShape(1, 1, 1);
  puz.getShape(id)->setState(0, 0, 0, voxel_c::VX_FILLED);
  return id;
}

// "sliding: place start and find a one-cell path"
void oneCell() {
  auto puz = newPuzzle();
  sliding::ensureSetup(*puz, 3, 1);
  const unsigned int piece = unitPiece(*puz);
  problem_c* pr = puz->getProblem(0);
  pr->setShapeMaximum(piece, 1);
  sliding::placeStart(*pr, piece, 0, 0);
  sliding::placeGoal(*pr, piece, 2, 0);
  sliding::syncMaxHoles(*pr);
  save(*puz, "slide_one_cell");
}

// "sliding: a variable cell is not a legal place to finish": the goal on
// the far normal cell, or on the variable one (no path)
void variableCell(bool goalOnVariable) {
  auto puz = newPuzzle();
  const unsigned int trayId = sliding::addStartGoalShape(*puz, 3, 1);
  voxel_c* tray = puz->getShape(trayId);
  tray->setState(1, 0, 0, voxel_c::VX_VARIABLE);
  const unsigned int piece = unitPiece(*puz);
  tray->setColor(0, 0, 0, piece + 1);
  tray->setGoalPiece((unsigned)tray->getIndex(goalOnVariable ? 1 : 2, 0, 0), piece + 1);
  sliding::syncSlidingProblems(*puz);
  save(*puz, goalOnVariable ? "slide_goal_on_variable" : "slide_variable_crossed");
}

// "sliding: a turn is one fluid move"
void turn() {
  auto puz = newPuzzle();
  sliding::ensureSetup(*puz, 2, 2);
  const unsigned int piece = unitPiece(*puz);
  problem_c* pr = puz->getProblem(0);
  pr->setShapeMaximum(piece, 1);
  sliding::placeStart(*pr, piece, 0, 0);
  sliding::placeGoal(*pr, piece, 1, 1);
  save(*puz, "slide_turn");
}

// "sliding: a piece following another is not a caterpillar"
void caterpillar() {
  auto puz = newPuzzle();
  sliding::ensureSetup(*puz, 6, 1);
  const unsigned int a = unitPiece(*puz);
  const unsigned int b = unitPiece(*puz);
  problem_c* pr = puz->getProblem(0);
  pr->setShapeMaximum(a, 1);
  pr->setShapeMaximum(b, 1);
  sliding::placeStart(*pr, a, 0, 0);
  sliding::placeStart(*pr, b, 1, 0);
  sliding::placeGoal(*pr, a, 3, 0);
  sliding::placeGoal(*pr, b, 5, 0);
  save(*puz, "slide_caterpillar");
}

// "sliding: a piece nested in a pocket rides along only with nested
// slides", and "... sticking out of a pocket ... only when partially
// nested"
void pocket(bool partial) {
  auto puz = newPuzzle();
  sliding::ensureSetup(*puz, partial ? 3 : 2, 4);
  const unsigned int outer = puz->addShape(2, 3, 1);
  for (auto c : std::vector<std::pair<int, int>>{{0, 0}, {1, 0}, {1, 1}, {0, 2}, {1, 2}})
    puz->getShape(outer)->setState(c.first, c.second, 0, voxel_c::VX_FILLED);
  const unsigned int inner = puz->addShape(partial ? 2 : 1, 1, 1);
  puz->getShape(inner)->setState(0, 0, 0, voxel_c::VX_FILLED);
  if (partial)
    puz->getShape(inner)->setState(1, 0, 0, voxel_c::VX_FILLED);
  problem_c* pr = puz->getProblem(0);
  pr->setShapeMaximum(outer, 1);
  pr->setShapeMaximum(inner, 1);
  if (partial) {
    sliding::placeGoal(*pr, outer, 1, 0);
    sliding::placeGoal(*pr, inner, 0, 1);
    sliding::placeStart(*pr, outer, 1, 1);
    sliding::placeStart(*pr, inner, 0, 2);
  } else {
    sliding::placeGoal(*pr, outer, 0, 0);
    sliding::placeGoal(*pr, inner, 0, 1);
    sliding::placeStart(*pr, outer, 0, 1);
    sliding::placeStart(*pr, inner, 0, 2);
  }
  save(*puz, partial ? "slide_pocket_partial" : "slide_pocket_nested");
}

// "sliding: the start stamp is the only initial placement": all corridor,
// the goal on a corridor cell
void allVariable() {
  auto puz = newPuzzle();
  const unsigned int trayId = sliding::addStartGoalShape(*puz, 3, 1);
  voxel_c* tray = puz->getShape(trayId);
  for (unsigned int x = 0; x < 3; x++)
    tray->setState(x, 0, 0, voxel_c::VX_VARIABLE);
  const unsigned int piece = unitPiece(*puz);
  tray->setColor(0, 0, 0, piece + 1);
  tray->setGoalPiece((unsigned)tray->getIndex(2, 0, 0), piece + 1);
  sliding::syncSlidingProblems(*puz);
  save(*puz, "slide_all_variable");
}

// "sliding: normal cells may stay empty": a free piece, no stamps
void emptyFloor() {
  auto puz = newPuzzle();
  sliding::addStartGoalShape(*puz, 3, 1);
  const unsigned int piece = unitPiece(*puz);
  sliding::syncSlidingProblems(*puz);
  problem_c* pr = puz->getProblem(0);
  pr->setShapeMinimum(piece, 1);
  pr->setShapeMaximum(piece, 1);
  save(*puz, "slide_empty_floor");
}

// "sliding: variable cells are corridors beside normal floor"
void corridor() {
  auto puz = newPuzzle();
  const unsigned int trayId = sliding::addStartGoalShape(*puz, 2, 1);
  puz->getShape(trayId)->setState(0, 0, 0, voxel_c::VX_VARIABLE);
  const unsigned int piece = unitPiece(*puz);
  sliding::syncSlidingProblems(*puz);
  puz->getProblem(0)->setShapeMaximum(piece, 1);
  save(*puz, "slide_corridor");
}

// "sliding: a shorter path keeps the solution number": two free pieces in a
// 2x2 tray, each with a goal: many starts, several to the same picture
void shorterPath() {
  auto puz = newPuzzle();
  sliding::ensureSetup(*puz, 2, 2);
  const unsigned int p = unitPiece(*puz);
  const unsigned int q = unitPiece(*puz);
  problem_c* pr = puz->getProblem(0);
  pr->setShapeMaximum(p, 1);
  pr->setShapeMaximum(q, 1);
  sliding::placeGoal(*pr, p, 1, 1);
  sliding::placeGoal(*pr, q, 0, 1);
  save(*puz, "slide_shorter_path");
}

// "sliding: every layer of a deeper tray blocks a move"
void twoLayers(bool blocked) {
  const std::string top1 = blocked ? "#_#" : "###";
  std::string xml =
      "<?xml version=\"1.0\"?>\n"
      "<puzzle version=\"2\"><gridType type=\"5\"/>"
      "<colors><color red=\"40\" green=\"70\" blue=\"100\"/>"
      "<color red=\"70\" green=\"100\" blue=\"40\"/></colors><shapes>"
      "<voxel x=\"3\" y=\"2\" z=\"2\" name=\"sg:1:\" goal=\"0,0,2,0,0,0,0,0,2,0,0,0\" type=\"0\">"
      "#2######2_#" + top1 + "</voxel>"
      "<voxel x=\"1\" y=\"1\" z=\"2\" name=\"A\" type=\"0\">#2#2</voxel>"
      "</shapes><problems><problem name=\"START/GOAL 1\" state=\"0\"><shapes>"
      "<shape id=\"1\" count=\"1\"/></shapes><result id=\"0\"/><bitmap/>"
      "</problem></problems><comment/></puzzle>";
  std::stringstream in(xml);
  xmlParser_c pars(in);
  puzzle_c puz(pars);
  save(puz, blocked ? "slide_two_layers_shut" : "slide_two_layers_open");
}

// slideFixture of the tests: each piece its own rectangle, its start at
// (x, y) and its goal at (goalX, goalY) or none
struct block_c {
  unsigned int w, h;
  int x, y;
  int goalX, goalY;
};

std::unique_ptr<puzzle_c> blocks(unsigned int trayW, unsigned int trayH,
                                 const std::vector<block_c>& list) {
  auto puz = newPuzzle();
  const unsigned int trayId = sliding::addStartGoalShape(*puz, trayW, trayH);
  voxel_c* tray = puz->getShape(trayId);
  for (const block_c& b : list) {
    const unsigned int id = puz->addShape(b.w, b.h, 1);
    for (unsigned int y = 0; y < b.h; y++)
      for (unsigned int x = 0; x < b.w; x++) {
        puz->getShape(id)->setState(x, y, 0, voxel_c::VX_FILLED);
        tray->setColor(b.x + x, b.y + y, 0, id + 1);
        if (b.goalX >= 0)
          tray->setGoalPiece((unsigned)tray->getIndex(b.goalX + x, b.goalY + y, 0), id + 1);
      }
  }
  sliding::syncSlidingProblems(*puz);
  return puz;
}

void blockPuzzles() {
  // Klotski: 81 moves, 25,955 arrangements with like pieces as one
  const std::vector<block_c> klotski = {
      {2, 2, 1, 0, 1, 3},   {1, 2, 0, 0, -1, -1}, {1, 2, 3, 0, -1, -1}, {1, 2, 0, 2, -1, -1},
      {1, 2, 3, 2, -1, -1}, {2, 1, 1, 2, -1, -1}, {1, 1, 1, 3, -1, -1}, {1, 1, 2, 3, -1, -1},
      {1, 1, 0, 4, -1, -1}, {1, 1, 3, 4, -1, -1},
  };
  save(*blocks(4, 5, klotski), "slide_klotski");

  // like pieces searched as one: a bar through six unit pieces
  const std::vector<block_c> like = {
      {2, 1, 0, 0, 2, 2},  {1, 1, 2, 0, -1, -1}, {1, 1, 3, 0, -1, -1}, {1, 1, 0, 1, -1, -1},
      {1, 1, 1, 1, -1, -1}, {1, 1, 2, 1, 0, 0},  {1, 1, 0, 2, 3, 0},
  };
  save(*blocks(4, 3, like), "slide_like_pieces");

  // thirteen pieces of 32 places: 128-bit keys
  std::vector<block_c> wide = {{1, 1, 0, 0, 7, 3}, {1, 1, 7, 3, 0, 0}};
  for (int k = 0; k < 11; k++)
    wide.push_back({1, 1, 1 + k % 6, 1 + k / 6, -1, -1});
  save(*blocks(8, 4, wide), "slide_wide_keys");

  // the eight and fifteen puzzles with the last two swapped, which no
  // sliding reaches: every arrangement searched (181,440), or a limit
  for (unsigned int side : {3U, 4U}) {
    std::vector<block_c> tiles;
    const int n = static_cast<int>(side * side) - 1;
    for (int k = 0; k < n; k++) {
      const int g = k == n - 2 ? n - 1 : k == n - 1 ? n - 2 : k;
      tiles.push_back({1, 1, k % static_cast<int>(side), k / static_cast<int>(side),
                       g % static_cast<int>(side), g / static_cast<int>(side)});
    }
    save(*blocks(side, side, tiles), side == 3 ? "slide_swapped_eight" : "slide_swapped_fifteen");
  }
}

// "sliding: a whole piece is placed as a start or a goal", as it ends: an L
// and a unit piece, each with a start and the unit with a goal
void wholePiece() {
  auto puz = newPuzzle();
  const unsigned int trayId = sliding::addStartGoalShape(*puz, 5, 3);
  const unsigned int L = puz->addShape(2, 2, 1);
  puz->getShape(L)->setState(0, 0, 0, voxel_c::VX_FILLED);
  puz->getShape(L)->setState(1, 0, 0, voxel_c::VX_FILLED);
  puz->getShape(L)->setState(0, 1, 0, voxel_c::VX_FILLED);
  const unsigned int U = unitPiece(*puz);
  voxel_c* tray = puz->getShape(trayId);
  sliding::stampPiece(tray, puz->getShape(L), L, 0, 2, false);
  sliding::stampPiece(tray, puz->getShape(U), U, 4, 2, false);
  sliding::stampPiece(tray, puz->getShape(U), U, 3, 0, true);
  sliding::syncSlidingProblems(*puz);
  save(*puz, "slide_whole_piece");
}

// the one-way puzzle of the tests: a C that can close round a piece it
// passes; its goal takes in a corridor cell, so no search finds a path
void oneWay() {
  auto puz = newPuzzle();
  voxel_c* tray = puz->getShape(sliding::addStartGoalShape(*puz, 5, 5));
  struct piece_c {
    std::vector<std::pair<int, int>> cells;
    int x, y;
  };
  const std::vector<std::pair<int, int>> unit = {{0, 0}};
  const std::vector<std::pair<int, int>> block = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
  const std::vector<piece_c> pieces = {
      {{{0, 0}, {1, 0}, {2, 0}, {0, 1}, {0, 2}, {1, 2}, {2, 2}}, 0, 0},
      {unit, 1, 1}, {unit, 4, 1}, {block, 2, 3}, {unit, 3, 0}, {unit, 4, 3}, {block, 0, 3},
  };
  for (const piece_c& pc : pieces) {
    const unsigned int id = puz->addShape(3, 3, 1);
    for (auto c : pc.cells) {
      puz->getShape(id)->setState(c.first, c.second, 0, voxel_c::VX_FILLED);
      tray->setColor(pc.x + c.first, pc.y + c.second, 0, id + 1);
      if (&pc == &pieces[0])
        tray->setGoalPiece((unsigned)tray->getIndex(2 + c.first, 2 + c.second, 0), id + 1);
    }
  }
  tray->setState(4, 4, 0, voxel_c::VX_VARIABLE);
  sliding::syncSlidingProblems(*puz);
  save(*puz, "slide_one_way");
}

// a 4x4 tray: A must go from (0,0) to (3,3); a bar and a unit piece have no
// stamps, so every layout of them is a start (the tests' manyStarts)
void manyStarts() {
  auto puz = newPuzzle();
  const unsigned int trayId = sliding::addStartGoalShape(*puz, 4, 4);
  const unsigned int a = unitPiece(*puz);
  const unsigned int b = puz->addShape(1, 2, 1);
  puz->getShape(b)->setState(0, 0, 0, voxel_c::VX_FILLED);
  puz->getShape(b)->setState(0, 1, 0, voxel_c::VX_FILLED);
  const unsigned int c = unitPiece(*puz);
  voxel_c* tray = puz->getShape(trayId);
  tray->setColor(0, 0, 0, a + 1);
  tray->setGoalPiece((unsigned)tray->getIndex(3, 3, 0), a + 1);
  sliding::syncSlidingProblems(*puz);
  problem_c* pr = puz->getProblem(0);
  for (unsigned int s : {b, c}) {
    pr->setShapeMaximum(s, 1);
    pr->setShapeMinimum(s, 1);
  }
  save(*puz, "slide_many_starts");
}

// The trays of "sliding: fuzz, the full search agrees with the queue
// search", the same generator and seed: the first `count` it makes.
void fuzz(int count) {
  uint64_t x = 1234567;
  auto rnd = [&x](int n) {
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return (int)(x % (uint64_t)n);
  };
  int made = 0;
  for (int trial = 0; made < count; trial++) {
    const int W = 5 + rnd(3), H = 4 + rnd(3);
    const bool reachable = rnd(2) == 0;
    auto puz = newPuzzle();
    voxel_c* tray = puz->getShape(sliding::addStartGoalShape(*puz, W, H));
    std::vector<std::vector<int>> occ((size_t)W, std::vector<int>((size_t)H, 0));
    struct piece_c {
      std::vector<std::pair<int, int>> cells;
      int x, y;
    };
    std::vector<piece_c> pieces = {
        {{{0, 0}, {1, 0}, {2, 0}, {0, 1}, {0, 2}, {1, 2}, {2, 2}}, 0, 0},
        {{{0, 0}}, 1, 1},
    };
    for (const piece_c& pc : pieces)
      for (auto c : pc.cells)
        occ[(size_t)(pc.x + c.first)][(size_t)(pc.y + c.second)] = 1;
    const std::vector<std::vector<std::pair<int, int>>> shapes = {
        {{0, 0}}, {{0, 0}, {1, 0}}, {{0, 0}, {0, 1}}, {{0, 0}, {1, 0}, {0, 1}, {1, 1}}};
    for (int extra = 2 + rnd(5); extra > 0; extra--) {
      const auto& shape = shapes[(size_t)rnd(4)];
      for (int tries = 0; tries < 50; tries++) {
        const int px = rnd(W), py = rnd(H);
        bool fits = true;
        for (auto c : shape)
          if (px + c.first >= W || py + c.second >= H ||
              occ[(size_t)(px + c.first)][(size_t)(py + c.second)])
            fits = false;
        if (!fits)
          continue;
        for (auto c : shape)
          occ[(size_t)(px + c.first)][(size_t)(py + c.second)] = 1;
        pieces.push_back({shape, px, py});
        break;
      }
    }
    for (size_t i = 0; i < pieces.size(); i++) {
      const unsigned int id = puz->addShape(3, 3, 1);
      for (auto c : pieces[i].cells) {
        puz->getShape(id)->setState(c.first, c.second, 0, voxel_c::VX_FILLED);
        tray->setColor(pieces[i].x + c.first, pieces[i].y + c.second, 0, id + 1);
        if (i == 0)
          tray->setGoalPiece((unsigned)tray->getIndex(W - 3 + c.first, H - 3 + c.second, 0),
                             id + 1);
      }
    }
    if (!reachable) {
      if (occ[(size_t)(W - 1)][(size_t)(H - 1)])
        continue;
      tray->setState(W - 1, H - 1, 0, voxel_c::VX_VARIABLE);
    }
    sliding::syncSlidingProblems(*puz);
    char name[32];
    std::snprintf(name, sizeof(name), "slide_fuzz_%02d", made);
    save(*puz, name);
    made++;
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fputs("usage: sliding_puzzles_old <dir>\n", stderr);
    return 2;
  }
  outDir = argv[1];
  std::filesystem::create_directories(outDir);
  oneCell();
  variableCell(false);
  variableCell(true);
  turn();
  caterpillar();
  pocket(false);
  pocket(true);
  allVariable();
  emptyFloor();
  corridor();
  shorterPath();
  twoLayers(false);
  twoLayers(true);
  blockPuzzles();
  wholePiece();
  oneWay();
  manyStarts();
  fuzz(12);
  return 0;
}

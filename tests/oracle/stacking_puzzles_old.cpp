// SPDX-License-Identifier: GPL-3.0-or-later
// stacking_puzzles_old <dir>: builds the stacking puzzles of the original's
// test/test_stacking.cpp with the original's own code (stacking.cpp's
// addDisk, syncMaps, placeDisk, and the rod sets of puzzle_c), a few more
// of the same kinds, and saves each into <dir> as <name>.xmpuzzle. Built by
// `oracle.py stacking-puzzles` against the original's build/libburr_core.a;
// the files give the stacking tier of the corpus its breadth
// (tests/oracle/corpus.txt).

#include "lib/gridtype.h"
#include "lib/problem.h"
#include "lib/puzzle.h"
#include "lib/stacking.h"
#include "lib/voxel.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

using namespace stacking;

namespace {

std::filesystem::path outDir;

void save(puzzle_c& puz, const std::string& name) {
  puz.save(outDir / (name + ".xmpuzzle"));
  std::printf("%s\n", name.c_str());
}

std::unique_ptr<puzzle_c> board() {
  return std::make_unique<puzzle_c>(new gridType_c(gridType_c::GT_STACKING));
}

void addSized(puzzle_c& puz, problem_c& prob, unsigned int size) {
  const unsigned int id = addDisk(puz, size);
  prob.setShapeMaximum(id, 1);
  prob.setShapeMinimum(id, 1);
}

void place(problem_c& pr, bool goal, unsigned int shape, unsigned int rod) {
  const std::string err = placeDisk(pr, goal, shape, rod);
  if (!err.empty()) {
    std::fprintf(stderr, "could not place S%u on rod %u: %s\n", shape + 1, rod + 1, err.c_str());
    std::exit(1);
  }
}

// "three-disc Hanoi is seven transfers"
void hanoi() {
  auto puz = board();
  problem_c* pr = puz->getProblem(puz->addProblem());
  pr->setName("Hanoi");
  addSized(*puz, *pr, 3);
  addSized(*puz, *pr, 2);
  addSized(*puz, *pr, 1);
  pr->setRodSetId(0);
  syncMaps(*pr);
  for (unsigned int i = 0; i < 3; i++) {
    place(*pr, false, i, 0);
    place(*pr, true, i, 2);
  }
  save(*puz, "stack_hanoi_3");
}

// "distance limits a jump to the neighbouring rod", with move-over on too
void distance(bool moveOver) {
  auto puz = board();
  rodSet_c& rods = puz->getRodSet(0);
  rods.sizeMatters = false;
  rods.distanceMatters = true;
  rods.canMoveOver = moveOver;
  problem_c* pr = puz->getProblem(puz->addProblem());
  addSized(*puz, *pr, 1);
  pr->setRodSetId(0);
  syncMaps(*pr);
  place(*pr, false, 0, 0);
  place(*pr, true, 0, 2);
  save(*puz, moveOver ? "stack_move_over" : "stack_distance");
}

// Panex Jr: six discs on two Panex columns six deep and a pocket beside the
// first ("Panex Jr with a pocket column is 143 transfers"); without the
// pocket nothing moves; with a pocket of two.
void panexJr(bool pocket, unsigned int pocketHeight, const char* name) {
  auto puz = board();
  rodSet_c& rods = puz->getRodSet(0);
  rods.name = "Panex Jr";
  rods.rodCount = 2;
  rods.growHeight = false;
  rods.definedHeight = 6;
  rods.sizeMatters = false;
  rods.panexColumns = true;
  rods.pocketColumn = pocket;
  rods.pocketHeight = pocketHeight;
  problem_c* pr = puz->getProblem(puz->addProblem());
  for (unsigned int size = 6; size >= 1; size--) {
    addSized(*puz, *pr, size);
  }
  pr->setRodSetId(0);
  syncMaps(*pr);
  for (unsigned int id = 0; id < 6; id++) {
    place(*pr, false, id, 0);
    place(*pr, true, id, 1);
  }
  save(*puz, name);
}

// The classic Panex swap of n discs a tower, with the outside channel or not.
void panexSwap(unsigned int n, bool channel) {
  auto puz = board();
  rodSet_c& rods = puz->getRodSet(0);
  rods.rodCount = 3;
  rods.growHeight = false;
  rods.definedHeight = n;
  rods.sizeMatters = false;
  rods.panexColumns = true;
  rods.outsideChannel = channel;
  problem_c* pr = puz->getProblem(puz->addProblem());
  for (unsigned int side = 0; side < 2; side++) {
    for (unsigned int size = n; size >= 1; size--) {
      addSized(*puz, *pr, size);
    }
  }
  pr->setRodSetId(0);
  syncMaps(*pr);
  for (unsigned int i = 0; i < n; i++) {
    place(*pr, false, i, 0);
    place(*pr, false, n + i, 2);
    place(*pr, true, n + i, 0);
    place(*pr, true, i, 2);
  }
  save(*puz, std::string(channel ? "stack_panex_channel_" : "stack_panex_swap_") +
                 std::to_string(n));
}

// "the search agrees with findStackPath on plain rods": the size rule, the
// distance rule, both, and neither, towers of n from rod 1 to rod 3
void plainRods(int rules, unsigned int n) {
  auto puz = board();
  rodSet_c& rods = puz->getRodSet(0);
  rods.sizeMatters = rules != 2;
  rods.distanceMatters = rules == 1 || rules == 3;
  problem_c* pr = puz->getProblem(puz->addProblem());
  for (unsigned int size = n; size >= 1; size--) {
    addSized(*puz, *pr, size);
  }
  pr->setRodSetId(0);
  syncMaps(*pr);
  for (unsigned int i = 0; i < n; i++) {
    place(*pr, false, i, 0);
    place(*pr, true, i, 2);
  }
  save(*puz, "stack_plain_r" + std::to_string(rules) + "_n" + std::to_string(n));
}

// "the classic tower needs no search": rod 3 to rod 2
void tower(unsigned int n) {
  auto puz = board();
  puz->getRodSet(0).sizeMatters = true;
  problem_c* pr = puz->getProblem(puz->addProblem());
  for (unsigned int size = n; size >= 1; size--) {
    addSized(*puz, *pr, size);
  }
  pr->setRodSetId(0);
  syncMaps(*pr);
  for (unsigned int i = 0; i < n; i++) {
    place(*pr, false, i, 2);
    place(*pr, true, i, 1);
  }
  save(*puz, "stack_tower_" + std::to_string(n));
}

// Nine rods: more than the Panex search packs, so the Stacking Solver's own
// search runs (and the Panex Solver refuses).
void nineRods() {
  auto puz = board();
  puz->getRodSet(0).rodCount = 9;
  problem_c* pr = puz->getProblem(puz->addProblem());
  addSized(*puz, *pr, 2);
  addSized(*puz, *pr, 1);
  pr->setRodSetId(0);
  syncMaps(*pr);
  place(*pr, false, 0, 0);
  place(*pr, false, 1, 0);
  place(*pr, true, 0, 8);
  place(*pr, true, 1, 8);
  save(*puz, "stack_nine_rods");
}

// Copies of one disc ("start and goal must use the same discs", made right).
void copies() {
  auto puz = board();
  puz->getRodSet(0).sizeMatters = false;
  problem_c* pr = puz->getProblem(puz->addProblem());
  const unsigned int a = addDisk(*puz, 1);
  const unsigned int b = addDisk(*puz, 2);
  pr->setRodSetId(0);
  syncMaps(*pr);
  pr->setShapeMaximum(a, 2);
  pr->setShapeMinimum(a, 2);
  pr->setShapeMaximum(b, 1);
  pr->setShapeMinimum(b, 1);
  place(*pr, false, a, 0);
  place(*pr, false, a, 0);
  place(*pr, false, b, 2);
  place(*pr, true, a, 1);
  place(*pr, true, b, 1);
  place(*pr, true, a, 2);
  save(*puz, "stack_copies");
}

// The start is the goal: a path of no transfers.
void sameStartGoal() {
  auto puz = board();
  problem_c* pr = puz->getProblem(puz->addProblem());
  addSized(*puz, *pr, 2);
  addSized(*puz, *pr, 1);
  pr->setRodSetId(0);
  syncMaps(*pr);
  place(*pr, false, 0, 1);
  place(*pr, false, 1, 1);
  place(*pr, true, 0, 1);
  place(*pr, true, 1, 1);
  save(*puz, "stack_same_start_goal");
}

// Two rods one disc high, the two discs to swap: no transfer is possible.
void noPath() {
  auto puz = board();
  rodSet_c& rods = puz->getRodSet(0);
  rods.rodCount = 2;
  rods.growHeight = false;
  rods.definedHeight = 1;
  rods.sizeMatters = false;
  problem_c* pr = puz->getProblem(puz->addProblem());
  addSized(*puz, *pr, 1);
  addSized(*puz, *pr, 1);
  pr->setRodSetId(0);
  syncMaps(*pr);
  place(*pr, false, 0, 0);
  place(*pr, false, 1, 1);
  place(*pr, true, 1, 0);
  place(*pr, true, 0, 1);
  save(*puz, "stack_no_path");
}

// A goal without one of the discs: the setup is not ready (setupError).
void invalid() {
  auto puz = board();
  puz->getRodSet(0).sizeMatters = false;
  problem_c* pr = puz->getProblem(puz->addProblem());
  addSized(*puz, *pr, 1);
  addSized(*puz, *pr, 3);
  pr->setRodSetId(0);
  syncMaps(*pr);
  place(*pr, false, 0, 0);
  place(*pr, false, 1, 0);
  place(*pr, true, 0, 2);
  save(*puz, "stack_invalid");
}

// Two problems on two rod sets, named: Hanoi and a Panex swap of three.
void twoBoards() {
  auto puz = board();
  puz->getRodSet(0).name = "Hanoi";
  stacking::rodSet_c panex;
  panex.name = "Panex 3";
  panex.rodCount = 3;
  panex.growHeight = false;
  panex.definedHeight = 3;
  panex.sizeMatters = false;
  panex.panexColumns = true;
  puz->addRodSet(panex);
  problem_c* h = puz->getProblem(puz->addProblem());
  h->setName("Hanoi");
  for (unsigned int size = 3; size >= 1; size--) {
    addSized(*puz, *h, size);
  }
  h->setRodSetId(0);
  syncMaps(*h);
  for (unsigned int i = 0; i < 3; i++) {
    place(*h, false, i, 0);
    place(*h, true, i, 1);
  }
  problem_c* p = puz->getProblem(puz->addProblem());
  p->setName("Panex swap");
  for (unsigned int side = 0; side < 2; side++) {
    for (unsigned int size = 3; size >= 1; size--) {
      addSized(*puz, *p, size);
    }
  }
  p->setRodSetId(1);
  syncMaps(*p);
  for (unsigned int i = 0; i < 3; i++) {
    place(*p, false, 3 + i, 0);
    place(*p, false, 6 + i, 2);
    place(*p, true, 6 + i, 0);
    place(*p, true, 3 + i, 2);
  }
  save(*puz, "stack_two_boards");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fputs("usage: stacking_puzzles_old <dir>\n", stderr);
    return 2;
  }
  outDir = argv[1];
  std::filesystem::create_directories(outDir);
  hanoi();
  distance(false);
  distance(true);
  panexJr(true, 1, "stack_panex_jr");
  panexJr(false, 1, "stack_panex_jr_no_pocket");
  panexJr(true, 2, "stack_panex_jr_pocket_2");
  for (unsigned int n = 2; n <= 5; n++) {
    panexSwap(n, false);
  }
  for (unsigned int n = 2; n <= 4; n++) {
    panexSwap(n, true);
  }
  for (int rules = 0; rules < 4; rules++) {
    for (unsigned int n = 3; n <= 6; n++) {
      plainRods(rules, n);
    }
  }
  for (const unsigned int n : {1U, 2U, 7U, 10U}) {
    tower(n);
  }
  nineRods();
  copies();
  sameStartGoal();
  noPath();
  invalid();
  twoBoards();
  return 0;
}

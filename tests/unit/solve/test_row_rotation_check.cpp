// SPDX-License-Identifier: GPL-3.0-or-later
// The rotation check on the rows of the matrix (RowRotationCheck) against
// the one on built assemblies (Assembly::smallerRotationExists): the same
// assemblies in the same order, with the switch on and off, on the fixtures
// and on boxes of pieces made here for what the check has to get right
// (many pieces of one shape, ties between the smallest placements, piece
// ranges, variable voxels, a pivot piece, mirror pairs).
#include "../model/model_test_helpers.h"
#include "solve_test_helpers.h"

#include "shapelogic/base/thread_pool.h"
#include "shapelogic/solve/assembler.h"
#include "shapelogic/solve/assembler_0.h"
#include "shapelogic/solve/assembler_1.h"
#include "shapelogic/solve/solver_type.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace bt;
using test::fingerprint;
using test::Fingerprints;

namespace {

// A shape as layers of rows of '#', '+' (variable) and '.'.
using Art = std::vector<std::vector<std::string>>;

struct Part {
  Art art;
  unsigned int min;
  unsigned int max;
};

unsigned int addArt(Puzzle& puz, const Art& art) {
  const auto sx = static_cast<unsigned int>(art[0][0].size());
  const auto sy = static_cast<unsigned int>(art[0].size());
  const auto sz = static_cast<unsigned int>(art.size());
  const unsigned int id = puz.addShape(sx, sy, sz);
  for (unsigned int z = 0; z < sz; z++) {
    for (unsigned int y = 0; y < sy; y++) {
      for (unsigned int x = 0; x < sx; x++) {
        const char c = art[z][y][x];
        if (c != '.') {
          puz.getShape(id).setState(x, y, z, c == '+' ? Voxel::kVariable : Voxel::kFilled);
        }
      }
    }
  }
  return id;
}

// A problem: fill `result` with the parts.
Puzzle boxPuzzle(const Art& result, const std::vector<Part>& parts) {
  Puzzle puz{GridType{GridKind::Bricks}};
  const unsigned int res = addArt(puz, result);
  const unsigned int prob = puz.addProblem();
  puz.getProblem(prob).setResultId(puz, res);
  for (const Part& p : parts) {
    const unsigned int id = addArt(puz, p.art);
    puz.getProblem(prob).setShapeMaximum(puz, id, p.max);
    puz.getProblem(prob).setShapeMinimum(puz, id, p.min);
  }
  return puz;
}

Art solid(unsigned int x, unsigned int y, unsigned int z) {
  return Art(z, std::vector<std::string>(y, std::string(x, '#')));
}

const Art kDomino = {{"##"}};
const Art kI3 = {{"###"}};
const Art kV = {{"##", "#."}};
const Art kL = {{"###", "#.."}};
const Art kT = {{"###", ".#."}};
const Art kZ = {{"##.", ".##"}};
const Art kO = {{"##", "##"}};
const Art kI4 = {{"####"}};
// the three tetracubes that do not lie flat: the branch, and the two screws,
// which are mirror images of each other
const Art kBranch = {{"##", "#."}, {"#.", ".."}};
const Art kScrewA = {{"##", "#."}, {".#", ".."}};
const Art kScrewB = {{"##", "#."}, {"..", "#."}};

struct Found {
  std::vector<std::string> inOrder;
  bool rowCheck = false;
};

// Every assembly of problem `prob`, in the order found.
Found solve(const Puzzle& p, unsigned int prob, bool rowCheck, bool vector, unsigned int threads,
            ThreadPool* pool, bool reduce = false) {
  std::unique_ptr<Assembler> a = makeAssembler(p, p.getProblem(prob), SolverType::Classic);
  REQUIRE(a->createMatrix({}) == AssemblerError::None);
  if (reduce) {
    (void)a->reduce(StopToken{});
  }
  SearchOptions o = test::searchOptions(threads, pool, vector);
  o.rowRotationCheck = rowCheck;
  Found f;
  const RunEnd end = a->assemble(
      [&f](Assembly asm_) {
        f.inOrder.push_back(fingerprint(asm_));
        return true;
      },
      o, StopToken{});
  REQUIRE(end == RunEnd::Finished);
  if (const auto* a0 = dynamic_cast<const Assembler0*>(a.get())) {
    f.rowCheck = a0->usedRowRotationCheck();
  } else if (const auto* a1 = dynamic_cast<const Assembler1*>(a.get())) {
    f.rowCheck = a1->usedRowRotationCheck();
  }
  return f;
}

// The check on the rows and the check on assemblies give the same
// assemblies in the same order, with the plain and the vector search, and
// the same multiset with the vector search on four threads. Returns the
// number of assemblies.
std::size_t sameWithAndWithout(const Puzzle& p, unsigned int prob, ThreadPool& pool,
                               bool reduce = false) {
  std::size_t count = 0;
  for (const bool vector : {false, true}) {
    INFO((vector ? "vector search" : "plain search"));
    const Found without = solve(p, prob, false, vector, 1, nullptr, reduce);
    const Found with = solve(p, prob, true, vector, 1, nullptr, reduce);
    CHECK_FALSE(without.rowCheck);
    CHECK(with.inOrder == without.inOrder);
    count = without.inOrder.size();

    if (vector) {
      const Found split = solve(p, prob, true, vector, 4, &pool, reduce);
      CHECK(Fingerprints(split.inOrder.begin(), split.inOrder.end()) ==
            Fingerprints(without.inOrder.begin(), without.inOrder.end()));
    }
  }
  return count;
}

}  // namespace

TEST_CASE("Row rotation check: the fixtures' assemblies are the same with and without",
          "[solve][assembler][parallel]") {
  ThreadPool pool(4);
  for (const char* file :
       {"AlPackino.xmpuzzle", "AugmentedSecondStellation.xmpuzzle", "BallRoom.xmpuzzle",
        "Bermuda.xmpuzzle", "CubeInCage.xmpuzzle", "DemoMirrorParadox.xmpuzzle",
        "DemoPieceGenerator.xmpuzzle", "DraculasDentalDesaster.xmpuzzle",
        "FourPieceTetrahedron.xmpuzzle", "PelikanBurr.xmpuzzle", "Prisgon.xmpuzzle",
        "test_rotation_solvers.xmpuzzle", "test_sphere_symmetry.xmpuzzle"}) {
    const Puzzle p = test::loadFixture(file);
    for (unsigned int prob = 0; prob < p.getNumberOfProblems(); prob++) {
      INFO(file << ", problem " << prob);
      (void)sameWithAndWithout(p, prob, pool);
    }
  }
}

namespace {

struct Case {
  const char* name;
  Puzzle puzzle;
  std::size_t assemblies;  ///< 0: any number above none
};

void checkCases(const std::vector<Case>& cases, ThreadPool& pool) {
  for (const Case& c : cases) {
    INFO(c.name);
    const std::size_t count = sameWithAndWithout(c.puzzle, 0, pool);
    if (c.assemblies != 0) {
      CHECK(count == c.assemblies);
    } else {
      CHECK(count > 0);
    }
    CHECK(solve(c.puzzle, 0, true, true, 1, nullptr).rowCheck);
  }
}

}  // namespace

TEST_CASE("Row rotation check: boxes of pieces", "[solve][assembler][parallel]") {
  ThreadPool pool(4);
  std::vector<Case> cases;
  // many pieces of one shape: no symmetry breaker, every cover is checked,
  // and the smallest placements of a cover and its rotation often tie
  cases.push_back({"8 dominoes in 4x4", boxPuzzle(solid(4, 4, 1), {{kDomino, 8, 8}}), 9});
  cases.push_back({"4 V in 2x3x2", boxPuzzle(solid(3, 2, 2), {{kV, 4, 4}}), 0});
  // piece ranges: pieces left out
  cases.push_back({"ranges in 2x2x3",
                   boxPuzzle(solid(3, 2, 2), {{kDomino, 0, 6}, {kV, 0, 4}, {kI3, 0, 4}}), 0});
  // variable voxels: the middle of the tray may stay empty
  cases.push_back(
      {"4 dominoes round a hole", boxPuzzle({{"###", "#+#", "###"}}, {{kDomino, 4, 4}}), 1});
  // a shape without mirror symmetry and no mirror partner: rotations only
  cases.push_back({"two screws make a cube", boxPuzzle(solid(2, 2, 2), {{kScrewA, 2, 2}}), 1});
  // mirror pairs of pieces that are several of their shape: the mirrored
  // transformations go to the assembly's own check
  cases.push_back({"two of each screw in 2x2x4",
                   boxPuzzle(solid(2, 2, 4), {{kScrewA, 2, 2}, {kScrewB, 2, 2}}), 0});
  // assembler 0 with nothing to break the symmetry on but the check
  cases.push_back({"pentominoes in 3x20", test::pentominoPuzzle(20, 3), 2});
  checkCases(cases, pool);
}

// The larger ones: seconds with the plain search.
TEST_CASE("Row rotation check: larger boxes and fixtures",
          "[solve][assembler][parallel][slow]") {
  ThreadPool pool(4);
  std::vector<Case> cases;
  cases.push_back({"12 dominoes in 2x3x4", boxPuzzle(solid(4, 3, 2), {{kDomino, 12, 12}}), 0});
  // several shapes with several pieces each
  cases.push_back({"two of each tetromino in 5x8",
                   boxPuzzle(solid(8, 5, 1),
                             {{kI4, 2, 2}, {kO, 2, 2}, {kT, 2, 2}, {kL, 2, 2}, {kZ, 2, 2}}),
                   783});
  cases.push_back({"ranges in 3x3x2",
                   boxPuzzle(solid(3, 3, 2), {{kDomino, 0, 9}, {kV, 0, 6}, {kI3, 0, 6}}), 0});
  // assembler 0: a pivot piece, and a mirror pair (the Soma cube, 240)
  cases.push_back({"Soma cube",
                   boxPuzzle(solid(3, 3, 3), {{kV, 1, 1},
                                              {kL, 1, 1},
                                              {kT, 1, 1},
                                              {kZ, 1, 1},
                                              {kBranch, 1, 1},
                                              {kScrewA, 1, 1},
                                              {kScrewB, 1, 1}}),
                   240});
  checkCases(cases, pool);

  for (const char* file : {"12PieceSeparation.xmpuzzle", "PiecesOfEight.xmpuzzle"}) {
    const Puzzle p = test::loadFixture(file);
    for (unsigned int prob = 0; prob < p.getNumberOfProblems(); prob++) {
      INFO(file << ", problem " << prob);
      (void)sameWithAndWithout(p, prob, pool);
    }
  }
  // the plain search needs seconds for this one as made: reduced
  const Puzzle p = test::loadFixture("SolidSixPieceBurrs.xmpuzzle");
  (void)sameWithAndWithout(p, 0, pool, true);
}

TEST_CASE("Row rotation check: not used for the complete check or with rotations kept",
          "[solve][assembler]") {
  const Puzzle p = boxPuzzle(solid(4, 4, 1), {{kDomino, 8, 8}});
  for (const bool complete : {false, true}) {
    for (const bool keep : {false, true}) {
      INFO("complete " << complete << ", keep rotations " << keep);
      Assembler1 a(p, p.getProblem(0));
      MatrixOptions m;
      m.completeRotations = complete;
      m.keepRotations = keep;
      REQUIRE(a.createMatrix(m) == AssemblerError::None);
      Assembler1 b(p, p.getProblem(0));
      REQUIRE(b.createMatrix(m) == AssemblerError::None);
      SearchOptions without = test::searchOptions(1, nullptr);
      without.rowRotationCheck = false;
      const test::RunResult ra = test::runOnce(a, test::searchOptions(1, nullptr));
      const test::RunResult rb = test::runOnce(b, without);
      CHECK(ra.found == rb.found);
      CHECK(a.usedRowRotationCheck() == (!complete && !keep));
      CHECK(ra.found.size() == (keep ? 36U : 9U));
    }
  }
}

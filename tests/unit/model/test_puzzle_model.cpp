// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from test/test_puzzle_model.cpp of the original. Left out: the
// PuzzleSolver3D and OpenSCAD import cases (imports come later) and the undo
// cases (stage 6). Assertions that only fired in the original's debug builds
// always fire here (BT_ASSERT is on in every build).

#include "../grid/grid_test_helpers.h"
#include "model_test_helpers.h"

#include "bt/base/assert.h"
#include "bt/io/puzzle_file.h"
#include "bt/model/puzzle.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace bt;
using bt::test::fromLayers;
using bt::test::loadFixture;

namespace {

/// A puzzle with `numShapes` cube shapes sized 1x1x1, 2x2x2, ... (told apart
/// by getX()) and one empty problem.
struct ProblemFixture {
  Puzzle puzzle;
  Problem& problem() { return puzzle.getProblem(0); }
};

ProblemFixture makeProblemFixture(unsigned int numShapes) {
  ProblemFixture f;
  for (unsigned int i = 1; i <= numShapes; i++) {
    f.puzzle.addShape(i, i, i);
  }
  f.puzzle.addProblem();
  return f;
}

/// examples/PelikanBurr.xmpuzzle: one solved problem (12 assemblies, 1
/// solution) with one saved solution carrying a full disassembly.
const Puzzle& pelikan() {
  static const Puzzle p = loadFixture("PelikanBurr.xmpuzzle");
  return p;
}

/// A result shape that is a domino along x, built from two unit cubes (one
/// part, maximum 2): small enough to work out transform() by hand.
struct DominoFixture {
  Puzzle puzzle;
  Problem& problem() { return puzzle.getProblem(0); }
};

DominoFixture makeDominoFixture() {
  DominoFixture f;
  const GridType gt = f.puzzle.getGridType();
  const unsigned int pieceShape = f.puzzle.addShape(fromLayers(gt, {{"#"}}));
  const unsigned int resultShape = f.puzzle.addShape(fromLayers(gt, {{"##"}}));
  f.puzzle.addProblem();
  f.problem().setResultId(f.puzzle, resultShape);
  f.problem().setShapeMaximum(f.puzzle, pieceShape, 2);
  return f;
}

/// The placement order of placement_c::operator<, pivot first, written from
/// the getters only, so the smallerRotationExists cases do not lean on the
/// private compare().
bool isLexSmaller(const Assembly& a, const Assembly& b, unsigned int pivot) {
  auto lessAt = [](const Assembly& x, const Assembly& y, unsigned int i) {
    if (x.getTransformation(i) != y.getTransformation(i)) {
      return x.getTransformation(i) < y.getTransformation(i);
    }
    if (x.getX(i) != y.getX(i)) {
      return x.getX(i) < y.getX(i);
    }
    if (x.getY(i) != y.getY(i)) {
      return x.getY(i) < y.getY(i);
    }
    return x.getZ(i) < y.getZ(i);
  };
  auto equalAt = [](const Assembly& x, const Assembly& y, unsigned int i) {
    return x.getTransformation(i) == y.getTransformation(i) && x.getX(i) == y.getX(i) &&
           x.getY(i) == y.getY(i) && x.getZ(i) == y.getZ(i);
  };

  const unsigned int n = a.placementCount();
  if (pivot < n) {
    if (lessAt(a, b, pivot)) {
      return true;
    }
    if (!equalAt(a, b, pivot)) {
      return false;
    }
  }
  for (unsigned int i = 0; i < n; i++) {
    if (i == pivot) {
      continue;
    }
    if (lessAt(a, b, i)) {
      return true;
    }
    if (!equalAt(a, b, i)) {
      return false;
    }
  }
  return false;
}

/// Whether a self-symmetry of the result maps `a` to a smaller assembly,
/// judged with isLexSmaller instead of the code under test.
bool aSmallerRotationExists(const Assembly& a, const Puzzle& puzzle, const Problem& problem) {
  const Symmetries& sym = puzzle.getGridType().getSymmetries();
  for (unsigned int t = 0; t < sym.getNumTransformations(); t++) {
    if (!sym.symmetrieContainsTransformation(problem.resultShape(puzzle).selfSymmetries(), t)) {
      continue;
    }
    Assembly tmp(a);
    if (!tmp.transform(static_cast<unsigned char>(t), puzzle, problem, nullptr)) {
      continue;
    }
    if (isLexSmaller(tmp, a, 0)) {
      return true;
    }
  }
  return false;
}

/// The index of the first i in [0, count-1) where outOfOrder(i), or count.
template <typename Pred>
unsigned int firstOutOfOrder(unsigned int count, Pred outOfOrder) {
  for (unsigned int i = 0; i + 1 < count; i++) {
    if (outOfOrder(i)) {
      return i;
    }
  }
  return count;
}

/// The index of the first i in [0, count) where fails(i), or count.
template <typename Pred>
unsigned int firstFailing(unsigned int count, Pred fails) {
  for (unsigned int i = 0; i < count; i++) {
    if (fails(i)) {
      return i;
    }
  }
  return count;
}

}  // namespace

// ---- puzzle ---------------------------------------------------------------------

TEST_CASE("puzzle: construction keeps the grid type", "[model][puzzle]") {
  for (const GridKind k : bt::test::kAllGrids) {
    INFO(toString(k));
    const Puzzle p{GridType(k)};
    REQUIRE(p.getGridType().getType() == k);
  }
}

TEST_CASE("puzzle: addShape returns successive indices", "[model][puzzle]") {
  Puzzle p;
  REQUIRE(p.getNumberOfShapes() == 0);
  REQUIRE(p.addShape(1, 1, 1) == 0);
  REQUIRE(p.addShape(2, 2, 2) == 1);
  REQUIRE(p.addShape(3, 3, 3) == 2);
  REQUIRE(p.getNumberOfShapes() == 3);
}

TEST_CASE("puzzle: removeShape renumbers shapes -- a problem's result still names the same shape",
          "[model][puzzle][problem]") {
  Puzzle p;
  const unsigned int first = p.addShape(1, 1, 1);
  p.addShape(1, 1, 1);
  const unsigned int third = p.addShape(1, 1, 1);
  p.getShape(third).setName("third");

  Problem& problem = p.getProblem(p.addProblem());
  problem.setResultId(p, third);
  REQUIRE(problem.getResultId() == 2);

  p.removeShape(first);

  REQUIRE(p.getNumberOfShapes() == 2);
  REQUIRE(p.getProblem(0).getResultId() == 1);
  REQUIRE(p.getShape(p.getProblem(0).getResultId()).getName() == "third");
}

TEST_CASE("puzzle: removeShape of the result shape invalidates the result",
          "[model][puzzle][problem]") {
  // two shapes, so that an uncleared result 0 would still be in range
  Puzzle p;
  const unsigned int target = p.addShape(1, 1, 1);
  const unsigned int bystander = p.addShape(2, 2, 2);
  p.getShape(bystander).setName("bystander");

  p.getProblem(p.addProblem()).setResultId(p, target);
  REQUIRE(p.getProblem(0).resultValid());

  p.removeShape(target);

  REQUIRE(p.getNumberOfShapes() == 1);
  REQUIRE(p.getShape(0).getName() == "bystander");
  REQUIRE_FALSE(p.getProblem(0).resultValid());
}

TEST_CASE("puzzle: exchangeShapes swaps two shapes and leaves the others alone",
          "[model][puzzle]") {
  Puzzle p;
  p.addShape(1, 1, 1);
  p.addShape(2, 2, 2);
  p.addShape(3, 3, 3);
  p.getShape(0).setName("a");
  p.getShape(1).setName("b");
  p.getShape(2).setName("c");

  p.exchangeShapes(0, 2);

  REQUIRE(p.getShape(0).getName() == "c");
  REQUIRE(p.getShape(1).getName() == "b");
  REQUIRE(p.getShape(2).getName() == "a");
}

TEST_CASE("puzzle: addColor assigns successive 0-based ids", "[model][puzzle][color]") {
  Puzzle p;
  REQUIRE(p.colorNumber() == 0);
  REQUIRE(p.addColor(255, 0, 0) == 0);
  REQUIRE(p.addColor(0, 255, 0) == 1);
  REQUIRE(p.addColor(0, 0, 255) == 2);
  REQUIRE(p.colorNumber() == 3);
}

TEST_CASE("puzzle: getColor and changeColor use the 0-based id addColor returned",
          "[model][puzzle][color]") {
  Puzzle p;
  const unsigned int red = p.addColor(255, 0, 0);
  const unsigned int green = p.addColor(0, 255, 0);

  REQUIRE(p.getColor(red) == Rgb{255, 0, 0});
  REQUIRE(p.getColor(green) == Rgb{0, 255, 0});

  p.changeColor(red, 10, 20, 30);
  REQUIRE(p.getColor(red) == Rgb{10, 20, 30});
  REQUIRE(p.getColor(green) == Rgb{0, 255, 0});
}

TEST_CASE("puzzle: removeColor takes a 1-based colour number and renumbers the ones above",
          "[model][puzzle][color]") {
  // addColor/getColor/changeColor use 0-based indices; removeColor, like
  // cells and colour rules, the colour number (index + 1)
  Puzzle p;
  const unsigned int first = p.addColor(255, 0, 0);
  p.addColor(0, 255, 0);
  p.addColor(0, 0, 255);

  p.removeColor(first + 1);

  REQUIRE(p.colorNumber() == 2);
  REQUIRE(p.getColor(0) == Rgb{0, 255, 0});
  REQUIRE(p.getColor(1) == Rgb{0, 0, 255});

  // colour 0 does not exist; the original erased at a wild offset
  REQUIRE_THROWS_AS(p.removeColor(0), InternalError);
}

TEST_CASE("puzzle: removeColor updates cells and colour rules", "[model][puzzle][color]") {
  Puzzle p;
  p.addColor(1, 1, 1);
  p.addColor(2, 2, 2);
  p.addColor(3, 3, 3);
  const unsigned int s = p.addShape(3, 1, 1);
  p.getShape(s).setState(0, 0, 0, Voxel::kFilled);
  p.getShape(s).setState(1, 0, 0, Voxel::kFilled);
  p.getShape(s).setState(2, 0, 0, Voxel::kFilled);
  p.getShape(s).setColor(0, 0, 0, 1);
  p.getShape(s).setColor(1, 0, 0, 2);
  p.getShape(s).setColor(2, 0, 0, 3);
  Problem& pr = p.getProblem(p.addProblem());
  pr.allowPlacement(p, 1, 2);
  pr.allowPlacement(p, 2, 3);
  pr.allowPlacement(p, 3, 3);

  p.removeColor(2);

  CHECK(p.getShape(s).getColor(0) == 1);
  CHECK(p.getShape(s).getColor(1) == 0);
  CHECK(p.getShape(s).getColor(2) == 2);
  const Problem& after = p.getProblem(0);
  CHECK(after.placementAllowed(p, 2, 2));        // was 3 -> 3
  CHECK_FALSE(after.placementAllowed(p, 1, 2));  // 1 -> 2 is gone with colour 2
  CHECK(after.getColorConstraints().size() == 1);
}

TEST_CASE("puzzle: comment and comment-popup flag", "[model][puzzle]") {
  Puzzle p;
  REQUIRE(p.getComment().empty());
  REQUIRE_FALSE(p.getCommentPopup());
  p.setComment("a comment describing the puzzle");
  REQUIRE(p.getComment() == "a comment describing the puzzle");
  p.setCommentPopup(true);
  REQUIRE(p.getCommentPopup());
}

TEST_CASE("puzzle: copies are deep", "[model][puzzle]") {
  Puzzle a = pelikan();
  const Puzzle b = a;
  a.getShape(0).setName("changed");
  a.getProblem(0).removeAllSolutions();
  CHECK(b.getShape(0).getName() != "changed");
  CHECK(b.getProblem(0).getNumberOfSavedSolutions() == 1);
  CHECK(io::writePuzzle(b) == io::writePuzzle(pelikan()));
}

TEST_CASE("puzzle: addProblem copies only the definition", "[model][puzzle][problem]") {
  Puzzle p = pelikan();
  p.getProblem(0).setName("orig");
  p.getProblem(0).setMaxHoles(2);
  const unsigned int c = p.addProblem(p.getProblem(0));
  const Problem& copy = p.getProblem(c);
  CHECK(copy.getName().empty());
  CHECK(copy.getSolveState() == ProblemState::Unsolved);
  CHECK(copy.getNumberOfSavedSolutions() == 0);
  CHECK(copy.getParts() == p.getProblem(0).getParts());
  CHECK(copy.getResultId() == p.getProblem(0).getResultId());
  CHECK(copy.getMaxHoles() == 2);

  // into a puzzle with fewer shapes: what it does not have is dropped
  Puzzle small;
  small.addShape(1, 1, 1);
  small.addShape(1, 1, 1);
  const Problem& moved = small.getProblem(small.addProblem(p.getProblem(0)));
  CHECK(moved.getResultId() == 0);
  CHECK(moved.getNumberOfParts() == 1);  // only the part of shape 1 is left
  CHECK(moved.getShapeIdOfPart(0) == 1);
}

// ---- problem --------------------------------------------------------------------

TEST_CASE("problem: setShapeMinimum/setShapeMaximum and their readers add and describe a part",
          "[model][problem]") {
  ProblemFixture f = makeProblemFixture(2);
  Problem& pr = f.problem();
  const Puzzle& puz = f.puzzle;

  REQUIRE(pr.getShapeMinimum(0) == 0);
  REQUIRE(pr.getShapeMaximum(0) == 0);
  REQUIRE_FALSE(pr.usesShape(0));
  REQUIRE(pr.getNumberOfParts() == 0);

  pr.setShapeMaximum(puz, 0, 3);
  REQUIRE(pr.getShapeMaximum(0) == 3);
  REQUIRE(pr.usesShape(0));
  REQUIRE(pr.getNumberOfParts() == 1);

  pr.setShapeMinimum(puz, 0, 2);
  REQUIRE(pr.getShapeMinimum(0) == 2);
  REQUIRE(pr.getShapeMaximum(0) == 3);

  REQUIRE_FALSE(pr.usesShape(1));

  // the result counts as used, but not as a part
  pr.setResultId(puz, 1);
  REQUIRE(pr.usesShape(1));
  REQUIRE(pr.getShapeMaximum(1) == 0);
  REQUIRE(pr.getNumberOfParts() == 1);

  // a minimum above the maximum raises it, a maximum below the minimum lowers it
  pr.setShapeMinimum(puz, 0, 5);
  REQUIRE(pr.getShapeMaximum(0) == 5);
  pr.setShapeMaximum(puz, 0, 1);
  REQUIRE(pr.getShapeMinimum(0) == 1);

  pr.setShapeMaximum(puz, 0, 0);
  REQUIRE_FALSE(pr.usesShape(0));
  REQUIRE(pr.getNumberOfParts() == 0);

  // a shape that does not exist
  REQUIRE_THROWS_AS(pr.setShapeMaximum(puz, 2, 1), InternalError);
}

TEST_CASE("problem: parts map to shapes by id, not by position", "[model][problem]") {
  ProblemFixture f = makeProblemFixture(3);
  Problem& pr = f.problem();
  pr.setShapeMaximum(f.puzzle, 0, 1);
  pr.setShapeMaximum(f.puzzle, 2, 1);

  REQUIRE(pr.getNumberOfParts() == 2);
  const unsigned int part0 = pr.getPartIdForShape(0);
  const unsigned int part2 = pr.getPartIdForShape(2);
  REQUIRE(part0 != part2);
  REQUIRE(pr.getShapeIdOfPart(part0) == 0);
  REQUIRE(pr.getShapeIdOfPart(part2) == 2);
  REQUIRE(pr.partShape(f.puzzle, part0).getX() == 1);
  REQUIRE(pr.partShape(f.puzzle, part2).getX() == 3);
  REQUIRE_THROWS_AS(pr.getPartIdForShape(1), InternalError);
}

TEST_CASE("problem: pieces are numbered part by part", "[model][problem]") {
  ProblemFixture f = makeProblemFixture(4);
  Problem& pr = f.problem();
  pr.setShapeMaximum(f.puzzle, 0, 2);
  pr.setShapeMaximum(f.puzzle, 2, 3);
  pr.setShapeMaximum(f.puzzle, 3, 1);

  REQUIRE(pr.getNumberOfParts() == 3);
  REQUIRE(pr.getNumberOfPieces() == 6);

  const unsigned int expectedPart[6] = {0, 0, 1, 1, 1, 2};
  const unsigned int expectedIndex[6] = {0, 1, 0, 1, 2, 0};
  const unsigned int expectedSize[6] = {1, 1, 3, 3, 3, 4};
  for (unsigned int piece = 0; piece < 6; piece++) {
    INFO("piece " << piece);
    const unsigned int part = pr.getPartIdToPieceId(piece);
    REQUIRE(part == expectedPart[piece]);
    REQUIRE(pr.getPartIndexToPieceId(piece) == expectedIndex[piece]);
    REQUIRE(pr.partShape(f.puzzle, part).getX() == expectedSize[piece]);
  }
  REQUIRE_THROWS_AS(pr.getPartIdToPieceId(6), InternalError);
}

TEST_CASE("problem: result id round trip, and getResultId asserts when it is unset",
          "[model][problem]") {
  ProblemFixture f = makeProblemFixture(3);
  Problem& pr = f.problem();

  REQUIRE_FALSE(pr.resultValid());
  pr.setResultId(f.puzzle, 1);
  REQUIRE(pr.getResultId() == 1);
  REQUIRE(pr.resultShape(f.puzzle).getX() == 2);
  pr.setResultId(f.puzzle, 2);
  REQUIRE(pr.getResultId() == 2);
  pr.clearResult();
  REQUIRE_FALSE(pr.resultValid());
  REQUIRE_THROWS_AS(pr.getResultId(), InternalError);
}

TEST_CASE("problem: removeShape drops the matching part; the rest still name the right shapes",
          "[model][problem]") {
  ProblemFixture f = makeProblemFixture(4);
  Problem& pr = f.problem();
  pr.setShapeMaximum(f.puzzle, 0, 1);
  pr.setShapeMaximum(f.puzzle, 2, 1);
  pr.setShapeMaximum(f.puzzle, 3, 2);
  pr.setResultId(f.puzzle, 1);

  f.puzzle.removeShape(0);
  const Problem& after = f.puzzle.getProblem(0);

  REQUIRE(f.puzzle.getNumberOfShapes() == 3);
  REQUIRE(after.getNumberOfParts() == 2);
  REQUIRE(after.getResultId() == 0);
  REQUIRE(after.resultShape(f.puzzle).getX() == 2);
  REQUIRE(after.partShape(f.puzzle, after.getPartIdForShape(1)).getX() == 3);
  REQUIRE(after.partShape(f.puzzle, after.getPartIdForShape(2)).getX() == 4);
  REQUIRE(after.getShapeMaximum(2) == 2);
}

TEST_CASE("problem: exchangeShapes swaps which shape each part and the result name",
          "[model][problem]") {
  ProblemFixture f = makeProblemFixture(4);
  Problem& pr = f.problem();
  pr.setShapeMaximum(f.puzzle, 0, 1);
  pr.setShapeMaximum(f.puzzle, 1, 1);
  pr.setShapeMaximum(f.puzzle, 3, 2);
  pr.setResultId(f.puzzle, 2);

  f.puzzle.exchangeShapes(0, 3);
  const Problem& after = f.puzzle.getProblem(0);

  REQUIRE(f.puzzle.getShape(0).getX() == 4);
  REQUIRE(f.puzzle.getShape(3).getX() == 1);
  REQUIRE(after.partShape(f.puzzle, after.getPartIdForShape(3)).getX() == 1);
  REQUIRE(after.getShapeMaximum(3) == 1);
  REQUIRE(after.partShape(f.puzzle, after.getPartIdForShape(0)).getX() == 4);
  REQUIRE(after.getShapeMaximum(0) == 2);
  REQUIRE(after.partShape(f.puzzle, after.getPartIdForShape(1)).getX() == 2);
  REQUIRE(after.getResultId() == 2);
}

TEST_CASE("problem: the saved solution of a loaded puzzle", "[model][problem][solution]") {
  const Problem& pr = pelikan().getProblem(0);
  REQUIRE(pr.getNumberOfSavedSolutions() == 1);
  const Solution& sol = pr.getSavedSolution(0);
  // asmNum="9" and no solNum attribute
  REQUIRE(sol.getAssemblyNumber() == 9);
  REQUIRE(sol.getSolutionNumber() == 0);
  REQUIRE(sol.getDisassembly() != nullptr);
}

TEST_CASE("problem: counts and time are only known once solved", "[model][problem]") {
  const Problem& solved = pelikan().getProblem(0);
  REQUIRE(solved.getSolveState() == ProblemState::Solved);
  REQUIRE(solved.getNumAssemblies() == 12);
  REQUIRE(solved.getNumSolutions() == 1);
  REQUIRE(solved.getUsedTime() == 0);

  ProblemFixture f = makeProblemFixture(1);
  const Problem& fresh = f.problem();
  REQUIRE(fresh.getSolveState() == ProblemState::Unsolved);
  REQUIRE_FALSE(fresh.numAssembliesKnown());
  REQUIRE_FALSE(fresh.numSolutionsKnown());
  REQUIRE_FALSE(fresh.usedTimeKnown());
  REQUIRE_THROWS_AS(fresh.getNumAssemblies(), InternalError);
  REQUIRE_THROWS_AS(fresh.getNumSolutions(), InternalError);
  REQUIRE_THROWS_AS(fresh.getUsedTime(), InternalError);
}

TEST_CASE("problem: editing a solved problem makes its counts unknown", "[model][problem]") {
  Puzzle p = pelikan();
  Problem& pr = p.getProblem(0);
  pr.setShapeMaximum(p, 1, 2);
  CHECK(pr.getSolveState() == ProblemState::Unknown);
  CHECK(pr.getNumAssemblies() == 0);
  // the solutions stay, with an unplaced piece added for the new maximum
  REQUIRE(pr.getNumberOfSavedSolutions() == 1);
  CHECK(pr.getSavedSolution(0).getAssembly().placementCount() == 8);
  CHECK_FALSE(pr.getSavedSolution(0).getAssembly().isPlaced(1));

  // a minimum the solution does not reach removes it
  pr.setShapeMinimum(p, 1, 2);
  CHECK(pr.getNumberOfSavedSolutions() == 0);
}

TEST_CASE("problem: a new part's unplaced pieces go after the others", "[model][problem]") {
  // the original inserted them one part's count too far and asserted
  Puzzle p = pelikan();
  Problem& pr = p.getProblem(0);
  REQUIRE(pr.getNumberOfPieces() == 7);
  pr.setShapeMinimum(p, 0, 2);
  REQUIRE(pr.getNumberOfPieces() == 9);
  CHECK(pr.getSavedSolution(0).getAssembly().placementCount() == 9);
  CHECK_FALSE(pr.getSavedSolution(0).getAssembly().isPlaced(8));
}

TEST_CASE("problem: removeSolution drops exactly the addressed solution",
          "[model][problem][solution]") {
  Puzzle puzzle = loadFixture("SolidSixPieceBurrs.xmpuzzle");
  Problem& pr = puzzle.getProblem(0);
  const unsigned int before = pr.getNumberOfSavedSolutions();
  REQUIRE(before > 5);
  const unsigned int survivor = pr.getSavedSolution(5).getAssemblyNumber();
  pr.removeSolution(4);
  REQUIRE(pr.getNumberOfSavedSolutions() == before - 1);
  REQUIRE(pr.getSavedSolution(4).getAssemblyNumber() == survivor);
}

TEST_CASE("problem: removeAllSolutions resets solving to Unsolved", "[model][problem][solution]") {
  Puzzle puzzle = loadFixture("SolidSixPieceBurrs.xmpuzzle");
  Problem& pr = puzzle.getProblem(0);
  REQUIRE(pr.getNumberOfSavedSolutions() > 0);
  REQUIRE(pr.getSolveState() == ProblemState::Solved);

  pr.removeAllSolutions();

  REQUIRE(pr.getNumberOfSavedSolutions() == 0);
  REQUIRE(pr.getSolveState() == ProblemState::Unsolved);
  REQUIRE_FALSE(pr.numAssembliesKnown());
}

TEST_CASE("problem: sortSolutions(0) orders by assembly number", "[model][problem][solution]") {
  Puzzle puzzle = loadFixture("SolidSixPieceBurrs.xmpuzzle");
  Problem& pr = puzzle.getProblem(0);
  const unsigned int n = pr.getNumberOfSavedSolutions();
  auto descends = [&pr](unsigned int i) {
    return pr.getSavedSolution(i).getAssemblyNumber() >
           pr.getSavedSolution(i + 1).getAssemblyNumber();
  };
  REQUIRE(firstOutOfOrder(n, descends) < n);  // not sorted to begin with
  pr.sortSolutions(0);
  REQUIRE(firstOutOfOrder(n, descends) == n);
}

TEST_CASE("problem: sortSolutions(1) orders by level", "[model][problem][solution]") {
  Puzzle puzzle = loadFixture("SolidSixPieceBurrs.xmpuzzle");
  Problem& pr = puzzle.getProblem(0);
  const unsigned int n = pr.getNumberOfSavedSolutions();
  REQUIRE(firstFailing(n, [&pr](unsigned int i) {
            return pr.getSavedSolution(i).getDisassemblyInfo() == nullptr;
          }) == n);
  auto descends = [&pr](unsigned int i) {
    return pr.getSavedSolution(i).getDisassemblyInfo()->compare(
               *pr.getSavedSolution(i + 1).getDisassemblyInfo()) > 0;
  };
  REQUIRE(firstOutOfOrder(n, descends) < n);
  pr.sortSolutions(1);
  REQUIRE(firstOutOfOrder(n, descends) == n);
}

TEST_CASE("problem: sortSolutions(2) orders by moves", "[model][problem][solution]") {
  Puzzle puzzle = loadFixture("SolidSixPieceBurrs.xmpuzzle");
  Problem& pr = puzzle.getProblem(0);
  const unsigned int n = pr.getNumberOfSavedSolutions();
  // the file is already in move order: scramble it by assembly number first
  pr.sortSolutions(0);
  auto descends = [&pr](unsigned int i) {
    return pr.getSavedSolution(i).getDisassemblyInfo()->sumMoves() >
           pr.getSavedSolution(i + 1).getDisassemblyInfo()->sumMoves();
  };
  REQUIRE(firstOutOfOrder(n, descends) < n);
  pr.sortSolutions(2);
  REQUIRE(firstOutOfOrder(n, descends) == n);
}

TEST_CASE("problem: sortSolutions(3) orders by the pieces used", "[model][problem][solution]") {
  Puzzle puzzle = loadFixture("SolidSixPieceBurrs.xmpuzzle");
  Problem& pr = puzzle.getProblem(0);
  const unsigned int n = pr.getNumberOfSavedSolutions();
  auto laterBelongsBefore = [&pr](unsigned int i) {
    return pr.getSavedSolution(i + 1).getAssembly().comparePieces(
               pr.getSavedSolution(i).getAssembly()) > 0;
  };
  REQUIRE(firstOutOfOrder(n, laterBelongsBefore) < n);
  pr.sortSolutions(3);
  REQUIRE(firstOutOfOrder(n, laterBelongsBefore) == n);
}

TEST_CASE("problem: strict colour placement requires an exact colour match",
          "[model][puzzle][color]") {
  Puzzle puzzle;
  puzzle.addShape(1, 1, 1);
  Problem& pr = puzzle.getProblem(puzzle.addProblem());
  puzzle.addColor(255, 0, 0);
  puzzle.addColor(0, 255, 0);
  pr.allowPlacement(puzzle, 1, 1);
  pr.allowPlacement(puzzle, 1, 2);

  REQUIRE(pr.placementAllowed(puzzle, 0, 0));
  REQUIRE(pr.placementAllowed(puzzle, 1, 0));
  REQUIRE(pr.placementAllowed(puzzle, 0, 1));
  REQUIRE(pr.placementAllowed(puzzle, 1, 1));
  REQUIRE(pr.placementAllowed(puzzle, 1, 2));
  REQUIRE_FALSE(pr.placementAllowed(puzzle, 2, 1));

  REQUIRE(pr.placementAllowed(puzzle, 0, 0, true));
  REQUIRE(pr.placementAllowed(puzzle, 1, 1, true));
  REQUIRE(pr.placementAllowed(puzzle, 2, 2, true));
  REQUIRE_FALSE(pr.placementAllowed(puzzle, 1, 0, true));
  REQUIRE_FALSE(pr.placementAllowed(puzzle, 0, 1, true));
  REQUIRE_FALSE(pr.placementAllowed(puzzle, 1, 2, true));
  REQUIRE_FALSE(pr.placementAllowed(puzzle, 2, 1, true));
}

TEST_CASE("problem: Time used survives a save and load, to the millisecond", "[model][problem]") {
  Puzzle p = pelikan();
  Problem& pr = p.getProblem(0);
  pr.removeAllSolutions();
  pr.markSolving();
  pr.addTimeMs(250);
  pr.finishedSolving();
  REQUIRE(pr.getUsedMs() == 250);

  const std::string saved = io::writePuzzle(p);
  {
    const auto loaded = io::parsePuzzle(saved);
    REQUIRE(loaded.has_value());
    CHECK(loaded->getProblem(0).getUsedMs() == 250);
    CHECK(loaded->getProblem(0).getUsedTime() == 0);
  }

  // an older file has only the seconds
  std::string old = saved;
  const std::size_t at = old.find(" timeMs=\"");
  REQUIRE(at != std::string::npos);
  old.erase(at, old.find('"', at + 9) + 1 - at);
  const std::size_t t = old.find("time=\"0\"");
  REQUIRE(t != std::string::npos);
  old.replace(t, 8, "time=\"7\"");
  const auto loaded = io::parsePuzzle(old);
  REQUIRE(loaded.has_value());
  CHECK(loaded->getProblem(0).getUsedMs() == 7000);
  CHECK(loaded->getProblem(0).getUsedTime() == 7);
}

TEST_CASE("problem: keepSolutionsSorted places new solutions as a full sort would",
          "[model][problem][solution]") {
  Puzzle incPuzzle = loadFixture("SolidSixPieceBurrs.xmpuzzle");
  Puzzle refPuzzle = loadFixture("SolidSixPieceBurrs.xmpuzzle");
  Problem& inc = incPuzzle.getProblem(0);
  Problem& ref = refPuzzle.getProblem(0);
  REQUIRE(inc.getNumberOfSavedSolutions() > 100);
  inc.markSolving();
  ref.markSolving();
  inc.keepSolutionsSorted(0);

  // copies of the file's solutions, from ref, whose first n0 never move
  const unsigned int n0 = ref.getNumberOfSavedSolutions();
  auto addCopies = [&ref, n0](Problem& pr, unsigned int count, unsigned int seed) {
    for (unsigned int i = 0; i < count; i++) {
      const Solution& s = ref.getSavedSolution((seed + i * 37) % n0);
      pr.addSolution(s.getAssembly(), (seed * 7 + i * 13) % 500);
    }
  };

  // short runs take the insert path, long ones the merge path
  for (unsigned int round = 0; round < 6; round++) {
    const unsigned int count = (round % 2) != 0 ? 100 : 3;
    addCopies(inc, count, round);
    addCopies(ref, count, round);
    inc.keepSolutionsSorted(0);
  }
  ref.sortSolutions(0);

  const unsigned int n = ref.getNumberOfSavedSolutions();
  REQUIRE(inc.getNumberOfSavedSolutions() == n);
  CHECK(firstFailing(n, [&inc, &ref](unsigned int i) {
          const Solution& a = inc.getSavedSolution(i);
          const Solution& b = ref.getSavedSolution(i);
          return a.getAssemblyNumber() != b.getAssemblyNumber() ||
                 a.getAssembly() != b.getAssembly();
        }) == n);
}

TEST_CASE("problem: keepSolutionsSorted stays sorted across removals",
          "[model][problem][solution]") {
  Puzzle puzzle = loadFixture("SolidSixPieceBurrs.xmpuzzle");
  Problem& pr = puzzle.getProblem(0);
  pr.markSolving();
  pr.keepSolutionsSorted(0);

  for (unsigned int round = 0; round < 6; round++) {
    const unsigned int n = pr.getNumberOfSavedSolutions();
    for (unsigned int i = 0; i < 5; i++) {
      pr.addSolution(Assembly(pr.getSavedSolution((round + i * 41) % n).getAssembly()),
                     (round * 31 + i * 97) % 400);
    }
    pr.removeSolution(round * 11);
    pr.removeSolution(pr.getNumberOfSavedSolutions() - 2);
    pr.keepSolutionsSorted(0);
  }

  const unsigned int n = pr.getNumberOfSavedSolutions();
  CHECK(firstOutOfOrder(n, [&pr](unsigned int i) {
          return pr.getSavedSolution(i).getAssemblyNumber() >
                 pr.getSavedSolution(i + 1).getAssemblyNumber();
        }) == n);
}

TEST_CASE("problem: groups", "[model][problem]") {
  ProblemFixture f = makeProblemFixture(1);
  Problem& pr = f.problem();
  pr.setShapeMaximum(f.puzzle, 0, 3);
  pr.setPartGroup(0, 2, 1);
  pr.setPartGroup(0, 3, 2);
  pr.setPartGroup(0, 0, 5);  // group 0 is not stored
  REQUIRE(pr.getNumberOfPartGroups(0) == 2);
  CHECK(pr.getPartGroupId(0, 1) == 3);
  CHECK(pr.getPartGroupCount(0, 1) == 2);
  pr.setPartGroup(0, 2, 0);  // count 0 removes
  REQUIRE(pr.getNumberOfPartGroups(0) == 1);
  CHECK(pr.getPartGroupId(0, 0) == 3);
}

TEST_CASE("problem: exchangeParts renumbers the pieces of the solutions",
          "[model][problem][solution]") {
  Puzzle p = pelikan();
  Problem& pr = p.getProblem(0);
  const Assembly before = pr.getSavedSolution(0).getAssembly();
  pr.exchangeParts(1, 2);
  const Assembly& after = pr.getSavedSolution(0).getAssembly();
  CHECK(pr.getShapeIdOfPart(1) == 3);
  CHECK(pr.getShapeIdOfPart(2) == 2);
  CHECK(after.getX(1) == before.getX(2));
  CHECK(after.getX(2) == before.getX(1));
  CHECK(pr.getSavedSolution(0).getDisassembly()->movesText() ==
        pelikan().getProblem(0).getSavedSolution(0).getDisassembly()->movesText());
  // only neighbouring parts
  REQUIRE_THROWS_AS(pr.exchangeParts(0, 2), InternalError);
}

// ---- solution -------------------------------------------------------------------

TEST_CASE("solution: holds the assembly and the disassembly it was given", "[model][solution]") {
  const Solution& src = pelikan().getProblem(0).getSavedSolution(0);
  REQUIRE(src.getDisassembly() != nullptr);

  const Solution sol(src.getAssembly(), 7, *src.getDisassembly(), 3);
  REQUIRE(sol.getAssembly() == src.getAssembly());
  REQUIRE(sol.getDisassembly() != nullptr);
  REQUIRE(sol.getDisassembly()->movesText() == src.getDisassembly()->movesText());
  REQUIRE(sol.getAssemblyNumber() == 7);
  REQUIRE(sol.getSolutionNumber() == 3);
}

TEST_CASE("solution: setDisassembly replaces the tree, leaving the assembly", "[model][solution]") {
  const Solution& src = pelikan().getProblem(0).getSavedSolution(0);
  Solution sol(src.getAssembly(), 0, SeparationInfo(*src.getDisassembly()), 0);
  REQUIRE(sol.getDisassembly() == nullptr);
  REQUIRE(sol.getSeparationInfo() != nullptr);

  sol.setDisassembly(*src.getDisassembly());
  REQUIRE(sol.getDisassembly() != nullptr);
  REQUIRE(sol.getSeparationInfo() == nullptr);
  REQUIRE(sol.getAssembly() == src.getAssembly());
}

TEST_CASE("solution: removeDisassembly keeps a summary", "[model][solution]") {
  const Solution& src = pelikan().getProblem(0).getSavedSolution(0);
  Solution sol(src.getAssembly(), 0, *src.getDisassembly(), 0);
  const std::string text = sol.getDisassemblyInfo()->movesText();

  sol.removeDisassembly();

  REQUIRE(sol.getDisassembly() == nullptr);
  REQUIRE(sol.getDisassemblyInfo() != nullptr);
  REQUIRE(sol.getDisassemblyInfo()->movesText() == text);
  REQUIRE(sol.getAssembly() == src.getAssembly());
}

// ---- assembly -------------------------------------------------------------------

TEST_CASE("assembly: placements report back exactly", "[model][assembly]") {
  Assembly a{GridType{}};
  REQUIRE(a.placementCount() == 0);
  a.addPlacement(3, 10, -2, 7);
  a.addNonPlacement();
  a.addPlacement(5, 0, 0, 0);

  REQUIRE(a.placementCount() == 3);
  REQUIRE(a.isPlaced(0));
  REQUIRE(a.getTransformation(0) == 3);
  REQUIRE(a.getX(0) == 10);
  REQUIRE(a.getY(0) == -2);
  REQUIRE(a.getZ(0) == 7);
  REQUIRE_FALSE(a.isPlaced(1));
  REQUIRE(a.getTransformation(2) == 5);

  // the getters assert that the piece is placed
  REQUIRE_THROWS_AS(a.getX(1), InternalError);
  REQUIRE_THROWS_AS(a.getTransformation(1), InternalError);
  // and addPlacement that the transformation is a rotation
  REQUIRE_THROWS_AS(a.addPlacement(24, 0, 0, 0), InternalError);
}

TEST_CASE("assembly: comparePieces sorts placed pieces before unplaced ones",
          "[model][assembly]") {
  Assembly full{GridType{}};
  full.addPlacement(0, 0, 0, 0);
  full.addPlacement(0, 1, 0, 0);
  Assembly partial{GridType{}};
  partial.addPlacement(0, 0, 0, 0);
  partial.addNonPlacement();

  REQUIRE(full.comparePieces(partial) == 1);
  REQUIRE(partial.comparePieces(full) == -1);
  REQUIRE(full.comparePieces(full) == 0);
}

TEST_CASE("assembly: transform matches the geometry worked out by hand", "[model][assembly]") {
  DominoFixture f = makeDominoFixture();

  Assembly a(f.puzzle.getGridType());
  a.addPlacement(0, 1, 0, 0);
  a.addPlacement(0, 0, 0, 0);

  /* transformation 10 is a half turn about z: the domino maps onto itself
   * with its cells swapped; its hotspot moves by (1,0,0), so each piece is
   * rotated and shifted by (1,0,0): x=1 -> 0, x=0 -> 1 */
  REQUIRE(a.transform(10, f.puzzle, f.problem(), nullptr));

  REQUIRE(a.getTransformation(0) == 0);
  REQUIRE(a.getX(0) == 0);
  REQUIRE(a.getY(0) == 0);
  REQUIRE(a.getZ(0) == 0);
  REQUIRE(a.getTransformation(1) == 0);
  REQUIRE(a.getX(1) == 1);
  REQUIRE(a.getY(1) == 0);
  REQUIRE(a.getZ(1) == 0);
}

TEST_CASE("assembly: smallerRotationExists is false for the ordinary form", "[model][assembly]") {
  DominoFixture f = makeDominoFixture();
  Assembly a(f.puzzle.getGridType());
  a.addPlacement(0, 0, 0, 0);
  a.addPlacement(0, 1, 0, 0);

  REQUIRE_FALSE(aSmallerRotationExists(a, f.puzzle, f.problem()));
  REQUIRE_FALSE(a.smallerRotationExists(f.puzzle, f.problem(), 0, nullptr, false));
  REQUIRE_FALSE(a.smallerRotationExists(f.puzzle, f.problem(), 0, nullptr, true));
}

TEST_CASE("assembly: smallerRotationExists is true when the cell swap gives a smaller one",
          "[model][assembly]") {
  DominoFixture f = makeDominoFixture();
  Assembly a(f.puzzle.getGridType());
  a.addPlacement(0, 1, 0, 0);
  a.addPlacement(0, 0, 0, 0);

  REQUIRE(aSmallerRotationExists(a, f.puzzle, f.problem()));
  REQUIRE(a.smallerRotationExists(f.puzzle, f.problem(), 0, nullptr, false));
  REQUIRE(a.smallerRotationExists(f.puzzle, f.problem(), 0, nullptr, true));
}

TEST_CASE("assembly: sort orders each part's placements, leaving other parts alone",
          "[model][assembly]") {
  ProblemFixture f = makeProblemFixture(2);
  Problem& pr = f.problem();
  pr.setShapeMaximum(f.puzzle, 0, 3);
  pr.setShapeMaximum(f.puzzle, 1, 1);

  Assembly a(f.puzzle.getGridType());
  a.addPlacement(0, 5, 0, 0);
  a.addPlacement(0, 1, 0, 0);
  a.addPlacement(0, 3, 0, 0);
  a.addPlacement(0, 9, 0, 0);

  a.sort(pr);

  REQUIRE(a.getX(0) == 1);
  REQUIRE(a.getX(1) == 3);
  REQUIRE(a.getX(2) == 5);
  REQUIRE(a.getX(3) == 9);
}

TEST_CASE("assembly: exchangeShape swaps exactly two placements", "[model][assembly]") {
  Assembly a{GridType{}};
  a.addPlacement(1, 2, 3, 4);
  a.addNonPlacement();
  a.addPlacement(5, 6, 7, 8);

  a.exchangeShape(0, 1);

  REQUIRE_FALSE(a.isPlaced(0));
  REQUIRE(a.getTransformation(1) == 1);
  REQUIRE(a.getX(1) == 2);
  REQUIRE(a.getTransformation(2) == 5);
  REQUIRE(a.getX(2) == 6);
}

TEST_CASE("assembly: removePieces and addNonPlacedPieces", "[model][assembly]") {
  Assembly a{GridType{}};
  a.addPlacement(0, 100, 0, 0);
  a.addNonPlacement();
  a.addNonPlacement();
  a.addNonPlacement();
  a.addPlacement(0, 200, 0, 0);

  a.removePieces(1, 3);
  REQUIRE(a.placementCount() == 2);
  REQUIRE(a.getX(0) == 100);
  REQUIRE(a.getX(1) == 200);

  a.addNonPlacedPieces(1, 2);
  REQUIRE(a.placementCount() == 4);
  REQUIRE_FALSE(a.isPlaced(1));
  REQUIRE_FALSE(a.isPlaced(2));
  REQUIRE(a.getX(3) == 200);

  // a placed piece cannot be removed
  REQUIRE_THROWS_AS(a.removePieces(0, 1), InternalError);
}

TEST_CASE("assembly: createSpace matches the result shape cell for cell", "[model][assembly]") {
  const Puzzle& p = pelikan();
  const Problem& pr = p.getProblem(0);
  const std::unique_ptr<Voxel> assembled = pr.getSavedSolution(0).getAssembly().createSpace(p, pr);
  const Voxel& result = pr.resultShape(p);

  REQUIRE(assembled->getX() == result.getX());
  REQUIRE(assembled->getY() == result.getY());
  REQUIRE(assembled->getZ() == result.getZ());

  // filled must be filled, empty empty; a variable cell may go either way
  for (unsigned int z = 0; z < result.getZ(); z++) {
    for (unsigned int y = 0; y < result.getY(); y++) {
      for (unsigned int x = 0; x < result.getX(); x++) {
        INFO("cell " << x << "," << y << "," << z);
        if (result.getState(x, y, z) == Voxel::kFilled) {
          REQUIRE(assembled->getState(x, y, z) == Voxel::kFilled);
        } else if (result.getState(x, y, z) == Voxel::kEmpty) {
          REQUIRE(assembled->getState(x, y, z) == Voxel::kEmpty);
        }
      }
    }
  }
}

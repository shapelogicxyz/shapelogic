// SPDX-License-Identifier: GPL-3.0-or-later
// The take-apart of the grids whose movement checks are derived from their
// cells (decision 0025): rhombic, tetra-octa, hexagonal and cairo. The
// original takes none of these apart, so there are no answers of its to
// compare with. In their place:
//  - every take-apart found is played back on the cells themselves
//    (replayError: no piece passes through another);
//  - every node of every take-apart has as many moves as a search written
//    for these tests needs at least (ReferenceTakeApart), and an assembly
//    said not to come apart comes apart for that search neither. On the
//    examples the search asks the movement cache (which the tests of
//    test_movement_checks.cpp hold against the cells), on random puzzles
//    the cells themselves;
//  - cubes and triangular prisms come apart with the derived checks as
//    they do with the original's tables.
// New.
#include "../model/model_test_helpers.h"
#include "movement_test_helpers.h"
#include "solve_test_helpers.h"

#include "shapelogic/grid/grid_type.h"
#include "shapelogic/model/assembly.h"
#include "shapelogic/model/disassembly.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/solve/assembler.h"
#include "shapelogic/solve/disassembler.h"
#include "shapelogic/solve/movement_cache.h"
#include "shapelogic/solve/solver_type.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <vector>

using namespace bt;

namespace {

// What taking every assembly of a problem apart gives: the counts, and
// each solution as its assembly and its levels.
struct Found {
  unsigned int assemblies = 0;
  unsigned int solutions = 0;
  std::multiset<std::string> levels;
  std::multiset<std::string> saved;
  /// Nodes of take-aparts and assemblies that do not come apart that the
  /// reference search was asked about (and agreed on, or the test failed).
  unsigned int nodesChecked = 0;
  unsigned int stuckChecked = 0;
  /// Assemblies the reference search gave up on (pieces that lie loose).
  unsigned int gaveUp = 0;
  /// How many take-aparts need so many moves until the first piece (or
  /// part) is off.
  std::map<unsigned int, unsigned int> firstMoves;

  [[nodiscard]] bool sameAnswers(const Found& o) const {
    return assemblies == o.assemblies && solutions == o.solutions && levels == o.levels &&
           saved == o.saved;
  }
};

// What a take-apart is held against, besides the playback.
enum class Against {
  Playback,  // nothing more
  Cache,     // the reference search on the movement cache's values
  Cells,     // the reference search on the cells themselves
};

// A reference search that asks the movement cache what two pieces of the
// assembly allow each other.
test::ReferenceTakeApart referenceOnCache(MovementCache& cache, const Assembly& a) {
  const unsigned int dirs = cache.numDirections();
  test::ReferenceTakeApart ref(
      dirs, [&cache, &a, dirs](unsigned int p1, unsigned int p2, const test::Step& between,
                               unsigned int dir, bool against) {
        std::vector<unsigned int> values(dirs);
        if (against) {
          // the first against the direction is the second along it
          cache.getMoValue(-between[0], -between[1], -between[2], a.getTransformation(p2),
                           a.getTransformation(p1), p2, p1, values.data());
        } else {
          cache.getMoValue(between[0], between[1], between[2], a.getTransformation(p1),
                           a.getTransformation(p2), p1, p2, values.data());
        }
        return values[dir];
      });
  std::vector<test::Step> steps(dirs);
  for (unsigned int d = 0; d < dirs; d++) {
    cache.getDirection(d, &steps[d][0], &steps[d][1], &steps[d][2]);
  }
  ref.setSteps(std::move(steps));
  return ref;
}

// Holds what the disassembler said of an assembly against a reference
// search: each node's moves are the fewest that separate its pieces, and
// an assembly without a take-apart has none.
void checkAgainst(test::ReferenceTakeApart& ref, const Assembly& a, const TakeApartResult& r,
                  Found& found) {
  if (!r.separation) {
    const bool apart = ref.comesApart(test::partOf(a));
    if (ref.gaveUp()) {
      found.gaveUp++;
    } else {
      CHECK_FALSE(apart);
      found.stuckChecked++;
    }
    return;
  }
  bool gaveUp = false;
  test::eachNode(*r.separation, [&](const Separation& node) {
    // no further than the node's own moves: one more would say nothing
    const std::optional<unsigned int> fewest =
        ref.movesToSeparate(test::partOf(node), node.getMoves());
    if (ref.gaveUp()) {
      gaveUp = true;
      return;
    }
    REQUIRE(fewest.has_value());
    CHECK(*fewest == node.getMoves());
    found.nodesChecked++;
  });
  found.gaveUp += gaveUp ? 1 : 0;
}

// Takes every assembly apart and plays each take-apart back on the cells
// (not with turns: the playback knows none). With maxAssemblies, only so
// many of them.
Found takeAllApart(const Puzzle& p, unsigned int problemNumber, const DisassemblerOptions& options,
                   Against against = Against::Playback, bool groups = false,
                   unsigned int maxAssemblies = 0) {
  const Problem& problem = p.getProblem(problemNumber);
  REQUIRE(Disassembler::canDisassemble(p));
  std::unique_ptr<Assembler> assm = makeAssembler(p, problem, SolverType::Classic);
  REQUIRE(assm->createMatrix({}) == AssemblerError::None);

  // a cache of the test's own: the disassembler's is not to be asked
  // from outside
  const std::unique_ptr<MovementCache> cache =
      makeMovementCache(p, problem, options.analysis.derivedChecks);
  REQUIRE(cache != nullptr);
  test::CellWorld w(p.getGridType().getType());

  Disassembler disasm(p, problem, options);
  Found found;
  const RunEnd end = assm->assemble(
      [&](const Assembly& a) {
        found.assemblies++;
        const TakeApartResult r = disasm.disassemble(a);
        CHECK_FALSE(r.stopped);
        INFO(test::fingerprint(a) << (r.separation ? " " + r.separation->movesText() : ""));
        if (r.separation) {
          found.solutions++;
          found.levels.insert(r.separation->movesText());
          found.saved.insert(test::fingerprint(a) + " " + r.separation->movesText());
          found.firstMoves[r.separation->getMoves()]++;
          if (!options.rotations) {
            const std::optional<std::string> wrong =
                test::replayError(p, problem, a, *r.separation, groups);
            if (wrong) {
              FAIL_CHECK(*wrong);
            }
          }
        }
        if (against == Against::Cache) {
          test::ReferenceTakeApart ref = referenceOnCache(*cache, a);
          checkAgainst(ref, a, r, found);
        } else if (against == Against::Cells) {
          const std::vector<test::Cells> cells = test::placedCells(p, problem, a);
          test::ReferenceTakeApart ref = test::referenceOnCells(w, cells, a);
          checkAgainst(ref, a, r, found);
        }
        return maxAssemblies == 0 || found.assemblies < maxAssemblies;
      },
      SearchOptions{}, StopToken{});
  CHECK(end == (maxAssemblies != 0 && found.assemblies == maxAssemblies ? RunEnd::Stopped
                                                                        : RunEnd::Finished));
  return found;
}

struct Example {
  const char* file;
  unsigned int problem;
  unsigned int assemblies;
  unsigned int solutions;
  std::multiset<std::string> levels;
};

void checkExample(const Example& e) {
  INFO(e.file << " problem " << e.problem);
  const Puzzle p = test::loadFixture(e.file);
  const Found found = takeAllApart(p, e.problem, {}, Against::Cache);
  CHECK(found.assemblies == e.assemblies);
  CHECK(found.solutions == e.solutions);
  CHECK(found.levels == e.levels);
  // the reference search was asked about every assembly
  CHECK(found.gaveUp == 0);
  CHECK(found.stuckChecked == e.assemblies - e.solutions);
  CHECK(found.nodesChecked >= e.solutions);
}

std::multiset<std::string> times(unsigned int n, const std::string& levels) {
  std::multiset<std::string> out;
  for (unsigned int i = 0; i < n; i++) {
    out.insert(levels);
  }
  return out;
}

// The size of the box the random puzzles of a grid are cut from (some
// tens of cells), or the larger one the cages are made of.
std::array<int, 3> boxOf(GridKind kind, bool cage) {
  switch (kind) {
    case GridKind::Bricks:
      return cage ? std::array<int, 3>{5, 5, 5} : std::array<int, 3>{3, 3, 3};
    case GridKind::TriangularPrism:
      return cage ? std::array<int, 3>{6, 6, 3} : std::array<int, 3>{4, 4, 2};
    case GridKind::Rhombic:
      return cage ? std::array<int, 3>{15, 15, 15} : std::array<int, 3>{5, 5, 5};
    case GridKind::TetraOcta:
      return cage ? std::array<int, 3>{9, 9, 9} : std::array<int, 3>{6, 6, 6};
    case GridKind::Hexagonal:
      return cage ? std::array<int, 3>{4, 4, 3} : std::array<int, 3>{3, 3, 2};
    case GridKind::Cairo:
      return cage ? std::array<int, 3>{8, 8, 3} : std::array<int, 3>{4, 4, 2};
    default:
      FAIL("no random puzzles for this grid");
      return {};
  }
}

// A puzzle of the given pieces (cells within the box), each once; its
// result is what they fill together.
Puzzle puzzleOf(GridKind kind, const std::array<int, 3>& box,
                const std::vector<test::Cells>& pieces) {
  Puzzle puz{GridType{kind}};
  test::Cells all;
  for (const test::Cells& piece : pieces) {
    all.insert(all.end(), piece.begin(), piece.end());
  }
  const unsigned int result = test::addShapeOf(puz, box[0], box[1], box[2], all);
  const unsigned int prob = puz.addProblem();
  puz.getProblem(prob).setResultId(puz, result);
  for (const test::Cells& piece : pieces) {
    const unsigned int id = test::addShapeOf(puz, box[0], box[1], box[2], piece);
    puz.getProblem(prob).setShapeMaximum(puz, id, 1);
    puz.getProblem(prob).setShapeMinimum(puz, id, 1);
  }
  return puz;
}

double distance2(test::CellWorld& w, mesh::CellAt a, mesh::CellAt b) {
  const std::array<double, 3> p = w.cell(a).centre;
  const std::array<double, 3> q = w.cell(b).centre;
  return (p[0] - q[0]) * (p[0] - q[0]) + (p[1] - q[1]) * (p[1] - q[1]) +
         (p[2] - q[2]) * (p[2] - q[2]);
}

// A box of cells cut into `count` pieces: each cell goes to the nearest of
// `count` cells picked at random (nearest by a distance stretched at
// random, which gives the pieces arms that hook into each other).
Puzzle cutPuzzle(test::CellWorld& w, GridKind kind, unsigned int count, std::mt19937& rng) {
  const std::array<int, 3> box = boxOf(kind, false);
  const test::Cells all = test::cellsInBox(w.voxel(), box[0], box[1], box[2]);
  REQUIRE(all.size() > 2 * count);
  for (;;) {
    std::vector<mesh::CellAt> seeds;
    for (unsigned int i = 0; i < count; i++) {
      seeds.push_back(all[rng() % all.size()]);
    }
    std::vector<test::Cells> pieces(count);
    for (const mesh::CellAt& c : all) {
      std::size_t best = 0;
      double bestDistance = 0;
      for (std::size_t i = 0; i < count; i++) {
        const double d = distance2(w, c, seeds[i]) * (1.0 + static_cast<double>(rng() % 100) / 50.0);
        if (i == 0 || d < bestDistance) {
          best = i;
          bestDistance = d;
        }
      }
      pieces[best].push_back(c);
    }
    if (std::ranges::none_of(pieces, [](const test::Cells& p) { return p.empty(); })) {
      return puzzleOf(kind, box, pieces);
    }
  }
}

// A puzzle of two pieces: a small one (a cell near the middle of a box
// and up to two of the cells nearest to it) inside the box around it.
// With `burrow`, the box is whole but for a way the small piece has dug:
// from the middle a step, then legs of one or two, each in another
// direction of the grid than the one before, and every cell it passed through is gone (it may
// find a shorter way out, or none when the legs end inside). Without, about three cells in four of the
// box are kept, wherever they are.
Puzzle cagePuzzle(test::CellWorld& w, GridKind kind, bool burrow, std::mt19937& rng) {
  const std::array<int, 3> box = boxOf(kind, true);
  test::Cells all = test::cellsInBox(w.voxel(), box[0], box[1], box[2]);
  std::array<double, 3> middle{};
  for (const mesh::CellAt& c : all) {
    for (std::size_t k = 0; k < 3; k++) {
      middle[k] += w.cell(c).centre[k] / static_cast<double>(all.size());
    }
  }
  const auto fromMiddle = [&](const mesh::CellAt& c) {
    const std::array<double, 3> p = w.cell(c).centre;
    return (p[0] - middle[0]) * (p[0] - middle[0]) + (p[1] - middle[1]) * (p[1] - middle[1]) +
           (p[2] - middle[2]) * (p[2] - middle[2]);
  };
  std::ranges::stable_sort(all, [&](const mesh::CellAt& a, const mesh::CellAt& b) {
    return fromMiddle(a) < fromMiddle(b);
  });
  const mesh::CellAt seed = all[rng() % (burrow ? 2 : all.size() / 8 + 1)];
  std::ranges::stable_sort(all, [&](const mesh::CellAt& a, const mesh::CellAt& b) {
    return distance2(w, a, seed) < distance2(w, b, seed);
  });
  const std::size_t small = 1 + rng() % 3;
  std::vector<test::Cells> pieces(2);
  pieces[0].assign(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(small));

  std::set<mesh::CellAt> gone(pieces[0].begin(), pieces[0].end());
  if (burrow) {
    test::Cells at = pieces[0];
    bool inside = true;
    std::size_t last = w.directions().size();
    for (unsigned int leg = 0; leg < 4 && inside; leg++) {
      std::size_t dir = last;
      while (dir == last) {
        dir = rng() % w.directions().size();
      }
      last = dir;
      const test::Step& d = w.directions()[dir];
      const int sign = rng() % 2 == 0 ? 1 : -1;
      const unsigned int steps = leg == 0 ? 1 : 1 + rng() % 2;
      const test::Step step{sign * d[0], sign * d[1], sign * d[2]};
      for (unsigned int n = 0; n < steps && inside; n++) {
        inside = false;
        for (const mesh::CellAt& c : all) {
          if (!gone.contains(c) && w.enters(at, step, 1, {c})) {
            gone.insert(c);
            inside = true;
          }
        }
        for (mesh::CellAt& c : at) {
          c.x += step[0];
          c.y += step[1];
          c.z += step[2];
          inside = inside || (c.x >= 0 && c.x < box[0] && c.y >= 0 && c.y < box[1] && c.z >= 0 &&
                              c.z < box[2]);
        }
      }
    }
  }
  while (pieces[1].empty()) {
    for (std::size_t i = small; i < all.size(); i++) {
      if (burrow ? !gone.contains(all[i]) : rng() % 4 != 0) {
        pieces[1].push_back(all[i]);
      }
    }
  }
  return puzzleOf(kind, box, pieces);
}

constexpr std::array<GridKind, 6> kGrids = {GridKind::Bricks,  GridKind::TriangularPrism,
                                            GridKind::Rhombic, GridKind::TetraOcta,
                                            GridKind::Hexagonal, GridKind::Cairo};

// What the random puzzles of a grid gave, summed.
struct Tally {
  unsigned int puzzles = 0;
  unsigned int stuck = 0;
  unsigned int atOnce = 0;
  unsigned int later = 0;
  unsigned int nodes = 0;
  unsigned int gaveUp = 0;

  void sum(const Tally& t) {
    puzzles += t.puzzles;
    stuck += t.stuck;
    atOnce += t.atOnce;
    later += t.later;
    nodes += t.nodes;
    gaveUp += t.gaveUp;
  }

  void add(const Found& f) {
    puzzles++;
    gaveUp += f.gaveUp;
    stuck += f.stuckChecked;
    nodes += f.nodesChecked;
    for (const auto& [moves, n] : f.firstMoves) {
      (moves == 1 ? atOnce : later) += n;
    }
  }
};

// Random puzzles of a grid, every assembly (up to 20 a puzzle) taken
// apart and held against the playback and the reference search on the
// cells; cubes and prisms with the ported tables and the derived checks.
Tally randomPuzzles(GridKind kind, unsigned int pieces, unsigned int count, std::uint32_t seed) {
  test::CellWorld w(kind);
  std::mt19937 rng(seed);
  Tally tally;
  const bool ported = kind == GridKind::Bricks || kind == GridKind::TriangularPrism;
  for (unsigned int n = 0; n < count; n++) {
    INFO("grid " << static_cast<int>(kind) << ", " << pieces << " pieces, seed " << seed
                 << ", puzzle " << n);
    const Puzzle p =
        pieces == 2 ? cagePuzzle(w, kind, n % 4 != 0, rng) : cutPuzzle(w, kind, pieces, rng);
    DisassemblerOptions options;
    options.analysis.derivedChecks = true;
    const Found found = takeAllApart(p, 0, options, Against::Cells, false, 20);
    CHECK(found.assemblies > 0);
    tally.add(found);
    if (ported) {
      const Found byTables = takeAllApart(p, 0, {}, Against::Playback, false, 20);
      CHECK(byTables.sameAnswers(found));
    }
  }
  return tally;
}

}  // namespace

// The original's rhombic and tetra-octa examples, which it can only
// assemble. The counts of assemblies are the original's. Which of them
// come apart and how is this version's own answer: pinned here, played
// back on the cells and held against the reference search.
TEST_CASE("derived take-apart: the original's rhombic and tetra-octa examples",
          "[solve][disasm][derived]") {
  const std::vector<Example> examples = {
      {"AugmentedSecondStellation.xmpuzzle", 0, 2, 1, {"2.1.1.1"}},
      // its second problem has a piece that fits nowhere
      {"BrokenSticks.xmpuzzle", 0, 2, 1, {"2.1.1.1"}},
      {"DiagonalCube.xmpuzzle", 0, 2, 1, {"2.1.1.1"}},
      {"FourPieceTetrahedron.xmpuzzle", 0, 1, 1, {"2.1"}},
      {"PermutatedThirdStellation.xmpuzzle", 0, 2, 2, times(2, "2.1.1.1")},
      {"PiecesOfEight.xmpuzzle", 0, 6, 5, times(5, "1.1.1")},
  };
  for (const Example& e : examples) {
    checkExample(e);
  }
}

TEST_CASE("derived take-apart: the original's larger rhombic examples",
          "[solve][disasm][derived][slow]") {
  std::multiset<std::string> four = times(3, "2.1.1.1");
  four.insert("1.1.1.1.1");
  std::multiset<std::string> five = times(36, "1.1.1.1.1");
  five.merge(times(7, "2.1.1.1"));
  const std::vector<Example> examples = {
      {"12PieceSeparation.xmpuzzle", 0, 1, 1, {"1.6.7.5.3.2.1"}},
      {"HexSticks.xmpuzzle", 0, 33, 2, {"2.1.1.2.1.1.1.1.1", "2.1.2.1.1.1.1.1.1"}},
      {"PiecesOfEight.xmpuzzle", 1, 4, 4, four},
      {"PiecesOfEight.xmpuzzle", 2, 43, 43, five},
      {"PiecesOfEight.xmpuzzle", 3, 12, 12, times(12, "1.1.1.1.1.1.1")},
      {"PiecesOfEight.xmpuzzle", 4, 20, 20, times(20, "1.1.1.1.1.1.1")},
      {"PiecesOfEight.xmpuzzle", 5, 12, 12, times(12, "1.1.1.1.1.1.1")},
  };
  for (const Example& e : examples) {
    checkExample(e);
  }
}

// The reference search itself, where the original is the judge: on cube
// and prism examples it finds each node's moves, and no way apart where
// the take-apart (which the oracle harness holds against the original's)
// finds none.
TEST_CASE("derived take-apart: the reference search agrees with the ported take-apart",
          "[solve][disasm][derived]") {
  unsigned int nodes = 0;
  for (const char* file :
       {"AlPackino.xmpuzzle", "Bermuda.xmpuzzle", "DemoMirrorParadox.xmpuzzle",
        "Prisgon.xmpuzzle"}) {
    const Puzzle p = test::loadFixture(file);
    for (unsigned int n = 0; n < p.getNumberOfProblems(); n++) {
      INFO(file << " problem " << n);
      const Found found = takeAllApart(p, n, {}, Against::Cache);
      CHECK(found.gaveUp == 0);
      nodes += found.nodesChecked;
    }
  }
  CHECK(nodes > 30);
}

// The six-piece burrs: 588 assemblies of which 179 come apart, each node
// with the reference's moves, and the other 409 stuck for the reference
// too. The Pelikan burr's one take-apart is long: the reference may give
// up on its longest node.
TEST_CASE("derived take-apart: the reference search agrees on the six-piece burrs",
          "[solve][disasm][derived][slow]") {
  {
    const Puzzle p = test::loadFixture("SolidSixPieceBurrs.xmpuzzle");
    const Found found = takeAllApart(p, 0, {}, Against::Cache);
    CHECK(found.assemblies == 588);
    CHECK(found.solutions == 179);
    CHECK(found.gaveUp == 0);
    CHECK(found.stuckChecked == 409);
    CHECK(found.nodesChecked >= 5 * 179);
  }
  {
    const Puzzle p = test::loadFixture("PelikanBurr.xmpuzzle");
    const Found found = takeAllApart(p, 0, {}, Against::Cache);
    CHECK(found.assemblies == 12);
    CHECK(found.solutions == 1);
    CHECK(found.stuckChecked == 11);
    CHECK(found.nodesChecked >= 4);
  }
}

// Cubes and triangular prisms have the original's tables and the derived
// checks both: the same assemblies come apart the same way, with the cube
// grid's turns too where `turns` says so. The solutions found by the
// derived checks, summed.
struct Ported {
  const char* file;
  bool groups;
  bool turns;
};

unsigned int sameAsTables(const std::vector<Ported>& files) {
  unsigned int solutions = 0;
  for (const Ported& f : files) {
    const Puzzle p = test::loadFixture(f.file);
    for (unsigned int n = 0; n < p.getNumberOfProblems(); n++) {
      for (const bool rotations : {false, true}) {
        if (rotations && !f.turns) {
          continue;
        }
        INFO(f.file << " problem " << n << (rotations ? ", with turns" : ""));
        DisassemblerOptions byTables;
        byTables.rotations = rotations;
        DisassemblerOptions derived = byTables;
        derived.analysis.derivedChecks = true;
        const Found a = takeAllApart(p, n, byTables, Against::Playback, f.groups);
        const Found b = takeAllApart(p, n, derived, Against::Playback, f.groups);
        CHECK(a.assemblies > 0);
        CHECK(a.sameAnswers(b));
        solutions += b.solutions;
      }
    }
  }
  return solutions;
}

TEST_CASE("derived take-apart: cubes and prisms come apart as with the original's tables",
          "[solve][disasm][derived]") {
  CHECK(sameAsTables({
            {"AlPackino.xmpuzzle", false, true},
            {"Bermuda.xmpuzzle", false, false},
            {"CubeInCage.xmpuzzle", true, false},
            {"DemoMirrorParadox.xmpuzzle", false, true},
            {"Prisgon.xmpuzzle", false, false},
            {"test_rotation_solvers.xmpuzzle", false, true},
        }) == 11);
}

TEST_CASE("derived take-apart: larger cube puzzles come apart as with the original's tables",
          "[solve][disasm][derived][slow]") {
  // the cage with turns only: without them it is in the quick case
  const Puzzle cage = test::loadFixture("CubeInCage.xmpuzzle");
  DisassemblerOptions byTables;
  byTables.rotations = true;
  DisassemblerOptions derived = byTables;
  derived.analysis.derivedChecks = true;
  const Found a = takeAllApart(cage, 0, byTables, Against::Playback, true);
  const Found b = takeAllApart(cage, 0, derived, Against::Playback, true);
  CHECK(a.solutions == 2);
  CHECK(a.sameAnswers(b));

  CHECK(sameAsTables({
            {"DraculasDentalDesaster.xmpuzzle", false, false},
            {"PelikanBurr.xmpuzzle", false, false},
            {"SolidSixPieceBurrs.xmpuzzle", false, false},
        }) == 181);
}

// The playback itself: it takes a right take-apart of a cube shut in a
// box with a hole above it, and says what is wrong with wrong ones.
TEST_CASE("derived take-apart: the playback tells a wrong take-apart",
          "[solve][disasm][derived]") {
  test::CellWorld w(GridKind::Bricks);
  test::Cells box = test::cellsInBox(w.voxel(), 3, 3, 3);
  std::erase_if(box, [](const mesh::CellAt& c) { return c.x == 1 && c.y == 1 && c.z >= 1; });
  const Puzzle p = puzzleOf(GridKind::Bricks, {3, 3, 3}, {box, {{1, 1, 1}}});
  const Problem& problem = p.getProblem(0);

  std::unique_ptr<Assembler> assm = makeAssembler(p, problem, SolverType::Classic);
  REQUIRE(assm->createMatrix({}) == AssemblerError::None);
  std::optional<Assembly> assembly;
  (void)assm->assemble(
      [&](const Assembly& a) {
        assembly = a;
        return false;
      },
      SearchOptions{}, StopToken{});
  REQUIRE(assembly.has_value());
  const Assembly& a = *assembly;

  // the disassembler's own: the cube lifted out
  Disassembler disasm(p, problem, {});
  const TakeApartResult r = disasm.disassemble(a);
  REQUIRE(r.separation.has_value());
  CHECK(r.separation->movesText() == "1");
  CHECK_FALSE(test::replayError(p, problem, a, *r.separation).has_value());

  // a state: the box and the cube, each moved from where the assembly has it
  const auto state = [&](test::Step boxBy, test::Step cubeBy, bool turned = false) {
    State st(2);
    st.set(0, a.getX(0) + boxBy[0], a.getY(0) + boxBy[1], a.getZ(0) + boxBy[2],
           a.getTransformation(0));
    st.set(1, a.getX(1) + cubeBy[0], a.getY(1) + cubeBy[1], a.getZ(1) + cubeBy[2],
           turned ? a.getTransformation(1) + 1U : a.getTransformation(1));
    return st;
  };
  const auto wrong = [&](std::vector<State> states) {
    Separation s(ClonePtr<Separation>{}, ClonePtr<Separation>{}, std::vector<unsigned int>{0, 1});
    for (State& st : states) {
      s.appendState(std::move(st));
    }
    return test::replayError(p, problem, a, s).value_or("");
  };
  const test::Step still{0, 0, 0};
  const auto says = [](const std::string& error, const char* what) {
    INFO(error);
    return error.find(what) != std::string::npos;
  };

  // right ones: out at once, up a step first, or the box pulled down
  CHECK(wrong({state(still, still), state(still, {0, 0, 20000})}).empty());
  CHECK(wrong({state(still, still), state(still, {0, 0, 1}), state(still, {0, 0, 20001})})
            .empty());
  CHECK(wrong({state(still, still), state({0, 0, -20000}, still)}).empty());

  CHECK(says(wrong({state(still, still), state(still, {20000, 0, 0})}), "through another"));
  CHECK(says(wrong({state(still, still), state(still, {0, 0, -20000})}), "through another"));
  CHECK(says(wrong({state(still, still), state(still, {1, 0, 0}), state(still, {1, 0, 20000})}),
             "through another"));
  CHECK(says(wrong({state(still, still), state(still, {20000, 0, 20000})}),
             "no direction of the grid"));
  CHECK(says(wrong({state(still, {0, 0, 1}), state(still, {0, 0, 20001})}),
             "does not start at the assembly"));
  CHECK(says(wrong({state(still, still), state(still, {0, 0, 1})}), "moved in the removal"));
  CHECK(says(wrong({state(still, still), state(still, {0, 0, 40000})}), "not 20000 steps"));
  CHECK(says(wrong({state(still, still), state({0, 0, 20000}, {0, 0, 20000})}),
             "every piece moved"));
  CHECK(says(wrong({state(still, still), state(still, still)}), "no piece moved"));
  CHECK(says(wrong({state(still, still), state(still, {0, 0, 20000}, true)}), "turned"));
  CHECK(says(wrong({state(still, still), state(still, {0, 0, 20000}),
                    state({1, 0, 0}, {0, 0, 20000})}),
             "removed before the end"));
}

TEST_CASE("derived take-apart: random puzzles of two pieces come apart as the cells allow",
          "[solve][disasm][derived]") {
  Tally all;
  for (const GridKind kind : kGrids) {
    const Tally t = randomPuzzles(kind, 2, 5, 100 + static_cast<std::uint32_t>(kind));
    CHECK(t.gaveUp == 0);
    all.sum(t);
  }
  CHECK(all.stuck > 2);
  CHECK(all.atOnce > 5);
  CHECK(all.later > 5);
}

TEST_CASE("derived take-apart: many random puzzles of two pieces",
          "[solve][disasm][derived][slow]") {
  for (const GridKind kind : kGrids) {
    INFO("grid " << static_cast<int>(kind));
    const Tally t = randomPuzzles(kind, 2, 40, 300 + static_cast<std::uint32_t>(kind));
    CHECK(t.gaveUp == 0);
    // shut in, out at once, and out after a way, on every grid
    CHECK(t.stuck > 0);
    CHECK(t.atOnce > 2);
    CHECK(t.later > 0);
  }
}

// Boxes cut into three and four pieces: here pieces push each other and
// leave in groups, on every grid.
TEST_CASE("derived take-apart: random puzzles of several pieces come apart as the cells allow",
          "[solve][disasm][derived]") {
  Tally all;
  for (const GridKind kind : kGrids) {
    for (const unsigned int pieces : {3U, 4U}) {
      const Tally t = randomPuzzles(kind, pieces, 4, 200 + static_cast<std::uint32_t>(kind));
      CHECK(t.gaveUp == 0);
      all.sum(t);
    }
  }
  CHECK(all.stuck > 2);
  CHECK(all.nodes > 50);
}

TEST_CASE("derived take-apart: many random puzzles of several pieces",
          "[solve][disasm][derived][slow]") {
  for (const GridKind kind : kGrids) {
    Tally all;
    for (const unsigned int pieces : {3U, 4U, 5U}) {
      INFO("grid " << static_cast<int>(kind) << ", " << pieces << " pieces");
      const Tally t = randomPuzzles(kind, pieces, 25, 400 + static_cast<std::uint32_t>(kind));
      all.sum(t);
    }
    CHECK(all.gaveUp == 0);
    CHECK(all.stuck > 2);
    CHECK(all.atOnce > 20);
  }
}

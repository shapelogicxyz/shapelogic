// SPDX-License-Identifier: GPL-3.0-or-later
// solveSetup (cleanup item 15.2): the one place the rules that build a
// solve's matrix are written. The session, the CLI, the assembly explorer
// and the Python module each had a copy; the last two lacked the tilted
// tray's rule, so a Tilted Square problem whose pieces stay face up was
// explored and, in Python, solved with them turned over. New.
#include "../model/model_test_helpers.h"
#include "solve_test_helpers.h"

#include "shapelogic/app/document.h"
#include "shapelogic/app/explorers.h"
#include "shapelogic/base/solve_state.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/model/sliding.h"
#include "shapelogic/solve/assembler.h"
#include "shapelogic/solve/solve_session.h"
#include "shapelogic/solve/solver_type.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace bt;

namespace {

// The matrix options the caller gives: every field off its default, so
// that a rule that leaves one alone shows.
MatrixOptions given() {
  MatrixOptions m;
  m.keepMirrors = false;
  m.keepRotations = false;
  m.completeRotations = true;
  m.strictColors = true;
  m.variableHoleColumns = true;
  return m;
}

// The assemblies an assembler built by `setup` finds.
std::size_t assembled(const Puzzle& p, const Problem& pr, const SolveSetup& setup) {
  std::unique_ptr<Assembler> a = makeAssembler(p, pr, setup.solver);
  REQUIRE(a->createMatrix(setup.matrix, StopToken()) == AssemblerError::None);
  (void)a->reduce(StopToken{});
  const test::RunResult r = test::runOnce(*a, test::searchOptions(1, nullptr));
  REQUIRE(r.end == RunEnd::Finished);
  return r.found.size();
}

// The assemblies a session counts for problem `index` of a copy of `p`.
std::uint64_t sessionCount(const Puzzle& p, unsigned int index, bool flipPieces) {
  Puzzle copy = p;
  Problem& pr = copy.getProblem(index);
  pr.removeAllSolutions();
  SolveOptions o;
  o.justCount = true;
  o.threads = 1;
  o.flipPieces = flipPieces;
  SolveSession s(copy, pr, o);
  REQUIRE(s.start().has_value());
  s.wait();
  REQUIRE(s.state() == SolveState::Finished);
  return pr.getNumAssemblies();
}

}  // namespace

TEST_CASE("solve setup: a sliding problem", "[solve][setup][sliding]") {
  const Puzzle p = test::loadFixture("test_sliding_solver.xmpuzzle");
  const Problem& pr = p.getProblem(0);
  const SolveSetup s = solveSetup(p, pr, given(), SolverType::Crowell, SetupFlags{});
  // Classic's assembler, every start kept, no thorough rotation check
  CHECK(s.solver == SolverType::Classic);
  CHECK(s.matrix.keepMirrors);
  CHECK(s.matrix.keepRotations);
  CHECK_FALSE(s.matrix.completeRotations);
  // held to their starts by the marks, or matched strictly without them
  const bool marks = sliding::usesStartMarks(p, pr);
  CHECK(s.matrix.startLocks == marks);
  CHECK(s.matrix.strictColors == !marks);
  CHECK(s.matrix.freeStarts ==
        (marks ? sliding::freeStartParts(p, pr) : std::vector<bool>{}));
  // each piece as drawn, or as the flags allow
  CHECK_FALSE(s.matrix.partTransforms.empty());
  CHECK(s.matrix.partTransforms == sliding::partTransforms(p, pr, false, false));
  const SolveSetup any = solveSetup(p, pr, given(), SolverType::Classic, SetupFlags{true, true, {}});
  CHECK(any.matrix.partTransforms == sliding::partTransforms(p, pr, true, true));
  // what the rules do not name stays the caller's
  CHECK(s.matrix.variableHoleColumns);
  // the flip flag is no part of a puzzle without a tray
  const SolveSetup flipped = solveSetup(p, pr, given(), SolverType::Classic, SetupFlags{false, false, false});
  CHECK(flipped.matrix.partTransforms == s.matrix.partTransforms);
}

TEST_CASE("solve setup: a tilted tray's problem", "[solve][setup][tilted]") {
  // Triple Play: problem 0 stores "pieces may be turned over" on, problem
  // 1 off
  const Puzzle p = test::loadFixture("tilted/TriplePlay.xmpuzzle");
  REQUIRE(p.tray().has_value());
  REQUIRE(storedFlipPieces(p.getProblem(0)));
  REQUIRE_FALSE(storedFlipPieces(p.getProblem(1)));
  const Problem& faceUp = p.getProblem(1);
  const std::vector<std::uint64_t> turns = trayPlaneTurns(p, faceUp);
  REQUIRE(turns.size() == faceUp.getNumberOfParts());

  // as stored, and as the flag says over it
  const SolveSetup stored = solveSetup(p, faceUp, given(), SolverType::Crowell, SetupFlags{});
  CHECK(stored.matrix.partTransforms == turns);
  CHECK(solveSetup(p, faceUp, given(), SolverType::Crowell, SetupFlags{false, false, false})
            .matrix.partTransforms == turns);
  CHECK(solveSetup(p, faceUp, given(), SolverType::Crowell, SetupFlags{false, false, true})
            .matrix.partTransforms.empty());
  CHECK(solveSetup(p, p.getProblem(0), given(), SolverType::Crowell, SetupFlags{})
            .matrix.partTransforms.empty());
  CHECK(solveSetup(p, p.getProblem(0), given(), SolverType::Crowell,
                   SetupFlags{false, false, false})
            .matrix.partTransforms == trayPlaneTurns(p, p.getProblem(0)));
  // nothing else of the caller's changes
  CHECK(stored.solver == SolverType::Crowell);
  CHECK_FALSE(stored.matrix.keepMirrors);
  CHECK_FALSE(stored.matrix.keepRotations);
  CHECK(stored.matrix.completeRotations);
  CHECK(stored.matrix.strictColors);
  CHECK_FALSE(stored.matrix.startLocks);
  CHECK(stored.matrix.freeStarts.empty());
  CHECK(stored.matrix.variableHoleColumns);

  // The session's matrix is this one: an assembler built by solveSetup
  // finds what a session counts, face up (2, the file's own count) and
  // turned over (1).
  const SolveSetup up = solveSetup(p, faceUp, MatrixOptions{}, SolverType::Classic, SetupFlags{});
  const SolveSetup over =
      solveSetup(p, faceUp, MatrixOptions{}, SolverType::Classic, SetupFlags{false, false, true});
  CHECK(assembled(p, faceUp, up) == 2);
  CHECK(assembled(p, faceUp, over) == 1);
  CHECK(sessionCount(p, 1, false) == 2);
  CHECK(sessionCount(p, 1, true) == 1);
}

TEST_CASE("solve setup: any other problem keeps the caller's options", "[solve][setup]") {
  const Puzzle p = test::loadFixture("PelikanBurr.xmpuzzle");
  const SolveSetup s =
      solveSetup(p, p.getProblem(0), given(), SolverType::Crowell, SetupFlags{true, true, false});
  CHECK(s.solver == SolverType::Crowell);
  CHECK_FALSE(s.matrix.keepMirrors);
  CHECK_FALSE(s.matrix.keepRotations);
  CHECK(s.matrix.completeRotations);
  CHECK(s.matrix.strictColors);
  CHECK_FALSE(s.matrix.startLocks);
  CHECK(s.matrix.freeStarts.empty());
  CHECK(s.matrix.partTransforms.empty());
  CHECK(s.matrix.variableHoleColumns);
}

// The assembly explorer browses the placements the problem's solve makes:
// on a tilted tray whose pieces stay face up, only the turns in the tray
// (it had no tray rule: it showed the turned-over placements too).
TEST_CASE("solve setup: the assembly explorer places a tilted tray's pieces as the solve does",
          "[solve][setup][tilted]") {
  const Result<app::Document> d = app::Document::open("tests/data/puzzles/tilted/TriplePlay.xmpuzzle");
  REQUIRE(d.has_value());
  const Puzzle& p = d->puzzle();
  const auto placements = [&](unsigned int problem) {
    const Result<app::AssemblyExplorer> e = d->exploreAssembly(
        d->problemIds()[problem], SolverType::Classic, MatrixOptions{}, false);
    REQUIRE(e.has_value());
    REQUIRE(e->placementsSupported());
    std::vector<std::size_t> counts;
    for (unsigned int piece = 0; piece < e->pieceCount(); piece++) {
      counts.push_back(e->placements(piece).size());
    }
    return counts;
  };
  // what an assembler built by solveSetup holds for each piece
  const auto built = [&](unsigned int problem, const SetupFlags& flags) {
    const Problem& pr = p.getProblem(problem);
    const SolveSetup setup = solveSetup(p, pr, MatrixOptions{}, SolverType::Classic, flags);
    std::unique_ptr<Assembler> a = makeAssembler(p, pr, setup.solver);
    REQUIRE(a->createMatrix(setup.matrix, StopToken()) == AssemblerError::None);
    REQUIRE(a->piecePlacementSupported());
    std::vector<std::size_t> counts;
    for (unsigned int piece = 0; piece < pr.getNumberOfPieces(); piece++) {
      counts.push_back(a->piecePlacementCount(piece));
    }
    return counts;
  };
  // problem 1 keeps its pieces face up
  const std::vector<std::size_t> faceUp = placements(1);
  CHECK(faceUp == built(1, SetupFlags{}));
  const std::vector<std::size_t> turnedOver = built(1, SetupFlags{false, false, true});
  REQUIRE(faceUp.size() == turnedOver.size());
  bool fewer = false;
  for (std::size_t i = 0; i < faceUp.size(); i++) {
    CHECK(faceUp[i] <= turnedOver[i]);
    fewer = fewer || faceUp[i] < turnedOver[i];
  }
  CHECK(fewer);
  // problem 0 turns them over: every placement
  CHECK(placements(0) == built(0, SetupFlags{}));
  CHECK(placements(0) == built(0, SetupFlags{false, false, true}));
}

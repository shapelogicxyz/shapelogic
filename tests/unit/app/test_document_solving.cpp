// SPDX-License-Identifier: GPL-3.0-or-later
// Solving from the document (decision 0011): what is refused while a run
// is active and what goes on, what an edit does to a paused solve, reads
// of a solving problem from the owner's thread (the [parallel] case is for
// ThreadSanitizer), and the autosave flow. New.
#include "app_test_helpers.h"

#include "bt/app/document.h"
#include "bt/base/solve_state.h"
#include "bt/model/problem.h"
#include "bt/model/solution.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <thread>

using namespace bt;
using namespace bt::app;
using test::openFixture;

namespace {

SolveOptions takeApart(unsigned int threads = 0) {
  SolveOptions o;
  o.disassemble = true;
  o.threads = threads;
  o.solutionLimit = 0;
  return o;
}

// The document of a fixture with its problems' solves dropped.
Document fresh(const std::string& name) {
  Puzzle stripped = openFixture(name).puzzle();
  for (unsigned int i = 0; i < stripped.getNumberOfProblems(); i++) {
    stripped.getProblem(i).removeAllSolutions();
  }
  Result<Document> d = Document::openBytes(io::writePuzzle(stripped));
  REQUIRE(d.has_value());
  return std::move(*d);
}

// A refusal while a run is active: InvalidState, no new revision.
template <class F>
void refusedWhileSolving(Document& d, F&& cmd) {
  const std::uint64_t rev = d.revision();
  auto r = std::forward<F>(cmd)(d);
  REQUIRE_FALSE(r.has_value());
  CHECK(r.error().code == Errc::InvalidState);
  CHECK(d.revision() == rev);
}

struct Totals {
  std::uint64_t assemblies = 0;
  std::uint64_t solutions = 0;
  bool operator==(const Totals&) const = default;
};

Totals totals(const Document& d, unsigned int p) {
  return d.readProblem(p, [](const Problem& pr) {
    REQUIRE(pr.getSolveState() == ProblemState::Solved);
    return Totals{pr.getNumAssemblies(), pr.getNumSolutions()};
  });
}

void solveToEnd(Document& d, Id p, const SolveOptions& o) {
  for (int runs = 0; d.solveState(p) != SolveState::Finished; runs++) {
    REQUIRE(runs < 50);
    REQUIRE(d.solveResume(p, o).has_value());
    REQUIRE(d.solveWait(p).has_value());
    REQUIRE(d.solveState(p) != SolveState::Error);
  }
}

}  // namespace

TEST_CASE("document solving: a solve, its progress, and abort", "[app][solving]") {
  Document d = fresh("DemoPieceGenerator.xmpuzzle");
  const Id p = d.problemIds().front();
  CHECK(d.solveState(p) == SolveState::Idle);
  CHECK(d.solvePause(p).error().code == Errc::InvalidState);
  const std::uint64_t rev = d.revision();
  REQUIRE(d.solveStart(p, takeApart()).has_value());
  CHECK(d.revision() > rev);
  REQUIRE(d.solveWait(p).has_value());
  CHECK(d.solveState(p) == SolveState::Finished);
  CHECK(totals(d, 0) == Totals{928, 927});
  CHECK(d.solveProgress(p).value().overall == 1.0F);
  CHECK(d.dirty());
  CHECK_FALSE(d.solveFailure(p).has_value());

  // a finished solve is not aborted (nothing is under way, as the
  // original's greyed-out button): its solutions are removed as a step
  CHECK(d.solveAbort(p).error().code == Errc::InvalidState);
  CHECK(d.solveState(p) == SolveState::Finished);
  REQUIRE(d.deleteSolutions(p, SolutionsToDelete::All).has_value());
  CHECK(d.puzzle().getProblem(0).getNumberOfSavedSolutions() == 0);
  CHECK(d.puzzle().getProblem(0).getSolveState() == ProblemState::Solved);
  REQUIRE(d.undo().has_value());
  CHECK(d.puzzle().getProblem(0).getNumberOfSavedSolutions() == 927);
  CHECK(d.solveState(p) == SolveState::Idle);
}

TEST_CASE("document solving: while a run is active the puzzle is frozen but for a few things",
          "[app][solving]") {
  Document d = fresh("SolidSixPieceBurrs.xmpuzzle");
  const Id other = d.createProblem().value();
  const Id p = d.problemIds().front();
  const Id s = d.shapeIds().front();
  const Id c = d.addColour(Rgb{1, 2, 3}).value();

  REQUIRE(d.solveStart(p, takeApart(1)).has_value());
  // Checked while holding the problem's lock: the run cannot write the
  // problem meanwhile, so it cannot end, however slow the machine (none
  // of these calls takes that lock).
  d.readProblem(0, [&](const Problem&) {
    REQUIRE(isActive(d.solveState(p)));
    CHECK(d.solving());

    refusedWhileSolving(d, [s](Document& x) {
      return x.editCells(s, CellTool::Fixed, 0, 0, {{0, 0, 0}});
    });
    refusedWhileSolving(d, [](Document& x) { return x.createShape(); });
    refusedWhileSolving(d, [s](Document& x) { return x.renameShape(s, "no"); });
    refusedWhileSolving(d, [](Document& x) { return x.createProblem(); });
    refusedWhileSolving(d, [c](Document& x) { return x.deleteColour(c); });
    refusedWhileSolving(d, [p](Document& x) { return x.renameProblem(p, "no"); });
    refusedWhileSolving(d, [p](Document& x) { return x.addOneOfEach(p); });
    refusedWhileSolving(d,
                        [p](Document& x) { return x.deleteSolutions(p, SolutionsToDelete::All); });
    refusedWhileSolving(d, [](Document& x) { return x.undo(); });
    refusedWhileSolving(d, [](Document& x) { return x.saveBytes(); });
    refusedWhileSolving(d, [p](Document& x) { return x.solveStart(p, {}); });
    // these go on: the notes, how a colour looks, another problem
    CHECK(d.setNotes("noted while solving").has_value());
    CHECK(d.editColour(c, Rgb{9, 9, 9}).has_value());
    CHECK(d.renameProblem(other, "the other").has_value());
    CHECK(isActive(d.solveState(p)));
  });

  REQUIRE(d.solveAbort(p).has_value());
  CHECK_FALSE(d.solving());
  CHECK(d.createShape().has_value());
}

TEST_CASE("document solving: an edit of a paused problem's shape throws its solve away",
          "[app][solving]") {
  // the original's changeShape: the paused search goes with the solutions
  Document d = fresh("DemoPieceGenerator.xmpuzzle");
  const Id p = d.problemIds().front();
  REQUIRE(d.solvePrepare(p, takeApart()).has_value());
  REQUIRE(d.solveWait(p).has_value());
  REQUIRE(d.solveState(p) == SolveState::Paused);
  REQUIRE(d.puzzle().getProblem(0).getSolveState() == ProblemState::Solving);

  const Id piece = d.shapeIds()[d.puzzle().getProblem(0).getShapeIdOfPart(0)];
  REQUIRE(d.shapeTask(piece, ShapeTask::Flip12, false).has_value());
  CHECK(d.puzzle().getProblem(0).getSolveState() == ProblemState::Unsolved);
  CHECK(d.solveState(p) == SolveState::Idle);
  // and the next solve starts from scratch
  solveToEnd(d, p, takeApart());
}

TEST_CASE("document solving: a paused solve survives a change of the lists", "[app][solving]") {
  // the session ends (it holds the problem by reference); the paused
  // position is in the problem, and the solve carries on from it
  Document straight = fresh("DemoPieceGenerator.xmpuzzle");
  const Id sp = straight.problemIds().front();
  REQUIRE(straight.solveStart(sp, takeApart(1)).has_value());
  REQUIRE(straight.solveWait(sp).has_value());
  const Totals want = totals(straight, 0);

  Document d = fresh("DemoPieceGenerator.xmpuzzle");
  const Id p = d.problemIds().front();
  bool landed = false;
  for (int attempt = 0; attempt < 10 && !landed; attempt++) {
    REQUIRE(d.solveStart(p, takeApart(1)).has_value());
    // as it starts assembling, a little later on each attempt
    while (d.solveState(p) == SolveState::Preparing) {
      std::this_thread::yield();
    }
    std::this_thread::sleep_for(std::chrono::microseconds(200 * attempt));
    REQUIRE(d.solvePause(p).has_value());
    REQUIRE(d.solveWait(p).has_value());
    landed = d.solveState(p) == SolveState::Paused;
  }
  REQUIRE(landed);
  const Id q = d.createProblem().value();
  CHECK(d.solveState(p) == SolveState::Idle);
  CHECK(d.puzzle().getProblem(0).getSolveState() == ProblemState::Solving);
  solveToEnd(d, p, takeApart(1));
  CHECK(totals(d, 0) == want);
  (void)q;
}

TEST_CASE("document solving: a paused BurrTools 2 search blocks a change of the lists",
          "[app][solving]") {
  // it can be carried on in memory only (decision 0009): ending its
  // session would lose it, so the change is refused until it is aborted
  Document d = fresh("DraculasDentalDesaster.xmpuzzle");
  const Id p = d.problemIds().front();
  SolveOptions o = takeApart();
  o.solver = SolverType::Bt2;
  REQUIRE(d.solvePrepare(p, o).has_value());
  REQUIRE(d.solveWait(p).has_value());
  REQUIRE(d.solveState(p) == SolveState::Paused);
  test::refused(d, Errc::InvalidState, [](Document& x) { return x.createProblem(); });
  test::refused(d, Errc::InvalidState, [](Document& x) { return x.addColour(Rgb{}); });
  // a new shape at the end is fine
  CHECK(d.createShape().has_value());
  REQUIRE(d.solveAbort(p).has_value());
  CHECK(d.createProblem().has_value());
}

TEST_CASE("document solving: the autosave flow and saving", "[app][solving][resume]") {
  Document d = fresh("DemoPieceGenerator.xmpuzzle");
  const Id p = d.problemIds().front();
  const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "bt_autosave.xmpuzzle";
  REQUIRE(d.solveStart(p, takeApart()).has_value());
  // pause at a save point, save, carry on
  while (d.solveState(p) == SolveState::Preparing) {
    std::this_thread::yield();
  }
  REQUIRE(d.solvePauseAtSavePoint(p).has_value());
  REQUIRE(d.solveWait(p).has_value());
  REQUIRE(d.save(tmp).has_value());
  CHECK_FALSE(d.dirty());
  solveToEnd(d, p, takeApart());
  CHECK(totals(d, 0) == Totals{928, 927});

  // the saved file carries on in a new document
  Document e = Document::open(tmp).value();
  if (e.puzzle().getProblem(0).getSolveState() != ProblemState::Solved) {
    solveToEnd(e, e.problemIds().front(), takeApart());
  }
  CHECK(totals(e, 0) == Totals{928, 927});
  std::filesystem::remove(tmp);
}

TEST_CASE("document solving: reading while a run is active", "[app][solving][parallel]") {
  // the owner reads the solving problem through the lock, and the shapes,
  // meshes and other problems directly, while the run works
  Document d = fresh("SolidSixPieceBurrs.xmpuzzle");
  const Id p = d.problemIds().front();
  REQUIRE(d.solveStart(p, takeApart(4)).has_value());
  unsigned int reads = 0;
  while (isActive(d.solveState(p))) {
    (void)d.readProblem(0, [](const Problem& pr) { return pr.getNumberOfSavedSolutions(); });
    (void)d.solveProgress(p);
    (void)d.solveStats(p);
    (void)d.fit(p);
    (void)d.shapeTable();
    (void)d.shapeMesh(d.shapeIds()[reads % d.shapeIds().size()], mesh::MeshStyle::Flat);
    reads++;
  }
  REQUIRE(d.solveWait(p).has_value());
  CHECK(d.solveState(p) == SolveState::Finished);
  CHECK(reads > 0);
  // a playback of a solution found
  const Id sol = d.readProblem(0, [](const Problem& pr) { return pr.getSavedSolution(0).getId(); });
  CHECK(d.playback(p, sol).has_value());
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Solving from the document (decision 0011): what is refused while a run
// is active and what goes on, what an edit does to a paused solve, reads
// of a solving problem from the owner's thread (the [parallel] case is for
// ThreadSanitizer), and the autosave flow. New.
#include "app_test_helpers.h"
#include "../scratch_dir.h"

#include "shapelogic/app/document.h"
#include "shapelogic/base/solve_state.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/solution.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace sl;
using namespace sl::app;
using test::openFixture;
using sl::test::ScratchDir;

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
  // original's grayed-out button): its solutions are removed as a step
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
  const Id c = d.addColor(Rgb{1, 2, 3}).value();

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
    refusedWhileSolving(d, [other](Document& x) { return x.deleteProblem(other); });
    refusedWhileSolving(d, [other](Document& x) { return x.moveProblem(other, 0); });
    refusedWhileSolving(d, [c](Document& x) { return x.deleteColor(c); });
    refusedWhileSolving(d, [p](Document& x) { return x.renameProblem(p, "no"); });
    refusedWhileSolving(d, [p](Document& x) { return x.addOneOfEach(p); });
    refusedWhileSolving(d,
                        [p](Document& x) { return x.deleteSolutions(p, SolutionsToDelete::All); });
    refusedWhileSolving(d, [](Document& x) { return x.undo(); });
    refusedWhileSolving(d, [](Document& x) { return x.saveBytes(); });
    refusedWhileSolving(d, [p](Document& x) { return x.solveStart(p, {}); });
    // the original replaced the puzzle under its solver thread
    refusedWhileSolving(d, [](Document& x) { return x.convertGrid(GridKind::Rhombic); });
    // these go on: the notes, how a color looks, another problem
    CHECK(d.setNotes("noted while solving").has_value());
    CHECK(d.editColor(c, Rgb{9, 9, 9}).has_value());
    CHECK(d.renameProblem(other, "the other").has_value());
    // a problem added at the end moves none: the run goes on
    CHECK(d.createProblem().has_value());
    CHECK(d.duplicateProblem(other).has_value());
    CHECK(d.exportPuzzlecad(other, "x").has_value());
    CHECK(d.exportPuzzlecad(std::nullopt, "x").has_value());
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
  // a problem added at the end leaves the session be; one deleted ends it
  const Id q = d.createProblem().value();
  CHECK(d.solveState(p) == SolveState::Paused);
  REQUIRE(d.deleteProblem(q).has_value());
  CHECK(d.solveState(p) == SolveState::Idle);
  CHECK(d.puzzle().getProblem(0).getSolveState() == ProblemState::Solving);
  solveToEnd(d, p, takeApart(1));
  CHECK(totals(d, 0) == want);
}

TEST_CASE("document solving: problems added while a run is active", "[app][solving][parallel]") {
  // new ones and copies of the problem being solved, as the run writes it;
  // the run's answers are a straight run's, and undo takes them out again
  Document d = fresh("DemoPieceGenerator.xmpuzzle");
  const Id p = d.problemIds().front();
  REQUIRE(d.solveStart(p, takeApart(4)).has_value());
  // one for sure while it runs (it cannot end while its problem is read)
  unsigned int added = 0;
  d.readProblem(0, [&](const Problem&) {
    REQUIRE(isActive(d.solveState(p)));
    REQUIRE(d.createProblem().has_value());
    added++;
  });
  // fewer than the undo depth, so that all of them can be undone
  while (isActive(d.solveState(p)) && added < 20) {
    const Result<Id> r = added % 2 == 1 ? d.duplicateProblem(p) : d.createProblem();
    REQUIRE(r.has_value());
    CHECK(d.solveState(*r) == SolveState::Idle);
    added++;
  }
  REQUIRE(d.solveWait(p).has_value());
  CHECK(d.solveState(p) == SolveState::Finished);
  CHECK(totals(d, 0) == Totals{928, 927});
  CHECK(d.problemIds().size() == 1 + added);
  // the run's step, then one for each problem added (cleanup item 15.5:
  // the run recorded none before)
  CHECK(d.history().undo.size() == 1 + added);
  CHECK(d.history().undo.front() == "Solve P1");
  if (added > 1) {
    // a copy has the definition, not the solutions
    const Problem& copy = d.puzzle().getProblem(2);
    CHECK(copy.getNumberOfParts() == d.puzzle().getProblem(0).getNumberOfParts());
    CHECK(copy.getResultId() == d.puzzle().getProblem(0).getResultId());
    CHECK(copy.getNumberOfSavedSolutions() == 0);
    CHECK(copy.getSolveState() == ProblemState::Unsolved);
  }
  const std::vector<Id> ids = d.problemIds();
  for (unsigned int i = 0; i < added; i++) {
    REQUIRE(d.undo().has_value());
  }
  CHECK(d.problemIds().size() == 1);
  CHECK(totals(d, 0) == Totals{928, 927});
  for (unsigned int i = 0; i < added; i++) {
    REQUIRE(d.redo().has_value());
  }
  CHECK(d.problemIds() == ids);
  // and all of them and the run undone: the problem as it was
  for (unsigned int i = 0; i < added + 1; i++) {
    REQUIRE(d.undo().has_value());
  }
  CHECK_FALSE(d.canUndo());
  CHECK(d.puzzle().getProblem(0).getSolveState() == ProblemState::Unsolved);
}

// Decision 0011 as amended on 2026-11-24 (cleanup item 15.5, D1): a run
// records an undo step that holds its problem as it was, so undo takes the
// run's results out before the edits made before it, and redo brings them
// back. Without it an undo of an older step (a Solver option, say) put the
// problem back as it was before the solve and left the solutions to the
// redo stack, where the next edit threw them away; and an undone edit of a
// shape left the solutions found for the edited shape.
TEST_CASE("document solving: a run is an undo step", "[app][solving]") {
  Document d = fresh("DemoPieceGenerator.xmpuzzle");
  const Id p = d.problemIds().front();
  const SolveOptions o = takeApart();

  SECTION("an option, a solve, undo, redo") {
    REQUIRE(d.setSolverOptions(p, {{"disassemble", "1"}}).has_value());
    const test::DocState optioned = test::state(d);
    REQUIRE(d.history().undo == std::vector<std::string>{"Solver options"});
    REQUIRE(d.solveStart(p, o).has_value());
    // while it runs (it cannot end while its problem is read): no undo
    d.readProblem(0, [&](const Problem&) {
      REQUIRE(isActive(d.solveState(p)));
      refusedWhileSolving(d, [](Document& x) { return x.undo(); });
    });
    REQUIRE(d.solveWait(p).has_value());
    (void)d.poll();
    CHECK(totals(d, 0) == Totals{928, 927});
    CHECK(d.history().undo == std::vector<std::string>{"Solver options", "Solve P1"});
    const test::DocState solved = test::state(d);
    const std::uint64_t solvedVersion = d.puzzle().getProblem(0).solutionsVersion();
    const std::uint64_t lastId = d.puzzle().getProblem(0).lastSolutionId();

    // undo: the run's results go, the option stays, to the byte
    REQUIRE(d.undo().has_value());
    CHECK((d.lastChange().what & kChangeSolutions) != 0);
    CHECK(test::state(d) == optioned);
    CHECK(d.puzzle().getProblem(0).getSolveState() == ProblemState::Unsolved);
    CHECK(d.puzzle().getProblem(0).getSolverOptions().at("disassemble") == "1");
    CHECK(d.solveState(p) == SolveState::Idle);
    CHECK(d.history().redo == std::vector<std::string>{"Solve P1"});
    // the list is another one: its version says so, and never goes back
    CHECK(d.puzzle().getProblem(0).solutionsVersion() > solvedVersion);
    // redo: they are back
    REQUIRE(d.redo().has_value());
    CHECK(test::state(d) == solved);
    CHECK(totals(d, 0) == Totals{928, 927});
    CHECK(d.puzzle().getProblem(0).getSavedSolution(0).getDisassemblyInfo() != nullptr);
    // undo twice: the option goes after the results
    REQUIRE(d.undo().has_value());
    REQUIRE(d.undo().has_value());
    CHECK(d.puzzle().getProblem(0).getSolverOptions().empty());
    CHECK_FALSE(d.canUndo());
    REQUIRE(d.redo().has_value());
    REQUIRE(d.redo().has_value());
    CHECK(test::state(d) == solved);

    // undone, then solved again: the new solutions take no id an undone
    // one had
    REQUIRE(d.undo().has_value());
    REQUIRE(d.solveStart(p, o).has_value());
    REQUIRE(d.solveWait(p).has_value());
    CHECK_FALSE(d.canRedo());
    const Problem& pr = d.puzzle().getProblem(0);
    for (unsigned int i = 0; i < pr.getNumberOfSavedSolutions(); i++) {
      REQUIRE(pr.getSavedSolution(i).getId() > lastId);
    }
  }

  SECTION("a solve, undo, then an edit: the solutions are gone, as asked") {
    REQUIRE(d.solveStart(p, o).has_value());
    REQUIRE(d.solveWait(p).has_value());
    REQUIRE(d.undo().has_value());
    CHECK(d.canRedo());
    REQUIRE(d.renameProblem(p, "edited").has_value());
    CHECK_FALSE(d.canRedo());
    CHECK(d.puzzle().getProblem(0).getNumberOfSavedSolutions() == 0);
    CHECK(d.puzzle().getProblem(0).getSolveState() == ProblemState::Unsolved);
  }

  SECTION("an edit of a shape, a solve, undo twice: the shape's solutions go before the shape") {
    // (the problem is unsolved, so the edit's step holds the shape alone)
    const test::DocState start = test::state(d);
    const Id piece = d.shapeIds()[d.puzzle().getProblem(0).getShapeIdOfPart(0)];
    REQUIRE(d.shapeTask(piece, ShapeTask::Flip12, false).has_value());
    const test::DocState edited = test::state(d);
    REQUIRE(d.solveStart(p, o).has_value());
    REQUIRE(d.solveWait(p).has_value());
    REQUIRE(d.puzzle().getProblem(0).getSolveState() == ProblemState::Solved);
    REQUIRE(d.undo().has_value());
    CHECK(test::state(d) == edited);
    CHECK(d.puzzle().getProblem(0).getSolveState() == ProblemState::Unsolved);
    REQUIRE(d.undo().has_value());
    CHECK(test::state(d) == start);
  }

  SECTION("one step for the runs of a solve carried on; a refused launch records none") {
    // paused before the search (Prepare), carried on, solved again
    REQUIRE(d.solvePrepare(p, o).has_value());
    REQUIRE(d.solveWait(p).has_value());
    REQUIRE(d.solveState(p) == SolveState::Paused);
    CHECK(d.history().undo == std::vector<std::string>{"Solve P1"});
    solveToEnd(d, p, o);
    CHECK(d.history().undo == std::vector<std::string>{"Solve P1"});
    // a finished solve cannot be carried on: refused, nothing recorded
    const std::uint64_t rev = d.revision();
    CHECK_FALSE(d.solveResume(p, o).has_value());
    CHECK(d.revision() == rev);
    CHECK(d.history().undo == std::vector<std::string>{"Solve P1"});
    CHECK(totals(d, 0) == Totals{928, 927});
    // solved again, the step on top still its own: the state to go back
    // to is the one before the first run
    REQUIRE(d.solveStart(p, o).has_value());
    REQUIRE(d.solveWait(p).has_value());
    CHECK(d.history().undo == std::vector<std::string>{"Solve P1"});
    REQUIRE(d.undo().has_value());
    CHECK(d.puzzle().getProblem(0).getSolveState() == ProblemState::Unsolved);
    CHECK(d.puzzle().getProblem(0).getNumberOfSavedSolutions() == 0);
    // another step in between: the next run records its own
    REQUIRE(d.redo().has_value());
    REQUIRE(d.setNotes("noted").has_value());
    REQUIRE(d.solveStart(p, o).has_value());
    REQUIRE(d.solveWait(p).has_value());
    CHECK(d.history().undo ==
          std::vector<std::string>{"Solve P1", "Edit notes", "Solve P1"});
    // which gives the first solve's results back
    REQUIRE(d.undo().has_value());
    CHECK(totals(d, 0) == Totals{928, 927});
  }

  SECTION("a run started with steps to redo clears them, step or not") {
    REQUIRE(d.solveStart(p, o).has_value());
    REQUIRE(d.solveWait(p).has_value());
    REQUIRE(d.setNotes("noted").has_value());
    REQUIRE(d.undo().has_value());
    REQUIRE(d.canRedo());
    // the step on top is this problem's: no new step, and nothing to redo
    REQUIRE(d.solveStart(p, o).has_value());
    REQUIRE(d.solveWait(p).has_value());
    CHECK(d.history().undo == std::vector<std::string>{"Solve P1"});
    CHECK_FALSE(d.canRedo());
  }

  SECTION("a paused solve undone and redone is carried on") {
    Document straight = fresh("DemoPieceGenerator.xmpuzzle");
    const Id sp = straight.problemIds().front();
    REQUIRE(straight.solveStart(sp, takeApart(1)).has_value());
    REQUIRE(straight.solveWait(sp).has_value());
    const Totals want = totals(straight, 0);

    REQUIRE(d.solvePrepare(p, takeApart(1)).has_value());
    REQUIRE(d.solveWait(p).has_value());
    REQUIRE(d.puzzle().getProblem(0).getSolveState() == ProblemState::Solving);
    REQUIRE(d.undo().has_value());
    CHECK(d.puzzle().getProblem(0).getSolveState() == ProblemState::Unsolved);
    REQUIRE(d.redo().has_value());
    CHECK(d.puzzle().getProblem(0).getSolveState() == ProblemState::Solving);
    CHECK(d.solveState(p) == SolveState::Idle);
    solveToEnd(d, p, takeApart(1));
    CHECK(totals(d, 0) == want);
  }

  SECTION("dirty, saved, and solved again") {
    const ScratchDir scratch("solve_step_dirty");
    REQUIRE(d.solveStart(p, o).has_value());
    REQUIRE(d.solveWait(p).has_value());
    CHECK(d.dirty());
    REQUIRE(d.save(scratch.path() / "solved.xmpuzzle").has_value());
    CHECK_FALSE(d.dirty());
    // a second run records no step: the document is changed all the same
    REQUIRE(d.solveStart(p, o).has_value());
    REQUIRE(d.solveWait(p).has_value());
    CHECK(d.dirty());
    REQUIRE(d.save(scratch.path() / "solved.xmpuzzle").has_value());
    // undone, the file on disk has what the document no longer has
    REQUIRE(d.undo().has_value());
    CHECK(d.dirty());
  }
}

TEST_CASE("document solving: two problems' runs are steps of their own", "[app][solving]") {
  Document d = fresh("DemoPieceGenerator.xmpuzzle");
  const Id p = d.problemIds().front();
  const Id q = d.duplicateProblem(p).value();
  const SolveOptions o = takeApart();
  REQUIRE(d.solveStart(p, o).has_value());
  REQUIRE(d.solveStart(q, o).has_value());
  REQUIRE(d.solveWait(p).has_value());
  REQUIRE(d.solveWait(q).has_value());
  CHECK(d.history().undo ==
        std::vector<std::string>{"Duplicate problem", "Solve P1", "Solve P2"});
  CHECK(totals(d, 0) == Totals{928, 927});
  CHECK(totals(d, 1) == Totals{928, 927});
  REQUIRE(d.undo().has_value());
  CHECK(totals(d, 0) == Totals{928, 927});
  CHECK(d.puzzle().getProblem(1).getSolveState() == ProblemState::Unsolved);
  REQUIRE(d.undo().has_value());
  CHECK(d.puzzle().getProblem(0).getSolveState() == ProblemState::Unsolved);
}

TEST_CASE("document solving: a take-apart is an undo step", "[app][solving]") {
  // a box of 2 x 2 x 2 cells and the four dominoes that fill it, assembled
  // without taking apart
  Document d(GridKind::Bricks);
  const Id box = d.createShape(std::array<unsigned int, 3>{2, 2, 2}).value();
  REQUIRE(d.editBox(box, CellTool::Fixed, 0, 0, CellBox{0, 0, 0, 1, 1, 1}).value() == 8);
  const Id domino = d.createShape(std::array<unsigned int, 3>{2, 1, 1}).value();
  REQUIRE(d.editBox(domino, CellTool::Fixed, 0, 0, CellBox{0, 0, 0, 1, 0, 0}).value() == 2);
  const Id p = d.createProblem().value();
  REQUIRE(d.setResult(p, box).has_value());
  REQUIRE(d.setPieceCount(p, domino, 4, 4).has_value());
  SolveOptions assemble;
  assemble.solutionLimit = 0;
  REQUIRE(d.solveStart(p, assemble).has_value());
  REQUIRE(d.solveWait(p).has_value());
  (void)d.poll();

  const auto trees = [&d] {
    unsigned int n = 0;
    const Problem& pr = d.puzzle().getProblem(0);
    for (unsigned int i = 0; i < pr.getNumberOfSavedSolutions(); i++) {
      n += pr.getSavedSolution(i).getDisassembly() != nullptr ? 1U : 0U;
    }
    return n;
  };
  const auto settle = [&d, p] {
    for (int i = 0; i < 60000 && d.analysing(p); i++) {
      (void)d.poll();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE_FALSE(d.analysing(p));
  };
  const unsigned int saved = d.puzzle().getProblem(0).getNumberOfSavedSolutions();
  REQUIRE(saved > 0);
  REQUIRE(trees() == 0);
  const test::DocState bare = test::state(d);
  const std::vector<std::string> before = d.history().undo;
  REQUIRE(before.back() == "Solve P1");
  const auto with = [&before](std::initializer_list<const char*> more) {
    std::vector<std::string> all = before;
    all.insert(all.end(), more.begin(), more.end());
    return all;
  };

  AnalyseOptions a;
  a.which = AnalyseWhich::All;
  const std::uint64_t rev = d.revision();
  REQUIRE(d.analyseStart(p, a).value() == saved);
  // the step is there at once, with a revision of its own, and cannot be
  // undone while the take-apart works (poll() has not taken it in)
  CHECK(d.revision() == rev + 1);
  CHECK(d.lastChange().what == 0);
  CHECK(d.history().undo == with({"Take apart P1"}));
  CHECK(d.undo().error().code == Errc::InvalidState);
  settle();
  REQUIRE(trees() == saved);
  const test::DocState apart = test::state(d);
  CHECK(d.dirty());

  // undo removes what it found, redo brings it back, to the byte
  REQUIRE(d.undo().has_value());
  CHECK(trees() == 0);
  CHECK(test::state(d) == bare);
  CHECK((d.lastChange().what & kChangeSolutions) != 0);
  CHECK(d.history().redo == std::vector<std::string>{"Take apart P1"});
  REQUIRE(d.redo().has_value());
  CHECK(trees() == saved);
  CHECK(test::state(d) == apart);

  // taken apart again with its step on top: no second step
  REQUIRE(d.analyseStart(p, a).value() == saved);
  settle();
  CHECK(d.history().undo == with({"Take apart P1"}));
  // nothing to take apart: nothing recorded, nothing changed
  a.which = AnalyseWhich::Missing;
  const std::uint64_t quiet = d.revision();
  CHECK(d.analyseStart(p, a).value() == 0);
  CHECK(d.revision() == quiet);
  // a solve after it is a step of its own, undone before the take-apart
  REQUIRE(d.solveStart(p, assemble).has_value());
  REQUIRE(d.solveWait(p).has_value());
  CHECK(d.history().undo == with({"Take apart P1", "Solve P1"}));
  CHECK(trees() == 0);
  REQUIRE(d.undo().has_value());
  CHECK(test::state(d) == apart);
  REQUIRE(d.undo().has_value());
  CHECK(test::state(d) == bare);
}

TEST_CASE("document solving: the autosave flow and saving", "[app][solving][resume]") {
  Document d = fresh("DemoPieceGenerator.xmpuzzle");
  const Id p = d.problemIds().front();
  const ScratchDir scratch("autosave");
  const std::filesystem::path tmp = scratch.path() / "autosave.xmpuzzle";
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

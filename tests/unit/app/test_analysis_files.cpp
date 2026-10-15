// SPDX-License-Identifier: GPL-3.0-or-later
// What stage 7 added to the document: taking saved assemblies apart
// afterwards (cb_AddDisasm / cb_AddAllDisasm), poll(), the paused solve
// files (BTSOLVE2, cb_ExportPaused / cb_ImportPaused) and the in-memory
// gzip they need.
#include "app_test_helpers.h"

#include "shapelogic/app/document.h"
#include "shapelogic/io/gzip_file.h"
#include "shapelogic/io/paused_solve.h"
#include "shapelogic/io/puzzle_file.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/solution.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>

using namespace bt;
using namespace bt::app;
using test::openFixture;

namespace {

Id firstProblem(const Document& d) { return d.problemIds().front(); }

// Polls until background work of the document is put in.
void settle(Document& d) {
  for (int i = 0; i < 6000 && d.solving(); i++) {
    (void)d.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  (void)d.poll();
  REQUIRE_FALSE(d.solving());
}

std::vector<std::string> levels(const Problem& pr) {
  std::vector<std::string> out;
  for (unsigned int i = 0; i < pr.getNumberOfSavedSolutions(); i++) {
    const Disassembly* d = pr.getSavedSolution(i).getDisassemblyInfo();
    out.push_back(d != nullptr ? d->movesText() : "-");
  }
  return out;
}

std::string fileBytes(const std::filesystem::path& p) {
  std::ifstream f(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

// A box of 2 x 4 x 4 cells and the 16 dominoes that fill it: 2856 assemblies.
Document dominoBox() {
  Document d(GridKind::Bricks);
  const Id box = d.createShape(std::array<unsigned int, 3>{2, 4, 4}).value();
  REQUIRE(d.editBox(box, CellTool::Fixed, 0, 0, CellBox{0, 0, 0, 1, 3, 3}).value() == 32);
  const Id domino = d.createShape(std::array<unsigned int, 3>{2, 1, 1}).value();
  REQUIRE(d.editBox(domino, CellTool::Fixed, 0, 0, CellBox{0, 0, 0, 1, 0, 0}).value() == 2);
  const Id p = d.createProblem().value();
  REQUIRE(d.setResult(p, box).has_value());
  REQUIRE(d.setPieceCount(p, domino, 16, 16).has_value());
  return d;
}

// Every saved assembly of a fixture taken apart again, compared with what
// the original's "Add disassembly" for all solutions wrote
// (tests/data/analysed_by_original/, `oracle.py record-analysed`).
void analysedAsTheOriginal(const std::string& name, bool rotations, SolverType solver,
                           const std::string& recorded) {
  INFO(name << " " << recorded);
  Document d = openFixture(name + ".xmpuzzle");
  for (const Id p : d.problemIds()) {
    REQUIRE(d.deleteDisassemblies(p, std::nullopt).has_value());
    AnalyseOptions o;
    o.which = AnalyseWhich::All;
    o.rotations = rotations;
    o.solver = solver;
    const std::uint64_t rev = d.revision();
    const unsigned int n = d.analyseStart(p, o).value();
    CHECK(n == d.puzzle().getProblem(d.problemIndex(p).value()).getNumberOfSavedSolutions());
    if (n == 0) {
      continue;
    }
    CHECK(d.analysing(p));
    settle(d);
    CHECK_FALSE(d.analysing(p));
    CHECK(d.revision() > rev);
    CHECK(d.lastChange().problems == std::vector<Id>{p});
    CHECK((d.lastChange().what & kChangeSolutions) != 0);
  }
  CHECK(io::writePuzzle(d.puzzle()) ==
        fileBytes("tests/data/analysed_by_original/" + recorded + ".xml"));
}

}  // namespace

TEST_CASE("analysis: the moves found again are the original's", "[app][analysis]") {
  for (const char* name : {"PelikanBurr", "Prisgon", "CubeInCage", "AlPackino", "Bermuda",
                           "DraculasDentalDesaster"}) {
    analysedAsTheOriginal(name, false, SolverType::Classic, name);
  }
  analysedAsTheOriginal("test_rotation_solvers_solved", true, SolverType::Classic,
                        "test_rotation_solvers_solved_rotations");
  analysedAsTheOriginal("test_rotation_solvers_solved", true, SolverType::Crowell,
                        "test_rotation_solvers_solved_rotations_crowell");
}

TEST_CASE("analysis: the original's level summaries are taken apart as it did", "[app][analysis][slow]") {
  // saved by the original with "just levels": 314 summaries, which the
  // trees found again agree with
  const Document d = openFixture("SolidSixPieceBurrs.xmpuzzle");
  const Document recorded = Document::openBytes(
                                fileBytes("tests/data/analysed_by_original/SolidSixPieceBurrs.xml"))
                                .value();
  CHECK(levels(recorded.puzzle().getProblem(0)) == levels(d.puzzle().getProblem(0)));
  analysedAsTheOriginal("SolidSixPieceBurrs", false, SolverType::Classic, "SolidSixPieceBurrs");
}

TEST_CASE("analysis: one solution, with turns, and what is refused", "[app][analysis]") {
  Document d = openFixture("test_rotation_solvers_solved.xmpuzzle");
  const Id p = firstProblem(d);
  const std::string before = io::writePuzzle(d.puzzle());
  REQUIRE(d.deleteDisassemblies(p, std::nullopt).has_value());
  AnalyseOptions o;
  o.which = AnalyseWhich::One;
  o.solution = d.puzzle().getProblem(0).getSavedSolution(0).getId();
  o.rotations = true;
  REQUIRE(d.analyseStart(p, o).value() == 1);
  // frozen meanwhile, as while solving
  CHECK(d.analyseStart(p, o).error().code == Errc::InvalidState);
  CHECK(d.solveStart(p, SolveOptions{}).error().code == Errc::InvalidState);
  CHECK(d.solveAbort(p).error().code == Errc::InvalidState);
  CHECK(d.createShape().error().code == Errc::InvalidState);
  CHECK(d.renameProblem(p, "x").error().code == Errc::InvalidState);
  CHECK(d.saveBytes().error().code == Errc::InvalidState);
  CHECK(d.undo().error().code == Errc::InvalidState);
  settle(d);
  CHECK(io::writePuzzle(d.puzzle()) == before);
  // nothing missing: nothing to do
  o.which = AnalyseWhich::Missing;
  CHECK(d.analyseStart(p, o).value() == 0);
  CHECK_FALSE(d.solving());
  o.which = AnalyseWhich::One;
  o.solution = 12345;
  CHECK(d.analyseStart(p, o).error().code == Errc::NotFound);
  CHECK(d.analyseStop(p).error().code == Errc::InvalidState);
  // the last analysis's end is kept once poll() put it in (an owner that
  // looks after the poll still learns its counts), until the next one
  const AnalyseProgress last = d.analyseProgress(p).value();
  CHECK_FALSE(last.active);
  CHECK(last.total == 1);
  CHECK(last.done == 1);
  CHECK(last.separated == 1);
  CHECK_FALSE(last.stopped);
  const Id none = d.createProblem().value();
  CHECK(d.analyseProgress(none).value().total == 0);

  Document spheres = openFixture("BallRoom_solved.xmpuzzle");
  CHECK(spheres.analyseStart(firstProblem(spheres), AnalyseOptions{}).error().code ==
        Errc::Unsupported);
  Document sliding = openFixture("test_sliding_solver.xmpuzzle");
  CHECK(sliding.analyseStart(firstProblem(sliding), AnalyseOptions{}).error().code ==
        Errc::Unsupported);
}

TEST_CASE("analysis: a stop keeps what was finished", "[app][analysis][parallel]") {
  Document d = openFixture("SolidSixPieceBurrs.xmpuzzle");
  const Id p = firstProblem(d);
  AnalyseOptions o;
  o.which = AnalyseWhich::All;
  o.threads = 2;
  REQUIRE(d.analyseStart(p, o).value() == 314);
  // the owner reads the progress and the document meanwhile
  for (int i = 0; i < 20; i++) {
    const AnalyseProgress a = d.analyseProgress(p).value();
    CHECK(a.total == 314);
    CHECK(a.done <= 314);
    (void)d.shapeTable();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  REQUIRE(d.analyseStop(p).has_value());
  for (int i = 0; i < 6000 && d.analyseProgress(p).value().active; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  const AnalyseProgress a = d.analyseProgress(p).value();
  CHECK_FALSE(a.active);
  CHECK(a.stopped == (a.done < a.total));
  CHECK(a.separated == a.done);
  settle(d);
  // the ones done have their moves, the others still their summaries
  unsigned int trees = 0;
  const Problem& pr = d.puzzle().getProblem(0);
  for (unsigned int i = 0; i < pr.getNumberOfSavedSolutions(); i++) {
    trees += pr.getSavedSolution(i).getDisassembly() != nullptr ? 1U : 0U;
    CHECK(pr.getSavedSolution(i).getDisassemblyInfo() != nullptr);
  }
  CHECK(trees == a.done);
}

TEST_CASE("document: poll notes the end of a run", "[app][analysis][parallel]") {
  Document d = openFixture("DemoPieceGenerator.xmpuzzle");
  const Id p = firstProblem(d);
  CHECK_FALSE(d.poll());
  SolveOptions o;
  o.disassemble = true;
  REQUIRE(d.solveStart(p, o).has_value());
  REQUIRE(d.solveWait(p).has_value());
  const std::uint64_t rev = d.revision();
  CHECK(d.poll());
  CHECK(d.revision() == rev + 1);
  CHECK(d.lastChange().problems == std::vector<Id>{p});
  CHECK(d.dirty());
  CHECK_FALSE(d.poll());
  CHECK(d.revision() == rev + 1);
}

TEST_CASE("gzip in memory: as the files are written", "[io]") {
  const std::string text = std::string(1000, 'x') + "<puzzle/>";
  const std::string z = io::gzipCompress(text);
  CHECK(static_cast<unsigned char>(z[0]) == 0x1f);
  CHECK(io::gunzipIfCompressed(z).value() == text);
  CHECK(io::gunzipIfCompressed(text).value() == text);
  CHECK(io::gunzipIfCompressed("").value().empty());
  CHECK(io::gunzipIfCompressed(z.substr(0, z.size() / 2)).error().code == Errc::ParseError);
  const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "bt_gzip.gz";
  REQUIRE(io::writeGzipFile(tmp, text).has_value());
  CHECK(fileBytes(tmp) == z);
  // what inflates past the limit is refused, in memory and from a file: a
  // small stream can stand for gigabytes (the limit is 256 MiB; here a
  // smaller one on a 4 MiB stream of zeros, which gzip packs into 4 KiB)
  const std::string bomb = io::gzipCompress(std::string(std::size_t{4} << 20, '\0'));
  CHECK(bomb.size() < 8192);
  CHECK(io::gunzipIfCompressed(bomb).value().size() == std::size_t{4} << 20);
  CHECK(io::gunzipIfCompressed(bomb, std::size_t{1} << 20).error().code == Errc::ParseError);
  CHECK(io::gunzipIfCompressed(z, text.size() - 1).error().code == Errc::ParseError);
  CHECK(io::gunzipIfCompressed(z, text.size()).value() == text);
  CHECK(io::readMaybeGzipFile(tmp, text.size() - 1).error().code == Errc::ParseError);
  CHECK(io::readMaybeGzipFile(tmp, text.size()).value() == text);
  std::filesystem::remove(tmp);
  // a puzzle's file bytes open as a document either way
  Document d = openFixture("PelikanBurr.xmpuzzle");
  const std::string xml = d.saveBytes().value();
  CHECK(io::writePuzzle(Document::openBytes(io::gzipCompress(xml)).value().puzzle()) == xml);
}

TEST_CASE("paused solves: BTSOLVE2 both ways", "[io][app][analysis]") {
  Document d = dominoBox();
  const Id p = firstProblem(d);
  const std::filesystem::path file = std::filesystem::temp_directory_path() / "bt_paused.slsolve";
  CHECK(d.exportPaused(p, file).error().code == Errc::InvalidState);  // nothing paused
  REQUIRE(d.solvePrepare(p, SolveOptions{}).has_value());
  REQUIRE(d.solveWait(p).has_value());
  REQUIRE(d.solveState(p) == SolveState::Paused);
  REQUIRE(d.exportPaused(p, file).has_value());

  // the original's layout: magic, then name length, name, size, bytes
  const std::string bytes = fileBytes(file);
  REQUIRE(bytes.starts_with("BTSOLVE2"));
  CHECK(bytes.substr(8, 4) == std::string("\x06\0\0\0", 4));
  CHECK(bytes.substr(12, 6) == "puzzle");
  std::uint64_t size = 0;
  for (int i = 7; i >= 0; i--) {
    size = size << 8 | static_cast<unsigned char>(bytes[18 + static_cast<std::size_t>(i)]);
  }
  CHECK(static_cast<unsigned char>(bytes[26]) == 0x1f);
  const std::size_t after = 26 + static_cast<std::size_t>(size);
  CHECK(bytes.substr(after, 4) == std::string("\x07\0\0\0", 4));
  CHECK(bytes.substr(after + 4, 7) == "problem");
  CHECK(bytes.substr(after + 19) == "0");
  CHECK(bytes.size() == after + 20);

  // read back: a new, untitled document carrying the paused solve on
  auto imported = Document::importPaused(file).value();
  Document& e = imported.first;
  CHECK(imported.second == e.problemIds()[0]);
  CHECK(e.dirty());
  CHECK(io::writePuzzle(e.puzzle()) == io::writePuzzle(d.puzzle()));
  SolveOptions o;
  o.disassemble = true;
  REQUIRE(e.solveResume(imported.second, o).has_value());
  REQUIRE(e.solveWait(imported.second).has_value());
  REQUIRE(e.solveState(imported.second) == SolveState::Finished);
  CHECK(e.puzzle().getProblem(0).getNumAssemblies() == 2856);
  CHECK(e.puzzle().getProblem(0).getNumSolutions() == 2856);
  std::filesystem::remove(file);

  // what the reader refuses, and what it skips
  const io::PausedSolve ps = io::parsePausedSolve(bytes).value();
  CHECK(ps.problem == 0);
  CHECK(io::parsePausedSolve("BTSOLVE1").error().code == Errc::ParseError);
  CHECK(io::parsePausedSolve(bytes.substr(0, bytes.size() - 1)).error().code == Errc::ParseError);
  CHECK(io::parsePausedSolve(bytes.substr(0, after)).error().code == Errc::ParseError);
  std::string extra = bytes;
  extra += std::string("\x0d\0\0\0", 4) + "searchz/x.lvl" + std::string("\x03\0\0\0\0\0\0\0", 8) + "abc";
  CHECK(io::parsePausedSolve(extra).value().problem == 0);
  std::string wrong = bytes;
  wrong.back() = '5';
  CHECK(io::parsePausedSolve(wrong).error().code == Errc::ParseError);
  CHECK(Document::importPaused("tests/data/puzzles/PelikanBurr.xmpuzzle").error().code ==
        Errc::ParseError);
  CHECK(Document::importPaused("tests/data/no such file").error().code == Errc::IoError);
}

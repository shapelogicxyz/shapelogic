// SPDX-License-Identifier: GPL-3.0-or-later
// Rotation take-apart and the take-apart over threads. Ported from the
// original's test/test_rotation_solvers.cpp (its "solve progress" case
// belongs to the solve session, stage 5); the solve thread it used is
// replaced by an assembler and a disassembler, as the solve thread drove
// them. The cases after the ported ones check the A/B switches and the
// movement analysator's calls for the GUI.
#include "../model/model_test_helpers.h"

#include "bt/base/stop.h"
#include "bt/base/thread_pool.h"
#include "bt/grid/voxel.h"
#include "bt/model/assembly.h"
#include "bt/model/disassembly.h"
#include "bt/model/problem.h"
#include "bt/model/puzzle.h"
#include "bt/solve/assembler.h"
#include "bt/solve/disassembler.h"
#include "bt/solve/movement_analysator.h"
#include "bt/solve/rotation_rules.h"
#include "bt/solve/solver_type.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace bt;

namespace {

// Every assembly of problem 0, on one thread, the matrix reduced (as the
// solve does); with initHotspot after the matrix, as the solve thread.
std::vector<Assembly> allAssemblies(Puzzle& puzzle, SolverType solver = SolverType::Classic) {
  const Problem& pr = puzzle.getProblem(0);
  std::unique_ptr<Assembler> assm = makeAssembler(puzzle, pr, solver);
  REQUIRE(assm->createMatrix({}) == AssemblerError::None);
  (void)assm->reduce(StopToken{});
  for (unsigned int i = 0; i < puzzle.getNumberOfShapes(); i++) {
    puzzle.getShape(i).initHotspot();
  }
  std::vector<Assembly> res;
  SearchOptions o;
  o.threads = 1;
  REQUIRE(assm->assemble(
              [&res](const Assembly& a) {
                res.push_back(a);
                return true;
              },
              o, StopToken{}) == RunEnd::Finished);
  return res;
}

// A tree as text: pieces, every state (positions, orientations, how a turn
// arrived) and both branches. Equal text, equal trees.
void treeText(const Separation* s, std::ostringstream& out) {
  if (s == nullptr) {
    out << "-";
    return;
  }
  out << "[";
  for (const unsigned int p : s->getPieces()) {
    out << p << ",";
  }
  out << "|";
  for (unsigned int k = 0; k < s->getNumStates(); k++) {
    const State& st = s->getState(k);
    for (unsigned int i = 0; i < st.getPiecenumber(); i++) {
      out << st.getX(i) << " " << st.getY(i) << " " << st.getZ(i) << " " << st.getOrient(i)
          << ";";
    }
    if (st.isRotationArrival()) {
      out << "R" << st.getRotPiece() << " " << st.getRotPivotX() << " " << st.getRotPivotY()
          << " " << st.getRotPivotZ() << " " << st.getRotAxis() << " " << st.getRotSense();
    }
    out << "/";
  }
  treeText(s->getRemoved(), out);
  treeText(s->getLeft(), out);
  out << "]";
}

std::string treeText(const TakeApartResult& r) {
  if (!r.separation) {
    return r.stopped ? "stopped" : "none";
  }
  std::ostringstream out;
  treeText(&*r.separation, out);
  return out.str();
}

// The EnigmaTIC fixture solved with rotation checks: one assembly comes
// apart, in 3R1.1R1.3R0.2R1 (9 slides and 3 turns). The saved solution of
// the file is not used.
void requireEnigmaRotationSolution(SolverType type) {
  INFO(solverTypeLabel(type));
  Puzzle puzzle = test::loadFixture("test_rotation_solvers.xmpuzzle");
  const std::vector<Assembly> assemblies = allAssemblies(puzzle, type);

  DisassemblerOptions o;
  o.rotations = true;
  o.solver = type;
  Disassembler d(puzzle, puzzle.getProblem(0), o);

  unsigned int solutions = 0;
  std::string level;
  unsigned int moves = 0;
  unsigned int rotations = 0;
  for (const Assembly& a : assemblies) {
    const TakeApartResult r = d.disassemble(a);
    if (r.separation) {
      solutions++;
      level = r.separation->movesText();
      moves = r.separation->sumMoves();
      rotations = r.separation->sumRotations();
    }
  }

  REQUIRE(solutions == 1);
  CHECK(level == "3R1.1R1.3R0.2R1");
  CHECK(moves == 9);
  CHECK(rotations == 3);
}

}  // namespace

TEST_CASE("EnigmaTIC with Check Rotations: Classic, Crowell, and BurrTools 2 agree",
          "[solve][disasm][rotations]") {
  SECTION("BurrTools Classic") { requireEnigmaRotationSolution(SolverType::Classic); }
  SECTION("Andrew Crowell") { requireEnigmaRotationSolution(SolverType::Crowell); }
  SECTION("BurrTools 2") { requireEnigmaRotationSolution(SolverType::Bt2); }
}

namespace {

using Cell = RotationRules::Cell;
using Pivot = RotationRules::Pivot;

// A small piece (2 to maxExtra + 1 cells around (4, 4, 4)) and random other
// cells in a 9 x 9 x 9 box.
void randomScene(std::mt19937& rng, int maxExtra, unsigned int density0, unsigned int densitySpan,
                 std::vector<Cell>& piece, std::vector<Cell>& others) {
  static const int kD[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  piece.assign(1, Cell(4, 4, 4));
  const int n = 2 + static_cast<int>(rng() % static_cast<unsigned int>(maxExtra));
  while (static_cast<int>(piece.size()) < n) {
    const Cell b = piece[rng() % piece.size()];
    const int* dd = kD[rng() % 6];
    const Cell c(b.x + dd[0], b.y + dd[1], b.z + dd[2]);
    bool have = false;
    for (const Cell& p : piece) {
      have = have || (p.x == c.x && p.y == c.y && p.z == c.z);
    }
    if (!have) {
      piece.push_back(c);
    }
  }
  others.clear();
  const unsigned int density = density0 + rng() % densitySpan;
  for (int x = 0; x < 9; x++) {
    for (int y = 0; y < 9; y++) {
      for (int z = 0; z < 9; z++) {
        bool onPiece = false;
        for (const Cell& p : piece) {
          onPiece = onPiece || (p.x == x && p.y == y && p.z == z);
        }
        if (!onPiece && rng() % 100 < density) {
          others.emplace_back(x, y, z);
        }
      }
    }
  }
}

// Every rule answer for many random but consistent cases, as one hash: the
// rules must give exactly the original's answers.
std::uint64_t rotationRulesHash(unsigned int cases, double* seconds) {
  std::mt19937 rng(12345);
  RotationRules rules;
  std::uint64_t h = 1469598103934665603ULL;
  const auto t0 = std::chrono::steady_clock::now();
  for (unsigned int k = 0; k < cases; k++) {
    // a piece of 2 to 5 cells, others in 5 to 34 percent of the box (the
    // original's draws, in its order)
    std::vector<Cell> piece;
    std::vector<Cell> others;
    randomScene(rng, 4, 5, 30, piece, others);
    const int cx = 4;
    const int cy = 4;
    const int cz = 4;
    const Pivot pivot(2 * cx, 2 * cy, 2 * cz);
    for (unsigned int axis = 0; axis < 3; axis++) {
      for (unsigned int sense = 0; sense < 2; sense++) {
        std::vector<Cell> end;
        for (const Cell& p : piece) {
          const int dx = p.x - cx;
          const int dy = p.y - cy;
          const int dz = p.z - cz;
          int ex = dx;
          int ey = dy;
          int ez = dz;
          const int s = sense != 0 ? -1 : 1;
          if (axis == 0) {
            ey = -s * dz;
            ez = s * dy;
          } else if (axis == 1) {
            ex = s * dz;
            ez = -s * dx;
          } else {
            ex = -s * dy;
            ey = s * dx;
          }
          end.emplace_back(cx + ex, cy + ey, cz + ez);
        }
        const bool ok = rules.allowRotation(others, piece, end, pivot, axis, sense);
        const bool blocked = rules.axisBlocked(others, piece, axis);
        h = (h ^ (ok ? 3U : 1U) ^ (blocked ? 8U : 4U)) * 1099511628211ULL;
      }
    }
  }
  *seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  return h;
}

}  // namespace

TEST_CASE("rotation rules: the same answers as the original (hash)", "[solve][rotation]") {
  double s = 0;
  const std::uint64_t h = rotationRulesHash(400, &s);
  INFO("hash " << h);
  CHECK(h == 3649067536594071595ULL);
}

TEST_CASE("rotation rules: the prepared path agrees with allowRotation", "[solve][rotation]") {
  std::mt19937 rng(777);
  RotationRules rules;
  RotationRules reference;
  unsigned int allowed = 0;
  unsigned int mismatches = 0;
  for (unsigned int k = 0; k < 600; k++) {
    std::vector<Cell> piece;
    std::vector<Cell> others;
    randomScene(rng, 5, 2, 25, piece, others);

    rules.setBodies(others, piece);
    for (unsigned int axis = 0; axis < 3; axis++) {
      const bool blocked = rules.preparedAxisBlocked(axis);
      if (blocked != reference.axisBlocked(others, piece, axis)) {
        mismatches++;
      }
      for (int tries = 0; tries < 6; tries++) {
        const Pivot pivot(5 + static_cast<int>(rng() % 7), 5 + static_cast<int>(rng() % 7),
                          5 + static_cast<int>(rng() % 7));
        for (unsigned int sense = 0; sense < 2; sense++) {
          std::vector<Cell> end;
          bool onGrid = true;
          for (const Cell& p : piece) {
            Cell e;
            onGrid = onGrid && RotationRules::rotateCell(p, pivot, axis, sense, e);
            end.push_back(e);
          }
          const bool want =
              onGrid && reference.allowRotation(others, piece, end, pivot, axis, sense);
          if (rules.allowPrepared(pivot, axis, sense, false) != want) {
            mismatches++;
          }
          // a blocked axis allows nothing
          if (blocked && want) {
            mismatches++;
          }
          if (!blocked && rules.allowPrepared(pivot, axis, sense, true) != want) {
            mismatches++;
          }
          if (want) {
            allowed++;
          }
        }
      }
    }
  }
  CHECK(mismatches == 0);
  CHECK(allowed > 100);
}

TEST_CASE("rotation rules: benchmark", "[.bench][rotation]") {
  double s = 0;
  const std::uint64_t h = rotationRulesHash(20000, &s);
  std::printf("rotation rules: 20000 cases in %.2f s, hash %llu\n", s,
              static_cast<unsigned long long>(h));
}

// The rules do not care how the whole scene lies: the scene turned a
// quarter about z (x becomes y, y becomes -x) gets the same answer for the
// matching turn. The Classic generator's one position per arrangement
// depends on it.
TEST_CASE("rotation rules: a turned scene gets the same answers", "[solve][rotation]") {
  std::mt19937 rng(4242);
  RotationRules a;
  RotationRules b;
  unsigned int differ = 0;
  unsigned int allowed = 0;
  auto turned = [](const Cell& c) { return Cell(-c.y - 1, c.x, c.z); };
  for (unsigned int k = 0; k < 500; k++) {
    std::vector<Cell> piece;
    std::vector<Cell> others;
    randomScene(rng, 6, 2, 20, piece, others);
    std::vector<Cell> piece2;
    std::vector<Cell> others2;
    for (const Cell& c : piece) {
      piece2.push_back(turned(c));
    }
    for (const Cell& c : others) {
      others2.push_back(turned(c));
    }
    a.setBodies(others, piece);
    b.setBodies(others2, piece2);
    for (unsigned int axis = 0; axis < 3; axis++) {
      for (int t = 0; t < 8; t++) {
        const Pivot pivot(5 + static_cast<int>(rng() % 7), 5 + static_cast<int>(rng() % 7),
                          5 + static_cast<int>(rng() % 7));
        const Pivot pivot2(-pivot.hy - 2, pivot.hx, pivot.hz);
        for (unsigned int sense = 0; sense < 2; sense++) {
          const unsigned int axis2 = axis == 0 ? 1 : axis == 1 ? 0 : 2;
          const unsigned int sense2 = axis == 1 ? 1 - sense : sense;
          const bool ra = a.allowPrepared(pivot, axis, sense, false);
          if (ra) {
            allowed++;
          }
          if (ra != b.allowPrepared(pivot2, axis2, sense2, false)) {
            differ++;
          }
        }
      }
    }
  }
  CHECK(differ == 0);
  CHECK(allowed > 1000);
}

namespace {

// Takes every assembly (at most 40) apart on one thread and with every
// level spread over four: the very same trees.
void requireSameOnThreads(const char* file, bool rotations, SolverType type, bool comesApart) {
  INFO(file << ", " << solverTypeLabel(type) << (rotations ? ", rotations" : ""));

  Puzzle puzzle = test::loadFixture(file);
  puzzle.getProblem(0).removeAllSolutions();
  const std::vector<Assembly> assemblies = allAssemblies(puzzle);
  REQUIRE(!assemblies.empty());

  ThreadPool pool(4);

  DisassemblerOptions one;
  one.rotations = rotations;
  one.solver = type;
  DisassemblerOptions spread = one;
  spread.pool = &pool;
  spread.levelThreadCostUs = 0;

  Disassembler serial(puzzle, puzzle.getProblem(0), one);
  Disassembler threaded(puzzle, puzzle.getProblem(0), spread);

  unsigned int found = 0;
  for (std::size_t i = 0; i < assemblies.size() && i < 40; i++) {
    INFO("assembly " << i);
    const TakeApartResult a = serial.disassemble(assemblies[i]);
    const TakeApartResult b = threaded.disassemble(assemblies[i]);
    CHECK(treeText(a) == treeText(b));
    CHECK_FALSE(a.stopped);
    if (a.separation) {
      found++;
    }
  }
  CHECK((found > 0) == comesApart);
}

}  // namespace

TEST_CASE("a take-apart spread over threads gives the same disassembly as on one",
          "[solve][disasm][parallel][slow]") {
  for (const SolverType type : {SolverType::Classic, SolverType::Crowell}) {
    requireSameOnThreads("test_rotation_solvers.xmpuzzle", true, type, true);
    // without rotations the puzzle does not come apart: the whole search is
    // gone through, on any number of threads
    requireSameOnThreads("test_rotation_solvers.xmpuzzle", false, type, false);
    requireSameOnThreads("PelikanBurr.xmpuzzle", false, type, true);
  }
}

TEST_CASE("a take-apart says how far it is, and stops when told to",
          "[solve][disasm][parallel][progress]") {
  Puzzle puzzle = test::loadFixture("test_rotation_solvers.xmpuzzle");
  puzzle.getProblem(0).removeAllSolutions();
  const std::vector<Assembly> assemblies = allAssemblies(puzzle);
  REQUIRE(!assemblies.empty());

  ThreadPool pool(4);

  SECTION("progress is there while it runs and gone afterwards") {
    DisassemblerOptions o;
    o.rotations = true;
    o.pool = &pool;
    o.levelThreadCostUs = 0;
    Disassembler d(puzzle, puzzle.getProblem(0), o);

    CHECK_FALSE(d.progress().active);

    std::atomic<bool> done{false};
    std::atomic<std::uint64_t> mostNodes{0};
    std::atomic<unsigned int> mostPieces{0};
    std::atomic<unsigned int> bad{0};
    std::thread watcher([&] {
      while (!done.load()) {
        const DisassemblyProgress q = d.progress();
        if (q.active) {
          if (q.nodes > mostNodes.load()) {
            mostNodes.store(q.nodes);
          }
          if (q.pieces > mostPieces.load()) {
            mostPieces.store(q.pieces);
          }
          if (q.levelDone > q.levelSize || q.separations >= q.pieces) {
            bad++;
          }
        }
        std::this_thread::yield();
      }
    });

    bool any = false;
    for (const Assembly& a : assemblies) {
      if (d.disassemble(a).separation) {
        any = true;
      }
    }
    done.store(true);
    watcher.join();

    CHECK(any);
    CHECK(bad.load() == 0);
    CHECK(mostNodes.load() > 0);
    CHECK(mostPieces.load() == puzzle.getProblem(0).getNumberOfPieces());
    CHECK_FALSE(d.progress().active);
  }

  SECTION("a stopped take-apart gives no disassembly, and says it was stopped") {
    for (const SolverType type : {SolverType::Classic, SolverType::Crowell}) {
      DisassemblerOptions o;
      o.rotations = true;
      o.solver = type;
      o.pool = &pool;
      o.levelThreadCostUs = 0;
      Disassembler d(puzzle, puzzle.getProblem(0), o);
      const StopSource stop;
      stop.request(StopKind::Pause);
      for (const Assembly& a : assemblies) {
        const TakeApartResult r = d.disassemble(a, stop.token());
        CHECK_FALSE(r.separation);
        CHECK(r.stopped);
      }
      // and the next take-apart, with a token not stopped, runs again
      bool any = false;
      for (const Assembly& a : assemblies) {
        any = any || d.disassemble(a).separation.has_value();
      }
      CHECK(any);
    }
  }

  SECTION("a stop from another thread ends a take-apart under way") {
    DisassemblerOptions o;
    o.rotations = true;
    o.pool = &pool;
    o.levelThreadCostUs = 0;
    Disassembler d(puzzle, puzzle.getProblem(0), o);
    // without rotations as well: the full search that does not come apart
    d.setCheckRotations(false);
    const StopSource stop;
    std::thread stopper([&stop] {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      stop.request(StopKind::Pause);
    });
    // until the stop has come (every take-apart after it is stopped)
    unsigned int stopped = 0;
    for (int round = 0; round < 100000 && stopped == 0; round++) {
      for (const Assembly& a : assemblies) {
        const TakeApartResult r = d.disassemble(a, stop.token());
        CHECK_FALSE(r.separation);
        stopped += r.stopped ? 1 : 0;
      }
    }
    stopper.join();
    CHECK(stopped > 0);
  }
}

// The A/B switches give the very same trees.
TEST_CASE("take-apart switches: the fast analysis and the fast turn rules change nothing",
          "[solve][disasm][rotations][slow]") {
  // turns on the rotation puzzle only: Pelikan has too many to try them all
  for (const auto& [file, rotations] :
       {std::pair{"test_rotation_solvers.xmpuzzle", true}, std::pair{"PelikanBurr.xmpuzzle", false}}) {
    INFO(file);
    Puzzle puzzle = test::loadFixture(file);
    puzzle.getProblem(0).removeAllSolutions();
    const std::vector<Assembly> assemblies = allAssemblies(puzzle);

    DisassemblerOptions fast;
    fast.rotations = rotations;
    DisassemblerOptions slowAnalysis = fast;
    slowAnalysis.analysis.fast = false;
    DisassemblerOptions slowRotations = fast;
    slowRotations.analysis.rotations.fast = false;

    Disassembler a(puzzle, puzzle.getProblem(0), fast);
    Disassembler b(puzzle, puzzle.getProblem(0), slowAnalysis);
    Disassembler c(puzzle, puzzle.getProblem(0), slowRotations);
    for (std::size_t i = 0; i < assemblies.size() && i < 10; i++) {
      INFO("assembly " << i);
      const std::string t = treeText(a.disassemble(assemblies[i]));
      CHECK(treeText(b.disassemble(assemblies[i])) == t);
      CHECK(treeText(c.disassemble(assemblies[i])) == t);
    }
  }
}

// What the GUI's movement explorer asks of the analysator.
TEST_CASE("movement analysator: completeFind and findMatching", "[solve][disasm]") {
  Puzzle puzzle = test::loadFixture("PelikanBurr.xmpuzzle");
  const std::vector<Assembly> assemblies = allAssemblies(puzzle);
  REQUIRE(!assemblies.empty());
  const Problem& problem = puzzle.getProblem(0);

  MovementAnalysator an(puzzle, problem, false, SolverType::Classic, {});
  const NodeRef start = NodeRef::make(assemblies[0]);
  std::vector<unsigned int> pieces;
  for (unsigned int j = 0; j < assemblies[0].placementCount(); j++) {
    if (assemblies[0].isPlaced(j)) {
      pieces.push_back(j);
    }
  }

  std::vector<NodeRef> moves;
  an.completeFind(start.get(), pieces, moves);
  REQUIRE(!moves.empty());
  for (std::size_t i = 0; i < moves.size(); i++) {
    CHECK(moves[i]->getComefrom() == start.get());
    // one step, or a removal
    CHECK((moves[i]->getAmount() == 1 || moves[i]->getAmount() == 30000));
    for (std::size_t j = 0; j < i; j++) {
      CHECK_FALSE(*moves[i] == *moves[j]);
    }
  }

  // each one-step move of a single piece is found again by findMatching
  unsigned int matched = 0;
  for (const NodeRef& m : moves) {
    if (m->getAmount() != 1 || m->isSeparation()) {
      continue;
    }
    for (unsigned int i = 0; i < pieces.size(); i++) {
      const int dx = m->getX(i) - start->getX(i);
      const int dy = m->getY(i) - start->getY(i);
      const int dz = m->getZ(i) - start->getZ(i);
      if (dx == 0 && dy == 0 && dz == 0) {
        continue;
      }
      const NodeRef f = an.findMatching(start.get(), pieces, pieces[i], dx, dy, dz);
      REQUIRE(f);
      CHECK(f->getX(i) - start->getX(i) == dx);
      CHECK(f->getY(i) - start->getY(i) == dy);
      CHECK(f->getZ(i) - start->getZ(i) == dz);
      matched++;
      break;
    }
  }
  CHECK(matched > 0);
}

// The turn generators hold a position's pieces in a 32-bit mask. The
// original shifted past it on a position of 33 pieces (undefined behaviour,
// which the sanitizer build stops at) and would go through 2^32 groups at
// 32; now a position of more than kMaxTurnPieces gets no turns, and the
// parts it comes apart into get them again (stage 4 audit, finding 3).
TEST_CASE("rotation take-apart: 33 pieces come apart without overflowing the piece masks",
          "[solve][rotation]") {
  const std::string xml =
      R"(<puzzle version="2"><gridType type="0"/><colors/><shapes>)"
      R"(<voxel x="33" y="1" z="1" type="0">#################################</voxel>)"
      R"(<voxel x="1" y="1" z="1" type="0">#</voxel></shapes>)"
      R"(<problems><problem state="0"><shapes><shape id="1" count="33"/></shapes>)"
      R"(<result id="0"/><bitmap/></problem></problems></puzzle>)";
  Result<Puzzle> loaded = io::parsePuzzle(xml);
  REQUIRE(loaded.has_value());
  Puzzle& puzzle = *loaded;
  const std::vector<Assembly> assemblies = allAssemblies(puzzle);
  REQUIRE(assemblies.size() == 1);

  for (const SolverType solver : {SolverType::Classic, SolverType::Crowell}) {
    INFO(solverTypeLabel(solver));
    DisassemblerOptions o;
    o.rotations = true;
    o.solver = solver;
    Disassembler d(puzzle, puzzle.getProblem(0), o);
    const TakeApartResult r = d.disassemble(assemblies[0]);
    REQUIRE(r.separation.has_value());
    CHECK(r.separation->sumMoves() == 32);
  }
}

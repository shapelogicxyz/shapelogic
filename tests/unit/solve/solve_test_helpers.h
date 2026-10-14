// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Helpers for the assembler tests: fingerprints of assemblies (to compare
// runs as multisets, AGENTS.md), runs with a pool, and the pentomino puzzle
// of the original's test_solver.cpp.

#include "shapelogic/base/stop.h"
#include "shapelogic/base/thread_pool.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/model/assembly.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/solve/assembler.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace bt::test {

using Fingerprints = std::multiset<std::string>;

/// The placements of an assembly as text ("t,x,y,z;" per piece, "-;" for an
/// unplaced one): equal for equal assemblies.
inline std::string fingerprint(const Assembly& a) {
  std::string s;
  for (unsigned int i = 0; i < a.placementCount(); i++) {
    if (a.isPlaced(i)) {
      s += std::to_string(a.getTransformation(i)) + "," + std::to_string(a.getX(i)) + "," +
           std::to_string(a.getY(i)) + "," + std::to_string(a.getZ(i)) + ";";
    } else {
      s += "-;";
    }
  }
  return s;
}

/// Search options for `threads` threads from `pool`, with or without the
/// vector search.
inline SearchOptions searchOptions(unsigned int threads, ThreadPool* pool,
                                   bool vectorSearch = true) {
  SearchOptions o;
  o.threads = threads;
  o.pool = pool;
  o.vectorSearch = vectorSearch;
  return o;
}

/// One run that records every assembly.
struct RunResult {
  Fingerprints found;
  RunEnd end = RunEnd::Stopped;
};

inline RunResult runOnce(Assembler& a, const SearchOptions& o, const StopToken& stop = {},
                         unsigned int stopAfter = 0) {
  RunResult r;
  r.end = a.assemble(
      [&r, stopAfter](Assembly asm_) {
        r.found.insert(fingerprint(asm_));
        return stopAfter == 0 || r.found.size() < stopAfter;
      },
      o, stop);
  return r;
}

/// An assembler of type A for problem 0 of `p`, its matrix made and reduced.
template <class A>
std::unique_ptr<Assembler> prepared(const Puzzle& p) {
  auto a = std::make_unique<A>(p, p.getProblem(0));
  REQUIRE(a->createMatrix({}) == AssemblerError::None);
  (void)a->reduce(StopToken{});
  return a;
}

/// A way a search runs: its threads, and whether it may take the vector
/// search.
struct PausePath {
  unsigned int threads;
  bool vector;
};

/// The plain and the vector search, each on one thread and split over four.
inline const std::vector<PausePath> kAllPaths = {{1, false}, {1, true}, {4, true}, {4, false}};

/// Pauses a search at random moments, from the callback and from another
/// thread, and continues it (in memory, or with `throughSaves` every other
/// time through a saved position) until it is through: every assembly
/// exactly once, on every path. `make` gives a prepared assembler. Only every
/// other run is paused from outside: a stopped vector search starts again
/// from the beginning, so pauses that always come too soon would never let
/// it reach anything new; the runs between are stopped by the callback,
/// after new assemblies.
template <class Make>
void randomPauses(Make make, std::size_t expected, unsigned int maxStopAfter, std::uint32_t seed,
                  const std::vector<PausePath>& paths, bool throughSaves = true) {
  ThreadPool pool(4);
  std::mt19937 rng(seed);

  for (const PausePath path : paths) {
    INFO(path.threads << " threads" << (path.vector ? "" : ", plain"));
    Fingerprints all;
    {
      std::unique_ptr<Assembler> a = make();
      all = runOnce(*a, searchOptions(4, &pool)).found;
      REQUIRE(all.size() == expected);
    }

    std::unique_ptr<Assembler> a = make();

    Fingerprints found;
    unsigned int runs = 0;
    bool viaSave = false;
    for (;;) {
      runs++;
      REQUIRE(runs < 2000);
      const StopSource stop;
      const unsigned int stopAfter =
          std::uniform_int_distribution<unsigned int>(1, maxStopAfter)(rng);
      const auto delay =
          std::chrono::microseconds(std::uniform_int_distribution<long>(0, 3000)(rng));
      std::thread stopper([&stop, delay, outside = runs % 2 == 1] {
        if (outside) {
          std::this_thread::sleep_for(delay);
          stop.request(StopKind::Pause);
        }
      });
      unsigned int thisRun = 0;
      const RunEnd end = a->assemble(
          [&found, &thisRun, stopAfter](Assembly asm_) {
            found.insert(fingerprint(asm_));
            return ++thisRun < stopAfter;
          },
          searchOptions(path.threads, &pool, path.vector), stop.token());
      stopper.join();
      if (end == RunEnd::Finished) {
        break;
      }
      // every other stop goes through a saved position
      viaSave = throughSaves && !viaSave;
      if (viaSave) {
        const SavedPosition pos = a->savePosition();
        a = make();
        REQUIRE(a->restorePosition(pos.text, pos.version) == AssemblerError::None);
      }
    }
    CHECK(runs > 2);
    CHECK(found == all);
    CHECK(a->finished() >= 1.0F);
  }
}

/// True when every assembly of `part` is in `whole`, at most as often.
inline bool isSubMultiset(const Fingerprints& part, const Fingerprints& whole) {
  return std::includes(whole.begin(), whole.end(), part.begin(), part.end());
}

/// A plain search stopped after `steps` steps of the debugger (which stops
/// where a pause from outside does, at the top of the search loop),
/// continued on every path, in memory and through its saved position: each
/// continuation must find what the plain search on one thread finds from
/// there (a stopped search is never taken for one that has not started).
/// Returns that.
template <class Make>
Fingerprints continueOnEveryPath(Make make, unsigned long steps, ThreadPool& pool) {
  std::unique_ptr<Assembler> stepped = make();
  stepped->debugStep(steps);
  const SavedPosition pos = stepped->savePosition();
  INFO("stopped after " << steps << " steps at \"" << pos.text.substr(0, 60) << "\"");

  Fingerprints reference;
  bool first = true;
  for (const PausePath path : kAllPaths) {
    INFO(path.threads << " threads" << (path.vector ? "" : ", plain"));
    for (const bool viaSave : {false, true}) {
      INFO((viaSave ? "through the saved position" : "in memory"));
      std::unique_ptr<Assembler> a = make();
      if (viaSave) {
        REQUIRE(a->restorePosition(pos.text, pos.version) == AssemblerError::None);
      } else {
        a->debugStep(steps);
      }
      const RunResult r = runOnce(*a, searchOptions(path.threads, &pool, path.vector));
      CHECK(r.end == RunEnd::Finished);
      if (first) {
        reference = r.found;
        first = false;
      } else {
        CHECK(r.found == reference);
      }
    }
  }
  return reference;
}

/// A vector search stopped before it reports anything, then the plain
/// search paused by the callback: the saved position restores and carries
/// on (the stopped vector search's flag must not stay behind).
template <class Make>
void vectorStoppedAtOnceThenPlain(Make make, const Fingerprints& all) {
  std::unique_ptr<Assembler> a = make();
  const StopSource early;
  early.request(StopKind::Pause);
  const RunResult r1 = runOnce(*a, searchOptions(1, nullptr, true), early.token());
  REQUIRE(r1.end == RunEnd::Stopped);
  REQUIRE(r1.found.empty());

  const RunResult r2 = runOnce(*a, searchOptions(1, nullptr, false), {}, 5);
  REQUIRE(r2.end == RunEnd::Stopped);
  REQUIRE(r2.found.size() == 5);
  const SavedPosition pos = a->savePosition();
  INFO(pos.text.substr(0, 60));
  CHECK(pos.text[0] == '0');

  std::unique_ptr<Assembler> b = make();
  REQUIRE(b->restorePosition(pos.text, pos.version) == AssemblerError::None);
  const RunResult r3 = runOnce(*b, searchOptions(1, nullptr, false));
  CHECK(r3.end == RunEnd::Finished);
  Fingerprints both = r2.found;
  both.insert(r3.found.begin(), r3.found.end());
  CHECK(both == all);
}

/// Every search kind run to its end saves a position that restores as
/// finished and finds nothing more, on every path.
template <class Make>
void finishedRestoresOnEveryPath(Make make, ThreadPool& pool) {
  for (const PausePath path : kAllPaths) {
    INFO("searched with " << path.threads << " threads" << (path.vector ? "" : ", plain"));
    std::unique_ptr<Assembler> a = make();
    REQUIRE(runOnce(*a, searchOptions(path.threads, &pool, path.vector)).end ==
            RunEnd::Finished);
    const SavedPosition pos = a->savePosition();
    INFO(pos.text.substr(0, 60));
    for (const PausePath next : kAllPaths) {
      INFO("continued with " << next.threads << " threads" << (next.vector ? "" : ", plain"));
      std::unique_ptr<Assembler> b = make();
      REQUIRE(b->restorePosition(pos.text, pos.version) == AssemblerError::None);
      CHECK(b->finished() >= 1.0F);
      const RunResult r = runOnce(*b, searchOptions(next.threads, &pool, next.vector));
      CHECK(r.end == RunEnd::Finished);
      CHECK(r.found.empty());
    }
  }
}

/// The twelve pentominoes in a w x h x 1 tray, built in memory (from the
/// original's test_solver.cpp). They fill 3 x 20 in 2 ways and 6 x 10 in
/// 2339, turned-over and mirrored copies not counted.
inline Puzzle pentominoPuzzle(unsigned int w, unsigned int h) {
  static const std::vector<std::vector<std::string>> kShapes = {
      {".##", "##.", ".#."},       // F
      {"#####"},                   // I
      {"#...", "####"},            // L
      {"##..", ".###"},            // N
      {"##", "##", "#."},          // P
      {"###", ".#.", ".#."},       // T
      {"#.#", "###"},              // U
      {"#..", "#..", "###"},       // V
      {"#..", "##.", ".##"},       // W
      {".#.", "###", ".#."},       // X
      {"..#.", "####"},            // Y
      {"##.", ".#.", ".##"},       // Z
  };
  Puzzle puz{GridType{GridKind::Bricks}};
  const unsigned int tray = puz.addShape(w, h, 1);
  for (unsigned int y = 0; y < h; y++) {
    for (unsigned int x = 0; x < w; x++) {
      puz.getShape(tray).setState(x, y, 0, Voxel::kFilled);
    }
  }
  const unsigned int prob = puz.addProblem();
  puz.getProblem(prob).setResultId(puz, tray);
  for (const auto& art : kShapes) {
    const unsigned int id = puz.addShape(static_cast<unsigned int>(art[0].size()),
                                         static_cast<unsigned int>(art.size()), 1);
    for (unsigned int y = 0; y < art.size(); y++) {
      for (unsigned int x = 0; x < art[y].size(); x++) {
        if (art[y][x] == '#') {
          puz.getShape(id).setState(x, y, 0, Voxel::kFilled);
        }
      }
    }
    puz.getProblem(prob).setShapeMaximum(puz, id, 1);
    puz.getProblem(prob).setShapeMinimum(puz, id, 1);
  }
  return puz;
}

}  // namespace bt::test

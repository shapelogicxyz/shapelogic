// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Helpers for the assembler tests: fingerprints of assemblies (to compare
// runs as multisets, AGENTS.md), runs with a pool, and the pentomino puzzle
// of the original's test_solver.cpp.

#include "bt/base/stop.h"
#include "bt/base/thread_pool.h"
#include "bt/grid/voxel.h"
#include "bt/model/assembly.h"
#include "bt/model/problem.h"
#include "bt/model/puzzle.h"
#include "bt/solve/assembler.h"

#include <set>
#include <string>
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

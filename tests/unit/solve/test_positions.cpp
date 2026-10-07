// SPDX-License-Identifier: GPL-3.0-or-later
// Saved search positions against the original (decision 0006): searches the
// original paused (tests/data/positions_by_original.txt, recorded by
// `oracle.py record-positions`) continue here to the same assemblies, and a
// search paused here on one thread saves the very same text. The harness
// (`just oracle-resume`) checks both directions against the running
// original on more puzzles.
#include "../model/model_test_helpers.h"
#include "solve_test_helpers.h"

#include "bt/solve/assembler_0.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace bt;
using test::Fingerprints;

namespace {

struct RecordedPause {
  std::string fixture;
  unsigned int threads = 1;
  bool plain = false;
  bool reduce = true;
  unsigned int stopAfter = 0;
  SavedPosition position;
  Fingerprints before;  // reported before the pause
};

std::vector<RecordedPause> readRecording() {
  std::ifstream in("tests/data/positions_by_original.txt");
  REQUIRE(in);
  std::vector<RecordedPause> out;
  std::string line;
  while (std::getline(in, line)) {
    if (line.rfind("POS ", 0) == 0) {
      RecordedPause r;
      std::istringstream s(line.substr(4));
      int plain = 0;
      int reduce = 0;
      s >> r.fixture >> r.threads >> plain >> reduce >> r.stopAfter;
      r.plain = plain != 0;
      r.reduce = reduce != 0;
      out.push_back(r);
    } else if (line.rfind("VERSION ", 0) == 0) {
      out.back().position.version = line.substr(8);
    } else if (line.rfind("TEXT ", 0) == 0) {
      out.back().position.text = line.substr(5);
    } else if (line.rfind("A ", 0) == 0) {
      out.back().before.insert(line.substr(2));
    }
  }
  return out;
}

std::unique_ptr<Assembler0> prepared(const Puzzle& p, bool reduce) {
  auto a = std::make_unique<Assembler0>(p, p.getProblem(0));
  REQUIRE(a->createMatrix({}) == AssemblerError::None);
  if (reduce) {
    (void)a->reduce(StopToken{});
  }
  return a;
}

}  // namespace

TEST_CASE("positions: searches the original paused continue here",
          "[solve][assembler][resume][parallel]") {
  const std::vector<RecordedPause> recording = readRecording();
  REQUIRE(recording.size() >= 10);

  for (const RecordedPause& r : recording) {
    INFO(r.fixture << ", " << r.threads << " threads" << (r.plain ? ", plain" : "") << ", stop at "
                   << r.stopAfter);
    ThreadPool pool(r.threads);
    const SearchOptions options = test::searchOptions(r.threads, &pool, !r.plain);
    const Puzzle p = test::loadFixture(r.fixture);

    Fingerprints all;
    {
      auto a = prepared(p, r.reduce);
      all = test::runOnce(*a, test::searchOptions(1, nullptr, false)).found;
    }
    REQUIRE(r.before.size() == r.stopAfter);

    // the original's position continues here to the same assemblies
    auto a = prepared(p, r.reduce);
    REQUIRE(a->restorePosition(r.position.text, r.position.version) == AssemblerError::None);
    const test::RunResult rest = test::runOnce(*a, options);
    CHECK(rest.end == RunEnd::Finished);
    Fingerprints both = r.before;
    both.insert(rest.found.begin(), rest.found.end());
    CHECK(both == all);

    // paused here on one thread, the same text and the same assemblies
    if (r.threads == 1) {
      auto b = prepared(p, r.reduce);
      const test::RunResult first = test::runOnce(*b, options, {}, r.stopAfter);
      CHECK(first.found == r.before);
      const SavedPosition mine = b->savePosition();
      CHECK(mine.version == r.position.version);
      CHECK(mine.text == r.position.text);
    }
  }
}

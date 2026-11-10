// SPDX-License-Identifier: GPL-3.0-or-later
// A search split into tasks whose tasks split further while it runs
// (SplitTasks, decision 0024): the task list itself, on a search made up
// for the purpose; then both assemblers: one task for four threads, which
// only hand-offs can keep busy, and a search paused again and again, which
// has to carry on where it stopped instead of beginning its tasks anew.
#include "../model/model_test_helpers.h"
#include "solve_test_helpers.h"

#include "shapelogic/base/thread_pool.h"
#include "shapelogic/solve/assembler.h"
#include "shapelogic/solve/assembler_0.h"
#include "shapelogic/solve/assembler_1.h"
#include "shapelogic/solve/search_run.h"
#include "shapelogic/solve/solver_type.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

using namespace bt;
using test::Fingerprints;
using test::runOnce;
using test::searchOptions;

namespace {

// The made-up search: a task is a range of numbers to add up.
struct Range {
  std::uint64_t lo = 0;
  std::uint64_t hi = 0;
};

struct Sums {
  std::atomic<std::uint64_t> sum{0};
  std::atomic<unsigned int> handOffs{0};
  std::atomic<unsigned int> leaves{0};
};

// One run over the ranges of `state` on four threads; `stopAt` (not 0)
// stops the run once the sum has reached it.
void addUp(SplitSearchState<Range>& state, ThreadPool& pool, Sums& sums, std::uint64_t stopAt) {
  const AssemblyCallback none;
  const StopSource stop;
  SearchRun run(stop.token(), none);
  TaskProgress progress;
  progress.begin(state.weights, state.completed, state.doneWeight);
  SplitTasks<Range> tasks(state, run, progress);

  runSplitTasks(tasks, 4, &pool, run, [&](unsigned int slot) {
    return [&, slot](std::size_t index, const Range& task, std::uint64_t) {
      Range r = task;
      while (r.lo < r.hi) {
        if (run.stopRequested()) {
          TaskSplit<Range> left;
          left.branches.push_back(r);
          left.shares.push_back(1.0);
          tasks.leave(slot, index, std::move(left));
          sums.leaves++;
          return false;
        }
        if (r.hi - r.lo > 64 && tasks.wanted()) {
          const std::uint64_t mid = r.lo + (r.hi - r.lo) / 2;
          TaskSplit<Range> split;
          split.branches.push_back(Range{mid, r.hi});
          split.shares.push_back(0.5);
          split.rest = Range{r.lo, mid};
          split.restShare = 0.5;
          (void)tasks.handOff(slot, index, std::move(split));
          sums.handOffs++;
          r.hi = mid;
        }
        const std::uint64_t now = sums.sum.fetch_add(r.lo) + r.lo;
        r.lo++;
        if (stopAt != 0 && now >= stopAt) {
          run.stop(StopKind::Pause);
        }
      }
      return true;
    };
  });
}

bool allDone(const SplitSearchState<Range>& state) {
  for (const std::uint8_t c : state.completed) {
    if (!c) {
      return false;
    }
  }
  return true;
}

// The number after " T " in a saved split search: its tasks.
std::size_t savedTasks(const std::string& text) {
  const std::size_t t = text.find(" T ");
  REQUIRE(t != std::string::npos);
  return static_cast<std::size_t>(std::stoull(text.substr(t + 3)));
}

// One of the two assemblers on a puzzle of its own.
struct Subject {
  const char* name;
  Puzzle puzzle;
};

std::vector<Subject> subjects() {
  std::vector<Subject> s;
  // assembler 0, and assembler 1 (pieces in several copies)
  s.push_back({"assembler 0: the pentominoes in 3 x 20", test::pentominoPuzzle(20, 3)});
  s.push_back(
      {"assembler 1: Demo Piece Generator", test::loadFixture("DemoPieceGenerator.xmpuzzle")});
  return s;
}

std::unique_ptr<Assembler> prepare(const Puzzle& p) {
  std::unique_ptr<Assembler> a = makeAssembler(p, p.getProblem(0), SolverType::Classic);
  REQUIRE(a->createMatrix({}) == AssemblerError::None);
  return a;
}

}  // namespace

TEST_CASE("SplitTasks: one task keeps four threads busy by handing its branches on",
          "[solve][parallel]") {
  ThreadPool pool(4);
  constexpr std::uint64_t kN = 400000;

  SplitSearchState<Range> state;
  state.tasks.push_back(Range{0, kN});
  state.completed.push_back(0);
  Sums sums;
  addUp(state, pool, sums, 0);

  CHECK(sums.sum.load() == kN * (kN - 1) / 2);
  CHECK(allDone(state));
  CHECK(state.tasks.size() == state.completed.size());
  CHECK(state.weights.size() == state.tasks.size());
  // three threads had nothing until the first hand-offs
  CHECK(sums.handOffs.load() >= 1);
  CHECK(state.tasks.size() == 1 + sums.handOffs.load());
}

TEST_CASE("SplitTasks: a stopped run leaves what is left, and the next runs finish it",
          "[solve][parallel]") {
  ThreadPool pool(4);
  constexpr std::uint64_t kN = 300000;
  const std::uint64_t whole = kN * (kN - 1) / 2;

  SplitSearchState<Range> state;
  for (std::uint64_t lo = 0; lo < kN; lo += kN / 6) {
    state.tasks.push_back(Range{lo, lo + kN / 6});
    state.completed.push_back(0);
  }
  Sums sums;
  unsigned int runs = 0;
  while (!allDone(state)) {
    REQUIRE(++runs < 200);
    // a stop every twentieth of the whole or so
    addUp(state, pool, sums, sums.sum.load() + whole / 20);
    if (!allDone(state)) {
      // the finished tasks go; their weight stays
      const double before = state.doneWeight;
      state.prune();
      CHECK(state.doneWeight >= before);
      for (const std::uint8_t c : state.completed) {
        CHECK((c == 0 || state.tasks.size() == 1));
      }
    }
  }
  CHECK(runs > 3);
  CHECK(sums.leaves.load() >= 1);
  // every number once: nothing lost at a stop, nothing added twice
  CHECK(sums.sum.load() == whole);
}

TEST_CASE("Split search: one task for four threads finds every assembly once",
          "[solve][assembler][parallel]") {
  ThreadPool pool(4);
  for (const Subject& subject : subjects()) {
    for (const bool vector : {true, false}) {
      INFO(subject.name << (vector ? ", vector search" : ", plain search"));
      Fingerprints all;
      {
        const std::unique_ptr<Assembler> a = prepare(subject.puzzle);
        all = runOnce(*a, searchOptions(1, nullptr, vector)).found;
        REQUIRE(!all.empty());
      }

      // the whole search as one task (assembler 1: the fewest it makes)
      SearchOptions o = searchOptions(4, &pool, vector);
      o.splitTasks = 1;
      o.splitDepth = 1;
      {
        const std::unique_ptr<Assembler> a = prepare(subject.puzzle);
        const test::RunResult r = runOnce(*a, o);
        CHECK(r.end == RunEnd::Finished);
        CHECK(r.found == all);
      }

      // stopped part way, the search is saved as the tasks left of it, and
      // carries on from them
      {
        std::unique_ptr<Assembler> a = prepare(subject.puzzle);
        test::RunResult first = runOnce(*a, o, {}, 1);
        if (first.end == RunEnd::Stopped) {
          const SavedPosition pos = a->savePosition();
          INFO(pos.text.substr(0, 80));
          CHECK(savedTasks(pos.text) >= 1);
          a = prepare(subject.puzzle);
          REQUIRE(a->restorePosition(pos.text, pos.version) == AssemblerError::None);
          const test::RunResult rest = runOnce(*a, o);
          CHECK(rest.end == RunEnd::Finished);
          first.found.insert(rest.found.begin(), rest.found.end());
        }
        CHECK(first.found == all);
      }
    }
  }
}

// Before tasks could be left part way, a stop threw away what each thread
// had done in its task; a search stopped more often than its tasks took
// (the autosave of a long solve) never finished. Here the search is split
// as little as can be (one task, or assembler 1's fewest) and stopped every
// twentieth of its work: it must be through in a number of runs to match,
// and without doing the work twice. (The stops are not closer than that:
// one that comes while assembler 1 still makes its tasks has them made
// again by the next run.)
TEST_CASE("Split search: paused again and again it carries on where it stopped",
          "[solve][assembler][parallel][resume]") {
  ThreadPool pool(4);
  for (const Subject& subject : subjects()) {
    for (const bool vector : {true, false}) {
      INFO(subject.name << (vector ? ", vector search" : ", plain search"));
      Fingerprints all;
      std::uint64_t work = 0;
      {
        const std::unique_ptr<Assembler> a = prepare(subject.puzzle);
        all = runOnce(*a, searchOptions(4, &pool, vector)).found;
        work = a->iterations();
      }
      REQUIRE(work > 400);
      const std::uint64_t budget = work / 20;
      SearchOptions o = searchOptions(4, &pool, vector);
      o.splitTasks = 1;
      o.splitDepth = 1;

      const std::unique_ptr<Assembler> a = prepare(subject.puzzle);
      Fingerprints found;
      unsigned int runs = 0;
      for (;;) {
        REQUIRE(++runs < 200);
        const StopSource stop;
        std::atomic<bool> over{false};
        const std::uint64_t until = a->iterations() + budget;
        std::thread stopper([&] {
          while (!over.load() && a->iterations() < until) {
            std::this_thread::sleep_for(std::chrono::microseconds(20));
          }
          stop.request(StopKind::Pause);
        });
        const RunEnd end = a->assemble(
            [&found](Assembly asm_) {
              found.insert(test::fingerprint(asm_));
              return true;
            },
            o, stop.token());
        over.store(true);
        stopper.join();
        if (end == RunEnd::Finished) {
          break;
        }
      }
      CHECK(found == all);
      CHECK(runs > 3);
      // the stops cost a little (a node here and there is begun again),
      // not the work over
      CHECK(a->iterations() < 2 * work + 1000);
    }
  }
}

// The vector search on one thread was the one search that began again after
// a stop (it kept only how many assemblies it had reported): stopped more
// often than it took, it never finished. It is a split search of one task
// now, and carries on from the branches it had not tried; with nothing
// found twice, it keeps no signatures.
TEST_CASE("Split search: on one thread the vector search keeps its place too",
          "[solve][assembler][resume]") {
  // the puzzle, and the flags its stopped search may save: assembler 0's
  // split search (2), assembler 1's (4, or 5 with the branches left), and
  // the plain search (0) where the matrix is not one for the vector search
  struct One {
    const char* name;
    Puzzle puzzle;
    std::string flags;
  };
  std::vector<One> ones;
  ones.push_back({"assembler 0: the pentominoes in 3 x 20", test::pentominoPuzzle(20, 3), "2"});
  ones.push_back({"assembler 1: Cube in Cage", test::loadFixture("CubeInCage.xmpuzzle"), "45"});
  ones.push_back({"assembler 1: Demo Piece Generator",
                  test::loadFixture("DemoPieceGenerator.xmpuzzle"), "0"});

  for (const One& one : ones) {
    INFO(one.name);
    Fingerprints all;
    std::uint64_t work = 0;
    {
      const std::unique_ptr<Assembler> a = prepare(one.puzzle);
      const test::RunResult r = runOnce(*a, searchOptions(1, nullptr));
      REQUIRE(r.end == RunEnd::Finished);
      all = r.found;
      work = a->iterations();
    }
    REQUIRE(all.size() >= 2);
    // a stop every eighth of the assemblies or so
    const std::size_t each = std::max<std::size_t>(1, all.size() / 8);

    // stopped by the callback, carried on in memory and through a saved
    // position by turns
    std::unique_ptr<Assembler> a = prepare(one.puzzle);
    Fingerprints found;
    std::uint64_t done = 0;
    unsigned int runs = 0;
    bool viaSave = false;
    for (;;) {
      REQUIRE(++runs < 2000);
      std::size_t thisRun = 0;
      const std::uint64_t before = a->iterations();
      const RunEnd end = a->assemble(
          [&found, &thisRun, each](Assembly asm_) {
            found.insert(test::fingerprint(asm_));
            return ++thisRun < each;
          },
          searchOptions(1, nullptr), StopToken{});
      // (a restored assembler 1 counts from nothing, assembler 0 from what
      // its position says)
      done += a->iterations() - before;
      if (end == RunEnd::Finished) {
        break;
      }
      const SavedPosition pos = a->savePosition();
      INFO(pos.text.substr(0, 100));
      // not the vector search's old position (flag 3, a count to pass over)
      CHECK(one.flags.find(pos.text[0]) != std::string::npos);
      // a split search with nothing found twice keeps no signatures (looked
      // at before the first reload: assembler 1 keeps them for a search it
      // loaded)
      if (runs == 1 && pos.text[0] != '0') {
        CHECK(pos.text.find(" S 0") != std::string::npos);
      }
      viaSave = !viaSave;
      if (viaSave) {
        a = prepare(one.puzzle);
        REQUIRE(a->restorePosition(pos.text, pos.version) == AssemblerError::None);
      }
    }
    CHECK(runs > 1);
    CHECK(found == all);
    // carried on, not begun again: about the work of one search
    CHECK(done < 2 * work + 1000);
  }
}

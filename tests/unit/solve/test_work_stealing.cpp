// SPDX-License-Identifier: GPL-3.0-or-later
// The run layer's work-stealing driver (work_stealing.h, from the
// original's bt2Assemble), with a search made up for the test so that the
// moments that matter can be set up exactly.
#include "bt/base/stop.h"
#include "bt/base/thread_pool.h"
#include "bt/solve/search_run.h"
#include "bt/solve/work_stealing.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

using namespace bt;

namespace {

// A search of `work` steps, each step one "assembly" (its number); a branch
// splits off the second half of what it has left.
struct Branch {
  unsigned int next = 0;
  unsigned int end = 0;
};

struct CountingSearch {
  std::mutex m;
  std::multiset<unsigned int> found;
  std::vector<std::unique_ptr<Branch>> parked;
  std::atomic<unsigned int> retired{0};
  /// Called at the end of every slice of the root.
  std::function<void(SearchRun&)> onRootSlice;
  Branch* root = nullptr;

  void searchSlice(Branch& b, SearchRun& run, unsigned int budget) {
    for (unsigned int i = 0; i < budget && b.next < b.end && !run.stopRequested(); i++) {
      const std::lock_guard<std::mutex> lock(m);
      found.insert(b.next++);
    }
    if (&b == root && onRootSlice) {
      onRootSlice(run);
    }
  }
  [[nodiscard]] bool branchFinished(const Branch& b) const { return b.next >= b.end; }
  [[nodiscard]] std::unique_ptr<Branch> splitBranch(Branch& b) {
    if (b.end - b.next < 2) {
      return nullptr;
    }
    auto c = std::make_unique<Branch>();
    c->end = b.end;
    c->next = b.next + (b.end - b.next) / 2;
    b.end = c->next;
    return c;
  }
  void retireBranch(Branch& b) {
    (void)b;
    retired++;
  }
  [[nodiscard]] std::vector<std::unique_ptr<Branch>> takeParked() {
    const std::lock_guard<std::mutex> lock(m);
    std::vector<std::unique_ptr<Branch>> res;
    res.swap(parked);
    return res;
  }
  void park(std::unique_ptr<Branch> b) {
    const std::lock_guard<std::mutex> lock(m);
    parked.push_back(std::move(b));
  }
};

const AssemblyCallback kNone;

}  // namespace

TEST_CASE("work stealing: every step once, over threads and across stops",
          "[solve][parallel]") {
  ThreadPool pool(4);
  for (const unsigned int threads : {1U, 2U, 4U}) {
    INFO(threads << " threads");
    CountingSearch s;
    Branch root{0, 100000};
    s.root = &root;

    // stopped now and then from inside, carried on by the next run
    unsigned int runs = 0;
    for (;;) {
      runs++;
      REQUIRE(runs < 1000);
      SearchRun run(StopToken{}, kNone);
      unsigned int slices = 0;
      s.onRootSlice = [&slices](SearchRun& r) {
        if (++slices == 7) {
          r.stop(StopKind::Pause);
        }
      };
      runWorkStealing(s, root, threads, &pool, run);
      if (root.next >= root.end && s.parked.empty()) {
        break;
      }
    }
    CHECK(s.found.size() == 100000);
    CHECK(std::set<unsigned int>(s.found.begin(), s.found.end()).size() == 100000);
  }
}

// The lent threads wait for work while the caller's thread searches the
// last branch; a pause comes during its last slice. The caller's thread
// then finds the run stopped as it comes to wait, and goes: it must wake the
// others, which wait for nothing but a notification (the stop itself comes
// from another thread and notifies nobody). The original's bt2Assemble
// returned without waking them, and the run never ended.
TEST_CASE("work stealing: a pause as the last branch ends wakes the waiting threads",
          "[solve][parallel]") {
  ThreadPool pool(4);
  for (int k = 0; k < 5; k++) {
    CountingSearch s;
    // too little to split: the lent threads have nothing from the start
    Branch root{0, 1};
    s.root = &root;
    s.onRootSlice = [](SearchRun& run) {
      // the lent threads are waiting by now
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      run.stop(StopKind::Pause);
    };
    SearchRun run(StopToken{}, kNone);
    const auto start = std::chrono::steady_clock::now();
    runWorkStealing(s, root, 4, &pool, run);
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(10));
    CHECK(s.parked.empty());
  }
}

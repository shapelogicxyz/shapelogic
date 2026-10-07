// SPDX-License-Identifier: GPL-3.0-or-later
// The thread pool that replaces the original's helperPool_c and its global
// thread limit (helperpool.cpp; the thread-limit case of test_solver.cpp).
#include "bt/base/thread_pool.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>

using bt::ThreadPool;

TEST_CASE("thread pool: hardware threads and the split of a budget", "[base][threads]") {
  CHECK(bt::hardwareThreads() >= 1);
  CHECK(bt::hardwareThreads() <= bt::kMaxThreads);

  // searching and taking apart at once stay within the budget together
  for (const unsigned int n : {2U, 3U, 4U, 8U, 10U, 64U}) {
    const bt::ThreadSplit split = bt::splitThreads(n);
    INFO(n << " threads");
    CHECK(split.assembly >= 1);
    CHECK(split.disassembly >= 1);
    CHECK(split.assembly + split.disassembly == n);
    CHECK(split.assembly >= split.disassembly);
  }
  CHECK(bt::splitThreads(3).disassembly == 1);
  CHECK(bt::splitThreads(1).assembly == 1);
  CHECK(bt::splitThreads(1).disassembly == 1);
  CHECK(bt::splitThreads(0).assembly == 1);
}

TEST_CASE("thread pool: a job runs on the caller and on lent threads", "[base][threads][parallel]") {
  ThreadPool pool(4);
  CHECK(pool.cores() == 4);

  std::mutex m;
  std::set<unsigned int> slots;
  std::set<std::thread::id> ids;
  const unsigned int ran = pool.run(4, [&](unsigned int slot) {
    const std::lock_guard<std::mutex> lock(m);
    slots.insert(slot);
    ids.insert(std::this_thread::get_id());
  });
  CHECK(ran == 4);
  CHECK(slots == std::set<unsigned int>{0, 1, 2, 3});
  CHECK(ids.size() == 4);
  CHECK(ids.contains(std::this_thread::get_id()));  // slot 0 is the caller

  // never more than asked for, and never more than the pool has
  CHECK(pool.run(2, [](unsigned int) {}) == 2);
  CHECK(pool.run(1, [](unsigned int) {}) == 1);
  CHECK(pool.run(100, [](unsigned int) {}) == 4);
}

TEST_CASE("thread pool: sizes are clamped", "[base][threads]") {
  const ThreadPool none(0);
  CHECK(none.cores() == 1);
  const ThreadPool huge(1000000);
  CHECK(huge.cores() == bt::kMaxThreads);
}

TEST_CASE("thread pool: load outside the pool leaves fewer threads to lend",
          "[base][threads][parallel]") {
  ThreadPool pool(4);
  pool.addLoad(3);  // three cores busy elsewhere, the caller among them
  CHECK(pool.run(4, [](unsigned int) {}) == 2);
  pool.addLoad(-3);
  CHECK(pool.run(4, [](unsigned int) {}) == 4);
}

TEST_CASE("thread pool: a job may use the pool again without deadlock",
          "[base][threads][parallel]") {
  ThreadPool pool(4);
  std::atomic<unsigned int> inner{0};
  (void)pool.run(2, [&](unsigned int) {
    (void)pool.run(4, [&](unsigned int) { inner.fetch_add(1); });
  });
  // the outer job holds two cores; each inner run gets what is left
  CHECK(inner.load() >= 2);
}

TEST_CASE("thread pool: an exception comes back to the caller once all are through",
          "[base][threads][parallel]") {
  ThreadPool pool(4);
  std::atomic<unsigned int> finished{0};
  CHECK_THROWS_AS(pool.run(4,
                           [&](unsigned int slot) {
                             if (slot == 2) {
                               throw std::runtime_error("worker failed");
                             }
                             finished.fetch_add(1);
                           }),
                  std::runtime_error);
  CHECK(finished.load() == 3);

  // and the pool still works
  CHECK(pool.run(4, [](unsigned int) {}) == 4);
}

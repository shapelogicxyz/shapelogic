// SPDX-License-Identifier: GPL-3.0-or-later
// The thread pool that replaces the original's helperPool_c and its global
// thread limit (helperpool.cpp; the thread-limit case of test_solver.cpp).
#include "shapelogic/base/thread_pool.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>

using sl::ThreadPool;

TEST_CASE("thread pool: hardware threads and the split of a budget", "[base][threads]") {
  CHECK(sl::hardwareThreads() >= 1);
  CHECK(sl::hardwareThreads() <= sl::kMaxThreads);

  // searching and taking apart at once stay within the budget together
  for (const unsigned int n : {2U, 3U, 4U, 8U, 10U, 64U}) {
    const sl::ThreadSplit split = sl::splitThreads(n);
    INFO(n << " threads");
    CHECK(split.assembly >= 1);
    CHECK(split.disassembly >= 1);
    CHECK(split.assembly + split.disassembly == n);
    CHECK(split.assembly >= split.disassembly);
  }
  CHECK(sl::splitThreads(3).disassembly == 1);
  CHECK(sl::splitThreads(1).assembly == 1);
  CHECK(sl::splitThreads(1).disassembly == 1);
  CHECK(sl::splitThreads(0).assembly == 1);
}

TEST_CASE("takeApartThreads: a limit is shared, no limit takes every core", "[base][threads]") {
  // a limit: split as splitThreads while both run, all of it for the
  // take-aparts when the search waits for them or is over
  for (unsigned int n = 1; n <= 64; n++) {
    const sl::TakeApartThreads t = sl::takeApartThreads(n, 10);
    const sl::ThreadSplit split = sl::splitThreads(n);
    CHECK(t.search == split.assembly);
    CHECK(t.beside == split.disassembly);
    CHECK(t.workers == n);
    CHECK(t.beside <= t.workers);
  }
  CHECK(sl::takeApartThreads(100000, 10).workers == sl::kMaxThreads);

  // no limit: the search on every core, the take-aparts on all but two
  // (at most 16), none held back
  const sl::TakeApartThreads ten = sl::takeApartThreads(0, 10);
  CHECK(ten.search == 10);
  CHECK(ten.workers == 8);
  CHECK(ten.beside == 8);
  CHECK(sl::takeApartThreads(0, 1).workers == 1);
  CHECK(sl::takeApartThreads(0, 2).workers == 1);
  CHECK(sl::takeApartThreads(0, 3).workers == 1);
  CHECK(sl::takeApartThreads(0, 64).workers == 16);
  CHECK(sl::takeApartThreads(0, 0).search == 1);
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
  // The lent jobs go to the pool's threads through one queue: a thread done
  // with a short job may take the next one too, so only the caller and at
  // least one pool thread are certain.
  CHECK(ids.size() >= 2);
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
  CHECK(huge.cores() == sl::kMaxThreads);
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

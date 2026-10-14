// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/helperpool.cpp.
#include "shapelogic/base/thread_pool.h"

#include <algorithm>
#include <exception>
#include <system_error>

namespace bt {

unsigned int hardwareThreads() noexcept {
  const unsigned int hw = std::thread::hardware_concurrency();
  return std::clamp(hw, 1U, kMaxThreads);
}

ThreadSplit splitThreads(unsigned int n) noexcept {
  ThreadSplit s;
  if (n < 2) {
    return s;
  }
  s.disassembly = n / 4 > 0 ? n / 4 : 1;
  s.assembly = n - s.disassembly;
  return s;
}

ThreadPool::ThreadPool(unsigned int cores) : total_(std::clamp(cores, 1U, kMaxThreads)) {
  threads_.reserve(total_ - 1);
  try {
    for (unsigned int i = 1; i < total_; i++) {
      threads_.emplace_back(&ThreadPool::threadMain, this);
    }
  } catch (const std::system_error&) {
    // fewer threads than asked for: go on with the ones there are
  }
}

ThreadPool::~ThreadPool() {
  {
    const std::lock_guard<std::mutex> lock(m_);
    quit_ = true;
  }
  cv_.notify_all();
  for (std::thread& t : threads_) {
    t.join();
  }
}

void ThreadPool::threadMain() {
  for (;;) {
    std::function<void()> task;
    {
      std::unique_lock<std::mutex> lock(m_);
      cv_.wait(lock, [this] { return quit_ || !tasks_.empty(); });
      if (tasks_.empty()) {
        return;
      }
      task = std::move(tasks_.front());
      tasks_.pop_front();
    }
    task();
  }
}

unsigned int ThreadPool::run(unsigned int maxThreads,
                             const std::function<void(unsigned int)>& job) {
  // the lent threads count down as they finish
  struct Latch {
    std::mutex m;
    std::condition_variable cv;
    unsigned int left = 0;
    std::exception_ptr error;
  } latch;

  unsigned int k = 0;

  if (maxThreads > 1 && !threads_.empty()) {
    const std::lock_guard<std::mutex> lock(m_);

    // the caller is one of the load, or uses a core all the same
    const int used = std::max(load_.load(std::memory_order_relaxed), 1);
    const int freeCores = static_cast<int>(total_) - used - static_cast<int>(lent_);
    const int freeThreads = static_cast<int>(threads_.size()) - static_cast<int>(lent_);
    const int want =
        std::min({static_cast<int>(std::min(maxThreads, kMaxThreads)) - 1, freeCores, freeThreads});

    if (want > 0) {
      k = static_cast<unsigned int>(want);
      lent_ += k;
      latch.left = k;
      for (unsigned int j = 1; j <= k; j++) {
        tasks_.emplace_back([&latch, &job, j] {
          std::exception_ptr e;
          try {
            job(j);
          } catch (...) {
            e = std::current_exception();
          }
          const std::lock_guard<std::mutex> l(latch.m);
          if (e && !latch.error) {
            latch.error = e;
          }
          latch.left--;
          latch.cv.notify_all();
        });
      }
    }
  }

  if (k > 0) {
    cv_.notify_all();
  }

  std::exception_ptr own;
  try {
    job(0);
  } catch (...) {
    own = std::current_exception();
  }

  if (k > 0) {
    {
      std::unique_lock<std::mutex> l(latch.m);
      latch.cv.wait(l, [&latch] { return latch.left == 0; });
    }
    const std::lock_guard<std::mutex> lock(m_);
    lent_ -= k;
  }

  if (own) {
    std::rethrow_exception(own);
  }
  if (latch.error) {
    std::rethrow_exception(latch.error);
  }
  return k + 1;
}

}  // namespace bt

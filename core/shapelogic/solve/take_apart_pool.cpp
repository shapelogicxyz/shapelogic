// SPDX-License-Identifier: GPL-3.0-or-later
// After src/lib/disassemblerpool.cpp.
#include "shapelogic/solve/take_apart_pool.h"

#include "shapelogic/base/thread_pool.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <utility>

namespace bt {

namespace {

/// A worker takes as many assemblies at once as take about this long, at
/// most kMaxBatch: the mutex and the wake-ups are paid once for them.
constexpr double kBatchNs = 100'000;
constexpr std::size_t kMaxBatch = 32;
/// A worker out of work looks for more this long before it sleeps: a wake-up
/// costs more than taking a small assembly apart.
constexpr std::chrono::microseconds kLookFor{50};

}  // namespace

TakeApartPool::TakeApartPool(const Puzzle& puzzle, const Problem& problem,
                             const TakeApartPoolOptions& options, OnResult onResult,
                             const StopToken& stop)
    : options_(options), onResult_(std::move(onResult)), stop_(stop) {
  const unsigned int workers = std::clamp(options_.workers, 1U, kMaxThreads);
  options_.workers = workers;
  if (options_.besideSearch == 0 || options_.besideSearch > workers) {
    options_.besideSearch = workers;
  }
  // room for every worker's batch and as much again waiting
  window_ = std::max<std::size_t>(256, std::size_t{2} * kMaxBatch * workers);
  done_.resize(window_);

  // the disassemblers warm the shapes' caches: made here, one after another
  disassemblers_.reserve(workers);
  for (unsigned int i = 0; i < workers; i++) {
    disassemblers_.push_back(std::make_unique<Disassembler>(puzzle, problem, options_.takeApart));
  }

  if (options_.searchCores > 0) {
    addLoad(static_cast<int>(options_.searchCores));
    searchLoad_ = true;
  }

  threads_.reserve(workers);
  for (unsigned int i = 0; i < workers; i++) {
    Disassembler& dis = *disassemblers_[i];
    threads_.emplace_back([this, &dis] { workerLoop(dis); });
  }
}

TakeApartPool::~TakeApartPool() {
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    quit_ = true;
    quitFlag_.store(true, std::memory_order_relaxed);
    if (searchLoad_) {
      addLoad(-static_cast<int>(options_.searchCores));
      searchLoad_ = false;
    }
  }
  workCv_.notify_all();
  doneCv_.notify_all();
  for (std::thread& t : threads_) {
    t.join();
  }
}

void TakeApartPool::addLoad(int cores) const {
  if (options_.takeApart.pool != nullptr) {
    options_.takeApart.pool->addLoad(cores);
  }
}

unsigned int TakeApartPool::limit() const {
  return behind_ || searchOver_ ? options_.workers : options_.besideSearch;
}

std::size_t TakeApartPool::batchSize() const {
  const double fit = kBatchNs / std::max(jobNs_, 1.0);
  if (fit >= static_cast<double>(kMaxBatch)) {
    return kMaxBatch;
  }
  return fit < 1.0 ? 1 : static_cast<std::size_t>(fit);
}

bool TakeApartPool::wakeForQueue() const {
  if (sleeping_ == 0 || working_ >= limit()) {
    return false;
  }
  // those looking for work take it at once; those at work come back for
  // more of small assemblies soon, and not soon for large ones (a batch of
  // one)
  return queue_.size() > looking_ + working_ * (batchSize() - 1);
}

void TakeApartPool::fail(const std::exception_ptr& error) {
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (!error_) {
      error_ = error;
    }
    quit_ = true;
    quitFlag_.store(true, std::memory_order_relaxed);
  }
  workCv_.notify_all();
  doneCv_.notify_all();
}

void TakeApartPool::submit(Assembly assembly) {
  bool wake = false;
  {
    std::unique_lock<std::mutex> lock(mutex_);
    if (nextSeq_ - delivered_ >= window_ && !quit_) {
      // The take-aparts are behind: every worker may work, and the search
      // stands still while this waits (its other threads soon wait behind
      // this one), so its cores are free for the take-aparts' levels.
      const bool open = !behind_;
      behind_ = true;
      const bool given = searchLoad_;
      if (given) {
        addLoad(-static_cast<int>(options_.searchCores));
        searchLoad_ = false;
      }
      if (open) {
        workCv_.notify_all();
      }
      // not for one place: a wait for every result handed on would cost the
      // workers more than taking a small assembly apart does
      submitWaiting_ = true;
      doneCv_.wait(lock, [this] { return nextSeq_ - delivered_ <= lowWater() || quit_; });
      submitWaiting_ = false;
      if (given && !quit_) {
        addLoad(static_cast<int>(options_.searchCores));
        searchLoad_ = true;
      }
    }
    if (quit_) {
      // failed (finish() says how): nothing more is taken apart
      return;
    }
    queue_.push_back(Task{nextSeq_++, std::move(assembly)});
    queued_.store(queue_.size(), std::memory_order_relaxed);
    wake = wakeForQueue();
  }
  if (wake) {
    workCv_.notify_one();
  }
}

void TakeApartPool::finish() {
  std::unique_lock<std::mutex> lock(mutex_);
  searchOver_ = true;
  if (searchLoad_) {
    addLoad(-static_cast<int>(options_.searchCores));
    searchLoad_ = false;
  }
  workCv_.notify_all();
  doneCv_.wait(lock, [this] { return delivered_ == nextSeq_ || quit_; });
  if (error_) {
    std::rethrow_exception(error_);
  }
}

void TakeApartPool::workerLoop(Disassembler& disassembler) {
  using Clock = std::chrono::steady_clock;
  std::vector<Task> batch;
  std::vector<Done> results;
  double batchNs = 0;

  std::unique_lock<std::mutex> lock(mutex_);
  for (;;) {
    if (!results.empty()) {
      // the last batch: its results wait for the ones before them
      working_--;
      const double each = batchNs / static_cast<double>(results.size());
      jobNs_ = jobNs_ >= 1e9 ? each : 0.75 * jobNs_ + 0.25 * each;
      for (Done& d : results) {
        const std::size_t at = static_cast<std::size_t>(d.seq % window_);
        done_[at].emplace(std::move(d));
      }
      results.clear();
      // this worker may go on to hand results on: its place is another's
      if (wakeForQueue()) {
        workCv_.notify_one();
      }
      deliver(lock);
    }
    if (quit_) {
      return;
    }

    if (queue_.empty()) {
      // look for more for a moment, without the mutex
      looking_++;
      lock.unlock();
      const Clock::time_point until = Clock::now() + kLookFor;
      for (unsigned int n = 1; queued_.load(std::memory_order_relaxed) == 0 &&
                               !quitFlag_.load(std::memory_order_relaxed);
           n++) {
        if (n % 16 == 0 && Clock::now() >= until) {
          break;
        }
        std::this_thread::yield();
      }
      lock.lock();
      looking_--;
    }
    if (!quit_ && (queue_.empty() || working_ >= limit())) {
      sleeping_++;
      workCv_.wait(lock, [this] { return quit_ || (!queue_.empty() && working_ < limit()); });
      sleeping_--;
    }
    if (quit_) {
      return;
    }

    // a batch; half of what is there at most, so that a worker coming at
    // the same time gets some
    const std::size_t take = std::min(batchSize(), (queue_.size() + 1) / 2);
    batch.clear();
    for (std::size_t i = 0; i < take; i++) {
      batch.push_back(std::move(queue_.front()));
      queue_.pop_front();
    }
    queued_.store(queue_.size(), std::memory_order_relaxed);
    working_++;
    // caught up with the search: back to the workers that run beside it
    if (behind_ && queue_.size() <= window_ / 8) {
      behind_ = false;
    }
    const bool wake = wakeForQueue();
    lock.unlock();
    if (wake) {
      workCv_.notify_one();
    }

    // this thread uses a core now: fewer to lend to the others' levels
    addLoad(1);
    const Clock::time_point began = Clock::now();
    try {
      for (Task& task : batch) {
        TakeApartResult r = disassembler.disassemble(task.assembly, stop_);
        results.push_back(Done{task.seq, std::move(task.assembly), std::move(r)});
      }
    } catch (...) {
      addLoad(-1);
      fail(std::current_exception());
      return;
    }
    batchNs = std::chrono::duration<double, std::nano>(Clock::now() - began).count();
    addLoad(-1);
    lock.lock();
  }
}

void TakeApartPool::deliver(std::unique_lock<std::mutex>& lock) {
  // one thread hands on at a time, so the results keep their order; what
  // the others finish meanwhile is picked up by the loop
  if (delivering_) {
    return;
  }
  delivering_ = true;
  std::vector<Done> ready;
  while (!quit_) {
    ready.clear();
    std::uint64_t next = delivered_;
    for (;; next++) {
      std::optional<Done>& slot = done_[static_cast<std::size_t>(next % window_)];
      if (!slot || slot->seq != next) {
        break;
      }
      ready.push_back(std::move(*slot));
      slot.reset();
    }
    if (ready.empty()) {
      break;
    }
    lock.unlock();
    try {
      for (Done& d : ready) {
        onResult_(d.assembly, d.result);
      }
    } catch (...) {
      fail(std::current_exception());
      lock.lock();
      break;
    }
    lock.lock();
    delivered_ = next;
    if ((submitWaiting_ && nextSeq_ - delivered_ <= lowWater()) ||
        (searchOver_ && delivered_ == nextSeq_)) {
      doneCv_.notify_all();
    }
  }
  delivering_ = false;
}

std::vector<DisassemblyProgress> TakeApartPool::progress() const {
  std::vector<DisassemblyProgress> running;
  for (const std::unique_ptr<Disassembler>& d : disassemblers_) {
    const DisassemblyProgress p = d->progress();
    if (p.active) {
      running.push_back(p);
    }
  }
  return running;
}

std::uint64_t TakeApartPool::submitted() const {
  const std::lock_guard<std::mutex> lock(mutex_);
  return nextSeq_;
}

std::uint64_t TakeApartPool::delivered() const {
  const std::lock_guard<std::mutex> lock(mutex_);
  return delivered_;
}

}  // namespace bt

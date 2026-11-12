// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The memory the sliding and stacking searches running at once share
/// (decision 0028): a program that runs several solves side by side (the
/// engine) makes one and hands it to every solve; each search running
/// holds a lease on it and reads its share as it goes.

#include <atomic>
#include <cstdint>
#include <mutex>

namespace bt {

/// A memory budget shared out equally among the searches holding a lease.
///
/// A lease taken or given back changes every holder's share at once; each
/// search keeps to the share it reads when it next decides where its data
/// goes (a level search moves older levels to its files, a search held in
/// memory stops at its limit). Thread-safe: leases are taken and given back
/// from the solves' threads, and shares are read from the searches' threads.
class MemoryShare {
 public:
  /// A search's hold on the budget while it runs; empty when there is no
  /// budget to share. Given back when destroyed.
  class Lease {
   public:
    Lease() = default;
    ~Lease() { release(); }
    Lease(const Lease&) = delete;
    Lease& operator=(const Lease&) = delete;
    Lease(Lease&& o) noexcept : share_(o.share_) { o.share_ = nullptr; }
    Lease& operator=(Lease&& o) noexcept {
      if (this != &o) {
        release();
        share_ = o.share_;
        o.share_ = nullptr;
      }
      return *this;
    }

    /// Gives the lease back before the destructor would.
    void release() noexcept {
      if (share_ != nullptr) {
        share_->leave();
        share_ = nullptr;
      }
    }

   private:
    friend class MemoryShare;
    explicit Lease(MemoryShare* share) noexcept : share_(share) {}
    MemoryShare* share_ = nullptr;
  };

  /// A budget of `total` bytes (at least 1).
  explicit MemoryShare(std::uint64_t total) noexcept : total_(total == 0 ? 1 : total) {
    share_.store(total_, std::memory_order_relaxed);
  }
  MemoryShare(const MemoryShare&) = delete;
  MemoryShare& operator=(const MemoryShare&) = delete;

  /// Changes the budget (a setting changed); the shares follow at once.
  void setTotal(std::uint64_t total) noexcept {
    const std::lock_guard<std::mutex> lock(m_);
    total_ = total == 0 ? 1 : total;
    updateLocked();
  }

  /// The budget, in bytes.
  [[nodiscard]] std::uint64_t total() const noexcept {
    const std::lock_guard<std::mutex> lock(m_);
    return total_;
  }

  /// Each holder's share, in bytes: the budget over the leases held (all
  /// of it with none).
  [[nodiscard]] std::uint64_t share() const noexcept {
    return share_.load(std::memory_order_relaxed);
  }

  /// The leases held at the moment.
  [[nodiscard]] unsigned int holders() const noexcept {
    return holders_.load(std::memory_order_relaxed);
  }

  /// A lease for a search about to run; every share shrinks to make room.
  [[nodiscard]] Lease join() noexcept {
    const std::lock_guard<std::mutex> lock(m_);
    holders_.fetch_add(1, std::memory_order_relaxed);
    updateLocked();
    return Lease(this);
  }

 private:
  void leave() noexcept {
    const std::lock_guard<std::mutex> lock(m_);
    holders_.fetch_sub(1, std::memory_order_relaxed);
    updateLocked();
  }

  void updateLocked() noexcept {
    const unsigned int n = holders_.load(std::memory_order_relaxed);
    share_.store(n == 0 ? total_ : total_ / n, std::memory_order_relaxed);
  }

  mutable std::mutex m_;
  std::uint64_t total_;
  std::atomic<unsigned int> holders_{0};
  std::atomic<std::uint64_t> share_{0};
};

}  // namespace bt

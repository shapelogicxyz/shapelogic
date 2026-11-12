// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// A picture of where a search is, taken now and then for a front end to
/// show while the solve runs (the Solver's live preview): where each piece
/// stands in the arrangement a sliding or stacking search, or a take-apart,
/// is looking at. The watcher asks for one (request); the next search
/// thread that passes a point where it can tell claims the request and
/// puts its picture (claim, put); the watcher takes it (take). A search
/// nobody asks pays one relaxed load at each such point, so a solve that
/// is not watched (the CLI, the Python module) runs as before.

#include "shapelogic/model/assembly.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace bt {

/// What a picture shows.
enum class SampleSource : std::uint8_t {
  Search,     ///< a sliding or stacking search's arrangement
  TakeApart,  ///< a position of a take-apart
};

/// One picture: a placement per piece of the problem, in the order of its
/// assemblies (parts, then copies), where a saved solution's assembly
/// would put them. kUnplacedTrans: the piece is not shown (taken out, or
/// not in the part being taken apart).
struct SearchSnapshot {
  SampleSource source = SampleSource::Search;
  std::vector<Placement> placements;
};

/// Where a watcher's request and the picture that answers it meet. Any
/// thread.
class SearchSample {
 public:
  /// Asks for a picture: the next search thread to claim() puts one.
  void request() noexcept { wanted_.store(true, std::memory_order_relaxed); }

  /// A picture is asked for. The cheap check a search makes before claim().
  [[nodiscard]] bool wanted() const noexcept { return wanted_.load(std::memory_order_relaxed); }

  /// True for the one thread that is to answer the request, which then
  /// put()s its picture.
  [[nodiscard]] bool claim() noexcept {
    return wanted() && wanted_.exchange(false, std::memory_order_relaxed);
  }

  /// The picture that answers a claimed request; replaces one not yet
  /// taken.
  void put(SearchSnapshot snapshot) {
    const std::lock_guard<std::mutex> lock(mutex_);
    snapshot_ = std::move(snapshot);
  }

  /// The newest picture, if one has come since the last take().
  [[nodiscard]] std::optional<SearchSnapshot> take() {
    const std::lock_guard<std::mutex> lock(mutex_);
    return std::exchange(snapshot_, std::nullopt);
  }

  /// Forgets a picture not yet taken (one of an earlier run, as a run
  /// starts); a request stands.
  void discard() {
    const std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.reset();
  }

 private:
  std::atomic<bool> wanted_{false};
  std::mutex mutex_;
  std::optional<SearchSnapshot> snapshot_;
};

}  // namespace bt

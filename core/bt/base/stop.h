// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Cooperative cancellation for every long-running operation in the core.
///
/// The rules (docs/decisions/0004-cancellation-and-solve-state.md):
/// - One StopSource per run. Starting a new run means making a new source;
///   nothing ever resets a source, so a stop requested before a search loop
///   is entered cannot be lost.
/// - Search code only reads a StopToken. It has no stop flag of its own.
/// - A child source is stopped whenever its parent is, and can also be
///   stopped on its own (e.g. only the assembly stage of a run).

#include <atomic>
#include <cstdint>
#include <memory>

namespace bt {

/// Why a run is asked to stop. A stronger request replaces a weaker one,
/// never the other way round: None < Pause < Abort.
enum class StopKind : std::uint8_t {
  None = 0,
  Pause = 1,  ///< Stop where the run can be continued later.
  Abort = 2,  ///< Stop and throw the run's work away.
};

namespace detail {
struct StopState {
  std::atomic<std::uint8_t> kind{0};
  std::shared_ptr<const StopState> parent;

  [[nodiscard]] StopKind effective() const noexcept {
    std::uint8_t k = 0;
    for (const StopState* s = this; s != nullptr; s = s->parent.get()) {
      const std::uint8_t mine = s->kind.load(std::memory_order_acquire);
      if (mine > k) {
        k = mine;
      }
    }
    return static_cast<StopKind>(k);
  }
};
}  // namespace detail

/// Read side, handed to the code that does the work. Cheap to copy and to poll.
class StopToken {
 public:
  /// A token that is never stopped.
  StopToken() = default;

  [[nodiscard]] bool stopRequested() const noexcept {
    return state_ != nullptr && state_->effective() != StopKind::None;
  }
  [[nodiscard]] StopKind kind() const noexcept {
    return state_ != nullptr ? state_->effective() : StopKind::None;
  }

 private:
  friend class StopSource;
  explicit StopToken(std::shared_ptr<const detail::StopState> state) : state_(std::move(state)) {}
  std::shared_ptr<const detail::StopState> state_;
};

/// Owner side: whoever may stop the run. Copies share the same state.
class StopSource {
 public:
  StopSource() : state_(std::make_shared<detail::StopState>()) {}

  [[nodiscard]] StopToken token() const { return StopToken(state_); }

  /// A source that is stopped whenever this one is, and can be stopped alone.
  [[nodiscard]] StopSource child() const {
    StopSource c;
    c.state_->parent = state_;
    return c;
  }

  /// Ask the run to stop. Thread safe. Abort overrides Pause; a request is
  /// never withdrawn. Writes made before this call are visible to code that
  /// then sees stopRequested().
  void request(StopKind kind) const noexcept {
    const auto want = static_cast<std::uint8_t>(kind);
    std::uint8_t cur = state_->kind.load(std::memory_order_relaxed);
    while (cur < want &&
           !state_->kind.compare_exchange_weak(cur, want, std::memory_order_release,
                                               std::memory_order_relaxed)) {
    }
  }

  [[nodiscard]] bool stopRequested() const noexcept {
    return state_->effective() != StopKind::None;
  }
  [[nodiscard]] StopKind kind() const noexcept { return state_->effective(); }

 private:
  std::shared_ptr<detail::StopState> state_;
};

}  // namespace bt

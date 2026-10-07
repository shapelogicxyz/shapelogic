// SPDX-License-Identifier: GPL-3.0-or-later
#include "bt/base/solve_state.h"

#include "bt/base/assert.h"

namespace bt {

std::string_view toString(SolveState s) noexcept {
  switch (s) {
    case SolveState::Idle: return "idle";
    case SolveState::Preparing: return "preparing";
    case SolveState::Assembling: return "assembling";
    case SolveState::Draining: return "draining";
    case SolveState::Paused: return "paused";
    case SolveState::Finished: return "finished";
    case SolveState::Error: return "error";
  }
  return "unknown";
}

bool isActive(SolveState s) noexcept {
  return s == SolveState::Preparing || s == SolveState::Assembling || s == SolveState::Draining;
}

bool canTransition(SolveState from, SolveState to) noexcept {
  using S = SolveState;
  switch (from) {
    case S::Idle: return to == S::Preparing;
    case S::Preparing:
      return to == S::Assembling || to == S::Paused || to == S::Error || to == S::Idle;
    case S::Assembling:
      return to == S::Draining || to == S::Paused || to == S::Finished || to == S::Error ||
             to == S::Idle;
    case S::Draining:
      return to == S::Finished || to == S::Paused || to == S::Error || to == S::Idle;
    case S::Paused: return to == S::Preparing || to == S::Assembling || to == S::Idle;
    case S::Finished: return to == S::Idle;
    case S::Error: return to == S::Idle;
  }
  return false;
}

void SolveStateCell::advance(SolveState to) {
  const SolveState from = state_.load(std::memory_order_relaxed);
  BT_ASSERT_MSG(canTransition(from, to), "solve state move not in the transition table");
  state_.store(to, std::memory_order_release);
}

}  // namespace bt

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The tests' seam into a SolveSession (its friend): what the original's
// tests did by calling the solve thread's callback with starts of their
// choosing, and the session's tally of starts a search gave up on.

#include "shapelogic/model/assembly.h"
#include "shapelogic/solve/solve_session.h"

#include <cstdint>
#include <utility>

namespace bt {

struct SolveSessionTestAccess {
  // The session as a run with `options` would have it, without a run.
  static void prime(SolveSession& s, const SolveOptions& options) {
    s.runOptions_ = options;
    s.action_ = options.disassemble ? SolveSession::Action::SaveSolutions
                                    : SolveSession::Action::SaveAssemblies;
    s.runStop_ = StopSource();
  }
  static bool offer(SolveSession& s, Assembly a) { return s.onAssembly(std::move(a)); }

  // As if the runs so far had given up on `starts` starts at a memory
  // limit of `states` arrangements (between two runs only).
  static void gaveUpAtMemory(SolveSession& s, unsigned int starts, std::uint64_t states) {
    s.slideStartsCut_ = starts;
    s.slideMemoryCut_ = true;
    s.slideMemoryStates_ = states;
  }
};

}  // namespace bt

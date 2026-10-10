// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from solveProgress_c::activity() in src/lib/solvethread.cpp.
#include "bt/solve/solve_progress.h"

namespace bt {

std::string SolveProgress::activity() const {
  using std::to_string;

  // the longest running take-apart: the level it is on and how far that is
  std::string row;
  if (!running.empty()) {
    const DisassemblyProgress& d = running[0];
    row = ", level " + to_string(d.level) + ": " + to_string(d.levelDone) + "/" +
          to_string(d.levelSize);
    if (d.pieces > 2) {
      row += ", split " + to_string(d.separations) + "/" + to_string(d.pieces - 1);
    }
    if (d.threads > 1) {
      row += ", " + to_string(d.threads) + " threads";
    }
  }

  switch (stage) {
    case SolveStage::Prepare:
      return "prepare piece " + to_string(piece) + " of " + to_string(pieces);
    case SolveStage::Reduce:
      return "optimize piece " + to_string(piece) + " of " + to_string(pieces);
    case SolveStage::Assemble:
      if (!disassembly) {
        return "assemble";
      }
      return "assemble, disassemble " + to_string(disasmCompleted) + " done " +
             to_string(disasmPending) + " waiting" + row;
    case SolveStage::Disassemble:
      return "disassemble " + to_string(disasmCompleted + (disasmPending != 0 ? 1U : 0U)) +
             " of " + to_string(disasmCompleted + disasmPending) + row;
    case SolveStage::Stopping:
      return "please wait";
    case SolveStage::Paused:
      return "pause";
    case SolveStage::Done:
      return "finished";
    case SolveStage::Error:
      return "error";
    case SolveStage::Idle:
    case SolveStage::Other:  // the original printed nothing for its own searches
      return "";
  }
  return "";
}

}  // namespace bt

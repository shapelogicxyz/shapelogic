// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// What the files of SolveSession share (solve_session.cpp,
/// solve_session_sliding.cpp, solve_session_stacking.cpp; cleanup item
/// 21.3): the words of the notes. Internal to them.

#include "shapelogic/solve/solve_session.h"

#include <cstdint>
#include <string>

namespace bt::session_detail {

/// 357913941 -> "357,913,941", for counts shown to the user.
std::string withCommas(std::uint64_t n);

/// Whether other solves' searches share the memory with this run's at the
/// moment (decision 0028): its own lease is one of the holders.
bool sharedNow(const SolveOptions& o);

/// The note's words for a search that stopped at its share of the memory.
std::string sharedMemoryWords(const SolveOptions& o);

}  // namespace bt::session_detail

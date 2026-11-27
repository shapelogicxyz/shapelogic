// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The memory a sliding or stacking search (the slide search, the Panex
/// search, the Stacking Solver's own) may fill: one rule for all three.

#include <cstdint>

namespace bt {

class MemoryShare;

/// Memory a search may fill with arrangements, about 2 GB. A search that
/// reaches it with nowhere to write its levels stops instead of exhausting
/// the machine's memory; with a work folder its levels go to files and the
/// disk is the limit.
inline constexpr std::uint64_t kSearchMemoryBytes = 2000000000ULL;

/// The memory a search may fill with arrangements, in bytes: its share of
/// `share` when that is set; else `bytes` when it is set; otherwise
/// kSearchMemoryBytes, or with `high`, half the machine's physical memory.
/// High never allows less than normal, and is the same as normal when the
/// memory size cannot be read.
[[nodiscard]] std::uint64_t memoryBudget(std::uint64_t bytes, bool high,
                                         const MemoryShare* share = nullptr);

/// Arrangements a search may hold when each takes stateBytes: as many as
/// fit in memoryBudget(bytes, high, share), and at least one.
[[nodiscard]] std::uint64_t memoryStates(std::uint64_t stateBytes, bool high,
                                         std::uint64_t bytes = 0,
                                         const MemoryShare* share = nullptr);

}  // namespace bt

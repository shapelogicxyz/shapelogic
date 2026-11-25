// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The movement analysis' special values, named once (cleanup item 11.9):
/// how far a piece with nothing in its way moves, the step of a removal,
/// and how a take-apart's states mark a piece that has left the puzzle.
/// The values are the original's (movementcache.cpp, movementanalysator.cpp,
/// disassembler_0.cpp); the state encoding is in its files and the move
/// lists made from them, so it must not change.

namespace bt::movement {

/// A movement cache's value for two pieces with nothing of each other in
/// the way: the largest a cache gives. The closure skips rows on it, which
/// is exact only while no value is above it.
inline constexpr unsigned int kFree = 32000;

/// The step a removal asks for (checkmovement): further than any gap a
/// cache measures, so only kFree reaches it.
inline constexpr unsigned int kRemoveStep = 30000;

/// A movement this long or longer in a found move takes the piece out
/// (newNode): every removal step is, no gap between two shapes is.
inline constexpr unsigned int kRemovedMovement = 10000;

/// A take-apart's state places a removed piece this many times the
/// direction it left in from where it was (the original's states).
inline constexpr int kRemovedScale = 20000;

/// A coordinate of a state beyond this (either way) is a removed piece's.
inline constexpr int kRemovedBeyond = 10000;

static_assert(kRemoveStep < kFree, "a removal must be reachable by a free piece");
static_assert(kRemovedMovement <= kRemoveStep, "a removal must count as removed");
static_assert(kRemovedScale >= 2 * kRemovedBeyond,
              "a piece within the mark must lie beyond it once removed by one step");

}  // namespace bt::movement

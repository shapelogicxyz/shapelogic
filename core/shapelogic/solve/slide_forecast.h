// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// How long a slide search will take to go deeper: from the time each move
/// deep took so far, the rate the times grow at, and from it the time of
/// every move to come and how long until the search is that deep (the
/// Solver's Performance Analysis).

#include <cstdint>
#include <vector>

namespace bt::sliding {

/// The forecast is given at every this-many moves deep...
inline constexpr std::uint64_t kForecastEvery = 5;
/// ...up to this many.
inline constexpr std::uint64_t kForecastTo = 100;
/// The growth is measured over the last this-many moves that took
/// measurable time.
inline constexpr unsigned int kForecastWindow = 5;
/// A move that took less than this many milliseconds is too short to time:
/// it plays no part in the growth.
inline constexpr std::uint64_t kMeasurableMs = 20;
/// Times are told up to a thousand years; longer ones are that.
inline constexpr std::uint64_t kForecastMaxMs = 31557600000000ULL;

/// The forecast for one depth.
struct SlideEstimate {
  /// Moves deep.
  std::uint64_t depth = 0;
  /// How long that move will take.
  std::uint64_t levelMs = 0;
  /// How long until the search has searched that deep, from now.
  std::uint64_t fromNowMs = 0;
};

/// What a slide search's level times say of its future.
struct SlideForecast {
  /// The moves searched to (the times known).
  std::uint64_t depth = 0;
  /// How long each move took: levelMs[k] is move k + 1 (bt::kUntimedMs
  /// for one whose time is not known).
  std::vector<std::uint64_t> levelMs;
  /// How long the move under way has run so far; 0 when none is.
  std::uint64_t runningMs = 0;
  /// Each move's time over the one before (the geometric mean of the
  /// last kForecastWindow ratios of measurable moves); 0 when fewer than
  /// two measurable moves follow one another (untimed moves are none),
  /// and there are no estimates.
  double growth = 0;
  /// Every kForecastEvery moves past depth, up to kForecastTo.
  std::vector<SlideEstimate> estimates;
};

/// The forecast from the times of the moves searched (levelMs[k], move
/// k + 1; untimed ones, such as those of a search paused before the times
/// were kept, play no part) and how long the move under way has run. From
/// the newest timed move, each move takes the one before's time times the
/// growth; the next one to search, at least as long as it has run.
[[nodiscard]] SlideForecast slideForecast(const std::vector<std::uint64_t>& levelMs,
                                          std::uint64_t runningMs);

}  // namespace bt::sliding

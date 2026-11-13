// SPDX-License-Identifier: GPL-3.0-or-later
#include "shapelogic/solve/slide_forecast.h"

#include "shapelogic/model/problem.h"

#include <algorithm>
#include <cmath>

namespace bt::sliding {

namespace {

std::uint64_t clampMs(double ms) {
  if (!(ms > 0)) {
    return 0;
  }
  if (ms >= static_cast<double>(kForecastMaxMs)) {
    return kForecastMaxMs;
  }
  return static_cast<std::uint64_t>(std::llround(ms));
}

}  // namespace

SlideForecast slideForecast(const std::vector<std::uint64_t>& levelMs, std::uint64_t runningMs) {
  SlideForecast f;
  f.levelMs = levelMs;
  f.depth = levelMs.size();
  f.runningMs = runningMs;

  // the growth from the newest ratios of moves long enough to time: the
  // times rise and fall with the levels' sizes, so the newest say most
  double logSum = 0;
  unsigned int ratios = 0;
  for (std::size_t k = levelMs.size(); k >= 2 && ratios < kForecastWindow; k--) {
    const std::uint64_t now = levelMs[k - 1];
    const std::uint64_t before = levelMs[k - 2];
    if (now < kMeasurableMs || before < kMeasurableMs || now == kUntimedMs ||
        before == kUntimedMs) {
      continue;
    }
    logSum += std::log(static_cast<double>(now) / static_cast<double>(before));
    ratios++;
  }
  if (ratios == 0 || f.depth >= kForecastTo) {
    return f;
  }
  f.growth = std::exp(logSum / ratios);

  // from the newest timed move (there is one: the growth has two)
  std::size_t base = levelMs.size();
  while (levelMs[base - 1] == kUntimedMs) {
    base--;
  }
  double level = std::max<double>(static_cast<double>(levelMs[base - 1]), 1.0);
  double total = 0;
  for (std::uint64_t d = base + 1; d <= kForecastTo; d++) {
    level *= f.growth;
    if (d <= f.depth) {
      // searched already, untimed
      continue;
    }
    if (d == f.depth + 1) {
      // the move under way takes at least as long as it has run
      level = std::max(level, static_cast<double>(runningMs));
    }
    total += level;
    if (d % kForecastEvery == 0) {
      f.estimates.push_back({d, clampMs(level),
                             clampMs(total - static_cast<double>(runningMs))});
    }
  }
  return f;
}

}  // namespace bt::sliding

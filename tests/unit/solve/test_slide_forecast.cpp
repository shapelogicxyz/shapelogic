// SPDX-License-Identifier: GPL-3.0-or-later
// The slide search's forecast (the Solver's Performance Analysis): the
// growth of the moves' times and the estimates every five moves to 100.
#include "shapelogic/solve/slide_forecast.h"

#include "shapelogic/model/problem.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

using namespace bt::sliding;

TEST_CASE("slide forecast: times doubling each move are told every five moves", "[sliding]") {
  // moves 1 to 6 took 100, 200, ..., 3200 ms
  const std::vector<std::uint64_t> times{100, 200, 400, 800, 1600, 3200};
  const SlideForecast f = slideForecast(times, 0);
  CHECK(f.depth == 6);
  CHECK(f.growth == Catch::Approx(2.0));
  // from move 10 by fives to 100
  REQUIRE(f.estimates.size() == 19);
  CHECK(f.estimates.front().depth == 10);
  CHECK(f.estimates.back().depth == 100);
  // move 10 takes 3200 * 2^4; until then moves 7 to 10: 6400 + ... + 51200
  CHECK(f.estimates.front().levelMs == 51200);
  CHECK(f.estimates.front().fromNowMs == 6400 + 12800 + 25600 + 51200);
  // very far off: held to a thousand years
  CHECK(f.estimates.back().levelMs == kForecastMaxMs);
  CHECK(f.estimates.back().fromNowMs == kForecastMaxMs);
}

TEST_CASE("slide forecast: the next depth is the first multiple of five past the search's",
          "[sliding]") {
  std::vector<std::uint64_t> times(36, 1000);
  SlideForecast f = slideForecast(times, 0);
  CHECK(f.growth == Catch::Approx(1.0));
  REQUIRE(!f.estimates.empty());
  CHECK(f.estimates.front().depth == 40);
  // four moves of a second each to move 40, sixty-four to move 100
  CHECK(f.estimates.front().fromNowMs == 4000);
  CHECK(f.estimates.back().fromNowMs == 64000);
  times.resize(35, 1000);
  CHECK(slideForecast(times, 0).estimates.front().depth == 40);
  // at 100 there is nothing more to tell
  CHECK(slideForecast(std::vector<std::uint64_t>(100, 1000), 0).estimates.empty());
}

TEST_CASE("slide forecast: the newest moves set the growth, short ones play no part",
          "[sliding]") {
  // the first moves too short to time; then a growth of 3 that slows to 1.5
  const std::vector<std::uint64_t> times{0,    1,    2,    50,    150,   450,
                                         1350, 4050, 6075, 9112, 13668, 20502};
  const SlideForecast f = slideForecast(times, 0);
  // the last five ratios: 3, 1.5, 1.5, 1.5, 1.5
  CHECK(f.growth == Catch::Approx(std::pow(3.0 * 1.5 * 1.5 * 1.5 * 1.5, 0.2)).epsilon(1e-3));
  // too few measurable moves: no growth, no estimates
  const SlideForecast early = slideForecast({0, 1, 3, 30}, 0);
  CHECK(early.growth == 0);
  CHECK(early.estimates.empty());
  CHECK(slideForecast({}, 0).estimates.empty());
}

TEST_CASE("slide forecast: the move under way takes at least as long as it has run",
          "[sliding]") {
  const std::vector<std::uint64_t> times{1000, 1000, 1000, 1000};
  // move 5 has run 10 s where a second was expected: it is taken to need
  // those 10 s, so it ends now
  const SlideForecast f = slideForecast(times, 10000);
  REQUIRE(f.estimates.front().depth == 5);
  CHECK(f.estimates.front().levelMs == 10000);
  CHECK(f.estimates.front().fromNowMs == 0);
  // the moves after it go on from it
  CHECK(f.estimates[1].depth == 10);
  CHECK(f.estimates[1].levelMs == 10000);
  CHECK(f.estimates[1].fromNowMs == 50000);
  CHECK(f.runningMs == 10000);
}

TEST_CASE("slide forecast: moves searched before the times were kept play no part",
          "[sliding]") {
  // a search paused 36 moves deep by a version that kept no times
  std::vector<std::uint64_t> times(36, bt::kUntimedMs);
  SlideForecast f = slideForecast(times, 5000);
  CHECK(f.depth == 36);
  CHECK(f.growth == 0);
  CHECK(f.estimates.empty());
  // one move timed since it was carried on: still too little
  times.push_back(60000);
  CHECK(slideForecast(times, 0).estimates.empty());
  // two: the estimates go on from move 38
  times.push_back(90000);
  f = slideForecast(times, 0);
  CHECK(f.growth == Catch::Approx(1.5));
  REQUIRE(f.estimates.size() == 13);
  CHECK(f.estimates.front().depth == 40);
  CHECK(f.estimates.front().levelMs == 202500);
  CHECK(f.estimates.front().fromNowMs == 135000 + 202500);
}

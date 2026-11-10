// SPDX-License-Identifier: GPL-3.0-or-later
// When a long search saves itself as it runs (the crash recovery journal).
#include "shapelogic/base/save_cadence.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace bt;
using namespace std::chrono_literals;

TEST_CASE("save cadence: a floor, then by what the last save cost", "[base]") {
  using Clock = SaveCadence::Clock;
  const Clock::time_point start{};

  SaveCadence saves(60s, start);
  // the first save a floor into the run
  CHECK_FALSE(saves.due(start + 59s));
  CHECK(saves.due(start + 60s));

  // a cheap save: the floor again, from when it ended
  Clock::time_point began = start + 60s;
  saves.saved(began, began + 200ms);
  CHECK(saves.wait() == 60s);
  CHECK_FALSE(saves.due(began + 200ms + 59s));
  CHECK(saves.due(began + 200ms + 60s));

  // a dear one: one part in fifty, so fifty times its cost
  began = start + 200s;
  saves.saved(began, began + 4s);
  CHECK(saves.wait() == 4s * SaveCadence::kCostFactor);
  CHECK_FALSE(saves.due(began + 4s + 199s));
  CHECK(saves.due(began + 4s + 200s));

  // and back to the floor when saving is cheap again
  began = start + 1000s;
  saves.saved(began, began + 1s);
  CHECK(saves.wait() == 60s);

  // the tests' floor
  SaveCadence quick(1s, start);
  CHECK(quick.due(start + 1s));
  quick.saved(start + 1s, start + 1s + 100ms);
  CHECK(quick.wait() == 5s);
}

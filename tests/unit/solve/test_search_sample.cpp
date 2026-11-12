// SPDX-License-Identifier: GPL-3.0-or-later
// The live preview's meeting point: a picture is kept only when one was
// asked for, and one request is answered once, whichever thread claims it.
#include "shapelogic/model/assembly.h"
#include "shapelogic/solve/search_sample.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <optional>
#include <thread>
#include <vector>

using namespace bt;

TEST_CASE("search sample: a request is claimed once and its picture taken once", "[solve]") {
  SearchSample s;
  CHECK_FALSE(s.wanted());
  CHECK_FALSE(s.claim());
  CHECK_FALSE(s.take().has_value());

  s.request();
  CHECK(s.wanted());
  REQUIRE(s.claim());
  CHECK_FALSE(s.claim());
  s.put(SearchSnapshot{SampleSource::TakeApart, std::vector<Placement>(3)});
  const std::optional<SearchSnapshot> shot = s.take();
  REQUIRE(shot.has_value());
  CHECK(shot->source == SampleSource::TakeApart);
  CHECK(shot->placements.size() == 3);
  CHECK_FALSE(s.take().has_value());

  // a picture of an earlier run is dropped; a request stands
  s.request();
  REQUIRE(s.claim());
  s.put(SearchSnapshot{});
  s.request();
  s.discard();
  CHECK_FALSE(s.take().has_value());
  CHECK(s.wanted());
}

TEST_CASE("search sample: of threads racing for a request, one answers it",
          "[solve][parallel]") {
  SearchSample s;
  for (int round = 0; round < 200; round++) {
    s.request();
    std::atomic<int> claimed{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; t++) {
      threads.emplace_back([&s, &claimed] {
        if (s.claim()) {
          claimed.fetch_add(1);
          s.put(SearchSnapshot{});
        }
      });
    }
    for (std::thread& t : threads) {
      t.join();
    }
    CHECK(claimed.load() == 1);
    CHECK(s.take().has_value());
  }
}

// SPDX-License-Identifier: GPL-3.0-or-later
#include "bt/base/stop.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>
#include <vector>

using bt::StopKind;
using bt::StopSource;
using bt::StopToken;

TEST_CASE("stop: a default token never stops", "[base][stop]") {
  const StopToken t;
  CHECK_FALSE(t.stopRequested());
  CHECK(t.kind() == StopKind::None);
}

TEST_CASE("stop: a request reaches every token of the source", "[base][stop]") {
  const StopSource s;
  const StopToken a = s.token();
  const StopToken b = s.token();
  CHECK_FALSE(a.stopRequested());
  s.request(StopKind::Pause);
  CHECK(a.stopRequested());
  CHECK(b.kind() == StopKind::Pause);
}

// The race the original BurrTools had: the assembler cleared its abort flag
// when it started, so a Pause pressed just before was lost. Here nothing can
// clear a request, so a stop made before the work starts is still seen.
TEST_CASE("stop: a request made before the work starts is not lost", "[base][stop]") {
  const StopSource s;
  s.request(StopKind::Pause);
  const StopToken handedToSearch = s.token();
  CHECK(handedToSearch.stopRequested());
}

TEST_CASE("stop: abort overrides pause, never the other way round", "[base][stop]") {
  const StopSource s;
  s.request(StopKind::Pause);
  s.request(StopKind::Abort);
  CHECK(s.kind() == StopKind::Abort);
  s.request(StopKind::Pause);
  CHECK(s.kind() == StopKind::Abort);
}

TEST_CASE("stop: a child stops with its parent, and alone", "[base][stop]") {
  const StopSource run;
  const StopSource assembly = run.child();

  SECTION("stopping the child leaves the parent running") {
    assembly.request(StopKind::Pause);
    CHECK(assembly.token().stopRequested());
    CHECK_FALSE(run.token().stopRequested());
  }
  SECTION("stopping the parent stops the child") {
    run.request(StopKind::Abort);
    CHECK(assembly.token().kind() == StopKind::Abort);
  }
  SECTION("the child sees the stronger of the two") {
    assembly.request(StopKind::Pause);
    run.request(StopKind::Abort);
    CHECK(assembly.kind() == StopKind::Abort);
  }
}

TEST_CASE("stop: a fresh source is independent of an old one", "[base][stop]") {
  const StopSource first;
  first.request(StopKind::Pause);
  const StopSource second;  // a continued run gets a new source
  CHECK_FALSE(second.token().stopRequested());
}

TEST_CASE("stop: workers see a request made on another thread", "[base][stop][parallel]") {
  const StopSource s;
  const StopToken t = s.token();
  std::atomic<int> stopped{0};
  std::vector<std::thread> workers;
  workers.reserve(4);
  for (int i = 0; i < 4; ++i) {
    workers.emplace_back([&] {
      while (!t.stopRequested()) {
        std::this_thread::yield();
      }
      stopped.fetch_add(1);
    });
  }
  s.request(StopKind::Pause);
  for (auto& w : workers) {
    w.join();
  }
  CHECK(stopped.load() == 4);
}

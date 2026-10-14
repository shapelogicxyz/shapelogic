// SPDX-License-Identifier: GPL-3.0-or-later
#include "shapelogic/base/assert.h"
#include "shapelogic/base/solve_state.h"

#include <catch2/catch_test_macros.hpp>

#include <array>

using bt::SolveState;

namespace {
constexpr std::array kAll = {SolveState::Idle,   SolveState::Preparing, SolveState::Assembling,
                             SolveState::Draining, SolveState::Paused,  SolveState::Finished,
                             SolveState::Error};
}

TEST_CASE("solve state: a normal run", "[base][state]") {
  bt::SolveStateCell c;
  CHECK(c.load() == SolveState::Idle);
  c.advance(SolveState::Preparing);
  c.advance(SolveState::Assembling);
  c.advance(SolveState::Draining);
  c.advance(SolveState::Finished);
  CHECK(c.load() == SolveState::Finished);
}

TEST_CASE("solve state: pause and continue", "[base][state]") {
  bt::SolveStateCell c;
  c.advance(SolveState::Preparing);
  c.advance(SolveState::Assembling);
  c.advance(SolveState::Paused);
  c.advance(SolveState::Assembling);  // continued with the prepared search
  c.advance(SolveState::Finished);
  CHECK(c.load() == SolveState::Finished);
}

TEST_CASE("solve state: a move outside the table is an internal error", "[base][state]") {
  bt::SolveStateCell c;
  CHECK_THROWS_AS(c.advance(SolveState::Finished), bt::InternalError);
  CHECK(c.load() == SolveState::Idle);
}

TEST_CASE("solve state: the table", "[base][state]") {
  // Finished and Error only ever go back to Idle.
  for (SolveState to : kAll) {
    CHECK(bt::canTransition(SolveState::Finished, to) == (to == SolveState::Idle));
    CHECK(bt::canTransition(SolveState::Error, to) == (to == SolveState::Idle));
  }
  // A paused run cannot be declared finished without running again.
  CHECK_FALSE(bt::canTransition(SolveState::Paused, SolveState::Finished));
  // Nothing moves to the state it is in.
  for (SolveState s : kAll) {
    CHECK_FALSE(bt::canTransition(s, s));
  }
  // Every active state can be aborted back to Idle.
  for (SolveState s : kAll) {
    if (bt::isActive(s)) {
      CHECK(bt::canTransition(s, SolveState::Idle));
    }
  }
}

TEST_CASE("solve state: names", "[base][state]") {
  for (SolveState s : kAll) {
    CHECK_FALSE(bt::toString(s).empty());
  }
  CHECK(bt::toString(SolveState::Draining) == "draining");
}

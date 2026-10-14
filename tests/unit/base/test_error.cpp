// SPDX-License-Identifier: GPL-3.0-or-later
#include "shapelogic/base/assert.h"
#include "shapelogic/base/error.h"
#include "shapelogic/base/version.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace {
bt::Result<int> parsePositive(int v) {
  if (v <= 0) {
    return bt::fail(bt::Errc::InvalidArgument, "must be positive");
  }
  return v;
}
}  // namespace

TEST_CASE("error: results carry a value or an error", "[base][error]") {
  const auto ok = parsePositive(3);
  REQUIRE(ok.has_value());
  CHECK(*ok == 3);

  const auto bad = parsePositive(0);
  REQUIRE_FALSE(bad.has_value());
  CHECK(bad.error().code == bt::Errc::InvalidArgument);
  CHECK(bt::toString(bad.error().code) == "invalid_argument");
}

TEST_CASE("error: BT_ASSERT throws an InternalError naming the check", "[base][error]") {
  try {
    BT_ASSERT_MSG(1 + 1 == 3, "arithmetic");
    FAIL("BT_ASSERT did not throw");
  } catch (const bt::InternalError& e) {
    const std::string what = e.what();
    CHECK(what.find("1 + 1 == 3") != std::string::npos);
    CHECK(what.find("arithmetic") != std::string::npos);
    CHECK(e.line() > 0);
  }
  CHECK_NOTHROW(BT_ASSERT(2 > 1));
}

TEST_CASE("version: set from CMake", "[base]") {
  CHECK_FALSE(bt::kVersion.empty());
}

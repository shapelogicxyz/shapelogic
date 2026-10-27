// SPDX-License-Identifier: GPL-3.0-or-later
#include "shapelogic/base/system.h"
#include "shapelogic/solve/sliding_search.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>

using bt::DiskBudget;
using bt::diskBudgetFor;
using bt::diskBudgetReason;
using bt::kAutoDiskBudgetMax;

namespace {

constexpr std::uint64_t kGB = 1000000000ULL;

}  // namespace

TEST_CASE("system: the free disk of an existing folder can be read", "[base][system]") {
  const std::optional<std::uint64_t> free =
      bt::availableDiskBytes(std::filesystem::temp_directory_path());
  REQUIRE(free.has_value());
  CHECK(*free > 0);
}

// The report: 90 GB free on a 2 TB disk left the sliding search 1 GB,
// when the margin was a twentieth of the whole disk. The budget now goes
// by the free space alone.
TEST_CASE("system: no budget asked for is half the free space, at most 200 GB",
          "[base][system]") {
  const std::filesystem::path dir = std::filesystem::temp_directory_path();
  const DiskBudget b = diskBudgetFor(dir, 0);
  REQUIRE(b.free.has_value());
  CHECK(b.automatic);
  CHECK_FALSE(b.heldToFree);
  CHECK(b.bytes == std::max<std::uint64_t>(std::min(*b.free / 2, kAutoDiskBudgetMax), 1));
  CHECK_FALSE(diskBudgetReason(b).empty());
  // the search's own files there count as free to it
  const DiskBudget own = diskBudgetFor(dir, 0, 10 * kGB);
  REQUIRE(own.free.has_value());
  CHECK(*own.free >= *b.free + 10 * kGB - kGB);
}

TEST_CASE("system: a budget asked for is given, but held to the free space", "[base][system]") {
  const std::filesystem::path dir = std::filesystem::temp_directory_path();
  const DiskBudget small = diskBudgetFor(dir, 1000);
  CHECK_FALSE(small.automatic);
  CHECK_FALSE(small.heldToFree);
  CHECK(small.bytes == 1000);
  CHECK(diskBudgetReason(small).empty());

  // far more than any disk has: the free space but a reserve, never less
  // than half of it, so the search stops before the disk is full
  const DiskBudget huge = diskBudgetFor(dir, 1000000 * kAutoDiskBudgetMax);
  REQUIRE(huge.free.has_value());
  CHECK(huge.heldToFree);
  CHECK(huge.bytes < *huge.free);
  CHECK(huge.bytes >= *huge.free / 2);
  CHECK(diskBudgetReason(huge).find("allows") != std::string::npos);
}

TEST_CASE("system: the memory budget is the one set, else 2 GB or with high half the memory",
          "[base][system]") {
  using bt::sliding::kSearchMemoryBytes;
  using bt::sliding::memoryBudget;
  using bt::sliding::memoryStates;
  CHECK(memoryBudget(0, false) == kSearchMemoryBytes);
  CHECK(memoryBudget(0, true) >= kSearchMemoryBytes);
  CHECK(memoryBudget(0, true) >= bt::physicalMemoryBytes() / 2);
  // set, high plays no part
  CHECK(memoryBudget(5 * kGB, false) == 5 * kGB);
  CHECK(memoryBudget(kGB, true) == kGB);
  CHECK(memoryStates(100, false, kGB) == kGB / 100);
  // never none
  CHECK(memoryStates(2 * kGB, false, kGB) == 1);
}

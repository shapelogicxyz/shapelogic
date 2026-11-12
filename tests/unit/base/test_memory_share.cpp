// SPDX-License-Identifier: GPL-3.0-or-later
// The memory the searches running at once share (decision 0028), and the
// check that a search's folder can be written.
#include "shapelogic/base/memory_share.h"
#include "shapelogic/base/system.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

using bt::MemoryShare;

TEST_CASE("memory share: the budget is shared equally by the leases held", "[base][memory]") {
  MemoryShare m(1200);
  CHECK(m.share() == 1200);
  CHECK(m.holders() == 0);
  {
    const MemoryShare::Lease a = m.join();
    CHECK(m.share() == 1200);
    MemoryShare::Lease b = m.join();
    CHECK(m.share() == 600);
    std::optional<MemoryShare::Lease> c = m.join();
    CHECK(m.holders() == 3);
    CHECK(m.share() == 400);
    // one ending gives the others its part
    c.reset();
    CHECK(m.share() == 600);
    // a lease moved is still one lease, given back once
    MemoryShare::Lease moved = std::move(b);
    CHECK(m.holders() == 2);
    moved.release();
    moved.release();
    CHECK(m.holders() == 1);
    CHECK(m.share() == 1200);
    // a setting changed: the shares follow at once
    const MemoryShare::Lease d = m.join();
    m.setTotal(3000);
    CHECK(m.total() == 3000);
    CHECK(m.share() == 1500);
  }
  CHECK(m.holders() == 0);
  CHECK(m.share() == 3000);
  // an empty lease holds nothing
  { const MemoryShare::Lease none; }
  CHECK(m.holders() == 0);
  // never a budget of nothing
  m.setTotal(0);
  CHECK(m.share() == 1);
}

TEST_CASE("memory share: leases taken and given back on many threads at once",
          "[base][memory][parallel]") {
  MemoryShare m(1000000);
  // (Catch's checks are not for other threads: they count what was wrong)
  std::atomic<int> wrong{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < 8; t++) {
    threads.emplace_back([&m, &wrong] {
      for (int i = 0; i < 2000; i++) {
        const MemoryShare::Lease l = m.join();
        const std::uint64_t s = m.share();
        if (s < 1000000 / 8 || s > 1000000) {
          wrong++;
        }
      }
    });
  }
  for (std::thread& t : threads) {
    t.join();
  }
  CHECK(wrong == 0);
  CHECK(m.holders() == 0);
  CHECK(m.share() == 1000000);
}

TEST_CASE("system: a folder that cannot be written is found out", "[base][system]") {
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "bt_test_folder_write_check";
  std::filesystem::remove_all(dir);
  // made when it is not there, and left empty
  CHECK(bt::folderWriteError(dir / "made").empty());
  CHECK(std::filesystem::is_directory(dir / "made"));
  CHECK(std::filesystem::is_empty(dir / "made"));
  // a file where the folder should be
  { std::ofstream(dir / "file") << "x"; }
  CHECK_FALSE(bt::folderWriteError(dir / "file").empty());
  CHECK_FALSE(bt::folderWriteError(dir / "file" / "below").empty());
  std::filesystem::remove_all(dir);
}

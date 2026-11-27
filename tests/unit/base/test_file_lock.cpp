// SPDX-License-Identifier: GPL-3.0-or-later
#include "../scratch_dir.h"

#include "shapelogic/base/file_lock.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

using sl::FileLock;
using sl::test::ScratchDir;


TEST_CASE("file lock: one holder at a time, and the file goes with the lock", "[base][lock]") {
  const ScratchDir dir("file_lock");
  const std::filesystem::path file = dir.path() / "solve.lock";

  FileLock first;
  CHECK_FALSE(first.held());
  REQUIRE(first.tryAcquire(file) == FileLock::Outcome::Held);
  CHECK(first.held());
  CHECK(std::filesystem::exists(file));

  // a second holder in the same program is refused as another program is
  FileLock second;
  CHECK(second.tryAcquire(file) == FileLock::Outcome::Busy);
  CHECK_FALSE(second.held());
  // and the refusal leaves the holder's lock alone
  CHECK(first.held());
  CHECK(std::filesystem::exists(file));

  first.release();
  CHECK_FALSE(first.held());
  CHECK_FALSE(std::filesystem::exists(file));
  // released twice: nothing
  first.release();

  REQUIRE(second.tryAcquire(file) == FileLock::Outcome::Held);
  CHECK(std::filesystem::exists(file));
}

TEST_CASE("file lock: it goes when its holder does, and moves with it", "[base][lock]") {
  const ScratchDir dir("file_lock");
  const std::filesystem::path file = dir.path() / "solve.lock";
  FileLock outer;
  {
    FileLock inner;
    REQUIRE(inner.tryAcquire(file) == FileLock::Outcome::Held);
    FileLock moved = std::move(inner);
    CHECK(moved.held());
    CHECK(outer.tryAcquire(file) == FileLock::Outcome::Busy);
    outer = std::move(moved);
    CHECK(outer.held());
    // the lock is `outer`'s now: the ones it came through let nothing go
  }
  CHECK(outer.held());
  CHECK(std::filesystem::exists(file));
  FileLock other;
  CHECK(other.tryAcquire(file) == FileLock::Outcome::Busy);
  outer.release();
  CHECK(other.tryAcquire(file) == FileLock::Outcome::Held);
}

TEST_CASE("file lock: a lock taken again lets the first go", "[base][lock]") {
  const ScratchDir dir("file_lock");
  FileLock lock;
  REQUIRE(lock.tryAcquire(dir.path() / "a.lock") == FileLock::Outcome::Held);
  REQUIRE(lock.tryAcquire(dir.path() / "b.lock") == FileLock::Outcome::Held);
  CHECK_FALSE(std::filesystem::exists(dir.path() / "a.lock"));
  CHECK(std::filesystem::exists(dir.path() / "b.lock"));
}

TEST_CASE("file lock: a file that cannot be made is no lock", "[base][lock]") {
  const ScratchDir dir("file_lock");
  FileLock lock;
  CHECK(lock.tryAcquire(dir.path() / "no-such-folder" / "solve.lock") == FileLock::Outcome::Failed);
  CHECK_FALSE(lock.held());
}

TEST_CASE("file lock: the lock files nobody holds are removed, the held one is kept",
          "[base][lock]") {
  const ScratchDir dir("file_lock");
  // what a solve that crashed leaves on macOS and Linux: the file alone
  const std::filesystem::path stale = dir.path() / "solve-00000000000000aa.lock";
  { std::ofstream(stale).put('\n'); }
  const std::filesystem::path held = dir.path() / "00000000000000bb.lock";
  FileLock holder;
  REQUIRE(holder.tryAcquire(held) == FileLock::Outcome::Held);
  // neither a lock file nor in this folder: left alone
  const std::filesystem::path other = dir.path() / "notes.txt";
  { std::ofstream(other).put('\n'); }
  const std::filesystem::path inner = dir.path() / "slide-00000000000000cc" / "old.lock";
  std::filesystem::create_directories(inner.parent_path());
  { std::ofstream(inner).put('\n'); }

  CHECK(sl::removeStaleLocks(dir.path()) == 1);
  CHECK_FALSE(std::filesystem::exists(stale));
  CHECK(std::filesystem::exists(held));
  CHECK(holder.held());
  CHECK(std::filesystem::exists(other));
  CHECK(std::filesystem::exists(inner));

  // still the holder's: another is refused
  FileLock second;
  CHECK(second.tryAcquire(held) == FileLock::Outcome::Busy);
  holder.release();
  CHECK(sl::removeStaleLocks(dir.path()) == 0);
  // a folder that is not there: nothing
  CHECK(sl::removeStaleLocks(dir.path() / "missing") == 0);
}

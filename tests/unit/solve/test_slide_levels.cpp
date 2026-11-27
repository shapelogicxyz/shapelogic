// SPDX-License-Identifier: GPL-3.0-or-later
// The slide search's key sets (slide_levels.h): found, joined, sought and
// reopened, compressed in chunks, damaged files told, older files read
// (cleanup item 21.2 moved these cases here from test_sliding.cpp).
#include "../scratch_dir.h"
#include "slide_levels_test_helpers.h"

#include "shapelogic/solve/slide_levels.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace bt;
using namespace bt::sliding;
using namespace bt::test;

#ifdef __SIZEOF_INT128__
TEST_CASE("sliding: a key set finds what was put in it, on disk and in memory", "[sliding]") {
  const ScratchDir scratch("keyset");
  const std::filesystem::path dir = scratch.path() / "work";
  std::filesystem::create_directories(dir);
  for (int onDisk = 0; onDisk < 2; onDisk++) {
    // keys spread over all 128 bits, a few close together, sorted
    std::vector<Wide> keys;
    std::uint64_t x = 88172645463325252ULL;
    auto rnd = [&x]() {
      x ^= x << 13;
      x ^= x >> 7;
      x ^= x << 17;
      return x;
    };
    for (int i = 0; i < 5000; i++) {
      Wide k = (static_cast<Wide>(rnd()) << 64) | rnd();
      if (i % 3 == 0) {
        k = keys.empty() ? 0 : keys.back() + 1 + rnd() % 300;
      }
      keys.push_back(k);
    }
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());

    levels::KeySet<Wide> set;
    REQUIRE(set.create(onDisk != 0 ? dir / "keys" : std::filesystem::path()));
    std::vector<Wide> in;
    std::vector<Wide> out;
    for (std::size_t i = 0; i < keys.size(); i++) {
      (i % 2 != 0 ? out : in).push_back(keys[i]);
    }
    REQUIRE(set.addAll(in.begin(), in.end()));
    REQUIRE(set.finish());
    CHECK(set.size() == in.size());

    std::vector<Wide> back;
    REQUIRE(set.load(back));
    CHECK(back == in);

    // every key looked up at once: those put in are found, the others not
    std::vector<char> found(keys.size(), 0);
    levels::KeySet<Wide>::Reader reader(set);
    REQUIRE(set.find(keys.data(), keys.size(), found.data(), reader));
    for (std::size_t i = 0; i < keys.size(); i++) {
      CHECK((found[i] != 0) == (i % 2 == 0));
    }
  }
  std::filesystem::remove_all(dir);
}

// Sorted parts join into one set without decoding; a cursor starts at any
// key; a set written to a file opens again.
TEST_CASE("sliding: key sets join, seek and reopen", "[sliding]") {
  const ScratchDir scratch("keyset2");
  const std::filesystem::path dir = scratch.path() / "work";
  std::filesystem::create_directories(dir);
  std::vector<Wide> keys;
  for (int i = 0; i < 1000; i++) {
    keys.push_back(static_cast<Wide>(i) * 37 + (i % 7 == 0 ? (static_cast<Wide>(1) << 70) : 0));
  }
  std::sort(keys.begin(), keys.end());
  for (int onDisk = 0; onDisk < 2; onDisk++) {
    const std::filesystem::path a = onDisk != 0 ? dir / "a" : std::filesystem::path();
    const std::filesystem::path b = onDisk != 0 ? dir / "b" : std::filesystem::path();
    levels::KeySet<Wide> first;
    levels::KeySet<Wide> second;
    REQUIRE(first.create(a));
    REQUIRE(second.create(b));
    // the cut leaves a part block in the first set
    REQUIRE(first.addAll(keys.begin(), keys.begin() + 333));
    REQUIRE(second.addAll(keys.begin() + 333, keys.end()));
    REQUIRE(first.finish());
    REQUIRE(second.finish());
    levels::KeySet<Wide> joined;
    REQUIRE(joined.create(onDisk != 0 ? dir / "joined" : std::filesystem::path()));
    REQUIRE(joined.appendSet(first));
    REQUIRE(joined.appendSet(second));
    REQUIRE(joined.finish());
    CHECK(joined.size() == keys.size());
    CHECK(joined.lastKey() == keys.back());
    std::vector<Wide> back;
    REQUIRE(joined.load(back));
    CHECK(back == keys);

    // a cursor from a key in the set, from one between two, and past the end
    for (const std::size_t from :
         {std::size_t{0}, std::size_t{128}, std::size_t{500}, keys.size() - 1}) {
      levels::KeySet<Wide>::Cursor c(joined, keys[from]);
      std::vector<Wide> rest;
      Wide k = 0;
      while (c.next(k)) {
        rest.push_back(k);
      }
      CHECK(rest ==
            std::vector<Wide>(keys.begin() + static_cast<std::ptrdiff_t>(from), keys.end()));
    }
    levels::KeySet<Wide>::Cursor between(joined, keys[10] + 1);
    Wide k = 0;
    REQUIRE(between.next(k));
    CHECK(k == keys[11]);
    levels::KeySet<Wide>::Cursor past(joined, keys.back() + 1);
    CHECK_FALSE(past.next(k));

    if (onDisk != 0) {
      // the file opens again by itself, every key found
      joined.release();
      levels::KeySet<Wide> reopened;
      REQUIRE(reopened.open(dir / "joined"));
      CHECK(reopened.size() == keys.size());
      std::vector<char> found(keys.size(), 0);
      levels::KeySet<Wide>::Reader reader(reopened);
      REQUIRE(reopened.find(keys.data(), keys.size(), found.data(), reader));
      CHECK(std::count(found.begin(), found.end(), 1) == static_cast<long>(keys.size()));
      // the reopened set owns the file now: the first does not delete it
      reopened.keepFile();
    }
  }
  std::filesystem::remove_all(dir);
}

#endif

// ---------------------------------------------------------------------------
// The levels' files compressed, and a search that cannot write one
// (decision 0040).

#ifdef __SIZEOF_INT128__
namespace {

// Sorted keys with the gaps of a level: a few sizes of gap over and over,
// so that zstd has something to find, and now and then a leap.
std::vector<Wide> levelLikeKeys(std::size_t count, std::uint64_t seed) {
  std::vector<Wide> keys;
  keys.reserve(count);
  std::uint64_t x = seed;
  auto rnd = [&x]() {
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return x;
  };
  const std::uint64_t gaps[5] = {1, 1, 6, 320, 70001};
  Wide k = (static_cast<Wide>(0x1234) << 80) | 77;
  for (std::size_t i = 0; i < count; i++) {
    k += gaps[rnd() % 5];
    if (rnd() % 997 == 0) {
      k += static_cast<Wide>(rnd()) << 40;
    }
    keys.push_back(k);
  }
  return keys;
}

}  // namespace

// A file's chunks compressed at any level or not at all, large chunks and
// small: the same keys back, read from end to end, from any key on and
// looked up a few at a time; the file's size is what the set says it
// takes, and smaller the better it is compressed.
TEST_CASE("sliding: a key set's file is compressed in chunks", "[sliding]") {
  const ScratchDir scratch("keychunks");
  const std::filesystem::path dir = scratch.path() / "work";
  std::filesystem::create_directories(dir);
  const std::vector<Wide> keys = levelLikeKeys(60000, 88172645463325252ULL);
  std::vector<Wide> in;
  for (std::size_t i = 0; i < keys.size(); i++) {
    if (i % 3 != 0) {
      in.push_back(keys[i]);
    }
  }
  std::uintmax_t plain = 0;
  std::uintmax_t fast = 0;
  std::uintmax_t well = 0;
  for (const int compression : {0, 1, 9, 19}) {
    for (const std::size_t chunk : {std::size_t{48}, std::size_t{4096}, levels::kChunkBytes}) {
      INFO("compression " << compression << ", chunks of " << chunk);
      levels::KeySet<Wide> set;
      REQUIRE(set.create(dir / "keys", levels::FileOptions{compression, chunk, nullptr}));
      REQUIRE(set.addAll(in.begin(), in.end()));
      REQUIRE(set.finish());
      CHECK(set.size() == in.size());
      const std::uintmax_t size = std::filesystem::file_size(dir / "keys");
      CHECK(set.bytes() == size);
      CHECK(set.rawBytes() > 0);
      if (chunk == levels::kChunkBytes) {
        (compression == 0 ? plain : compression == 1 ? fast : well) = size;
      }

      std::vector<Wide> back;
      REQUIRE(set.load(back));
      CHECK(back == in);

      // the index let go and read back: the same set
      set.release();
      CHECK_FALSE(set.indexed());
      REQUIRE(set.reload());
      CHECK(set.size() == in.size());
      CHECK(set.bytes() == size);
      CHECK(set.lastKey() == in.back());

      // from a key in the middle on, one that is in the set and one that is not
      for (const std::size_t from : {in.size() / 2, in.size() - 3}) {
        levels::KeySet<Wide>::Cursor c(set, in[from] - 1);
        Wide k = 0;
        std::size_t at = from;
        while (c.next(k)) {
          REQUIRE(at < in.size());
          REQUIRE(k == in[at]);
          at++;
        }
        CHECK_FALSE(c.failed());
        CHECK(at == in.size());
      }

      // a few keys looked up, far apart: those put in are found, the others not
      std::vector<Wide> look;
      std::vector<bool> isIn;
      for (std::size_t i = 0; i < keys.size(); i += 131) {
        look.push_back(keys[i]);
        isIn.push_back(i % 3 != 0);
      }
      std::vector<char> found(look.size(), 0);
      levels::KeySet<Wide>::Reader reader(set, std::size_t{1} << 12);
      REQUIRE(set.find(look.data(), look.size(), found.data(), reader));
      for (std::size_t i = 0; i < look.size(); i++) {
        CHECK((found[i] != 0) == isIn[i]);
      }
    }
  }
  // gaps that repeat: a good part of the file goes, more at the higher level
  CHECK(fast * 10 < plain * 8);
  CHECK(well <= fast);
  std::filesystem::remove_all(dir);
}

// Sets joined without decoding their keys, whatever each is kept in: a
// file's chunks go into a file as they are (compressed at another level,
// in chunks of another size, or not at all), blocks in memory are gathered
// into chunks, and a file's chunks are decompressed into memory.
TEST_CASE("sliding: key sets are joined whatever they are kept in", "[sliding]") {
  const ScratchDir scratch("keyjoin");
  const std::filesystem::path dir = scratch.path() / "work";
  std::filesystem::create_directories(dir);
  const std::vector<Wide> keys = levelLikeKeys(9000, 4242424242ULL);
  // a few keys added first, then three parts
  const std::size_t cuts[5] = {0, 200, 3000, 3001, keys.size()};
  struct Kind {
    bool file;
    int compression;
    std::size_t chunk;
  };
  const Kind kinds[4] = {{false, 0, 0}, {true, 0, 300}, {true, 1, 64}, {true, 9, levels::kChunkBytes}};
  int name = 0;
  for (const Kind& target : kinds) {
    for (int mix = 0; mix < 4; mix++) {
      INFO("target " << (target.file ? "file" : "memory") << " " << target.compression << ", mix "
                     << mix);
      std::vector<std::unique_ptr<levels::KeySet<Wide>>> parts;
      for (int p = 1; p < 4; p++) {
        const Kind& kind = kinds[(mix + p) % 4];
        parts.push_back(std::make_unique<levels::KeySet<Wide>>());
        REQUIRE(parts.back()->create(
            kind.file ? dir / ("part" + std::to_string(name++)) : std::filesystem::path(),
            levels::FileOptions{kind.compression, kind.chunk, nullptr}));
        REQUIRE(parts.back()->addAll(keys.begin() + static_cast<std::ptrdiff_t>(cuts[p]),
                                     keys.begin() + static_cast<std::ptrdiff_t>(cuts[p + 1])));
        REQUIRE(parts.back()->finish());
      }
      levels::KeySet<Wide> joined;
      REQUIRE(joined.create(
          target.file ? dir / ("joined" + std::to_string(name++)) : std::filesystem::path(),
          levels::FileOptions{target.compression, target.chunk, nullptr}));
      REQUIRE(joined.addAll(keys.begin(), keys.begin() + static_cast<std::ptrdiff_t>(cuts[1])));
      for (const std::unique_ptr<levels::KeySet<Wide>>& part : parts) {
        REQUIRE(joined.appendSet(*part));
      }
      REQUIRE(joined.finish());
      CHECK(joined.size() == keys.size());
      CHECK(joined.lastKey() == keys.back());
      std::vector<Wide> back;
      REQUIRE(joined.load(back));
      CHECK(back == keys);
      if (target.file) {
        CHECK(joined.bytes() == std::filesystem::file_size(joined.path()));
        // read back from the file alone
        levels::KeySet<Wide> again;
        const std::filesystem::path kept = dir / "kept";
        std::filesystem::copy_file(joined.path(), kept,
                                   std::filesystem::copy_options::overwrite_existing);
        REQUIRE(again.open(kept));
        back.clear();
        REQUIRE(again.load(back));
        CHECK(back == keys);
      }
    }
  }
  std::filesystem::remove_all(dir);
}

// A file cut short, one whose chunk is damaged, one of a later version and
// one that is not there: none is read as keys, and each says what it is.
TEST_CASE("sliding: a damaged key set file is told", "[sliding]") {
  const ScratchDir scratch("keydamage");
  const std::filesystem::path dir = scratch.path() / "work";
  std::filesystem::create_directories(dir);
  const std::vector<Wide> keys = levelLikeKeys(20000, 31337);
  const std::filesystem::path whole = dir / "whole";
  {
    levels::KeySet<Wide> set;
    REQUIRE(set.create(whole));
    REQUIRE(set.addAll(keys.begin(), keys.end()));
    REQUIRE(set.finish());
    set.keepFile();
  }
  const std::uintmax_t size = std::filesystem::file_size(whole);
  auto copyOf = [&](const char* name) {
    const std::filesystem::path p = dir / name;
    std::filesystem::copy_file(whole, p);
    return p;
  };
  auto poke = [](const std::filesystem::path& p, std::uintmax_t at, const std::string& bytes) {
    std::fstream f(p, std::ios::in | std::ios::out | std::ios::binary);
    f.seekp(static_cast<std::streamoff>(at));
    f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(f.good());
  };
  {
    levels::KeySet<Wide> set;
    REQUIRE(set.open(whole));
    CHECK_FALSE(set.older());
    CHECK(set.fault() == levels::SetFault::None);
    std::vector<Wide> back;
    REQUIRE(set.load(back));
    CHECK(back == keys);
    set.keepFile();
  }
  {
    // cut short: no footer where one should be
    const std::filesystem::path p = copyOf("short");
    std::filesystem::resize_file(p, size - 1);
    levels::KeySet<Wide> set;
    CHECK_FALSE(set.open(p));
    CHECK(set.fault() == levels::SetFault::Damaged);
    // not opened, not deleted
    CHECK(std::filesystem::file_size(p) == size - 1);
  }
  {
    // a byte of a chunk changed: the index reads, the keys do not (each
    // chunk carries a checksum)
    const std::filesystem::path p = copyOf("chunk");
    poke(p, 100, std::string(1, '\x5a'));
    poke(p, 101, std::string(1, '\xa5'));
    levels::KeySet<Wide> set;
    REQUIRE(set.open(p));
    std::vector<Wide> back;
    CHECK_FALSE(set.load(back));
    set.keepFile();
  }
  {
    // the ending of a format after this one: told apart from a damaged file
    const std::filesystem::path p = copyOf("later");
    poke(p, size - 8, "BTSLKEY4");
    levels::KeySet<Wide> set;
    CHECK_FALSE(set.open(p));
    CHECK(set.fault() == levels::SetFault::Newer);
  }
  {
    // the ending of the format before on a file that is not one
    const std::filesystem::path p = copyOf("notolder");
    poke(p, size - 8, "BTSLKEY2");
    levels::KeySet<Wide> set;
    CHECK_FALSE(set.open(p));
    CHECK(set.fault() == levels::SetFault::Damaged);
  }
  {
    // no file: the system's reason is kept
    levels::KeySet<Wide> set;
    CHECK_FALSE(set.open(dir / "nothing"));
    CHECK(set.fault() == levels::SetFault::Unread);
    CHECK(set.faultError() == ENOENT);
  }
  std::filesystem::remove_all(dir);
}

// A file of the versions up to 1.9 (its blocks not compressed, no chunks)
// is read as it is: every key, a key looked up, a cursor from the middle.
// Appended to a file of this version it is written as that file is, in
// compressed chunks, the same keys.
TEST_CASE("sliding: a key set file of an earlier version is read, and written again",
          "[sliding]") {
  const ScratchDir scratch("keyolder");
  const std::filesystem::path dir = scratch.path() / "work";
  std::filesystem::create_directories(dir);
  for (const std::size_t count : {std::size_t{0}, std::size_t{1}, std::size_t{128},
                                  std::size_t{129}, std::size_t{200000}}) {
    INFO(count << " keys");
    const std::vector<Wide> keys = levelLikeKeys(count, 2463534242ULL + count);
    const std::filesystem::path old = dir / "old.keys";
    writeOlderSet(old, keys);
    const std::uintmax_t oldSize = std::filesystem::file_size(old);
    levels::KeySet<Wide> set;
    REQUIRE(set.open(old));
    CHECK(set.older());
    CHECK(set.size() == keys.size());
    CHECK(set.bytes() == oldSize);
    CHECK(set.lastKey() == (keys.empty() ? Wide{0} : keys.back()));
    std::vector<Wide> back;
    REQUIRE(set.load(back));
    CHECK(back == keys);
    // the index let go and read back
    set.release();
    REQUIRE(set.reload());
    CHECK(set.older());
    if (count > 1000) {
      // keys looked up, there and not there
      std::vector<Wide> cand = {keys[3], keys[3] + 1, keys[count / 2], keys.back()};
      std::sort(cand.begin(), cand.end());
      std::vector<char> found(cand.size(), 0);
      levels::KeySet<Wide>::Reader reader(set, std::size_t{1} << 12);
      REQUIRE(set.find(cand.data(), cand.size(), found.data(), reader));
      for (std::size_t i = 0; i < cand.size(); i++) {
        CHECK((found[i] != 0) == std::binary_search(keys.begin(), keys.end(), cand[i]));
      }
      // a cursor from the middle on
      levels::KeySet<Wide>::Cursor cursor(set, keys[count / 2]);
      Wide k = 0;
      for (std::size_t i = count / 2; i < count; i++) {
        REQUIRE(cursor.next(k));
        REQUIRE(k == keys[i]);
      }
      CHECK_FALSE(cursor.next(k));
      CHECK_FALSE(cursor.failed());
    }
    // written again as this version writes: after keys of its own, too
    for (const bool after : {false, true}) {
      const std::filesystem::path fresh = dir / "fresh.keys";
      levels::KeySet<Wide> made;
      REQUIRE(made.create(fresh));
      std::vector<Wide> want;
      if (after && !keys.empty() && keys.front() > 5) {
        want = {1, 2, 5};
        REQUIRE(made.addAll(want.begin(), want.end()));
      }
      want.insert(want.end(), keys.begin(), keys.end());
      std::uint64_t heard = 0;
      REQUIRE(made.appendOlder(set, [&heard](std::uint64_t bytes) {
        heard += bytes;
        return true;
      }));
      REQUIRE(made.finish());
      CHECK_FALSE(made.older());
      CHECK(made.size() == want.size());
      CHECK(fileEnding(fresh) == std::string(levels::kSetMagic, sizeof(levels::kSetMagic)));
      if (count > 0) {
        CHECK(heard > 0);
      }
      if (count > 1000) {
        CHECK(made.bytes() < oldSize);
      }
      std::vector<Wide> again;
      REQUIRE(made.load(again));
      CHECK(again == want);
      // and opened anew from its file
      made.keepFile();
      levels::KeySet<Wide> opened;
      REQUIRE(opened.open(fresh));
      CHECK_FALSE(opened.older());
      REQUIRE(opened.load(again));
      CHECK(again == want);
    }
    if (count > 1000) {
      // the work ended from outside: nothing more is read
      levels::KeySet<Wide> made;
      REQUIRE(made.create(dir / "cut.keys"));
      unsigned int steps = 0;
      CHECK_FALSE(made.appendOlder(set, [&steps](std::uint64_t) { return ++steps < 2; }));
      CHECK(steps == 2);
      // a file cut short is not opened as an older set either
      set.keepFile();
      std::filesystem::resize_file(old, oldSize - 9);
      levels::KeySet<Wide> cut;
      CHECK_FALSE(cut.open(old));
    } else {
      set.keepFile();
    }
  }
  std::filesystem::remove_all(dir);
}

// A level file's index was trusted once its footer added up, and so were
// the blocks of a chunk left as it is, which has no checksum: a chunk zstd
// could not shrink, every chunk of a file written without compression,
// and every file of the versions up to 1.9, which a carried-on search
// reads (cleanup item 16.3). A reader followed a damaged offset out of its
// buffer, a block that said it had no keys wrote past an empty vector, and
// one that said it had billions asked for their room. The index is now
// checked once, as the file is opened (Damaged, as a file cut short is),
// and a block as it is read (a read that fails).
TEST_CASE("sliding: a key set file with a damaged index or block is told", "[sliding]") {
  const ScratchDir scratch("keytables");
  const std::filesystem::path dir = scratch.path() / "work";
  std::filesystem::create_directories(dir);
  const std::vector<Wide> keys = levelLikeKeys(20000, 4711);
  const auto u64At = [](const std::filesystem::path& p, std::uintmax_t at) {
    std::ifstream f(p, std::ios::binary);
    f.seekg(static_cast<std::streamoff>(at));
    std::uint64_t v = 0;
    f.read(reinterpret_cast<char*>(&v), sizeof(v));
    REQUIRE(f.good());
    return v;
  };
  const auto poke = [](const std::filesystem::path& p, std::uintmax_t at, const void* bytes,
                       std::size_t len) {
    std::fstream f(p, std::ios::in | std::ios::out | std::ios::binary);
    f.seekp(static_cast<std::streamoff>(at));
    f.write(static_cast<const char*>(bytes), static_cast<std::streamsize>(len));
    REQUIRE(f.good());
  };
  const auto pokeU64 = [&poke](const std::filesystem::path& p, std::uintmax_t at, std::uint64_t v) {
    poke(p, at, &v, sizeof(v));
  };
  struct Kind {
    const char* name;
    bool older;
    int compression;
  };
  int made = 0;
  const Kind kinds[3] = {{"not compressed", false, 0},
                         {"compressed", false, 3},
                         {"of a version up to 1.9", true, 0}};
  for (const Kind& kind : kinds) {
    const std::filesystem::path whole = dir / "whole";
    std::filesystem::remove(whole);
    if (kind.older) {
      writeOlderSet(whole, keys);
    } else {
      levels::KeySet<Wide> set;
      REQUIRE(set.create(whole, levels::FileOptions{kind.compression, 4096, nullptr}));
      REQUIRE(set.addAll(keys.begin(), keys.end()));
      REQUIRE(set.finish());
      set.keepFile();
    }
    // where the file keeps what: its footer's numbers, then the tables
    const std::uintmax_t size = std::filesystem::file_size(whole);
    const std::uintmax_t tailAt = size - (kind.older ? 24 : 40) - sizeof(Wide) - 8;
    const std::uint64_t count = u64At(whole, tailAt);
    const std::uint64_t blocks = u64At(whole, tailAt + 8);
    const std::uint64_t chunks = kind.older ? 0 : u64At(whole, tailAt + 16);
    const std::uint64_t dataBytes = u64At(whole, tailAt + (kind.older ? 16 : 32));
    const std::uintmax_t firstsAt = dataBytes;
    const std::uintmax_t offsetsAt = firstsAt + blocks * sizeof(Wide);
    const std::uintmax_t chunkRawAt = offsetsAt + (blocks + 1) * 8;
    const std::uintmax_t chunkAtAt = chunkRawAt + (chunks + 1) * 8;
    const std::uintmax_t lastAt = size - 8 - sizeof(Wide);
    REQUIRE(count == keys.size());
    REQUIRE(blocks == (keys.size() + levels::kBlockKeys - 1) / levels::kBlockKeys);
    REQUIRE((kind.older || chunks > 3));

    const auto copy = [&]() {
      const std::filesystem::path p = dir / ("damaged" + std::to_string(made++));
      std::filesystem::copy_file(whole, p);
      return p;
    };
    const auto reads = [&](const std::filesystem::path& p) {
      levels::KeySet<Wide> set;
      if (!set.open(p)) {
        return false;
      }
      std::vector<Wide> back;
      const bool ok = set.load(back);
      CHECK((!ok || back == keys));
      set.keepFile();
      return ok;
    };
    // the index: the file is not opened, and says it is damaged
    const auto notASet = [&](const char* what, const std::filesystem::path& p) {
      INFO(kind.name << ": " << what);
      levels::KeySet<Wide> set;
      CHECK_FALSE(set.open(p));
      CHECK(set.fault() == levels::SetFault::Damaged);
      CHECK(std::filesystem::file_size(p) == size);
    };
    REQUIRE(reads(copy()));
    {
      const std::filesystem::path p = copy();
      pokeU64(p, offsetsAt + 8, u64At(p, offsetsAt + 16) + 1);
      notASet("a block that begins after the next", p);
    }
    {
      const std::filesystem::path p = copy();
      pokeU64(p, offsetsAt + 8, 0);
      notASet("a block of no bytes", p);
    }
    {
      const std::filesystem::path p = copy();
      pokeU64(p, offsetsAt + 8 * (blocks - 1), dataBytes + (std::uint64_t{1} << 40));
      notASet("a block far past the end", p);
    }
    {
      const std::filesystem::path p = copy();
      Wide first[2];
      {
        std::ifstream f(p, std::ios::binary);
        f.seekg(static_cast<std::streamoff>(firstsAt));
        f.read(reinterpret_cast<char*>(first), sizeof(first));
        REQUIRE(f.good());
      }
      std::swap(first[0], first[1]);
      poke(p, firstsAt, first, sizeof(first));
      notASet("first keys that do not rise", p);
    }
    {
      const std::filesystem::path p = copy();
      const Wide none = 0;
      poke(p, lastAt, &none, sizeof(none));
      notASet("a last key below the last block's first", p);
    }
    for (const std::uint64_t claim :
         {blocks - 1, blocks * levels::kBlockKeys + 1, std::uint64_t{1} << 60}) {
      const std::filesystem::path p = copy();
      pokeU64(p, tailAt, claim);
      notASet("a key count the blocks cannot hold", p);
    }
    {
      const std::filesystem::path p = copy();
      pokeU64(p, tailAt + (kind.older ? 16 : 32), size + 1);
      notASet("more bytes of blocks than the file has", p);
    }
    if (!kind.older) {
      {
        const std::filesystem::path p = copy();
        pokeU64(p, chunkRawAt + 16, u64At(p, chunkRawAt + 8));
        notASet("a chunk of no blocks", p);
      }
      {
        const std::filesystem::path p = copy();
        pokeU64(p, chunkRawAt + 8, u64At(p, chunkRawAt + 8) + 1);
        notASet("a block that lies across two chunks", p);
      }
      {
        const std::filesystem::path p = copy();
        pokeU64(p, chunkAtAt + 16, u64At(p, chunkAtAt + 8));
        notASet("a chunk of no bytes in the file", p);
      }
      {
        // the chunk before it then takes more of the file than its blocks
        const std::filesystem::path p = copy();
        pokeU64(p, chunkAtAt + 8, u64At(p, chunkAtAt + 16) - 1);
        pokeU64(p, chunkRawAt + 8, 1);
        notASet("a chunk larger in the file than its blocks", p);
      }
      {
        // a chunk that claims 2^40 bytes of blocks (its last block as
        // many): no room is made for it
        const std::filesystem::path p = copy();
        const std::uint64_t huge = std::uint64_t{1} << 40;
        for (std::uint64_t c = 2; c <= chunks; c++) {
          pokeU64(p, chunkRawAt + 8 * c, u64At(p, chunkRawAt + 8 * c) + huge);
        }
        for (std::uint64_t b = 0; b <= blocks; b++) {
          const std::uint64_t at = u64At(p, offsetsAt + 8 * b);
          if (at >= u64At(whole, chunkRawAt + 16)) {
            pokeU64(p, offsetsAt + 8 * b, at + huge);
          }
        }
        pokeU64(p, tailAt + 24, u64At(p, tailAt + 24) + huge);
        notASet("a chunk of more blocks' bytes than any has", p);
      }
    }
    if (kind.compression != 0) {
      continue;  // a compressed chunk's bytes are under its checksum
    }
    // the blocks: the file opens, and its keys are not read
    const auto notRead = [&](const char* what, const std::filesystem::path& p) {
      INFO(kind.name << ": " << what);
      CHECK_FALSE(reads(p));
      levels::KeySet<Wide> set;
      REQUIRE(set.open(p));
      levels::KeySet<Wide>::Cursor cursor(set);
      Wide k = 0;
      std::size_t got = 0;
      while (cursor.next(k)) {
        got++;
      }
      CHECK(cursor.failed());
      CHECK(got < keys.size());
      set.keepFile();
    };
    // the first block: its 128 keys' count is the bytes 0x80 0x01
    REQUIRE(u64At(whole, 0) % 65536 == 0x0180);
    {
      const std::filesystem::path p = copy();
      const std::uint8_t zero = 0;
      poke(p, 0, &zero, 1);
      notRead("a block of no keys", p);
    }
    {
      const std::filesystem::path p = copy();
      poke(p, 0, "\x81\x01", 2);
      notRead("a block of more keys than a block has", p);
    }
    {
      const std::filesystem::path p = copy();
      poke(p, 0, "\xff\xff\xff\xff\x7f", 5);
      notRead("a block of billions of keys", p);
    }
    {
      const std::filesystem::path p = copy();
      poke(p, 0, "\x7f", 1);
      notRead("a block of fewer keys than its bytes hold", p);
    }
    {
      const std::filesystem::path p = copy();
      const std::string run(40, '\xff');
      poke(p, 2, run.data(), run.size());
      notRead("a gap that never ends", p);
    }
    {
      // the last byte of the last block says another follows
      const std::filesystem::path p = copy();
      const auto end = static_cast<unsigned char>(u64At(p, dataBytes - 8) >> 56);
      const unsigned char on = end | 0x80U;
      poke(p, dataBytes - 1, &on, 1);
      notRead("a gap cut off by the end of the blocks", p);
    }
  }
  std::filesystem::remove_all(dir);
}

// A set of more keys than 2^33, as a level of the largest searches has,
// written as the threads of a merge write its parts and joined; and one
// read of more than 2 GB (macOS reads no more than that at a time). About
// 4 GB of disk, as much memory and a few minutes: hidden.
TEST_CASE("sliding: a key set of more than 2^33 keys", "[.huge][sliding]") {
  const ScratchDir scratch("keyhuge");
  const std::filesystem::path dir = scratch.path() / "work";
  std::filesystem::create_directories(dir);
  using Set = levels::KeySet<Wide>;

  {
    // a file with nothing in it but a mark near its end, read in one call
    const std::filesystem::path big = dir / "big";
    const std::uint64_t size = 2600000000ULL;
    const std::uint64_t markAt = size - 1000;
    {
      std::ofstream f(big, std::ios::binary);
      f.seekp(static_cast<std::streamoff>(markAt));
      f.write("mark", 4);
      REQUIRE(f.good());
    }
    std::filesystem::resize_file(big, size);
    levels::ReadFile f;
    REQUIRE(f.open(big));
    std::vector<char> all(size);
    REQUIRE(f.readAt(0, all.data(), all.size()));
    CHECK(std::string(all.data() + markAt, 4) == "mark");
    CHECK(all[markAt - 1] == 0);
    std::filesystem::remove(big);
  }

  const std::uint64_t half = (std::uint64_t{1} << 32) + 300000000ULL;
  const std::uint64_t total = 2 * half;
  REQUIRE(total > (std::uint64_t{1} << 33));
  const Wide base = (static_cast<Wide>(1) << 100) + 5;
  // gaps of 3 and 1 in turn
  auto keyAt = [&](std::uint64_t i) { return base + static_cast<Wide>(2) * i + (i & 1); };

  Set joined;
  {
    Set parts[2];
    std::atomic<bool> ok{true};
    std::vector<std::thread> writers;
    for (int p = 0; p < 2; p++) {
      writers.emplace_back([&, p] {
        Set& set = parts[p];
        bool good = set.create(dir / ("part" + std::to_string(p)),
                               levels::FileOptions{p == 0 ? 1 : 9, levels::kChunkBytes, nullptr});
        const std::uint64_t from = p == 0 ? 0 : half;
        for (std::uint64_t i = from; good && i < from + half; i++) {
          good = set.add(keyAt(i));
        }
        if (!good || !set.finish()) {
          ok.store(false);
        }
      });
    }
    for (std::thread& t : writers) {
      t.join();
    }
    REQUIRE(ok.load());
    CHECK(parts[0].size() == half);
    REQUIRE(joined.create(dir / "joined"));
    REQUIRE(joined.appendSet(parts[0]));
    REQUIRE(joined.appendSet(parts[1]));
    REQUIRE(joined.finish());
  }
  CHECK(joined.size() == total);
  CHECK(joined.lastKey() == keyAt(total - 1));
  CHECK(joined.bytes() == std::filesystem::file_size(joined.path()));
  joined.release();
  REQUIRE(joined.reload());
  CHECK(joined.size() == total);
  CHECK(joined.lastKey() == keyAt(total - 1));
  const std::size_t blocks = joined.blocks();
  CHECK(blocks == 2 * ((half + levels::kBlockKeys - 1) / levels::kBlockKeys));

  // from keys on either side of 2^32 and 2^33, of the join and of the end
  for (const std::uint64_t from :
       {std::uint64_t{0}, (std::uint64_t{1} << 32) - 70, half - 3, (std::uint64_t{1} << 33) - 70,
        total - 500}) {
    INFO("from key " << from);
    Set::Cursor c(joined, keyAt(from));
    Wide k = 0;
    for (std::uint64_t i = from; i < std::min(total, from + 1000); i++) {
      REQUIRE(c.next(k));
      REQUIRE(k == keyAt(i));
    }
    CHECK_FALSE(c.failed());
  }
  {
    // looked up: keys are found, the numbers between two keys are not
    const std::uint64_t at[4] = {10, (std::uint64_t{1} << 32) + 10, (std::uint64_t{1} << 33) + 10,
                                 total - 2};
    std::vector<Wide> look;
    for (const std::uint64_t i : at) {
      look.push_back(keyAt(i));
      look.push_back(keyAt(i) + 1);
    }
    std::vector<char> found(look.size(), 0);
    Set::Reader reader(joined, std::size_t{1} << 12);
    REQUIRE(joined.find(look.data(), look.size(), found.data(), reader));
    for (std::size_t i = 0; i < look.size(); i++) {
      CHECK((found[i] != 0) == (i % 2 == 0));
    }
  }

  // every key, each thread a range of blocks: none missing, none wrong
  const unsigned int threads = 8;
  std::atomic<std::uint64_t> counted{0};
  std::atomic<std::uint64_t> wrong{0};
  std::vector<std::thread> readers;
  for (unsigned int t = 0; t < threads; t++) {
    readers.emplace_back([&, t] {
      Set::Reader reader(joined);
      std::vector<Wide> keys;
      std::uint64_t n = 0;
      std::uint64_t bad = 0;
      for (std::size_t b = blocks * t / threads; b < blocks * (t + 1) / threads; b++) {
        if (!reader.block(b, keys)) {
          bad++;
          continue;
        }
        // a key says which it is: twice its number, and one more when odd
        std::uint64_t i = static_cast<std::uint64_t>((keys.front() - base) / 2);
        for (const Wide k : keys) {
          bad += k != keyAt(i++) ? 1 : 0;
        }
        n += keys.size();
      }
      counted.fetch_add(n);
      wrong.fetch_add(bad);
    });
  }
  for (std::thread& t : readers) {
    t.join();
  }
  CHECK(counted.load() == total);
  CHECK(wrong.load() == 0);
  joined.remove();
  std::filesystem::remove_all(dir);
}

#endif

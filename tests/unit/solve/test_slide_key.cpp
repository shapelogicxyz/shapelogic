// SPDX-License-Identifier: GPL-3.0-or-later
// The slide search's keys wider than 128 bits (shapelogic/solve/slide_key.h): they
// do what a built-in integer does, and the key sets hold them.
#include "../scratch_dir.h"

#include "shapelogic/solve/slide_key.h"
#include "shapelogic/solve/slide_levels.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <type_traits>
#include <vector>

using namespace sl::sliding;
using sl::test::ScratchDir;

namespace {

struct Rnd {
  std::uint64_t x = 88172645463325252ULL;
  std::uint64_t operator()() {
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return x;
  }
};

template <unsigned int W>
BigKey<W> randomKey(Rnd& rnd) {
  BigKey<W> k;
  for (unsigned int i = 0; i < W; i++) {
    k.w[i] = rnd();
  }
  return k;
}

}  // namespace

static_assert(std::is_trivially_copyable_v<BigKey<3>>);
static_assert(sizeof(BigKey<3>) == 24);
static_assert(sizeof(BigKey<8>) == 64);
static_assert(levels::kMaxVarint<std::uint64_t> == 10);
static_assert(levels::kMaxVarint<BigKey<4>> == 37);

#ifdef __SIZEOF_INT128__
namespace {
__extension__ using Wide = unsigned __int128;

Wide wide(const BigKey<2>& k) { return (static_cast<Wide>(k.w[1]) << 64) | k.w[0]; }
}  // namespace

// Two words do what the compiler's 128-bit integer does.
TEST_CASE("sliding: a key of two words is a 128-bit integer", "[sliding]") {
  static_assert(levels::kMaxVarint<Wide> == 19);
  Rnd rnd;
  for (int trial = 0; trial < 2000; trial++) {
    BigKey<2> a = randomKey<2>(rnd);
    BigKey<2> b = randomKey<2>(rnd);
    if (trial % 4 == 0) {
      // close together, and across a word's end
      b = a;
      b.w[0] += rnd() % 5;
    }
    if (trial % 7 == 0) {
      a.w[0] = ~std::uint64_t{0} - rnd() % 3;
    }
    const Wide x = wide(a);
    const Wide y = wide(b);
    CHECK(wide(a + b) == static_cast<Wide>(x + y));
    CHECK(wide(a - b) == static_cast<Wide>(x - y));
    CHECK(wide(a & b) == (x & y));
    CHECK(wide(a | b) == (x | y));
    CHECK(wide(a ^ b) == (x ^ y));
    CHECK(wide(~a) == static_cast<Wide>(~x));
    CHECK((a == b) == (x == y));
    CHECK((a < b) == (x < y));
    CHECK((a <= b) == (x <= y));
    CHECK((a > b) == (x > y));
    const unsigned int s = static_cast<unsigned int>(rnd() % 128);
    CHECK(wide(a << s) == static_cast<Wide>(x << s));
    CHECK(wide(a >> s) == (x >> s));
    CHECK(static_cast<std::uint64_t>(a) == static_cast<std::uint64_t>(x));
    CHECK(static_cast<unsigned int>(a) == static_cast<unsigned int>(x));
    CHECK(static_cast<bool>(a) == (x != 0));
    // a field of up to 64 bits, anywhere
    const unsigned int width = 1 + static_cast<unsigned int>(rnd() % 64);
    const unsigned int shift = static_cast<unsigned int>(rnd() % (129 - width));
    const std::uint64_t mask = width == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << width) - 1;
    CHECK(keyField(a, shift, mask) == keyField(x, shift, mask));
    CHECK(wide(keyClear(a, shift, mask)) == keyClear(x, shift, mask));
    const std::uint64_t value = rnd() & mask;
    CHECK(wide(keyAt<BigKey<2>>(value, shift)) == keyAt<Wide>(value, shift));
    CHECK(keyField(keyClear(a, shift, mask) | keyAt<BigKey<2>>(value, shift), shift, mask) ==
          value);
  }
  CHECK(wide(BigKey<2>{5} << 128) == 0);
  CHECK(wide(BigKey<2>{5} >> 128) == 0);
  CHECK(BigKey<2>{} == BigKey<2>{0});
  CHECK_FALSE(static_cast<bool>(BigKey<2>{}));
}
#endif

// The wider keys: shifts and fields across every word, carries all the way.
TEST_CASE("sliding: a wide key shifts, adds and keeps fields across its words", "[sliding]") {
  Rnd rnd;
  for (int trial = 0; trial < 1000; trial++) {
    const BigKey<8> a = randomKey<8>(rnd);
    const BigKey<8> b = randomKey<8>(rnd);
    const unsigned int s = static_cast<unsigned int>(rnd() % 512);
    // the bits below s and the bits from s on, each shifted out and back
    const BigKey<8> low = (a << (512 - s)) >> (512 - s);
    const BigKey<8> high = (a >> s) << s;
    CHECK((low | high) == a);
    CHECK((low & high) == BigKey<8>{0});
    if (s > 0) {
      CHECK((a << s) == ((a << (s - 1)) << 1));
      CHECK((a >> s) == ((a >> (s - 1)) >> 1));
    }
    CHECK((a + b) - b == a);
    CHECK((a - b) + b == a);
    CHECK(a + (~a) == ~BigKey<8>{0});
    CHECK((a < b) != (b <= a));
    CHECK((a ^ b ^ b) == a);
    const unsigned int width = 1 + static_cast<unsigned int>(rnd() % 64);
    const unsigned int shift = static_cast<unsigned int>(rnd() % (513 - width));
    const std::uint64_t mask = width == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << width) - 1;
    CHECK(keyField(a, shift, mask) == static_cast<std::uint64_t>((a >> shift) & BigKey<8>{mask}));
    CHECK(keyClear(a, shift, mask) == (a & ~(BigKey<8>{mask} << shift)));
    const std::uint64_t value = rnd() & mask;
    CHECK(keyAt<BigKey<8>>(value, shift) == (BigKey<8>{value} << shift));
  }
  // a carry runs through every word, and so does a borrow
  BigKey<3> ones = ~BigKey<3>{0};
  CHECK(ones + 1 == BigKey<3>{0});
  CHECK(BigKey<3>{0} - 1 == ones);
  BigKey<3> top;
  top.w[2] = 1;
  CHECK(top > BigKey<3>{~std::uint64_t{0}});
  CHECK(top - 1 < top);
  CHECK((top - 1).w[1] == ~std::uint64_t{0});
}

// The key sets keep the gaps between keys as varints: a wide key's gaps too.
TEST_CASE("sliding: a key set holds keys wider than 128 bits", "[sliding]") {
  using Key = BigKey<4>;
  const ScratchDir scratch("widekeyset");
  const std::filesystem::path dir = scratch.path() / "work";
  std::filesystem::create_directories(dir);
  for (int onDisk = 0; onDisk < 2; onDisk++) {
    // keys spread over all 256 bits, a few close together, sorted
    Rnd rnd;
    std::vector<Key> keys;
    for (int i = 0; i < 5000; i++) {
      Key k = randomKey<4>(rnd);
      if (i % 3 == 0 && !keys.empty()) {
        k = keys.back() + (1 + rnd() % 300);
      }
      keys.push_back(k);
    }
    keys.push_back(Key{0});
    keys.push_back(~Key{0});
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());

    levels::KeySet<Key> set;
    REQUIRE(set.create(onDisk != 0 ? dir / "keys" : std::filesystem::path()));
    std::vector<Key> in;
    for (std::size_t i = 0; i < keys.size(); i += 2) {
      in.push_back(keys[i]);
    }
    REQUIRE(set.addAll(in.begin(), in.end()));
    REQUIRE(set.finish());
    CHECK(set.size() == in.size());
    CHECK(set.lastKey() == in.back());

    std::vector<Key> back;
    REQUIRE(set.load(back));
    CHECK(back == in);

    // every key looked up at once: those put in are found, the others not
    std::vector<char> found(keys.size(), 0);
    levels::KeySet<Key>::Reader reader(set);
    REQUIRE(set.find(keys.data(), keys.size(), found.data(), reader));
    for (std::size_t i = 0; i < keys.size(); i++) {
      CHECK((found[i] != 0) == (i % 2 == 0));
    }

    // from a key on, in order
    const std::size_t from = in.size() / 3;
    levels::KeySet<Key>::Cursor cursor(set, in[from] - 1);
    Key k;
    for (std::size_t i = from; i < in.size(); i++) {
      REQUIRE(cursor.next(k));
      CHECK(k == in[i]);
    }
    CHECK_FALSE(cursor.next(k));

    if (onDisk != 0) {
      // read back from the file by a set of its own
      set.keepFile();
      levels::KeySet<Key> again;
      REQUIRE(again.open(dir / "keys"));
      std::vector<Key> reread;
      REQUIRE(again.load(reread));
      CHECK(reread == in);
    }
  }
  std::filesystem::remove_all(dir);
}

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// What the key set tests and the saved search tests share
/// (test_slide_levels.cpp, test_sliding_saved.cpp; cleanup item 21.2): a
/// key of 128 bits and a key set's file as the versions up to 1.9 wrote it.

#include "shapelogic/solve/slide_levels.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace bt::test {

// The helpers are written in the slide search's names, as the cases are.
using namespace bt::sliding;

#ifdef __SIZEOF_INT128__
__extension__ using Wide = unsigned __int128;

// A key set's file as the versions up to 1.9 wrote it (BTSLKEY2): the
// blocks as they are, the first keys, the offsets, three numbers, the last
// key and the magic.
template <class Key>
void writeOlderSet(const std::filesystem::path& p, const std::vector<Key>& keys) {
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  std::vector<Key> firsts;
  std::vector<std::uint64_t> offsets{0};
  std::vector<std::uint8_t> block;
  for (std::size_t i = 0; i < keys.size(); i += levels::kBlockKeys) {
    const std::size_t n = std::min<std::size_t>(levels::kBlockKeys, keys.size() - i);
    block.clear();
    std::uint8_t head[levels::kMaxVarint<unsigned int>];
    unsigned int len = levels::putVarint(head, static_cast<unsigned int>(n));
    block.insert(block.end(), head, head + len);
    for (std::size_t k = 1; k < n; k++) {
      std::uint8_t gap[levels::kMaxVarint<Key>];
      len = levels::putVarint(gap, static_cast<Key>(keys[i + k] - keys[i + k - 1]));
      block.insert(block.end(), gap, gap + len);
    }
    out.write(reinterpret_cast<const char*>(block.data()),
              static_cast<std::streamsize>(block.size()));
    firsts.push_back(keys[i]);
    offsets.push_back(offsets.back() + block.size());
  }
  out.write(reinterpret_cast<const char*>(firsts.data()),
            static_cast<std::streamsize>(firsts.size() * sizeof(Key)));
  out.write(reinterpret_cast<const char*>(offsets.data()),
            static_cast<std::streamsize>(offsets.size() * sizeof(std::uint64_t)));
  const std::uint64_t tail[3] = {keys.size(), firsts.size(), offsets.back()};
  out.write(reinterpret_cast<const char*>(tail), sizeof(tail));
  const Key last = keys.empty() ? Key{0} : keys.back();
  out.write(reinterpret_cast<const char*>(&last), sizeof(Key));
  out.write(levels::kOlderSetMagic, sizeof(levels::kOlderSetMagic));
  REQUIRE(out.good());
}

// The last eight bytes of a file: a key set's say which version wrote it.
inline std::string fileEnding(const std::filesystem::path& p) {
  std::ifstream in(p, std::ios::binary);
  in.seekg(-8, std::ios::end);
  std::string ending(8, '\0');
  in.read(ending.data(), 8);
  return in.good() ? ending : std::string();
}

#endif

}  // namespace bt::test

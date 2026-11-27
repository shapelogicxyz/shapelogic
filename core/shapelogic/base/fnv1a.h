// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// FNV-1a, the hash that names the saved searches (the slide search's
/// folders, the Panex search's) and signs the assemblies a search reports.

#include <cstdint>

namespace bt {

/// FNV-1a's offset basis and prime. The assemblers' signatures mix whole
/// values with them (fnvMix, the original's formula; saved positions carry
/// the signatures).
inline constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
inline constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

/// FNV-1a over the bytes of values. Each value goes in at the width its
/// call names, the least significant byte first (cleanup item 19.3): a hash
/// over the raw bytes of whatever it was handed would rename every saved
/// search when a getter came to return another type. A hasher may start
/// from another basis: the Panex search's names start from the one the
/// original wrote, a digit short of kFnvOffset, and stay so.
/// tests/unit/solve/test_search_names.cpp pins the names that come out.
class Fnv1a {
 public:
  Fnv1a() = default;
  /// Starts from `offset` in place of kFnvOffset.
  explicit Fnv1a(std::uint64_t offset) : h_(offset) {}

  /// A value in one byte, four or eight.
  void u8(std::uint8_t v) { bytes(v, 1); }
  void u32(std::uint32_t v) { bytes(v, 4); }
  void u64(std::uint64_t v) { bytes(v, 8); }
  /// A signed number in its 32 bits.
  void i32(std::int32_t v) { u32(static_cast<std::uint32_t>(v)); }
  /// A flag in one byte, 1 or 0.
  void flag(bool v) { u8(v ? 1 : 0); }
  /// The hash of everything mixed in so far.
  [[nodiscard]] std::uint64_t value() const { return h_; }

 private:
  void bytes(std::uint64_t v, unsigned int count) {
    for (unsigned int i = 0; i < count; i++) {
      h_ ^= (v >> (8 * i)) & 0xFF;
      h_ *= kFnvPrime;
    }
  }

  std::uint64_t h_ = kFnvOffset;
};

}  // namespace bt

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// A fixed-size bit set that can be built from a hexadecimal string at
/// compile time; the sphere grid's symmetry tables (240 transformations) are
/// stored that way. Ported from src/lib/bitfield.h.

#include "shapelogic/base/assert.h"

#include <array>
#include <bit>
#include <cstdint>
#include <string_view>

namespace bt {

/// `bits` bits, all clear on construction.
template <int bits>
class Bitfield {
  static_assert(bits > 0, "Bitfield needs a positive bit count");
  static constexpr int kWords = (bits + 63) / 64;

 public:
  constexpr Bitfield() = default;

  /// The bits of a hexadecimal number, most significant digit first, no
  /// prefix. Shorter strings leave the upper bits clear.
  constexpr explicit Bitfield(std::string_view hex) {
    BT_ASSERT(4 * hex.size() <= static_cast<std::size_t>(bits));
    int word = 0;
    int bitPos = 0;
    for (auto it = hex.rbegin(); it != hex.rend(); ++it) {
      const char c = *it;
      std::uint64_t v = 0;
      if (c >= '0' && c <= '9') {
        v = static_cast<std::uint64_t>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        v = static_cast<std::uint64_t>(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        v = static_cast<std::uint64_t>(c - 'A' + 10);
      } else {
        BT_ASSERT_MSG(false, "not a hexadecimal digit");
      }
      field_[static_cast<std::size_t>(word)] |= v << bitPos;
      bitPos += 4;
      if (bitPos >= 64) {
        bitPos = 0;
        word++;
      }
    }
  }
  /// Implicit, so tables can list plain string literals (tabs_2/*.inc).
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr Bitfield(const char* hex) : Bitfield(std::string_view(hex)) {}

  /// True when bit `pos` is set.
  [[nodiscard]] constexpr bool get(unsigned int pos) const {
    BT_ASSERT(pos < static_cast<unsigned int>(bits));
    return (field_[pos >> 6] & (std::uint64_t{1} << (pos & 63))) != 0;
  }
  /// Sets bit `pos`.
  constexpr void set(unsigned int pos) {
    BT_ASSERT(pos < static_cast<unsigned int>(bits));
    field_[pos >> 6] |= std::uint64_t{1} << (pos & 63);
  }
  /// Clears bit `pos`.
  constexpr void reset(unsigned int pos) {
    BT_ASSERT(pos < static_cast<unsigned int>(bits));
    field_[pos >> 6] &= ~(std::uint64_t{1} << (pos & 63));
  }
  /// True when at least one bit is set.
  [[nodiscard]] constexpr bool notNull() const {
    for (auto w : field_) {
      if (w != 0) {
        return true;
      }
    }
    return false;
  }
  /// The number of set bits.
  [[nodiscard]] constexpr unsigned int countBits() const {
    unsigned int n = 0;
    for (auto w : field_) {
      n += static_cast<unsigned int>(std::popcount(w));
    }
    return n;
  }

  constexpr Bitfield operator&(const Bitfield& o) const {
    Bitfield r;
    for (std::size_t i = 0; i < field_.size(); i++) {
      r.field_[i] = field_[i] & o.field_[i];
    }
    return r;
  }
  constexpr Bitfield operator|(const Bitfield& o) const {
    Bitfield r;
    for (std::size_t i = 0; i < field_.size(); i++) {
      r.field_[i] = field_[i] | o.field_[i];
    }
    return r;
  }
  constexpr bool operator==(const Bitfield&) const = default;

 private:
  std::array<std::uint64_t, kWords> field_{};
};

}  // namespace bt

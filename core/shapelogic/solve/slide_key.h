// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// A key wider than the widest built-in integer, for the slide search
/// (bt/solve/sliding_search.h): an arrangement of a board whose pieces'
/// positions do not fit 128 bits is packed into one of these, and the level
/// search works on it as it does on a built-in integer.
///
/// `_BitInt(N)` would do the same, but GCC has it in C only and clang stops
/// at 128 bits on arm64 and WebAssembly, so the key is written out here.

#include <array>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>

namespace bt::sliding {

/// An unsigned integer of W 64-bit words, with the operations the level
/// search asks of a key: order, the bit operations, shifts, and the sums
/// and differences its sets keep the gaps between keys with. Plain data,
/// with no padding, so keys are written to disk as they are.
template <unsigned int W>
struct BigKey {
  /// The words, least significant first.
  std::array<std::uint64_t, W> w{};

  constexpr BigKey() = default;
  /// A small number, as a built-in integer takes one.
  // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions): a key is a number
  constexpr BigKey(std::uint64_t v) { w[0] = v; }

  /// The low bits, as a cast of a built-in integer gives them.
  template <std::integral T>
    requires(!std::same_as<T, bool>)
  constexpr explicit operator T() const {
    return static_cast<T>(w[0]);
  }
  constexpr explicit operator bool() const {
    for (const std::uint64_t word : w) {
      if (word != 0) {
        return true;
      }
    }
    return false;
  }

  friend constexpr bool operator==(const BigKey& a, const BigKey& b) = default;
  friend constexpr std::strong_ordering operator<=>(const BigKey& a, const BigKey& b) {
    for (unsigned int i = W; i-- > 0;) {
      if (a.w[i] != b.w[i]) {
        return a.w[i] < b.w[i] ? std::strong_ordering::less : std::strong_ordering::greater;
      }
    }
    return std::strong_ordering::equal;
  }

  friend constexpr BigKey operator&(const BigKey& a, const BigKey& b) {
    BigKey r;
    for (unsigned int i = 0; i < W; i++) {
      r.w[i] = a.w[i] & b.w[i];
    }
    return r;
  }
  friend constexpr BigKey operator|(const BigKey& a, const BigKey& b) {
    BigKey r;
    for (unsigned int i = 0; i < W; i++) {
      r.w[i] = a.w[i] | b.w[i];
    }
    return r;
  }
  friend constexpr BigKey operator^(const BigKey& a, const BigKey& b) {
    BigKey r;
    for (unsigned int i = 0; i < W; i++) {
      r.w[i] = a.w[i] ^ b.w[i];
    }
    return r;
  }
  constexpr BigKey operator~() const {
    BigKey r;
    for (unsigned int i = 0; i < W; i++) {
      r.w[i] = ~w[i];
    }
    return r;
  }

  /// Shifts by any count; one as wide as the key or wider gives 0.
  template <std::integral S>
  friend constexpr BigKey operator<<(const BigKey& a, S count) {
    const auto s = static_cast<std::size_t>(count);
    BigKey r;
    const std::size_t words = s / 64;
    const std::size_t bits = s % 64;
    for (std::size_t i = W; i-- > words;) {
      r.w[i] = a.w[i - words] << bits;
      if (bits != 0 && i > words) {
        r.w[i] |= a.w[i - words - 1] >> (64 - bits);
      }
    }
    return r;
  }
  template <std::integral S>
  friend constexpr BigKey operator>>(const BigKey& a, S count) {
    const auto s = static_cast<std::size_t>(count);
    BigKey r;
    const std::size_t words = s / 64;
    const std::size_t bits = s % 64;
    for (std::size_t i = 0; i + words < W; i++) {
      r.w[i] = a.w[i + words] >> bits;
      if (bits != 0 && i + words + 1 < W) {
        r.w[i] |= a.w[i + words + 1] << (64 - bits);
      }
    }
    return r;
  }

  /// Sums and differences wrap round, as a built-in integer's do.
  friend constexpr BigKey operator+(const BigKey& a, const BigKey& b) {
    BigKey r;
    std::uint64_t carry = 0;
    for (unsigned int i = 0; i < W; i++) {
      const std::uint64_t sum = a.w[i] + b.w[i];
      const std::uint64_t out = sum + carry;
      carry = (sum < a.w[i] ? 1U : 0U) + (out < sum ? 1U : 0U);
      r.w[i] = out;
    }
    return r;
  }
  friend constexpr BigKey operator-(const BigKey& a, const BigKey& b) {
    BigKey r;
    std::uint64_t borrow = 0;
    for (unsigned int i = 0; i < W; i++) {
      const std::uint64_t diff = a.w[i] - b.w[i];
      const std::uint64_t out = diff - borrow;
      borrow = (a.w[i] < b.w[i] ? 1U : 0U) + (diff < borrow ? 1U : 0U);
      r.w[i] = out;
    }
    return r;
  }

  constexpr BigKey& operator&=(const BigKey& b) { return *this = *this & b; }
  constexpr BigKey& operator|=(const BigKey& b) { return *this = *this | b; }
  constexpr BigKey& operator^=(const BigKey& b) { return *this = *this ^ b; }
  constexpr BigKey& operator+=(const BigKey& b) { return *this = *this + b; }
  constexpr BigKey& operator-=(const BigKey& b) { return *this = *this - b; }
  template <std::integral S>
  constexpr BigKey& operator<<=(S count) {
    return *this = *this << count;
  }
  template <std::integral S>
  constexpr BigKey& operator>>=(S count) {
    return *this = *this >> count;
  }

  /// The bits from `shift` on that `mask` keeps: a field of at most 64 bits,
  /// read from the one or two words it lies in.
  [[nodiscard]] constexpr std::uint64_t field(unsigned int shift, std::uint64_t mask) const {
    const unsigned int word = shift / 64;
    const unsigned int bits = shift % 64;
    std::uint64_t v = w[word] >> bits;
    if (bits != 0 && word + 1 < W) {
      v |= w[word + 1] << (64 - bits);
    }
    return v & mask;
  }

  /// The key with `value` or'd in from `shift` on.
  constexpr void orField(unsigned int shift, std::uint64_t value) {
    const unsigned int word = shift / 64;
    const unsigned int bits = shift % 64;
    w[word] |= value << bits;
    if (bits != 0 && word + 1 < W) {
      w[word + 1] |= value >> (64 - bits);
    }
  }

  /// The key with the bits from `shift` on that `mask` covers cleared.
  constexpr void clearField(unsigned int shift, std::uint64_t mask) {
    const unsigned int word = shift / 64;
    const unsigned int bits = shift % 64;
    w[word] &= ~(mask << bits);
    if (bits != 0 && word + 1 < W) {
      w[word + 1] &= ~(mask >> (64 - bits));
    }
  }
};

/// The field of `key` from `shift` on under `mask`, at most 64 bits wide.
template <class Key>
[[nodiscard]] constexpr std::uint64_t keyField(const Key& key, unsigned int shift,
                                               std::uint64_t mask) {
  return static_cast<std::uint64_t>(key >> shift) & mask;
}
template <unsigned int W>
[[nodiscard]] constexpr std::uint64_t keyField(const BigKey<W>& key, unsigned int shift,
                                               std::uint64_t mask) {
  return key.field(shift, mask);
}

/// A key that is `value` from `shift` on and 0 elsewhere.
template <class Key>
[[nodiscard]] constexpr Key keyAt(std::uint64_t value, unsigned int shift) {
  return static_cast<Key>(value) << shift;
}
template <class Key>
  requires requires(Key k) { k.orField(0U, std::uint64_t{0}); }
[[nodiscard]] constexpr Key keyAt(std::uint64_t value, unsigned int shift) {
  Key key;
  key.orField(shift, value);
  return key;
}

/// `key` with the field from `shift` on under `mask` cleared.
template <class Key>
[[nodiscard]] constexpr Key keyClear(Key key, unsigned int shift, std::uint64_t mask) {
  return key & ~(static_cast<Key>(mask) << shift);
}
template <unsigned int W>
[[nodiscard]] constexpr BigKey<W> keyClear(BigKey<W> key, unsigned int shift,
                                           std::uint64_t mask) {
  key.clearField(shift, mask);
  return key;
}

}  // namespace bt::sliding

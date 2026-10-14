// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Short names for the int <-> unsigned conversions the ported code is full
/// of. The original relied on implicit conversions; the build warns on those
/// (-Wsign-conversion), so ported code spells them out with these. They are
/// plain static_casts: same bits, same behaviour as the implicit conversion.

#include <cstddef>

namespace bt {

/// The value as `int` (what an implicit unsigned -> int conversion did).
[[nodiscard]] constexpr int asInt(unsigned int v) noexcept { return static_cast<int>(v); }
/// The value as `int`, from a size.
[[nodiscard]] constexpr int asInt(std::size_t v) noexcept { return static_cast<int>(v); }
/// The value as `unsigned int` (what an implicit int -> unsigned conversion did).
[[nodiscard]] constexpr unsigned int asUnsigned(int v) noexcept {
  return static_cast<unsigned int>(v);
}

}  // namespace bt

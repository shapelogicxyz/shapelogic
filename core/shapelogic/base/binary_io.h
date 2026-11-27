// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// A value's raw bytes to a stream and back: how the saved searches'
/// state files (the slide search's, the Panex search's) hold their numbers.
/// The bytes are the machine's own, so a state file is read by the build
/// that wrote it, or one of the same byte order.

#include <istream>
#include <ostream>

namespace bt::binary {

/// Writes the bytes of `v`.
template <class T>
void put(std::ostream& out, const T& v) {
  out.write(reinterpret_cast<const char*>(&v), sizeof(T));
}

/// Reads the bytes of `v`; the stream's state tells whether they were there.
template <class T>
void get(std::istream& in, T& v) {
  in.read(reinterpret_cast<char*>(&v), sizeof(T));
}

}  // namespace bt::binary

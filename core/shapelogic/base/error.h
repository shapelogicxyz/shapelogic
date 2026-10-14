// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Recoverable errors: a bad file, a request the current state does not
/// allow, a missing item. These are returned as Result<T>, never thrown.
/// (Broken invariants are a different thing: see assert.h.)

#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace bt {

enum class Errc {
  InvalidArgument,  ///< A parameter is out of range or malformed.
  NotFound,         ///< The referenced shape, problem, solution... does not exist.
  ParseError,       ///< A file or message could not be understood.
  IoError,          ///< Reading or writing a file failed.
  Unsupported,      ///< Valid, but not handled by this version (e.g. sliding puzzles for now).
  InvalidState,     ///< Not allowed right now (e.g. editing a problem while it is solving).
  Internal,         ///< An InternalError, reported across an API boundary.
};

[[nodiscard]] std::string_view toString(Errc e) noexcept;

struct Error {
  Errc code;
  std::string message;
};

template <class T>
using Result = std::expected<T, Error>;

/// Shorthand for `return std::unexpected(Error{...})`.
[[nodiscard]] inline std::unexpected<Error> fail(Errc code, std::string message) {
  return std::unexpected(Error{code, std::move(message)});
}

}  // namespace bt

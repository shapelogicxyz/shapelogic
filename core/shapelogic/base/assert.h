// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Invariant checks. BT_ASSERT is on in every build, release included: the
/// checks are cheap next to the searches, and a broken invariant must stop
/// the run with a report rather than produce wrong answers.
///
/// A failed check throws bt::InternalError. The solve session catches it and
/// moves the run to SolveState::Error; the engine process keeps running.
/// Never put an expression with side effects inside BT_ASSERT.

#include <stdexcept>
#include <string>

namespace bt {

class InternalError : public std::logic_error {
 public:
  InternalError(const std::string& what, const char* file, int line)
      : std::logic_error(what), file_(file), line_(line) {}
  [[nodiscard]] const char* file() const noexcept { return file_; }
  [[nodiscard]] int line() const noexcept { return line_; }

 private:
  const char* file_;
  int line_;
};

namespace detail {
[[noreturn]] void assertionFailed(const char* expr, const char* msg, const char* file, int line);
}  // namespace detail

}  // namespace bt

#define BT_ASSERT(cond) \
  ((cond) ? static_cast<void>(0) : ::bt::detail::assertionFailed(#cond, nullptr, __FILE__, __LINE__))

#define BT_ASSERT_MSG(cond, msg) \
  ((cond) ? static_cast<void>(0) : ::bt::detail::assertionFailed(#cond, (msg), __FILE__, __LINE__))

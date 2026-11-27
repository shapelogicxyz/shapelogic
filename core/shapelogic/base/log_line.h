// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// A line for the application's log: what a search or a solve tells about
/// itself so that a problem can be understood afterwards (a saved search
/// that could not be carried on, and why). The core never prints: it hands
/// these lines to a callback or keeps them for its caller to take, and the
/// caller writes them where it keeps its log.

#include <cstdint>
#include <string>
#include <string_view>

namespace sl {

/// How much a log line matters.
enum class LogLevel : std::uint8_t {
  Debug,    ///< how it went, step by step: each level, each save (a debug log's only)
  Info,     ///< what was done: a search carried on, stopped, saved
  Warning,  ///< a limit reached: the disk budget, the memory
  Error,    ///< something went wrong: a file damaged, a search started again
};

/// The level's name as the log writes it: DEBUG, INFO, WARN or ERROR.
[[nodiscard]] constexpr std::string_view logLevelName(LogLevel level) {
  switch (level) {
    case LogLevel::Debug:
      return "DEBUG";
    case LogLevel::Info:
      return "INFO";
    case LogLevel::Warning:
      return "WARN";
    case LogLevel::Error:
      return "ERROR";
  }
  return "INFO";
}

/// One line of the log, without its time (the writer adds it).
struct LogLine {
  LogLevel level = LogLevel::Info;
  std::string text;
};

}  // namespace sl

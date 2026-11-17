// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The engine's lines for the application's log (the setting `log`): a
/// line on the engine's log stream (stderr) for each thing that helps to
/// understand afterwards what happened: a file opened or saved, or not and
/// why; a solve started (its options), paused, continued, aborted, and how
/// its runs ended; the settings changed; what the slide searches told
/// (SolveSession::takeLog); and every call that failed. Each line is
/// `shapelogic_engine: LEVEL: text`, LEVEL one of DEBUG, INFO, WARN, ERROR,
/// on one line; the app adds the time and writes it to its log file. A
/// debug log (Settings::log Debug) also has the searches' DEBUG lines.

#include "engine.h"
#include "rpc.h"

#include "shapelogic/base/log_line.h"

#include <string>
#include <string_view>

namespace bt::engine {

/// Whether the log takes a line of this level (Settings::log: Debug's
/// only in a debug log).
[[nodiscard]] bool logs(const State& s, LogLevel level);

/// Writes a line when the log takes its level (Settings::log).
void logLine(State& s, LogLevel level, std::string_view text);

/// A problem as the log names it: its id and its name, `p2 "Board 1"`.
[[nodiscard]] std::string problemTitle(const State& s, app::Id problem);

/// The line of a call, when it is one the log tells or it failed.
void logCall(State& s, std::string_view method, const Json& params, const Reply& reply);

}  // namespace bt::engine

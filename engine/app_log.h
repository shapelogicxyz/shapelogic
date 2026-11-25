// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The engine's lines for the application's log (the setting `log`): a
/// line on the engine's log stream (stderr) for each thing that helps to
/// understand afterwards what happened: a file opened or saved, or not and
/// why; a solve started (its options), paused, continued, aborted, and how
/// its runs ended; the settings changed; what the slide searches told
/// (SolveSession::takeLog); and every call that failed. Each line is
/// `shapelogic_engine: LEVEL: Event: text`, LEVEL one of DEBUG, INFO,
/// WARN, ERROR, and Event what happened in a few capitalised words
/// (`File Opened`, `Solve Paused`), on one line; the app adds the time and
/// writes it to its log file, where its log manager shows the event in a
/// column of its own. A
/// debug log (Settings::log Debug) also has the searches' DEBUG lines.

#include "engine.h"
#include "rpc.h"

#include "shapelogic/base/log_line.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace bt::engine {

/// Whether the log takes a line of this level (Settings::log: Debug's
/// only in a debug log).
[[nodiscard]] bool logs(const State& s, LogLevel level);

/// Writes a line when the log takes its level (Settings::log): `event`
/// is what happened in a few capitalised words, letters and spaces only
/// (`Solve Started`), `text` the rest.
void logLine(State& s, LogLevel level, std::string_view event, std::string_view text);

/// A problem as the log names it: its label and its name, `P2 "Board 1"`
/// (the label alone for a problem without a name; the id, `p7`, for one
/// the document no longer has).
[[nodiscard]] std::string problemTitle(const State& s, app::Id problem);

/// The line of a call, when it is one the log tells or it failed.
void logCall(State& s, std::string_view method, const Json& params, const Reply& reply);

/// The engine's first line on its log stream, written at every start
/// whatever the `log` setting (the app logs it, cleanup item 2.6):
/// `shapelogic_engine: INFO: Engine Started: version 2.1.0, release build,
/// vectors AVX2, open files 4096`. `openFiles` is what raiseOpenFileLimit
/// got (0: unknown).
[[nodiscard]] std::string startLine(std::uint64_t openFiles);

/// What a search folder is like, for the log: `"<folder>" (exists,
/// writable)`, `(not made yet)`, `(not writable)`; `none` for no folder.
/// Writable is tried with a small file made and removed at once.
[[nodiscard]] std::string folderText(const std::string& folder);

}  // namespace bt::engine

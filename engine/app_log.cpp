// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_log.h"

#include "shapelogic/model/problem.h"

#include <optional>
#include <string>

namespace bt::engine {

namespace {

// The calls that fail as part of the work (a file that cannot be opened, a
// solve that cannot start): errors; any other failed call is a warning.
bool errorWhenFailed(std::string_view method) {
  return method.starts_with("document.") || method.starts_with("solve.") ||
         method.starts_with("settings.");
}

std::string stringParam(const Json& params, const char* name) {
  if (params.is_object() && params.contains(name) && params[name].is_string()) {
    return params[name].get<std::string>();
  }
  return {};
}

// The problem a call names, as the log names it; empty when it names none.
std::string problemOf(const State& s, const Json& params) {
  const std::string text = stringParam(params, "problem");
  if (text.empty()) {
    return {};
  }
  const std::optional<std::uint64_t> id = parseId(IdKind::Problem, text);
  return id ? problemTitle(s, *id) : text;
}

// What a solve runs with besides its options: the engine's settings.
std::string settingsText(const State& s) {
  return "threads " + std::to_string(s.settings.threads) + ", search memory " +
         std::to_string(s.settings.searchMemoryGB) + " GB, sliding disk " +
         std::to_string(s.settings.slideDiskGB) + " GB in \"" + s.settings.slideFolder +
         "\", stacking disk " + std::to_string(s.settings.panexDiskGB) + " GB in \"" +
         s.settings.panexFolder + "\" (0: automatic)";
}

// The file a document call names, or the document's own.
std::string fileOf(const State& s, const Json& params) {
  std::string path = stringParam(params, "path");
  if (path.empty() && s.path) {
    path = s.path->string();
  }
  return path.empty() ? std::string("an untitled puzzle") : "\"" + path + "\"";
}

}  // namespace

bool logs(const State& s, LogLevel level) {
  return s.settings.log == LogSetting::Debug ||
         (s.settings.log == LogSetting::Info && level != LogLevel::Debug);
}

void logLine(State& s, LogLevel level, std::string_view text) {
  if (!logs(s, level)) {
    return;
  }
  std::string line(text);
  // one line each: the app reads them line by line
  for (char& c : line) {
    if (c == '\n' || c == '\r') {
      c = ' ';
    }
  }
  s.log << "shapelogic_engine: " << logLevelName(level) << ": " << line << '\n';
  s.log.flush();
}

std::string problemTitle(const State& s, app::Id problem) {
  std::string title = formatId(IdKind::Problem, problem);
  const app::Document& d = s.document();
  if (const Result<unsigned int> index = d.problemIndex(problem)) {
    const std::string name =
        d.readProblem(*index, [](const Problem& pr) { return pr.getName(); });
    title += " \"" + (name.empty() ? app::Document::problemLabel(*index) : name) + "\"";
  }
  return title;
}

void logCall(State& s, std::string_view method, const Json& params, const Reply& reply) {
  if (s.settings.log == LogSetting::Off) {
    return;
  }
  const std::string what(method);
  if (!reply) {
    const RpcError& e = reply.error();
    std::string text = what + " failed (" + e.errc + "): " + e.message;
    if (method == "document.open" || method == "document.save") {
      text = (method == "document.open" ? "Could not open " : "Could not save ") +
             fileOf(s, params) + " (" + e.errc + "): " + e.message;
    } else if (const std::string problem = problemOf(s, params); !problem.empty()) {
      text += " [" + problem + "]";
    }
    logLine(s, errorWhenFailed(method) ? LogLevel::Error : LogLevel::Warning, text);
    return;
  }
  const Json given = params.is_object() && params.contains("options") ? params["options"] : Json();
  if (method == "document.open") {
    logLine(s, LogLevel::Info, "Opened " + fileOf(s, params));
  } else if (method == "document.openBytes") {
    const std::string path = stringParam(params, "path");
    logLine(s, LogLevel::Info,
            "Opened a puzzle from its bytes (a crash recovery copy, or a file read by the "
            "app)" +
                (path.empty() ? std::string() : ", as \"" + path + "\""));
  } else if (method == "document.new") {
    logLine(s, LogLevel::Info, "New puzzle on the " + stringParam(params, "grid") + " grid");
  } else if (method == "document.import") {
    logLine(s, LogLevel::Info, "Imported \"" + stringParam(params, "path") + "\"");
  } else if (method == "document.save") {
    logLine(s, LogLevel::Info, "Saved " + fileOf(s, params));
  } else if (method == "solve.start" || method == "solve.continue" || method == "solve.prepare") {
    const char* verb = method == "solve.start"      ? "Solve started"
                       : method == "solve.continue" ? "Solve continued"
                                                    : "Solve prepared";
    logLine(s, LogLevel::Info,
            std::string(verb) + ": " + problemOf(s, params) + " of " + fileOf(s, Json()) +
                ", options " + (given.is_null() ? std::string("{}") : given.dump()) + "; " +
                settingsText(s));
  } else if (method == "solve.pause") {
    logLine(s, LogLevel::Info, "Pause asked for: " + problemOf(s, params));
  } else if (method == "solve.savePoint") {
    logLine(s, LogLevel::Info,
            "Pause at a save point asked for (the crash recovery copy, or File > Save): " +
                problemOf(s, params));
  } else if (method == "solve.abort") {
    logLine(s, LogLevel::Info, "Solve aborted: " + problemOf(s, params));
  } else if (method == "solve.reset") {
    logLine(s, LogLevel::Info, "Solve results thrown away: " + problemOf(s, params));
  } else if (method == "solve.importPaused") {
    logLine(s, LogLevel::Info,
            "Opened the paused solve \"" + stringParam(params, "path") + "\"");
  } else if (method == "solve.exportPaused") {
    logLine(s, LogLevel::Info, "Saved the paused solve of " + problemOf(s, params) + " as \"" +
                                   stringParam(params, "path") + "\"");
  } else if (method == "settings.set") {
    logLine(s, LogLevel::Info, "Settings changed: " + params.dump());
  }
}

}  // namespace bt::engine

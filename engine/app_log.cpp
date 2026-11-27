// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_log.h"

#include "convert.h"

#include "shapelogic/app/queries.h"
#include "shapelogic/base/system.h"
#include "shapelogic/base/thread_pool.h"
#include "shapelogic/base/version.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/solve/simd_bitset.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace sl::engine {

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

// A size in whole GB, as the settings' sliders give it.
std::string gbText(double gb) { return std::to_string(std::llround(gb)) + " GB"; }

// The disk a sliding or stacking search may take in its folder (0 GB:
// automatic, diskBudgetFor), or what it does without a folder.
std::string diskText(const char* what, double gb, const std::string& folder,
                     const char* noFolder) {
  if (folder.empty()) {
    return std::string(what) + " " + noFolder;
  }
  const std::string size =
      gb > 0 ? gbText(gb)
             : "automatic (half the free space, at most " +
                   gbText(static_cast<double>(kAutoDiskBudgetMax) / 1e9) + ")";
  return std::string(what) + " disk " + size + " in \"" + folder + "\"";
}

// Which solver a solve runs, and what it runs with: the threads, and for
// the searches that keep arrangements, their memory and disk (the
// engine's settings, "automatic" spelled out).
std::string solverText(const State& s, const SolveOptions& o) {
  const unsigned int threads = o.threads > 0 ? std::min(o.threads, kMaxThreads) : 0;
  const std::string threadText =
      threads > 0 ? "threads " + std::to_string(threads)
                  : "threads " + std::to_string(hardwareThreads()) +
                        " (automatic: every hardware thread)";
  const std::string memory =
      "search memory " + gbText(static_cast<double>(s.settings.searchMemoryBytes()) / 1e9) +
      (s.settings.searchMemoryGB > 0 ? "" : " (automatic: half the machine's memory)");
  switch (app::problemKind(s.document().puzzle())) {
    case app::ProblemKind::Sliding:
      return "solver: the slide search, in the narrowest key that holds a start layout's "
             "arrangements (64, 128, 192, 256 or 512 bits, else text keys; a Search Note "
             "names it for each start); " +
             threadText + ", " + memory + ", " +
             diskText("sliding", s.settings.slideDiskGB, s.settings.slideFolder,
                      "levels kept in memory (no folder)");
    case app::ProblemKind::Stacking:
      return std::string("solver: the ") +
             (o.stacking.solver == stacking::StackingSolver::Panex ? "Panex Solver"
                                                                   : "Stacking Solver") +
             "; " + threadText + ", " + memory + ", " +
             diskText("stacking", s.settings.panexDiskGB, s.settings.panexFolder,
                      "search not saved (no folder)");
    case app::ProblemKind::Brick:
      break;
  }
  return "solver: " + std::string(solverTypeLabel(o.solver)) + "; " + threadText;
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

void logLine(State& s, LogLevel level, std::string_view event, std::string_view text) {
  if (!logs(s, level)) {
    return;
  }
  std::string line = std::string(event) + ": " + std::string(text);
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
  // the label the puzzle shows (P1, P2…), not the engine's id: the log is
  // read beside the puzzle
  const app::Document& d = s.document();
  const Result<unsigned int> index = d.problemIndex(problem);
  if (!index) {
    return formatId(IdKind::Problem, problem);
  }
  std::string title = app::Document::problemLabel(*index);
  const std::string name = d.readProblem(*index, [](const Problem& pr) { return pr.getName(); });
  if (!name.empty()) {
    title += " \"" + name + "\"";
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
    const LogLevel level = errorWhenFailed(method) ? LogLevel::Error : LogLevel::Warning;
    if (method == "document.open" || method == "document.save") {
      logLine(s, level, method == "document.open" ? "File Open Failed" : "File Save Failed",
              fileOf(s, params) + " (" + e.errc + "): " + e.message);
      return;
    }
    std::string text = what + " (" + e.errc + "): " + e.message;
    if (const std::string problem = problemOf(s, params); !problem.empty()) {
      text += " [" + problem + "]";
    }
    logLine(s, level, "Request Failed", text);
    return;
  }
  const Json given = params.is_object() && params.contains("options") ? params["options"] : Json();
  if (method == "document.open") {
    std::string text = "\"" + stringParam(params, "path") + "\"";
    if (params.is_object() && params.contains("as")) {
      // a rescue copy reopened (cleanup item 10.11): the file it stands for
      text += s.path ? ", as \"" + s.path->string() + "\"" : std::string(", as an untitled puzzle");
    }
    logLine(s, LogLevel::Info, "File Opened", text);
  } else if (method == "document.openBytes") {
    const std::string path = stringParam(params, "path");
    logLine(s, LogLevel::Info, "File Opened",
            "from its bytes (a crash recovery copy, or a file read by the app)" +
                (path.empty() ? std::string() : ", as \"" + path + "\""));
  } else if (method == "document.new") {
    logLine(s, LogLevel::Info, "Puzzle Created", "on the " + stringParam(params, "grid") + " grid");
  } else if (method == "document.import") {
    logLine(s, LogLevel::Info, "File Imported", "\"" + stringParam(params, "path") + "\"");
  } else if (method == "document.save") {
    logLine(s, LogLevel::Info, "File Saved", fileOf(s, params));
  } else if (method == "solve.start" || method == "solve.continue" || method == "solve.prepare") {
    const char* event = method == "solve.start"      ? "Solve Started"
                        : method == "solve.continue" ? "Solve Continued"
                                                     : "Solve Prepared";
    // the options the solve was launched with (a Continue without any
    // goes on with the paused solve's, not the defaults)
    const std::optional<SolveOptions> options = std::exchange(s.launchedWith, std::nullopt);
    logLine(s, LogLevel::Info, event,
            problemOf(s, params) + " of " + fileOf(s, Json()) + ", options " +
                (given.is_null() ? std::string("{}") : given.dump()) +
                (options ? "; " + solverText(s, *options) : std::string()));
  } else if (method == "solve.pause") {
    logLine(s, LogLevel::Info, "Pause Requested", problemOf(s, params));
  } else if (method == "solve.savePoint") {
    logLine(s, LogLevel::Info, "Save Point Requested",
            problemOf(s, params) + " (the crash recovery copy, or File > Save)");
  } else if (method == "solve.abort") {
    logLine(s, LogLevel::Info, "Solve Aborted", problemOf(s, params));
  } else if (method == "solve.reset") {
    logLine(s, LogLevel::Info, "Results Removed", problemOf(s, params));
  } else if (method == "solve.importPaused") {
    logLine(s, LogLevel::Info, "Paused Solve Opened", "\"" + stringParam(params, "path") + "\"");
  } else if (method == "solve.exportPaused") {
    logLine(s, LogLevel::Info, "Paused Solve Saved",
            problemOf(s, params) + " as \"" + stringParam(params, "path") + "\"");
  } else if (method == "settings.set") {
    logLine(s, LogLevel::Info, "Settings Changed", params.dump());
    if (params.is_object() && (params.contains("slideFolder") || params.contains("panexFolder")) &&
        logs(s, LogLevel::Info)) {
      logLine(s, LogLevel::Info, "Engine Folders",
              "sliding " + folderText(s.settings.slideFolder) + ", Panex " +
                  folderText(s.settings.panexFolder));
    }
  }
}

std::string startLine(std::uint64_t openFiles) {
#if defined(NDEBUG)
  const char* build = "release build";
#else
  const char* build = "debug build";
#endif
  // the vector paths the covers choose (each behind runtime detection)
#if defined(SL_SIMD_X86)
  const char* vectors = cpuHasAvx512() ? "AVX-512" : cpuHasAvx2() ? "AVX2" : "scalar (no AVX2)";
#elif defined(SL_SIMD_NEON)
  const char* vectors = "NEON";
#else
  const char* vectors = "scalar";
#endif
  const std::string files = openFiles == UINT64_MAX ? "unlimited"
                            : openFiles == 0        ? "unknown"
                                                    : std::to_string(openFiles);
  return "shapelogic_engine: " + std::string(logLevelName(LogLevel::Info)) +
         ": Engine Started: version " + std::string(versionString()) + ", " + build + ", vectors " +
         vectors + ", open files " + files;
}

std::string folderText(const std::string& folder) {
  if (folder.empty()) {
    return "none";
  }
  namespace fs = std::filesystem;
  std::error_code ec;
  // as the searches make their paths from the setting (sliding_search.cpp)
  const fs::path dir(folder);
  const std::string quoted = "\"" + folder + "\"";
  if (!fs::is_directory(dir, ec)) {
    return quoted + (fs::exists(dir, ec) ? " (not a folder)" : " (not made yet)");
  }
  const fs::path probe = dir / ".shapelogic-write-probe";
  bool writable = false;
  {
    std::ofstream out(probe, std::ios::binary | std::ios::trunc);
    writable = out.good() && static_cast<bool>(out << 'x');
  }
  fs::remove(probe, ec);
  return quoted + (writable ? " (exists, writable)" : " (exists, not writable)");
}

}  // namespace sl::engine

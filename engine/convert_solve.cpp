// SPDX-License-Identifier: GPL-3.0-or-later
// The JSON of solving (convert.h; cleanup item 21.4 moved it here from
// convert.cpp): the stored solver settings, the solve options a call
// gives, and the progress, statistics and failures a solve tells.
#include "convert.h"

#include "shapelogic/base/system.h"
#include "shapelogic/base/thread_pool.h"
#include "shapelogic/model/sliding.h"
#include "shapelogic/model/stacking.h"
#include "shapelogic/solve/slide_forecast.h"

#include <array>
#include <charconv>
#include <cstdio>

namespace bt::engine {

// ---- the stored solver settings ---------------------------------------------------------

// The original's check boxes (mainwindow_solve.cpp's optionNames), and
// verticalMoves, which sliding trays of several layers added,
// anyOrientation and mirroredOrientations (a sliding piece's orientation),
// keepAssemblies (Always show Assemblies), optimizedPaths (Automatically
// use optimized paths) and flipPieces (a tilted tray's pieces may be
// turned over).
const std::vector<std::string_view>& solverBoxNames() {
  static const std::vector<std::string_view> n = {
      "disassemble",   "checkRotations", "justCount",      "justLevels",
      "deepSymmetry",  "keepMirrors",    "keepRotations",  "strictColors",
      "nestedSlides",  "partialNested",  "verticalMoves",  "anyOrientation",
      "mirroredOrientations",            "highMemory",     "autosave",
      "keepAssemblies", "optimizedPaths", "flipPieces"};
  return n;
}

namespace {

bool isBox(std::string_view name) {
  for (const std::string_view b : solverBoxNames()) {
    if (b == name) {
      return true;
    }
  }
  return false;
}

}  // namespace

Json solverSettingsJson(const std::map<std::string, std::string>& o) {
  Json j = Json::object();
  for (const auto& [k, v] : o) {
    if (isBox(k)) {
      j[k] = v == "1";
    } else {
      j[k] = v;
    }
  }
  return j;
}

std::optional<std::map<std::string, std::string>> solverSettingsFrom(const Json& j,
                                                                    std::string& error) {
  if (!j.is_object()) {
    error = "'settings' must be an object";
    return std::nullopt;
  }
  std::map<std::string, std::string> o;
  for (const auto& [k, v] : j.items()) {
    if (isBox(k)) {
      if (!v.is_boolean()) {
        error = "'settings." + k + "' must be true or false";
        return std::nullopt;
      }
      o[k] = v.get<bool>() ? "1" : "0";
    } else {
      if (!v.is_string()) {
        error = "'settings." + k + "' must be a string";
        return std::nullopt;
      }
      o[k] = v.get<std::string>();
    }
  }
  return o;
}

// ---- solving ----------------------------------------------------------------------------

void searchSettingsInto(const State& s, SolveOptions& o) {
  // where a sliding search keeps its levels and a Panex search is
  // saved: the engine's settings
  o.sliding.workDir = s.settings.slideFolder;
  // (the settings' ranges keep them budgets)
  o.sliding.diskBudget = gbToBytes(s.settings.slideDiskGB).value_or(0);
  o.stacking.workDir = s.settings.panexFolder;
  o.stacking.diskBudget = gbToBytes(s.settings.panexDiskGB).value_or(0);
  // the memory of both: a share of what the searches running at once
  // share (decision 0028); memoryBytes is the whole of it, for a search
  // that runs without the share
  const std::uint64_t memory = s.settings.searchMemoryBytes();
  o.sliding.memoryBytes = memory;
  o.stacking.memoryBytes = memory;
  o.memoryShare = s.memory;
  o.settingsPlace = "Settings ▸ Solver";
}

RpcResult<SolveOptions> solveOptionsFrom(const State& s, const Json& j) {
  SolveOptions o;
  o.solver = s.settings.solver;
  o.threads = s.settings.threads;
  searchSettingsInto(s, o);
  if (j.is_null()) {
    return o;
  }
  if (!j.is_object()) {
    return std::unexpected(invalidParams("'options' must be an object"));
  }
  Params p(j);
  o.solver = p.optChoice("solver", solverNames()).value_or(o.solver);
  o.disassemble = p.optBoolean("disassemble", o.disassemble);
  o.checkRotations = p.optBoolean("checkRotations", o.checkRotations);
  o.justCount = p.optBoolean("justCount", o.justCount);
  o.keepAssemblies = p.optBoolean("keepAssemblies", o.keepAssemblies);
  o.flipPieces = p.optBoolean("flipPieces", o.flipPieces);
  o.dropDisassemblies = p.optBoolean("justLevels", o.dropDisassemblies);
  o.matrix.keepMirrors = p.optBoolean("keepMirrors", o.matrix.keepMirrors);
  o.matrix.keepRotations = p.optBoolean("keepRotations", o.matrix.keepRotations);
  o.matrix.completeRotations = p.optBoolean("completeRotations", o.matrix.completeRotations);
  o.matrix.strictColors = p.optBoolean("strictColors", o.matrix.strictColors);
  o.reduce = p.optBoolean("reduce", o.reduce);
  o.order = p.optChoice("sortBy", sortByNames()).value_or(o.order);
  o.solutionLimit = static_cast<unsigned int>(p.optInteger("keepAtMost", o.solutionLimit, 0, 0xFFFFFFFF));
  o.solutionDrop = static_cast<unsigned int>(p.optInteger("keepEach", o.solutionDrop, 0, 0xFFFFFFFF));
  o.threads = static_cast<unsigned int>(p.optInteger("threads", o.threads, 0, kMaxThreads));
  o.takeApartWorkers = static_cast<unsigned int>(p.optInteger("takeApartWorkers", 0, 0, kMaxTakeApartWorkers));
  if (p.has("sliding")) {
    const Json& sj = p.value("sliding");
    if (!sj.is_object()) {
      p.fail("'sliding' must be an object");
    } else {
      Params sp(sj);
      o.sliding.nested = sp.optBoolean("nestedSlides", o.sliding.nested);
      o.sliding.partialNested = sp.optBoolean("partialNested", o.sliding.partialNested);
      o.sliding.verticalMoves = sp.optBoolean("verticalMoves", o.sliding.verticalMoves);
      o.sliding.anyOrientation = sp.optBoolean("anyOrientation", o.sliding.anyOrientation);
      o.sliding.mirroredOrientations =
          sp.optBoolean("mirroredOrientations", o.sliding.mirroredOrientations);
      o.sliding.highMemory = sp.optBoolean("highMemory", o.sliding.highMemory);
      o.sliding.optimizedPaths = sp.optBoolean("optimizedPaths", o.sliding.optimizedPaths);
      if (RpcResult<void> r = sp.finish(); !r) {
        RpcError e = r.error();
        e.message = "options: sliding: " + e.message;
        return std::unexpected(e);
      }
    }
  }
  if (p.has("stacking")) {
    const Json& sj = p.value("stacking");
    if (!sj.is_object()) {
      p.fail("'stacking' must be an object");
    } else {
      Params sp(sj);
      o.stacking.solver =
          sp.optChoice("solver", stackingSolverNames()).value_or(o.stacking.solver);
      o.stacking.highMemory = sp.optBoolean("highMemory", o.stacking.highMemory);
      o.stacking.autosave = sp.optBoolean("autosave", o.stacking.autosave);
      if (RpcResult<void> r = sp.finish(); !r) {
        RpcError e = r.error();
        e.message = "options: stacking: " + e.message;
        return std::unexpected(e);
      }
    }
  }
  if (RpcResult<void> r = p.finish(); !r) {
    RpcError e = r.error();
    e.message = "options: " + e.message;
    return std::unexpected(e);
  }
  return o;
}

void defaultStackingSolver(const State& s, app::Id problem, const Json& given,
                           SolveOptions& options) {
  const Json* named = nullptr;
  if (given.is_object() && given.contains("stacking") && given["stacking"].is_object() &&
      given["stacking"].contains("solver")) {
    named = &given["stacking"]["solver"];
  }
  const app::Document& d = s.document();
  if (named != nullptr || !stacking::isStacking(d.puzzle())) {
    return;
  }
  const unsigned int index = d.problemIndex(problem).value();
  d.readProblem(index, [&d, &options](const Problem& pr) {
    if (pr.rodSetValid(d.puzzle()) && d.puzzle().getRodSet(pr.getRodSetId()).panexColumns) {
      options.stacking.solver = stacking::StackingSolver::Panex;
    }
  });
}

Json takeApartJson(const DisassemblyProgress& d) {
  Json j = Json::object();
  j["elapsedMs"] = d.elapsedMs;
  j["pieces"] = d.pieces;
  j["searchPieces"] = d.searchPieces;
  j["separations"] = d.separations;
  j["depth"] = d.depth;
  j["level"] = d.level;
  j["levelDone"] = d.levelDone;
  j["levelSize"] = d.levelSize;
  j["nextLevelSize"] = d.nextLevelSize;
  j["nodes"] = d.nodes;
  j["threads"] = d.threads;
  return j;
}

Json slideForecastJson(const std::vector<std::uint64_t>& levelMs, std::uint64_t runningMs) {
  if (levelMs.empty()) {
    return nullptr;
  }
  const sliding::SlideForecast f = sliding::slideForecast(levelMs, runningMs);
  Json estimates = Json::array();
  for (const sliding::SlideEstimate& e : f.estimates) {
    estimates.push_back(Json{{"depth", e.depth}, {"levelMs", e.levelMs}, {"fromNowMs", e.fromNowMs}});
  }
  // a move not timed (searched before the times were kept) is null
  Json times = Json::array();
  for (const std::uint64_t ms : f.levelMs) {
    times.push_back(ms == kUntimedMs ? Json(nullptr) : Json(ms));
  }
  return Json{{"depth", f.depth},
              {"levelMs", std::move(times)},
              {"runningMs", f.runningMs},
              {"growth", f.growth > 0 ? Json(f.growth) : Json(nullptr)},
              {"estimates", std::move(estimates)}};
}

namespace {

// The part of a move the slide search is in that visits no arrangement:
// null while it expands one (SolveProgress::slidePhase).
Json slidePhaseJson(const SolveProgress& g) {
  const char* part = nullptr;
  switch (g.slidePhase) {
    case sliding::SlidePhase::None:
      return nullptr;
    case sliding::SlidePhase::Visited:
      part = "visited";
      break;
    case sliding::SlidePhase::Merging:
      part = "merging";
      break;
    case sliding::SlidePhase::Writing:
      part = "writing";
      break;
    case sliding::SlidePhase::Saving:
      part = "saving";
      break;
    case sliding::SlidePhase::Goal:
      part = "goal";
      break;
  }
  return Json{{"part", part},
              {"done", g.slidePhaseDone},
              {"total", g.slidePhaseTotal},
              {"ms", g.slidePhaseMs}};
}

}  // namespace

Json prepareJson(const PrepareProgress& r) {
  return Json{{"pass", r.pass},
              {"done", r.done},
              {"total", r.total},
              {"placements", r.placements},
              {"removed", r.removed},
              {"passMs", r.passMs},
              {"passSecondsLeft", r.passSecondsLeft < 0 ? Json(nullptr) : Json(r.passSecondsLeft)}};
}

Json progressJson(const SolveProgress& g) {
  Json j = Json::object();
  j["stage"] = solveStageName(g.stage);
  j["elapsedMs"] = g.elapsedMs;
  j["usedMs"] = g.usedMs;
  j["piece"] = g.piece;
  j["pieces"] = g.pieces;
  j["prepare"] = g.stage != SolveStage::Prepare && g.stage != SolveStage::Reduce
                     ? Json(nullptr)
                     : prepareJson(g.prepare);
  j["assemblyFraction"] = g.assemblyFraction;
  j["iterations"] = g.iterations;
  j["assemblerThreads"] = g.assemblerThreads;
  j["assemblies"] = g.assemblies;
  j["solutions"] = g.solutions;
  j["disassembly"] = g.disassembly;
  j["disasmCompleted"] = g.disasmCompleted;
  j["disasmPending"] = g.disasmPending;
  j["disasmWorkers"] = g.disasmWorkers;
  j["overall"] = g.overall;
  j["overallKnown"] = g.overallKnown;
  j["levelFraction"] = g.levelFraction;
  j["secondsLeft"] = g.secondsLeft < 0 ? Json(nullptr) : Json(g.secondsLeft);
  j["activity"] = g.activity();
  j["searched"] = g.searched;
  j["searchDepth"] = g.searchDepth;
  j["searchTraced"] = g.searchTraced;
  j["panexSearch"] = g.panexSearch;
  j["stopKind"] = g.stage != SolveStage::Stopping ? Json(nullptr)
                  : g.pausing                     ? Json("pause")
                                                  : Json("abort");
  j["slideStop"] = g.slideStopping ? Json{{"saving", g.stopSaving},
                                          {"doneBytes", g.stopBytes},
                                          {"totalBytes", g.stopTotalBytes}}
                                   : Json(nullptr);
  j["atSavePoint"] = g.atSavePoint;
  j["slideForecast"] = slideForecastJson(g.slideLevelMs, g.slideLevelRunningMs);
  j["slidePickingUp"] = g.slidePickingUp;
  j["slidePickingUpMs"] = g.slidePickingUpMs;
  j["slidePhase"] = slidePhaseJson(g);
  return j;
}

Json statsJson(const SolveStats& t) {
  Json running = Json::array();
  for (const DisassemblyProgress& d : t.running) {
    running.push_back(takeApartJson(d));
  }
  Json j = Json::object();
  j["state"] = solveStateName(t.state);
  j["solver"] = nameOf(solverNames(), t.solver);
  j["disassemblyEnabled"] = t.disassemblyEnabled;
  j["rotationsEnabled"] = t.rotationsEnabled;
  j["hardwareThreads"] = t.hardwareThreads;
  j["assemblerThreads"] = t.assemblerThreads;
  j["disasmWorkers"] = t.disasmWorkers;
  j["assembliesFound"] = t.assembliesFound;
  j["solutionsFound"] = t.solutionsFound;
  j["inseparable"] = t.inseparable;
  j["iterations"] = t.iterations;
  j["assemblyProgress"] = t.assemblyProgress;
  j["pending"] = t.pending;
  j["peakPending"] = t.peakPending;
  j["disasmCompleted"] = t.disasmCompleted;
  j["elapsedMs"] = t.elapsedMs;
  j["prepareMs"] = t.prepareMs;
  j["reduceMs"] = t.reduceMs;
  j["assemblyMs"] = t.assemblyMs;
  j["disasmWorkMs"] = t.disasmWorkMs;
  j["linearSearchMs"] = t.linearSearchMs;
  j["rotationSearchMs"] = t.rotationSearchMs;
  j["drainMs"] = t.drainMs;
  j["avgDisasmSeconds"] = t.avgDisasmSeconds;
  j["running"] = std::move(running);
  return j;
}

Json failureJson(const SolveFailure& f) {
  Json assembler = nullptr;
  switch (f.assembler) {
    case AssemblerError::None: break;
    case AssemblerError::TooManyUnits: assembler = "tooManyUnits"; break;
    case AssemblerError::TooFewUnits: assembler = "tooFewUnits"; break;
    case AssemblerError::CannotPlace: assembler = "cannotPlace"; break;
    case AssemblerError::CannotRestoreVersion: assembler = "cannotRestoreVersion"; break;
    case AssemblerError::CannotRestoreSyntax: assembler = "cannotRestoreSyntax"; break;
    case AssemblerError::CannotRestoreInterrupted: assembler = "cannotRestoreInterrupted"; break;
    case AssemblerError::PuzzleUnhandable: assembler = "puzzleUnhandable"; break;
    // a stopped build is the run's stop, never a failure (prepareSearch)
    case AssemblerError::Stopped: break;
  }
  Json j = Json::object();
  j["assembler"] = std::move(assembler);
  j["param"] = f.param;
  j["message"] = f.message;
  j["file"] = f.file;
  j["line"] = f.line;
  return j;
}

}  // namespace bt::engine

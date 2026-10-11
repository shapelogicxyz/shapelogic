// SPDX-License-Identifier: GPL-3.0-or-later
// solve.* (api/solve.json).
#include "convert.h"
#include "engine.h"

#include "bt/model/stacking.h"

#include <optional>

namespace bt::engine {

namespace {

// The stacking solver a request did not name: the one the original's menu
// offered for the problem's board (syncSolverTypeMenu), the Panex Solver
// for Panex columns, else the Stacking Solver.
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

enum class Launch : std::uint8_t { Start, Prepare, Continue };

Reply launch(State& s, Params& p, Launch how) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const Json given = p.has("options") ? p.value("options") : Json(nullptr);
  BTE_CHECK(p.finish());
  BTE_TRY(SolveOptions options, solveOptionsFrom(s, given));
  app::Document& d = s.document();
  BTE_CHECK(d.problemIndex(problem));
  defaultStackingSolver(s, problem, given, options);
  BTE_CHECK(how == Launch::Start     ? d.solveStart(problem, options)
            : how == Launch::Prepare ? d.solvePrepare(problem, options)
                                     : d.solveResume(problem, options));
  s.watches[problem].launched = true;
  return s.changed();
}

Reply solveStart(State& s, Params& p) { return launch(s, p, Launch::Start); }
Reply solvePrepare(State& s, Params& p) { return launch(s, p, Launch::Prepare); }
Reply solveContinue(State& s, Params& p) { return launch(s, p, Launch::Continue); }

// A pause is for a run under way (the original's Pause button).
RpcResult<void> running(const State& s, app::Id problem) {
  BTE_CHECK(s.document().problemIndex(problem));
  if (!isActive(s.document().solveState(problem))) {
    return std::unexpected(toRpc(Error{Errc::InvalidState, "the problem is not being solved"}));
  }
  return {};
}

Reply solvePause(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  BTE_CHECK(p.finish());
  BTE_CHECK(running(s, problem));
  BTE_CHECK(s.document().solvePause(problem));
  return Json::object();
}

Reply solveSavePoint(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  BTE_CHECK(p.finish());
  BTE_CHECK(running(s, problem));
  // the original never autosaved a stacking solve: its search saves itself
  // (every 20 minutes, and when it stops), and has no point to pause at
  // for the puzzle's save
  if (stacking::isStacking(s.document().puzzle())) {
    return coreError(Errc::Unsupported,
                     "a stacking search saves itself: it has no save point (pause it instead)");
  }
  BTE_CHECK(s.document().solvePauseAtSavePoint(problem));
  return Json::object();
}

Reply solveAbort(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().solveAbort(problem, s.settings.panexFolder));
  return s.changed();
}

Reply solveStatus(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  BTE_CHECK(p.finish());
  app::Document& d = s.document();
  BTE_TRY(const SolveProgress progress, d.solveProgress(problem));
  BTE_TRY(const SolveStats stats, d.solveStats(problem));
  const std::optional<SolveFailure> failure = d.solveFailure(problem);
  const std::string note = d.solveNote(problem);
  BTE_TRY(const std::optional<app::SavedSearchInfo> saved,
          d.savedSearch(problem, s.settings.panexFolder));
  Json savedJson = nullptr;
  if (saved) {
    savedJson = Json{{"description", saved->description},
                     {"ms", saved->ms},
                     {"levelBytes", saved->levelBytes}};
  }
  return Json{{"state", solveStateName(d.solveState(problem))},
              {"progress", progressJson(progress)},
              {"stats", statsJson(stats)},
              {"failure", failure ? failureJson(*failure) : Json(nullptr)},
              {"note", note.empty() ? Json(nullptr) : Json(note)},
              {"savedSearch", std::move(savedJson)}};
}

Reply solveExportPaused(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const std::string path = p.string("path");
  const bool withLevels = p.optBoolean("withLevels", true);
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().exportPaused(problem, path,
                                      app::PausedExport{s.settings.panexFolder, withLevels}));
  return Json::object();
}

Reply solveImportPaused(State& s, Params& p) {
  const std::string path = p.string("path");
  BTE_CHECK(p.finish());
  // A stacking search the file carries is unpacked into the Panex folder,
  // its folder emptied first, before the open document goes; a stacking
  // search of that document still running would write the same folder as
  // it stops, over the imported one (the original refused an import while
  // it solved: threadStopped)
  if (stacking::isStacking(s.document().puzzle()) && s.document().solving()) {
    return coreError(Errc::InvalidState,
                     "a stacking puzzle is being solved: pause it first (an import writes into "
                     "the folder its search saves in)");
  }
  BTE_TRY(auto imported,
          app::Document::importPaused(path, app::DocumentSettings{s.settings.undoDepth},
                                      s.settings.panexFolder));
  const app::Id problem = imported.second;
  s.replaceDocument(std::move(imported.first), std::nullopt);
  return Json{{"problem", formatId(IdKind::Problem, problem)}, {"document", openedSnapshot(s)}};
}

}  // namespace

void addSolveMethods(MethodTable& t) {
  t["solve.start"] = solveStart;
  t["solve.prepare"] = solvePrepare;
  t["solve.continue"] = solveContinue;
  t["solve.pause"] = solvePause;
  t["solve.savePoint"] = solveSavePoint;
  t["solve.abort"] = solveAbort;
  t["solve.status"] = solveStatus;
  t["solve.exportPaused"] = solveExportPaused;
  t["solve.importPaused"] = solveImportPaused;
}

}  // namespace bt::engine

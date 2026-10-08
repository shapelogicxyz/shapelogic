// SPDX-License-Identifier: GPL-3.0-or-later
// solve.* (api/solve.json).
#include "convert.h"
#include "engine.h"

namespace bt::engine {

namespace {

enum class Launch : std::uint8_t { Start, Prepare, Continue };

Reply launch(State& s, Params& p, Launch how) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const Json given = p.has("options") ? p.value("options") : Json(nullptr);
  BTE_CHECK(p.finish());
  BTE_TRY(const SolveOptions options, solveOptionsFrom(s, given));
  app::Document& d = s.document();
  BTE_CHECK(d.problemIndex(problem));
  if (app::problemKind(d.puzzle()) != app::ProblemKind::Brick) {
    return coreError(Errc::Unsupported, "sliding and stacking puzzles are not solved yet");
  }
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
  BTE_CHECK(s.document().solvePauseAtSavePoint(problem));
  return Json::object();
}

Reply solveAbort(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().solveAbort(problem));
  return s.changed();
}

Reply solveStatus(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  BTE_CHECK(p.finish());
  app::Document& d = s.document();
  BTE_TRY(const SolveProgress progress, d.solveProgress(problem));
  BTE_TRY(const SolveStats stats, d.solveStats(problem));
  const std::optional<SolveFailure> failure = d.solveFailure(problem);
  return Json{{"state", solveStateName(d.solveState(problem))},
              {"progress", progressJson(progress)},
              {"stats", statsJson(stats)},
              {"failure", failure ? failureJson(*failure) : Json(nullptr)}};
}

Reply solveExportPaused(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const std::string path = p.string("path");
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().exportPaused(problem, path));
  return Json::object();
}

Reply solveImportPaused(State& s, Params& p) {
  const std::string path = p.string("path");
  BTE_CHECK(p.finish());
  BTE_TRY(auto imported,
          app::Document::importPaused(path, app::DocumentSettings{s.settings.undoDepth}));
  const app::Id problem = imported.second;
  s.replaceDocument(std::move(imported.first), std::nullopt);
  return Json{{"problem", formatId(IdKind::Problem, problem)}, {"document", snapshot(s)}};
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

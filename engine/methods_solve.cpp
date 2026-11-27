// SPDX-License-Identifier: GPL-3.0-or-later
// solve.* (api/solve.json).
#include "convert.h"
#include "engine.h"

#include <optional>
#include <utility>

namespace bt::engine {

namespace {

enum class Launch : std::uint8_t { Start, Prepare, Continue };

Reply launch(State& s, Params& p, Launch how) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const Json given = p.has("options") ? p.value("options") : Json(nullptr);
  SLE_CHECK(p.finish());
  SLE_TRY(SolveOptions options, solveOptionsFrom(s, given));
  app::Document& d = s.document();
  SLE_CHECK(d.problemIndex(problem));
  // solve.continue without options: a solve paused in memory goes on as it
  // ran (the caller need not have kept its options: the autosave, a page
  // reloaded); any other is carried on with the defaults, as it is started
  const std::optional<SolveOptions> asItRan =
      how == Launch::Continue && given.is_null() ? d.pausedOptions(problem) : std::nullopt;
  if (asItRan) {
    options = *asItRan;
    // but in the folders, with the disk and the memory of the settings as
    // they are now, as a Continue with options: a run that paused because
    // its folder could not be written goes on once another one is set
    // (cleanup item 15.7; it paused again in the folder it started with)
    searchSettingsInto(s, options);
  } else {
    defaultStackingSolver(s, problem, given, options);
  }
  SLE_CHECK(how == Launch::Start     ? d.solveStart(problem, options)
            : how == Launch::Prepare ? d.solvePrepare(problem, options)
                                     : d.solveResume(problem, options));
  s.watches[problem].launched = true;
  s.launchedWith = std::move(options);
  return s.changed();
}

Reply solveStart(State& s, Params& p) { return launch(s, p, Launch::Start); }
Reply solvePrepare(State& s, Params& p) { return launch(s, p, Launch::Prepare); }
Reply solveContinue(State& s, Params& p) { return launch(s, p, Launch::Continue); }

// A pause is for a run under way (the original's Pause button).
RpcResult<void> running(const State& s, app::Id problem) {
  SLE_CHECK(s.document().problemIndex(problem));
  if (!isActive(s.document().solveState(problem))) {
    return std::unexpected(toRpc(Error{Errc::InvalidState, "the problem is not being solved"}));
  }
  return {};
}

Reply solvePause(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  SLE_CHECK(p.finish());
  SLE_CHECK(running(s, problem));
  SLE_CHECK(s.document().solvePause(problem));
  return Json::object();
}

Reply solveSavePoint(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  SLE_CHECK(p.finish());
  SLE_CHECK(running(s, problem));
  // the original never autosaved a stacking solve: its search saves itself
  // (by base/save_cadence.h's rule as it runs, and when it stops), and has
  // no point to pause at for the puzzle's save
  if (stacking::isStacking(s.document().puzzle())) {
    return coreError(Errc::Unsupported,
                     "a stacking search saves itself: it has no save point (pause it instead)");
  }
  SLE_CHECK(s.document().solvePauseAtSavePoint(problem));
  return Json::object();
}

Reply solveAbort(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  SLE_CHECK(p.finish());
  SLE_CHECK(s.document().solveAbort(problem, s.settings.panexFolder));
  return s.changed();
}

Reply solveReset(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  SLE_CHECK(p.finish());
  SLE_CHECK(s.document().solveReset(problem, s.settings.panexFolder));
  return s.changed();
}

Reply solveStatus(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  SLE_CHECK(p.finish());
  app::Document& d = s.document();
  SLE_TRY(const SolveProgress progress, d.solveProgress(problem));
  SLE_TRY(const SolveStats stats, d.solveStats(problem));
  const std::optional<SolveFailure> failure = d.solveFailure(problem);
  const std::string note = d.solveNote(problem);
  const std::optional<FolderFailure> folder = d.solveFolderFailure(problem);
  SLE_TRY(const std::optional<app::SavedSearchInfo> saved,
          d.savedSearch(problem, s.settings.panexFolder));
  SLE_TRY(const bool startsOver, d.continueStartsOver(problem, s.settings.slideFolder));
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
              {"folderFailure", folder ? Json{{"folder", folder->folder}, {"reason", folder->reason}}
                                       : Json(nullptr)},
              {"savedSearch", std::move(savedJson)},
              {"continueStartsOver", startsOver}};
}

// The live preview: while it is on and a run is active, the poll sends
// solve.preview with each solve.progress, when the search has a picture.
Reply solvePreview(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const bool on = p.boolean("on");
  SLE_CHECK(p.finish());
  SLE_CHECK(s.document().problemIndex(problem));
  s.watches[problem].preview = on;
  if (on) {
    s.document().requestSolveSample(problem);
  }
  return Json::object();
}

Reply solveSearchFolders(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  SLE_CHECK(p.finish());
  SLE_TRY(const std::vector<std::string> folders,
          s.document().searchFolders(problem, s.settings.slideFolder, s.settings.panexFolder));
  return Json{{"folders", folders}};
}

Reply solveExportPaused(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const std::string path = p.string("path");
  const bool withLevels = p.optBoolean("withLevels", true);
  SLE_CHECK(p.finish());
  SLE_CHECK(s.document().exportPaused(problem, path,
                                      app::PausedExport{s.settings.panexFolder, withLevels}));
  return Json::object();
}

Reply solveImportPaused(State& s, Params& p) {
  const std::string path = p.string("path");
  SLE_CHECK(p.finish());
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
  SLE_TRY(auto imported,
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
  t["solve.reset"] = solveReset;
  t["solve.status"] = solveStatus;
  t["solve.preview"] = solvePreview;
  t["solve.searchFolders"] = solveSearchFolders;
  t["solve.exportPaused"] = solveExportPaused;
  t["solve.importPaused"] = solveImportPaused;
}

}  // namespace bt::engine

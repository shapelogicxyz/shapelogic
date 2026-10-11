// SPDX-License-Identifier: GPL-3.0-or-later
// The events (decision 0012): document.changed after a change, and what
// the background work does, polled: solve.state, solve.progress (at most
// four times a second, and once when a run ends), solve.solutionsAdded,
// solve.error, solution.analyseProgress and solution.analyseEnded.
#include "convert.h"
#include "engine.h"

#include <algorithm>

namespace bt::engine {

namespace {

constexpr std::chrono::milliseconds kProgressEvery(250);

std::string problemName(app::Id id) { return formatId(IdKind::Problem, id); }

// The solves of one problem.
void pollSolve(State& s, app::Id id, unsigned int index, State::Watch& w,
               std::chrono::steady_clock::time_point now) {
  app::Document& d = s.document();
  const SolveState state = d.solveState(id);
  const bool changed = state != w.state || w.launched;
  const bool ended = changed && !isActive(state);
  w.launched = false;
  if (changed) {
    s.out.notify("solve.state", Json{{"problem", problemName(id)}, {"state", solveStateName(state)}});
    if (state == SolveState::Error) {
      if (const std::optional<SolveFailure> f = d.solveFailure(id)) {
        s.out.notify("solve.error", Json{{"problem", problemName(id)}, {"failure", failureJson(*f)}});
      }
    }
  }
  const bool due = now - w.progressAt >= kProgressEvery;
  if ((isActive(state) && due) || ended) {
    if (const Result<SolveProgress> g = d.solveProgress(id)) {
      s.out.notify("solve.progress", Json{{"problem", problemName(id)},
                                          {"state", solveStateName(state)},
                                          {"progress", progressJson(*g)}});
    }
    w.progressAt = now;
  }
  if (isActive(state) || changed) {
    // the saved list: the problem counts its changes (a solution added, a
    // sorted insert at the limit, the list thinned)
    struct Seen {
      std::uint64_t version;
      std::size_t saved;
      std::uint64_t assemblies;
      std::uint64_t solutions;
    };
    const Seen seen = d.readProblem(index, [](const Problem& pr) {
      Seen v{pr.solutionsVersion(), pr.getNumberOfSavedSolutions(), 0, 0};
      if (pr.numAssembliesKnown()) {
        v.assemblies = pr.getNumAssemblies();
        v.solutions = pr.getNumSolutions();
      }
      return v;
    });
    if (seen.version != w.solutionsVersion && (due || ended)) {
      s.out.notify("solve.solutionsAdded", Json{{"problem", problemName(id)},
                                                {"saved", seen.saved},
                                                {"solutionsVersion", seen.version},
                                                {"assemblies", seen.assemblies},
                                                {"solutions", seen.solutions}});
      w.solutionsVersion = seen.version;
    }
  }
  w.state = state;
}

Json analyseEvent(app::Id id, const app::AnalyseProgress& a) {
  return Json{{"problem", problemName(id)},
              {"total", a.total},
              {"done", a.done},
              {"separated", a.separated},
              {"stopped", a.stopped},
              {"failure", a.failure ? Json(*a.failure) : Json(nullptr)}};
}

// The analysis of one problem.
void pollAnalysis(State& s, app::Id id, State::Watch& w,
                  std::chrono::steady_clock::time_point now) {
  if (!w.analysing) {
    return;
  }
  const Result<app::AnalyseProgress> a = s.document().analyseProgress(id);
  if (!a) {
    w.analysing = false;
    return;
  }
  if (a->active) {
    if (now - w.analyseAt >= kProgressEvery && a->done != w.analysed) {
      s.out.notify("solution.analyseProgress", analyseEvent(id, *a));
      w.analyseAt = now;
      w.analysed = a->done;
    }
    return;
  }
  if (a->failure) {
    s.log << "bt-engine: the analysis of " << problemName(id) << " failed: " << *a->failure << '\n';
  }
  s.out.notify("solution.analyseEnded", analyseEvent(id, *a));
  w.analysing = false;
}

}  // namespace

void announceChange(State& s) {
  if (s.revision() == s.announced) {
    return;
  }
  s.announced = s.revision();
  const app::ChangeSet& c = s.document().lastChange();
  Json what = Json::array();
  for (const auto& [bit, name] : {std::pair{app::kChangeShapes, "shapes"},
                                  std::pair{app::kChangeColours, "colours"},
                                  std::pair{app::kChangeProblems, "problems"},
                                  std::pair{app::kChangeSolutions, "solutions"},
                                  std::pair{app::kChangeNotes, "notes"},
                                  std::pair{app::kChangeRodSets, "rodSets"}}) {
    if ((c.what & bit) != 0) {
      what.push_back(name);
    }
  }
  // ids of items that are gone by now (a later command in a group) are left out
  Json shapes = Json::array();
  for (const app::Id id : c.shapes) {
    if (s.document().shapeIndex(id)) {
      shapes.push_back(formatId(IdKind::Shape, id));
    }
  }
  Json problems = Json::array();
  for (const app::Id id : c.problems) {
    if (s.document().problemIndex(id)) {
      problems.push_back(formatId(IdKind::Problem, id));
    }
  }
  Json changed{{"revision", s.revision()},
               {"what", std::move(what)},
               {"shapes", std::move(shapes)},
               {"problems", std::move(problems)}};
  if (!c.notice.empty()) {
    changed["notice"] = c.notice;
  }
  s.out.notify("document.changed", std::move(changed));
}

void pollEvents(State& s, std::chrono::steady_clock::time_point now,
                const std::function<void()>& between) {
  app::Document& d = s.document();
  // What background work finished goes in first (a revision of its own,
  // announced below). Taken in, a run or an analysis has ended, so the
  // telling below sees its last state and its final counts and tells them
  // before that revision; work that ends after the taking in is told here
  // and taken in at the next poll. (Told first and taken in after, a run
  // ending in between had its document.changed before its last state, and
  // an analysis's analyseEnded came after its results, with zeros.)
  d.poll();
  if (between) {
    between();
  }
  const std::vector<app::Id>& ids = d.problemIds();
  for (unsigned int i = 0; i < ids.size(); i++) {
    const app::Id id = ids[i];
    const auto it = s.watches.find(id);
    if (it == s.watches.end() && d.solveState(id) == SolveState::Idle) {
      continue;
    }
    State::Watch& w = it != s.watches.end() ? it->second : s.watches[id];
    pollSolve(s, id, i, w, now);
    pollAnalysis(s, id, w, now);
  }
  // problems that are gone
  std::erase_if(s.watches, [&d](const auto& w) { return !d.problemIndex(w.first); });
  // the revision of what was taken in (and of anything else not yet told)
  announceChange(s);
}

}  // namespace bt::engine

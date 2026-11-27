// SPDX-License-Identifier: GPL-3.0-or-later
// solution.* (api/solution.json).
#include "convert.h"
#include "engine.h"

#include "shapelogic/base/thread_pool.h"
#include "shapelogic/model/disassembly.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/solution.h"
#include "shapelogic/model/stacking.h"

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bt::engine {

namespace {

constexpr std::int64_t kDefaultPage = 100;

// Disassembly::compare() < 0 on two solutions' sequence lengths: the first
// that differs decides, a sequence past a list's end counting as 0.
bool lowerLevel(const std::vector<unsigned int>& a, const std::vector<unsigned int>& b) {
  const std::size_t n = std::max(a.size(), b.size());
  for (std::size_t i = 0; i < n; i++) {
    const unsigned int x = i < a.size() ? a[i] : 0;
    const unsigned int y = i < b.size() ? b[i] : 0;
    if (x != y) {
      return x < y;
    }
  }
  return false;
}

// What the comparisons of sort `by` read of a solution (PageOrder::Key):
// the level's sequences for 1, the moves for 2.
PageOrder::Key sortKey(const Solution& sol, int by) {
  PageOrder::Key key;
  const Disassembly* d = sol.getDisassemblyInfo();
  key.info = d != nullptr;
  if (d == nullptr) {
    return key;
  }
  if (by == 1) {
    const unsigned int n = d->getNumSequences();
    key.levels.reserve(n);
    for (unsigned int i = 0; i < n; i++) {
      key.levels.push_back(d->getSequenceLength(i));
    }
  } else {
    key.moves = d->sumMoves();
  }
  return key;
}

// The problem's solutions in the order solution.page gives them, kept in
// the engine's state while the list, the document and the sort asked for
// are the same (PageOrder). The order is the one a stable sort of the list
// by Problem::sortSolutions' comparison gives (solutionSortOrder), ties as
// the list has them: by level and by moves that comparison walks both
// solutions' trees, so here each solution's key is worked out once and the
// same sort runs on the keys, each comparison answering what that one
// answers (comp1Level, comp2Moves of problem.cpp; the test holds the pages
// to them).
const PageOrder& pageOrder(State& s, app::Id problem, const Problem& pr, int by, bool descending) {
  PageOrder& o = s.pageOrders[problem];
  const std::uint64_t revision = s.revision();
  const std::uint64_t version = pr.solutionsVersion();
  const unsigned int n = pr.getNumberOfSavedSolutions();
  if (o.revision == revision && o.version == version && o.by == by &&
      o.descending == descending && o.order.size() == n) {
    return o;
  }
  // the keys stand while the revision and the sort do
  if (o.revision != revision || o.by != by) {
    o.keys.clear();
  }
  o.revision = revision;
  o.version = version;
  o.by = by;
  o.descending = descending;
  o.disassembled = 0;
  o.order.resize(n);
  std::iota(o.order.begin(), o.order.end(), 0U);
  for (unsigned int i = 0; i < n; i++) {
    if (pr.getSavedSolution(i).getDisassemblyInfo() != nullptr) {
      o.disassembled++;
    }
  }
  if (by == 1 || by == 2) {
    // each solution's key: kept from the last order (taken out of it, so
    // that an id met twice, which a damaged file can hold, is worked out
    // for its second solution), or worked out now
    std::vector<PageOrder::Key> keys(n);
    for (unsigned int i = 0; i < n; i++) {
      const Solution& sol = pr.getSavedSolution(i);
      if (const auto kept = o.keys.find(sol.getId()); kept != o.keys.end()) {
        keys[i] = std::move(kept->second);
        o.keys.erase(kept);
      } else {
        keys[i] = sortKey(sol, by);
      }
    }
    const auto less = [by](const PageOrder::Key& x, const PageOrder::Key& y) {
      return x.info && y.info && (by == 1 ? lowerLevel(x.levels, y.levels) : x.moves < y.moves);
    };
    std::stable_sort(o.order.begin(), o.order.end(), [&](unsigned int a, unsigned int b) {
      return descending ? less(keys[b], keys[a]) : less(keys[a], keys[b]);
    });
    // kept for the next order, the first of an id; the keys of solutions
    // that are gone go
    o.keys.clear();
    o.keys.reserve(n);
    for (unsigned int i = 0; i < n; i++) {
      o.keys.try_emplace(pr.getSavedSolution(i).getId(), std::move(keys[i]));
    }
  } else if (const SolutionLess less = solutionSortOrder(by); less != nullptr) {
    // by assembly number or by the pieces used: the comparison is cheap
    std::stable_sort(o.order.begin(), o.order.end(), [&](unsigned int a, unsigned int b) {
      const Solution& x = pr.getSavedSolution(a);
      const Solution& y = pr.getSavedSolution(b);
      return descending ? less(y, x) : less(x, y);
    });
  } else if (descending) {
    std::reverse(o.order.begin(), o.order.end());
  }
  return o;
}

Reply solutionPage(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  // -1: the stored order; else Problem::sortSolutions' number
  const int by = p.optChoice("sortBy", solutionPageSortNames()).value_or(-1);
  const bool descending = p.optBoolean("descending", false);
  const auto offset = static_cast<std::size_t>(p.optInteger("offset", 0, 0, 0xFFFFFFFF));
  const auto limit = static_cast<std::size_t>(p.optInteger("limit", kDefaultPage, 1, kMaxPageLimit));
  BTE_CHECK(p.finish());
  BTE_TRY(const unsigned int index, s.document().problemIndex(problem));
  // sorted as a view, under the lock a running solve writes with: the
  // order is kept from page to page (pageOrder)
  const bool stackingPuzzle = stacking::isStacking(s.document().puzzle());
  return s.document().readProblem(index, [&](const Problem& pr) -> Reply {
    const PageOrder& sorted = pageOrder(s, problem, pr, by, descending);
    const std::vector<unsigned int>& order = sorted.order;
    Json rows = Json::array();
    for (std::size_t k = offset; k < order.size() && k < offset + limit; k++) {
      rows.push_back(solutionRow(pr.getSavedSolution(order[k]), order[k], stackingPuzzle));
    }
    return Json{{"total", order.size()},
                {"disassembled", sorted.disassembled},
                {"offset", offset},
                {"solutionsVersion", pr.solutionsVersion()},
                {"rows", std::move(rows)}};
  });
}

Reply solutionGet(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const app::Id solution = p.id("solution", IdKind::Solution);
  BTE_CHECK(p.finish());
  BTE_TRY(const unsigned int index, s.document().problemIndex(problem));
  return s.document().readProblem(index, [&](const Problem& pr) -> Reply {
    for (unsigned int i = 0; i < pr.getNumberOfSavedSolutions(); i++) {
      const Solution& sol = pr.getSavedSolution(i);
      if (sol.getId() == solution) {
        return Json{{"row", solutionRow(sol, i, stacking::isStacking(s.document().puzzle()))},
                    {"placements", placementsJson(sol.getAssembly())}};
      }
    }
    return coreError(Errc::NotFound, "there is no such solution");
  });
}

Reply solutionDelete(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const app::SolutionsToDelete which = p.choice("which", solutionsToDeleteNames());
  const std::optional<app::Id> solution = p.optId("solution", IdKind::Solution);
  BTE_CHECK(p.finish());
  const bool relative = which == app::SolutionsToDelete::One ||
                        which == app::SolutionsToDelete::Before ||
                        which == app::SolutionsToDelete::After;
  if (relative && !solution) {
    return coreError(Errc::InvalidArgument, "'solution' names the one to remove from");
  }
  BTE_CHECK(s.document().deleteSolutions(problem, which, solution.value_or(0)));
  return s.changed();
}

Reply solutionDeleteDisassembly(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const std::optional<app::Id> solution = p.optId("solution", IdKind::Solution);
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().deleteDisassemblies(problem, solution));
  return s.changed();
}

Reply solutionToShapes(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  app::AssemblyImport o;
  o.solution = p.optId("solution", IdKind::Solution);
  o.dropDisconnected = p.optBoolean("dropDisconnected", false);
  o.dropMirror = p.optBoolean("dropMirror", false);
  o.dropSymmetric = p.optBoolean("dropSymmetric", false);
  o.dropNonMillable = p.optBoolean("dropNonMillable", false);
  o.dropNonNotchable = p.optBoolean("dropNonNotchable", false);
  o.dropIdentical = p.optBoolean("dropIdentical", false);
  o.minCells = static_cast<unsigned int>(p.optInteger("minCells", 0, 0, 0xFFFFFFFF));
  o.maxCells = static_cast<unsigned int>(p.optInteger("maxCells", 0xFFFFFFFF, 0, 0xFFFFFFFF));
  o.addTo = p.optChoice("addTo", assemblyAddToNames()).value_or(app::AssemblyImport::AddTo::Nothing);
  o.target = p.optId("target", IdKind::Problem);
  o.min = static_cast<unsigned int>(p.optInteger("min", 1, 0, kMaxPartCount));
  o.max = static_cast<unsigned int>(p.optInteger("max", 1, 0, kMaxPartCount));
  BTE_CHECK(p.finish());
  BTE_TRY(const std::vector<app::Id> shapes, s.document().assembliesToShapes(problem, o));
  Json ids = Json::array();
  for (const app::Id id : shapes) {
    ids.push_back(formatId(IdKind::Shape, id));
  }
  return Json{{"shapes", std::move(ids)}, {"revision", s.revision()}};
}

Reply solutionAnalyse(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  app::AnalyseOptions o;
  o.which = p.choice("which", analyseWhichNames());
  const std::optional<app::Id> solution = p.optId("solution", IdKind::Solution);
  o.solver = p.optChoice("solver", solverNames()).value_or(s.settings.solver);
  o.rotations = p.optBoolean("rotations", false);
  o.threads = static_cast<unsigned int>(p.optInteger("threads", s.settings.threads, 0, kMaxThreads));
  BTE_CHECK(p.finish());
  if (o.which == app::AnalyseWhich::One) {
    if (!solution) {
      return coreError(Errc::InvalidArgument, "'solution' names the one to take apart");
    }
    o.solution = *solution;
  }
  BTE_TRY(const unsigned int total, s.document().analyseStart(problem, o));
  if (total > 0) {
    State::Watch& w = s.watches[problem];
    w.analysing = true;
    w.analysed = 0;
  }
  return Json{{"total", total}};
}

Reply solutionAnalyseStop(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().analyseStop(problem));
  return Json::object();
}

Json analyseJson(const app::AnalyseProgress& a) {
  return Json{{"active", a.active},
              {"total", a.total},
              {"done", a.done},
              {"separated", a.separated},
              {"stopped", a.stopped},
              {"failure", a.failure ? Json(*a.failure) : Json(nullptr)},
              {"current", a.active ? takeApartJson(a.current) : Json(nullptr)}};
}

Reply solutionAnalyseStatus(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  BTE_CHECK(p.finish());
  BTE_TRY(const app::AnalyseProgress a, s.document().analyseProgress(problem));
  return analyseJson(a);
}

}  // namespace

void addSolutionMethods(MethodTable& t) {
  t["solution.page"] = solutionPage;
  t["solution.get"] = solutionGet;
  t["solution.delete"] = solutionDelete;
  t["solution.deleteDisassembly"] = solutionDeleteDisassembly;
  t["solution.toShapes"] = solutionToShapes;
  t["solution.analyse"] = solutionAnalyse;
  t["solution.analyseStop"] = solutionAnalyseStop;
  t["solution.analyseStatus"] = solutionAnalyseStatus;
}

}  // namespace bt::engine

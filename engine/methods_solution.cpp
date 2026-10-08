// SPDX-License-Identifier: GPL-3.0-or-later
// solution.* (api/solution.json).
#include "convert.h"
#include "engine.h"

#include <algorithm>
#include <numeric>

namespace bt::engine {

namespace {

constexpr std::int64_t kDefaultPage = 100;

Reply solutionPage(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  // -1: the stored order; else Problem::sortSolutions' number
  const int by = p.optChoice("sortBy", Names<int>{{"list", -1},
                                                  {"assembly", 0},
                                                  {"level", 1},
                                                  {"moves", 2},
                                                  {"pieces", 3}})
                     .value_or(-1);
  const bool descending = p.optBoolean("descending", false);
  const auto offset = static_cast<std::size_t>(p.optInteger("offset", 0, 0, 0xFFFFFFFF));
  const auto limit = static_cast<std::size_t>(p.optInteger("limit", kDefaultPage, 1, 10000));
  BTE_CHECK(p.finish());
  BTE_TRY(const unsigned int index, s.document().problemIndex(problem));
  // sorted as a view, under the lock a running solve writes with
  return s.document().readProblem(index, [&](const Problem& pr) -> Reply {
    std::vector<unsigned int> order(pr.getNumberOfSavedSolutions());
    std::iota(order.begin(), order.end(), 0U);
    const SolutionLess less = solutionSortOrder(by);
    if (less != nullptr) {
      std::stable_sort(order.begin(), order.end(), [&](unsigned int a, unsigned int b) {
        const Solution& x = pr.getSavedSolution(a);
        const Solution& y = pr.getSavedSolution(b);
        return descending ? less(y, x) : less(x, y);
      });
    } else if (descending) {
      std::reverse(order.begin(), order.end());
    }
    Json rows = Json::array();
    for (std::size_t k = offset; k < order.size() && k < offset + limit; k++) {
      rows.push_back(solutionRow(pr.getSavedSolution(order[k]), order[k]));
    }
    return Json{{"total", order.size()},
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
        return Json{{"row", solutionRow(sol, i)}, {"placements", placementsJson(sol.getAssembly())}};
      }
    }
    return coreError(Errc::NotFound, "there is no such solution");
  });
}

Reply solutionDelete(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const app::SolutionsToDelete which =
      p.choice("which", Names<app::SolutionsToDelete>{
                            {"one", app::SolutionsToDelete::One},
                            {"before", app::SolutionsToDelete::Before},
                            {"after", app::SolutionsToDelete::After},
                            {"withoutDisassembly", app::SolutionsToDelete::WithoutDisassembly},
                            {"all", app::SolutionsToDelete::All}});
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
  o.addTo = p.optChoice("addTo", Names<app::AssemblyImport::AddTo>{
                                     {"nothing", app::AssemblyImport::AddTo::Nothing},
                                     {"newProblem", app::AssemblyImport::AddTo::NewProblem},
                                     {"problem", app::AssemblyImport::AddTo::Problem}})
                .value_or(app::AssemblyImport::AddTo::Nothing);
  o.target = p.optId("target", IdKind::Problem);
  o.min = static_cast<unsigned int>(p.optInteger("min", 1, 0, 0xFFFF));
  o.max = static_cast<unsigned int>(p.optInteger("max", 1, 0, 0xFFFF));
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
  o.which = p.choice("which", Names<app::AnalyseWhich>{{"all", app::AnalyseWhich::All},
                                                       {"missing", app::AnalyseWhich::Missing},
                                                       {"one", app::AnalyseWhich::One}});
  const std::optional<app::Id> solution = p.optId("solution", IdKind::Solution);
  o.solver = p.optChoice("solver", solverNames()).value_or(s.settings.solver);
  o.rotations = p.optBoolean("rotations", false);
  o.threads = static_cast<unsigned int>(p.optInteger("threads", s.settings.threads, 0, 256));
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

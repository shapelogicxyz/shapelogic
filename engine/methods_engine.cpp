// SPDX-License-Identifier: GPL-3.0-or-later
// engine.info, engine.shutdown, settings.get / set (api/engine.json).
#include "convert.h"
#include "engine.h"

#include "bt/app/shape_tools.h"
#include "bt/base/thread_pool.h"
#include "bt/base/version.h"
#include "bt/grid/grid_type.h"

namespace bt::engine {

namespace {

// The grids this version solves (sliding and stacking load and save only).
bool solvable(GridKind g) { return g != GridKind::Sliding && g != GridKind::Stacking; }

Reply engineInfo(State& s, Params& p) {
  BTE_CHECK(p.finish());
  Json grids = Json::array();
  for (const auto& [name, kind] : gridNames()) {
    const unsigned int caps = GridType(kind).getCapabilities();
    grids.push_back(Json{{"grid", name},
                         {"label", toString(kind)},
                         {"assemble", (caps & kCapAssemble) != 0},
                         {"disassemble", (caps & kCapDisassemble) != 0},
                         {"supported", solvable(kind)},
                         {"defaultSize", app::defaultShapeSize(kind)},
                         {"sizeUnit", app::sizeUnit(kind)}});
  }
  Json solvers = Json::array();
  for (const auto& [name, type] : solverNames()) {
    solvers.push_back(Json{{"solver", name}, {"label", solverTypeLabel(type)}});
  }
  Json methods = Json::array();
  for (const auto& [name, m] : s.methods) {
    methods.push_back(name);
  }
  Json j = Json::object();
  j["name"] = "bt-engine";
  j["version"] = kVersion;
  j["protocol"] = 1;
  j["grids"] = std::move(grids);
  j["solvers"] = std::move(solvers);
  j["hardwareThreads"] = hardwareThreads();
  j["maxThreads"] = kMaxThreads;
  j["methods"] = std::move(methods);
  return j;
}

Reply engineShutdown(State& s, Params& p) {
  BTE_CHECK(p.finish());
  s.shutdown = true;
  return Json::object();
}

Json settingsJson(const State& s) {
  return Json{{"undoDepth", s.settings.undoDepth},
              {"threads", s.settings.threads},
              {"solver", nameOf(solverNames(), s.settings.solver)}};
}

Reply settingsGet(State& s, Params& p) {
  BTE_CHECK(p.finish());
  return settingsJson(s);
}

Reply settingsSet(State& s, Params& p) {
  const std::optional<std::int64_t> depth = p.maybeInteger("undoDepth", 1, 10000);
  const std::optional<std::int64_t> threads = p.maybeInteger("threads", 0, kMaxThreads);
  const std::optional<SolverType> solver = p.optChoice("solver", solverNames());
  BTE_CHECK(p.finish());
  if (depth) {
    s.settings.undoDepth = static_cast<unsigned int>(*depth);
    s.document().setSettings(app::DocumentSettings{s.settings.undoDepth});
  }
  if (threads) {
    s.settings.threads = static_cast<unsigned int>(*threads);
  }
  if (solver) {
    s.settings.solver = *solver;
  }
  return settingsJson(s);
}

}  // namespace

void addEngineMethods(MethodTable& t) {
  t["engine.info"] = engineInfo;
  t["engine.shutdown"] = engineShutdown;
  t["settings.get"] = settingsGet;
  t["settings.set"] = settingsSet;
}

}  // namespace bt::engine

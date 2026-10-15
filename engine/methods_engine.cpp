// SPDX-License-Identifier: GPL-3.0-or-later
// engine.info, engine.shutdown, settings.get / set (api/engine.json).
#include "convert.h"
#include "engine.h"

#include "shapelogic/app/shape_tools.h"
#include "shapelogic/base/thread_pool.h"
#include "shapelogic/base/version.h"
#include "shapelogic/grid/grid_type.h"
#include "shapelogic/model/grid_convert.h"

#include <limits>
#include <string>

namespace bt::engine {

namespace {

// The protocol this engine speaks (engine.hello, engine.info).
constexpr int kProtocol = 1;

// The sliding solvers' labels: the original's menu.
std::string_view slidingSolverLabel(sliding::SlidingSolver s) {
  switch (s) {
    case sliding::SlidingSolver::Fast:
      return "Sliding Fast Solver (250k depth)";
    case sliding::SlidingSolver::Deep:
      return "Sliding Deep Solver (1mil depth)";
    case sliding::SlidingSolver::Full:
      break;
  }
  return "Sliding Full Solver (full depth)";
}

// The stacking solvers' labels: the original's menu (syncSolverTypeMenu).
std::string_view stackingSolverLabel(stacking::StackingSolver s) {
  return s == stacking::StackingSolver::Panex ? "Panex Solver" : "Stacking Solver";
}

Reply engineHello(State& s, Params& p) {
  const std::int64_t protocol = p.integer("protocol", 1, std::numeric_limits<std::int64_t>::max());
  BTE_CHECK(p.finish());
  (void)s;
  if (protocol != kProtocol) {
    return coreError(Errc::Unsupported, "this engine speaks protocol " + std::to_string(kProtocol) +
                                            ", not " + std::to_string(protocol));
  }
  return Json{{"protocol", kProtocol}, {"name", "shapelogic_engine"}, {"version", kVersion}};
}

Reply engineInfo(State& s, Params& p) {
  BTE_CHECK(p.finish());
  Json grids = Json::array();
  for (const auto& [name, kind] : gridNames()) {
    const unsigned int caps = GridType(kind).getCapabilities();
    Json convertsTo = Json::array();
    for (const GridKind to : convertTargets(kind)) {
      convertsTo.push_back(nameOf(gridNames(), to));
    }
    grids.push_back(Json{{"grid", name},
                         {"label", toString(kind)},
                         {"assemble", (caps & kCapAssemble) != 0},
                         {"disassemble", (caps & kCapDisassemble) != 0},
                         {"supported", true},
                         {"defaultSize", app::defaultShapeSize(kind)},
                         {"sizeUnit", app::sizeUnit(kind)},
                         {"convertsTo", std::move(convertsTo)}});
  }
  Json solvers = Json::array();
  for (const auto& [name, type] : solverNames()) {
    solvers.push_back(Json{{"solver", name}, {"label", solverTypeLabel(type)}});
  }
  Json slidingSolvers = Json::array();
  for (const auto& [name, solver] : slidingSolverNames()) {
    slidingSolvers.push_back(Json{{"solver", name}, {"label", slidingSolverLabel(solver)}});
  }
  Json stackingSolvers = Json::array();
  for (const auto& [name, solver] : stackingSolverNames()) {
    stackingSolvers.push_back(Json{{"solver", name}, {"label", stackingSolverLabel(solver)}});
  }
  Json methods = Json::array();
  for (const auto& [name, m] : s.methods) {
    methods.push_back(name);
  }
  Json j = Json::object();
  j["name"] = "shapelogic_engine";
  j["version"] = kVersion;
  j["protocol"] = kProtocol;
  j["grids"] = std::move(grids);
  j["solvers"] = std::move(solvers);
  j["slidingSolvers"] = std::move(slidingSolvers);
  j["stackingSolvers"] = std::move(stackingSolvers);
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
              {"solver", nameOf(solverNames(), s.settings.solver)},
              {"slideFolder", s.settings.slideFolder},
              {"slideDiskGB", s.settings.slideDiskGB},
              {"panexFolder", s.settings.panexFolder},
              {"panexDiskGB", s.settings.panexDiskGB}};
}

Reply settingsGet(State& s, Params& p) {
  BTE_CHECK(p.finish());
  return settingsJson(s);
}

Reply settingsSet(State& s, Params& p) {
  const std::optional<std::int64_t> depth = p.maybeInteger("undoDepth", 1, 10000);
  const std::optional<std::int64_t> threads = p.maybeInteger("threads", 0, kMaxThreads);
  const std::optional<SolverType> solver = p.optChoice("solver", solverNames());
  const std::optional<std::string> slideFolder = p.optString("slideFolder");
  const std::optional<double> slideDiskGB =
      p.has("slideDiskGB") ? std::optional<double>(p.number("slideDiskGB", 0, 1e9)) : std::nullopt;
  const std::optional<std::string> panexFolder = p.optString("panexFolder");
  const std::optional<double> panexDiskGB =
      p.has("panexDiskGB") ? std::optional<double>(p.number("panexDiskGB", 0, 1e9)) : std::nullopt;
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
  if (slideFolder) {
    s.settings.slideFolder = *slideFolder;
  }
  if (slideDiskGB) {
    s.settings.slideDiskGB = *slideDiskGB;
  }
  if (panexFolder) {
    s.settings.panexFolder = *panexFolder;
  }
  if (panexDiskGB) {
    s.settings.panexDiskGB = *panexDiskGB;
  }
  return settingsJson(s);
}

}  // namespace

void addEngineMethods(MethodTable& t) {
  t["engine.hello"] = engineHello;
  t["engine.info"] = engineInfo;
  t["engine.shutdown"] = engineShutdown;
  t["settings.get"] = settingsGet;
  t["settings.set"] = settingsSet;
}

}  // namespace bt::engine

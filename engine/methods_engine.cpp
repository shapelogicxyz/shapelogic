// SPDX-License-Identifier: GPL-3.0-or-later
// engine.info, engine.shutdown, settings.get / set (api/engine.json).
#include "convert.h"
#include "engine.h"

#include "shapelogic/app/shape_tools.h"
#include "shapelogic/base/system.h"
#include "shapelogic/base/thread_pool.h"
#include "shapelogic/base/version.h"
#include "shapelogic/grid/grid_traits.h"
#include "shapelogic/grid/grid_type.h"
#include "shapelogic/mesh/grid_geometry.h"
#include "shapelogic/model/grid_convert.h"

#include <limits>
#include <string>

namespace bt::engine {

namespace {

// The protocol this engine speaks (engine.hello, engine.info).
constexpr int kProtocol = 1;

// A matrix as rows of numbers.
Json matrixJson(const mesh::Matrix3& m) {
  Json rows = Json::array();
  for (const auto& row : m) {
    rows.push_back(Json{row[0], row[1], row[2]});
  }
  return rows;
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
  return Json{
      {"protocol", kProtocol}, {"name", "shapelogic_engine"}, {"version", versionString()}};
}

Reply engineInfo(State& s, Params& p) {
  BTE_CHECK(p.finish());
  Json grids = Json::array();
  for (const auto& [name, kind] : gridNames()) {
    const unsigned int caps = GridType(kind).getCapabilities();
    const mesh::SpaceRule space = mesh::spaceRule(kind);
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
                         {"convertsTo", std::move(convertsTo)},
                         {"cubeLike", gridTraits(kind).cubeLike},
                         {"cellToMesh", matrixJson(space.cellToMesh)},
                         {"extent",
                          Json{{"unit", space.extentUnit},
                               {"matrix", matrixJson(space.extentMatrix)},
                               {"offset", Json{space.extentOffset[0], space.extentOffset[1],
                                               space.extentOffset[2]}}}}});
  }
  Json solvers = Json::array();
  for (const auto& [name, type] : solverNames()) {
    solvers.push_back(Json{{"solver", name}, {"label", solverTypeLabel(type)}});
  }
  // the one sliding solver
  Json slidingSolvers = Json::array();
  slidingSolvers.push_back(Json{{"solver", "sliding"}, {"label", sliding::kSlidingSolverName}});
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
  j["version"] = versionString();
  j["protocol"] = kProtocol;
  j["grids"] = std::move(grids);
  j["solvers"] = std::move(solvers);
  j["slidingSolvers"] = std::move(slidingSolvers);
  j["stackingSolvers"] = std::move(stackingSolvers);
  j["hardwareThreads"] = hardwareThreads();
  j["maxThreads"] = kMaxThreads;
  j["memoryBytes"] = physicalMemoryBytes();
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
              {"panexDiskGB", s.settings.panexDiskGB},
              {"searchMemoryGB", s.settings.searchMemoryGB},
              {"log", nameOf(logSettingNames(), s.settings.log)}};
}

Reply settingsGet(State& s, Params& p) {
  BTE_CHECK(p.finish());
  return settingsJson(s);
}

Reply settingsSet(State& s, Params& p) {
  const std::optional<std::int64_t> depth = p.maybeInteger("undoDepth", 1, Settings::kMaxUndoDepth);
  const std::optional<std::int64_t> threads = p.maybeInteger("threads", 0, kMaxThreads);
  const std::optional<SolverType> solver = p.optChoice("solver", solverNames());
  const std::optional<std::string> slideFolder = p.optString("slideFolder");
  const std::optional<double> slideDiskGB =
      p.has("slideDiskGB") ? std::optional<double>(p.number("slideDiskGB", 0, kMaxDiskBudgetGB)) : std::nullopt;
  const std::optional<std::string> panexFolder = p.optString("panexFolder");
  const std::optional<double> panexDiskGB =
      p.has("panexDiskGB") ? std::optional<double>(p.number("panexDiskGB", 0, kMaxDiskBudgetGB)) : std::nullopt;
  const std::optional<double> searchMemoryGB =
      p.has("searchMemoryGB")
          ? std::optional<double>(p.number("searchMemoryGB", 0, Settings::kMaxSearchMemoryGB))
          : std::nullopt;
  const std::optional<LogSetting> log = p.optChoice("log", logSettingNames());
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
  if (log) {
    s.settings.log = *log;
  }
  if (searchMemoryGB) {
    s.settings.searchMemoryGB = *searchMemoryGB;
    // the searches running now follow at their next level
    s.memory->setTotal(s.settings.searchMemoryBytes());
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

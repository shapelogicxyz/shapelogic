// SPDX-License-Identifier: GPL-3.0-or-later
// The engine's tables against the API schema (cleanup item 11.1): every
// Names table against the schema's enum, the stored solver settings' check
// boxes against SolverSettings, the numeric maxima against the core's
// constants, and the error codes against api/errors.json. The contract
// suite checks what the engine sends; these check what it accepts too,
// whether or not a test happens to send it.
//
// The schema is walked whole (checkpoint E): an `enum` or a `maximum` that
// no row here ties to the C++ must be listed with the reason it is not, so
// a new one cannot be left out without saying so.
#include "convert.h"
#include "engine.h"
#include "rpc.h"

#include "shapelogic/app/document.h"
#include "shapelogic/app/sliding_edits.h"
#include "shapelogic/base/solve_state.h"
#include "shapelogic/base/system.h"
#include "shapelogic/base/thread_pool.h"
#include "shapelogic/grid/shape_image.h"
#include "shapelogic/grid/tray_border.h"
#include "shapelogic/mesh/stl.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/stacking_types.h"
#include "shapelogic/model/tilted_tray.h"
#include "shapelogic/solve/solve_progress.h"
#include "shapelogic/solve/solve_session.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace bt;
using namespace bt::engine;

namespace {

// api/<file> (the tests run from the source folder, as they find
// tests/data).
const nlohmann::json& schemaFile(const std::string& file) {
  static std::map<std::string, nlohmann::json> files;
  auto it = files.find(file);
  if (it == files.end()) {
    std::ifstream in(std::filesystem::path("api") / file);
    REQUIRE(in.good());
    it = files.emplace(file, nlohmann::json::parse(in)).first;
  }
  return it->second;
}

// The schema at "file#/pointer".
const nlohmann::json& at(const std::string& where) {
  const std::size_t hash = where.find('#');
  REQUIRE(hash != std::string::npos);
  const nlohmann::json& doc = schemaFile(where.substr(0, hash));
  const nlohmann::json::json_pointer pointer(where.substr(hash + 1));
  REQUIRE(doc.contains(pointer));
  return doc.at(pointer);
}

// The names of a table, each once.
template <class E>
std::vector<std::string> namesOf(const Names<E>& table) {
  std::vector<std::string> out;
  for (const auto& [name, value] : table) {
    out.emplace_back(name);
  }
  return out;
}

std::set<std::string> asSet(const std::vector<std::string>& v) { return {v.begin(), v.end()}; }

// One Names table and where the schema keeps its enum.
struct EnumRow {
  std::string where;
  std::vector<std::string> names;
};

// Every place of the schema ("file#/pointer") that holds `key` ("enum",
// "maximum"), in every file of api/.
void placesOf(const nlohmann::json& node, const std::string& file,
              const nlohmann::json::json_pointer& pointer, const std::string& key,
              std::set<std::string>& out) {
  if (node.is_object()) {
    if (node.contains(key)) {
      out.insert(file + "#" + pointer.to_string());
    }
    for (const auto& [name, child] : node.items()) {
      placesOf(child, file, pointer / name, key, out);
    }
  } else if (node.is_array()) {
    for (std::size_t i = 0; i < node.size(); i++) {
      placesOf(node[i], file, pointer / i, key, out);
    }
  }
}

std::set<std::string> placesOf(const std::string& key) {
  std::set<std::string> out;
  for (const auto& entry : std::filesystem::directory_iterator("api")) {
    if (entry.path().extension() == ".json") {
      const std::string file = entry.path().filename().string();
      placesOf(schemaFile(file), file, nlohmann::json::json_pointer(), key, out);
    }
  }
  return out;
}

// True for a value its enum names: a switch with a case for every
// enumerator and no default, so the build names a new enumerator here
// (-Wswitch, an error), as detail::isGridKind does for the grids. The
// engine writes these by nameOf, which gives "?" for a value its table
// lacks; the case below asks every value for its name.
constexpr bool named(SolveState s) {
  switch (s) {
    case SolveState::Idle:
    case SolveState::Preparing:
    case SolveState::Assembling:
    case SolveState::Draining:
    case SolveState::Paused:
    case SolveState::Finished:
    case SolveState::Error: return true;
  }
  return false;
}
constexpr bool named(SolveStage s) {
  switch (s) {
    case SolveStage::Idle:
    case SolveStage::Prepare:
    case SolveStage::Reduce:
    case SolveStage::Assemble:
    case SolveStage::Disassemble:
    case SolveStage::Stopping:
    case SolveStage::Paused:
    case SolveStage::Done:
    case SolveStage::Error:
    case SolveStage::Other: return true;
  }
  return false;
}
constexpr bool named(ProblemState s) {
  switch (s) {
    case ProblemState::Unsolved:
    case ProblemState::Solving:
    case ProblemState::Solved:
    case ProblemState::Unknown: return true;
  }
  return false;
}
constexpr bool named(ImageFit f) {
  switch (f) {
    case ImageFit::Crop:
    case ImageFit::Stretch:
    case ImageFit::Manual: return true;
  }
  return false;
}
constexpr bool named(BorderCorners c) {
  switch (c) {
    case BorderCorners::Round:
    case BorderCorners::Sharp: return true;
  }
  return false;
}
constexpr bool named(BorderMaterial m) {
  switch (m) {
    case BorderMaterial::Smooth:
    case BorderMaterial::Textured:
    case BorderMaterial::Wood: return true;
  }
  return false;
}
constexpr bool named(mesh::StlParameterType t) {
  switch (t) {
    case mesh::StlParameterType::Double:
    case mesh::StlParameterType::PositiveDouble:
    case mesh::StlParameterType::PositiveInteger:
    case mesh::StlParameterType::Switch: return true;
  }
  return false;
}
constexpr bool named(tilted::Tray::Placement p) {
  switch (p) {
    case tilted::Tray::Placement::Best:
    case tilted::Tray::Placement::Custom: return true;
  }
  return false;
}

// Every value of E (an enum of one byte) has a name in its table, and the
// table names nothing else.
template <class E>
void everyValueNamed(const Names<E>& table) {
  std::size_t values = 0;
  for (unsigned int i = 0; i < 256; i++) {
    const E e = static_cast<E>(i);
    if (named(e)) {
      INFO("value " << i);
      CHECK(nameOf(table, e) != "?");
      values++;
    }
  }
  CHECK(values == table.size());
}

}  // namespace

TEST_CASE("api tables: every Names table is the schema's enum", "[engine][api]") {
  const std::vector<EnumRow> rows = {
      {"types.json#/$defs/Grid", namesOf(gridNames())},
      {"types.json#/$defs/Solver", namesOf(solverNames())},
      {"types.json#/$defs/StackingSolver", namesOf(stackingSolverNames())},
      {"types.json#/$defs/CellTool", namesOf(cellToolNames())},
      {"types.json#/$defs/ShapeTask", namesOf(shapeTaskNames())},
      {"types.json#/$defs/MeshStyle", namesOf(meshStyleNames())},
      {"types.json#/$defs/SortBy", namesOf(sortByNames())},
      {"types.json#/$defs/TrayPlacement", namesOf(trayPlacementNames())},
      {"types.json#/$defs/ImageFit", namesOf(imageFitNames())},
      {"types.json#/$defs/BorderCorners", namesOf(borderCornersNames())},
      {"types.json#/$defs/BorderMaterial", namesOf(borderMaterialNames())},
      {"types.json#/$defs/SolveState", namesOf(solveStateNames())},
      {"types.json#/$defs/SolveStage", namesOf(solveStageNames())},
      {"types.json#/$defs/ProblemState", namesOf(problemStateNames())},
      {"types.json#/$defs/ChangeKind", namesOf(changeKindNames())},
      {"engine.json#/$defs/LogSetting", namesOf(logSettingNames())},
      {"document.json#/methods/document.import/params/properties/format",
       namesOf(importFormatNames())},
      {"problem.json#/methods/problem.setColorRules/params/properties/preset",
       namesOf(colorPresetNames())},
      {"solution.json#/methods/solution.page/params/properties/sortBy",
       namesOf(solutionPageSortNames())},
      {"solution.json#/methods/solution.delete/params/properties/which",
       namesOf(solutionsToDeleteNames())},
      {"solution.json#/methods/solution.toShapes/params/properties/addTo",
       namesOf(assemblyAddToNames())},
      {"solution.json#/methods/solution.analyse/params/properties/which",
       namesOf(analyseWhichNames())},
      {"export.json#/$defs/StlParameter/properties/type", namesOf(stlParameterTypeNames())},
      {"types.json#/$defs/Axes/items", namesOf(axisNames())},
  };
  for (const EnumRow& row : rows) {
    INFO(row.where);
    const nlohmann::json& schema = at(row.where);
    REQUIRE(schema.contains("enum"));
    std::vector<std::string> values;
    for (const nlohmann::json& v : schema.at("enum")) {
      values.push_back(v.get<std::string>());
    }
    // each name once, on both sides, and the same names
    CHECK(asSet(row.names).size() == row.names.size());
    CHECK(asSet(values).size() == values.size());
    CHECK(asSet(row.names) == asSet(values));
  }

  // The schema's other enums: the engine reads none of them. It writes
  // each from one place, a switch or a choice over the core's values, and
  // the contract suite checks a reply that carries one (a new enum of the
  // schema is a row above, or a line here that says where it is written).
  std::set<std::string> rest = placesOf("enum");
  for (const EnumRow& row : rows) {
    CHECK(rest.erase(row.where) == 1);
  }
  const std::set<std::string> written = {
      // numbers, not names: -1 or 1 (methods_problem.cpp)
      "problem.json#/methods/problem.movePart/params/properties/delta",
      // problemKind (methods_problem.cpp)
      "problem.json#/methods/problem.kind/result/properties/kind",
      // the picture's type, the core's (Document::imageData)
      "shape.json#/methods/shape.imageData/result/properties/type",
      // the core's group labels (app::taskSet)
      "shape.json#/methods/shape.taskSet/result/properties/tasks/items/properties/group",
      // solve.preview's source (events.cpp)
      "solve.json#/events/solve.preview/params/properties/source",
      // one value (engine.info's sliding solver, methods_engine.cpp)
      "types.json#/$defs/SlidingSolver",
      // a cell's state (methods_shape.cpp's shape.get, methods_sliding.cpp's tray)
      "types.json#/$defs/CellState",
      // a frame's array types (output.h's FrameBuilder)
      "types.json#/$defs/ArrayRef/properties/type",
      // fitJson, progressJson's stopKind, slidePhaseJson, failureJson, solutionRow (convert.cpp)
      "types.json#/$defs/FitStats/properties/verdict",
      "types.json#/$defs/SolveProgress/properties/stopKind",
      "types.json#/$defs/SolveProgress/properties/slidePhase/oneOf/0/properties/part",
      "types.json#/$defs/SolveFailure/properties/assembler",
      "types.json#/$defs/SolutionRow/properties/disassembly",
  };
  CHECK(rest == written);
}

TEST_CASE("api tables: every value the engine writes by name has one", "[engine][api]") {
  everyValueNamed(solveStateNames());
  everyValueNamed(solveStageNames());
  everyValueNamed(problemStateNames());
  everyValueNamed(imageFitNames());
  everyValueNamed(borderCornersNames());
  everyValueNamed(borderMaterialNames());
  everyValueNamed(stlParameterTypeNames());
  everyValueNamed(trayPlacementNames());
  // the grids' names are GridTraits' rows, one a kind (grid_traits.h)
  CHECK(gridNames().size() == static_cast<std::size_t>(kNumGridKinds));
}

TEST_CASE("api tables: the stored settings' check boxes are SolverSettings' booleans",
          "[engine][api]") {
  std::set<std::string> booleans;
  for (const auto& [name, schema] : at("types.json#/$defs/SolverSettings/properties").items()) {
    if (schema.value("type", "") == "boolean") {
      booleans.insert(name);
    }
  }
  std::vector<std::string> boxes(solverBoxNames().begin(), solverBoxNames().end());
  CHECK(asSet(boxes).size() == boxes.size());
  CHECK(asSet(boxes) == booleans);
}

TEST_CASE("api tables: the schema's maxima are the engine's and the core's bounds",
          "[engine][api]") {
  const std::vector<std::pair<std::string, double>> maxima = {
      // the threads of a solve or an analysis
      {"engine.json#/methods/settings.set/params/properties/threads", kMaxThreads},
      {"engine.json#/$defs/Settings/properties/threads", kMaxThreads},
      {"solution.json#/methods/solution.analyse/params/properties/threads", kMaxThreads},
      {"types.json#/$defs/SolveOptions/properties/threads", kMaxThreads},
      // the settings
      {"engine.json#/methods/settings.set/params/properties/undoDepth", Settings::kMaxUndoDepth},
      {"engine.json#/$defs/Settings/properties/undoDepth", Settings::kMaxUndoDepth},
      {"engine.json#/methods/settings.set/params/properties/slideDiskGB", kMaxDiskBudgetGB},
      {"engine.json#/methods/settings.set/params/properties/panexDiskGB", kMaxDiskBudgetGB},
      {"engine.json#/$defs/Settings/properties/slideDiskGB", kMaxDiskBudgetGB},
      {"engine.json#/$defs/Settings/properties/panexDiskGB", kMaxDiskBudgetGB},
      {"engine.json#/methods/settings.set/params/properties/searchMemoryGB",
       Settings::kMaxSearchMemoryGB},
      {"engine.json#/$defs/Settings/properties/searchMemoryGB", Settings::kMaxSearchMemoryGB},
      // a stacking puzzle's rod sets and discs (cleanup item 11.2)
      {"stacking.json#/$defs/Rules/properties/rodCount", stacking::kMaxRods},
      {"stacking.json#/$defs/Rules/properties/definedHeight", stacking::kMaxHeight},
      {"stacking.json#/$defs/Rules/properties/pocketHeight", stacking::kMaxHeight},
      {"stacking.json#/methods/stacking.setDiscSize/params/properties/size",
       stacking::kMaxDiscSize},
      // a tilted tray
      {"types.json#/$defs/TraySettings/properties/size", tilted::Tray::kMaxSize},
      {"types.json#/$defs/TraySettings/properties/width", tilted::Tray::kMaxSize},
      {"types.json#/$defs/TraySettings/properties/height", tilted::Tray::kMaxSize},
      {"types.json#/$defs/TraySettings/properties/over", tilted::Tray::kMaxSlope},
      {"types.json#/$defs/TraySettings/properties/down", tilted::Tray::kMaxSlope},
      {"types.json#/$defs/TraySettings/properties/allowance", tilted::Tray::kMaxAllowance},
      {"types.json#/$defs/TraySettings/properties/shiftX", tilted::Tray::kMaxShift},
      {"types.json#/$defs/TraySettings/properties/shiftY", tilted::Tray::kMaxShift},
      // a tray's rim
      {"shape.json#/methods/shape.setBorder/params/properties/thickness", kMaxBorderThickness},
      {"types.json#/$defs/TrayBorder/properties/thickness", kMaxBorderThickness},
      // a sliding puzzle's new start/goal shape
      {"sliding.json#/$defs/TraySize/items", app::kMaxStartGoalSide},
      // a solve's take-apart workers
      {"types.json#/$defs/SolveOptions/properties/takeApartWorkers", kMaxTakeApartWorkers},
      // the engine's own bounds (convert.h)
      {"document.json#/methods/document.exportPuzzlecad/params/properties/lip",
       static_cast<double>(kMaxLip)},
      {"playback.json#/methods/playback.poses/params/properties/samplesPerStep",
       static_cast<double>(kMaxSamplesPerStep)},
      {"playback.json#/methods/playback.track/params/properties/samplesPerStep",
       static_cast<double>(kMaxSamplesPerStep)},
      {"solution.json#/methods/solution.page/params/properties/limit",
       static_cast<double>(kMaxPageLimit)},
      {"explore.json#/methods/explore.step/params/properties/steps",
       static_cast<double>(kMaxExploreSteps)},
  };
  for (const auto& [where, most] : maxima) {
    INFO(where);
    const nlohmann::json& schema = at(where);
    REQUIRE(schema.contains("maximum"));
    CHECK(schema.at("maximum").get<double>() == most);
  }

  // The schema's other maxima are no bound of a count or a size (a new
  // maximum of the schema is a row above, or a line here that says why
  // not).
  std::set<std::string> rest = placesOf("maximum");
  for (const auto& [where, most] : maxima) {
    CHECK(rest.erase(where) == 1);
  }
  const std::set<std::string> others = {
      // a step up or down: -1 to 1 (methods_stacking.cpp)
      "stacking.json#/methods/stacking.moveDisc/params/properties/delta",
      // a fraction, 0 to 1 (written, never read)
      "tray.json#/$defs/FitProgress/properties/current",
      "tray.json#/$defs/FitEvent/properties/current",
      // one of the eight ways a picture lies (written, never read: it
      // turns and flips with its shape's cells)
      "types.json#/$defs/ShapeImage/properties/turn",
  };
  CHECK(rest == others);
}

TEST_CASE("api tables: the error codes are api/errors.json's", "[engine][api]") {
  // (code, errc) of every row, and of the core's alone
  std::set<std::pair<int, std::string>> rows;
  std::set<std::pair<int, std::string>> core;
  for (const nlohmann::json& row : schemaFile("errors.json").at("errors")) {
    const std::pair<int, std::string> r{row.at("code").get<int>(), row.at("errc").get<std::string>()};
    CHECK(rows.insert(r).second);
    if (!row.at("protocol").get<bool>()) {
      core.insert(r);
    }
  }
  // the protocol's, as the engine writes them
  CHECK(rows.contains({kParseErrorCode, "parse_error"}));
  CHECK(rows.contains({kInvalidRequestCode, "invalid_request"}));
  CHECK(rows.contains({kMethodNotFoundCode, "method_not_found"}));
  const RpcError params = invalidParams("");
  CHECK(rows.contains({params.code, params.errc}));
  CHECK_FALSE(core.contains({params.code, params.errc}));
  // the core's: toRpc of every Errc, each a row of its own
  std::set<std::pair<int, std::string>> mapped;
  for (const Errc e : {Errc::InvalidArgument, Errc::NotFound, Errc::ParseError, Errc::IoError,
                       Errc::Unsupported, Errc::InvalidState, Errc::Internal}) {
    const RpcError r = toRpc(Error{e, ""});
    CHECK(mapped.insert({r.code, r.errc}).second);
  }
  CHECK(mapped == core);
}

// SPDX-License-Identifier: GPL-3.0-or-later
// The engine's tables against the API schema (cleanup item 11.1): every
// Names table against the schema's enum, the stored solver settings' check
// boxes against SolverSettings, the numeric maxima against the core's
// constants, and the error codes against api/errors.json. The contract
// suite checks what the engine sends; these check what it accepts too,
// whether or not a test happens to send it.
#include "convert.h"
#include "engine.h"
#include "rpc.h"

#include "shapelogic/base/system.h"
#include "shapelogic/base/thread_pool.h"
#include "shapelogic/model/stacking_types.h"
#include "shapelogic/model/tilted_tray.h"

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
  };
  for (const auto& [where, most] : maxima) {
    INFO(where);
    const nlohmann::json& schema = at(where);
    REQUIRE(schema.contains("maximum"));
    CHECK(schema.at("maximum").get<double>() == most);
  }
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

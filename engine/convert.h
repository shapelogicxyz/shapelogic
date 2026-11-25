// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The engine's names for the core's values and the JSON of the items it
/// returns (api/types.json): shapes, colors, problems, the snapshot, the
/// status rows, meshes as frames, solve options, progress and solutions.

#include "engine.h"

#include "shapelogic/app/queries.h"
#include "shapelogic/base/solve_state.h"
#include "shapelogic/mesh/draw_mesh.h"
#include "shapelogic/mesh/stl.h"
#include "shapelogic/model/assembly.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/solution.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/solve/solve_progress.h"
#include "shapelogic/solve/solve_session.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bt::engine {

template <class E>
using Names = std::vector<std::pair<std::string_view, E>>;

/// The names of the schema's enums, each value once: the API's own, apart
/// from the file format's names (imageFitName and the like), so renaming
/// one changes neither. Every one the engine reads or writes is here, and
/// tests/unit/engine/test_api_tables.cpp holds each to the schema's enum.
[[nodiscard]] const Names<GridKind>& gridNames();
[[nodiscard]] const Names<SolverType>& solverNames();
[[nodiscard]] const Names<stacking::StackingSolver>& stackingSolverNames();
[[nodiscard]] const Names<app::CellTool>& cellToolNames();
[[nodiscard]] const Names<app::ShapeTask>& shapeTaskNames();
[[nodiscard]] const Names<mesh::MeshStyle>& meshStyleNames();
[[nodiscard]] const Names<SolutionOrder>& sortByNames();
[[nodiscard]] const Names<tilted::Tray::Placement>& trayPlacementNames();
[[nodiscard]] const Names<ImageFit>& imageFitNames();
[[nodiscard]] const Names<BorderCorners>& borderCornersNames();
[[nodiscard]] const Names<BorderMaterial>& borderMaterialNames();
[[nodiscard]] const Names<LogSetting>& logSettingNames();
[[nodiscard]] const Names<app::ImportFormat>& importFormatNames();
[[nodiscard]] const Names<app::ColorPreset>& colorPresetNames();
/// solution.page's sortBy: -1 the stored order, else Problem::sortSolutions'
/// number.
[[nodiscard]] const Names<int>& solutionPageSortNames();
[[nodiscard]] const Names<app::SolutionsToDelete>& solutionsToDeleteNames();
[[nodiscard]] const Names<app::AssemblyImport::AddTo>& assemblyAddToNames();
[[nodiscard]] const Names<app::AnalyseWhich>& analyseWhichNames();
[[nodiscard]] const Names<mesh::StlParameterType>& stlParameterTypeNames();
[[nodiscard]] const Names<SolveState>& solveStateNames();
[[nodiscard]] const Names<SolveStage>& solveStageNames();
[[nodiscard]] const Names<ProblemState>& problemStateNames();
/// document.changed's `what`: a ChangeSet bit each.
[[nodiscard]] const Names<unsigned int>& changeKindNames();
/// SolverSettings' check boxes (types.json): stored "1" or "0", sent as
/// true or false.
[[nodiscard]] const std::vector<std::string_view>& solverBoxNames();
/// A Tilted Square puzzle's tray (types.json Tray), or null.
[[nodiscard]] Json trayJson(const app::Document& d);
/// A tray and its board (types.json Tray).
[[nodiscard]] Json trayJson(const tilted::Tray& t, const tilted::Layout& lay);
/// Where a fit of a tray's placements stands (tray.json FitProgress without
/// `active`: the fields a tray.fitProgress event and the status share).
[[nodiscard]] Json placementFitJson(const app::PlacementFitProgress& f);
/// A tray's settings read from `given` (types.json TraySettings) over
/// `base`: the slope in lowest terms, a new size keeping the walls inside
/// it. An error when `given` is no object, holds a field it should not, or
/// has down above over.
[[nodiscard]] RpcResult<tilted::Tray> readTraySettings(const Json& given, const tilted::Tray& base);

/// The name of a value in a table.
template <class E>
[[nodiscard]] std::string_view nameOf(const Names<E>& names, E value) {
  for (const auto& [n, e] : names) {
    if (e == value) {
      return n;
    }
  }
  return "?";
}

[[nodiscard]] std::string_view solveStateName(SolveState s);
[[nodiscard]] std::string_view solveStageName(SolveStage s);
[[nodiscard]] std::string_view problemStateName(ProblemState s);

/// "#rrggbb".
[[nodiscard]] std::string rgbText(Rgb c);
[[nodiscard]] std::optional<Rgb> parseRgb(std::string_view text);
/// A color the core keeps as 0xrrggbb.
[[nodiscard]] Rgb rgbOf(std::uint32_t packed);
/// A color field that may be null (none): read from `name`, as 0xrrggbb;
/// a value that is neither fails the params.
[[nodiscard]] std::optional<std::uint32_t> readRgbOrNull(Params& p, std::string_view name);

/// The most shapes one call names (a Puzzlecad export, STL file names).
inline constexpr std::size_t kMaxShapeList = 65536;
/// A list of 1 to kMaxShapeList shape ids, read from `list`; what is no
/// such list fails the params (and gives none).
[[nodiscard]] std::vector<app::Id> readShapeIds(Params& p, const Json& list);
/// The most bytes of a name that goes into an exported file (a comment
/// line, an STL solid's name).
inline constexpr std::size_t kMaxFileName = 255;
/// Refuses such a name when it is too long or holds control characters.
[[nodiscard]] RpcResult<void> checkFileName(const std::string& name);

/// The problem a play call names, on a puzzle that `isKind` says the play
/// is for; Unsupported, `refusal`, on any other.
[[nodiscard]] RpcResult<const Problem*> playProblem(const State& s, app::Id problem,
                                                    bool (*isKind)(const Puzzle&),
                                                    std::string_view refusal);
/// A replay's reply (sliding.playReplay, stacking.playReplay): whether it
/// starts solved, the steps made, and the move that could not be made and
/// why (null when every one was).
[[nodiscard]] Json replayJson(bool solved, Json steps, const std::string& error);

/// The ids of the document's items, as the API writes them.
[[nodiscard]] std::string shapeId(const State& s, unsigned int index);
[[nodiscard]] std::string colorId(const State& s, unsigned int index);
[[nodiscard]] std::string problemId(const State& s, unsigned int index);
[[nodiscard]] std::string rodSetId(const State& s, unsigned int index);

/// The items (api/types.json).
[[nodiscard]] Json shapeSummary(const State& s, unsigned int index);
/// A rod set's rules as the original's fields showed them (loadRodFields,
/// syncPanexFields): what rodSetJson sends and what stacking.setRules
/// keeps of a field it is not given.
[[nodiscard]] stacking::RodSet rodSetFields(const stacking::RodSet& r);
/// A stacking puzzle's rod set (RodSet): its id, label, name and rules as
/// the original's fields showed them.
[[nodiscard]] Json rodSetJson(const State& s, unsigned int index);
/// A stacking (Stacks): each rod's discs, bottom to top.
[[nodiscard]] Json stacksJson(const State& s, const stacking::StackMap& map);
/// Where rods and discs stand (BoardLayout); `pieces` names the discs of
/// `disks` (none for a rod set alone).
[[nodiscard]] Json layoutJson(const State& s, const stacking::BoardLayout& lay,
                              const std::vector<stacking::Disc>& pieces);
[[nodiscard]] Json colorJson(const State& s, unsigned int index);
[[nodiscard]] Json problemSummary(const State& s, unsigned int index);
[[nodiscard]] Json problemDetail(const State& s, unsigned int index);
[[nodiscard]] Json snapshot(const State& s);
/// The snapshot of a document just opened, with what opening it changed
/// (Document::openNotice) when there is something to tell.
[[nodiscard]] Json openedSnapshot(const State& s);
[[nodiscard]] Json shapeInfoJson(const State& s, const app::ShapeInfo& info, unsigned int index);
[[nodiscard]] Json fitJson(const app::FitStats& f);
/// A mesh: sent as a frame now, described for the reply.
[[nodiscard]] Json meshReply(State& s, const mesh::DrawMesh& m);

/// The Solver tab's stored settings, both ways (the original's names and
/// "1" / "0" for the check boxes).
[[nodiscard]] Json solverSettingsJson(const std::map<std::string, std::string>& o);
[[nodiscard]] std::optional<std::map<std::string, std::string>> solverSettingsFrom(
    const Json& j, std::string& error);

/// The options of a solve: the request's (null: none) over the settings.
[[nodiscard]] RpcResult<SolveOptions> solveOptionsFrom(const State& s, const Json& j);
/// Sets the stacking solver a request (`given`, its options) did not name:
/// the one the original's menu offered for the problem's board
/// (syncSolverTypeMenu), the Panex Solver for Panex columns, else the
/// Stacking Solver. The problem is the document's.
void defaultStackingSolver(const State& s, app::Id problem, const Json& given,
                           SolveOptions& options);
[[nodiscard]] Json progressJson(const SolveProgress& p);
/// A slide search's forecast (sliding::slideForecast) from its moves'
/// times; null when there are none.
[[nodiscard]] Json slideForecastJson(const std::vector<std::uint64_t>& levelMs,
                                     std::uint64_t runningMs);
[[nodiscard]] Json statsJson(const SolveStats& t);
[[nodiscard]] Json failureJson(const SolveFailure& f);
[[nodiscard]] Json takeApartJson(const DisassemblyProgress& d);
/// A saved solution's row (solution.page) and the placements of an assembly.
[[nodiscard]] Json solutionRow(const Solution& sol, unsigned int index, bool stackingPuzzle);
[[nodiscard]] Json placementsJson(const Assembly& a);
/// The same for placements a search's picture holds (SearchSnapshot).
[[nodiscard]] Json placementsJson(const std::vector<Placement>& placements);

}  // namespace bt::engine

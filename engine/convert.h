// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The engine's names for the core's values and the JSON of the items it
/// returns (api/types.json): shapes, colours, problems, the snapshot, the
/// status rows, meshes as frames, solve options, progress and solutions.

#include "engine.h"

#include "bt/app/queries.h"
#include "bt/base/solve_state.h"
#include "bt/mesh/draw_mesh.h"
#include "bt/model/assembly.h"
#include "bt/model/problem.h"
#include "bt/model/solution.h"
#include "bt/model/puzzle.h"
#include "bt/solve/solve_progress.h"
#include "bt/solve/solve_session.h"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bt::engine {

template <class E>
using Names = std::vector<std::pair<std::string_view, E>>;

/// The names of the schema's enums, each value once.
[[nodiscard]] const Names<GridKind>& gridNames();
[[nodiscard]] const Names<SolverType>& solverNames();
[[nodiscard]] const Names<sliding::SlidingSolver>& slidingSolverNames();
[[nodiscard]] const Names<app::CellTool>& cellToolNames();
[[nodiscard]] const Names<app::ShapeTask>& shapeTaskNames();
[[nodiscard]] const Names<mesh::MeshStyle>& meshStyleNames();
[[nodiscard]] const Names<SolutionOrder>& sortByNames();

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

/// The ids of the document's items, as the API writes them.
[[nodiscard]] std::string shapeId(const State& s, unsigned int index);
[[nodiscard]] std::string colourId(const State& s, unsigned int index);
[[nodiscard]] std::string problemId(const State& s, unsigned int index);

/// The items (api/types.json).
[[nodiscard]] Json shapeSummary(const State& s, unsigned int index);
[[nodiscard]] Json colourJson(const State& s, unsigned int index);
[[nodiscard]] Json problemSummary(const State& s, unsigned int index);
[[nodiscard]] Json problemDetail(const State& s, unsigned int index);
[[nodiscard]] Json snapshot(const State& s);
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
[[nodiscard]] Json progressJson(const SolveProgress& p);
[[nodiscard]] Json statsJson(const SolveStats& t);
[[nodiscard]] Json failureJson(const SolveFailure& f);
[[nodiscard]] Json takeApartJson(const DisassemblyProgress& d);
/// A saved solution's row (solution.page) and the placements of an assembly.
[[nodiscard]] Json solutionRow(const Solution& sol, unsigned int index);
[[nodiscard]] Json placementsJson(const Assembly& a);

}  // namespace bt::engine

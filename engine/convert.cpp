// SPDX-License-Identifier: GPL-3.0-or-later
#include "convert.h"

#include "bt/app/shape_tools.h"
#include "bt/base/thread_pool.h"
#include "bt/grid/voxel.h"

#include <array>
#include <charconv>
#include <cstdio>

namespace bt::engine {

// ---- names ------------------------------------------------------------------------------

const Names<GridKind>& gridNames() {
  static const Names<GridKind> n = {
      {"bricks", GridKind::Bricks},     {"prisms", GridKind::TriangularPrism},
      {"spheres", GridKind::Spheres},   {"rhombic", GridKind::Rhombic},
      {"tetraOcta", GridKind::TetraOcta}, {"sliding", GridKind::Sliding},
      {"stacking", GridKind::Stacking},
  };
  return n;
}

const Names<SolverType>& solverNames() {
  static const Names<SolverType> n = {
      {"classic", SolverType::Classic}, {"crowell", SolverType::Crowell}, {"bt2", SolverType::Bt2}};
  return n;
}

const Names<app::CellTool>& cellToolNames() {
  static const Names<app::CellTool> n = {{"fixed", app::CellTool::Fixed},
                                         {"variable", app::CellTool::Variable},
                                         {"erase", app::CellTool::Erase},
                                         {"paint", app::CellTool::Paint}};
  return n;
}

const Names<app::ShapeTask>& shapeTaskNames() {
  using T = app::ShapeTask;
  static const Names<T> n = {
      {"nudge0", T::Nudge0},
      {"nudge1", T::Nudge1},
      {"nudge2", T::Nudge2},
      {"nudge3", T::Nudge3},
      {"nudge4", T::Nudge4},
      {"nudge5", T::Nudge5},
      {"rotate6", T::Rotate6},
      {"rotate7", T::Rotate7},
      {"rotate8", T::Rotate8},
      {"rotate9", T::Rotate9},
      {"rotate10", T::Rotate10},
      {"rotate11", T::Rotate11},
      {"flip12", T::Flip12},
      {"flip13", T::Flip13},
      {"flip14", T::Flip14},
      {"prune", T::Prune},
      {"insideFixed", T::InsideFixed},
      {"outsideFixed", T::OutsideFixed},
      {"insideVariable", T::InsideVariable},
      {"outsideVariable", T::OutsideVariable},
      {"insideDecolour", T::InsideDecolour},
      {"outsideDecolour", T::OutsideDecolour},
      {"double", T::Double},
      {"triple", T::Triple},
      {"toOrigin", T::ToOrigin},
      {"centre", T::Centre},
      {"minimize", T::Minimize},
      {"nudge27", T::Nudge27},
      {"nudge28", T::Nudge28},
      {"nudge29", T::Nudge29},
      {"nudge30", T::Nudge30},
      {"nudge31", T::Nudge31},
      {"nudge32", T::Nudge32},
      {"fillHoles", T::FillHoles},
      {"gridScale", T::GridScale},
  };
  return n;
}

const Names<mesh::MeshStyle>& meshStyleNames() {
  static const Names<mesh::MeshStyle> n = {{"drawing", mesh::MeshStyle::Drawing},
                                           {"wireframe", mesh::MeshStyle::Wireframe},
                                           {"flat", mesh::MeshStyle::Flat}};
  return n;
}

const Names<SolutionOrder>& sortByNames() {
  static const Names<SolutionOrder> n = {{"unsorted", SolutionOrder::Unsorted},
                                         {"moves", SolutionOrder::Moves},
                                         {"level", SolutionOrder::Level},
                                         {"rotations", SolutionOrder::Rotations}};
  return n;
}

std::string_view solveStateName(SolveState s) { return toString(s); }

std::string_view solveStageName(SolveStage s) {
  switch (s) {
    case SolveStage::Idle: return "idle";
    case SolveStage::Prepare: return "prepare";
    case SolveStage::Reduce: return "reduce";
    case SolveStage::Assemble: return "assemble";
    case SolveStage::Disassemble: return "disassemble";
    case SolveStage::Stopping: return "stopping";
    case SolveStage::Paused: return "paused";
    case SolveStage::Done: return "done";
    case SolveStage::Error: return "error";
  }
  return "idle";
}

std::string_view problemStateName(ProblemState s) {
  switch (s) {
    case ProblemState::Unsolved: return "unsolved";
    case ProblemState::Solving: return "solving";
    case ProblemState::Solved: return "solved";
    case ProblemState::Unknown: return "unknown";
  }
  return "unknown";
}

std::string rgbText(Rgb c) {
  std::array<char, 8> buf{};
  std::snprintf(buf.data(), buf.size(), "#%02x%02x%02x", c.r, c.g, c.b);
  return buf.data();
}

std::optional<Rgb> parseRgb(std::string_view text) {
  if (text.size() != 7 || text[0] != '#') {
    return std::nullopt;
  }
  unsigned int v = 0;
  const auto [end, ec] = std::from_chars(text.data() + 1, text.data() + 7, v, 16);
  if (ec != std::errc{} || end != text.data() + 7) {
    return std::nullopt;
  }
  return Rgb{static_cast<std::uint8_t>(v >> 16), static_cast<std::uint8_t>(v >> 8),
             static_cast<std::uint8_t>(v)};
}

// ---- items ------------------------------------------------------------------------------

std::string shapeId(const State& s, unsigned int index) {
  return formatId(IdKind::Shape, s.document().shapeIds()[index]);
}

std::string colourId(const State& s, unsigned int index) {
  return formatId(IdKind::Colour, s.document().colourIds()[index]);
}

std::string problemId(const State& s, unsigned int index) {
  return formatId(IdKind::Problem, s.document().problemIds()[index]);
}

namespace {

Json shapeColour(const Voxel& v) {
  const int c = v.getShapeColor();
  if (c < 0) {
    return nullptr;
  }
  const auto u = static_cast<unsigned int>(c);
  return rgbText(Rgb{static_cast<std::uint8_t>(u >> 16), static_cast<std::uint8_t>(u >> 8),
                     static_cast<std::uint8_t>(u)});
}

Json optionalCount(bool known, std::uint64_t value) {
  return known ? Json(value) : Json(nullptr);
}

// The fields a problem's summary and its detail share.
void problemFields(const State& s, unsigned int index, const Problem& pr, Json& j) {
  const app::Document& d = s.document();
  j["id"] = problemId(s, index);
  j["label"] = app::Document::problemLabel(index);
  j["name"] = pr.getName();
  j["result"] = pr.resultValid() ? Json(shapeId(s, pr.getResultId())) : Json(nullptr);
  j["pieces"] = pr.getNumberOfPieces();
  j["state"] = problemStateName(pr.getSolveState());
  j["solve"] = solveStateName(d.solveState(d.problemIds()[index]));
  j["analysing"] = d.analysing(d.problemIds()[index]);
  j["assemblies"] = optionalCount(pr.numAssembliesKnown(), pr.numAssembliesKnown() ? pr.getNumAssemblies() : 0);
  j["solutions"] = optionalCount(pr.numSolutionsKnown(), pr.numSolutionsKnown() ? pr.getNumSolutions() : 0);
  j["saved"] = pr.getNumberOfSavedSolutions();
  j["timeMs"] = optionalCount(pr.usedTimeKnown(), pr.usedTimeKnown() ? pr.getUsedMs() : 0);
}

}  // namespace

Json shapeSummary(const State& s, unsigned int index) {
  const Voxel& v = s.document().puzzle().getShape(index);
  Json j = Json::object();
  j["id"] = shapeId(s, index);
  j["label"] = app::Document::shapeLabel(index);
  j["name"] = v.getName();
  j["size"] = Json::array({v.getX(), v.getY(), v.getZ()});
  j["fixed"] = v.countState(Voxel::kFilled);
  j["variable"] = v.countState(Voxel::kVariable);
  j["weight"] = v.getWeight();
  j["colour"] = shapeColour(v);
  return j;
}

Json colourJson(const State& s, unsigned int index) {
  Json j = Json::object();
  j["id"] = colourId(s, index);
  j["label"] = app::Document::colourLabel(index);
  j["rgb"] = rgbText(s.document().puzzle().getColor(index));
  return j;
}

Json problemSummary(const State& s, unsigned int index) {
  return s.document().readProblem(index, [&s, index](const Problem& pr) {
    Json j = Json::object();
    problemFields(s, index, pr, j);
    return j;
  });
}

Json problemDetail(const State& s, unsigned int index) {
  return s.document().readProblem(index, [&s, index](const Problem& pr) {
    Json j = Json::object();
    problemFields(s, index, pr, j);
    Json parts = Json::array();
    for (const Part& part : pr.getParts()) {
      Json groups = Json::array();
      for (const PartGroup& g : part.groups) {
        groups.push_back(Json{{"group", g.group}, {"count", g.count}});
      }
      parts.push_back(Json{{"shape", shapeId(s, part.shapeId)},
                           {"min", part.min},
                           {"max", part.max},
                           {"groups", std::move(groups)}});
    }
    j["parts"] = std::move(parts);
    Json rules = Json::array();
    for (const std::uint32_t c : pr.getColorConstraints()) {
      const unsigned int pc = c >> 16;
      const unsigned int rc = c & 0xFFFFU;
      if (pc < s.document().colourIds().size() && rc < s.document().colourIds().size()) {
        rules.push_back(Json{{"piece", colourId(s, pc)}, {"result", colourId(s, rc)}});
      }
    }
    j["colourRules"] = std::move(rules);
    j["maxHoles"] = pr.maxHolesDefined() ? Json(pr.getMaxHoles()) : Json(nullptr);
    j["savedPosition"] =
        pr.getSolveState() == ProblemState::Solving && !pr.getAssemblerState().empty();
    j["solverSettings"] = solverSettingsJson(pr.getSolverOptions());
    return j;
  });
}

Json snapshot(const State& s) {
  const app::Document& d = s.document();
  const Puzzle& p = d.puzzle();
  Json j = Json::object();
  j["revision"] = s.revision();
  j["dirty"] = d.dirty();
  j["grid"] = nameOf(gridNames(), p.getGridType().getType());
  j["path"] = s.path ? Json(s.path->string()) : Json(nullptr);
  j["notes"] = p.getComment();
  j["canUndo"] = d.canUndo();
  j["canRedo"] = d.canRedo();
  j["solving"] = d.solving();
  Json shapes = Json::array();
  for (unsigned int i = 0; i < p.getNumberOfShapes(); i++) {
    shapes.push_back(shapeSummary(s, i));
  }
  j["shapes"] = std::move(shapes);
  Json colours = Json::array();
  for (unsigned int i = 0; i < p.colorNumber(); i++) {
    colours.push_back(colourJson(s, i));
  }
  j["colours"] = std::move(colours);
  Json problems = Json::array();
  for (unsigned int i = 0; i < p.getNumberOfProblems(); i++) {
    problems.push_back(problemSummary(s, i));
  }
  j["problems"] = std::move(problems);
  return j;
}

Json shapeInfoJson(const State& s, const app::ShapeInfo& info, unsigned int index) {
  const auto earlier = [&s](const std::optional<unsigned int>& i) {
    return i ? Json(shapeId(s, *i)) : Json(nullptr);
  };
  const auto maybe = [](const std::optional<bool>& b) { return b ? Json(*b) : Json(nullptr); };
  Json j = Json::object();
  j["shape"] = shapeId(s, index);
  j["fixed"] = info.fixed;
  j["variable"] = info.variable;
  j["identicalMirror"] = earlier(info.identicalMirror);
  j["identicalShape"] = earlier(info.identicalShape);
  j["identicalComplete"] = earlier(info.identicalComplete);
  j["connectedFace"] = info.connectedFace;
  j["connectedEdge"] = info.connectedEdge;
  j["connectedCorner"] = info.connectedCorner;
  j["holes2D"] = info.holes2D;
  j["holes3D"] = info.holes3D;
  j["notchable"] = maybe(info.notchable);
  j["millable"] = maybe(info.millable);
  j["symmetry"] = info.symmetry ? Json(*info.symmetry) : Json(nullptr);
  j["statusLine"] = app::shapeStatusLine(s.document().puzzle(), index);
  return j;
}

Json fitJson(const app::FitStats& f) {
  std::string_view verdict = "noResult";
  switch (f.verdict) {
    case app::FitVerdict::NoResult: verdict = "noResult"; break;
    case app::FitVerdict::Fits: verdict = "fits"; break;
    case app::FitVerdict::TooLarge: verdict = "tooLarge"; break;
    case app::FitVerdict::TooSmall: verdict = "tooSmall"; break;
  }
  Json j = Json::object();
  j["verdict"] = verdict;
  j["resultMin"] = f.resultMin;
  j["resultMax"] = f.resultMax;
  j["piecesMin"] = f.piecesMin;
  j["piecesMax"] = f.piecesMax;
  j["cellsMin"] = f.cellsMin;
  j["cellsMax"] = f.cellsMax;
  j["holesMin"] = f.holesMin;
  j["holesMax"] = f.holesMax;
  j["maxHoles"] = f.maxHoles ? Json(*f.maxHoles) : Json(nullptr);
  j["statusLine"] = f.statusLine;
  return j;
}

Json meshReply(State& s, const mesh::DrawMesh& m) {
  FrameBuilder f;
  f.add("positions", m.positions);
  f.add("normals", m.normals);
  f.add("indices", m.indices);
  f.add("voxel", m.voxel);
  f.add("voxelFace", m.voxelFace);
  f.add("flags", m.flags);
  f.add("colour", m.colour);
  f.add("polygon", m.polygon);
  f.add("edgeLines", m.edgeLines);
  Json j = Json::object();
  j["frame"] = s.sendFrame(f);
  j["triangles"] = m.triangleCount();
  j["vertices"] = m.vertexCount();
  j["lo"] = Json::array({m.lo[0], m.lo[1], m.lo[2]});
  j["hi"] = Json::array({m.hi[0], m.hi[1], m.hi[2]});
  return j;
}

// ---- the stored solver settings ---------------------------------------------------------

namespace {

// The original's check boxes (mainwindow_solve.cpp's optionNames).
constexpr std::array<std::string_view, 11> kSolverBoxes = {
    "disassemble", "checkRotations", "justCount", "justLevels", "deepSymmetry", "keepMirrors",
    "keepRotations", "strictColors", "nestedSlides", "highMemory", "autosave"};

bool isBox(std::string_view name) {
  for (const std::string_view b : kSolverBoxes) {
    if (b == name) {
      return true;
    }
  }
  return false;
}

}  // namespace

Json solverSettingsJson(const std::map<std::string, std::string>& o) {
  Json j = Json::object();
  for (const auto& [k, v] : o) {
    if (isBox(k)) {
      j[k] = v == "1";
    } else {
      j[k] = v;
    }
  }
  return j;
}

std::optional<std::map<std::string, std::string>> solverSettingsFrom(const Json& j,
                                                                    std::string& error) {
  if (!j.is_object()) {
    error = "'settings' must be an object";
    return std::nullopt;
  }
  std::map<std::string, std::string> o;
  for (const auto& [k, v] : j.items()) {
    if (isBox(k)) {
      if (!v.is_boolean()) {
        error = "'settings." + k + "' must be true or false";
        return std::nullopt;
      }
      o[k] = v.get<bool>() ? "1" : "0";
    } else {
      if (!v.is_string()) {
        error = "'settings." + k + "' must be a string";
        return std::nullopt;
      }
      o[k] = v.get<std::string>();
    }
  }
  return o;
}

// ---- solving ----------------------------------------------------------------------------

RpcResult<SolveOptions> solveOptionsFrom(const State& s, const Json& j) {
  SolveOptions o;
  o.solver = s.settings.solver;
  o.threads = s.settings.threads;
  if (j.is_null()) {
    return o;
  }
  if (!j.is_object()) {
    return std::unexpected(invalidParams("'options' must be an object"));
  }
  Params p(j);
  o.solver = p.optChoice("solver", solverNames()).value_or(o.solver);
  o.disassemble = p.optBoolean("disassemble", o.disassemble);
  o.checkRotations = p.optBoolean("checkRotations", o.checkRotations);
  o.justCount = p.optBoolean("justCount", o.justCount);
  o.dropDisassemblies = p.optBoolean("justLevels", o.dropDisassemblies);
  o.matrix.keepMirrors = p.optBoolean("keepMirrors", o.matrix.keepMirrors);
  o.matrix.keepRotations = p.optBoolean("keepRotations", o.matrix.keepRotations);
  o.matrix.completeRotations = p.optBoolean("completeRotations", o.matrix.completeRotations);
  o.matrix.strictColors = p.optBoolean("strictColours", o.matrix.strictColors);
  o.reduce = p.optBoolean("reduce", o.reduce);
  o.order = p.optChoice("sortBy", sortByNames()).value_or(o.order);
  o.solutionLimit = static_cast<unsigned int>(p.optInteger("keepAtMost", o.solutionLimit, 0, 0xFFFFFFFF));
  o.solutionDrop = static_cast<unsigned int>(p.optInteger("keepEach", o.solutionDrop, 0, 0xFFFFFFFF));
  o.threads = static_cast<unsigned int>(p.optInteger("threads", o.threads, 0, kMaxThreads));
  o.takeApartWorkers = static_cast<unsigned int>(p.optInteger("takeApartWorkers", 0, 0, 64));
  if (RpcResult<void> r = p.finish(); !r) {
    RpcError e = r.error();
    e.message = "options: " + e.message;
    return std::unexpected(e);
  }
  return o;
}

Json takeApartJson(const DisassemblyProgress& d) {
  Json j = Json::object();
  j["elapsedMs"] = d.elapsedMs;
  j["pieces"] = d.pieces;
  j["searchPieces"] = d.searchPieces;
  j["separations"] = d.separations;
  j["depth"] = d.depth;
  j["level"] = d.level;
  j["levelDone"] = d.levelDone;
  j["levelSize"] = d.levelSize;
  j["nextLevelSize"] = d.nextLevelSize;
  j["nodes"] = d.nodes;
  j["threads"] = d.threads;
  return j;
}

Json progressJson(const SolveProgress& g) {
  Json j = Json::object();
  j["stage"] = solveStageName(g.stage);
  j["elapsedMs"] = g.elapsedMs;
  j["piece"] = g.piece;
  j["pieces"] = g.pieces;
  j["assemblyFraction"] = g.assemblyFraction;
  j["iterations"] = g.iterations;
  j["assemblerThreads"] = g.assemblerThreads;
  j["assemblies"] = g.assemblies;
  j["solutions"] = g.solutions;
  j["disassembly"] = g.disassembly;
  j["disasmCompleted"] = g.disasmCompleted;
  j["disasmPending"] = g.disasmPending;
  j["disasmWorkers"] = g.disasmWorkers;
  j["overall"] = g.overall;
  j["overallKnown"] = g.overallKnown;
  j["levelFraction"] = g.levelFraction;
  j["secondsLeft"] = g.secondsLeft < 0 ? Json(nullptr) : Json(g.secondsLeft);
  j["activity"] = g.activity();
  return j;
}

Json statsJson(const SolveStats& t) {
  Json running = Json::array();
  for (const DisassemblyProgress& d : t.running) {
    running.push_back(takeApartJson(d));
  }
  Json j = Json::object();
  j["state"] = solveStateName(t.state);
  j["solver"] = nameOf(solverNames(), t.solver);
  j["disassemblyEnabled"] = t.disassemblyEnabled;
  j["rotationsEnabled"] = t.rotationsEnabled;
  j["hardwareThreads"] = t.hardwareThreads;
  j["assemblerThreads"] = t.assemblerThreads;
  j["disasmWorkers"] = t.disasmWorkers;
  j["assembliesFound"] = t.assembliesFound;
  j["solutionsFound"] = t.solutionsFound;
  j["inseparable"] = t.inseparable;
  j["iterations"] = t.iterations;
  j["assemblyProgress"] = t.assemblyProgress;
  j["pending"] = t.pending;
  j["peakPending"] = t.peakPending;
  j["disasmCompleted"] = t.disasmCompleted;
  j["elapsedMs"] = t.elapsedMs;
  j["prepareMs"] = t.prepareMs;
  j["reduceMs"] = t.reduceMs;
  j["assemblyMs"] = t.assemblyMs;
  j["disasmWorkMs"] = t.disasmWorkMs;
  j["linearSearchMs"] = t.linearSearchMs;
  j["rotationSearchMs"] = t.rotationSearchMs;
  j["drainMs"] = t.drainMs;
  j["avgDisasmSeconds"] = t.avgDisasmSeconds;
  j["running"] = std::move(running);
  return j;
}

Json failureJson(const SolveFailure& f) {
  Json assembler = nullptr;
  switch (f.assembler) {
    case AssemblerError::None: break;
    case AssemblerError::TooManyUnits: assembler = "tooManyUnits"; break;
    case AssemblerError::TooFewUnits: assembler = "tooFewUnits"; break;
    case AssemblerError::CannotPlace: assembler = "cannotPlace"; break;
    case AssemblerError::CannotRestoreVersion: assembler = "cannotRestoreVersion"; break;
    case AssemblerError::CannotRestoreSyntax: assembler = "cannotRestoreSyntax"; break;
    case AssemblerError::CannotRestoreInterrupted: assembler = "cannotRestoreInterrupted"; break;
    case AssemblerError::PuzzleUnhandable: assembler = "puzzleUnhandable"; break;
  }
  Json j = Json::object();
  j["assembler"] = std::move(assembler);
  j["param"] = f.param;
  j["message"] = f.message;
  j["file"] = f.file;
  j["line"] = f.line;
  return j;
}

// ---- solutions --------------------------------------------------------------------------

Json solutionRow(const Solution& sol, unsigned int index) {
  unsigned int placed = 0;
  for (const Placement& pl : sol.getAssembly().getPlacements()) {
    placed += pl.transformation != kUnplacedTrans ? 1U : 0U;
  }
  const Disassembly* d = sol.getDisassemblyInfo();
  const std::string_view kind = sol.getDisassembly() != nullptr   ? "tree"
                                : sol.getSeparationInfo() != nullptr ? "summary"
                                                                     : "none";
  Json j = Json::object();
  j["id"] = formatId(IdKind::Solution, sol.getId());
  j["index"] = index;
  j["assembly"] = sol.getAssemblyNumber();
  j["solution"] = sol.getSolutionNumber();
  j["pieces"] = placed;
  j["disassembly"] = kind;
  j["moves"] = d != nullptr ? Json(d->sumMoves()) : Json(nullptr);
  j["rotations"] = d != nullptr ? Json(d->sumRotations()) : Json(nullptr);
  j["steps"] = d != nullptr ? Json(d->sumSteps()) : Json(nullptr);
  j["movesText"] = d != nullptr ? Json(d->movesText()) : Json(nullptr);
  return j;
}

Json placementsJson(const Assembly& a) {
  Json out = Json::array();
  const std::vector<Placement>& pl = a.getPlacements();
  for (unsigned int i = 0; i < pl.size(); i++) {
    const bool placed = pl[i].transformation != kUnplacedTrans;
    out.push_back(Json{{"piece", i},
                       {"placed", placed},
                       {"transformation", placed ? pl[i].transformation : 0},
                       {"at", Json::array({pl[i].xpos, pl[i].ypos, pl[i].zpos})}});
  }
  return out;
}

}  // namespace bt::engine

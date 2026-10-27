// SPDX-License-Identifier: GPL-3.0-or-later
#include "convert.h"

#include "shapelogic/app/shape_tools.h"
#include "shapelogic/base/thread_pool.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/model/sliding.h"
#include "shapelogic/model/stacking.h"

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
      {"classic", SolverType::Classic}, {"crowell", SolverType::Crowell}};
  return n;
}

const Names<sliding::SlidingSolver>& slidingSolverNames() {
  static const Names<sliding::SlidingSolver> n = {{"fast", sliding::SlidingSolver::Fast},
                                                  {"deep", sliding::SlidingSolver::Deep},
                                                  {"full", sliding::SlidingSolver::Full}};
  return n;
}

const Names<stacking::StackingSolver>& stackingSolverNames() {
  static const Names<stacking::StackingSolver> n = {{"stacking", stacking::StackingSolver::Shortest},
                                                    {"panex", stacking::StackingSolver::Panex}};
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
                                           {"flat", mesh::MeshStyle::Flat},
                                           {"stl", mesh::MeshStyle::Stl}};
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
    case SolveStage::Other: return "search";
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

std::string rodSetId(const State& s, unsigned int index) {
  return formatId(IdKind::RodSet, s.document().rodSetIds()[index]);
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

// A disc's colour from the palette: its first coloured cell's, the colour
// the original's 3D view drew it in (diskTint in voxelframe.cpp). The
// original looked no further than that cell.
Json discColour(const State& s, const Voxel& v) {
  for (unsigned int i = 0; i < v.getXYZ(); i++) {
    if (v.getState(i) != Voxel::kEmpty && v.getColor(i) != 0) {
      const unsigned int c = v.getColor(i);
      return c <= s.document().colourIds().size() ? Json(colourId(s, c - 1)) : Json(nullptr);
    }
  }
  return nullptr;
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
  j["rodSet"] = pr.rodSetValid(d.puzzle()) ? Json(rodSetId(s, pr.getRodSetId())) : Json(nullptr);
  j["pieces"] = pr.getNumberOfPieces();
  j["state"] = problemStateName(pr.getSolveState());
  j["solve"] = solveStateName(d.solveState(d.problemIds()[index]));
  j["analysing"] = d.analysing(d.problemIds()[index]);
  j["assemblies"] = optionalCount(pr.numAssembliesKnown(), pr.numAssembliesKnown() ? pr.getNumAssemblies() : 0);
  j["solutions"] = optionalCount(pr.numSolutionsKnown(), pr.numSolutionsKnown() ? pr.getNumSolutions() : 0);
  j["saved"] = pr.getNumberOfSavedSolutions();
  j["timeMs"] = optionalCount(pr.usedTimeKnown(), pr.usedTimeKnown() ? pr.getUsedMs() : 0);
  j["searched"] = pr.getSearched();
  j["searchDepth"] = pr.getSearchDepth();
  j["triedApart"] = pr.getTriedApart();
}

}  // namespace

Json shapeSummary(const State& s, unsigned int index) {
  const Voxel& v = s.document().puzzle().getShape(index);
  Json j = Json::object();
  j["id"] = shapeId(s, index);
  j["label"] = s.document().shapeLabelOf(index);
  // a start/goal shape's name holds its number ("sg:<n>:"): the number
  // goes apart, the name is the user's part
  const bool startGoal = sliding::isSliding(s.document().puzzle()) && sliding::isStartGoalShape(&v);
  j["name"] = startGoal ? sliding::startGoalUserName(&v) : v.getName();
  j["size"] = Json::array({v.getX(), v.getY(), v.getZ()});
  j["fixed"] = v.countState(Voxel::kFilled);
  j["variable"] = v.countState(Voxel::kVariable);
  j["weight"] = v.getWeight();
  j["colour"] = shapeColour(v);
  j["startGoal"] = startGoal ? Json(sliding::startGoalNumber(&v)) : Json(nullptr);
  const bool disc = stacking::isStacking(s.document().puzzle()) && v.getDiskSize() > 0;
  j["disc"] = disc ? Json(v.getDiskSize()) : Json(nullptr);
  j["discColour"] = disc ? discColour(s, v) : Json(nullptr);
  return j;
}

Json rodSetJson(const State& s, unsigned int index) {
  const stacking::RodSet& r = s.document().puzzle().getRodSet(index);
  Json j = Json::object();
  j["id"] = rodSetId(s, index);
  j["label"] = app::Document::rodSetLabel(index);
  j["name"] = r.name;
  j["rodCount"] = r.rodCount;
  j["rods"] = stacking::totalRods(r);
  j["growHeight"] = r.growHeight;
  j["definedHeight"] = r.definedHeight;
  j["sizeMatters"] = r.sizeMatters;
  // what the GUI's box showed (loadRodFields): older files cancelled the
  // distance rule with "can move over", which an edit writes off
  j["distanceMatters"] = r.distanceMatters && !r.canMoveOver;
  j["panexColumns"] = r.panexColumns;
  // the pocket and the channel as syncPanexFields left the boxes
  j["pocketColumn"] = r.panexColumns && r.pocketColumn;
  j["pocketHeight"] = r.pocketHeight;
  j["outsideChannel"] = stacking::hasOutsideChannel(r);
  return j;
}

Json stacksJson(const State& s, const stacking::StackMap& map) {
  Json rods = Json::array();
  for (const std::vector<stacking::DiskRef>& rod : map.rods) {
    Json discs = Json::array();
    for (const stacking::DiskRef& d : rod) {
      const bool known = d.shapeId < s.document().shapeIds().size();
      discs.push_back(Json{{"shape", known ? Json(shapeId(s, d.shapeId)) : Json(nullptr)},
                           {"instance", d.instance}});
    }
    rods.push_back(std::move(discs));
  }
  return rods;
}

Json layoutJson(const State& s, const stacking::BoardLayout& lay,
                const std::vector<stacking::Disc>& pieces) {
  Json j = Json::object();
  j["spacing"] = lay.spacing;
  j["rodCount"] = lay.rodCount;
  j["rodHeight"] = lay.rodHeight;
  j["rodX"] = lay.rodX;
  j["rodHeights"] = lay.rodHeights;
  j["panex"] = lay.panex;
  j["pocket"] = lay.pocket;
  j["maxSize"] = lay.maxSize;
  Json discs = Json::array();
  for (std::size_t i = 0; i < lay.disks.size() && i < pieces.size(); i++) {
    const stacking::Hotspot& h = lay.disks[i];
    discs.push_back(Json{{"shape", shapeId(s, pieces[i].shapeId)},
                         {"instance", pieces[i].instance},
                         {"size", pieces[i].size},
                         {"at", h.placed ? Json::array({h.x, h.y, h.z}) : Json(nullptr)}});
  }
  j["discs"] = std::move(discs);
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
  const Puzzle& puzzle = s.document().puzzle();
  const bool slidingPuzzle = sliding::isSliding(puzzle);
  return s.document().readProblem(index, [&s, index, &puzzle, slidingPuzzle](const Problem& pr) {
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
                           {"groups", std::move(groups)},
                           {"required", slidingPuzzle && sliding::shapeIsRequired(
                                                             puzzle, pr, part.shapeId)},
                           {"placed", slidingPuzzle
                                          ? sliding::stampedCopies(puzzle, pr, part.shapeId)
                                          : 0U}});
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
    j["stacks"] = stacking::isStacking(puzzle)
                      ? Json{{"start", stacksJson(s, pr.startStacks())},
                             {"goal", stacksJson(s, pr.goalStacks())}}
                      : Json(nullptr);
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
  Json rodSets = Json::array();
  for (unsigned int i = 0; i < p.rodSetCount(); i++) {
    rodSets.push_back(rodSetJson(s, i));
  }
  j["rodSets"] = std::move(rodSets);
  return j;
}

Json openedSnapshot(const State& s) {
  Json j = snapshot(s);
  if (!s.document().openNotice().empty()) {
    j["notice"] = s.document().openNotice();
  }
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
  j["hotspot"] = Json::array({m.hotspot[0], m.hotspot[1], m.hotspot[2]});
  return j;
}

// ---- the stored solver settings ---------------------------------------------------------

namespace {

// The original's check boxes (mainwindow_solve.cpp's optionNames), and
// verticalMoves, which sliding trays of several layers added.
constexpr std::array<std::string_view, 13> kSolverBoxes = {
    "disassemble",  "checkRotations", "justCount",     "justLevels",    "deepSymmetry",
    "keepMirrors",  "keepRotations",  "strictColors",  "nestedSlides",  "partialNested",
    "verticalMoves", "highMemory",    "autosave"};

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
  // where a full sliding search keeps its levels and a Panex search is
  // saved: the engine's settings
  o.sliding.workDir = s.settings.slideFolder;
  o.sliding.diskBudget = static_cast<std::uint64_t>(s.settings.slideDiskGB * 1e9);
  o.stacking.workDir = s.settings.panexFolder;
  o.stacking.diskBudget = static_cast<std::uint64_t>(s.settings.panexDiskGB * 1e9);
  // the memory of both: the setting, or half the machine's (the GUI's
  // Enable High Memory, which the setting replaced)
  const std::uint64_t memory =
      s.settings.searchMemoryGB > 0
          ? static_cast<std::uint64_t>(s.settings.searchMemoryGB * 1e9)
          : sliding::memoryBudget(0, true);
  o.sliding.memoryBytes = memory;
  o.stacking.memoryBytes = memory;
  o.settingsPlace = "Settings ▸ Solver";
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
  if (p.has("sliding")) {
    const Json& sj = p.value("sliding");
    if (!sj.is_object()) {
      p.fail("'sliding' must be an object");
    } else {
      Params sp(sj);
      o.sliding.solver = sp.optChoice("solver", slidingSolverNames()).value_or(o.sliding.solver);
      o.sliding.nested = sp.optBoolean("nestedSlides", o.sliding.nested);
      o.sliding.partialNested = sp.optBoolean("partialNested", o.sliding.partialNested);
      o.sliding.verticalMoves = sp.optBoolean("verticalMoves", o.sliding.verticalMoves);
      o.sliding.highMemory = sp.optBoolean("highMemory", o.sliding.highMemory);
      if (RpcResult<void> r = sp.finish(); !r) {
        RpcError e = r.error();
        e.message = "options: sliding: " + e.message;
        return std::unexpected(e);
      }
    }
  }
  if (p.has("stacking")) {
    const Json& sj = p.value("stacking");
    if (!sj.is_object()) {
      p.fail("'stacking' must be an object");
    } else {
      Params sp(sj);
      o.stacking.solver =
          sp.optChoice("solver", stackingSolverNames()).value_or(o.stacking.solver);
      o.stacking.highMemory = sp.optBoolean("highMemory", o.stacking.highMemory);
      o.stacking.autosave = sp.optBoolean("autosave", o.stacking.autosave);
      if (RpcResult<void> r = sp.finish(); !r) {
        RpcError e = r.error();
        e.message = "options: stacking: " + e.message;
        return std::unexpected(e);
      }
    }
  }
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
  j["usedMs"] = g.usedMs;
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
  j["searched"] = g.searched;
  j["searchDepth"] = g.searchDepth;
  j["searchTraced"] = g.searchTraced;
  j["panexSearch"] = g.panexSearch;
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

Json solutionRow(const Solution& sol, unsigned int index, bool stackingPuzzle) {
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
  if (stackingPuzzle && d != nullptr) {
    // a transfer is three frames (lift, cross, drop): the original showed
    // the transfers (logicalMoves), as the playback counts its steps
    const unsigned int transfers = d->sumMoves() / stacking::kStepsPerMove;
    j["moves"] = transfers;
    j["rotations"] = d->sumRotations();
    j["steps"] = transfers;
    j["movesText"] = std::to_string(transfers);
    return j;
  }
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

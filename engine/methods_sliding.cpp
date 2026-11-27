// SPDX-License-Identifier: GPL-3.0-or-later
// sliding.* (api/sliding.json): start/goal shapes and their stamps
// (decision 0014), and a puzzle played by hand.
#include "convert.h"
#include "engine.h"

#include "shapelogic/app/sliding_edits.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/model/sliding.h"
#include "shapelogic/solve/sliding_play.h"
#include "shapelogic/solve/sliding_search.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bt::engine {

namespace {

// The shape a stamp names (its color or goal mark, shape index + 1).
Json markJson(const State& s, unsigned int mark) {
  if (mark == 0 || mark > s.document().puzzle().getNumberOfShapes()) {
    return nullptr;
  }
  return shapeId(s, mark - 1);
}

// A start/goal shape's index; refused for another shape or grid.
RpcResult<unsigned int> trayIndex(const State& s, app::Id tray) {
  if (!sliding::isSliding(s.document().puzzle())) {
    return coreError(Errc::Unsupported, "only sliding puzzles have start/goal shapes");
  }
  BTE_TRY(const unsigned int t, s.document().shapeIndex(tray));
  if (!sliding::isStartGoalShape(&s.document().puzzle().getShape(t))) {
    return coreError(Errc::InvalidArgument, "Choose a start/goal shape.");
  }
  return t;
}

Reply slidingAddStartGoal(State& s, Params& p) {
  std::array<unsigned int, 3> size{6, 6, 1};
  if (p.has("size")) {
    const std::array<int, 3> c = p.pointOrCell("size", 1);
    if (c[0] < 1 || c[1] < 1 || c[2] < 1) {
      p.fail("'size' must be at least 1 in each direction");
    } else {
      size = {static_cast<unsigned int>(c[0]), static_cast<unsigned int>(c[1]),
              static_cast<unsigned int>(c[2])};
    }
  }
  BTE_CHECK(p.finish());
  BTE_TRY(const app::Id id, app::SlidingEdits(s.document()).addStartGoal(size));
  return Json{{"shape", formatId(IdKind::Shape, id)}, {"revision", s.revision()}};
}

Reply slidingPlace(State& s, Params& p) {
  const app::Id tray = p.id("tray", IdKind::Shape);
  const app::Id piece = p.id("piece", IdKind::Shape);
  const std::array<int, 3> at = p.pointOrCell("at", 0);
  const bool goal = p.boolean("goal");
  const app::Id problem = p.has("problem") ? p.id("problem", IdKind::Problem) : 0;
  BTE_CHECK(p.finish());
  BTE_TRY(
      const bool placed,
      app::SlidingEdits(s.document()).placeStamp(tray, piece, at[0], at[1], at[2], goal, problem));
  return Json{{"placed", placed}, {"revision", s.revision()}};
}

Reply slidingClearAt(State& s, Params& p) {
  const app::Id tray = p.id("tray", IdKind::Shape);
  const std::array<int, 3> at = p.pointOrCell("at", 0);
  const bool goal = p.boolean("goal");
  const app::Id problem = p.has("problem") ? p.id("problem", IdKind::Problem) : 0;
  BTE_CHECK(p.finish());
  BTE_TRY(const bool cleared,
          app::SlidingEdits(s.document()).clearStampAt(tray, at[0], at[1], at[2], goal, problem));
  return Json{{"cleared", cleared}, {"revision", s.revision()}};
}

Reply slidingClone(State& s, Params& p) {
  const app::Id tray = p.id("tray", IdKind::Shape);
  const bool toGoal = p.boolean("toGoal");
  const app::Id problem = p.has("problem") ? p.id("problem", IdKind::Problem) : 0;
  BTE_CHECK(p.finish());
  BTE_CHECK(app::SlidingEdits(s.document()).cloneStamps(tray, toGoal, problem));
  return Json{{"revision", s.revision()}};
}

Reply slidingClear(State& s, Params& p) {
  const app::Id tray = p.id("tray", IdKind::Shape);
  const bool goal = p.boolean("goal");
  const app::Id problem = p.has("problem") ? p.id("problem", IdKind::Problem) : 0;
  BTE_CHECK(p.finish());
  BTE_TRY(const bool cleared, app::SlidingEdits(s.document()).clearStamps(tray, goal, problem));
  return Json{{"cleared", cleared}, {"revision", s.revision()}};
}

Reply slidingTray(State& s, Params& p) {
  const app::Id tray = p.id("tray", IdKind::Shape);
  const app::Id problem = p.has("problem") ? p.id("problem", IdKind::Problem) : 0;
  BTE_CHECK(p.finish());
  BTE_TRY(const unsigned int t, trayIndex(s, tray));
  BTE_TRY(const Problem* on, app::SlidingEdits(s.document()).trayProblemOf(problem, t));
  const Puzzle& puzzle = s.document().puzzle();
  const Voxel& v = puzzle.getShape(t);
  // the starts and goals are the problem's (decision 0027): the one named,
  // else the first on this shape; a shape no problem uses has none
  const SlideMarks none;
  const SlideMarks& marks = on != nullptr ? on->slideMarks() : none;
  Json starts = Json::array();
  Json goals = Json::array();
  for (unsigned int i = 0; i < v.getXYZ(); i++) {
    starts.push_back(markJson(s, marks.start(i)));
    goals.push_back(markJson(s, marks.goal(i)));
  }
  // the stamps larger than their pieces
  const std::string overflow =
      on != nullptr ? sliding::stampOverflowMessage(puzzle, *on) : std::string();
  const std::string error = sliding::startGoalError(puzzle, t, on);
  // each copy placed, start and goal, as whole pieces (marks that are not
  // whole pieces: all of them as one)
  Json placed = Json::array();
  for (unsigned int i = 0; i < puzzle.getNumberOfShapes() && i < 255; i++) {
    const Voxel& piece = puzzle.getShape(i);
    if (sliding::isStartGoalShape(&piece) || sliding::isHiddenSlidingShape(&piece)) {
      continue;
    }
    for (const bool goal : {false, true}) {
      std::vector<std::vector<sliding::TrayCell>> copies;
      if (auto split = sliding::stampCopies(v, marks, piece, i, goal)) {
        copies = std::move(*split);
      } else {
        std::vector<sliding::TrayCell> all;
        for (unsigned int c = 0; c < v.getXYZ(); c++) {
          if (marks.get(c, goal) == i + 1) {
            all.push_back({static_cast<int>(c % v.getX()),
                           static_cast<int>((c / v.getX()) % v.getY()),
                           static_cast<int>(c / (v.getX() * v.getY()))});
          }
        }
        copies.push_back(std::move(all));
      }
      for (const std::vector<sliding::TrayCell>& copy : copies) {
        Json cells = Json::array();
        for (const sliding::TrayCell& c : copy) {
          cells.push_back(Json::array({c[0], c[1], c[2]}));
        }
        placed.push_back(
            Json{{"piece", shapeId(s, i)}, {"goal", goal}, {"cells", std::move(cells)}});
      }
    }
  }
  return Json{{"number", sliding::startGoalNumber(&v)},
              {"name", sliding::startGoalUserName(&v)},
              {"size", Json::array({v.getX(), v.getY(), v.getZ()})},
              {"starts", std::move(starts)},
              {"goals", std::move(goals)},
              {"error", error.empty() ? Json(nullptr) : Json(error)},
              {"overflow", overflow.empty() ? Json(nullptr) : Json(overflow)},
              {"floor", sliding::floorCells(v)},
              {"placed", std::move(placed)}};
}

Reply slidingPreview(State& s, Params& p) {
  const app::Id tray = p.id("tray", IdKind::Shape);
  const app::Id piece = p.id("piece", IdKind::Shape);
  const std::array<int, 3> at = p.pointOrCell("at", 0);
  const bool goal = p.boolean("goal");
  const app::Id problem = p.has("problem") ? p.id("problem", IdKind::Problem) : 0;
  BTE_CHECK(p.finish());
  BTE_TRY(const unsigned int t, trayIndex(s, tray));
  BTE_TRY(const Problem* on, app::SlidingEdits(s.document()).trayProblemOf(problem, t));
  BTE_TRY(const unsigned int i, s.document().shapeIndex(piece));
  const Puzzle& puzzle = s.document().puzzle();
  const Voxel& trayShape = puzzle.getShape(t);
  const Voxel& pieceShape = puzzle.getShape(i);
  if (sliding::isStartGoalShape(&pieceShape) || sliding::isHiddenSlidingShape(&pieceShape)) {
    return coreError(Errc::InvalidArgument, "a start/goal shape is no piece");
  }
  const unsigned int copies = sliding::pieceCopies(puzzle, t, i, on);
  const SlideMarks none;
  const SlideMarks* marks = on != nullptr ? &on->slideMarks() : &none;
  Json cells = Json::array();
  for (const sliding::TrayCell& c :
       sliding::stampCells(&trayShape, marks, &pieceShape, i, at[0], at[1], at[2], goal, copies)) {
    cells.push_back(Json::array({c[0], c[1], c[2]}));
  }
  const bool there =
      sliding::stampIsAt(&trayShape, marks, &pieceShape, i, at[0], at[1], at[2], goal);
  const std::string why = there ? std::string()
                                : sliding::stampFits(&trayShape, marks, &pieceShape, i, at[0],
                                                     at[1], at[2], goal, copies);
  return Json{{"cells", std::move(cells)},
              {"fits", why.empty()},
              {"why", why.empty() ? Json(nullptr) : Json(why)},
              {"there", there}};
}

Reply slidingGridAdvice(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  BTE_CHECK(p.finish());
  BTE_TRY(const unsigned int index, s.document().problemIndex(problem));
  const Puzzle& puzzle = s.document().puzzle();
  if (!sliding::isSliding(puzzle)) {
    return coreError(Errc::Unsupported, "only sliding puzzles have optimized paths");
  }
  const sliding::SlideGrid g = sliding::slideGridFor(puzzle, puzzle.getProblem(index));
  return Json{{"grid", g.size}, {"stops", g.stops}, {"gridStops", g.gridStops}};
}

Reply slidingResumeAdvice(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const Json given = p.has("options") ? p.value("options") : Json(nullptr);
  BTE_CHECK(p.finish());
  BTE_TRY(const SolveOptions options, solveOptionsFrom(s, given));
  BTE_CHECK(s.document().problemIndex(problem));
  if (!sliding::isSliding(s.document().puzzle())) {
    return coreError(Errc::Unsupported, "only sliding puzzles have sliding searches");
  }
  BTE_TRY(const bool alone, s.document().slideFromStartAlone(problem, options.sliding));
  return Json{{"fromStartAlone", alone}};
}

// The play calls (sliding.play*, sliding.setLayout): a sliding puzzle
// played by hand (bt/solve/sliding_play.h).

// The most places and moves a call may give, and the furthest a cell or a
// shift may be from the origin: far past any board, and safe to add up.
constexpr std::size_t kMostPlaces = 65536;
constexpr std::size_t kMostMoves = 100000;
constexpr std::int64_t kFarthest = 1000000;

// [x, y, z], each within kFarthest.
std::optional<std::array<int, 3>> tripleOf(const Json& j) {
  if (!j.is_array() || j.size() != 3) {
    return std::nullopt;
  }
  std::array<int, 3> out{};
  for (std::size_t k = 0; k < 3; k++) {
    if (!j[k].is_number_integer()) {
      return std::nullopt;
    }
    const auto v = j[k].get<std::int64_t>();
    if (v < -kFarthest || v > kFarthest) {
      return std::nullopt;
    }
    out.at(k) = static_cast<int>(v);
  }
  return out;
}

Json tripleJson(const std::array<int, 3>& c) { return Json::array({c[0], c[1], c[2]}); }

// A position: 'places', a cell for each piece.
std::vector<sliding::TrayCell> placesOf(Params& p) {
  std::vector<sliding::TrayCell> out;
  const Json& list = p.value("places");
  if (!list.is_array() || list.size() > kMostPlaces) {
    p.fail("'places' must be a list of cells [x, y, z]");
    return out;
  }
  for (const Json& j : list) {
    const std::optional<std::array<int, 3>> c = tripleOf(j);
    if (!c) {
      p.fail("'places' must be a list of cells [x, y, z]");
      return out;
    }
    out.push_back(*c);
  }
  return out;
}

// The rules a game is played by: 'rules', every one off when left out.
sliding::PlayRules rulesOf(Params& p) {
  sliding::PlayRules rules;
  if (!p.has("rules")) {
    return rules;
  }
  const Json& j = p.value("rules");
  if (!j.is_object()) {
    p.fail("'rules' must be an object");
    return rules;
  }
  for (const auto& [name, value] : j.items()) {
    bool* to = name == "nestedSlides"    ? &rules.nested
               : name == "partialNested" ? &rules.partialNested
               : name == "verticalMoves" ? &rules.verticalMoves
                                         : nullptr;
    if (to == nullptr) {
      p.fail("'rules' has no '" + name + "'");
    } else if (!value.is_boolean()) {
      p.fail("'rules." + name + "' must be true or false");
    } else {
      *to = value.get<bool>();
    }
  }
  return rules;
}

// The problem of a play call, on a sliding puzzle.
RpcResult<const Problem*> playProblem(const State& s, app::Id problem) {
  return engine::playProblem(s, problem, sliding::isSliding, "only sliding puzzles are played");
}

Reply slidingPlayStart(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  BTE_CHECK(p.finish());
  BTE_TRY(const Problem* pr, playProblem(s, problem));
  const Puzzle& puzzle = s.document().puzzle();
  const sliding::PlayStart start = sliding::playStart(puzzle, *pr);
  // the board, also when the starts place no layout yet
  Json board = nullptr;
  Json goals = Json::array();
  if (sliding::usesStartMarks(puzzle, *pr)) {
    const Voxel& v = pr->resultShape(puzzle);
    Json cells = Json::array();
    for (unsigned int z = 0; z < v.getZ(); z++) {
      for (unsigned int y = 0; y < v.getY(); y++) {
        for (unsigned int x = 0; x < v.getX(); x++) {
          const int state = v.getState(x, y, z);
          if (state != Voxel::kEmpty) {
            cells.push_back(Json{{"at", Json::array({x, y, z})},
                                 {"state", state == Voxel::kVariable ? "variable" : "fixed"}});
          }
        }
      }
    }
    board = Json{{"shape", shapeId(s, pr->getResultId())},
                 {"size", Json::array({v.getX(), v.getY(), v.getZ()})},
                 {"cells", std::move(cells)}};
    // each goal, a whole piece (marks that are not whole pieces are left
    // out: the Puzzle tab shows what is wrong with them)
    for (unsigned int i = 0; i < puzzle.getNumberOfShapes() && i < 255; i++) {
      const Voxel& piece = puzzle.getShape(i);
      if (sliding::isStartGoalShape(&piece) || sliding::isHiddenSlidingShape(&piece)) {
        continue;
      }
      const auto copies = sliding::stampCopies(v, pr->slideMarks(), piece, i, true);
      if (!copies) {
        continue;
      }
      for (const std::vector<sliding::TrayCell>& copy : *copies) {
        Json filled = Json::array();
        for (const sliding::TrayCell& c : copy) {
          filled.push_back(tripleJson(c));
        }
        goals.push_back(Json{{"piece", shapeId(s, i)}, {"cells", std::move(filled)}});
      }
    }
  }
  Json pieces = Json::array();
  for (const sliding::PlayPiece& piece : start.pieces) {
    Json cells = Json::array();
    for (const sliding::TrayCell& c : piece.cells) {
      cells.push_back(tripleJson(c));
    }
    pieces.push_back(Json{{"piece", shapeId(s, piece.shapeId)},
                          {"at", tripleJson(piece.at)},
                          {"cells", std::move(cells)},
                          {"origin", tripleJson(piece.origin)}});
  }
  return Json{{"error", start.error.empty() ? Json(nullptr) : Json(start.error)},
              {"board", std::move(board)},
              {"pieces", std::move(pieces)},
              {"goals", std::move(goals)},
              {"hasGoal", start.hasGoal},
              {"solved", start.solved}};
}

Reply slidingPlayReach(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const std::vector<sliding::TrayCell> places = placesOf(p);
  const unsigned int piece = p.count("piece", static_cast<unsigned int>(kMostPlaces));
  const sliding::PlayRules rules = rulesOf(p);
  BTE_CHECK(p.finish());
  BTE_TRY(const Problem* pr, playProblem(s, problem));
  BTE_TRY(const std::vector<sliding::PlayGroup> reach,
          sliding::playReach(s.document().puzzle(), *pr, places, piece, rules));
  Json groups = Json::array();
  for (const sliding::PlayGroup& g : reach) {
    Json stops = Json::array();
    for (const sliding::PlayStop& stop : g.stops) {
      stops.push_back(Json{{"by", tripleJson(stop.by)}, {"solved", stop.solved}});
    }
    groups.push_back(Json{{"movers", g.movers}, {"stops", std::move(stops)}});
  }
  return Json{{"groups", std::move(groups)}};
}

Reply slidingPlayReplay(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const std::vector<sliding::TrayCell> places = placesOf(p);
  std::vector<sliding::PlayMove> moves;
  const Json& list = p.value("moves");
  const std::string_view shape = "'moves' must be a list of {movers: [piece, ...], by: [dx, dy, dz]}";
  if (!list.is_array() || list.size() > kMostMoves) {
    p.fail(std::string(shape));
  } else {
    for (const Json& j : list) {
      sliding::PlayMove m;
      const std::optional<std::array<int, 3>> by =
          j.is_object() && j.size() == 2 && j.contains("by") ? tripleOf(j["by"]) : std::nullopt;
      bool good = by.has_value() && j.contains("movers") && j["movers"].is_array() &&
                  !j["movers"].empty() && j["movers"].size() <= kMostPlaces;
      for (std::size_t k = 0; good && k < j["movers"].size(); k++) {
        const Json& n = j["movers"][k];
        good = n.is_number_integer() && n.get<std::int64_t>() >= 0 &&
               n.get<std::int64_t>() <= static_cast<std::int64_t>(kMostPlaces);
        if (good) {
          m.movers.push_back(static_cast<unsigned int>(n.get<std::int64_t>()));
        }
      }
      if (!good) {
        p.fail(std::string(shape));
        break;
      }
      // NOLINTNEXTLINE(bugprone-unchecked-optional-access): good says it has a value
      m.by = *by;
      moves.push_back(std::move(m));
    }
  }
  const sliding::PlayRules rules = rulesOf(p);
  BTE_CHECK(p.finish());
  BTE_TRY(const Problem* pr, playProblem(s, problem));
  BTE_TRY(const sliding::PlayReplay replay,
          sliding::playReplay(s.document().puzzle(), *pr, places, moves, rules));
  Json steps = Json::array();
  for (const sliding::PlayStep& step : replay.steps) {
    Json route = Json::array();
    for (const sliding::SlideShift& c : step.route) {
      route.push_back(tripleJson(c));
    }
    steps.push_back(Json{{"route", std::move(route)}, {"solved", step.solved}});
  }
  return replayJson(replay.solved, std::move(steps), replay.error);
}

Reply slidingSetLayout(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const bool goal = p.boolean("goal");
  const std::vector<sliding::TrayCell> places = placesOf(p);
  BTE_CHECK(p.finish());
  BTE_TRY(const bool changed,
          app::SlidingEdits(s.document()).setSlideLayout(problem, goal, places));
  return Json{{"changed", changed}, {"revision", s.revision()}};
}

}  // namespace

void addSlidingMethods(MethodTable& t) {
  t["sliding.addStartGoal"] = slidingAddStartGoal;
  t["sliding.place"] = slidingPlace;
  t["sliding.clearAt"] = slidingClearAt;
  t["sliding.clone"] = slidingClone;
  t["sliding.clear"] = slidingClear;
  t["sliding.tray"] = slidingTray;
  t["sliding.preview"] = slidingPreview;
  t["sliding.gridAdvice"] = slidingGridAdvice;
  t["sliding.resumeAdvice"] = slidingResumeAdvice;
  t["sliding.playStart"] = slidingPlayStart;
  t["sliding.playReach"] = slidingPlayReach;
  t["sliding.playReplay"] = slidingPlayReplay;
  t["sliding.setLayout"] = slidingSetLayout;
}

}  // namespace bt::engine

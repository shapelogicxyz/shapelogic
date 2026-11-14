// SPDX-License-Identifier: GPL-3.0-or-later
// sliding.* (api/sliding.json): start/goal shapes and their stamps
// (decision 0014).
#include "convert.h"
#include "engine.h"

#include "shapelogic/grid/voxel.h"
#include "shapelogic/model/sliding.h"
#include "shapelogic/solve/sliding_search.h"

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace bt::engine {

namespace {

// The shape a stamp names (its colour or goal mark, shape index + 1).
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
  BTE_TRY(const app::Id id, s.document().addStartGoal(size));
  return Json{{"shape", formatId(IdKind::Shape, id)}, {"revision", s.revision()}};
}

Reply slidingPlace(State& s, Params& p) {
  const app::Id tray = p.id("tray", IdKind::Shape);
  const app::Id piece = p.id("piece", IdKind::Shape);
  const std::array<int, 3> at = p.pointOrCell("at", 0);
  const bool goal = p.boolean("goal");
  const app::Id problem = p.has("problem") ? p.id("problem", IdKind::Problem) : 0;
  BTE_CHECK(p.finish());
  BTE_TRY(const bool placed,
          s.document().placeStamp(tray, piece, at[0], at[1], at[2], goal, problem));
  return Json{{"placed", placed}, {"revision", s.revision()}};
}

Reply slidingClearAt(State& s, Params& p) {
  const app::Id tray = p.id("tray", IdKind::Shape);
  const std::array<int, 3> at = p.pointOrCell("at", 0);
  const bool goal = p.boolean("goal");
  const app::Id problem = p.has("problem") ? p.id("problem", IdKind::Problem) : 0;
  BTE_CHECK(p.finish());
  BTE_TRY(const bool cleared,
          s.document().clearStampAt(tray, at[0], at[1], at[2], goal, problem));
  return Json{{"cleared", cleared}, {"revision", s.revision()}};
}

Reply slidingClone(State& s, Params& p) {
  const app::Id tray = p.id("tray", IdKind::Shape);
  const bool toGoal = p.boolean("toGoal");
  const app::Id problem = p.has("problem") ? p.id("problem", IdKind::Problem) : 0;
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().cloneStamps(tray, toGoal, problem));
  return Json{{"revision", s.revision()}};
}

Reply slidingClear(State& s, Params& p) {
  const app::Id tray = p.id("tray", IdKind::Shape);
  const bool goal = p.boolean("goal");
  const app::Id problem = p.has("problem") ? p.id("problem", IdKind::Problem) : 0;
  BTE_CHECK(p.finish());
  BTE_TRY(const bool cleared, s.document().clearStamps(tray, goal, problem));
  return Json{{"cleared", cleared}, {"revision", s.revision()}};
}

Reply slidingTray(State& s, Params& p) {
  const app::Id tray = p.id("tray", IdKind::Shape);
  const app::Id problem = p.has("problem") ? p.id("problem", IdKind::Problem) : 0;
  BTE_CHECK(p.finish());
  BTE_TRY(const unsigned int t, trayIndex(s, tray));
  BTE_TRY(const Problem* on, s.document().trayProblemOf(problem, t));
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
  BTE_TRY(const Problem* on, s.document().trayProblemOf(problem, t));
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
}

}  // namespace bt::engine

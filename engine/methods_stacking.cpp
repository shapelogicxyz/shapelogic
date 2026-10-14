// SPDX-License-Identifier: GPL-3.0-or-later
// stacking.* (api/stacking.json): a stacking puzzle's rod sets, discs and
// stacks (decision 0015), and the reads its editor and player need: where
// rods and discs stand, the validity bar's sentence, the board's centre.
#include "convert.h"
#include "engine.h"

#include "shapelogic/app/playback.h"
#include "shapelogic/app/queries.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/model/stacking.h"

#include <string>
#include <utility>
#include <vector>

namespace bt::engine {

namespace {

// The largest rod index and disc place a request may name: a stack map
// keeps rods past the board's count while they hold discs, so the core
// says which exist.
constexpr std::int64_t kMaxPlace = 0xFFFF;

Reply stackingAddRodSet(State& s, Params& p) {
  BTE_CHECK(p.finish());
  BTE_TRY(const app::Id id, s.document().addRodSet());
  return Json{{"rodSet", formatId(IdKind::RodSet, id)}, {"revision", s.revision()}};
}

Reply stackingDuplicateRodSet(State& s, Params& p) {
  const app::Id r = p.id("rodSet", IdKind::RodSet);
  BTE_CHECK(p.finish());
  BTE_TRY(const app::Id id, s.document().duplicateRodSet(r));
  return Json{{"rodSet", formatId(IdKind::RodSet, id)}, {"revision", s.revision()}};
}

Reply stackingDeleteRodSet(State& s, Params& p) {
  const app::Id r = p.id("rodSet", IdKind::RodSet);
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().deleteRodSet(r));
  return s.changed();
}

Reply stackingRenameRodSet(State& s, Params& p) {
  const app::Id r = p.id("rodSet", IdKind::RodSet);
  std::string name = p.string("name");
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().renameRodSet(r, std::move(name)));
  return s.changed();
}

Reply stackingMoveRodSet(State& s, Params& p) {
  const app::Id r = p.id("rodSet", IdKind::RodSet);
  const unsigned int to = p.count("toIndex");
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().moveRodSet(r, to));
  return s.changed();
}

// The fields given, over the rod set's as the original's fields showed
// them (rodSetJson), so a client sends only what it changes.
Reply stackingSetRules(State& s, Params& p) {
  const app::Id r = p.id("rodSet", IdKind::RodSet);
  const Json& given = p.value("rules");
  BTE_CHECK(p.finish());
  if (!given.is_object()) {
    return std::unexpected(invalidParams("'rules' must be an object"));
  }
  BTE_TRY(const unsigned int index, s.document().rodSetIndex(r));
  const Json shown = rodSetJson(s, index);
  stacking::RodSet f;
  Params rp(given);
  // the inputs' own bounds (the document clamps as the GUI did)
  f.rodCount = static_cast<unsigned int>(
      rp.optInteger("rodCount", shown["rodCount"].get<std::int64_t>(), 1, 16));
  f.growHeight = rp.optBoolean("growHeight", shown["growHeight"].get<bool>());
  f.definedHeight = static_cast<unsigned int>(
      rp.optInteger("definedHeight", shown["definedHeight"].get<std::int64_t>(), 1, 64));
  f.sizeMatters = rp.optBoolean("sizeMatters", shown["sizeMatters"].get<bool>());
  f.distanceMatters = rp.optBoolean("distanceMatters", shown["distanceMatters"].get<bool>());
  f.panexColumns = rp.optBoolean("panexColumns", shown["panexColumns"].get<bool>());
  f.pocketColumn = rp.optBoolean("pocketColumn", shown["pocketColumn"].get<bool>());
  f.pocketHeight = static_cast<unsigned int>(
      rp.optInteger("pocketHeight", shown["pocketHeight"].get<std::int64_t>(), 1, 64));
  f.outsideChannel = rp.optBoolean("outsideChannel", shown["outsideChannel"].get<bool>());
  if (RpcResult<void> e = rp.finish(); !e) {
    RpcError err = e.error();
    err.message = "rules: " + err.message;
    return std::unexpected(err);
  }
  BTE_CHECK(s.document().setRodSetRules(r, f));
  return s.changed();
}

Reply stackingSetDiscSize(State& s, Params& p) {
  const app::Id shape = p.id("shape", IdKind::Shape);
  const auto size = static_cast<unsigned int>(p.integer("size", 1, 100));
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().setDiscSize(shape, size));
  return s.changed();
}

Reply stackingSetProblemRodSet(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const app::Id r = p.id("rodSet", IdKind::RodSet);
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().setProblemRodSet(problem, r));
  return s.changed();
}

Reply stackingPlaceDisc(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const app::Id shape = p.id("shape", IdKind::Shape);
  const auto rod = static_cast<unsigned int>(p.integer("rod", 0, kMaxPlace));
  const bool goal = p.boolean("goal");
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().placeDisc(problem, shape, rod, goal));
  return s.changed();
}

Reply stackingPlaceAll(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const auto rod = static_cast<unsigned int>(p.integer("rod", 0, kMaxPlace));
  const bool goal = p.boolean("goal");
  BTE_CHECK(p.finish());
  BTE_TRY(const app::DiscsPlaced placed, s.document().placeAllDiscs(problem, rod, goal));
  return Json{{"placed", placed.placed},
              {"error", placed.error.empty() ? Json(nullptr) : Json(placed.error)},
              {"revision", s.revision()}};
}

Reply stackingRemoveDisc(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const bool goal = p.boolean("goal");
  const auto rod = static_cast<unsigned int>(p.integer("rod", 0, kMaxPlace));
  const auto index = static_cast<unsigned int>(p.integer("index", 0, kMaxPlace));
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().removeDisc(problem, goal, rod, index));
  return s.changed();
}

Reply stackingClear(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const bool goal = p.boolean("goal");
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().clearStacks(problem, goal));
  return s.changed();
}

Reply stackingMoveDisc(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const bool goal = p.boolean("goal");
  const auto rod = static_cast<unsigned int>(p.integer("rod", 0, kMaxPlace));
  const auto index = static_cast<unsigned int>(p.integer("index", 0, kMaxPlace));
  const auto delta = static_cast<int>(p.integer("delta", -1, 1));
  BTE_CHECK(p.finish());
  BTE_TRY(const unsigned int at, s.document().moveDisc(problem, goal, rod, index, delta));
  return Json{{"index", at}, {"revision", s.revision()}};
}

// The board of a problem as the Problems tab showed it: its rods with
// their names and capacities, the start's or the goal's stacks, where
// rods and discs stand, the validity bar's sentence, and the centre the
// playback's poses are placed around.
Reply stackingBoard(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const bool goal = p.boolean("goal");
  BTE_CHECK(p.finish());
  const app::Document& d = s.document();
  if (!stacking::isStacking(d.puzzle())) {
    return coreError(Errc::Unsupported, "only stacking puzzles have rods");
  }
  BTE_TRY(const unsigned int index, d.problemIndex(problem));
  const Puzzle& puzzle = d.puzzle();
  return d.readProblem(index, [&s, &puzzle, goal](const Problem& pr) -> Reply {
    const std::string error = stacking::setupError(puzzle, pr);
    Json j = Json::object();
    j["stacks"] = stacksJson(s, goal ? pr.goalStacks() : pr.startStacks());
    j["error"] = error.empty() ? Json(nullptr) : Json(error);
    if (!pr.rodSetValid(puzzle)) {
      j["rodSet"] = nullptr;
      j["rods"] = Json::array();
      j["layout"] = nullptr;
      j["centre"] = nullptr;
      return j;
    }
    const stacking::RodSet& board = puzzle.getRodSet(pr.getRodSetId());
    const std::vector<stacking::Disc> pieces = stacking::discsOf(puzzle, pr);
    const auto discs = static_cast<unsigned int>(pieces.size());
    Json rods = Json::array();
    for (unsigned int r = 0; r < stacking::totalRods(board); r++) {
      rods.push_back(Json{{"name", stacking::rodName(board, r)},
                          {"pocket", stacking::isPocket(board, r)},
                          {"position", stacking::rodPosition(board, r)},
                          {"capacity", stacking::rodCapacity(board, r, discs)}});
    }
    j["rodSet"] = rodSetId(s, pr.getRodSetId());
    j["rods"] = std::move(rods);
    j["layout"] = layoutJson(s, stacking::layoutBoard(puzzle, pr, goal), pieces);
    const std::array<double, 3> c = app::stackingCentre(puzzle, pr);
    j["centre"] = Json::array({c[0], c[1], c[2]});
    return j;
  });
}

// A rod set drawn alone (the Entities tab's view of it).
Reply stackingRodSetLayout(State& s, Params& p) {
  const app::Id r = p.id("rodSet", IdKind::RodSet);
  BTE_CHECK(p.finish());
  BTE_TRY(const unsigned int index, s.document().rodSetIndex(r));
  return layoutJson(s, app::rodSetLayout(s.document().puzzle(), index), {});
}

}  // namespace

void addStackingMethods(MethodTable& t) {
  t["stacking.addRodSet"] = stackingAddRodSet;
  t["stacking.duplicateRodSet"] = stackingDuplicateRodSet;
  t["stacking.deleteRodSet"] = stackingDeleteRodSet;
  t["stacking.renameRodSet"] = stackingRenameRodSet;
  t["stacking.moveRodSet"] = stackingMoveRodSet;
  t["stacking.setRules"] = stackingSetRules;
  t["stacking.setDiscSize"] = stackingSetDiscSize;
  t["stacking.setProblemRodSet"] = stackingSetProblemRodSet;
  t["stacking.placeDisc"] = stackingPlaceDisc;
  t["stacking.placeAll"] = stackingPlaceAll;
  t["stacking.removeDisc"] = stackingRemoveDisc;
  t["stacking.clear"] = stackingClear;
  t["stacking.moveDisc"] = stackingMoveDisc;
  t["stacking.board"] = stackingBoard;
  t["stacking.rodSetLayout"] = stackingRodSetLayout;
}

}  // namespace bt::engine

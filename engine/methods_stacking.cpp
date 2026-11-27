// SPDX-License-Identifier: GPL-3.0-or-later
// stacking.* (api/stacking.json): a stacking puzzle's rod sets, discs and
// stacks (decision 0015), the reads its editor and player need (where rods
// and discs stand, the validity bar's sentence, the board's center), and a
// puzzle played by hand (decision 0042).
#include "convert.h"
#include "engine.h"

#include "shapelogic/app/playback.h"
#include "shapelogic/app/queries.h"
#include "shapelogic/app/stacking_edits.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/model/stacking.h"
#include "shapelogic/model/stacking_play.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace sl::engine {

namespace {

// The largest rod index and disc place a request may name: a stack map
// keeps rods past the board's count while they hold discs, so the core
// says which exist.
constexpr std::int64_t kMaxPlace = 0xFFFF;

Reply stackingAddRodSet(State& s, Params& p) {
  SLE_CHECK(p.finish());
  SLE_TRY(const app::Id id, app::StackingEdits(s.document()).addRodSet());
  return Json{{"rodSet", formatId(IdKind::RodSet, id)}, {"revision", s.revision()}};
}

Reply stackingDuplicateRodSet(State& s, Params& p) {
  const app::Id r = p.id("rodSet", IdKind::RodSet);
  SLE_CHECK(p.finish());
  SLE_TRY(const app::Id id, app::StackingEdits(s.document()).duplicateRodSet(r));
  return Json{{"rodSet", formatId(IdKind::RodSet, id)}, {"revision", s.revision()}};
}

Reply stackingDeleteRodSet(State& s, Params& p) {
  const app::Id r = p.id("rodSet", IdKind::RodSet);
  SLE_CHECK(p.finish());
  SLE_CHECK(app::StackingEdits(s.document()).deleteRodSet(r));
  return s.changed();
}

Reply stackingRenameRodSet(State& s, Params& p) {
  const app::Id r = p.id("rodSet", IdKind::RodSet);
  std::string name = p.string("name");
  SLE_CHECK(p.finish());
  SLE_CHECK(app::StackingEdits(s.document()).renameRodSet(r, std::move(name)));
  return s.changed();
}

Reply stackingMoveRodSet(State& s, Params& p) {
  const app::Id r = p.id("rodSet", IdKind::RodSet);
  const unsigned int to = p.count("toIndex");
  SLE_CHECK(p.finish());
  SLE_CHECK(app::StackingEdits(s.document()).moveRodSet(r, to));
  return s.changed();
}

// The fields given, over the rod set's as the original's fields showed
// them (rodSetJson), so a client sends only what it changes.
Reply stackingSetRules(State& s, Params& p) {
  const app::Id r = p.id("rodSet", IdKind::RodSet);
  const Json& given = p.value("rules");
  SLE_CHECK(p.finish());
  if (!given.is_object()) {
    return std::unexpected(invalidParams("'rules' must be an object"));
  }
  SLE_TRY(const unsigned int index, s.document().rodSetIndex(r));
  // what is not given stays as the fields show it (the core's struct, not
  // its JSON)
  const stacking::RodSet shown = rodSetFields(s.document().puzzle().getRodSet(index));
  stacking::RodSet f;
  Params rp(given);
  // the inputs' own bounds, which the document refuses past too
  f.rodCount = static_cast<unsigned int>(
      rp.optInteger("rodCount", shown.rodCount, 1, stacking::kMaxRods));
  f.growHeight = rp.optBoolean("growHeight", shown.growHeight);
  f.definedHeight = static_cast<unsigned int>(
      rp.optInteger("definedHeight", shown.definedHeight, 1, stacking::kMaxHeight));
  f.sizeMatters = rp.optBoolean("sizeMatters", shown.sizeMatters);
  f.distanceMatters = rp.optBoolean("distanceMatters", shown.distanceMatters);
  f.panexColumns = rp.optBoolean("panexColumns", shown.panexColumns);
  f.pocketColumn = rp.optBoolean("pocketColumn", shown.pocketColumn);
  f.pocketHeight = static_cast<unsigned int>(
      rp.optInteger("pocketHeight", shown.pocketHeight, 1, stacking::kMaxHeight));
  f.outsideChannel = rp.optBoolean("outsideChannel", shown.outsideChannel);
  if (RpcResult<void> e = rp.finish(); !e) {
    RpcError err = e.error();
    err.message = "rules: " + err.message;
    return std::unexpected(err);
  }
  SLE_CHECK(app::StackingEdits(s.document()).setRodSetRules(r, f));
  return s.changed();
}

Reply stackingSetDiscSize(State& s, Params& p) {
  const app::Id shape = p.id("shape", IdKind::Shape);
  const auto size = static_cast<unsigned int>(p.integer("size", 1, stacking::kMaxDiscSize));
  SLE_CHECK(p.finish());
  SLE_CHECK(app::StackingEdits(s.document()).setDiscSize(shape, size));
  return s.changed();
}

Reply stackingSetProblemRodSet(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const app::Id r = p.id("rodSet", IdKind::RodSet);
  SLE_CHECK(p.finish());
  SLE_CHECK(app::StackingEdits(s.document()).setProblemRodSet(problem, r));
  return s.changed();
}

Reply stackingPlaceDisc(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const app::Id shape = p.id("shape", IdKind::Shape);
  const auto rod = static_cast<unsigned int>(p.integer("rod", 0, kMaxPlace));
  const bool goal = p.boolean("goal");
  SLE_CHECK(p.finish());
  SLE_CHECK(app::StackingEdits(s.document()).placeDisc(problem, shape, rod, goal));
  return s.changed();
}

Reply stackingPlaceAll(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const auto rod = static_cast<unsigned int>(p.integer("rod", 0, kMaxPlace));
  const bool goal = p.boolean("goal");
  SLE_CHECK(p.finish());
  SLE_TRY(const app::DiscsPlaced placed,
          app::StackingEdits(s.document()).placeAllDiscs(problem, rod, goal));
  return Json{{"placed", placed.placed},
              {"error", placed.error.empty() ? Json(nullptr) : Json(placed.error)},
              {"revision", s.revision()}};
}

Reply stackingRemoveDisc(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const bool goal = p.boolean("goal");
  const auto rod = static_cast<unsigned int>(p.integer("rod", 0, kMaxPlace));
  const auto index = static_cast<unsigned int>(p.integer("index", 0, kMaxPlace));
  SLE_CHECK(p.finish());
  SLE_CHECK(app::StackingEdits(s.document()).removeDisc(problem, goal, rod, index));
  return s.changed();
}

Reply stackingClear(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const bool goal = p.boolean("goal");
  SLE_CHECK(p.finish());
  SLE_CHECK(app::StackingEdits(s.document()).clearStacks(problem, goal));
  return s.changed();
}

Reply stackingMoveDisc(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const bool goal = p.boolean("goal");
  const auto rod = static_cast<unsigned int>(p.integer("rod", 0, kMaxPlace));
  const auto index = static_cast<unsigned int>(p.integer("index", 0, kMaxPlace));
  const auto delta = static_cast<int>(p.integer("delta", -1, 1));
  SLE_CHECK(p.finish());
  SLE_TRY(const unsigned int at,
          app::StackingEdits(s.document()).moveDisc(problem, goal, rod, index, delta));
  return Json{{"index", at}, {"revision", s.revision()}};
}

// A board's rods (Rod): their names, the pocket, where they stand and the
// discs they hold, of a problem with `discs` discs.
Json rodsJson(const stacking::RodSet& board, unsigned int discs) {
  Json rods = Json::array();
  for (unsigned int r = 0; r < stacking::totalRods(board); r++) {
    rods.push_back(Json{{"name", stacking::rodName(board, r)},
                        {"pocket", stacking::isPocket(board, r)},
                        {"position", stacking::rodPosition(board, r)},
                        {"capacity", stacking::rodCapacity(board, r, discs)}});
  }
  return rods;
}

// The board of a problem as the Problems tab showed it: its rods with
// their names and capacities, the start's or the goal's stacks, where
// rods and discs stand, the validity bar's sentence, and the center the
// playback's poses are placed around.
Reply stackingBoard(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const bool goal = p.boolean("goal");
  SLE_CHECK(p.finish());
  const app::Document& d = s.document();
  if (!stacking::isStacking(d.puzzle())) {
    return coreError(Errc::Unsupported, "only stacking puzzles have rods");
  }
  SLE_TRY(const unsigned int index, d.problemIndex(problem));
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
      j["center"] = nullptr;
      return j;
    }
    const stacking::RodSet& board = puzzle.getRodSet(pr.getRodSetId());
    const std::vector<stacking::Disc> pieces = stacking::discsOf(puzzle, pr);
    j["rodSet"] = rodSetId(s, pr.getRodSetId());
    j["rods"] = rodsJson(board, static_cast<unsigned int>(pieces.size()));
    j["layout"] = layoutJson(s, stacking::layoutBoard(puzzle, pr, goal), pieces);
    const std::array<double, 3> c = app::stackingCenter(puzzle, pr);
    j["center"] = Json::array({c[0], c[1], c[2]});
    return j;
  });
}

// A rod set drawn alone (the Entities tab's view of it).
Reply stackingRodSetLayout(State& s, Params& p) {
  const app::Id r = p.id("rodSet", IdKind::RodSet);
  SLE_CHECK(p.finish());
  SLE_TRY(const unsigned int index, s.document().rodSetIndex(r));
  return layoutJson(s, app::rodSetLayout(s.document().puzzle(), index), {});
}

// The play calls (stacking.play*, stacking.setStacks): a stacking puzzle
// played by hand (shapelogic/model/stacking_play.h).

// The most rods, discs and transfers a call may give: past any board (a
// stack map keeps rods past the board's sixteen and its pocket only while
// they hold discs), and a game far longer than any played.
constexpr std::size_t kMostRods = 64;
constexpr std::size_t kMostDiscs = 65536;
constexpr std::size_t kMostTransfers = 100000;

// A position: 'stacks', each rod's discs bottom to top by their numbers.
stacking::Stacking stacksOf(Params& p) {
  stacking::Stacking out;
  const Json& list = p.value("stacks");
  const char* const shape = "'stacks' must be a list of rods, each a list of disc numbers";
  if (!list.is_array() || list.size() > kMostRods) {
    p.fail(shape);
    return out;
  }
  std::size_t discs = 0;
  for (const Json& rod : list) {
    if (!rod.is_array()) {
      p.fail(shape);
      return out;
    }
    discs += rod.size();
    if (discs > kMostDiscs) {
      p.fail(shape);
      return out;
    }
    std::vector<unsigned int> stack;
    for (const Json& d : rod) {
      if (!d.is_number_integer() || d.get<std::int64_t>() < 0 ||
          d.get<std::int64_t>() >= static_cast<std::int64_t>(kMostDiscs)) {
        p.fail(shape);
        return out;
      }
      stack.push_back(static_cast<unsigned int>(d.get<std::int64_t>()));
    }
    out.push_back(std::move(stack));
  }
  return out;
}

// A stacking with where its discs rest: each rod's discs as {disc, z}.
Json restingJson(const stacking::Stacking& stacks, const stacking::Heights& heights) {
  Json rods = Json::array();
  for (std::size_t r = 0; r < stacks.size(); r++) {
    Json rod = Json::array();
    for (std::size_t h = 0; h < stacks[r].size(); h++) {
      rod.push_back(Json{{"disc", stacks[r][h]}, {"z", heights[r][h]}});
    }
    rods.push_back(std::move(rod));
  }
  return rods;
}

// The problem of a play call, on a stacking puzzle.
RpcResult<const Problem*> playProblem(const State& s, app::Id problem) {
  return engine::playProblem(s, problem, stacking::isStacking, "only stacking puzzles have rods");
}

Reply stackingPlayStart(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  SLE_CHECK(p.finish());
  SLE_TRY(const Problem* pr, playProblem(s, problem));
  const Puzzle& puzzle = s.document().puzzle();
  const stacking::PlayStart start = stacking::playStart(puzzle, *pr);
  // the board, also when the start is no position yet
  Json rods = Json::array();
  bool panex = false;
  if (pr->rodSetValid(puzzle)) {
    const stacking::RodSet& board = puzzle.getRodSet(pr->getRodSetId());
    rods = rodsJson(board, static_cast<unsigned int>(stacking::discsOf(puzzle, *pr).size()));
    panex = board.panexColumns;
  }
  Json discs = Json::array();
  for (const stacking::Disc& d : start.discs) {
    discs.push_back(
        Json{{"shape", shapeId(s, d.shapeId)}, {"instance", d.instance}, {"size", d.size}});
  }
  return Json{{"error", start.error.empty() ? Json(nullptr) : Json(start.error)},
              {"rods", std::move(rods)},
              {"panex", panex},
              {"discs", std::move(discs)},
              {"stacks", restingJson(start.stacks, start.heights)},
              {"goal", start.hasGoal ? restingJson(start.goal, start.goalHeights) : Json(nullptr)},
              {"goalError", start.goalError.empty() ? Json(nullptr) : Json(start.goalError)},
              {"solved", start.solved}};
}

Reply stackingPlayMoves(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const stacking::Stacking stacks = stacksOf(p);
  SLE_CHECK(p.finish());
  SLE_TRY(const Problem* pr, playProblem(s, problem));
  SLE_TRY(const std::vector<stacking::PlayTransfer> allowed,
          stacking::playMoves(s.document().puzzle(), *pr, stacks));
  Json moves = Json::array();
  for (const stacking::PlayTransfer& t : allowed) {
    moves.push_back(Json{{"from", t.from}, {"to", t.to}, {"z", t.z}, {"solved", t.solved}});
  }
  return Json{{"moves", std::move(moves)}};
}

Reply stackingPlayReplay(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const stacking::Stacking stacks = stacksOf(p);
  std::vector<stacking::PlayMove> moves;
  const Json& list = p.value("moves");
  const char* const shape = "'moves' must be a list of {from: rod, to: rod}";
  if (!list.is_array() || list.size() > kMostTransfers) {
    p.fail(shape);
  } else {
    const auto rodOf = [](const Json& j, const char* name) -> std::int64_t {
      return j.contains(name) && j[name].is_number_integer() ? j[name].get<std::int64_t>() : -1;
    };
    for (const Json& j : list) {
      const std::int64_t from = j.is_object() && j.size() == 2 ? rodOf(j, "from") : -1;
      const std::int64_t to = from < 0 ? -1 : rodOf(j, "to");
      const auto most = static_cast<std::int64_t>(kMostRods);
      if (from < 0 || to < 0 || from >= most || to >= most) {
        p.fail(shape);
        break;
      }
      moves.push_back(
          stacking::PlayMove{static_cast<unsigned int>(from), static_cast<unsigned int>(to)});
    }
  }
  SLE_CHECK(p.finish());
  SLE_TRY(const Problem* pr, playProblem(s, problem));
  SLE_TRY(const stacking::PlayReplay replay,
          stacking::playReplay(s.document().puzzle(), *pr, stacks, moves));
  Json steps = Json::array();
  for (const stacking::PlayStep& step : replay.steps) {
    steps.push_back(Json{{"disc", step.disc}, {"z", step.z}, {"solved", step.solved}});
  }
  return replayJson(replay.solved, std::move(steps), replay.error);
}

Reply stackingSetStacks(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const bool goal = p.boolean("goal");
  const stacking::Stacking stacks = stacksOf(p);
  SLE_CHECK(p.finish());
  SLE_TRY(const bool changed, app::StackingEdits(s.document()).setStacks(problem, goal, stacks));
  return Json{{"changed", changed}, {"revision", s.revision()}};
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
  t["stacking.playStart"] = stackingPlayStart;
  t["stacking.playMoves"] = stackingPlayMoves;
  t["stacking.playReplay"] = stackingPlayReplay;
  t["stacking.setStacks"] = stackingSetStacks;
}

}  // namespace sl::engine

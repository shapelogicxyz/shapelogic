// SPDX-License-Identifier: GPL-3.0-or-later
// tray.* (api/tray.json): a Tilted Square puzzle's tray (decision 0045), its
// placements and the job that tries them all (stage G).
#include "convert.h"
#include "engine.h"

#include "shapelogic/app/tray_edits.h"
#include "shapelogic/model/tilted_tray.h"

#include <array>
#include <numeric>
#include <utility>
#include <vector>

namespace sl::engine {

RpcResult<tilted::Tray> readTraySettings(const Json& given, const tilted::Tray& base) {
  if (!given.is_object()) {
    return std::unexpected(invalidParams("'tray' must be an object"));
  }
  Params p(given);
  // size sets both sides; width and height each go over it
  const bool sized = p.has("size");
  const auto size = static_cast<unsigned int>(p.optInteger("size", 1, 1, tilted::Tray::kMaxSize));
  const auto width = static_cast<unsigned int>(
      p.optInteger("width", sized ? size : base.width, 1, tilted::Tray::kMaxSize));
  const auto height = static_cast<unsigned int>(
      p.optInteger("height", sized ? size : base.height, 1, tilted::Tray::kMaxSize));
  auto over = static_cast<unsigned int>(p.optInteger("over", base.over, 1, tilted::Tray::kMaxSlope));
  auto down = static_cast<unsigned int>(p.optInteger("down", base.down, 0, tilted::Tray::kMaxSlope));
  const double allowance = p.optNumber("allowance", base.allowance, 0.0, tilted::Tray::kMaxAllowance);
  const tilted::Tray::Placement placement =
      p.optChoice("placement", trayPlacementNames()).value_or(base.placement);
  const double shiftX =
      p.optNumber("shiftX", base.shiftX, -tilted::Tray::kMaxShift, tilted::Tray::kMaxShift);
  const double shiftY =
      p.optNumber("shiftY", base.shiftY, -tilted::Tray::kMaxShift, tilted::Tray::kMaxShift);
  if (RpcResult<void> e = p.finish(); !e) {
    RpcError err = e.error();
    err.message = "tray: " + err.message;
    return std::unexpected(err);
  }
  if (down > over) {
    return std::unexpected(toRpc(
        Error{Errc::InvalidArgument, "tray: down is above over (the slope is at most 1)"}));
  }
  // the slope in lowest terms; a straight tray is 0 down, 1 over
  const unsigned int g = std::gcd(over, down);
  over /= g;
  down /= g;
  tilted::Tray t = width == base.width && height == base.height
                       ? base
                       : tilted::withSize(base, width, height);
  t.over = over;
  t.down = down;
  t.allowance = allowance;
  t.placement = placement;
  t.shiftX = shiftX;
  t.shiftY = shiftY;
  return t;
}

Json placementFitJson(const app::PlacementFitProgress& f) {
  Json boards = Json::array();
  for (const app::BoardFit& b : f.boards) {
    boards.push_back(Json{{"shiftX", b.board.shiftX},
                          {"shiftY", b.board.shiftY},
                          {"whole", b.board.whole},
                          {"counted", b.counted},
                          {"assemblies", b.assemblies}});
  }
  return Json{{"complete", f.complete},
              {"total", f.boards.size()},
              {"done", f.done},
              {"current", f.current},
              {"stopped", f.stopped},
              {"failure", f.failure ? Json(*f.failure) : Json(nullptr)},
              {"boards", std::move(boards)}};
}

Json trayJson(const app::Document& d) {
  if (!d.tilted()) {
    return nullptr;
  }
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access): tilted() says the puzzle has one
  return trayJson(*d.puzzle().tray(), d.trayLayout());
}

Json trayJson(const tilted::Tray& t, const tilted::Layout& lay) {
  Json j = Json::object();
  j["width"] = t.width;
  j["height"] = t.height;
  j["over"] = t.over;
  j["down"] = t.down;
  j["allowance"] = t.allowance;
  j["placement"] = nameOf(trayPlacementNames(), t.placement);
  j["shiftX"] = t.shiftX;
  j["shiftY"] = t.shiftY;
  Json walls = Json::array();
  for (unsigned int y = 0; y < t.height; y++) {
    for (unsigned int x = 0; x < t.width; x++) {
      if (t.isWall(x, y)) {
        walls.push_back(Json::array({x, y}));
      }
    }
  }
  j["walls"] = std::move(walls);
  j["degrees"] = t.degrees();
  j["whole"] = lay.whole;
  j["cut"] = lay.cut;
  j["box"] = Json::array({lay.sizeX, lay.sizeY});
  std::string cells;
  cells.reserve(lay.cells.size());
  for (const tilted::Cell c : lay.cells) {
    cells.push_back(c == tilted::Cell::Whole ? 'w' : c == tilted::Cell::Cut ? 'c' : 'o');
  }
  j["cells"] = std::move(cells);
  const auto square = [](const std::array<tilted::Point, 4>& pts) {
    Json out = Json::array();
    for (const tilted::Point& p : pts) {
      out.push_back(Json::array({p.x, p.y}));
    }
    return out;
  };
  j["corners"] = square(lay.corners);
  Json polygons = Json::array();
  for (const std::array<tilted::Point, 4>& w : lay.walls) {
    polygons.push_back(square(w));
  }
  j["wallPolygons"] = std::move(polygons);
  return j;
}

namespace {

/// The tray the settings go over: the open puzzle's, or a new one.
tilted::Tray baseTray(const State& s) {
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access): tilted() says the puzzle has one
  return s.document().tilted() ? *s.document().puzzle().tray() : tilted::Tray{};
}

Reply traySet(State& s, Params& p) {
  const Json& given = p.value("tray");
  SLE_CHECK(p.finish());
  if (!s.document().tilted()) {
    return coreError(Errc::Unsupported, "the puzzle has no tilted tray");
  }
  RpcResult<tilted::Tray> tray = readTraySettings(given, baseTray(s));
  if (!tray) {
    return std::unexpected(tray.error());
  }
  SLE_CHECK(app::TrayEdits(s.document()).setTray(std::move(*tray)));
  return s.changed();
}

Reply traySetWalls(State& s, Params& p) {
  const Json& given = p.value("squares");
  const bool wall = p.boolean("wall");
  SLE_CHECK(p.finish());
  std::vector<std::array<unsigned int, 2>> squares;
  if (!given.is_array()) {
    return std::unexpected(invalidParams("'squares' must be an array"));
  }
  for (const Json& sq : given) {
    if (!sq.is_array() || sq.size() != 2 || !sq[0].is_number_unsigned() ||
        !sq[1].is_number_unsigned()) {
      return std::unexpected(invalidParams("'squares' holds [x, y] pairs of whole numbers"));
    }
    const auto x = sq[0].get<std::uint64_t>();
    const auto y = sq[1].get<std::uint64_t>();
    if (x >= tilted::Tray::kMaxSize || y >= tilted::Tray::kMaxSize) {
      return coreError(Errc::InvalidArgument, "a square is outside the tray");
    }
    squares.push_back({static_cast<unsigned int>(x), static_cast<unsigned int>(y)});
  }
  SLE_TRY(const unsigned int changed, app::TrayEdits(s.document()).setTrayWalls(squares, wall));
  return Json{{"changed", changed}, {"revision", s.revision()}};
}

Reply trayPreview(State& s, Params& p) {
  const Json& given = p.value("tray");
  SLE_CHECK(p.finish());
  RpcResult<tilted::Tray> read = readTraySettings(given, baseTray(s));
  if (!read) {
    return std::unexpected(read.error());
  }
  tilted::Tray t = std::move(*read);
  if (t.placement == tilted::Tray::Placement::Best) {
    const tilted::Placed best = tilted::bestShift(t);
    t.shiftX = best.shiftX;
    t.shiftY = best.shiftY;
  } else {
    const tilted::Point at = tilted::canonicalShift(t, {t.shiftX, t.shiftY});
    t.shiftX = at.x;
    t.shiftY = at.y;
  }
  return trayJson(t, tilted::layout(t));
}

Reply trayPlacements(State& s, Params& p) {
  SLE_CHECK(p.finish());
  if (!s.document().tilted()) {
    return coreError(Errc::Unsupported, "the puzzle has no tilted tray");
  }
  const tilted::Placements& found = s.document().trayPlacements();
  Json boards = Json::array();
  for (const tilted::Placed& b : found.boards) {
    boards.push_back(Json{{"shiftX", b.shiftX}, {"shiftY", b.shiftY}, {"whole", b.whole}});
  }
  return Json{{"boards", std::move(boards)},
              {"complete", found.complete},
              {"inUse", found.inUse ? Json(*found.inUse) : Json(nullptr)}};
}

Reply trayFitPlacements(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const Json given = p.has("options") ? p.value("options") : Json(nullptr);
  SLE_CHECK(p.finish());
  SLE_TRY(const SolveOptions options, solveOptionsFrom(s, given));
  SLE_TRY(const unsigned int total,
          app::TrayEdits(s.document()).fitPlacementsStart(problem, options));
  State::Watch& w = s.watches[problem];
  w.fitting = true;
  w.fitAt = {};
  return Json{{"total", total}};
}

Reply trayFitPlacementsStop(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  SLE_CHECK(p.finish());
  SLE_CHECK(app::TrayEdits(s.document()).fitPlacementsStop(problem));
  return Json::object();
}

Reply trayFitPlacementsStatus(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  SLE_CHECK(p.finish());
  SLE_TRY(const app::PlacementFitProgress f,
          app::TrayEdits(s.document()).fitPlacementsProgress(problem));
  Json j = placementFitJson(f);
  j["active"] = f.active;
  return j;
}

}  // namespace

void addTrayMethods(MethodTable& t) {
  t["tray.set"] = traySet;
  t["tray.setWalls"] = traySetWalls;
  t["tray.preview"] = trayPreview;
  t["tray.placements"] = trayPlacements;
  t["tray.fitPlacements"] = trayFitPlacements;
  t["tray.fitPlacementsStop"] = trayFitPlacementsStop;
  t["tray.fitPlacementsStatus"] = trayFitPlacementsStatus;
}

}  // namespace sl::engine

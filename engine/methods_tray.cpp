// SPDX-License-Identifier: GPL-3.0-or-later
// tray.* (api/tray.json): a Tilted Square puzzle's tray (decision 0045).
#include "convert.h"
#include "engine.h"

#include "shapelogic/model/tilted_tray.h"

#include <array>
#include <numeric>
#include <utility>
#include <vector>

namespace bt::engine {

RpcResult<tilted::Tray> readTraySettings(const Json& given, const tilted::Tray& base) {
  if (!given.is_object()) {
    return std::unexpected(invalidParams("'tray' must be an object"));
  }
  Params p(given);
  const auto size = static_cast<unsigned int>(p.optInteger("size", base.size, 1, tilted::Tray::kMaxSize));
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
  tilted::Tray t = size == base.size ? base : tilted::withSize(base, size);
  t.over = over;
  t.down = down;
  t.allowance = allowance;
  t.placement = placement;
  t.shiftX = shiftX;
  t.shiftY = shiftY;
  return t;
}

namespace {

/// The tray the settings go over: the open puzzle's, or a new one.
tilted::Tray baseTray(const State& s) {
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access): tilted() says the puzzle has one
  return s.document().tilted() ? *s.document().puzzle().tray() : tilted::Tray{};
}

Reply traySet(State& s, Params& p) {
  const Json& given = p.value("tray");
  BTE_CHECK(p.finish());
  if (!s.document().tilted()) {
    return coreError(Errc::Unsupported, "the puzzle has no tilted tray");
  }
  RpcResult<tilted::Tray> tray = readTraySettings(given, baseTray(s));
  if (!tray) {
    return std::unexpected(tray.error());
  }
  BTE_CHECK(s.document().setTray(std::move(*tray)));
  return s.changed();
}

Reply traySetWalls(State& s, Params& p) {
  const Json& given = p.value("squares");
  const bool wall = p.boolean("wall");
  BTE_CHECK(p.finish());
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
  BTE_TRY(const unsigned int changed, s.document().setTrayWalls(squares, wall));
  return Json{{"changed", changed}, {"revision", s.revision()}};
}

Reply trayPreview(State& s, Params& p) {
  const Json& given = p.value("tray");
  BTE_CHECK(p.finish());
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
  const tilted::Layout lay = tilted::layout(t);
  return Json{{"whole", lay.whole},
              {"cut", lay.cut},
              {"degrees", t.degrees()},
              {"box", Json::array({lay.sizeX, lay.sizeY})},
              {"over", t.over},
              {"down", t.down},
              {"shiftX", t.shiftX},
              {"shiftY", t.shiftY}};
}

}  // namespace

void addTrayMethods(MethodTable& t) {
  t["tray.set"] = traySet;
  t["tray.setWalls"] = traySetWalls;
  t["tray.preview"] = trayPreview;
}

}  // namespace bt::engine

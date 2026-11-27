// SPDX-License-Identifier: GPL-3.0-or-later
// color.* (api/color.json).
#include "convert.h"
#include "engine.h"

namespace sl::engine {

namespace {

Rgb readRgb(Params& p) {
  const std::string text = p.string("rgb");
  const std::optional<Rgb> c = parseRgb(text);
  if (!c) {
    p.fail("'rgb' must be \"#rrggbb\"");
    return {};
  }
  return *c;
}

Reply colorList(State& s, Params& p) {
  SLE_CHECK(p.finish());
  Json colors = Json::array();
  for (unsigned int i = 0; i < s.document().puzzle().colorNumber(); i++) {
    colors.push_back(colorJson(s, i));
  }
  return Json{{"colors", std::move(colors)}};
}

Reply colorAdd(State& s, Params& p) {
  const Rgb rgb = readRgb(p);
  SLE_CHECK(p.finish());
  SLE_TRY(const app::Id id, s.document().addColor(rgb));
  return Json{{"color", formatId(IdKind::Color, id)}, {"revision", s.revision()}};
}

Reply colorEdit(State& s, Params& p) {
  const app::Id color = p.id("color", IdKind::Color);
  const Rgb rgb = readRgb(p);
  SLE_CHECK(p.finish());
  SLE_CHECK(s.document().editColor(color, rgb));
  return s.changed();
}

Reply colorDelete(State& s, Params& p) {
  const app::Id color = p.id("color", IdKind::Color);
  SLE_CHECK(p.finish());
  SLE_CHECK(s.document().deleteColor(color));
  return s.changed();
}

}  // namespace

void addColorMethods(MethodTable& t) {
  t["color.list"] = colorList;
  t["color.add"] = colorAdd;
  t["color.edit"] = colorEdit;
  t["color.delete"] = colorDelete;
}

}  // namespace sl::engine

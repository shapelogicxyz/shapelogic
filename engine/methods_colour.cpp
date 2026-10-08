// SPDX-License-Identifier: GPL-3.0-or-later
// colour.* (api/colour.json).
#include "convert.h"
#include "engine.h"

namespace bt::engine {

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

Reply colourList(State& s, Params& p) {
  BTE_CHECK(p.finish());
  Json colours = Json::array();
  for (unsigned int i = 0; i < s.document().puzzle().colorNumber(); i++) {
    colours.push_back(colourJson(s, i));
  }
  return Json{{"colours", std::move(colours)}};
}

Reply colourAdd(State& s, Params& p) {
  const Rgb rgb = readRgb(p);
  BTE_CHECK(p.finish());
  BTE_TRY(const app::Id id, s.document().addColour(rgb));
  return Json{{"colour", formatId(IdKind::Colour, id)}, {"revision", s.revision()}};
}

Reply colourEdit(State& s, Params& p) {
  const app::Id colour = p.id("colour", IdKind::Colour);
  const Rgb rgb = readRgb(p);
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().editColour(colour, rgb));
  return s.changed();
}

Reply colourDelete(State& s, Params& p) {
  const app::Id colour = p.id("colour", IdKind::Colour);
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().deleteColour(colour));
  return s.changed();
}

}  // namespace

void addColourMethods(MethodTable& t) {
  t["colour.list"] = colourList;
  t["colour.add"] = colourAdd;
  t["colour.edit"] = colourEdit;
  t["colour.delete"] = colourDelete;
}

}  // namespace bt::engine

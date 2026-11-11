// SPDX-License-Identifier: GPL-3.0-or-later
// export.* (api/export.json): the STL export (deferred part 5): each
// grid's mesher parameters, a shape's STL mesh for the dialog's preview
// and its volume, the STL file as bytes, and the pieces of a solution's
// export with their file names, and the file names of shapes exported
// together.
#include "convert.h"
#include "engine.h"

#include "shapelogic/app/queries.h"
#include "shapelogic/mesh/draw_mesh.h"
#include "shapelogic/mesh/modifiers.h"
#include "shapelogic/mesh/stl.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace bt::engine {

namespace {

// at most this many values (the sphere grid has eight parameters)
constexpr std::size_t kMaxValues = 16;
// the most bytes of a solid's name
constexpr std::size_t kMaxName = 255;
// the most shapes named at once
constexpr std::size_t kMaxShapes = 65536;

std::string_view typeName(mesh::StlParameterType t) {
  switch (t) {
    case mesh::StlParameterType::Double:
      return "number";
    case mesh::StlParameterType::PositiveDouble:
      return "positiveNumber";
    case mesh::StlParameterType::PositiveInteger:
      return "positiveInteger";
    case mesh::StlParameterType::Switch:
      return "switch";
  }
  return "number";
}

// The values of the request's parameters, or the grid's defaults when it
// has none; the count is checked by the core.
std::vector<double> readValues(const State& s, Params& p) {
  std::vector<double> values;
  if (!p.has("parameters")) {
    for (const mesh::StlParameter& q :
         mesh::stlParameters(s.document().puzzle().getGridType().getType())) {
      values.push_back(q.defaultValue);
    }
    return values;
  }
  const Json& v = p.value("parameters");
  if (!v.is_array() || v.size() > kMaxValues) {
    p.fail("'parameters' must be an array of at most 16 numbers");
    return values;
  }
  for (const Json& x : v) {
    if (!x.is_number()) {
      p.fail("'parameters' must be an array of numbers");
      return {};
    }
    values.push_back(x.get<double>());
  }
  return values;
}

Reply exportStlParameters(State& s, Params& p) {
  const GridKind grid =
      p.optChoice("grid", gridNames()).value_or(s.document().puzzle().getGridType().getType());
  BTE_CHECK(p.finish());
  Json out = Json::array();
  for (const mesh::StlParameter& q : mesh::stlParameters(grid)) {
    out.push_back(Json{{"name", q.name},
                       {"type", typeName(q.type)},
                       {"default", q.defaultValue},
                       {"tooltip", q.tooltip}});
  }
  return Json{{"parameters", std::move(out)}};
}

Reply exportStlMesh(State& s, Params& p) {
  const app::Id shape = p.id("shape", IdKind::Shape);
  const std::vector<double> values = readValues(s, p);
  BTE_CHECK(p.finish());
  BTE_TRY(const mesh::Polyhedron poly, s.document().stlMesh(shape, values));
  Json out = meshReply(s, mesh::toDrawMesh(poly, false));
  out["volume"] = mesh::volume(poly);
  return out;
}

Reply exportStl(State& s, Params& p) {
  const app::Id shape = p.id("shape", IdKind::Shape);
  const std::vector<double> values = readValues(s, p);
  const bool binary = p.optBoolean("binary", true);
  const std::string name = p.optString("name").value_or("");
  BTE_CHECK(p.finish());
  if (name.size() > kMaxName) {
    return coreError(Errc::InvalidArgument, "the name is longer than 255 bytes");
  }
  for (const char c : name) {
    if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) {
      return coreError(Errc::InvalidArgument, "the name cannot hold control characters");
    }
  }
  BTE_TRY(const mesh::Polyhedron poly, s.document().stlMesh(shape, values));
  const std::vector<std::uint8_t> file = mesh::stlFile(poly, name, binary);
  FrameBuilder f;
  f.add("file", file);
  return Json{{"frame", s.sendFrame(f)}};
}

Reply exportSolutionPieces(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const app::Id solution = p.id("solution", IdKind::Solution);
  BTE_CHECK(p.finish());
  BTE_TRY(const std::vector<app::StlPiece> pieces, s.document().stlPieces(problem, solution));
  Json out = Json::array();
  for (const app::StlPiece& piece : pieces) {
    out.push_back(Json{{"shape", shapeId(s, piece.shape)},
                       {"count", piece.count},
                       {"fileName", piece.fileName}});
  }
  return Json{{"pieces", std::move(out)}};
}

Reply exportStlFileNames(State& s, Params& p) {
  const Json& list = p.value("shapes");
  std::vector<app::Id> ids;
  if (!list.is_array() || list.empty() || list.size() > kMaxShapes) {
    p.fail("'shapes' must be a list of 1 to 65536 shape ids");
  } else {
    for (const Json& v : list) {
      const std::optional<std::uint64_t> id =
          v.is_string() ? parseId(IdKind::Shape, v.get<std::string>()) : std::nullopt;
      if (!id) {
        p.fail("'shapes' must be a list of shape ids (s1, ...)");
        break;
      }
      ids.push_back(*id);
    }
  }
  BTE_CHECK(p.finish());
  std::vector<unsigned int> shapes;
  for (const app::Id id : ids) {
    BTE_TRY(const unsigned int index, s.document().shapeIndex(id));
    shapes.push_back(index);
  }
  return Json{{"fileNames", app::stlFileNames(s.document().puzzle(), shapes)}};
}

}  // namespace

void addExportMethods(MethodTable& t) {
  t["export.stlParameters"] = exportStlParameters;
  t["export.stlMesh"] = exportStlMesh;
  t["export.stl"] = exportStl;
  t["export.solutionPieces"] = exportSolutionPieces;
  t["export.stlFileNames"] = exportStlFileNames;
}

}  // namespace bt::engine

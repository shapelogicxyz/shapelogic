// SPDX-License-Identifier: GPL-3.0-or-later
// shape.*, grid.geometry and grid.neighbor (api/shape.json).
#include "convert.h"
#include "engine.h"

#include "shapelogic/app/layer_geometry.h"
#include "shapelogic/app/queries.h"
#include "shapelogic/app/shape_tools.h"
#include "shapelogic/base/int_cast.h"
#include "shapelogic/grid/grid_type.h"
#include "shapelogic/grid/voxel.h"

#include <algorithm>
#include <limits>
#include <string>

namespace bt::engine {

namespace {

// The largest space grid.geometry draws (the document's limit for shapes).
constexpr std::uint64_t kMaxCells = std::uint64_t{1} << 24;
// The most cells of one layer it draws.
constexpr std::uint64_t kMaxLayerCells = std::uint64_t{1} << 16;

Reply shapeList(State& s, Params& p) {
  BTE_CHECK(p.finish());
  Json shapes = Json::array();
  for (unsigned int i = 0; i < s.document().puzzle().getNumberOfShapes(); i++) {
    shapes.push_back(shapeSummary(s, i));
  }
  return Json{{"shapes", std::move(shapes)}};
}

Reply shapeGet(State& s, Params& p) {
  const app::Id shape = p.id("shape", IdKind::Shape);
  const std::optional<std::int64_t> layer = p.maybeInteger("layer", 0, std::numeric_limits<int>::max());
  BTE_CHECK(p.finish());
  BTE_TRY(const unsigned int index, s.document().shapeIndex(shape));
  const Voxel& v = s.document().puzzle().getShape(index);
  const unsigned int colors = s.document().puzzle().colorNumber();
  Json cells = Json::array();
  for (unsigned int z = 0; z < v.getZ(); z++) {
    if (layer && z != *layer) {
      continue;
    }
    for (unsigned int y = 0; y < v.getY(); y++) {
      for (unsigned int x = 0; x < v.getX(); x++) {
        const int state = v.getState(x, y, z);
        if (state == Voxel::kEmpty) {
          continue;
        }
        Json c = Json::object();
        c["at"] = Json::array({x, y, z});
        c["state"] = state == Voxel::kVariable ? "variable" : "fixed";
        const unsigned int color = v.getColor(x, y, z);
        if (color > 0 && color <= colors) {
          c["color"] = colorId(s, color - 1);
        }
        cells.push_back(std::move(c));
      }
    }
  }
  return Json{{"shape", shapeSummary(s, index)}, {"cells", std::move(cells)}};
}

Json made(State& s, app::Id shape) {
  return Json{{"shape", formatId(IdKind::Shape, shape)}, {"revision", s.revision()}};
}

std::optional<std::array<unsigned int, 3>> readSize(Params& p, std::string_view name) {
  const std::array<int, 3> c = p.cell(name);
  for (const int v : c) {
    if (v < 1) {
      p.fail("'" + std::string(name) + "' must be at least 1 in each direction");
      return std::nullopt;
    }
  }
  return std::array<unsigned int, 3>{static_cast<unsigned int>(c[0]),
                                     static_cast<unsigned int>(c[1]),
                                     static_cast<unsigned int>(c[2])};
}

Reply shapeCreate(State& s, Params& p) {
  std::optional<std::array<unsigned int, 3>> size;
  if (p.has("size")) {
    size = readSize(p, "size");
  }
  BTE_CHECK(p.finish());
  BTE_TRY(const app::Id id, s.document().createShape(size));
  return made(s, id);
}

Reply shapeDuplicate(State& s, Params& p) {
  const app::Id shape = p.id("shape", IdKind::Shape);
  BTE_CHECK(p.finish());
  BTE_TRY(const app::Id id, s.document().duplicateShape(shape));
  return made(s, id);
}

Reply shapeDelete(State& s, Params& p) {
  const app::Id shape = p.id("shape", IdKind::Shape);
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().deleteShape(shape));
  return s.changed();
}

Reply shapeMove(State& s, Params& p) {
  const app::Id shape = p.id("shape", IdKind::Shape);
  const unsigned int to = p.count("toIndex");
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().moveShape(shape, to));
  return s.changed();
}

Reply shapeRename(State& s, Params& p) {
  const app::Id shape = p.id("shape", IdKind::Shape);
  std::string name = p.string("name");
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().renameShape(shape, std::move(name)));
  return s.changed();
}

Reply shapeSetWeight(State& s, Params& p) {
  const app::Id shape = p.id("shape", IdKind::Shape);
  const auto weight = static_cast<int>(
      p.integer("weight", std::numeric_limits<int>::min(), std::numeric_limits<int>::max()));
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().setShapeWeight(shape, weight));
  return s.changed();
}

Reply shapeSetColor(State& s, Params& p) {
  const app::Id shape = p.id("shape", IdKind::Shape);
  const Json& rgb = p.value("rgb");
  std::optional<std::uint32_t> color;
  if (!rgb.is_null()) {
    const std::optional<Rgb> c = rgb.is_string() ? parseRgb(rgb.get<std::string>()) : std::nullopt;
    if (!c) {
      p.fail("'rgb' must be \"#rrggbb\" or null");
    } else {
      color = std::uint32_t{c->r} << 16 | std::uint32_t{c->g} << 8 | c->b;
    }
  }
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().setShapeColor(shape, color));
  return s.changed();
}

// a picture's fit by its name
std::optional<ImageFit> fitOf(const Json& j) {
  if (!j.is_string()) {
    return std::nullopt;
  }
  const std::string f = j.get<std::string>();
  return f == "crop"      ? std::optional{ImageFit::Crop}
         : f == "stretch" ? std::optional{ImageFit::Stretch}
         : f == "manual"  ? std::optional{ImageFit::Manual}
                          : std::nullopt;
}

// the part of a picture shown, [x, y, width, height] in pixels: x and y may
// be below 0 (decision 0047: a picture smaller than its box), the sizes at
// least 1
std::optional<std::array<int, 4>> srcOf(const Json& j) {
  constexpr std::int64_t kMax = 0x7FFFFFFF;
  if (!j.is_array() || j.size() != 4 ||
      !std::all_of(j.begin(), j.end(), [](const Json& n) { return n.is_number_integer(); })) {
    return std::nullopt;
  }
  std::array<int, 4> out{};
  for (std::size_t k = 0; k < 4; k++) {
    const auto n = j[k].get<std::int64_t>();
    if (n < (k < 2 ? -kMax : 1) || n > kMax) {
      return std::nullopt;
    }
    out.at(k) = static_cast<int>(n);
  }
  return out;
}

Reply shapeSetImage(State& s, Params& p) {
  const app::Id shape = p.id("shape", IdKind::Shape);
  app::Document::ImageChoice choice;
  if (const std::optional<std::string> data = p.optString("data")) {
    choice.bytes = base64Decode(*data);
    if (!choice.bytes) {
      p.fail("'data' is not base64");
    }
  }
  if (const std::optional<std::int64_t> id = p.maybeInteger("image", 0, 0x7FFFFFFF)) {
    choice.id = static_cast<unsigned int>(*id);
  }
  choice.fit = p.choice<ImageFit>(
      "fit", {{"crop", ImageFit::Crop}, {"stretch", ImageFit::Stretch}, {"manual", ImageFit::Manual}});
  if (const std::optional<std::array<int, 4>> src = srcOf(p.value("src"))) {
    choice.src = *src;
  } else {
    p.fail("'src' must be [x, y, width, height] in pixels");
  }
  choice.border = p.optBoolean("border", false);
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().setShapeImage(shape, choice));
  return s.changed();
}

Reply shapeSetImages(State& s, Params& p) {
  const app::Id shape = p.id("shape", IdKind::Shape);
  std::vector<app::Document::ImageLayer> layers;
  const Json& list = p.value("layers");
  if (!list.is_array()) {
    p.fail("'layers' must be a list of pictures");
  } else {
    for (const Json& l : list) {
      app::Document::ImageLayer layer;
      const std::optional<ImageFit> fit = l.is_object() && l.contains("fit")
                                              ? fitOf(l.at("fit"))
                                              : std::nullopt;
      const std::optional<std::array<int, 4>> src =
          l.is_object() && l.contains("src") ? srcOf(l.at("src")) : std::nullopt;
      if (!fit || !src) {
        p.fail("each layer needs 'fit' and 'src' ([x, y, width, height] in pixels)");
        break;
      }
      layer.fit = *fit;
      layer.src = *src;
      if (l.contains("data")) {
        layer.bytes = l.at("data").is_string() ? base64Decode(l.at("data").get<std::string>())
                                               : std::nullopt;
        if (!layer.bytes) {
          p.fail("a layer's 'data' is not base64");
          break;
        }
      }
      if (l.contains("image")) {
        const Json& id = l.at("image");
        if (!id.is_number_integer() || id.get<std::int64_t>() < 0 ||
            id.get<std::int64_t>() > 0x7FFFFFFF) {
          p.fail("a layer's 'image' must be a picture's number");
          break;
        }
        layer.id = static_cast<unsigned int>(id.get<std::int64_t>());
      }
      layers.push_back(std::move(layer));
    }
  }
  const bool border = p.optBoolean("border", false);
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().setShapeImages(shape, layers, border));
  return s.changed();
}

Reply shapeClearImage(State& s, Params& p) {
  const app::Id shape = p.id("shape", IdKind::Shape);
  const bool border = p.optBoolean("border", false);
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().clearShapeImage(shape, border));
  return s.changed();
}

Reply shapeSetBorder(State& s, Params& p) {
  const app::Id shape = p.id("shape", IdKind::Shape);
  app::Document::BorderChange change;
  if (p.has("enabled")) {
    change.enabled = p.boolean("enabled");
  }
  change.corners = p.optChoice<BorderCorners>(
      "corners", {{"round", BorderCorners::Round}, {"sharp", BorderCorners::Sharp}});
  change.material = p.optChoice<BorderMaterial>("material", {{"smooth", BorderMaterial::Smooth},
                                                             {"textured", BorderMaterial::Textured},
                                                             {"wood", BorderMaterial::Wood}});
  if (p.has("thickness")) {
    change.thickness = static_cast<unsigned int>(p.integer("thickness", 0, kMaxBorderThickness));
  }
  if (p.has("color")) {
    const Json& rgb = p.value("color");
    std::optional<std::uint32_t> color;
    if (!rgb.is_null()) {
      const std::optional<Rgb> c = rgb.is_string() ? parseRgb(rgb.get<std::string>()) : std::nullopt;
      if (!c) {
        p.fail("'color' must be \"#rrggbb\" or null");
      } else {
        color = std::uint32_t{c->r} << 16 | std::uint32_t{c->g} << 8 | c->b;
      }
    }
    change.color = color;
  }
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().setTrayBorder(shape, change));
  return s.changed();
}

Reply shapeImageData(State& s, Params& p) {
  const auto id = static_cast<int>(p.integer("image", 0, 0x7FFFFFFF));
  BTE_CHECK(p.finish());
  const PuzzleImage* im = s.document().puzzle().findImage(id);
  if (im == nullptr) {
    return coreError(Errc::NotFound, "the puzzle has no picture " + std::to_string(id));
  }
  return Json{{"type", im->type},
              {"width", im->width},
              {"height", im->height},
              {"hash", im->hash},
              {"data", base64Encode(*im->bytes)}};
}

// The mirror and span axes as the editor's bits.
std::uint8_t readSymmetry(Params& p) {
  std::uint8_t bits = 0;
  const auto axes = [&p, &bits](std::string_view name, std::uint8_t x, std::uint8_t y,
                                std::uint8_t z) {
    if (!p.has(name)) {
      (void)p.optString(name);  // marks it read
      return;
    }
    const Json& a = p.value(name);
    if (!a.is_array() || a.size() > 3) {
      p.fail("'" + std::string(name) + "' must be a list of axes");
      return;
    }
    for (const Json& axis : a) {
      if (axis == "x") {
        bits |= x;
      } else if (axis == "y") {
        bits |= y;
      } else if (axis == "z") {
        bits |= z;
      } else {
        p.fail("'" + std::string(name) + "' holds x, y and z only");
      }
    }
  };
  axes("mirror", app::kMirrorX, app::kMirrorY, app::kMirrorZ);
  axes("span", app::kSpanX, app::kSpanY, app::kSpanZ);
  return bits;
}

app::Id readColor(Params& p) { return p.optId("color", IdKind::Color).value_or(0); }

Json cellsChanged(State& s, unsigned int n) {
  return Json{{"revision", s.revision()}, {"changed", n}};
}

Reply shapeSetCells(State& s, Params& p) {
  const app::Id shape = p.id("shape", IdKind::Shape);
  const app::CellTool tool = p.choice("tool", cellToolNames());
  const app::Id color = readColor(p);
  const std::uint8_t symmetry = readSymmetry(p);
  const Json& list = p.value("cells");
  std::vector<app::CellEdit> cells;
  if (!list.is_array()) {
    p.fail("'cells' must be a list of [x, y, z]");
  } else {
    for (const Json& c : list) {
      if (!c.is_array() || c.size() != 3 || !c[0].is_number_integer() ||
          !c[1].is_number_integer() || !c[2].is_number_integer()) {
        p.fail("'cells' must be a list of [x, y, z]");
        break;
      }
      const auto coord = [](const Json& v) {
        const auto n = v.get<std::int64_t>();
        // far outside any space: left out like every cell outside
        return static_cast<int>(std::clamp<std::int64_t>(n, -1, std::numeric_limits<int>::max()));
      };
      cells.push_back(app::CellEdit{coord(c[0]), coord(c[1]), coord(c[2])});
    }
  }
  BTE_CHECK(p.finish());
  BTE_TRY(const unsigned int n, s.document().editCells(shape, tool, color, symmetry, cells));
  return cellsChanged(s, n);
}

Reply shapeSetBox(State& s, Params& p) {
  const app::Id shape = p.id("shape", IdKind::Shape);
  const app::CellTool tool = p.choice("tool", cellToolNames());
  const app::Id color = readColor(p);
  const std::uint8_t symmetry = readSymmetry(p);
  const std::array<int, 3> from = p.cell("from");
  const std::array<int, 3> to = p.cell("to");
  BTE_CHECK(p.finish());
  const app::CellBox box{from[0], from[1], from[2], to[0], to[1], to[2]};
  BTE_TRY(const unsigned int n, s.document().editBox(shape, tool, color, symmetry, box));
  return cellsChanged(s, n);
}

Reply shapeResize(State& s, Params& p) {
  const app::Id shape = p.id("shape", IdKind::Shape);
  const std::optional<std::array<unsigned int, 3>> size = readSize(p, "size");
  const bool toAll = p.optBoolean("toAll", false);
  BTE_CHECK(p.finish());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access): p.finish() passed, so the size was read
  BTE_CHECK(s.document().resizeShape(shape, (*size)[0], (*size)[1], (*size)[2], toAll));
  return s.changed();
}

Reply shapeTaskSet(State& s, Params& p) {
  BTE_CHECK(p.finish());
  Json tasks = Json::array();
  for (const app::ShapeTaskInfo& t : app::taskSet(s.document().puzzle().getGridType().getType())) {
    tasks.push_back(Json{{"task", nameOf(shapeTaskNames(), t.task)},
                         {"group", t.group},
                         {"label", t.label},
                         {"toAll", t.toAll}});
  }
  return Json{{"tasks", std::move(tasks)}};
}

Reply shapeTask(State& s, Params& p) {
  const app::Id shape = p.id("shape", IdKind::Shape);
  const app::ShapeTask task = p.choice("task", shapeTaskNames());
  const bool toAll = p.optBoolean("toAll", false);
  BTE_CHECK(p.finish());
  BTE_TRY(const app::TaskOutcome o, s.document().shapeTask(shape, task, toAll));
  return Json{{"revision", s.revision()}, {"changed", o.changed}, {"cellsChanged", o.cellsChanged}};
}

Reply shapeTaskPreview(State& s, Params& p) {
  const app::Id shape = p.id("shape", IdKind::Shape);
  const app::ShapeTask task = p.choice("task", shapeTaskNames());
  BTE_CHECK(p.finish());
  BTE_TRY(const mesh::DrawMesh m, s.document().shapeTaskPreview(shape, task));
  return meshReply(s, m);
}

Reply shapeInfo(State& s, Params& p) {
  const app::Id shape = p.id("shape", IdKind::Shape);
  BTE_CHECK(p.finish());
  BTE_TRY(const unsigned int index, s.document().shapeIndex(shape));
  const std::vector<app::ShapeInfo> rows = s.document().shapeTable();
  return shapeInfoJson(s, rows[index], index);
}

Reply shapeTable(State& s, Params& p) {
  BTE_CHECK(p.finish());
  const std::vector<app::ShapeInfo> rows = s.document().shapeTable();
  Json out = Json::array();
  for (unsigned int i = 0; i < rows.size(); i++) {
    out.push_back(shapeInfoJson(s, rows[i], i));
  }
  return Json{{"rows", std::move(out)}};
}

Reply shapeMesh(State& s, Params& p) {
  const app::Id shape = p.id("shape", IdKind::Shape);
  const mesh::MeshStyle style = p.optChoice("style", meshStyleNames()).value_or(mesh::MeshStyle::Drawing);
  const std::optional<std::int64_t> transformation = p.maybeInteger("transformation", 0, 0xFFFF);
  BTE_CHECK(p.finish());
  std::optional<unsigned int> turned;
  if (transformation) {
    turned = static_cast<unsigned int>(*transformation);
  }
  BTE_TRY(const mesh::DrawMesh m, s.document().shapeMesh(shape, style, turned));
  return meshReply(s, m);
}

Reply gridGeometry(State& s, Params& p) {
  const std::optional<std::array<unsigned int, 3>> size = readSize(p, "size");
  const unsigned int layer = p.count("layer");
  BTE_CHECK(p.finish());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access): p.finish() passed, so the size was read
  const auto [x, y, z] = *size;
  if (std::uint64_t{x} * y * z > kMaxCells || std::uint64_t{x} * y > kMaxLayerCells) {
    return coreError(Errc::InvalidArgument, "the space would be too large to draw");
  }
  if (layer >= z) {
    return coreError(Errc::InvalidArgument, "the layer is outside the space");
  }
  const std::unique_ptr<Voxel> v = s.document().puzzle().getGridType().makeVoxel(x, y, z);
  const app::LayerGeometry g = app::layerGeometry(*v, layer);
  Json cells = Json::array();
  for (const app::LayerCell& c : g.cells) {
    Json j = Json::object();
    j["at"] = Json::array({c.x, c.y, layer});
    if (c.circle) {
      j["circle"] = Json{{"center", Json::array({c.center.x, c.center.y})}, {"radius", c.radius}};
    } else {
      Json poly = Json::array();
      for (const app::LayerPoint& q : c.polygon) {
        poly.push_back(Json::array({q.x, q.y}));
      }
      j["polygon"] = std::move(poly);
      j["neighbors"] = c.neighbors;
    }
    cells.push_back(std::move(j));
  }
  return Json{{"width", g.width}, {"height", g.height}, {"cells", std::move(cells)}};
}

// The cell across a side: the side numbering of the meshes' voxelFace,
// which is getNeighbor's (voxel_mesh.cpp, sphere_mesh.cpp). Neither
// getNeighbor nor validCoordinate depends on the size, so a one-cell space
// of the grid answers for any.
Reply gridNeighbor(State& s, Params& p) {
  const std::optional<std::array<unsigned int, 3>> size = readSize(p, "size");
  const std::array<int, 3> cell = p.cell("cell");
  const unsigned int face = p.count("face");
  BTE_CHECK(p.finish());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access): p.finish() passed, so the size was read
  const auto [x, y, z] = *size;
  if (std::uint64_t{x} * y * z > kMaxCells) {
    return coreError(Errc::InvalidArgument, "the space would be too large");
  }
  const auto inside = [&](int cx, int cy, int cz) {
    return cx >= 0 && cy >= 0 && cz >= 0 && cx < asInt(x) && cy < asInt(y) && cz < asInt(z);
  };
  const std::unique_ptr<Voxel> v = s.document().puzzle().getGridType().makeVoxel(1, 1, 1);
  const auto [cx, cy, cz] = cell;
  if (!inside(cx, cy, cz) || !v->validCoordinate(cx, cy, cz)) {
    return coreError(Errc::InvalidArgument, "the cell is not a cell of the space");
  }
  int nx = 0;
  int ny = 0;
  int nz = 0;
  if (!v->getNeighbor(face, 0, cx, cy, cz, &nx, &ny, &nz)) {
    return coreError(Errc::InvalidArgument, "the cell has no side " + std::to_string(face));
  }
  if (!inside(nx, ny, nz) || !v->validCoordinate(nx, ny, nz)) {
    return Json{{"cell", nullptr}};
  }
  return Json{{"cell", Json::array({nx, ny, nz})}};
}

}  // namespace

void addShapeMethods(MethodTable& t) {
  t["shape.list"] = shapeList;
  t["shape.get"] = shapeGet;
  t["shape.create"] = shapeCreate;
  t["shape.duplicate"] = shapeDuplicate;
  t["shape.delete"] = shapeDelete;
  t["shape.move"] = shapeMove;
  t["shape.rename"] = shapeRename;
  t["shape.setWeight"] = shapeSetWeight;
  t["shape.setColor"] = shapeSetColor;
  t["shape.setImage"] = shapeSetImage;
  t["shape.setImages"] = shapeSetImages;
  t["shape.clearImage"] = shapeClearImage;
  t["shape.setBorder"] = shapeSetBorder;
  t["shape.imageData"] = shapeImageData;
  t["shape.setCells"] = shapeSetCells;
  t["shape.setBox"] = shapeSetBox;
  t["shape.resize"] = shapeResize;
  t["shape.taskSet"] = shapeTaskSet;
  t["shape.task"] = shapeTask;
  t["shape.taskPreview"] = shapeTaskPreview;
  t["shape.info"] = shapeInfo;
  t["shape.table"] = shapeTable;
  t["shape.mesh"] = shapeMesh;
  t["grid.geometry"] = gridGeometry;
  t["grid.neighbor"] = gridNeighbor;
}

}  // namespace bt::engine

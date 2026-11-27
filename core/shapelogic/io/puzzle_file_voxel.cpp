// SPDX-License-Identifier: GPL-3.0-or-later
// The <voxel> element (puzzle_file_internal.h; cleanup item 21.5 moved it
// here from puzzle_file.cpp): a shape's cells, its marks, its pictures and
// its tray border, read and written. Ported from the XML load and save code
// of src/lib/voxel.cpp.
#include "shapelogic/io/puzzle_file_internal.h"

#include "shapelogic/base/base64.h"
#include "shapelogic/base/int_cast.h"
#include "shapelogic/model/sliding.h"

#include <array>
#include <cstdio>
#include <sstream>

namespace sl::io::detail {

namespace {

/// Exactly four space-separated integers, the last two positive; nothing
/// when the text is anything else.
std::optional<std::array<int, 4>> fourInts(const std::string& text) {
  std::array<int, 4> out{};
  const char* p = text.c_str();
  for (int& v : out) {
    char* end = nullptr;
    const long n = std::strtol(p, &end, 10);
    if (end == p || n < -0x10000 || n > 0x10000) {
      return std::nullopt;
    }
    v = static_cast<int>(n);
    p = end;
  }
  while (*p == ' ') {
    p++;
  }
  if (*p != '\0' || out[2] <= 0 || out[3] <= 0) {
    return std::nullopt;
  }
  return out;
}

/// The picture of a shape (decision 0038): image, imageFit, imageSrc,
/// imageBox, imageTurn; with the prefix "borderImage", the picture of its
/// tray's border (decision 0046). A picture with a bad attribute is
/// dropped; the reader checks the image id once it has the <images>
/// (readPuzzleElement).
ShapeImage readShapeImage(const XmlElement& e, const std::string& prefix = "image") {
  ShapeImage im;
  const std::string* id = e.findAttribute(prefix);
  if (id == nullptr) {
    return im;
  }
  const std::optional<std::array<int, 4>> src = fourInts(e.attribute(prefix + "Src"));
  const std::optional<std::array<int, 4>> box = fourInts(e.attribute(prefix + "Box"));
  const int turn = atoiOf(e.attribute(prefix + "Turn"));
  const std::string fit = e.attribute(prefix + "Fit");
  // the part shown may reach past the picture's edges (decision 0047)
  if (id->empty() || atoiOf(*id) < 0 || !src || !box || src->at(2) <= 0 || src->at(3) <= 0 ||
      turn < 0 || turn > 7) {
    return im;
  }
  im.id = atoiOf(*id);
  im.fit = fit == "stretch"  ? ImageFit::Stretch
           : fit == "manual" ? ImageFit::Manual
                             : ImageFit::Crop;
  im.src = *src;
  im.box = *box;
  im.turn = asUnsigned(turn);
  return im;
}

/// The attribute prefix of the k-th picture of a stack (decision 0047):
/// `base` for the bottom one, as before, then base2, base3 and so on.
std::string imagePrefix(const std::string& base, std::size_t k) {
  return k == 0 ? base : base + std::to_string(k + 1);
}

/// A stack of pictures: the one at `base`, then base2, base3... while
/// there is one; a picture with a bad attribute is left out.
ShapeImages readShapeImages(const XmlElement& e, const std::string& base) {
  ShapeImages out;
  for (std::size_t k = 0; k < kMaxShapeImages && e.findAttribute(imagePrefix(base, k)) != nullptr;
       k++) {
    if (ShapeImage im = readShapeImage(e, imagePrefix(base, k)); im.present()) {
      out.push_back(im);
    }
  }
  return out;
}

/// Whether an attribute belongs to a stack of pictures: image, image2...,
/// borderImage, borderImage2..., each with Fit, Src, Box or Turn or none.
bool isImageAttribute(std::string_view name) {
  for (const std::string_view base : {std::string_view{"borderImage"}, std::string_view{"image"}}) {
    if (!name.starts_with(base)) {
      continue;
    }
    std::string_view rest = name.substr(base.size());
    while (!rest.empty() && rest.front() >= '0' && rest.front() <= '9') {
      rest.remove_prefix(1);
    }
    if (rest.empty() || rest == "Fit" || rest == "Src" || rest == "Box" || rest == "Turn") {
      return true;
    }
  }
  return false;
}

/// The border of a board's tray (decision 0046): border ("1" when shown),
/// borderCorners, borderMaterial, borderColor, borderThickness and the
/// borderImage attributes. A value this version does not know reads as the default.
TrayBorder readTrayBorder(const XmlElement& e) {
  TrayBorder b;
  b.enabled = e.attribute("border") == "1";
  if (e.attribute("borderCorners") == "sharp") {
    b.corners = BorderCorners::Sharp;
  }
  const std::string material = e.attribute("borderMaterial");
  b.material = material == "textured" ? BorderMaterial::Textured
               : material == "wood"   ? BorderMaterial::Wood
                                      : BorderMaterial::Smooth;
  const std::string col = e.attribute("borderColor");
  if (col.size() == 7 && col[0] == '#') {
    b.color = static_cast<int>(std::strtoul(col.c_str() + 1, nullptr, 16) & 0xFFFFFF);
  }
  const unsigned long thick = std::strtoul(e.attribute("borderThickness").c_str(), nullptr, 10);
  b.thickness = thick <= kMaxBorderThickness ? static_cast<unsigned int>(thick) : 0;
  b.images = readShapeImages(e, "borderImage");
  return b;
}

}  // namespace

std::unique_ptr<Voxel> readVoxel(const XmlElement& e, GridType gt) {
  auto required = [&e](const char* name) {
    const std::string* v = e.findAttribute(name);
    if (v == nullptr || v->empty()) {
      error(e, std::string("voxel space requires '") + name + "' attribute");
    }
    return *v;
  };

  const auto sx = asUnsigned(atoiOf(required("x")));
  const auto sy = asUnsigned(atoiOf(required("y")));
  const auto sz = asUnsigned(atoiOf(required("z")));
  const auto type = asUnsigned(atoiOf(required("type")));

  // the sizes come from an untrusted file: keep sx*sy*sz far from overflowing
  if (sx > 0x10000 || sy > 0x10000 || sz > 0x10000) {
    error(e, "voxel space dimension too large");
  }
  if (static_cast<unsigned long long>(sx) * sy * sz > 0x40000000ull) {
    error(e, "voxel space too large");
  }

  // the cells: '_' empty, '#' filled, '+' variable, each optionally followed
  // by its color number
  if (e.text.empty()) {
    error(e, "voxel space requires content");
  }
  const std::string& c = e.text;
  unsigned int idx = 0;
  unsigned int color = 0;

  if (type != 0) {
    error(e, "unknown voxel type");
  }
  // Every cell is a character at least: a text shorter than the space is
  // refused before room is made for the space (three numbers could ask for
  // a gigabyte, which was filled before the text was looked at).
  if (c.size() < static_cast<unsigned long long>(sx) * sy * sz) {
    error(e, "not enough voxels defined for voxelspace");
  }

  std::unique_ptr<Voxel> v = gt.makeVoxel(sx, sy, sz, 0);
  // one bounding box when the shape is read, not one per cell
  const Voxel::BatchEdit batch(*v, Voxel::BatchEdit::Box::Always);

  for (const char ch : c) {
    switch (ch) {
      case '#':
      case '+':
      case '_':
        if (idx >= v->getXYZ()) {
          error(e, "too many voxels defined for voxelspace");
        }
        v->setState(idx++, ch == '#' ? Voxel::kFilled : ch == '+' ? Voxel::kVariable : Voxel::kEmpty);
        color = 0;
        break;
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
        color = color * 10 + static_cast<unsigned int>(ch - '0');
        break;
      default:
        error(e, "unrecognized character in piece voxel space");
    }

    if (color > 63) {
      error(e, "constraint color too big > 63");
    }

    if (idx > 0) {
      v->setColor(idx - 1, color);
    }
  }
  if (idx < v->getXYZ()) {
    error(e, "not enough voxels defined for voxelspace");
  }

  // Start and goal marks of sliding puzzles as files had them before they
  // were the problems' (decision 0027; the original wrote only goal, the
  // starts were the colors): the shape carries them until the puzzle is
  // read, then they go to its problems (sliding::adoptBoardMarks).
  readMarkList(e.attribute("start"), v->getXYZ(),
               [&v](unsigned int i, unsigned int mark) { v->setStartPiece(i, mark); });
  readMarkList(e.attribute("goal"), v->getXYZ(),
               [&v](unsigned int i, unsigned int mark) { v->setGoalPiece(i, mark); });

  const std::array<int, 3> hot = {atoiOf(e.attribute("hx")), atoiOf(e.attribute("hy")),
                                  atoiOf(e.attribute("hz"))};
  for (const int h : hot) {
    if (h < -kMaxFileCoordinate || h > kMaxFileCoordinate) {
      error(e, "voxel space hotspot out of range");
    }
  }
  // (the sphere grid's cells are every second place: its transformations
  // take a hotspot on one for granted)
  if (gt.getType() == GridKind::Spheres && ((hot[0] + hot[1] + hot[2]) & 1) != 0) {
    error(e, "voxel space hotspot is not a place of the sphere grid");
  }
  v->setHotspot(hot[0], hot[1], hot[2]);
  v->setName(e.attribute("name"));

  const std::string weight = e.attribute("weight");
  if (!weight.empty()) {
    v->setWeight(atoiOf(weight));
  }

  const std::string col = e.attribute("color");
  if (col.size() == 7 && col[0] == '#') {
    v->setShapeColor(static_cast<int>(std::strtoul(col.c_str() + 1, nullptr, 16) & 0xFFFFFF));
  }

  const std::string disk = e.attribute("diskSize");
  if (!disk.empty()) {
    // a disc is a shape of twice its size and one more cells a side, so no
    // size is past half of what a side may be; the board's layout
    // multiplies it
    const unsigned int size = asUnsigned(atoiOf(disk));
    if (size > 0x8000) {
      error(e, "disc size out of range");
    }
    v->setDiskSize(size);
  }

  v->setImages(readShapeImages(e, "image"));
  v->setBorder(readTrayBorder(e));

  std::vector<std::pair<std::string, std::string>> extra;
  for (const XmlAttribute& a : e.attributes) {
    static constexpr std::string_view kKnown[] = {
        "x",     "y",        "z",     "type",     "hx",       "hy",       "hz",
        "name",  "start",    "goal",  "weight",   "color",    "diskSize",
        "border", "borderCorners", "borderMaterial", "borderColor", "borderThickness"};
    if (std::find(std::begin(kKnown), std::end(kKnown), a.name) == std::end(kKnown) &&
        !isImageAttribute(a.name)) {
      extra.emplace_back(a.name, a.value);
    }
  }
  v->setExtraAttributes(std::move(extra));

  return v;
}

void writeVoxel(XmlWriter& xml, const Voxel& v) {
  xml.newTag("voxel");

  xml.newAttrib("x", v.getX());
  xml.newAttrib("y", v.getY());
  xml.newAttrib("z", v.getZ());

  if (v.getHx() != 0) {
    xml.newAttrib("hx", v.getHx());
  }
  if (v.getHy() != 0) {
    xml.newAttrib("hy", v.getHy());
  }
  if (v.getHz() != 0) {
    xml.newAttrib("hz", v.getHz());
  }

  if (v.getWeight() != 1) {
    xml.newAttrib("weight", v.getWeight());
  }

  if (!v.getName().empty()) {
    xml.newAttrib("name", v.getName());
  }

  if (v.getDiskSize() != 0) {
    xml.newAttrib("diskSize", v.getDiskSize());
  }

  // "#RRGGBB"; versions that do not know it pass over it
  if (v.getShapeColor() >= 0) {
    std::array<char, 8> c{};
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    std::snprintf(c.data(), c.size(), "#%06X", asUnsigned(v.getShapeColor()) & 0xFFFFFF);
    xml.newAttrib("color", c.data());
  }

  // the picture (decision 0038); the original passes over it
  auto writeImage = [&xml](const ShapeImage& im, const std::string& prefix) {
    auto four = [](const std::array<int, 4>& a) {
      return std::to_string(a[0]) + ' ' + std::to_string(a[1]) + ' ' + std::to_string(a[2]) + ' ' +
             std::to_string(a[3]);
    };
    xml.newAttrib(prefix, im.id);
    xml.newAttrib(prefix + "Fit", imageFitName(im.fit));
    xml.newAttrib(prefix + "Src", four(im.src));
    xml.newAttrib(prefix + "Box", four(im.box));
    xml.newAttrib(prefix + "Turn", im.turn);
  };
  for (std::size_t k = 0; k < v.getImages().size(); k++) {
    writeImage(v.getImages()[k], imagePrefix("image", k));
  }

  // the tray's border (decision 0046): only what differs from none
  if (const TrayBorder& b = v.getBorder(); !b.isDefault()) {
    if (b.enabled) {
      xml.newAttrib("border", 1);
    }
    if (b.corners != BorderCorners::Round) {
      xml.newAttrib("borderCorners", borderCornersName(b.corners));
    }
    if (b.material != BorderMaterial::Smooth) {
      xml.newAttrib("borderMaterial", borderMaterialName(b.material));
    }
    if (b.color >= 0) {
      std::array<char, 8> c{};
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
      std::snprintf(c.data(), c.size(), "#%06X", asUnsigned(b.color) & 0xFFFFFF);
      xml.newAttrib("borderColor", c.data());
    }
    if (b.thickness != 0) {
      xml.newAttrib("borderThickness", b.thickness);
    }
    for (std::size_t k = 0; k < b.images.size(); k++) {
      writeImage(b.images[k], imagePrefix("borderImage", k));
    }
  }

  // room for other formats one day
  xml.newAttrib("type", 0);

  for (const auto& [name, value] : v.getExtraAttributes()) {
    xml.newAttrib(name, value);
  }

  std::string cells;
  cells.reserve(v.getXYZ());
  for (unsigned int i = 0; i < v.getXYZ(); i++) {
    switch (v.getState(i)) {
      case Voxel::kEmpty:
        cells += '_';
        break;
      case Voxel::kFilled:
        cells += '#';
        break;
      case Voxel::kVariable:
        cells += '+';
        break;
      default:
        break;
    }

    // the color, for non-empty cells, unless it is 0
    if (v.getState(i) == Voxel::kFilled || v.getState(i) == Voxel::kVariable) {
      if (v.getColor(i) != 0) {
        cells += std::to_string(v.getColor(i));
      }
    }
  }
  xml.addContent(cells);

  xml.endTag("voxel");
}

}  // namespace sl::io::detail

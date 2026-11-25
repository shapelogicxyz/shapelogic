// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from the XML load and save code of src/lib/puzzle.cpp, problem.cpp,
// solution.cpp, assembly.cpp, disassembly.cpp, voxel.cpp and gridtype.cpp,
// and the file part of src/lib/stacking.cpp (saveRodSets, loadRodSets,
// saveProblem, loadProblem, encodeRod, decodeRod, countAttribute).
//
// The original read with a pull parser and skipped what it did not know. This
// reads the XmlElement tree of xml_document.h and keeps what it does not know
// (decision 0008). Numbers are parsed with the same C functions the original
// used (atoi is strtol cast to int), so odd values read the same.

#include "shapelogic/io/puzzle_file.h"

#include "shapelogic/base/base64.h"
#include "shapelogic/base/int_cast.h"
#include "shapelogic/io/gzip_file.h"
#include "shapelogic/io/xml_document.h"
#include "shapelogic/io/xml_writer.h"
#include "shapelogic/model/sliding.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace bt {

// Write access to the model for the reader. Defined here so the model's
// public API keeps its checks.
struct PuzzleFileAccess {
  static std::vector<std::uint32_t>& colors(Puzzle& p) { return p.colors_; }
  static void setGridType(Puzzle& p, GridType gt) { p.gt_ = gt; }
  static const std::vector<std::uint32_t>& colors(const Puzzle& p) { return p.colors_; }
  static std::vector<PuzzleImage>& images(Puzzle& p) { return p.images_; }
  static std::optional<tilted::Tray>& tray(Puzzle& p) { return p.tray_; }
};

struct ProblemFileAccess {
  static std::vector<Part>& parts(Problem& p) { return p.parts_; }
  static unsigned int& result(Problem& p) { return p.result_; }
  static unsigned int& goal(Problem& p) { return p.goal_; }
  static std::vector<Solution>& solutions(Problem& p) { return p.solutions_; }
  static int& sortedBy(Problem& p) { return p.sortedBy_; }
  static bool& solutionsWithRotations(Problem& p) { return p.solutionsWithRotations_; }
  static std::set<std::uint32_t>& colorConstraints(Problem& p) { return p.colorConstraints_; }
  static ProblemState& state(Problem& p) { return p.state_; }
  static std::uint64_t& numAssemblies(Problem& p) { return p.numAssemblies_; }
  static std::uint64_t& numSolutions(Problem& p) { return p.numSolutions_; }
  static std::uint64_t& usedMs(Problem& p) { return p.usedMs_; }
  static std::string& assemblerState(Problem& p) { return p.assemblerState_; }
  static std::string& assemblerVersion(Problem& p) { return p.assemblerVersion_; }
  static unsigned int& maxHoles(Problem& p) { return p.maxHoles_; }
  static std::uint64_t& uid(Problem& p) { return p.uid_; }
  static unsigned int resultRaw(const Problem& p) { return p.result_; }
};

}  // namespace bt

namespace bt::io {

namespace {

// ---- reading helpers ------------------------------------------------------------

/// A file the reader cannot accept; turned into a Result at the top.
class ReadError : public std::runtime_error {
 public:
  ReadError(const std::string& what, int line)
      : std::runtime_error(line > 0 ? what + " (line " + std::to_string(line) + ")" : what) {}
};

[[noreturn]] void error(const XmlElement& e, const std::string& what) { throw ReadError(what, e.line); }

/// The original's atoi(): strtol cast to int.
int atoiOf(const std::string& s) { return static_cast<int>(std::strtol(s.c_str(), nullptr, 10)); }
unsigned long strtoulOf(const std::string& s) { return std::strtoul(s.c_str(), nullptr, 10); }
unsigned long long strtoullOf(const std::string& s) {
  return std::strtoull(s.c_str(), nullptr, 10);
}

/// Collects unknown attributes and child elements into a PreservedXml, with
/// each kept child's slot: one past the writer position of the last known
/// child before it.
class Keeper {
 public:
  explicit Keeper(PreservedXml& p) : p_(p) {}

  void attributes(const XmlElement& e, std::initializer_list<std::string_view> known) {
    for (const XmlAttribute& a : e.attributes) {
      if (std::find(known.begin(), known.end(), a.name) == known.end()) {
        p_.attributes.push_back(a);
      }
    }
  }

  /// A known child with writer position `pos` was read.
  void known(unsigned int pos) { slot_ = std::max(slot_, pos + 1); }

  /// An unknown child: keep it.
  void unknown(const XmlElement& c) { p_.children.push_back(PreservedXml::Child{slot_, c}); }

 private:
  PreservedXml& p_;
  unsigned int slot_ = 0;
};

const std::string& requireText(const XmlElement& e, const char* what) {
  if (e.text.empty()) {
    error(e, what);
  }
  return e.text;
}

/// Space-separated integers, exactly `out.size()` of them (getNumbers()).
void getNumbers(const XmlElement& e, const std::string& str, std::vector<int>& out,
                bool negAllowed) {
  std::size_t at = 0;
  int val = 0;
  bool gotNum = false;
  bool negative = false;

  for (const char c : str) {
    if (c == '-' && negAllowed) {
      // only one '-', and before the digits
      if (negative || gotNum) {
        error(e, "too many '-' signs in disassembly number");
      }
      negative = true;
    } else if (c >= '0' && c <= '9') {
      val = val * 10 + c - '0';
      gotNum = true;
    } else if (c == ' ') {
      if (gotNum) {
        if (at == out.size()) {
          error(e, "too many numbers in disassembly");
        }
        out[at++] = negative ? -val : val;
        val = 0;
        gotNum = negative = false;
      }
      if (negative) {
        error(e, "only '-' encountered in disassembly");
      }
    } else {
      error(e, "not allowed character in disassembly");
    }
  }

  // a last number not followed by a space
  if (gotNum) {
    if (at == out.size()) {
      error(e, "too many numbers in disassembly");
    }
    out[at++] = negative ? -val : val;
  }

  if (at != out.size()) {
    error(e, "too few number in disassembly");
  }
}

// ---- voxel spaces and the grid --------------------------------------------------

GridType readGridType(const XmlElement& e) {
  const std::string typeStr = e.attribute("type");
  if (typeStr.empty()) {
    error(e, "grid type needs a valid 'type' attribute");
  }
  const int type = atoiOf(typeStr);
  if (type < 0 || type >= kNumGridKinds) {
    error(e, "puzzle with unknown grid type");
  }
  return GridType(static_cast<GridKind>(type));
}

/// The start or goal marks of a sliding puzzle as a file lists them: one
/// comma-separated number per cell. set(i, mark) is called for each cell
/// below `cells` that has a number.
template <class Set>
void readMarkList(const std::string& attr, unsigned int cells, Set set) {
  if (attr.empty()) {
    return;
  }
  unsigned int gi = 0;
  unsigned int val = 0;
  bool inNum = false;
  for (std::size_t pos = 0; pos <= attr.size(); pos++) {
    const char ch = pos < attr.size() ? attr[pos] : ',';
    if (ch >= '0' && ch <= '9') {
      val = val * 10 + static_cast<unsigned int>(ch - '0');
      inNum = true;
    } else if (ch == ',') {
      if (inNum && gi < cells) {
        set(gi, static_cast<unsigned char>(val));
      }
      gi++;
      val = 0;
      inNum = false;
    }
  }
}

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

/// The <images> of a puzzle: each <image> with its id, type and the file in
/// base64. A picture whose bytes are not a PNG or JPEG of the stated type,
/// or whose id comes twice, is left out (the shapes showing it lose it).
void readImages(const XmlElement& e, std::vector<PuzzleImage>& images) {
  for (const XmlElement& c : e.children) {
    if (c.name != "image") {
      continue;
    }
    const std::string idText = c.attribute("id");
    const int id = atoiOf(idText);
    if (idText.empty() || id < 0) {
      continue;
    }
    if (std::any_of(images.begin(), images.end(),
                    [id](const PuzzleImage& im) { return static_cast<int>(im.id) == id; })) {
      continue;
    }
    std::optional<std::string> bytes = base64Decode(c.text);
    if (!bytes) {
      continue;
    }
    const std::optional<ImageInfo> info = sniffImage(*bytes);
    if (!info || info->type != c.attribute("type")) {
      continue;
    }
    std::string hash = imageHash(*bytes);
    images.push_back(PuzzleImage{asUnsigned(id), info->type, info->width, info->height,
                                 std::make_shared<const std::string>(std::move(*bytes)),
                                 std::move(hash)});
  }
}

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

  std::unique_ptr<Voxel> v = gt.makeVoxel(sx, sy, sz, 0);
  v->skipRecalcBoundingBox(true);

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

  v->setHotspot(atoiOf(e.attribute("hx")), atoiOf(e.attribute("hy")), atoiOf(e.attribute("hz")));
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
    v->setDiskSize(asUnsigned(atoiOf(disk)));
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

  v->skipRecalcBoundingBox(false);
  return v;
}

// ---- assemblies, disassemblies, solutions ---------------------------------------

Assembly readAssembly(const XmlElement& e, unsigned int pieces, GridType gt) {
  const std::string& str = requireText(e, "expected: TEXT");
  const unsigned int numTrans = gt.getSymmetries().getNumTransformations();

  Assembly a(gt);

  int x = 0;
  int y = 0;
  int z = 0;
  int trans = 0;
  int state = 0;
  int sign = 1;

  auto addPlacement = [&]() {
    if (trans != kUnplacedTrans && (trans < 0 || asUnsigned(trans) >= numTrans)) {
      error(e, "transformations need to be between 0 and NUM_TRANSFORMATIONS");
    }
    a.addRawPlacement(Placement{static_cast<unsigned char>(trans), x, y, z});
  };

  // a number with an optional leading '-', as the original's state machine
  auto digit = [&](int& value, char c) {
    if (c == '-') {
      if (value != 0) {
        error(e, "assembly placements minus in number");
      }
      sign = -1;
    } else {
      value *= 10;
      value += sign * (c - '0');
    }
  };

  for (const char c : str) {
    if (c == ' ') {
      if (state == 3) {
        addPlacement();
        x = y = z = trans = state = 0;
      } else {
        sign = 1;
        state++;
      }
    } else if (c == 'x' && state == 0) {
      x = y = z = 0;
      trans = kUnplacedTrans;
      state = 3;
    } else {
      if (c != '-' && (c < '0' || c > '9')) {
        error(e, "non number character found where number is expected");
      }
      switch (state) {
        case 0:
          digit(x, c);
          break;
        case 1:
          digit(y, c);
          break;
        case 2:
          digit(z, c);
          break;
        case 3:
          digit(trans, c);
          break;
        default:
          break;
      }
    }
  }

  if (state != 3) {
    error(e, "not the right number of numbers in assembly");
  }
  addPlacement();

  if (a.placementCount() != pieces) {
    error(e, "not the right number of placements in assembly");
  }
  return a;
}

int parseDoubledPivot(const std::string& s) {
  return static_cast<int>(std::floor(std::strtod(s.c_str(), nullptr) * 2.0 + 0.5));
}

State readState(const XmlElement& e, unsigned int pn) {
  State st = State::withoutOrientations(pn);

  bool hasDx = false;
  bool hasDy = false;
  bool hasDz = false;

  for (const XmlElement& c : e.children) {
    if (c.name == "dx" || c.name == "dy" || c.name == "dz" || c.name == "dt") {
      std::vector<int> v(pn);
      getNumbers(c, c.text, v, c.name != "dt");
      if (c.name == "dx") {
        st.setXs(std::move(v));
        hasDx = true;
      } else if (c.name == "dy") {
        st.setYs(std::move(v));
        hasDy = true;
      } else if (c.name == "dz") {
        st.setZs(std::move(v));
        hasDz = true;
      } else {
        st.setOrientations(std::move(v));
      }
    } else if (c.name == "rotation") {
      const std::string piece = c.attribute("piece");
      if (piece.empty()) {
        error(c, "rotation needs piece");
      }
      st.setRotationArrival(asUnsigned(atoiOf(piece)), parseDoubledPivot(c.attribute("px")),
                            parseDoubledPivot(c.attribute("py")),
                            parseDoubledPivot(c.attribute("pz")),
                            asUnsigned(atoiOf(c.attribute("axis"))),
                            asUnsigned(atoiOf(c.attribute("sense"))));
    }
  }

  if (!hasDx || !hasDy || !hasDz) {
    error(e, "disassembly state needs dx, dy and dz subnode");
  }
  return st;
}

Separation readSeparation(const XmlElement& e, unsigned int pieceCnt) {
  std::vector<unsigned int> pieces;
  std::vector<State> states;
  ClonePtr<Separation> removed;
  ClonePtr<Separation> left;
  unsigned int piecenumber = 0;
  unsigned int removedPc = 0;
  unsigned int leftPc = 0;

  for (const XmlElement& c : e.children) {
    if (c.name == "pieces") {
      const std::string count = c.attribute("count");
      if (count.empty()) {
        error(c, "pieces node needs a 'count' attribute");
      }
      piecenumber = asUnsigned(atoiOf(count));
      if (piecenumber != pieceCnt) {
        error(c, "the number of pieces in the count array is not as expected");
      }
      std::vector<int> v(piecenumber);
      getNumbers(c, requireText(c, "expected: TEXT"), v, false);
      pieces.assign(v.begin(), v.end());
    } else if (c.name == "state") {
      if (piecenumber == 0) {
        error(c, "there must be a pieces array before the states");
      }
      if (removed || left) {
        error(c, "there are states behind the sub separations");
      }
      states.push_back(readState(c, piecenumber));
    } else if (c.name == "separation") {
      if (states.empty()) {
        error(c, "there are no state nodes in the separation");
      }
      if (removedPc == 0) {
        for (unsigned int i = 0; i < pieceCnt; i++) {
          if (states.back().pieceRemoved(i)) {
            removedPc++;
          } else {
            leftPc++;
          }
        }
        if (removedPc == 0 || leftPc == 0) {
          error(c, "there need to be pieces in both parts of the tree");
        }
      }

      const std::string type = c.attribute("type");
      if (type.empty()) {
        error(c, "sub-sepatations need to have a type field");
      }
      if (type == "left") {
        if (left) {
          error(c, "more than one left branch in disassembly");
        }
        left = ClonePtr<Separation>(std::make_unique<Separation>(readSeparation(c, leftPc)));
      } else if (type == "removed") {
        if (removed) {
          error(c, "more than one removed branch in disassembly");
        }
        removed =
            ClonePtr<Separation>(std::make_unique<Separation>(readSeparation(c, removedPc)));
      } else {
        error(c, "subnodes must have either left or removed type");
      }
    }
  }

  if (states.empty()) {
    error(e, "there are no state nodes in the separation");
  }

  Separation sep(std::move(removed), std::move(left), std::move(pieces));
  for (State& st : states) {
    sep.appendState(std::move(st));
  }
  return sep;
}

SeparationInfo readSeparationInfo(const XmlElement& e) {
  const std::string& str = requireText(e, "expected: TEXT");

  std::vector<unsigned int> values;
  std::vector<unsigned int> rotValues;
  std::size_t pos = 0;
  unsigned int num = 0;
  bool inRots = false;

  while (pos < str.size()) {
    if (str[pos] == '|') {
      inRots = true;
      pos++;
      while (pos < str.size() && str[pos] == ' ') {
        pos++;
      }
      continue;
    }

    if (str[pos] == ' ') {
      (inRots ? rotValues : values).push_back(num);
      num = 0;
    }

    if (str[pos] >= '0' && str[pos] <= '9') {
      num = 10 * num + static_cast<unsigned int>(str[pos] - '0');
    }

    pos++;
  }

  (inRots ? rotValues : values).push_back(num);

  if (rotValues.empty()) {
    rotValues.assign(values.size(), 0);
  } else if (rotValues.size() != values.size()) {
    error(e, "separationInfo rotation counts do not match value counts");
  }

  // the tree must end: a node opens one more branch, a 0 closes one
  unsigned int branches = 1;
  std::size_t at = 0;
  while (branches != 0) {
    if (at >= values.size()) {
      error(e, "the tree in the disassemblyInformation tag is incomplete");
    }
    if (values[at] != 0) {
      branches++;
    } else {
      branches--;
    }
    at++;
  }

  return SeparationInfo(std::move(values), std::move(rotValues));
}

Solution readSolution(const XmlElement& e, unsigned int pieces, GridType gt) {
  // 64 bits, as the counts: a solve can find more than 2^32 assemblies
  const std::uint64_t assemblyNum = strtoullOf(e.attribute("asmNum"));
  const std::uint64_t solutionNum = strtoullOf(e.attribute("solNum"));

  std::optional<Assembly> assembly;
  std::optional<Separation> tree;
  std::optional<SeparationInfo> treeInfo;
  PreservedXml preserved;
  Keeper keep(preserved);
  keep.attributes(e, {"asmNum", "solNum"});

  for (const XmlElement& c : e.children) {
    if (c.name == "assembly") {
      assembly = readAssembly(c, pieces, gt);
      keep.known(0);
    } else if (c.name == "separation") {
      if (!assembly) {
        error(c, "an assembly must always be before a separation in a solution");
      }
      // the number of pieces really placed
      unsigned int pl = 0;
      for (unsigned int i = 0; i < assembly->placementCount(); i++) {
        if (assembly->isPlaced(i)) {
          pl++;
        }
      }
      tree = readSeparation(c, pl);
      keep.known(1);
    } else if (c.name == "separationInfo") {
      treeInfo = readSeparationInfo(c);
      keep.known(1);
    } else {
      keep.unknown(c);
    }
  }

  if (!assembly) {
    error(e, "no assembly in solution");
  }

  // a full tree wins over its summary
  std::optional<Solution> sol;
  if (tree) {
    sol.emplace(std::move(*assembly), assemblyNum, std::move(*tree), solutionNum);
  } else if (treeInfo) {
    sol.emplace(std::move(*assembly), assemblyNum, std::move(*treeInfo), solutionNum);
  } else {
    // the number is kept (it sorts), though it is only written with a disassembly
    sol.emplace(std::move(*assembly), assemblyNum, solutionNum);
  }
  sol->preserved() = std::move(preserved);
  return std::move(*sol);
}

// ---- problems -------------------------------------------------------------------

// Writer positions of the known children of <problem>.
enum ProblemSlot : unsigned int {
  kSlotParts = 0,
  kSlotResult,
  kSlotGoal,
  kSlotStacking,
  kSlotSlideMarks,
  kSlotSolverOptions,
  kSlotBitmap,
  kSlotAssembler,
  kSlotPending,
  kSlotSolutions,
};

// ---- stacking (stacking.cpp) --------------------------------------------------

/// Rods, or discs a rod holds, that a file may ask for (MAX_FILE_COUNT).
constexpr unsigned int kMaxFileCount = 1000;

/// An attribute as a count from 0 to `most`; `fallback` when it is missing
/// or not a number in range (an edited file can hold anything).
unsigned int countAttribute(const XmlElement& e, const char* name, unsigned int fallback,
                            unsigned int most) {
  const std::string str = e.attribute(name);
  if (str.empty()) {
    return fallback;
  }
  char* end = nullptr;
  const long v = std::strtol(str.c_str(), &end, 10);
  if (end == str.c_str() || v < 0 || static_cast<unsigned long>(v) > most) {
    return fallback;
  }
  return static_cast<unsigned int>(v);
}

/// <rodSets>: each <rodSet> a board; anything else is skipped, as the
/// original did.
void readRodSets(const XmlElement& e, Puzzle& puzzle) {
  for (const XmlElement& c : e.children) {
    if (c.name != "rodSet") {
      continue;
    }
    stacking::RodSet r;
    r.name = c.attribute("name");
    r.rodCount = std::max(1U, countAttribute(c, "rods", r.rodCount, kMaxFileCount));
    r.growHeight = countAttribute(c, "grow", r.growHeight ? 1U : 0U, 1) != 0;
    r.definedHeight = std::max(1U, countAttribute(c, "height", r.definedHeight, kMaxFileCount));
    r.sizeMatters = countAttribute(c, "sizeMatters", r.sizeMatters ? 1U : 0U, 1) != 0;
    r.distanceMatters = countAttribute(c, "distanceMatters", r.distanceMatters ? 1U : 0U, 1) != 0;
    r.canMoveOver = countAttribute(c, "moveOver", r.canMoveOver ? 1U : 0U, 1) != 0;
    r.panexColumns = countAttribute(c, "panex", r.panexColumns ? 1U : 0U, 1) != 0;
    const unsigned int pocket = countAttribute(c, "pocket", 0, kMaxFileCount);
    if (pocket > 0) {
      r.pocketColumn = true;
      r.pocketHeight = pocket;
    }
    r.outsideChannel = countAttribute(c, "channel", r.outsideChannel ? 1U : 0U, 1) != 0;
    puzzle.addRodSet(r);
  }
}

/// A rod's discs, "shape:copy,shape:copy", read as the original's stream
/// read them (decodeRod): it stops at the first thing that is not one.
std::vector<stacking::DiskRef> decodeRod(const std::string& str) {
  std::vector<stacking::DiskRef> rod;
  if (str.empty()) {
    return rod;
  }
  std::istringstream in(str);
  while (in.good()) {
    unsigned int shape = 0;
    unsigned int inst = 0;
    char colon = 0;
    in >> shape >> colon >> inst;
    if (!in.fail() && colon == ':') {
      rod.push_back(stacking::DiskRef{shape, inst});
    }
    if (in.peek() == ',') {
      in.get();
    }
  }
  return rod;
}

/// <start> or <goal>: a <rod> per rod; anything else is skipped.
stacking::StackMap readStackMap(const XmlElement& e) {
  stacking::StackMap map;
  for (const XmlElement& c : e.children) {
    if (c.name == "rod") {
      map.rods.push_back(decodeRod(c.attribute("pieces")));
    }
  }
  return map;
}

/// A problem's <stacking>: its rod set and the start and goal stacks.
void readStacking(const XmlElement& e, Problem& prob) {
  const std::string id = e.attribute("rodSet");
  if (!id.empty()) {
    prob.setRodSetId(static_cast<unsigned int>(atoiOf(id)));
  }
  for (const XmlElement& c : e.children) {
    if (c.name == "start") {
      prob.editStart() = readStackMap(c);
    } else if (c.name == "goal") {
      prob.editGoal() = readStackMap(c);
    }
  }
}

/// The most cells a problem's <slideMarks> may name (a board is at most
/// 1024 cells each way; an edited file can hold anything).
constexpr unsigned int kMaxMarkCells = 1U << 30U;

/// A problem's <slideMarks>: its starts and goals on its board.
void readSlideMarks(const XmlElement& e, Problem& prob) {
  SlideMarks& marks = prob.editSlideMarks();
  marks.clear();
  for (int goal = 0; goal < 2; goal++) {
    // the last cell first, so the marks grow once
    std::vector<std::pair<unsigned int, unsigned int>> read;
    readMarkList(e.attribute(goal != 0 ? "goal" : "start"), kMaxMarkCells,
                 [&read](unsigned int i, unsigned int mark) {
                   if (mark != 0) {
                     read.emplace_back(i, mark);
                   }
                 });
    for (auto it = read.rbegin(); it != read.rend(); ++it) {
      marks.set(it->first, goal != 0, it->second);
    }
  }
}

void writeProblem(XmlWriter& xml, const Puzzle& puzzle, const Problem& prob);

// Every problem a uid of its own (decision 0044), once the problems are as
// the file means them. A problem the file gives none gets one made from
// its place in the file and its definition, so that the same file opened
// again, or in another window, names its problems' searches as before,
// and two problems alike in every cell are told apart; so does a problem
// whose uid an earlier one has (copied by hand in the file). A problem
// paused by a version before uids keeps none, so its saved search, named
// without one, is still its own; of such problems with the same
// definition only the first does, as they shared one folder.
void giveUids(Puzzle& puzzle) {
  using A = ProblemFileAccess;
  std::set<std::uint64_t> seen;
  std::set<std::string> legacy;
  for (unsigned int i = 0; i < puzzle.getNumberOfProblems(); i++) {
    Problem& prob = puzzle.getProblem(i);
    std::uint64_t uid = prob.getUid();
    if (uid != 0 && seen.insert(uid).second) {
      continue;
    }
    Problem def = prob.copyDefinition();
    A::uid(def) = 0;
    XmlWriter xml;
    writeProblem(xml, puzzle, def);
    const std::string text = xml.str();
    if (uid == 0 && prob.getSolveState() == ProblemState::Solving &&
        legacy.insert(text).second) {
      continue;
    }
    // FNV-1a over the place, the definition and the uid it had
    std::uint64_t h = 14695981039346656037ULL;
    const auto mix = [&h](std::uint64_t v) {
      for (int b = 0; b < 8; b++) {
        h ^= (v >> (8 * b)) & 0xFF;
        h *= 1099511628211ULL;
      }
    };
    mix(i);
    mix(uid);
    for (const char ch : text) {
      mix(static_cast<unsigned char>(ch));
    }
    while (h == 0 || seen.contains(h)) {
      mix(h);
    }
    A::uid(prob) = h;
    seen.insert(h);
  }
}

Problem readProblem(const XmlElement& e, const Puzzle& puzzle) {
  using A = ProblemFileAccess;
  Problem prob;
  prob.setName(e.attribute("name"));

  Keeper keep(prob.preserved());
  keep.attributes(e, {"name", "uid", "state", "assemblies", "solutions", "time", "timeMs",
                      "searched", "searchDepth", "triedApart", "slideGrid", "slideLevelMs",
                      "maxHoles"});

  // 16 hex digits; anything else is no uid, which giveUids settles
  std::string str = e.attribute("uid");
  if (str.size() == 16 && std::ranges::all_of(str, [](char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
      })) {
    A::uid(prob) = std::stoull(str, nullptr, 16);
  }

  str = e.attribute("maxHoles");
  if (!str.empty()) {
    A::maxHoles(prob) = asUnsigned(atoiOf(str));
  }

  str = e.attribute("state");
  if (!str.empty()) {
    const int st = atoiOf(str);
    if (st == 1 || st == 2 || st == 3) {
      A::state(prob) = static_cast<ProblemState>(st);
    }
  }

  if (prob.getSolveState() != ProblemState::Unsolved) {
    // the original read these with atoi(); larger counts are kept here
    str = e.attribute("assemblies");
    if (!str.empty()) {
      A::numAssemblies(prob) = strtoullOf(str);
    }
    str = e.attribute("solutions");
    if (!str.empty()) {
      A::numSolutions(prob) = strtoullOf(str);
    }
    str = e.attribute("time");
    if (!str.empty()) {
      A::usedMs(prob) = 1000ull * strtoulOf(str);
    }
    str = e.attribute("timeMs");
    if (!str.empty()) {
      A::usedMs(prob) = strtoullOf(str);
    }
    std::uint64_t searched = 0;
    std::uint64_t depth = 0;
    std::uint64_t apart = 0;
    str = e.attribute("searched");
    if (!str.empty()) {
      searched = strtoullOf(str);
    }
    str = e.attribute("searchDepth");
    if (!str.empty()) {
      depth = strtoulOf(str);
    }
    str = e.attribute("triedApart");
    if (!str.empty()) {
      apart = strtoulOf(str);
    }
    prob.setSearchStats(searched, depth, apart);
    str = e.attribute("slideGrid");
    if (!str.empty()) {
      prob.setSlideGrid(static_cast<unsigned int>(strtoulOf(str)));
    }
    // each move's time, "-" for one not timed
    str = e.attribute("slideLevelMs");
    std::vector<std::uint64_t> ms;
    std::istringstream in(str);
    std::string v;
    while (in >> v) {
      ms.push_back(v == "-" ? kUntimedMs : strtoullOf(v));
    }
    // a sliding solve paused by a version that kept no times: the moves
    // searched are there, untimed, and the times go on from them
    if (ms.empty() && depth > 0 && prob.getSolveState() == ProblemState::Solving &&
        sliding::isSliding(puzzle)) {
      ms.assign(static_cast<std::size_t>(depth), kUntimedMs);
    }
    prob.setSlideLevelMs(std::move(ms));
  }

  unsigned int pieces = 0;

  for (const XmlElement& c : e.children) {
    if (c.name == "shapes") {
      // the parts are saved as <shape> for historical reasons
      keep.known(kSlotParts);
      for (const XmlElement& s : c.children) {
        if (s.name != "shape") {
          continue;
        }

        str = s.attribute("id");
        if (str.empty()) {
          error(s, "a shape node must have an 'idt' attribute");
        }
        const auto id = static_cast<unsigned short>(atoiOf(str));

        unsigned short min = 0;
        unsigned short max = 0;
        str = s.attribute("count");
        if (!str.empty()) {
          min = max = static_cast<unsigned short>(atoiOf(str));
        } else if (!s.attribute("min").empty() && !s.attribute("max").empty()) {
          min = static_cast<unsigned short>(atoiOf(s.attribute("min")));
          max = static_cast<unsigned short>(atoiOf(s.attribute("max")));
          if (min > max) {
            error(s, "min of shape count must by <= max");
          }
        } else {
          min = max = 1;
        }

        const auto grp = static_cast<unsigned short>(atoiOf(s.attribute("group")));

        pieces += max;

        if (id >= puzzle.getNumberOfShapes()) {
          error(s, "the shape ids must be for valid shapes");
        }

        /* the groups are either one group attribute (all pieces in it) or a
         * list of <group> elements with a group and a count each */
        Part part{id, min, max, {}};
        if (grp != 0) {
          part.groups.push_back(PartGroup{grp, max});
        } else {
          for (const XmlElement& g : s.children) {
            if (g.name != "group") {
              continue;
            }
            str = g.attribute("group");
            if (str.empty()) {
              error(g, "a group node must have a valid group attribute");
            }
            const auto gr = static_cast<unsigned short>(atoiOf(str));
            str = g.attribute("count");
            if (str.empty()) {
              error(g, "a group node must have a valid count attribute");
            }
            part.groups.push_back(PartGroup{gr, static_cast<unsigned short>(atoiOf(str))});
          }
        }
        A::parts(prob).push_back(std::move(part));
      }
    } else if (c.name == "result") {
      keep.known(kSlotResult);
      str = c.attribute("id");
      if (str.empty()) {
        error(c, "the result node must have an 'id' attribute with content");
      }
      A::result(prob) = asUnsigned(atoiOf(str));
    } else if (c.name == "goal") {
      keep.known(kSlotGoal);
      str = c.attribute("id");
      if (str.empty()) {
        error(c, "the goal node must have an 'id' attribute with content");
      }
      A::goal(prob) = asUnsigned(atoiOf(str));
    } else if (c.name == "stacking") {
      keep.known(kSlotStacking);
      readStacking(c, prob);
    } else if (c.name == "slideMarks") {
      keep.known(kSlotSlideMarks);
      readSlideMarks(c, prob);
    } else if (c.name == "solutions" || c.name == "solutionsWithRotations") {
      keep.known(kSlotSolutions);
      if (c.name == "solutionsWithRotations") {
        // the rotation-aware block wins; drop classic solutions read before it
        A::solutionsWithRotations(prob) = true;
        A::solutions(prob).clear();
        A::sortedBy(prob) = -1;
      } else if (prob.getSolutionsWithRotations()) {
        continue;  // a classic block after the rotation-aware one is ignored
      }
      for (const XmlElement& s : c.children) {
        if (s.name == "solution") {
          A::solutions(prob).push_back(readSolution(s, pieces, puzzle.getGridType()));
        }
      }
      prob.assignSolutionIds();
    } else if (c.name == "bitmap") {
      keep.known(kSlotBitmap);
      for (const XmlElement& p : c.children) {
        if (p.name != "pair") {
          continue;
        }
        str = p.attribute("piece");
        if (str.empty()) {
          error(p, "a pair node must have a piece attribute");
        }
        const auto piece = asUnsigned(atoiOf(str));
        str = p.attribute("result");
        if (str.empty()) {
          error(p, "a pair node must have a result attribute");
        }
        const auto result = asUnsigned(atoiOf(str));
        A::colorConstraints(prob).insert(piece << 16 | result);
      }
    } else if (c.name == "solverOptions") {
      keep.known(kSlotSolverOptions);
      std::map<std::string, std::string> opts;
      for (const XmlAttribute& a : c.attributes) {
        opts[a.name] = a.value;
      }
      prob.setSolverOptions(opts);
    } else if (c.name == "pending") {
      keep.known(kSlotPending);
      for (const XmlElement& item : c.children) {
        if (item.name != "item") {
          continue;
        }
        std::optional<Assembly> assembly;
        for (const XmlElement& a : item.children) {
          if (a.name == "assembly") {
            assembly = readAssembly(a, pieces, puzzle.getGridType());
          }
        }
        // an item without an assembly is dropped
        if (assembly) {
          prob.addPending(PendingAssembly{std::move(*assembly),
                                          atoiOf(item.attribute("counted")) != 0,
                                          strtoullOf(item.attribute("assembly")),
                                          strtoullOf(item.attribute("solution"))});
        }
      }
    } else if (c.name == "assembler") {
      keep.known(kSlotAssembler);
      // the saved search position; without a version it is useless
      str = c.attribute("version");
      if (!str.empty()) {
        A::assemblerVersion(prob) = str;
        A::assemblerState(prob) = c.text;
      }
    } else {
      keep.unknown(c);
    }
  }

  // a shape id that is not right is reset to "none"
  if (A::result(prob) >= puzzle.getNumberOfShapes()) {
    A::result(prob) = kNoShape;
  }
  if (A::goal(prob) >= puzzle.getNumberOfShapes()) {
    A::goal(prob) = kNoShape;
  }

  return prob;
}

// ---- the tilted tray (decision 0045) ---------------------------------------------

/// A number as the file holds it; nothing when the text is not one.
std::optional<double> realOf(const std::string& str) {
  if (str.empty()) {
    return std::nullopt;
  }
  char* end = nullptr;
  const double v = std::strtod(str.c_str(), &end);
  if (end != str.c_str() + str.size() || !std::isfinite(v)) {
    return std::nullopt;
  }
  return v;
}

/// A whole number from 0 to `most`; nothing when the attribute is missing
/// or is not one.
std::optional<unsigned int> wholeOf(const XmlElement& e, const char* name, unsigned int most) {
  const std::string str = e.attribute(name);
  char* end = nullptr;
  const long v = std::strtol(str.c_str(), &end, 10);
  if (str.empty() || end != str.c_str() + str.size() || v < 0 ||
      static_cast<unsigned long>(v) > most) {
    return std::nullopt;
  }
  return static_cast<unsigned int>(v);
}

/// <tray>: the tray of a Tilted Square puzzle. Nothing when a setting is
/// missing or out of range (an edited file can hold anything); the element
/// is then kept as the file has it. A square tray has `size`, a rectangle
/// `width` and `height` (writeTray()); either of those two, when there,
/// goes over `size`.
std::optional<tilted::Tray> readTray(const XmlElement& e) {
  tilted::Tray t;
  const std::optional<unsigned int> size = wholeOf(e, "size", tilted::Tray::kMaxSize);
  const auto sideOf = [&](const char* name) {
    return e.findAttribute(name) != nullptr ? wholeOf(e, name, tilted::Tray::kMaxSize) : size;
  };
  const std::optional<unsigned int> width = sideOf("width");
  const std::optional<unsigned int> height = sideOf("height");
  const std::optional<unsigned int> over = wholeOf(e, "over", tilted::Tray::kMaxSlope);
  const std::optional<unsigned int> down = wholeOf(e, "down", tilted::Tray::kMaxSlope);
  const std::optional<double> allowance = realOf(e.attribute("allowance"));
  const std::optional<double> shiftX = realOf(e.attribute("shiftX"));
  const std::optional<double> shiftY = realOf(e.attribute("shiftY"));
  const std::string placement = e.attribute("placement");
  if (!width || !height || !over || !down || !allowance || !shiftX || !shiftY ||
      (placement != "best" && placement != "custom")) {
    return std::nullopt;
  }
  // a size beside both sides says nothing, but a bad one is still a bad file
  if (e.findAttribute("size") != nullptr && !size) {
    return std::nullopt;
  }
  t.width = *width;
  t.height = *height;
  t.over = *over;
  t.down = *down;
  t.allowance = *allowance;
  t.placement = placement == "best" ? tilted::Tray::Placement::Best : tilted::Tray::Placement::Custom;
  t.shiftX = *shiftX;
  t.shiftY = *shiftY;
  // the walls: a character a square, row by row, '#' a wall and '_' open
  if (const std::string* walls = e.findAttribute("walls"); walls != nullptr) {
    if (walls->size() != static_cast<std::size_t>(t.width) * t.height) {
      return std::nullopt;
    }
    for (const char ch : *walls) {
      if (ch != '#' && ch != '_') {
        return std::nullopt;
      }
      t.walls.push_back(ch == '#' ? 1 : 0);
    }
    if (std::find(t.walls.begin(), t.walls.end(), 1) == t.walls.end()) {
      t.walls.clear();
    }
  }
  if (!t.valid()) {
    return std::nullopt;
  }
  return t;
}

// ---- the puzzle -----------------------------------------------------------------

// Writer positions of the known children of <puzzle>.
enum PuzzleSlot : unsigned int {
  kSlotGridType = 0,
  kSlotTray,
  kSlotColors,
  kSlotImages,
  kSlotShapes,
  kSlotRodSets,
  kSlotProblems,
  kSlotComment,
};

Puzzle readPuzzleElement(const XmlElement& root) {
  using A = PuzzleFileAccess;

  if (root.name != "puzzle") {
    error(root, "expected: START_TAGpuzzle");
  }

  const std::string versionStr = root.attribute("version");
  if (versionStr.empty()) {
    error(root, "puzzle node must have a 'version' attribute");
  }
  const int version = atoiOf(versionStr);
  if (version != 1 && version != 2) {
    error(root, "can only load files of version 1 and 2");
  }

  Puzzle puzzle;
  bool haveGrid = false;
  // A sliding file says where its starts and goals are: slideMarks
  // "problem", each problem has its own (decision 0027). Without that they
  // are the start/goal shapes': a shape that says start has both as marks
  // (decision 0021), and when none does the starts are the shapes' colors
  // (the original).
  const bool problemMarks = root.attribute("slideMarks") == "problem";
  bool startMarks = false;
  Keeper keep(puzzle.preserved());
  keep.attributes(root, {"version", "slideMarks"});

  for (const XmlElement& c : root.children) {
    if (c.name == "gridType") {
      keep.known(kSlotGridType);
      if (haveGrid) {
        error(c, "only one gridtype can be defined, and it must be before the first shape");
      }
      A::setGridType(puzzle, readGridType(c));
      haveGrid = true;
    } else if (c.name == "tray") {
      // a second one, or one that cannot be read, is kept as it is
      std::optional<tilted::Tray> tray = A::tray(puzzle) ? std::nullopt : readTray(c);
      if (tray) {
        keep.known(kSlotTray);
        A::tray(puzzle) = std::move(tray);
      } else {
        keep.unknown(c);
      }
    } else if (c.name == "colors") {
      keep.known(kSlotColors);
      for (const XmlElement& col : c.children) {
        if (col.name == "color") {
          A::colors(puzzle).push_back(
              (asUnsigned(atoiOf(col.attribute("red"))) & 0xFF) |
              (asUnsigned(atoiOf(col.attribute("green"))) & 0xFF) << 8 |
              (asUnsigned(atoiOf(col.attribute("blue"))) & 0xFF) << 16);
        }
      }
    } else if (c.name == "images") {
      keep.known(kSlotImages);
      readImages(c, A::images(puzzle));
    } else if (c.name == "shapes") {
      keep.known(kSlotShapes);
      // without a grid type, the grid is cubes
      haveGrid = true;
      for (const XmlElement& v : c.children) {
        if (v.name == "voxel") {
          startMarks = startMarks || v.findAttribute("start") != nullptr;
          puzzle.addShape(readVoxel(v, puzzle.getGridType()));
        }
      }
    } else if (c.name == "rodSets") {
      keep.known(kSlotRodSets);
      readRodSets(c, puzzle);
    } else if (c.name == "problems") {
      keep.known(kSlotProblems);
      for (const XmlElement& p : c.children) {
        if (p.name == "problem") {
          (void)puzzle.appendProblem(readProblem(p, puzzle));
        }
      }
    } else if (c.name == "comment") {
      keep.known(kSlotComment);
      if (c.findAttribute("popup") != nullptr) {
        puzzle.setCommentPopup(true);
      }
      puzzle.setComment(c.text);
    } else {
      keep.unknown(c);
    }
  }

  // a shape loses a picture the file does not have, or one whose part
  // shown misses it
  auto present = [&puzzle](ShapeImages stack) {
    std::erase_if(stack, [&puzzle](const ShapeImage& im) {
      const PuzzleImage* file = puzzle.findImage(im.id);
      return file == nullptr || !overlapsPicture(im.src, file->width, file->height);
    });
    return stack;
  };
  for (unsigned int s = 0; s < puzzle.getNumberOfShapes(); s++) {
    Voxel& v = puzzle.getShape(s);
    v.setImages(present(v.getImages()));
    TrayBorder b = v.getBorder();
    b.images = present(b.images);
    v.setBorder(b);
  }

  // only a cube-grid puzzle has a tilted tray
  if (puzzle.getGridType().getType() != GridKind::Bricks) {
    A::tray(puzzle).reset();
  }
  // a stacking puzzle has a board, even when its file has none
  if (puzzle.getGridType().getType() == GridKind::Stacking && puzzle.rodSetCount() == 0) {
    puzzle.addRodSet();
  }
  if (puzzle.getGridType().getType() == GridKind::Sliding && !problemMarks) {
    if (startMarks) {
      sliding::adoptBoardMarks(puzzle);
    } else {
      sliding::convertLegacyStarts(puzzle);
    }
  }
  giveUids(puzzle);
  if (problemMarks) {
    // the marks are the problems': a shape carries none
    for (unsigned int s = 0; s < puzzle.getNumberOfShapes(); s++) {
      Voxel& v = puzzle.getShape(s);
      for (unsigned int i = 0; i < v.getXYZ() && (v.hasStartPieces() || v.hasGoalPieces()); i++) {
        v.setStartPiece(i, 0);
        v.setGoalPiece(i, 0);
      }
    }
  }

  return puzzle;
}

// ---- writing --------------------------------------------------------------------

void writeKept(XmlWriter& xml, const PreservedXml& p, unsigned int slot) {
  for (const PreservedXml::Child& c : p.children) {
    if (c.slot == slot) {
      xml.writeElement(c.element);
    }
  }
}

void writeKeptAttributes(XmlWriter& xml, const PreservedXml& p) {
  for (const XmlAttribute& a : p.attributes) {
    xml.newAttrib(a.name, a.value);
  }
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

void writeAssembly(XmlWriter& xml, const Assembly& a) {
  xml.newTag("assembly");

  std::string str;
  const std::vector<Placement>& pl = a.getPlacements();
  for (std::size_t i = 0; i < pl.size(); i++) {
    if (pl[i].transformation != kUnplacedTrans) {
      str += std::to_string(pl[i].xpos) + " " + std::to_string(pl[i].ypos) + " " +
             std::to_string(pl[i].zpos) + " " + std::to_string(pl[i].transformation);
    } else {
      str += 'x';
    }
    if (i + 1 < pl.size()) {
      str += ' ';
    }
  }
  xml.addContent(str);

  xml.endTag("assembly");
}

std::string joinNumbers(const std::vector<int>& v, unsigned int n) {
  std::string s;
  for (unsigned int i = 0; i < n; i++) {
    s += std::to_string(v[i]);
    if (i + 1 < n) {
      s += ' ';
    }
  }
  return s;
}

std::string doubledPivotAttr(int doubled) {
  std::array<char, 32> buf{};
  // NOLINTBEGIN(cppcoreguidelines-pro-type-vararg)
  if (doubled % 2 == 0) {
    std::snprintf(buf.data(), buf.size(), "%d", doubled / 2);
  } else {
    std::snprintf(buf.data(), buf.size(), "%.1f", doubled * 0.5);
  }
  // NOLINTEND(cppcoreguidelines-pro-type-vararg)
  return buf.data();
}

void writeState(XmlWriter& xml, const State& st, unsigned int piecenumber,
                bool includeRotationFields) {
  xml.newTag("state");

  xml.newTag("dx");
  xml.addContent(joinNumbers(st.xs(), piecenumber));
  xml.endTag("dx");

  xml.newTag("dy");
  xml.addContent(joinNumbers(st.ys(), piecenumber));
  xml.endTag("dy");

  xml.newTag("dz");
  xml.addContent(joinNumbers(st.zs(), piecenumber));
  xml.endTag("dz");

  /* only in <solutionsWithRotations>: older versions cannot skip unknown
   * elements inside <state>, so classic <solutions> keep dx/dy/dz only */
  if (includeRotationFields) {
    if (st.hasOrientations()) {
      xml.newTag("dt");
      xml.addContent(joinNumbers(st.orientations(), piecenumber));
      xml.endTag("dt");
    }

    if (st.isRotationArrival()) {
      xml.newTag("rotation");
      xml.newAttrib("piece", st.getRotPiece());
      xml.newAttrib("px", doubledPivotAttr(st.getRotPivotX()));
      xml.newAttrib("py", doubledPivotAttr(st.getRotPivotY()));
      xml.newAttrib("pz", doubledPivotAttr(st.getRotPivotZ()));
      xml.newAttrib("axis", st.getRotAxis());
      xml.newAttrib("sense", st.getRotSense());
      xml.endTag("rotation");
    }
  }

  xml.endTag("state");
}

void writeSeparation(XmlWriter& xml, const Separation& sep, int type,
                     bool includeRotationFields) {
  xml.newTag("separation");

  if (type == 1) {
    xml.newAttrib("type", "left");
  } else if (type == 2) {
    xml.newAttrib("type", "removed");
  }

  xml.newTag("pieces");
  xml.newAttrib("count", asInt(sep.getPieceNumber()));
  for (unsigned int i = 0; i < sep.getPieceNumber(); i++) {
    xml.addContent(sep.getPieceName(i));
    if (i + 1 < sep.getPieceNumber()) {
      xml.addContent(" ");
    }
  }
  xml.endTag("pieces");

  for (unsigned int i = 0; i < sep.getNumStates(); i++) {
    writeState(xml, sep.getState(i), sep.getPieceNumber(), includeRotationFields);
  }

  // the removed and the remaining part, told apart by their type
  if (sep.getRemoved() != nullptr) {
    writeSeparation(xml, *sep.getRemoved(), 2, includeRotationFields);
  }
  if (sep.getLeft() != nullptr) {
    writeSeparation(xml, *sep.getLeft(), 1, includeRotationFields);
  }

  xml.endTag("separation");
}

void writeSeparationInfo(XmlWriter& xml, const SeparationInfo& info,
                         bool includeRotationCounts) {
  xml.newTag("separationInfo");

  const std::vector<unsigned int>& values = info.getValues();
  const std::vector<unsigned int>& rotValues = info.getRotValues();

  for (std::size_t i = 0; i < values.size(); i++) {
    xml.addContent(values[i]);
    if (i + 1 < values.size()) {
      xml.addContent(" ");
    }
  }

  if (includeRotationCounts &&
      std::any_of(rotValues.begin(), rotValues.end(), [](unsigned int r) { return r != 0; })) {
    xml.addContent(" | ");
    for (std::size_t i = 0; i < rotValues.size(); i++) {
      xml.addContent(rotValues[i]);
      if (i + 1 < rotValues.size()) {
        xml.addContent(" ");
      }
    }
  }

  xml.endTag("separationInfo");
}

void writeSolution(XmlWriter& xml, const Solution& sol, bool includeRotationFields) {
  xml.newTag("solution");

  if (sol.getAssemblyNumber() != 0) {
    xml.newAttrib("asmNum", sol.getAssemblyNumber());
  }

  if (sol.getDisassemblyInfo() != nullptr && sol.getSolutionNumber() != 0) {
    xml.newAttrib("solNum", sol.getSolutionNumber());
  }

  writeKeptAttributes(xml, sol.preserved());

  writeKept(xml, sol.preserved(), 0);
  writeAssembly(xml, sol.getAssembly());
  writeKept(xml, sol.preserved(), 1);

  if (sol.getDisassembly() != nullptr) {
    writeSeparation(xml, *sol.getDisassembly(), 0, includeRotationFields);
  } else if (sol.getSeparationInfo() != nullptr) {
    writeSeparationInfo(xml, *sol.getSeparationInfo(), includeRotationFields);
  }
  writeKept(xml, sol.preserved(), 2);

  xml.endTag("solution");
}

/// A rod's discs as the file holds them (encodeRod).
std::string encodeRod(const std::vector<stacking::DiskRef>& rod) {
  std::string str;
  for (std::size_t i = 0; i < rod.size(); i++) {
    if (i != 0) {
      str += ',';
    }
    str += std::to_string(rod[i].shapeId);
    str += ':';
    str += std::to_string(rod[i].instance);
  }
  return str;
}

void writeStackMap(XmlWriter& xml, const char* tag, const stacking::StackMap& map) {
  xml.newTag(tag);
  for (const std::vector<stacking::DiskRef>& rod : map.rods) {
    xml.newTag("rod");
    const std::string body = encodeRod(rod);
    if (!body.empty()) {
      xml.newAttrib("pieces", body);
    }
    xml.endTag("rod");
  }
  xml.endTag(tag);
}

/// A problem's <stacking> (stacking::saveProblem): written when it has a
/// rod set of the puzzle or stacks; a rod set the puzzle does not have is
/// not written.
void writeStacking(XmlWriter& xml, const Puzzle& puzzle, const Problem& prob) {
  const bool valid = prob.rodSetValid(puzzle);
  if (!valid && prob.startStacks().rods.empty() && prob.goalStacks().rods.empty()) {
    return;
  }
  xml.newTag("stacking");
  if (valid) {
    xml.newAttrib("rodSet", prob.getRodSetId());
  }
  writeStackMap(xml, "start", prob.startStacks());
  writeStackMap(xml, "goal", prob.goalStacks());
  xml.endTag("stacking");
}

/// A problem's <slideMarks>: the starts and goals of a sliding problem on
/// its board, a comma-separated mark per cell up to the last marked one
/// (a mark is a piece's shape index + 1; 0 for none). Written when it has
/// any.
void writeSlideMarks(XmlWriter& xml, const Problem& prob) {
  const SlideMarks& marks = prob.slideMarks();
  if (marks.empty()) {
    return;
  }
  xml.newTag("slideMarks");
  for (int goal = 0; goal < 2; goal++) {
    if (!marks.any(goal != 0)) {
      continue;
    }
    std::string list;
    for (std::size_t i = 0; i < marks.size(goal != 0); i++) {
      if (i != 0) {
        list += ',';
      }
      list += std::to_string(marks.get(i, goal != 0));
    }
    xml.newAttrib(goal != 0 ? "goal" : "start", list);
  }
  xml.endTag("slideMarks");
}

/// A number written in as few digits as read back to the same value.
std::string realText(double v) {
  std::array<char, 40> buf{};
  for (const int digits : {15, 16, 17}) {
    std::snprintf(buf.data(), buf.size(), "%.*g", digits, v);
    if (std::strtod(buf.data(), nullptr) == v) {
      break;
    }
  }
  return buf.data();
}

/// The puzzle's <tray> (decision 0045). A square tray is written with
/// `size`, as every tray was before there were rectangles, so those files
/// read and write as they did; a rectangle with `width` and `height`.
void writeTray(XmlWriter& xml, const tilted::Tray& t) {
  xml.newTag("tray");
  if (t.square()) {
    xml.newAttrib("size", t.width);
  } else {
    xml.newAttrib("width", t.width);
    xml.newAttrib("height", t.height);
  }
  xml.newAttrib("over", t.over);
  xml.newAttrib("down", t.down);
  xml.newAttrib("allowance", realText(t.allowance));
  xml.newAttrib("placement", t.placement == tilted::Tray::Placement::Best ? "best" : "custom");
  xml.newAttrib("shiftX", realText(t.shiftX));
  xml.newAttrib("shiftY", realText(t.shiftY));
  if (!t.walls.empty()) {
    std::string walls;
    walls.reserve(t.walls.size());
    for (const std::uint8_t w : t.walls) {
      walls.push_back(w != 0 ? '#' : '_');
    }
    xml.newAttrib("walls", walls);
  }
  xml.endTag("tray");
}

/// The puzzle's <rodSets> (stacking::saveRodSets): on a stacking puzzle, or
/// when there are any.
void writeRodSets(XmlWriter& xml, const Puzzle& puzzle) {
  if (puzzle.getGridType().getType() != GridKind::Stacking && puzzle.rodSetCount() == 0) {
    return;
  }
  xml.newTag("rodSets");
  for (unsigned int i = 0; i < puzzle.rodSetCount(); i++) {
    const stacking::RodSet& r = puzzle.getRodSet(i);
    xml.newTag("rodSet");
    if (!r.name.empty()) {
      xml.newAttrib("name", r.name);
    }
    xml.newAttrib("rods", r.rodCount);
    xml.newAttrib("grow", r.growHeight ? 1U : 0U);
    xml.newAttrib("height", r.definedHeight);
    xml.newAttrib("sizeMatters", r.sizeMatters ? 1U : 0U);
    xml.newAttrib("distanceMatters", r.distanceMatters ? 1U : 0U);
    xml.newAttrib("moveOver", r.canMoveOver ? 1U : 0U);
    if (r.panexColumns) {
      xml.newAttrib("panex", 1U);
    }
    if (r.pocketColumn) {
      xml.newAttrib("pocket", r.pocketHeight);
    }
    if (r.outsideChannel) {
      xml.newAttrib("channel", 1U);
    }
    xml.endTag("rodSet");
  }
  xml.endTag("rodSets");
}

void writeProblem(XmlWriter& xml, const Puzzle& puzzle, const Problem& prob) {
  const PreservedXml& kept = prob.preserved();

  xml.newTag("problem");

  if (!prob.getName().empty()) {
    xml.newAttrib("name", prob.getName());
  }
  if (prob.getUid() != 0) {
    char uid[17];
    std::snprintf(uid, sizeof(uid), "%016llx", static_cast<unsigned long long>(prob.getUid()));
    xml.newAttrib("uid", uid);
  }

  xml.newAttrib("state", static_cast<int>(prob.getSolveState()));

  if (prob.getSolveState() != ProblemState::Unsolved) {
    xml.newAttrib("assemblies", prob.getNumAssemblies());
    xml.newAttrib("solutions", prob.getNumSolutions());
    xml.newAttrib("time", prob.getUsedMs() / 1000);
    // the same to the millisecond; older versions read only "time"
    xml.newAttrib("timeMs", prob.getUsedMs());
    if (prob.getSearched() != 0) {
      xml.newAttrib("searched", prob.getSearched());
    }
    if (prob.getSearchDepth() != 0) {
      xml.newAttrib("searchDepth", prob.getSearchDepth());
    }
    if (prob.getTriedApart() != 0) {
      xml.newAttrib("triedApart", prob.getTriedApart());
    }
    if (prob.getSlideGrid() != 0) {
      xml.newAttrib("slideGrid", prob.getSlideGrid());
    }
    if (!prob.getSlideLevelMs().empty()) {
      std::string ms;
      for (const std::uint64_t v : prob.getSlideLevelMs()) {
        ms += (ms.empty() ? "" : " ") + (v == kUntimedMs ? std::string("-") : std::to_string(v));
      }
      xml.newAttrib("slideLevelMs", ms);
    }
  }

  if (prob.maxHolesDefined()) {
    xml.newAttrib("maxHoles", prob.getMaxHoles());
  }

  writeKeptAttributes(xml, kept);

  // the parts are saved as <shape> for historical reasons
  writeKept(xml, kept, kSlotParts);
  xml.newTag("shapes");
  for (const Part& part : prob.getParts()) {
    xml.newTag("shape");
    xml.newAttrib("id", part.shapeId);

    if (part.min == part.max) {
      xml.newAttrib("count", part.min);
    } else {
      xml.newAttrib("min", part.min);
      xml.newAttrib("max", part.max);
    }

    if (part.groups.size() == 1 && part.groups[0].count == part.max) {
      // all pieces in one group: an attribute, and none for group 0
      if (part.groups[0].group != 0) {
        xml.newAttrib("group", part.groups[0].group);
      }
    } else {
      for (const PartGroup& g : part.groups) {
        if (g.group != 0) {
          xml.newTag("group");
          xml.newAttrib("group", g.group);
          xml.newAttrib("count", g.count);
          xml.endTag("group");
        }
      }
    }
    xml.endTag("shape");
  }
  xml.endTag("shapes");

  writeKept(xml, kept, kSlotResult);
  xml.newTag("result");
  xml.newAttrib("id", ProblemFileAccess::resultRaw(prob));
  xml.endTag("result");

  writeKept(xml, kept, kSlotGoal);
  if (prob.goalValid()) {
    xml.newTag("goal");
    xml.newAttrib("id", prob.getGoalId());
    xml.endTag("goal");
  }

  writeKept(xml, kept, kSlotStacking);
  writeStacking(xml, puzzle, prob);

  writeKept(xml, kept, kSlotSlideMarks);
  writeSlideMarks(xml, prob);

  writeKept(xml, kept, kSlotSolverOptions);
  if (!prob.getSolverOptions().empty()) {
    xml.newTag("solverOptions");
    for (const auto& [name, value] : prob.getSolverOptions()) {
      xml.newAttrib(name, value);
    }
    xml.endTag("solverOptions");
  }

  writeKept(xml, kept, kSlotBitmap);
  xml.newTag("bitmap");
  for (const std::uint32_t c : prob.getColorConstraints()) {
    xml.newTag("pair");
    xml.newAttrib("piece", c >> 16);
    xml.newAttrib("result", c & 0xFFFF);
    xml.endTag("pair");
  }
  xml.endTag("bitmap");

  writeKept(xml, kept, kSlotAssembler);
  if (prob.getSolveState() == ProblemState::Solving && !prob.getAssemblerState().empty() &&
      !prob.getAssemblerVersion().empty()) {
    xml.newTag("assembler");
    xml.newAttrib("version", prob.getAssemblerVersion());
    xml.addContent(prob.getAssemblerState());
    xml.endTag("assembler");
  }

  // unfinished work of a paused solve; older versions skip the element
  writeKept(xml, kept, kSlotPending);
  if (prob.getSolveState() == ProblemState::Solving && !prob.getPending().empty()) {
    xml.newTag("pending");
    for (const PendingAssembly& p : prob.getPending()) {
      xml.newTag("item");
      xml.newAttrib("counted", p.counted ? 1u : 0u);
      xml.newAttrib("assembly", p.assemblyNumber);
      xml.newAttrib("solution", p.solutionNumber);
      writeAssembly(xml, p.assembly);
      xml.endTag("item");
    }
    xml.endTag("pending");
  }

  writeKept(xml, kept, kSlotSolutions);
  if (prob.getNumberOfSavedSolutions() != 0) {
    /* rotation-aware solutions go into a sibling element that older versions
     * skip; classic <solutions> stay free of <dt>/<rotation> */
    const bool rot = prob.getSolutionsWithRotations();
    const char* tag = rot ? "solutionsWithRotations" : "solutions";
    xml.newTag(tag);
    for (unsigned int i = 0; i < prob.getNumberOfSavedSolutions(); i++) {
      writeSolution(xml, prob.getSavedSolution(i), rot);
    }
    xml.endTag(tag);
  }
  writeKept(xml, kept, kSlotSolutions + 1);

  xml.endTag("problem");
}

}  // namespace

Result<Puzzle> readPuzzle(const XmlElement& root) {
  try {
    return readPuzzleElement(root);
  } catch (const ReadError& e) {
    return fail(Errc::ParseError, e.what());
  }
}

Result<Puzzle> parsePuzzle(std::string_view xml) {
  Result<XmlElement> root = parseXmlDocument(xml);
  if (!root) {
    return std::unexpected(root.error());
  }
  return readPuzzle(*root);
}

Result<Puzzle> loadPuzzle(const std::filesystem::path& path) {
  Result<std::string> data = readMaybeGzipFile(path);
  if (!data) {
    return std::unexpected(data.error());
  }
  Result<Puzzle> puzzle = parsePuzzle(*data);
  if (!puzzle) {
    return fail(puzzle.error().code, path.string() + ": " + puzzle.error().message);
  }
  return puzzle;
}

std::string writePuzzle(const Puzzle& puzzle) {
  const PreservedXml& kept = puzzle.preserved();
  XmlWriter xml;

  xml.newTag("puzzle");
  xml.newAttrib("version", "2");
  if (puzzle.getGridType().getType() == GridKind::Sliding) {
    // the starts and goals are the problems' (see readPuzzleElement)
    xml.newAttrib("slideMarks", "problem");
  }
  writeKeptAttributes(xml, kept);

  writeKept(xml, kept, kSlotGridType);
  xml.newTag("gridType");
  xml.newAttrib("type", static_cast<unsigned long>(puzzle.getGridType().getType()));
  xml.endTag("gridType");

  // the tilted tray, when the puzzle has one (decision 0045); the original
  // passes over the element
  writeKept(xml, kept, kSlotTray);
  if (puzzle.tray()) {
    writeTray(xml, *puzzle.tray());
  }

  writeKept(xml, kept, kSlotColors);
  xml.newTag("colors");
  for (const std::uint32_t c : PuzzleFileAccess::colors(puzzle)) {
    xml.newTag("color");
    xml.newAttrib("red", (c >> 0) & 0xFF);
    xml.newAttrib("green", (c >> 8) & 0xFF);
    xml.newAttrib("blue", (c >> 16) & 0xFF);
    xml.endTag("color");
  }
  xml.endTag("colors");

  // the pictures some shape shows; none, no element (decision 0038)
  writeKept(xml, kept, kSlotImages);
  if (std::any_of(puzzle.images().begin(), puzzle.images().end(),
                  [&puzzle](const PuzzleImage& im) { return puzzle.imageUsed(im.id); })) {
    xml.newTag("images");
    for (const PuzzleImage& im : puzzle.images()) {
      if (puzzle.imageUsed(im.id)) {
        xml.newTag("image");
        xml.newAttrib("id", im.id);
        xml.newAttrib("type", im.type);
        xml.newAttrib("width", im.width);
        xml.newAttrib("height", im.height);
        xml.addContent(base64Encode(*im.bytes));
        xml.endTag("image");
      }
    }
    xml.endTag("images");
  }

  writeKept(xml, kept, kSlotShapes);
  xml.newTag("shapes");
  for (unsigned int i = 0; i < puzzle.getNumberOfShapes(); i++) {
    writeVoxel(xml, puzzle.getShape(i));
  }
  xml.endTag("shapes");

  writeKept(xml, kept, kSlotRodSets);
  writeRodSets(xml, puzzle);

  writeKept(xml, kept, kSlotProblems);
  xml.newTag("problems");
  for (unsigned int i = 0; i < puzzle.getNumberOfProblems(); i++) {
    writeProblem(xml, puzzle, puzzle.getProblem(i));
  }
  xml.endTag("problems");

  writeKept(xml, kept, kSlotComment);
  xml.newTag("comment");
  if (!puzzle.getComment().empty()) {
    if (puzzle.getCommentPopup()) {
      xml.newAttrib("popup", "");
    }
    xml.addContent(puzzle.getComment());
  }
  xml.endTag("comment");
  writeKept(xml, kept, kSlotComment + 1);

  xml.endTag("puzzle");
  return xml.str();
}

Result<void> savePuzzle(const Puzzle& puzzle, const std::filesystem::path& path) {
  return writeGzipFile(path, writePuzzle(puzzle));
}

}  // namespace bt::io

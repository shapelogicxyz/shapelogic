// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/scadloader.cpp.

#include "bt/grid/voxel.h"
#include "bt/io/imports.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bt::io {

namespace {

constexpr unsigned int kMaxDim = 256;

bool isAlpha(char c) {
  return std::isalpha(static_cast<unsigned char>(c)) != 0;
}

bool isAlnum(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) != 0;
}

bool isDigit(char c) {
  return std::isdigit(static_cast<unsigned char>(c)) != 0;
}

bool isSpace(char c) {
  return std::isspace(static_cast<unsigned char>(c)) != 0;
}

class ScadParser {
 public:
  explicit ScadParser(std::string_view text) : s_(text) {}

  void setPos(std::size_t p) { i_ = p; }
  [[nodiscard]] bool eof() const { return i_ >= s_.size(); }

  void skip() {
    while (!eof()) {
      if (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r') {
        i_++;
        continue;
      }
      if (s_[i_] == '/' && i_ + 1 < s_.size() && s_[i_ + 1] == '/') {
        i_ += 2;
        while (!eof() && s_[i_] != '\n') {
          i_++;
        }
        continue;
      }
      if (s_[i_] == '/' && i_ + 1 < s_.size() && s_[i_ + 1] == '*') {
        i_ += 2;
        while (i_ + 1 < s_.size() && !(s_[i_] == '*' && s_[i_ + 1] == '/')) {
          i_++;
        }
        if (i_ + 1 < s_.size()) {
          i_ += 2;
        }
        continue;
      }
      break;
    }
  }

  bool eat(char c) {
    skip();
    if (!eof() && s_[i_] == c) {
      i_++;
      return true;
    }
    return false;
  }

  bool parseString(std::string& out) {
    skip();
    if (eof() || s_[i_] != '"') {
      return false;
    }
    i_++;
    out.clear();
    while (!eof() && s_[i_] != '"') {
      if (s_[i_] == '\\' && i_ + 1 < s_.size()) {
        i_++;
        out.push_back(s_[i_]);
        i_++;
        continue;
      }
      out.push_back(s_[i_]);
      i_++;
    }
    if (eof()) {
      return false;
    }
    i_++;  // the closing quote

    // OpenSCAD concatenates adjacent string literals
    skip();
    while (!eof() && s_[i_] == '"') {
      std::string more;
      if (!parseString(more)) {
        return false;
      }
      out += more;
      skip();
    }
    return true;
  }

  // Skips one OpenSCAD value (string, array, number, identifier, call...).
  bool skipValue() {
    skip();
    if (eof()) {
      return false;
    }

    if (s_[i_] == '"') {
      std::string dummy;
      return parseString(dummy);
    }

    if (s_[i_] == '[') {
      // arrays in arrays recurse: a crafted file could run the stack out
      // (real puzzlecad files nest a few deep)
      if (depth_ >= kMaxNesting) {
        return false;
      }
      const Nest nest(depth_);
      i_++;
      skip();
      if (!eat(']')) {
        while (true) {
          if (!skipValue()) {
            return false;
          }
          skip();
          if (eat(']')) {
            break;
          }
          if (!eat(',')) {
            return false;
          }
          skip();
          if (eat(']')) {
            break;
          }
        }
      }
      return true;
    }

    if (s_[i_] == '(') {
      return skipMatched('(', ')');
    }

    if (s_[i_] == '{') {
      return skipMatched('{', '}');
    }

    if (s_[i_] == '$' || s_[i_] == '_' || isAlpha(s_[i_])) {
      if (s_[i_] == '$') {
        i_++;
      }
      while (!eof() && (s_[i_] == '_' || isAlnum(s_[i_]))) {
        i_++;
      }
      skip();
      if (!eof() && s_[i_] == '(') {
        return skipMatched('(', ')');
      }
      return true;
    }

    if (s_[i_] == '-' || s_[i_] == '+' || s_[i_] == '.' || isDigit(s_[i_])) {
      if (s_[i_] == '-' || s_[i_] == '+') {
        i_++;
      }
      while (!eof() && (isDigit(s_[i_]) || s_[i_] == '.' || s_[i_] == 'e' || s_[i_] == 'E')) {
        i_++;
      }
      return true;
    }

    return false;
  }

  bool skipMatched(char open, char close) {
    skip();
    if (eof() || s_[i_] != open) {
      return false;
    }
    int depth = 0;
    bool inStr = false;
    while (!eof()) {
      const char c = s_[i_++];
      if (inStr) {
        if (c == '\\' && !eof()) {
          i_++;
        } else if (c == '"') {
          inStr = false;
        }
        continue;
      }
      if (c == '"') {
        inStr = true;
        continue;
      }
      if (c == open) {
        depth++;
      } else if (c == close) {
        depth--;
        if (depth == 0) {
          return true;
        }
      }
    }
    return false;
  }

  // Skips `$name = value` style named arguments.
  void skipNamedArgs() {
    while (true) {
      const std::size_t saved = i_;
      skip();
      if (eof()) {
        return;
      }
      if (s_[i_] != '$' && s_[i_] != '_' && !isAlpha(s_[i_])) {
        return;
      }
      if (s_[i_] == '$') {
        i_++;
      }
      while (!eof() && (s_[i_] == '_' || isAlnum(s_[i_]))) {
        i_++;
      }
      skip();
      if (!eat('=')) {
        i_ = saved;
        return;
      }
      if (!skipValue()) {
        i_ = saved;
        return;
      }
      skip();
      eat(',');
    }
  }

  bool parseStringList(std::vector<std::string>& layers) {
    if (!eat('[')) {
      return false;
    }
    layers.clear();
    skip();
    if (eat(']')) {
      return true;
    }
    while (true) {
      std::string str;
      if (!parseString(str)) {
        return false;
      }
      layers.push_back(str);
      skip();
      if (eat(']')) {
        return true;
      }
      if (!eat(',')) {
        return false;
      }
      skip();
      if (eat(']')) {
        return true;
      }
    }
  }

  // A piece is a string (one Z layer) or a vector of layer strings.
  bool parsePiece(std::vector<std::string>& layers) {
    skip();
    if (eof()) {
      return false;
    }
    if (s_[i_] == '"') {
      std::string str;
      if (!parseString(str)) {
        return false;
      }
      layers.clear();
      layers.push_back(str);
      return true;
    }
    if (s_[i_] == '[') {
      return parseStringList(layers);
    }
    return false;
  }

  bool parsePlate(std::vector<std::vector<std::string>>& pieces) {
    if (!eat('[')) {
      return false;
    }
    pieces.clear();
    skip();
    if (eat(']')) {
      return true;
    }
    while (true) {
      std::vector<std::string> piece;
      if (!parsePiece(piece)) {
        return false;
      }
      pieces.push_back(piece);
      skip();
      if (eat(']')) {
        return true;
      }
      if (!eat(',')) {
        return false;
      }
      skip();
      if (eat(']')) {
        return true;
      }
    }
  }

 private:
  static constexpr unsigned int kMaxNesting = 200;

  // counts one level of nesting while it lives
  class Nest {
   public:
    explicit Nest(unsigned int& d) : d_(d) { d_++; }
    ~Nest() { d_--; }
    Nest(const Nest&) = delete;
    Nest& operator=(const Nest&) = delete;

   private:
    unsigned int& d_;
  };

  std::string_view s_;
  std::size_t i_ = 0;
  unsigned int depth_ = 0;
};

bool isIdentStart(char c) {
  return c == '_' || isAlpha(c);
}

bool isIdentChar(char c) {
  return c == '_' || isAlnum(c);
}

struct ScadCall {
  std::string name;
  std::size_t argsPos = 0;  // the index of '('
};

// Scans the file, skipping comments and strings, for the puzzlecad calls
// not disabled with OpenSCAD's '*' prefix.
std::vector<ScadCall> findCalls(std::string_view s) {
  std::vector<ScadCall> calls;
  std::size_t i = 0;
  char lastSig = 0;
  enum class St { Norm, Line, Block, Str };
  St st = St::Norm;

  while (i < s.size()) {
    const char c = s[i];

    if (st == St::Line) {
      if (c == '\n') {
        st = St::Norm;
      }
      i++;
      continue;
    }
    if (st == St::Block) {
      if (c == '*' && i + 1 < s.size() && s[i + 1] == '/') {
        st = St::Norm;
        i += 2;
      } else {
        i++;
      }
      continue;
    }
    if (st == St::Str) {
      if (c == '\\' && i + 1 < s.size()) {
        i += 2;
      } else {
        if (c == '"') {
          st = St::Norm;
        }
        i++;
      }
      continue;
    }

    if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
      st = St::Line;
      i += 2;
      continue;
    }
    if (c == '/' && i + 1 < s.size() && s[i + 1] == '*') {
      st = St::Block;
      i += 2;
      continue;
    }
    if (c == '"') {
      st = St::Str;
      i++;
      continue;
    }

    if (isIdentStart(c) && (i == 0 || !isIdentChar(s[i - 1]))) {
      const std::size_t start = i;
      i++;
      while (i < s.size() && isIdentChar(s[i])) {
        i++;
      }
      const std::string_view word = s.substr(start, i - start);
      if (word == "burr_plate" || word == "burr_piece" || word == "packing_box") {
        if (lastSig != '*') {
          calls.push_back(ScadCall{std::string(word), i});
        }
      }
      lastSig = 'I';
      continue;
    }

    if (!isSpace(c)) {
      lastSig = c;
    }
    i++;
  }
  return calls;
}

using Rows = std::vector<std::vector<char>>;

Rows decodeLayer(const std::string& layer) {
  Rows rows;
  std::vector<char> row;
  for (std::size_t i = 0; i < layer.size();) {
    if (layer[i] == '{') {
      int depth = 1;
      i++;
      while (i < layer.size() && depth > 0) {
        if (layer[i] == '{') {
          depth++;
        } else if (layer[i] == '}') {
          depth--;
        }
        i++;
      }
      continue;
    }
    if (layer[i] == '|') {
      rows.push_back(row);
      row.clear();
      i++;
      continue;
    }
    if (layer[i] == ' ' || layer[i] == '\t' || layer[i] == '\r' || layer[i] == '\n') {
      i++;
      continue;
    }
    row.push_back(layer[i]);
    i++;
  }
  rows.push_back(row);
  return rows;
}

// What addShapeFromLayers did.
enum class Added { Yes, No, TooLarge };

// Turns puzzlecad layer strings into a cube-grid shape named `name`; No
// when the piece is empty or larger than kMaxDim, TooLarge when the
// import's shapes (`cells` so far) would pass `maxCells`.
Added addShapeFromLayers(Puzzle& p, const std::vector<std::string>& layers,
                         const std::string& name, std::uint64_t& cells, std::uint64_t maxCells,
                         unsigned int& idOut) {
  if (layers.empty()) {
    return Added::No;
  }

  std::vector<Rows> decoded;
  unsigned int sx = 0;
  unsigned int sy = 0;
  const auto sz = static_cast<unsigned int>(layers.size());

  for (const std::string& layer : layers) {
    Rows rows = decodeLayer(layer);
    if (rows.size() > sy) {
      sy = static_cast<unsigned int>(rows.size());
    }
    for (const std::vector<char>& row : rows) {
      if (row.size() > sx) {
        sx = static_cast<unsigned int>(row.size());
      }
    }
    decoded.push_back(std::move(rows));
  }

  if (sx == 0 || sy == 0 || sz == 0) {
    return Added::No;
  }
  if (sx > kMaxDim || sy > kMaxDim || sz > kMaxDim) {
    return Added::No;
  }

  bool anyFilled = false;
  for (unsigned int z = 0; z < sz && !anyFilled; z++) {
    for (std::size_t y = 0; y < decoded[z].size() && !anyFilled; y++) {
      for (const char c : decoded[z][y]) {
        if (c != '.') {
          anyFilled = true;
          break;
        }
      }
    }
  }

  if (!anyFilled) {
    return Added::No;
  }

  cells += std::uint64_t{sx} * sy * sz;
  if (cells > maxCells) {
    return Added::TooLarge;
  }

  const unsigned int id = p.addShape(sx, sy, sz);
  Voxel& sh = p.getShape(id);
  sh.setName(name);

  for (unsigned int z = 0; z < sz; z++) {
    for (std::size_t y = 0; y < decoded[z].size(); y++) {
      for (std::size_t x = 0; x < decoded[z][y].size(); x++) {
        if (decoded[z][y][x] != '.') {
          sh.setState(static_cast<unsigned int>(x), static_cast<unsigned int>(y), z,
                      Voxel::kFilled);
        }
      }
    }
  }

  idOut = id;
  return Added::Yes;
}

Result<Puzzle> tooLarge() {
  return fail(Errc::ParseError, "the pieces would be too large");
}

bool shapeToLayers(const Voxel& v, std::vector<std::string>& layers) {
  layers.clear();
  if (v.countState(Voxel::kFilled) == 0 && v.countState(Voxel::kVariable) == 0) {
    return false;
  }

  const unsigned int x0 = v.boundX1();
  const unsigned int x1 = v.boundX2();
  const unsigned int y0 = v.boundY1();
  const unsigned int y1 = v.boundY2();
  const unsigned int z0 = v.boundZ1();
  const unsigned int z1 = v.boundZ2();

  for (unsigned int z = z0; z <= z1; z++) {
    std::string layer;
    for (unsigned int y = y0; y <= y1; y++) {
      if (y > y0) {
        layer += '|';
      }
      for (unsigned int x = x0; x <= x1; x++) {
        layer += (v.getState(x, y, z) == Voxel::kEmpty) ? '.' : 'x';
      }
    }
    layers.push_back(layer);
  }
  return !layers.empty();
}

std::string baseName(std::string_view path) {
  if (path.empty()) {
    return "unknown";
  }
  std::string n(path);
  const std::size_t slash = n.find_last_of("/\\");
  if (slash != std::string::npos) {
    n = n.substr(slash + 1);
  }
  return n.empty() ? "unknown" : n;
}

}  // namespace

Result<Puzzle> readPuzzlecad(std::string_view text, std::uint64_t maxCells) {
  const std::vector<ScadCall> calls = findCalls(text);
  if (calls.empty()) {
    return fail(Errc::ParseError, "the file has no burr_plate, burr_piece or packing_box");
  }

  Puzzle p;
  const unsigned int prIdx = p.addProblem();
  std::vector<std::string> resultLayers;
  unsigned int pieceCount = 0;
  std::uint64_t cells = 0;

  const auto addPiece = [&](const std::vector<std::string>& layers) -> bool {
    const std::string nm = "Piece " + std::to_string(pieceCount + 1);
    unsigned int id = 0;
    const Added added = addShapeFromLayers(p, layers, nm, cells, maxCells, id);
    if (added == Added::Yes) {
      p.getProblem(prIdx).setShapeMinimum(p, id, 1);
      pieceCount++;
    }
    return added != Added::TooLarge;
  };

  for (const ScadCall& call : calls) {
    ScadParser parser(text);
    parser.setPos(call.argsPos);
    if (!parser.eat('(')) {
      continue;
    }
    parser.skipNamedArgs();

    if (call.name == "burr_plate") {
      std::vector<std::vector<std::string>> pieces;
      if (!parser.parsePlate(pieces)) {
        continue;
      }
      for (const std::vector<std::string>& piece : pieces) {
        if (!addPiece(piece)) {
          return tooLarge();
        }
      }
    } else if (call.name == "burr_piece") {
      std::vector<std::string> layers;
      if (!parser.parsePiece(layers)) {
        continue;
      }
      if (!addPiece(layers)) {
        return tooLarge();
      }
    } else if (call.name == "packing_box") {
      std::vector<std::string> layers;
      if (parser.parsePiece(layers) && resultLayers.empty()) {
        resultLayers = layers;
      }
    }
  }

  if (pieceCount == 0) {
    return fail(Errc::ParseError, "the file has no piece BurrTools can read");
  }

  Problem& pr = p.getProblem(prIdx);
  pr.setName("Puzzle");

  if (!resultLayers.empty()) {
    unsigned int id = 0;
    const Added added = addShapeFromLayers(p, resultLayers, "Result", cells, maxCells, id);
    if (added == Added::TooLarge) {
      return tooLarge();
    }
    if (added == Added::Yes) {
      pr.setResultId(p, id);
    }
  }

  if (!pr.resultValid()) {
    unsigned int cube = 1;
    for (unsigned int s = 0; s < p.getNumberOfShapes(); s++) {
      const Voxel& v = p.getShape(s);
      if (v.countState(Voxel::kFilled) == 0 && v.countState(Voxel::kVariable) == 0) {
        continue;
      }
      const unsigned int dx = v.boundX2() - v.boundX1() + 1;
      const unsigned int dy = v.boundY2() - v.boundY1() + 1;
      const unsigned int dz = v.boundZ2() - v.boundZ1() + 1;
      if (dx > cube) {
        cube = dx;
      }
      if (dy > cube) {
        cube = dy;
      }
      if (dz > cube) {
        cube = dz;
      }
    }
    if (cube > kMaxDim) {
      cube = kMaxDim;
    }

    cells += std::uint64_t{cube} * cube * cube;
    if (cells > maxCells) {
      return tooLarge();
    }

    const unsigned int id = p.addShape(cube, cube, cube);
    Voxel& sh = p.getShape(id);
    sh.setName("Result");
    sh.skipRecalcBoundingBox(true);
    for (unsigned int i = 0; i < sh.getXYZ(); i++) {
      sh.setState(i, Voxel::kVariable);
    }
    sh.skipRecalcBoundingBox(false);
    pr.setResultId(p, id);
  }

  return p;
}

std::optional<std::string> writePuzzlecad(const Puzzle& p, const Problem* problem,
                                          std::string_view sourceName) {
  std::vector<unsigned int> shapeIds;
  std::int64_t skipResult = -1;

  if (problem != nullptr) {
    if (problem->resultValid()) {
      skipResult = problem->getResultId();
    }
    for (unsigned int i = 0; i < problem->getNumberOfParts(); i++) {
      const unsigned int id = problem->getShapeIdOfPart(i);
      if (std::int64_t{id} == skipResult) {
        continue;
      }
      bool seen = false;
      for (const unsigned int k : shapeIds) {
        if (k == id) {
          seen = true;
        }
      }
      if (!seen) {
        shapeIds.push_back(id);
      }
    }
  }

  if (shapeIds.empty()) {
    for (unsigned int s = 0; s < p.getNumberOfShapes(); s++) {
      if (std::int64_t{s} != skipResult) {
        shapeIds.push_back(s);
      }
    }
  }

  std::vector<std::vector<std::string>> pieces;
  for (const unsigned int id : shapeIds) {
    std::vector<std::string> layers;
    if (shapeToLayers(p.getShape(id), layers)) {
      pieces.push_back(layers);
    }
  }

  if (pieces.empty()) {
    return std::nullopt;
  }

  std::string str =
      "include <puzzlecad.scad>\n"
      "\n"
      "// This model was generated by BurrTools export for the file:\n"
      "// " +
      baseName(sourceName) +
      "\n"
      "\n"
      "// You can freely edit this file to make changes to the model structure or parameters.\n"
      "\n"
      "require_puzzlecad_version(\"2.3.1\");\n"
      "\n"
      "$plate_width = 240;\n"
      "$plate_sep = 3;\n"
      "$unit_beveled = false;\n"
      "$auto_layout = true;\n"
      "\n"
      "// Use burr_scal = 15.7 to match live cubes\n"
      "$burr_scale = 15.7;\n"
      "$burr_bevel = 1.2;\n"
      "\n"
      "$burr_inset = 0.05;\n"
      "$joint_inset= 0.01;\n"
      "\n"
      "$box_wall_thickness = 4;\n"
      "$thatch_thickness = 4;\n"
      "\n"
      "*packing_box(\n"
      "\n"
      "$box_wall_thickness = 4\n"
      ");\n"
      "\n"
      "burr_plate([\n";

  for (const std::vector<std::string>& piece : pieces) {
    str += "    [ ";
    for (std::size_t z = 0; z < piece.size(); z++) {
      if (z != 0) {
        str += "      ";
      }
      str += '"';
      str += piece[z];
      str += '"';
      if (z + 1 < piece.size()) {
        str += ",\n";
      }
    }
    str += " ],\n";
  }

  str += "]);\n";
  return str;
}

}  // namespace bt::io

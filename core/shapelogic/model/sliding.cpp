// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/sliding.cpp: the start/goal shapes, stamps, start
// locks, the older goal-map model and the checks (the first half of the
// file; the search is bt/solve/sliding_search.cpp).
#include "shapelogic/model/sliding.h"

#include "shapelogic/base/int_cast.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/model/assembly.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <set>
#include <sstream>

namespace bt::sliding {

namespace {

constexpr const char* kGoalShapeName = "__sliding_goal__";
constexpr const char* kStartGoalPrefix = "sg:";
constexpr const char* kHiddenPrefix = "__sggoal:";

// The older goal-map model's trays are one layer: their floor is z = 0.
bool isFloor(const Voxel& tray, int x, int y) {
  if (x < 0 || y < 0 || asUnsigned(x) >= tray.getX() || asUnsigned(y) >= tray.getY()) {
    return false;
  }
  return tray.getState(asUnsigned(x), asUnsigned(y), 0) != Voxel::kEmpty;
}

// Colour indices (from 1) that follow the piece's part order are preferred.
unsigned int findOrAddColor(Puzzle& puzzle, unsigned int preferredIndex) {
  while (puzzle.colorNumber() < preferredIndex) {
    const unsigned int n = puzzle.colorNumber();
    puzzle.addColor(static_cast<std::uint8_t>(40 + 30 * (n % 3)),
                    static_cast<std::uint8_t>(40 + 30 * ((n + 1) % 3)),
                    static_cast<std::uint8_t>(40 + 30 * ((n + 2) % 3)));
  }
  return preferredIndex;
}

unsigned int piecePartIndex(const Problem& problem, unsigned int shapeId) {
  for (unsigned int i = 0; i < problem.getNumberOfParts(); i++) {
    if (problem.getShapeIdOfPart(i) == shapeId) {
      return i + 1;
    }
  }
  return 1;
}

void fillTray(Voxel& v) {
  for (unsigned int y = 0; y < v.getY(); y++) {
    for (unsigned int x = 0; x < v.getX(); x++) {
      v.setState(x, y, 0, Voxel::kFilled);
    }
  }
}

void paintPieceNeutral(Voxel& piece) {
  for (unsigned int i = 0; i < piece.getXYZ(); i++) {
    if (piece.getState(i) != Voxel::kEmpty) {
      piece.setColor(i, 0);
    }
  }
}

void paintPieceColor(Voxel& piece, unsigned int color) {
  for (unsigned int i = 0; i < piece.getXYZ(); i++) {
    if (piece.getState(i) != Voxel::kEmpty) {
      piece.setColor(i, color);
    }
  }
}

bool footprintFits(const Voxel& tray, const Voxel& piece, int ax, int ay,
                   unsigned int ignoreColor = 0) {
  const int hx = piece.getHx();
  const int hy = piece.getHy();
  for (unsigned int z = 0; z < piece.getZ(); z++) {
    for (unsigned int y = 0; y < piece.getY(); y++) {
      for (unsigned int x = 0; x < piece.getX(); x++) {
        if (piece.getState(x, y, z) == Voxel::kEmpty) {
          continue;
        }
        const int tx = ax + asInt(x) - hx;
        const int ty = ay + asInt(y) - hy;
        if (!isFloor(tray, tx, ty)) {
          return false;
        }
        const unsigned int c = tray.getColor(asUnsigned(tx), asUnsigned(ty), 0);
        if (c != 0 && c != ignoreColor) {
          return false;
        }
      }
    }
  }
  return true;
}

void paintFootprint(Voxel& tray, const Voxel& piece, int ax, int ay, unsigned int color) {
  const int hx = piece.getHx();
  const int hy = piece.getHy();
  for (unsigned int z = 0; z < piece.getZ(); z++) {
    for (unsigned int y = 0; y < piece.getY(); y++) {
      for (unsigned int x = 0; x < piece.getX(); x++) {
        if (piece.getState(x, y, z) == Voxel::kEmpty) {
          continue;
        }
        const int tx = ax + asInt(x) - hx;
        const int ty = ay + asInt(y) - hy;
        if (isFloor(tray, tx, ty)) {
          tray.setColor(asUnsigned(tx), asUnsigned(ty), 0, color);
        }
      }
    }
  }
}

void eraseColor(Voxel& tray, unsigned int color) {
  for (unsigned int i = 0; i < tray.getXYZ(); i++) {
    if (tray.getColor(i) == color) {
      tray.setColor(i, 0);
    }
  }
}

// Finds any cell of the colour and works the hotspot position back from the
// first filled cell of the piece.
bool findColorAnchor(const Voxel& tray, const Voxel& piece, unsigned int color, int* ax,
                     int* ay) {
  int firstPx = -1;
  int firstPy = -1;
  for (unsigned int y = 0; y < piece.getY() && firstPx < 0; y++) {
    for (unsigned int x = 0; x < piece.getX() && firstPx < 0; x++) {
      if (piece.getState(x, y, 0) != Voxel::kEmpty) {
        firstPx = asInt(x);
        firstPy = asInt(y);
      }
    }
  }
  if (firstPx < 0) {
    return false;
  }

  for (unsigned int y = 0; y < tray.getY(); y++) {
    for (unsigned int x = 0; x < tray.getX(); x++) {
      if (tray.getState(x, y, 0) != Voxel::kEmpty && tray.getColor(x, y, 0) == color) {
        const int hx = piece.getHx();
        const int hy = piece.getHy();
        *ax = asInt(x) - firstPx + hx;
        *ay = asInt(y) - firstPy + hy;
        // the whole footprint must match
        if (footprintFits(tray, piece, *ax, *ay, color)) {
          bool ok = true;
          for (unsigned int py = 0; py < piece.getY() && ok; py++) {
            for (unsigned int px = 0; px < piece.getX() && ok; px++) {
              if (piece.getState(px, py, 0) == Voxel::kEmpty) {
                continue;
              }
              const int tx = *ax + asInt(px) - hx;
              const int ty = *ay + asInt(py) - hy;
              if (!isFloor(tray, tx, ty) ||
                  tray.getColor(asUnsigned(tx), asUnsigned(ty), 0) != color) {
                ok = false;
              }
            }
          }
          if (ok) {
            return true;
          }
        }
      }
    }
  }
  return false;
}

// The lowest layer first, then the top row of a layer, as the grid editor
// shows it (y rises upwards), and left to right in a row: the first cell
// is the top-left one of the lowest layer, where a click places a piece.
bool topLeftFirst(const TrayCell& a, const TrayCell& b) {
  if (a[2] != b[2]) {
    return a[2] < b[2];
  }
  return a[1] != b[1] ? a[1] > b[1] : a[0] < b[0];
}

// The filled cells of a piece, every layer.
std::vector<TrayCell> pieceCells(const Voxel& piece) {
  std::vector<TrayCell> cells;
  for (unsigned int z = 0; z < piece.getZ(); z++) {
    for (unsigned int y = 0; y < piece.getY(); y++) {
      for (unsigned int x = 0; x < piece.getX(); x++) {
        if (piece.getState(x, y, z) != Voxel::kEmpty) {
          cells.push_back({asInt(x), asInt(y), asInt(z)});
        }
      }
    }
  }
  return cells;
}

// The cells moved so that the first one (topLeftFirst) is at (0, 0, 0).
std::vector<TrayCell> fromFirst(std::vector<TrayCell> cells) {
  std::sort(cells.begin(), cells.end(), topLeftFirst);
  if (!cells.empty()) {
    const TrayCell o = cells[0];
    for (TrayCell& c : cells) {
      for (std::size_t a = 0; a < 3; a++) {
        c[a] -= o[a];
      }
    }
  }
  return cells;
}

// The cells of a tray marked with shape id, as its start or goal.
std::vector<TrayCell> stampedCells(const Voxel& tray, unsigned int id, bool goal);

unsigned int stampOf(const Voxel& tray, unsigned int i, bool goal) {
  return goal ? tray.getGoalPiece(i) : tray.getColor(i);
}

std::vector<TrayCell> stampedCells(const Voxel& tray, unsigned int id, bool goal) {
  std::vector<TrayCell> cells;
  for (unsigned int z = 0; z < tray.getZ(); z++) {
    for (unsigned int y = 0; y < tray.getY(); y++) {
      for (unsigned int x = 0; x < tray.getX(); x++) {
        if (stampOf(tray, tray.getIndex(x, y, z), goal) == id) {
          cells.push_back({asInt(x), asInt(y), asInt(z)});
        }
      }
    }
  }
  return cells;
}

// The tray holds (x, y, z).
bool inTray(const Voxel& tray, int x, int y, int z) {
  return x >= 0 && y >= 0 && z >= 0 && x < asInt(tray.getX()) && y < asInt(tray.getY()) &&
         z < asInt(tray.getZ());
}

void paintPieceLock(Voxel& piece, unsigned int color) {
  for (unsigned int i = 0; i < piece.getXYZ(); i++) {
    const unsigned int want = piece.getState(i) == Voxel::kEmpty ? 0 : color;
    // A cell that already has it is left alone: every write drops the
    // shape's caches, which the threads of a run read (the original wrote
    // every cell; the shape ends the same).
    if (piece.getColor(i) != want) {
      piece.setColor(i, want);
    }
  }
}

// The cells of a piece placed in an assembly (transformed and moved).
std::vector<TrayCell> placedCells(const Voxel& shape, const Assembly& a, unsigned int pc) {
  std::vector<TrayCell> out;
  std::unique_ptr<Voxel> v = shape.clone();
  if (!v->transform(a.getTransformation(pc))) {
    return out;
  }
  const int hx = v->getHx();
  const int hy = v->getHy();
  const int hz = v->getHz();
  for (const TrayCell& c : pieceCells(*v)) {
    out.push_back({a.getX(pc) + c[0] - hx, a.getY(pc) + c[1] - hy, a.getZ(pc) + c[2] - hz});
  }
  return out;
}

}  // namespace

bool isSliding(const Puzzle& puzzle) { return puzzle.getGridType().getType() == GridKind::Sliding; }

bool isStartGoalShape(const Voxel* v) {
  return v != nullptr && v->getName().rfind(kStartGoalPrefix, 0) == 0;
}

bool isHiddenSlidingShape(const Voxel* v) {
  return v != nullptr && v->getName().rfind(kHiddenPrefix, 0) == 0;
}

bool isReservedName(std::string_view name) {
  return name.starts_with(kStartGoalPrefix) || name.starts_with(kHiddenPrefix);
}

int startGoalNumber(const Voxel* v) {
  if (!isStartGoalShape(v)) {
    return 0;
  }
  return std::atoi(v->getName().c_str() + 3);
}

std::string startGoalUserName(const Voxel* v) {
  if (!isStartGoalShape(v)) {
    return v != nullptr ? v->getName() : std::string();
  }
  const std::string& n = v->getName();
  const std::size_t colon = n.find(':', 3);
  if (colon == std::string::npos || colon + 1 >= n.size()) {
    return std::string();
  }
  return n.substr(colon + 1);
}

void setStartGoalUserName(Voxel* v, const std::string& user) {
  if (v == nullptr || !isStartGoalShape(v)) {
    if (v != nullptr) {
      v->setName(user);
    }
    return;
  }
  const int num = startGoalNumber(v);
  v->setName(std::string(kStartGoalPrefix) + std::to_string(num) + ":" + user);
}

std::string defaultStartGoalName(int n) { return "(START/GOAL " + std::to_string(n) + ")"; }

std::string displayName(const Voxel* v) {
  if (v == nullptr) {
    return std::string();
  }
  if (!isStartGoalShape(v)) {
    return v->getName();
  }
  const std::string user = startGoalUserName(v);
  return user.empty() ? defaultStartGoalName(startGoalNumber(v)) : user;
}

std::string shapeLabel(const Puzzle& puzzle, unsigned int shape) {
  if (!isSliding(puzzle) || shape >= puzzle.getNumberOfShapes()) {
    return "S" + std::to_string(shape + 1);
  }
  const bool board = isStartGoalShape(&puzzle.getShape(shape));
  unsigned int place = 1;
  for (unsigned int i = 0; i < shape; i++) {
    if (isStartGoalShape(&puzzle.getShape(i)) == board) {
      place++;
    }
  }
  return (board ? "B" : "S") + std::to_string(place);
}

void renumberStartGoalShapes(Puzzle& puzzle) {
  int n = 0;
  for (unsigned int i = 0; i < puzzle.getNumberOfShapes(); i++) {
    Voxel& v = puzzle.getShape(i);
    if (!isStartGoalShape(&v)) {
      continue;
    }
    n++;
    if (startGoalNumber(&v) != n) {
      v.setName(std::string(kStartGoalPrefix) + std::to_string(n) + ":" + startGoalUserName(&v));
    }
  }
}

unsigned int addStartGoalShape(Puzzle& puzzle, unsigned int sx, unsigned int sy,
                               unsigned int sz) {
  int next = 1;
  for (unsigned int i = 0; i < puzzle.getNumberOfShapes(); i++) {
    const int n = startGoalNumber(&puzzle.getShape(i));
    if (n >= next) {
      next = n + 1;
    }
  }
  sx = std::max(sx, 1U);
  sy = std::max(sy, 1U);
  sz = std::max(sz, 1U);
  const unsigned int id = puzzle.addShape(sx, sy, sz);
  Voxel& v = puzzle.getShape(id);
  for (unsigned int z = 0; z < v.getZ(); z++) {
    for (unsigned int y = 0; y < v.getY(); y++) {
      for (unsigned int x = 0; x < v.getX(); x++) {
        if (v.validCoordinate(asInt(x), asInt(y), asInt(z))) {
          v.setState(x, y, z, Voxel::kFilled);
        }
      }
    }
  }
  v.setName(std::string(kStartGoalPrefix) + std::to_string(next) + ":");
  return id;
}

void noteShapeRemoved(Puzzle& puzzle, unsigned int shapeId) {
  const unsigned int gone = shapeId + 1;
  for (unsigned int s = 0; s < puzzle.getNumberOfShapes(); s++) {
    if (!isStartGoalShape(&puzzle.getShape(s))) {
      continue;
    }
    Voxel& v = puzzle.getShape(s);
    for (unsigned int i = 0; i < v.getXYZ(); i++) {
      const unsigned int c = v.getColor(i);
      if (c == gone) {
        v.setColor(i, 0);
      } else if (c > gone) {
        v.setColor(i, c - 1);
      }
      const unsigned int g = v.getGoalPiece(i);
      if (g == gone) {
        v.setGoalPiece(i, 0);
      } else if (g > gone) {
        v.setGoalPiece(i, g - 1);
      }
    }
  }
}

std::vector<unsigned int> noteShapeSwap(Puzzle& puzzle, unsigned int a, unsigned int b) {
  std::vector<unsigned int> lost;
  if (a == b) {
    return lost;
  }
  const unsigned int ca = a + 1;
  const unsigned int cb = b + 1;
  // A mark is a cell's colour, six bits: a shape past the 63rd place can
  // hold none (toggleCellMark and stampFits refuse it), so a mark that
  // would follow its shape there goes instead.
  const auto to = [](unsigned int id) { return id > 63 ? 0U : id; };
  for (unsigned int s = 0; s < puzzle.getNumberOfShapes(); s++) {
    if (!isStartGoalShape(&puzzle.getShape(s))) {
      continue;
    }
    Voxel& v = puzzle.getShape(s);
    bool dropped = false;
    for (unsigned int i = 0; i < v.getXYZ(); i++) {
      const unsigned int c = v.getColor(i);
      if (c == ca) {
        v.setColor(i, to(cb));
        dropped = dropped || to(cb) == 0;
      } else if (c == cb) {
        v.setColor(i, to(ca));
        dropped = dropped || to(ca) == 0;
      }
      const unsigned int g = v.getGoalPiece(i);
      if (g == ca) {
        v.setGoalPiece(i, to(cb));
        dropped = dropped || to(cb) == 0;
      } else if (g == cb) {
        v.setGoalPiece(i, to(ca));
        dropped = dropped || to(ca) == 0;
      }
    }
    if (dropped) {
      lost.push_back(s);
    }
  }
  return lost;
}

bool toggleCellMark(Voxel* tray, int x, int y, int z, unsigned int shapeId, bool goal) {
  if (tray == nullptr || !inTray(*tray, x, y, z) || !tray->validCoordinate(x, y, z)) {
    return false;
  }
  const unsigned int i = tray->getIndex(asUnsigned(x), asUnsigned(y), asUnsigned(z));
  if (tray->getState(i) == Voxel::kEmpty) {
    return false;
  }
  const unsigned int id = shapeId + 1;
  if (id > 63) {
    return false;
  }
  if (goal) {
    tray->setGoalPiece(i, tray->getGoalPiece(i) == id ? 0 : id);
  } else {
    tray->setColor(i, tray->getColor(i) == id ? 0 : id);
  }
  return true;
}

namespace {

// Why cells marked for shape id would not do, or empty when they would. A
// cell of the piece's own marks is free only when the stamp replaces them
// (a piece of one copy moved).
std::string cellsFit(const Voxel& tray, const std::vector<TrayCell>& cells, unsigned int id,
                     bool goal, bool replaces) {
  for (const TrayCell& c : cells) {
    if (!inTray(tray, c[0], c[1], c[2])) {
      return "The piece does not fit there: part of it would be off the floor.";
    }
    const unsigned int i = tray.getIndex(asUnsigned(c[0]), asUnsigned(c[1]), asUnsigned(c[2]));
    if (tray.getState(i) == Voxel::kEmpty) {
      return "The piece does not fit there: part of it would be off the floor.";
    }
    if (goal && tray.getState(i) == Voxel::kVariable) {
      return "A goal cannot be on a variable cell: pieces may cross those, not stop on them.";
    }
    const unsigned int other = stampOf(tray, i, goal);
    if (other != 0 && other != id) {
      return std::string("The piece would cover the ") + (goal ? "goal" : "start") + " of S" +
             std::to_string(other) + ".";
    }
    if (other == id && !replaces) {
      return std::string("The piece would cover the ") + (goal ? "goal" : "start") +
             " of another copy of it.";
    }
  }
  return std::string();
}

// The marked cells split into whole copies of the piece as drawn, by their
// first cells: the first cell left is the first of a copy, so the split is
// forced. nullopt when the marks are not whole copies.
std::optional<std::vector<std::vector<TrayCell>>> splitCopies(std::vector<TrayCell> marked,
                                                              const Voxel& piece) {
  std::vector<std::vector<TrayCell>> out;
  if (marked.empty()) {
    return out;
  }
  const std::vector<TrayCell> shape = fromFirst(pieceCells(piece));
  if (shape.empty()) {
    return std::nullopt;
  }
  std::sort(marked.begin(), marked.end(), topLeftFirst);
  std::set<TrayCell> left(marked.begin(), marked.end());
  for (const TrayCell& first : marked) {
    if (!left.contains(first)) {
      continue;
    }
    std::vector<TrayCell> copy;
    copy.reserve(shape.size());
    for (const TrayCell& c : shape) {
      const TrayCell at{first[0] + c[0], first[1] + c[1], first[2] + c[2]};
      if (left.erase(at) == 0) {
        return std::nullopt;
      }
      copy.push_back(at);
    }
    std::sort(copy.begin(), copy.end(), topLeftFirst);
    out.push_back(std::move(copy));
  }
  return out;
}

// How many copies of the piece the marks of id hold: whole copies, or the
// cells over the piece's size rounded up when they are not whole pieces.
unsigned int placedCopies(const Voxel& tray, const Voxel& piece, unsigned int id, bool goal) {
  std::vector<TrayCell> marked = stampedCells(tray, id, goal);
  if (marked.empty()) {
    return 0;
  }
  const std::size_t size = pieceCells(piece).size();
  const std::size_t cells = marked.size();
  const auto copies = splitCopies(std::move(marked), piece);
  if (copies) {
    return static_cast<unsigned int>(copies->size());
  }
  return size == 0 ? 1U : static_cast<unsigned int>((cells + size - 1) / size);
}

}  // namespace

std::vector<TrayCell> stampCells(const Voxel* tray, const Voxel* piece, unsigned int shapeId,
                                 int x, int y, int z, bool goal, unsigned int copies) {
  std::vector<TrayCell> none;
  if (piece == nullptr) {
    return none;
  }
  // the piece's cells, its lowest layer at 0
  std::vector<TrayCell> cells = pieceCells(*piece);
  if (cells.empty()) {
    return none;
  }
  int low = cells[0][2];
  int high = low;
  for (const TrayCell& c : cells) {
    low = std::min(low, c[2]);
    high = std::max(high, c[2]);
  }
  for (TrayCell& c : cells) {
    c[2] -= low;
  }
  const int height = high - low + 1;

  // Each height that puts a layer of the piece on layer z, lowest first:
  // the slice of the piece there goes with its top-left cell at (x, y). The
  // lowest that fits wins, else the lowest with a slice there.
  std::vector<TrayCell> first;
  for (int base = std::max(0, z - height + 1); base <= z; base++) {
    const int layer = z - base;
    std::vector<TrayCell> slice;
    for (const TrayCell& c : cells) {
      if (c[2] == layer) {
        slice.push_back(c);
      }
    }
    if (slice.empty()) {
      continue;
    }
    const TrayCell anchor = *std::min_element(slice.begin(), slice.end(), topLeftFirst);
    std::vector<TrayCell> placed = cells;
    for (TrayCell& c : placed) {
      c = {c[0] - anchor[0] + x, c[1] - anchor[1] + y, c[2] + base};
    }
    std::sort(placed.begin(), placed.end(), topLeftFirst);
    if (tray == nullptr || cellsFit(*tray, placed, shapeId + 1, goal, copies <= 1).empty()) {
      return placed;
    }
    if (first.empty()) {
      first = std::move(placed);
    }
  }
  return first;
}

std::string stampFits(const Voxel* tray, const Voxel* piece, unsigned int shapeId, int x, int y,
                      int z, bool goal, unsigned int copies) {
  if (tray == nullptr || piece == nullptr) {
    return "No piece is chosen.";
  }
  const unsigned int id = shapeId + 1;
  if (id > 63) {
    return "Only the first 63 shapes can be placed.";
  }
  const std::vector<TrayCell> cells = stampCells(tray, piece, shapeId, x, y, z, goal, copies);
  if (cells.empty()) {
    return pieceCells(*piece).empty() ? "The piece has no cells."
                                      : "The piece has no cells on this layer there.";
  }
  if (copies > 1 && placedCopies(*tray, *piece, id, goal) >= copies) {
    return "All " + std::to_string(copies) + " copies of the piece have a " +
           (goal ? "goal" : "start") + " already: take one off first (right-click it).";
  }
  return cellsFit(*tray, cells, id, goal, copies <= 1);
}

std::string stampPiece(Voxel* tray, const Voxel* piece, unsigned int shapeId, int x, int y, int z,
                       bool goal, unsigned int copies) {
  std::string why = stampFits(tray, piece, shapeId, x, y, z, goal, copies);
  if (!why.empty()) {
    return why;
  }
  const unsigned int id = shapeId + 1;
  const std::vector<TrayCell> cells = stampCells(tray, piece, shapeId, x, y, z, goal, copies);
  // one copy moves; another copy goes beside those placed
  if (copies <= 1) {
    clearStamp(tray, shapeId, goal);
  }
  for (const TrayCell& c : cells) {
    const unsigned int i = tray->getIndex(asUnsigned(c[0]), asUnsigned(c[1]), asUnsigned(c[2]));
    if (goal) {
      tray->setGoalPiece(i, id);
    } else {
      tray->setColor(i, id);
    }
  }
  return std::string();
}

std::vector<unsigned int> dropChangedStamps(Puzzle& puzzle, unsigned int shapeId) {
  std::vector<unsigned int> trays;
  if (shapeId >= puzzle.getNumberOfShapes() || shapeId >= 63) {
    return trays;
  }
  const Voxel& piece = puzzle.getShape(shapeId);
  if (isStartGoalShape(&piece) || isHiddenSlidingShape(&piece)) {
    return trays;
  }
  for (unsigned int t = 0; t < puzzle.getNumberOfShapes(); t++) {
    Voxel& tray = puzzle.getShape(t);
    if (!isStartGoalShape(&tray)) {
      continue;
    }
    // the marks stay while they are still whole copies, no more than the
    // problem has (a piece cut in two would otherwise count twice the copies)
    const unsigned int copies = pieceCopies(puzzle, t, shapeId);
    bool dropped = false;
    for (int goal = 0; goal < 2; goal++) {
      const auto split = stampCopies(tray, piece, shapeId, goal != 0);
      if (!split || split->size() > copies) {
        dropped = clearStamp(&tray, shapeId, goal != 0) || dropped;
      }
    }
    if (dropped) {
      trays.push_back(t);
    }
  }
  return trays;
}

bool stampIsAt(const Voxel* tray, const Voxel* piece, unsigned int shapeId, int x, int y, int z,
               bool goal) {
  if (tray == nullptr || piece == nullptr) {
    return false;
  }
  std::vector<TrayCell> there = stampCells(tray, piece, shapeId, x, y, z, goal);
  std::sort(there.begin(), there.end());
  if (there.empty()) {
    return false;
  }
  std::vector<TrayCell> now = stampedCells(*tray, shapeId + 1, goal);
  const auto copies = splitCopies(now, *piece);
  if (!copies) {
    // marks that are not whole pieces: only all of them together
    std::sort(now.begin(), now.end());
    return now == there;
  }
  return std::any_of(copies->begin(), copies->end(), [&there](std::vector<TrayCell> c) {
    std::sort(c.begin(), c.end());
    return c == there;
  });
}

std::optional<std::vector<std::vector<TrayCell>>> stampCopies(const Voxel& tray,
                                                             const Voxel& piece,
                                                             unsigned int shapeId, bool goal) {
  return splitCopies(stampedCells(tray, shapeId + 1, goal), piece);
}

bool clearCopyAt(Voxel* tray, const Voxel* piece, unsigned int shapeId, int x, int y, int z,
                 bool goal) {
  if (tray == nullptr || piece == nullptr || stampAt(tray, x, y, z, goal) != shapeId) {
    return false;
  }
  const auto copies = stampCopies(*tray, *piece, shapeId, goal);
  if (!copies) {
    return clearStamp(tray, shapeId, goal);
  }
  for (const std::vector<TrayCell>& copy : *copies) {
    if (std::find(copy.begin(), copy.end(), TrayCell{x, y, z}) == copy.end()) {
      continue;
    }
    for (const TrayCell& c : copy) {
      const unsigned int i = tray->getIndex(asUnsigned(c[0]), asUnsigned(c[1]), asUnsigned(c[2]));
      if (goal) {
        tray->setGoalPiece(i, 0);
      } else {
        tray->setColor(i, 0);
      }
    }
    return true;
  }
  return false;
}

bool clearStamp(Voxel* tray, unsigned int shapeId, bool goal) {
  if (tray == nullptr) {
    return false;
  }
  const unsigned int id = shapeId + 1;
  bool had = false;
  for (unsigned int i = 0; i < tray->getXYZ(); i++) {
    if (stampOf(*tray, i, goal) == id) {
      had = true;
      if (goal) {
        tray->setGoalPiece(i, 0);
      } else {
        tray->setColor(i, 0);
      }
    }
  }
  return had;
}

unsigned int stampAt(const Voxel* tray, int x, int y, int z, bool goal) {
  if (tray == nullptr || !inTray(*tray, x, y, z)) {
    return static_cast<unsigned int>(-1);
  }
  const unsigned int id =
      stampOf(*tray, tray->getIndex(asUnsigned(x), asUnsigned(y), asUnsigned(z)), goal);
  return id == 0 ? static_cast<unsigned int>(-1) : id - 1;
}

std::string startGoalError(const Puzzle& puzzle, unsigned int trayShape) {
  if (trayShape >= puzzle.getNumberOfShapes() ||
      !isStartGoalShape(&puzzle.getShape(trayShape))) {
    return "Choose a start/goal shape.";
  }
  const Voxel& tray = puzzle.getShape(trayShape);
  bool both = false;
  for (unsigned int s = 0; s < puzzle.getNumberOfShapes() && s < 63; s++) {
    const Voxel& piece = puzzle.getShape(s);
    if (isStartGoalShape(&piece) || isHiddenSlidingShape(&piece)) {
      continue;
    }
    const auto starts = stampCopies(tray, piece, s, false);
    const auto goals = stampCopies(tray, piece, s, true);
    const std::string name = shapeLabel(puzzle, s);
    if (!starts) {
      return "The start of " + name + " is not the whole piece. Place it again.";
    }
    if (!goals) {
      return "The goal of " + name + " is not the whole piece. Place it again.";
    }
    const unsigned int copies = pieceCopies(puzzle, trayShape, s);
    for (int goal = 0; goal < 2; goal++) {
      const std::size_t n = (goal != 0 ? goals : starts)->size();
      if (n > copies) {
        return name + " has " + std::to_string(n) + (goal != 0 ? " goals" : " starts") +
               " but the puzzle has " + std::to_string(copies) +
               (copies == 1 ? " copy of it." : " copies of it.");
      }
    }
    if (starts->size() == copies && goals->size() == copies) {
      both = true;
    }
  }
  if (!both) {
    return "Place at least one piece at its Start and at its Goal (every copy of it, when the "
           "puzzle has several).";
  }
  return std::string();
}

const Problem* trayProblem(const Puzzle& puzzle, unsigned int trayShape) {
  for (unsigned int p = 0; p < puzzle.getNumberOfProblems(); p++) {
    const Problem& pr = puzzle.getProblem(p);
    if (pr.resultValid() && pr.getResultId() == trayShape) {
      return &pr;
    }
  }
  return nullptr;
}

unsigned int pieceCopies(const Puzzle& puzzle, unsigned int trayShape, unsigned int shapeId) {
  const Problem* pr = trayProblem(puzzle, trayShape);
  return pr == nullptr ? 1U : std::max(1U, pr->getShapeMaximum(shapeId));
}

unsigned int stampedCopies(const Puzzle& puzzle, const Problem& problem, unsigned int shapeId) {
  if (!problem.resultValid() || shapeId >= puzzle.getNumberOfShapes() || shapeId >= 63) {
    return 0;
  }
  const Voxel& tray = problem.resultShape(puzzle);
  if (!isStartGoalShape(&tray)) {
    return 0;
  }
  const Voxel& piece = puzzle.getShape(shapeId);
  return std::max(placedCopies(tray, piece, shapeId + 1, false),
                  placedCopies(tray, piece, shapeId + 1, true));
}

namespace {

// The lock colours (bit id - 1) of the pieces with starts for fewer than
// all their copies on the problem's start/goal shape.
std::uint64_t neutralFitColors(const Puzzle& puzzle, const Problem& problem) {
  if (!problem.resultValid() || !isStartGoalShape(&problem.resultShape(puzzle))) {
    return 0;
  }
  const Voxel& tray = problem.resultShape(puzzle);
  std::uint64_t colors = 0;
  for (unsigned int s = 0; s < puzzle.getNumberOfShapes() && s < 63; s++) {
    const Voxel& piece = puzzle.getShape(s);
    if (isStartGoalShape(&piece) || isHiddenSlidingShape(&piece)) {
      continue;
    }
    const unsigned int starts = placedCopies(tray, piece, s + 1, false);
    if (starts > 0 && starts < problem.getShapeMaximum(s)) {
      colors |= std::uint64_t{1} << s;
    }
  }
  return colors;
}

}  // namespace

bool startFillsStamps(const Puzzle& puzzle, const Problem& problem, const Assembly& assembly) {
  const std::uint64_t colors = problem.getNeutralFitColors();
  if (colors == 0 || !problem.resultValid()) {
    return true;
  }
  const Voxel& tray = problem.resultShape(puzzle);
  unsigned int pc = 0;
  for (unsigned int part = 0; part < problem.getNumberOfParts(); part++) {
    const unsigned int shapeId = problem.getShapeIdOfPart(part);
    const unsigned int count = problem.getPartMaximum(part);
    if (shapeId >= 63 || ((colors >> shapeId) & 1U) == 0) {
      pc += count;
      continue;
    }
    // As many copies wholly on the piece's starts as it has starts fill
    // them: the starts hold that many copies' cells.
    const Voxel& piece = puzzle.getShape(shapeId);
    const unsigned int id = shapeId + 1;
    const unsigned int want = placedCopies(tray, piece, id, false);
    unsigned int on = 0;
    for (unsigned int j = 0; j < count; j++, pc++) {
      if (pc >= assembly.placementCount() || !assembly.isPlaced(pc)) {
        continue;
      }
      const std::vector<TrayCell> cells = placedCells(piece, assembly, pc);
      const bool all = !cells.empty() && std::all_of(cells.begin(), cells.end(),
                                                     [&tray, id](const TrayCell& c) {
                                                       return inTray(tray, c[0], c[1], c[2]) &&
                                                              tray.getColor(asUnsigned(c[0]),
                                                                            asUnsigned(c[1]),
                                                                            asUnsigned(c[2])) ==
                                                                  id;
                                                     });
      on += all ? 1 : 0;
    }
    if (on < want) {
      return false;
    }
  }
  return true;
}

void refreshStartLocks(Puzzle& puzzle, Problem& problem) {
  problem.setNeutralFitColors(neutralFitColors(puzzle, problem));
  if (!problem.resultValid()) {
    return;
  }
  const unsigned int trayId = problem.getResultId();
  if (!isStartGoalShape(&puzzle.getShape(trayId))) {
    return;
  }
  // The assembler skips colour checks when the palette is empty. Start
  // stamps are cell colours, so the palette has to cover them or the start
  // cell does not constrain the placement.
  unsigned int maxStamp = 0;
  {
    const Voxel& tray = puzzle.getShape(trayId);
    for (unsigned int i = 0; i < tray.getXYZ(); i++) {
      maxStamp = std::max({maxStamp, tray.getColor(i), tray.getGoalPiece(i)});
    }
  }
  if (maxStamp != 0) {
    findOrAddColor(puzzle, maxStamp);
  }

  const Voxel& tray = puzzle.getShape(trayId);
  for (unsigned int s = 0; s < puzzle.getNumberOfShapes(); s++) {
    if (isStartGoalShape(&puzzle.getShape(s)) || isHiddenSlidingShape(&puzzle.getShape(s))) {
      continue;
    }
    const unsigned int id = s + 1;
    bool used = false;
    for (unsigned int i = 0; i < tray.getXYZ() && !used; i++) {
      if (tray.getColor(i) == id) {
        used = true;
      }
    }
    paintPieceLock(puzzle.getShape(s), used ? id : 0);
  }
}

bool shapeIsRequired(const Puzzle& puzzle, const Problem& problem, unsigned int shapeId) {
  if (!problem.resultValid()) {
    return false;
  }
  const Voxel& tray = problem.resultShape(puzzle);
  if (!isStartGoalShape(&tray)) {
    return false;
  }
  const unsigned int id = shapeId + 1;
  for (unsigned int i = 0; i < tray.getXYZ(); i++) {
    if (tray.getColor(i) == id || tray.getGoalPiece(i) == id) {
      return true;
    }
  }
  return false;
}

void syncSlidingProblems(Puzzle& puzzle) {
  if (!isSliding(puzzle)) {
    return;
  }
  renumberStartGoalShapes(puzzle);

  // drop the problems that are not tied to a start/goal shape
  for (int p = asInt(puzzle.getNumberOfProblems()) - 1; p >= 0; p--) {
    const Problem& pr = puzzle.getProblem(asUnsigned(p));
    if (!pr.resultValid() || !isStartGoalShape(&puzzle.getShape(pr.getResultId()))) {
      puzzle.removeProblem(asUnsigned(p));
    }
  }

  for (unsigned int s = 0; s < puzzle.getNumberOfShapes(); s++) {
    if (!isStartGoalShape(&puzzle.getShape(s))) {
      continue;
    }
    unsigned int found = puzzle.getNumberOfProblems();
    for (unsigned int p = 0; p < puzzle.getNumberOfProblems(); p++) {
      const Problem& pr = puzzle.getProblem(p);
      if (pr.resultValid() && pr.getResultId() == s) {
        found = p;
        break;
      }
    }
    if (found == puzzle.getNumberOfProblems()) {
      found = puzzle.addProblem();
      Problem& pr = puzzle.getProblem(found);
      pr.setResultId(puzzle, s);
    } else if (puzzle.getProblem(found).getResultId() != s) {
      puzzle.getProblem(found).setResultId(puzzle, s);
    }

    Problem& pr = puzzle.getProblem(found);
    const Voxel& tray = puzzle.getShape(s);
    // the problem is named as its start/goal shape shows, and no start/goal
    // shape is a piece of it
    pr.setName(displayName(&tray));
    for (unsigned int i = 0; i < puzzle.getNumberOfShapes(); i++) {
      if (i != s && isStartGoalShape(&puzzle.getShape(i)) && pr.getShapeMaximum(i) > 0) {
        pr.setShapeMaximum(puzzle, i, 0);
      }
    }
    for (unsigned int i = 0; i < puzzle.getNumberOfShapes(); i++) {
      if (isStartGoalShape(&puzzle.getShape(i)) || isHiddenSlidingShape(&puzzle.getShape(i))) {
        continue;
      }
      bool used = false;
      const unsigned int id = i + 1;
      for (unsigned int c = 0; c < tray.getXYZ() && !used; c++) {
        if (tray.getColor(c) == id || tray.getGoalPiece(c) == id) {
          used = true;
        }
      }
      if (used) {
        // a copy for each start and each goal
        const unsigned int least = std::max(1U, stampedCopies(puzzle, pr, i));
        if (pr.getShapeMaximum(i) < least) {
          pr.setShapeMaximum(puzzle, i, least);
        }
        if (pr.getShapeMinimum(i) < least) {
          pr.setShapeMinimum(puzzle, i, least);
        }
        if (pr.getShapeMaximum(i) < pr.getShapeMinimum(i)) {
          pr.setShapeMaximum(puzzle, i, pr.getShapeMinimum(i));
        }
      }
    }
    refreshStartLocks(puzzle, pr);
  }
}

unsigned int floorCells(const Voxel& tray) {
  unsigned int n = 0;
  for (unsigned int i = 0; i < tray.getXYZ(); i++) {
    if (tray.getState(i) != Voxel::kEmpty) {
      n++;
    }
  }
  return n;
}

unsigned int pieceColor(const Puzzle& puzzle, unsigned int shapeId) {
  if (shapeId >= puzzle.getNumberOfShapes()) {
    return 0;
  }
  const Voxel& piece = puzzle.getShape(shapeId);
  for (unsigned int i = 0; i < piece.getXYZ(); i++) {
    if (piece.getState(i) != Voxel::kEmpty && piece.getColor(i) != 0) {
      return piece.getColor(i);
    }
  }
  return 0;
}

// The goal-map model below (a separate goal shape painted in piece colours)
// came before start/goal shapes. The editor no longer uses it, but older
// files can hold such goals and the solver still reads them; the tests use
// these to build such puzzles.

void ensureSetup(Puzzle& puzzle, unsigned int width, unsigned int height) {
  if (!isSliding(puzzle)) {
    return;
  }

  if (puzzle.getNumberOfShapes() == 0) {
    const unsigned int trayId = puzzle.addShape(width, height, 1);
    fillTray(puzzle.getShape(trayId));
    puzzle.getShape(trayId).setName("Tray");

    const unsigned int goalId = puzzle.addShape(width, height, 1);
    fillTray(puzzle.getShape(goalId));
    puzzle.getShape(goalId).setName(kGoalShapeName);

    const unsigned int prob = puzzle.addProblem();
    puzzle.getProblem(prob).setResultId(puzzle, trayId);
    puzzle.getProblem(prob).setGoalId(puzzle, goalId);
    return;
  }

  // every problem gets a goal map
  for (unsigned int pi = 0; pi < puzzle.getNumberOfProblems(); pi++) {
    if (puzzle.getProblem(pi).goalValid()) {
      continue;
    }

    // an unused shape named as a goal, or a new one
    unsigned int goalId = 0xFFFFFFFF;
    for (unsigned int s = 0; s < puzzle.getNumberOfShapes(); s++) {
      if (puzzle.getShape(s).getName() == kGoalShapeName) {
        const Problem& pr = puzzle.getProblem(pi);
        bool used = pr.resultValid() && pr.getResultId() == s;
        for (unsigned int p = 0; p < pr.getNumberOfParts() && !used; p++) {
          if (pr.getShapeIdOfPart(p) == s) {
            used = true;
          }
        }
        if (!used) {
          goalId = s;
          break;
        }
      }
    }
    if (goalId == 0xFFFFFFFF) {
      unsigned int w = 6;
      unsigned int h = 6;
      const Problem& pr = puzzle.getProblem(pi);
      if (pr.resultValid()) {
        w = pr.resultShape(puzzle).getX();
        h = pr.resultShape(puzzle).getY();
      }
      goalId = puzzle.addShape(w, h, 1);
      fillTray(puzzle.getShape(goalId));
      puzzle.getShape(goalId).setName(kGoalShapeName);
    }
    puzzle.getProblem(pi).setGoalId(puzzle, goalId);
  }
}

bool hasStart(const Puzzle& puzzle, const Problem& problem, unsigned int shapeId) {
  const unsigned int col = pieceColor(puzzle, shapeId);
  if (col == 0 || !problem.resultValid()) {
    return false;
  }
  const Voxel& tray = problem.resultShape(puzzle);
  for (unsigned int i = 0; i < tray.getXYZ(); i++) {
    if (tray.getColor(i) == col) {
      return true;
    }
  }
  return false;
}

bool hasGoal(const Puzzle& puzzle, const Problem& problem, unsigned int shapeId) {
  if (!problem.goalValid()) {
    return false;
  }
  const unsigned int col = piecePartIndex(problem, shapeId);
  // the goal takes the part's colour even when the piece is neutral
  if (puzzle.colorNumber() < col) {
    return false;
  }
  const Voxel& goal = puzzle.getShape(problem.getGoalId());
  for (unsigned int i = 0; i < goal.getXYZ(); i++) {
    if (goal.getColor(i) == col) {
      return true;
    }
  }
  // or the piece's painted colour, when it has one
  const unsigned int pcol = pieceColor(puzzle, shapeId);
  if (pcol == 0) {
    return false;
  }
  for (unsigned int i = 0; i < goal.getXYZ(); i++) {
    if (goal.getColor(i) == pcol) {
      return true;
    }
  }
  return false;
}

bool placeStart(Puzzle& puzzle, Problem& problem, unsigned int shapeId, int ax, int ay) {
  if (!problem.resultValid()) {
    return false;
  }
  unsigned int col = pieceColor(puzzle, shapeId);
  if (col == 0) {
    col = findOrAddColor(puzzle, piecePartIndex(problem, shapeId));
  }

  Voxel& tray = puzzle.getShape(problem.getResultId());
  Voxel& piece = puzzle.getShape(shapeId);
  if (!footprintFits(tray, piece, ax, ay, col)) {
    return false;
  }

  eraseColor(tray, col);
  paintPieceColor(piece, col);
  paintFootprint(tray, piece, ax, ay, col);
  syncMaxHoles(puzzle, problem);
  return true;
}

void clearStart(Puzzle& puzzle, Problem& problem, unsigned int shapeId) {
  if (!problem.resultValid()) {
    return;
  }
  const unsigned int col = pieceColor(puzzle, shapeId);
  if (col == 0) {
    return;
  }
  eraseColor(puzzle.getShape(problem.getResultId()), col);
  paintPieceNeutral(puzzle.getShape(shapeId));
  // the goals stay: they may refer to the part's colour
  syncMaxHoles(puzzle, problem);
}

bool placeGoal(Puzzle& puzzle, Problem& problem, unsigned int shapeId, int ax, int ay) {
  if (!problem.goalValid() || !problem.resultValid()) {
    return false;
  }
  unsigned int col = pieceColor(puzzle, shapeId);
  if (col == 0) {
    col = findOrAddColor(puzzle, piecePartIndex(problem, shapeId));
  }

  const Voxel& tray = puzzle.getShape(problem.getResultId());
  const Voxel& piece = puzzle.getShape(shapeId);
  if (!footprintFits(tray, piece, ax, ay, 0)) {
    return false;
  }

  Voxel& goal = puzzle.getShape(problem.getGoalId());
  eraseColor(goal, col);
  paintFootprint(goal, piece, ax, ay, col);
  return true;
}

void clearGoal(Puzzle& puzzle, Problem& problem, unsigned int shapeId) {
  if (!problem.goalValid()) {
    return;
  }
  const unsigned int col = pieceColor(puzzle, shapeId);
  const unsigned int partCol = piecePartIndex(problem, shapeId);
  Voxel& goal = puzzle.getShape(problem.getGoalId());
  if (col != 0) {
    eraseColor(goal, col);
  }
  eraseColor(goal, partCol);
}

bool clearStartAt(Puzzle& puzzle, Problem& problem, int x, int y) {
  if (!problem.resultValid()) {
    return false;
  }
  const Voxel& tray = puzzle.getShape(problem.getResultId());
  if (!isFloor(tray, x, y)) {
    return false;
  }
  const unsigned int col = tray.getColor(asUnsigned(x), asUnsigned(y), 0);
  if (col == 0) {
    return false;
  }
  for (unsigned int p = 0; p < problem.getNumberOfParts(); p++) {
    const unsigned int sid = problem.getShapeIdOfPart(p);
    if (pieceColor(puzzle, sid) == col) {
      clearStart(puzzle, problem, sid);
      return true;
    }
  }
  return false;
}

bool toggleGoal(Puzzle& puzzle, Problem& problem, unsigned int shapeId, int ax, int ay) {
  if (!problem.goalValid()) {
    return false;
  }
  unsigned int col = pieceColor(puzzle, shapeId);
  if (col == 0) {
    col = piecePartIndex(problem, shapeId);
  }

  const Voxel& piece = puzzle.getShape(shapeId);
  const Voxel& goal = puzzle.getShape(problem.getGoalId());
  int curAx = 0;
  int curAy = 0;
  if (findColorAnchor(goal, piece, col, &curAx, &curAy) && curAx == ax && curAy == ay) {
    clearGoal(puzzle, problem, shapeId);
    return true;
  }
  return placeGoal(puzzle, problem, shapeId, ax, ay);
}

void syncMaxHoles(const Puzzle& puzzle, Problem& problem) {
  if (!problem.resultValid()) {
    return;
  }
  const unsigned int floor = floorCells(problem.resultShape(puzzle));
  unsigned int volume = 0;
  for (unsigned int p = 0; p < problem.getNumberOfParts(); p++) {
    const Voxel& sh = problem.partShape(puzzle, p);
    const unsigned int cnt = problem.getPartMaximum(p);
    unsigned int cells = 0;
    for (unsigned int i = 0; i < sh.getXYZ(); i++) {
      if (sh.getState(i) != Voxel::kEmpty) {
        cells++;
      }
    }
    volume += cells * cnt;
  }
  if (volume > floor) {
    problem.setMaxHoles(0);
  } else {
    problem.setMaxHoles(floor - volume);
  }
}

std::string stampOverflowMessage(const Puzzle& puzzle, const Problem& problem) {
  if (!problem.resultValid()) {
    return std::string();
  }
  const Voxel& tray = problem.resultShape(puzzle);
  if (!isStartGoalShape(&tray)) {
    return std::string();
  }

  std::ostringstream msg;
  for (unsigned int s = 0; s < puzzle.getNumberOfShapes(); s++) {
    const Voxel& piece = puzzle.getShape(s);
    if (isStartGoalShape(&piece) || isHiddenSlidingShape(&piece)) {
      continue;
    }
    unsigned int voxels = 0;
    for (unsigned int i = 0; i < piece.getXYZ(); i++) {
      if (piece.getState(i) != Voxel::kEmpty) {
        voxels++;
      }
    }
    const unsigned int id = s + 1;
    // every copy may have a start and a goal
    const unsigned int copies = std::max(1U, problem.getShapeMaximum(s));
    voxels *= copies;
    unsigned int starts = 0;
    unsigned int goals = 0;
    for (unsigned int i = 0; i < tray.getXYZ(); i++) {
      if (tray.getColor(i) == id) {
        starts++;
      }
      if (tray.getGoalPiece(i) == id) {
        goals++;
      }
    }
    const std::string has = copies == 1 ? " cells but the piece only has "
                                        : " cells but its " + std::to_string(copies) +
                                              " copies only have ";
    if (starts > voxels) {
      msg << "S" << id << " has " << starts << " start" << has << voxels
          << (voxels == 1 ? " voxel.\n" : " voxels.\n");
    }
    if (goals > voxels) {
      msg << "S" << id << " has " << goals << " goal" << has << voxels
          << (voxels == 1 ? " voxel.\n" : " voxels.\n");
    }
  }
  if (msg.str().empty()) {
    return std::string();
  }
  msg << "A piece cannot cover more cells than it contains.";
  return msg.str();
}

bool needsPreparing(const Puzzle& puzzle, const Problem& problem) {
  if (!problem.resultValid()) {
    return false;
  }
  // the holes (syncMaxHoles)
  const unsigned int floor = floorCells(problem.resultShape(puzzle));
  unsigned int volume = 0;
  for (unsigned int p = 0; p < problem.getNumberOfParts(); p++) {
    const Voxel& sh = problem.partShape(puzzle, p);
    unsigned int cells = 0;
    for (unsigned int i = 0; i < sh.getXYZ(); i++) {
      if (sh.getState(i) != Voxel::kEmpty) {
        cells++;
      }
    }
    volume += cells * problem.getPartMaximum(p);
  }
  const unsigned int holes = volume > floor ? 0 : floor - volume;
  if (!problem.maxHolesDefined() || problem.getMaxHoles() != holes) {
    return true;
  }
  // the start locks (refreshStartLocks)
  if (problem.getNeutralFitColors() != neutralFitColors(puzzle, problem)) {
    return true;
  }
  const Voxel& tray = problem.resultShape(puzzle);
  if (!isStartGoalShape(&tray)) {
    return false;
  }
  unsigned int maxStamp = 0;
  for (unsigned int i = 0; i < tray.getXYZ(); i++) {
    maxStamp = std::max({maxStamp, tray.getColor(i), tray.getGoalPiece(i)});
  }
  if (puzzle.colorNumber() < maxStamp) {
    return true;
  }
  for (unsigned int s = 0; s < puzzle.getNumberOfShapes(); s++) {
    const Voxel& piece = puzzle.getShape(s);
    if (isStartGoalShape(&piece) || isHiddenSlidingShape(&piece)) {
      continue;
    }
    const unsigned int id = s + 1;
    bool used = false;
    for (unsigned int i = 0; i < tray.getXYZ() && !used; i++) {
      used = tray.getColor(i) == id;
    }
    const unsigned int lock = used ? id : 0;
    for (unsigned int i = 0; i < piece.getXYZ(); i++) {
      if (piece.getColor(i) != (piece.getState(i) == Voxel::kEmpty ? 0 : lock)) {
        return true;
      }
    }
  }
  return false;
}

bool prepareForSolving(Puzzle& puzzle, Problem& problem) {
  // what is there before, to tell whether anything changed
  const bool hadMaxHoles = problem.maxHolesDefined();
  const unsigned int maxHoles = hadMaxHoles ? problem.getMaxHoles() : 0;
  const std::uint64_t neutral = problem.getNeutralFitColors();
  const unsigned int colours = puzzle.colorNumber();
  std::vector<std::unique_ptr<Voxel>> shapes;
  shapes.reserve(puzzle.getNumberOfShapes());
  for (unsigned int s = 0; s < puzzle.getNumberOfShapes(); s++) {
    shapes.push_back(puzzle.getShape(s).clone());
  }

  syncMaxHoles(puzzle, problem);
  refreshStartLocks(puzzle, problem);

  bool changed = colours != puzzle.colorNumber() || neutral != problem.getNeutralFitColors() ||
                 hadMaxHoles != problem.maxHolesDefined() ||
                 (hadMaxHoles && maxHoles != problem.getMaxHoles());
  for (unsigned int s = 0; s < puzzle.getNumberOfShapes() && !changed; s++) {
    changed = !(*shapes[s] == puzzle.getShape(s));
  }
  return changed;
}

}  // namespace bt::sliding

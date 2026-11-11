// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/sliding.cpp: the start/goal shapes, stamps, start
// locks, the older goal-map model and the checks (the first half of the
// file; the search is bt/solve/sliding_search.cpp).
#include "shapelogic/model/sliding.h"

#include "shapelogic/base/assert.h"
#include "shapelogic/base/int_cast.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/model/assembly.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/model/slide_marks.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <map>
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

// The highest start or goal mark a cell holds (a byte; 0 is none).
constexpr unsigned int kMaxMark = 255;

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
std::vector<TrayCell> stampedCells(const Voxel& tray, const SlideMarks& marks, unsigned int id,
                                   bool goal) {
  std::vector<TrayCell> cells;
  for (unsigned int z = 0; z < tray.getZ(); z++) {
    for (unsigned int y = 0; y < tray.getY(); y++) {
      for (unsigned int x = 0; x < tray.getX(); x++) {
        if (marks.get(tray.getIndex(x, y, z), goal) == id) {
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

// A problem with no marks reads these.
const SlideMarks kNoMarks;

// True when the problem's result is the start/goal shape trayShape.
bool onTray(const Problem& pr, unsigned int trayShape) {
  return pr.resultValid() && pr.getResultId() == trayShape;
}

// True when shape s is a piece: no start/goal shape and no hidden one.
bool isPiece(const Puzzle& puzzle, unsigned int s) {
  return s < puzzle.getNumberOfShapes() && !isStartGoalShape(&puzzle.getShape(s)) &&
         !isHiddenSlidingShape(&puzzle.getShape(s));
}

// Every mark m of the marks becomes to(m).
template <class To>
void remapMarks(SlideMarks& marks, To to) {
  for (int goal = 0; goal < 2; goal++) {
    for (std::size_t i = marks.size(goal != 0); i-- > 0;) {
      const unsigned int m = marks.get(i, goal != 0);
      if (m != 0) {
        marks.set(i, goal != 0, to(m));
      }
    }
  }
}

// True when a cell has the mark, as a start or a goal.
bool hasMark(const SlideMarks& marks, unsigned int id) {
  for (std::size_t i = 0; i < marks.size(); i++) {
    if (marks.start(i) == id || marks.goal(i) == id) {
      return true;
    }
  }
  return false;
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

std::string defaultStartGoalName(int n) { return "Board " + std::to_string(n); }

std::string defaultStartGoalProblemName(int n) { return defaultStartGoalName(n) + " - Start / Goal"; }

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

std::string problemName(const Voxel* v) {
  const std::string user = startGoalUserName(v);
  return user.empty() ? defaultStartGoalProblemName(startGoalNumber(v)) : user;
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
  for (unsigned int p = 0; p < puzzle.getNumberOfProblems(); p++) {
    remapMarks(puzzle.getProblem(p).editSlideMarks(), [gone](unsigned int m) {
      return m == gone ? 0U : m > gone ? m - 1 : m;
    });
  }
}

std::vector<unsigned int> noteShapeSwap(Puzzle& puzzle, unsigned int a, unsigned int b) {
  std::vector<unsigned int> lost;
  if (a == b) {
    return lost;
  }
  const unsigned int ca = a + 1;
  const unsigned int cb = b + 1;
  // A mark is a byte: a shape past the 255th place can hold none
  // (toggleCellMark and stampFits refuse it), so a mark that would follow
  // its shape there goes instead.
  const auto to = [](unsigned int id) { return id > kMaxMark ? 0U : id; };
  for (unsigned int p = 0; p < puzzle.getNumberOfProblems(); p++) {
    Problem& pr = puzzle.getProblem(p);
    bool dropped = false;
    remapMarks(pr.editSlideMarks(), [&](unsigned int m) {
      if (m != ca && m != cb) {
        return m;
      }
      const unsigned int now = to(m == ca ? cb : ca);
      dropped = dropped || now == 0;
      return now;
    });
    if (dropped && pr.resultValid() &&
        std::find(lost.begin(), lost.end(), pr.getResultId()) == lost.end()) {
      lost.push_back(pr.getResultId());
    }
  }
  // the boards as they are numbered once a and b have swapped
  for (unsigned int& s : lost) {
    s = s == a ? b : s == b ? a : s;
  }
  std::sort(lost.begin(), lost.end());
  return lost;
}

bool toggleCellMark(const Voxel* tray, SlideMarks* marks, int x, int y, int z,
                    unsigned int shapeId, bool goal) {
  if (tray == nullptr || marks == nullptr || !inTray(*tray, x, y, z) || !tray->validCoordinate(x, y, z)) {
    return false;
  }
  const unsigned int i = tray->getIndex(asUnsigned(x), asUnsigned(y), asUnsigned(z));
  if (tray->getState(i) == Voxel::kEmpty) {
    return false;
  }
  const unsigned int id = shapeId + 1;
  if (id > kMaxMark) {
    return false;
  }
  marks->set(i, goal, marks->get(i, goal) == id ? 0 : id);
  return true;
}

namespace {

// Why cells marked for shape id would not do, or empty when they would. A
// cell of the piece's own marks is free only when the stamp replaces them
// (a piece of one copy moved).
std::string cellsFit(const Voxel& tray, const SlideMarks& marks,
                     const std::vector<TrayCell>& cells, unsigned int id, bool goal,
                     bool replaces) {
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
    const unsigned int other = marks.get(i, goal);
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
unsigned int placedCopies(const Voxel& tray, const SlideMarks& marks, const Voxel& piece,
                          unsigned int id, bool goal) {
  std::vector<TrayCell> marked = stampedCells(tray, marks, id, goal);
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

std::vector<TrayCell> stampCells(const Voxel* tray, const SlideMarks* marks, const Voxel* piece,
                                 unsigned int shapeId, int x, int y, int z, bool goal,
                                 unsigned int copies) {
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
    if (tray == nullptr ||
        cellsFit(*tray, marks != nullptr ? *marks : kNoMarks, placed, shapeId + 1, goal,
                 copies <= 1)
            .empty()) {
      return placed;
    }
    if (first.empty()) {
      first = std::move(placed);
    }
  }
  return first;
}

std::string stampFits(const Voxel* tray, const SlideMarks* marks, const Voxel* piece,
                      unsigned int shapeId, int x, int y, int z, bool goal,
                      unsigned int copies) {
  if (tray == nullptr || marks == nullptr || piece == nullptr) {
    return "No piece is chosen.";
  }
  const unsigned int id = shapeId + 1;
  if (id > kMaxMark) {
    return "Only the first " + std::to_string(kMaxMark) + " shapes can be placed.";
  }
  const std::vector<TrayCell> cells =
      stampCells(tray, marks, piece, shapeId, x, y, z, goal, copies);
  if (cells.empty()) {
    return pieceCells(*piece).empty() ? "The piece has no cells."
                                      : "The piece has no cells on this layer there.";
  }
  if (copies > 1 && placedCopies(*tray, *marks, *piece, id, goal) >= copies) {
    return "All " + std::to_string(copies) + " copies of the piece have a " +
           (goal ? "goal" : "start") + " already: take one off first (right-click it).";
  }
  return cellsFit(*tray, *marks, cells, id, goal, copies <= 1);
}

std::string stampPiece(const Voxel* tray, SlideMarks* marks, const Voxel* piece,
                       unsigned int shapeId, int x, int y, int z, bool goal,
                       unsigned int copies) {
  std::string why = stampFits(tray, marks, piece, shapeId, x, y, z, goal, copies);
  if (!why.empty()) {
    return why;
  }
  const unsigned int id = shapeId + 1;
  const std::vector<TrayCell> cells =
      stampCells(tray, marks, piece, shapeId, x, y, z, goal, copies);
  // one copy moves; another copy goes beside those placed
  if (copies <= 1) {
    clearStamp(marks, shapeId, goal);
  }
  for (const TrayCell& c : cells) {
    marks->set(tray->getIndex(asUnsigned(c[0]), asUnsigned(c[1]), asUnsigned(c[2])), goal, id);
  }
  return std::string();
}

std::vector<unsigned int> dropChangedStamps(Puzzle& puzzle, unsigned int shapeId) {
  std::vector<unsigned int> trays;
  if (shapeId >= puzzle.getNumberOfShapes() || shapeId >= kMaxMark) {
    return trays;
  }
  if (!isPiece(puzzle, shapeId)) {
    return trays;
  }
  const Voxel& piece = puzzle.getShape(shapeId);
  for (unsigned int p = 0; p < puzzle.getNumberOfProblems(); p++) {
    Problem& pr = puzzle.getProblem(p);
    if (!usesStartMarks(puzzle, pr)) {
      continue;
    }
    const unsigned int t = pr.getResultId();
    const Voxel& tray = puzzle.getShape(t);
    // the marks stay while they are still whole copies, no more than the
    // problem has (a piece cut in two would otherwise count twice the copies)
    const unsigned int copies = pieceCopies(puzzle, t, shapeId, &pr);
    bool dropped = false;
    for (int goal = 0; goal < 2; goal++) {
      const auto split = stampCopies(tray, pr.slideMarks(), piece, shapeId, goal != 0);
      if (!split || split->size() > copies) {
        dropped = clearStamp(&pr.editSlideMarks(), shapeId, goal != 0) || dropped;
      }
    }
    if (dropped && std::find(trays.begin(), trays.end(), t) == trays.end()) {
      trays.push_back(t);
    }
  }
  std::sort(trays.begin(), trays.end());
  return trays;
}

bool stampIsAt(const Voxel* tray, const SlideMarks* marks, const Voxel* piece,
               unsigned int shapeId, int x, int y, int z, bool goal) {
  if (tray == nullptr || marks == nullptr || piece == nullptr) {
    return false;
  }
  std::vector<TrayCell> there = stampCells(tray, marks, piece, shapeId, x, y, z, goal);
  std::sort(there.begin(), there.end());
  if (there.empty()) {
    return false;
  }
  std::vector<TrayCell> now = stampedCells(*tray, *marks, shapeId + 1, goal);
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
                                                             const SlideMarks& marks,
                                                             const Voxel& piece,
                                                             unsigned int shapeId, bool goal) {
  return splitCopies(stampedCells(tray, marks, shapeId + 1, goal), piece);
}

bool clearCopyAt(const Voxel* tray, SlideMarks* marks, const Voxel* piece, unsigned int shapeId,
                 int x, int y, int z, bool goal) {
  if (tray == nullptr || marks == nullptr || piece == nullptr ||
      stampAt(tray, marks, x, y, z, goal) != shapeId) {
    return false;
  }
  const auto copies = stampCopies(*tray, *marks, *piece, shapeId, goal);
  if (!copies) {
    return clearStamp(marks, shapeId, goal);
  }
  for (const std::vector<TrayCell>& copy : *copies) {
    if (std::find(copy.begin(), copy.end(), TrayCell{x, y, z}) == copy.end()) {
      continue;
    }
    for (const TrayCell& c : copy) {
      marks->set(tray->getIndex(asUnsigned(c[0]), asUnsigned(c[1]), asUnsigned(c[2])), goal, 0);
    }
    return true;
  }
  return false;
}

bool clearStamp(SlideMarks* marks, unsigned int shapeId, bool goal) {
  if (marks == nullptr) {
    return false;
  }
  const unsigned int id = shapeId + 1;
  bool had = false;
  for (std::size_t i = marks->size(goal); i-- > 0;) {
    if (marks->get(i, goal) == id) {
      had = true;
      marks->set(i, goal, 0);
    }
  }
  return had;
}

std::string copyStampsRefusal(const Puzzle& puzzle, const Problem& problem, bool toGoal) {
  if (!usesStartMarks(puzzle, problem)) {
    return "Choose a start/goal shape.";
  }
  const Voxel& tray = problem.resultShape(puzzle);
  const SlideMarks& marks = problem.slideMarks();
  for (unsigned int i = 0; i < tray.getXYZ() && toGoal; i++) {
    if (marks.start(i) != 0 && tray.getState(i) == Voxel::kVariable) {
      return "A goal cannot be on a variable cell: the start of " +
             shapeLabel(puzzle, marks.start(i) - 1) + " is on one.";
    }
  }
  return std::string();
}

std::string copyStamps(const Puzzle& puzzle, Problem& problem, bool toGoal) {
  if (std::string why = copyStampsRefusal(puzzle, problem, toGoal); !why.empty()) {
    return why;
  }
  SlideMarks& marks = problem.editSlideMarks();
  const std::size_t cells = marks.size();
  for (std::size_t i = cells; i-- > 0;) {
    marks.set(i, toGoal, marks.get(i, !toGoal));
  }
  return std::string();
}

unsigned int stampAt(const Voxel* tray, const SlideMarks* marks, int x, int y, int z, bool goal) {
  if (tray == nullptr || marks == nullptr || !inTray(*tray, x, y, z)) {
    return static_cast<unsigned int>(-1);
  }
  const unsigned int id =
      marks->get(tray->getIndex(asUnsigned(x), asUnsigned(y), asUnsigned(z)), goal);
  return id == 0 ? static_cast<unsigned int>(-1) : id - 1;
}

std::string startGoalError(const Puzzle& puzzle, unsigned int trayShape,
                           const Problem* problem) {
  if (trayShape >= puzzle.getNumberOfShapes() ||
      !isStartGoalShape(&puzzle.getShape(trayShape))) {
    return "Choose a start/goal shape.";
  }
  const Voxel& tray = puzzle.getShape(trayShape);
  const Problem* on = problem != nullptr ? problem : trayProblem(puzzle, trayShape);
  const SlideMarks& marks = on != nullptr ? on->slideMarks() : kNoMarks;
  bool both = false;
  for (unsigned int s = 0; s < puzzle.getNumberOfShapes() && s < kMaxMark; s++) {
    const Voxel& piece = puzzle.getShape(s);
    if (isStartGoalShape(&piece) || isHiddenSlidingShape(&piece)) {
      continue;
    }
    const auto starts = stampCopies(tray, marks, piece, s, false);
    const auto goals = stampCopies(tray, marks, piece, s, true);
    const std::string name = shapeLabel(puzzle, s);
    if (!starts) {
      return "The start of " + name + " is not the whole piece. Place it again.";
    }
    if (!goals) {
      return "The goal of " + name + " is not the whole piece. Place it again.";
    }
    const unsigned int copies = pieceCopies(puzzle, trayShape, s, on);
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

unsigned int pieceCopies(const Puzzle& puzzle, unsigned int trayShape, unsigned int shapeId,
                         const Problem* problem) {
  const Problem* pr = problem != nullptr ? problem : trayProblem(puzzle, trayShape);
  return pr == nullptr ? 1U : std::max(1U, pr->getShapeMaximum(shapeId));
}

unsigned int stampedCopies(const Puzzle& puzzle, const Problem& problem, unsigned int shapeId) {
  if (!problem.resultValid() || shapeId >= puzzle.getNumberOfShapes() || shapeId >= kMaxMark) {
    return 0;
  }
  const Voxel& tray = problem.resultShape(puzzle);
  if (!isStartGoalShape(&tray)) {
    return 0;
  }
  const Voxel& piece = puzzle.getShape(shapeId);
  const SlideMarks& marks = problem.slideMarks();
  return std::max(placedCopies(tray, marks, piece, shapeId + 1, false),
                  placedCopies(tray, marks, piece, shapeId + 1, true));
}

namespace {

// Per shape, whether it has starts for fewer than all its copies on the
// problem's start/goal shape (the other copies start anywhere); empty when
// no shape has.
std::vector<bool> partlyStartedShapes(const Puzzle& puzzle, const Problem& problem) {
  std::vector<bool> partly;
  if (!problem.resultValid() || !isStartGoalShape(&problem.resultShape(puzzle))) {
    return partly;
  }
  const Voxel& tray = problem.resultShape(puzzle);
  for (unsigned int s = 0; s < puzzle.getNumberOfShapes() && s < kMaxMark; s++) {
    const Voxel& piece = puzzle.getShape(s);
    if (isStartGoalShape(&piece) || isHiddenSlidingShape(&piece)) {
      continue;
    }
    const unsigned int starts = placedCopies(tray, problem.slideMarks(), piece, s + 1, false);
    if (starts > 0 && starts < problem.getShapeMaximum(s)) {
      partly.resize(puzzle.getNumberOfShapes(), false);
      partly[s] = true;
    }
  }
  return partly;
}

}  // namespace

bool usesStartMarks(const Puzzle& puzzle, const Problem& problem) {
  return problem.resultValid() && isStartGoalShape(&problem.resultShape(puzzle));
}

std::vector<bool> freeStartParts(const Puzzle& puzzle, const Problem& problem) {
  std::vector<bool> free(problem.getNumberOfParts(), true);
  if (!problem.resultValid() || !isStartGoalShape(&problem.resultShape(puzzle))) {
    return free;
  }
  const Voxel& tray = problem.resultShape(puzzle);
  for (unsigned int part = 0; part < problem.getNumberOfParts(); part++) {
    const unsigned int s = problem.getShapeIdOfPart(part);
    const Voxel& piece = puzzle.getShape(s);
    // a piece with a start for every copy goes only on its starts
    free[part] = placedCopies(tray, problem.slideMarks(), piece, s + 1, false) <
                 problem.getPartMaximum(part);
  }
  return free;
}

bool startFillsStamps(const Puzzle& puzzle, const Problem& problem, const Assembly& assembly) {
  const std::vector<bool> partly = partlyStartedShapes(puzzle, problem);
  if (partly.empty() || !problem.resultValid()) {
    return true;
  }
  const Voxel& tray = problem.resultShape(puzzle);
  unsigned int pc = 0;
  for (unsigned int part = 0; part < problem.getNumberOfParts(); part++) {
    const unsigned int shapeId = problem.getShapeIdOfPart(part);
    const unsigned int count = problem.getPartMaximum(part);
    if (shapeId >= partly.size() || !partly[shapeId]) {
      pc += count;
      continue;
    }
    // As many copies wholly on the piece's starts as it has starts fill
    // them: the starts hold that many copies' cells.
    const Voxel& piece = puzzle.getShape(shapeId);
    const unsigned int id = shapeId + 1;
    const SlideMarks& marks = problem.slideMarks();
    const unsigned int want = placedCopies(tray, marks, piece, id, false);
    unsigned int on = 0;
    for (unsigned int j = 0; j < count; j++, pc++) {
      if (pc >= assembly.placementCount() || !assembly.isPlaced(pc)) {
        continue;
      }
      const std::vector<TrayCell> cells = placedCells(piece, assembly, pc);
      const bool all =
          !cells.empty() && std::all_of(cells.begin(), cells.end(), [&](const TrayCell& c) {
            return inTray(tray, c[0], c[1], c[2]) &&
                   marks.start(tray.getIndex(asUnsigned(c[0]), asUnsigned(c[1]),
                                             asUnsigned(c[2]))) == id;
          });
      on += all ? 1 : 0;
    }
    if (on < want) {
      return false;
    }
  }
  return true;
}

bool shapeIsRequired(const Puzzle& puzzle, const Problem& problem, unsigned int shapeId) {
  if (!problem.resultValid()) {
    return false;
  }
  const Voxel& tray = problem.resultShape(puzzle);
  if (!isStartGoalShape(&tray)) {
    return false;
  }
  return hasMark(problem.slideMarks(), shapeId + 1);
}

namespace {

// A problem whose result is the start/goal shape s: no start/goal shape is
// a piece of it, and every piece stamped on s is (a copy for each start and
// each goal).
void syncTrayProblem(Puzzle& puzzle, Problem& pr, unsigned int s) {
  for (unsigned int i = 0; i < puzzle.getNumberOfShapes(); i++) {
    if (i != s && isStartGoalShape(&puzzle.getShape(i)) && pr.getShapeMaximum(i) > 0) {
      pr.setShapeMaximum(puzzle, i, 0);
    }
  }
  for (unsigned int i = 0; i < puzzle.getNumberOfShapes(); i++) {
    if (isStartGoalShape(&puzzle.getShape(i)) || isHiddenSlidingShape(&puzzle.getShape(i))) {
      continue;
    }
    if (hasMark(pr.slideMarks(), i + 1)) {
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
}

}  // namespace

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
      puzzle.getProblem(found).setResultId(puzzle, s);
    }
  }

  // marks drawn on a board itself, as the original kept them, are its
  // problems' now
  adoptBoardMarks(puzzle);

  for (unsigned int p = 0; p < puzzle.getNumberOfProblems(); p++) {
    // the problem is named after its start/goal shape
    Problem& pr = puzzle.getProblem(p);
    const unsigned int s = pr.getResultId();
    if (trayProblem(puzzle, s) == &pr) {
      pr.setName(problemName(&puzzle.getShape(s)));
    }
    syncTrayProblem(puzzle, pr, s);
  }
}

void syncSlidingParts(Puzzle& puzzle) {
  if (!isSliding(puzzle)) {
    return;
  }
  renumberStartGoalShapes(puzzle);
  for (unsigned int p = 0; p < puzzle.getNumberOfProblems(); p++) {
    Problem& pr = puzzle.getProblem(p);
    const bool onTray = pr.resultValid() && isStartGoalShape(&puzzle.getShape(pr.getResultId()));
    if (onTray) {
      syncTrayProblem(puzzle, pr, pr.getResultId());
      continue;
    }
    // a problem with no start/goal shape yet: still no board is a piece of it
    for (unsigned int i = 0; i < puzzle.getNumberOfShapes(); i++) {
      if (isStartGoalShape(&puzzle.getShape(i)) && pr.getShapeMaximum(i) > 0) {
        pr.setShapeMaximum(puzzle, i, 0);
      }
    }
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
    const SlideMarks& marks = problem.slideMarks();
    for (unsigned int i = 0; i < tray.getXYZ(); i++) {
      if (marks.start(i) == id) {
        starts++;
      }
      if (marks.goal(i) == id) {
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
  return !problem.maxHolesDefined() || problem.getMaxHoles() != holes;
}

bool prepareForSolving(Puzzle& puzzle, Problem& problem) {
  // what is there before, to tell whether anything changed
  const bool hadMaxHoles = problem.maxHolesDefined();
  const unsigned int maxHoles = hadMaxHoles ? problem.getMaxHoles() : 0;
  syncMaxHoles(puzzle, problem);
  return hadMaxHoles != problem.maxHolesDefined() ||
         (hadMaxHoles && maxHoles != problem.getMaxHoles());
}

void convertLegacyStarts(Puzzle& puzzle) {
  if (!isSliding(puzzle)) {
    return;
  }
  bool trays = false;
  for (unsigned int s = 0; s < puzzle.getNumberOfShapes(); s++) {
    Voxel& v = puzzle.getShape(s);
    if (!isStartGoalShape(&v)) {
      continue;
    }
    trays = true;
    for (unsigned int i = 0; i < v.getXYZ(); i++) {
      if (const unsigned int c = v.getColor(i); c != 0) {
        v.setStartPiece(i, c);
        v.setColor(i, 0);
      }
    }
  }
  if (!trays) {
    // the older model, a tray and a goal map painted in piece colours
    return;
  }
  // the pieces' lock colours, and the palette the locks needed, unless a
  // problem has rules of its own for it (none the original made had)
  for (unsigned int s = 0; s < puzzle.getNumberOfShapes(); s++) {
    Voxel& v = puzzle.getShape(s);
    if (isStartGoalShape(&v)) {
      continue;
    }
    for (unsigned int i = 0; i < v.getXYZ(); i++) {
      if (v.getColor(i) != 0) {
        v.setColor(i, 0);
      }
    }
  }
  bool rules = false;
  for (unsigned int p = 0; p < puzzle.getNumberOfProblems() && !rules; p++) {
    rules = !puzzle.getProblem(p).getColorConstraints().empty();
  }
  if (!rules) {
    while (puzzle.colorNumber() > 0) {
      puzzle.removeColor(puzzle.colorNumber());
    }
  }
  adoptBoardMarks(puzzle);
}

void adoptBoardMarks(Puzzle& puzzle) {
  for (unsigned int s = 0; s < puzzle.getNumberOfShapes(); s++) {
    Voxel& v = puzzle.getShape(s);
    if (!v.hasStartPieces() && !v.hasGoalPieces()) {
      continue;
    }
    for (unsigned int p = 0; p < puzzle.getNumberOfProblems() && isStartGoalShape(&v); p++) {
      Problem& pr = puzzle.getProblem(p);
      if (!onTray(pr, s)) {
        continue;
      }
      SlideMarks& marks = pr.editSlideMarks();
      marks.clear();
      for (unsigned int i = 0; i < v.getXYZ(); i++) {
        marks.set(i, false, v.getStartPiece(i));
        marks.set(i, true, v.getGoalPiece(i));
      }
    }
    for (unsigned int i = 0; i < v.getXYZ(); i++) {
      v.setStartPiece(i, 0);
      v.setGoalPiece(i, 0);
    }
  }
}

// ---- a board that changes under its problems' starts and goals ----------------

namespace {

// The most cells a board edit can follow: a tag is two bytes, the cell's
// start mark and goal mark on the shape being edited.
constexpr unsigned int kMaxTag = 65535;

// The cell with index i of a shape.
TrayCell cellOf(const Voxel& v, unsigned int i) {
  const unsigned int x = i % v.getX();
  const unsigned int y = (i / v.getX()) % v.getY();
  const unsigned int z = i / (v.getX() * v.getY());
  return {asInt(x), asInt(y), asInt(z)};
}

// Adds the starts and goals of problem p (on board `tray`) to the edit: a
// stamp for each whole copy of a piece, one for marks that are no whole
// copies, each cell under a tag (tagOf: the tags given so far, by cell).
void captureStamps(const Puzzle& puzzle, unsigned int p, const Voxel& tray, BoardEdit& edit,
                   std::map<unsigned int, unsigned int>& tagOf) {
  const SlideMarks& marks = puzzle.getProblem(p).slideMarks();
  for (int goal = 0; goal < 2; goal++) {
    std::set<unsigned int> ids;
    for (unsigned int i = 0; i < tray.getXYZ(); i++) {
      if (const unsigned int m = marks.get(i, goal != 0); m != 0) {
        ids.insert(m);
      }
    }
    for (const unsigned int id : ids) {
      const unsigned int s = id - 1;
      if (!isPiece(puzzle, s)) {
        continue;  // no piece: the mark goes
      }
      std::vector<TrayCell> cells = stampedCells(tray, marks, id, goal != 0);
      auto copies = splitCopies(cells, puzzle.getShape(s));
      const bool whole = copies.has_value();
      if (!whole) {
        copies.emplace().push_back(std::move(cells));
      }
      for (const std::vector<TrayCell>& copy : *copies) {
        BoardEdit::Stamp stamp{p, s, goal != 0, whole, {}};
        for (const TrayCell& c : copy) {
          const unsigned int i =
              tray.getIndex(asUnsigned(c[0]), asUnsigned(c[1]), asUnsigned(c[2]));
          auto [at, added] = tagOf.try_emplace(i, 0U);
          if (added) {
            if (edit.cells.size() >= kMaxTag) {
              tagOf.erase(at);
              continue;  // more cells than tags: this one is lost
            }
            edit.cells.push_back(i);
            at->second = static_cast<unsigned int>(edit.cells.size());
          }
          stamp.tags.push_back(at->second);
        }
        if (whole && stamp.tags.size() != copy.size()) {
          stamp.tags.clear();
        }
        edit.stamps.push_back(std::move(stamp));
      }
    }
  }
}

// Puts the edit's stamps on `tray` as it is now, where the cells went
// (edit.moved): a whole copy when every cell of it is still there, on the
// floor (a goal on no variable cell), and the cells are still the piece as
// drawn; of marks that were no whole copies, the cells still there. The
// problems' marks were cleared first. Returns the pieces that lost a start
// or a goal, in order.
std::vector<unsigned int> placeStamps(Puzzle& puzzle, const BoardEdit& edit, const Voxel& tray) {
  std::set<unsigned int> lost;
  for (const BoardEdit::Stamp& stamp : edit.stamps) {
    SlideMarks& marks = puzzle.getProblem(stamp.problem).editSlideMarks();
    const unsigned int id = stamp.shapeId + 1;
    const auto stays = [&](unsigned int i) {
      return i < tray.getXYZ() && tray.getState(i) != Voxel::kEmpty &&
             !(stamp.goal && tray.getState(i) == Voxel::kVariable) &&
             marks.get(i, stamp.goal) == 0;
    };
    std::vector<unsigned int> now;
    bool all = !stamp.tags.empty();
    for (const unsigned int tag : stamp.tags) {
      const std::vector<unsigned int>& to = edit.moved[tag - 1];
      if (to.size() == 1 && stays(to[0])) {
        now.push_back(to[0]);
      } else {
        all = false;
      }
    }
    if (stamp.whole && all) {
      std::vector<TrayCell> cells;
      cells.reserve(now.size());
      for (const unsigned int i : now) {
        cells.push_back(cellOf(tray, i));
      }
      all = isPiece(puzzle, stamp.shapeId) &&
            fromFirst(std::move(cells)) == fromFirst(pieceCells(puzzle.getShape(stamp.shapeId)));
    }
    if (!all) {
      lost.insert(stamp.shapeId);
      if (stamp.whole) {
        continue;
      }
    }
    for (const unsigned int i : now) {
      marks.set(i, stamp.goal, id);
    }
  }
  return {lost.begin(), lost.end()};
}

}  // namespace

BoardEdit beginBoardEdit(const Puzzle& puzzle, unsigned int trayShape, Voxel& v) {
  BoardEdit edit;
  edit.tray = trayShape;
  const Voxel& tray = puzzle.getShape(trayShape);
  BT_ASSERT(v.getXYZ() == tray.getXYZ());
  std::map<unsigned int, unsigned int> tagOf;
  for (unsigned int p = 0; p < puzzle.getNumberOfProblems(); p++) {
    if (onTray(puzzle.getProblem(p), trayShape)) {
      captureStamps(puzzle, p, tray, edit, tagOf);
    }
  }
  for (unsigned int i = 0; i < v.getXYZ(); i++) {
    v.setStartPiece(i, 0);
    v.setGoalPiece(i, 0);
  }
  for (const auto& [cell, tag] : tagOf) {
    v.setStartPiece(cell, tag & 0xFFU);
    v.setGoalPiece(cell, tag >> 8U);
  }
  return edit;
}

void readBoardTags(BoardEdit& edit, Voxel& v) {
  edit.moved.assign(edit.cells.size(), {});
  for (unsigned int i = 0; i < v.getXYZ(); i++) {
    const unsigned int tag = v.getStartPiece(i) | v.getGoalPiece(i) << 8U;
    if (tag == 0) {
      continue;
    }
    if (tag <= edit.moved.size()) {
      edit.moved[tag - 1].push_back(i);
    }
    v.setStartPiece(i, 0);
    v.setGoalPiece(i, 0);
  }
}

std::vector<unsigned int> applyBoardEdit(Puzzle& puzzle, const BoardEdit& edit) {
  BT_ASSERT(edit.moved.size() == edit.cells.size());
  for (unsigned int p = 0; p < puzzle.getNumberOfProblems(); p++) {
    if (onTray(puzzle.getProblem(p), edit.tray)) {
      puzzle.getProblem(p).editSlideMarks().clear();
    }
  }
  return placeStamps(puzzle, edit, puzzle.getShape(edit.tray));
}

std::vector<unsigned int> changeBoard(Puzzle& puzzle, unsigned int problem,
                                      unsigned int trayShape) {
  Problem& pr = puzzle.getProblem(problem);
  BoardEdit edit;
  edit.tray = trayShape;
  if (usesStartMarks(puzzle, pr) && pr.getResultId() != trayShape) {
    // each cell goes to the cell with its coordinates on the new board
    const Voxel& from = pr.resultShape(puzzle);
    const Voxel& to = puzzle.getShape(trayShape);
    std::map<unsigned int, unsigned int> tagOf;
    captureStamps(puzzle, problem, from, edit, tagOf);
    for (const unsigned int i : edit.cells) {
      const TrayCell c = cellOf(from, i);
      edit.moved.emplace_back();
      if (inTray(to, c[0], c[1], c[2])) {
        edit.moved.back().push_back(
            to.getIndex(asUnsigned(c[0]), asUnsigned(c[1]), asUnsigned(c[2])));
      }
    }
  } else if (pr.resultValid() && pr.getResultId() == trayShape) {
    return {};
  }
  pr.setResultId(puzzle, trayShape);
  if (!isStartGoalShape(&puzzle.getShape(trayShape))) {
    std::set<unsigned int> lost;
    for (const BoardEdit::Stamp& stamp : edit.stamps) {
      lost.insert(stamp.shapeId);
    }
    return {lost.begin(), lost.end()};
  }
  return placeStamps(puzzle, edit, puzzle.getShape(trayShape));
}

namespace {

// True when the problem's tray has a start or a goal of the piece.
bool hasStamp(const Puzzle& puzzle, const Problem& problem, unsigned int shapeId) {
  const Voxel& tray = problem.resultShape(puzzle);
  if (!isStartGoalShape(&tray)) {
    // the older model: a coloured start on the tray, a goal on the goal map
    return hasStart(puzzle, problem, shapeId) || hasGoal(puzzle, problem, shapeId);
  }
  return shapeId < kMaxMark && hasMark(problem.slideMarks(), shapeId + 1);
}

}  // namespace

std::vector<std::uint64_t> partTransforms(const Puzzle& puzzle, const Problem& problem,
                                          bool anyOrientation, bool mirrored) {
  std::vector<std::uint64_t> out(problem.getNumberOfParts(), 1U);
  if (!anyOrientation || !problem.resultValid()) {
    return out;
  }
  // The transformations that keep the layers' axis as it is: the quarter
  // turns about it, and (mirrored) their mirror images in the layers.
  const Voxel& tray = problem.resultShape(puzzle);
  const Symmetries& sym = puzzle.getGridType().getSymmetries();
  const unsigned int end = std::min(64U, sym.getNumTransformationsMirror());
  std::uint64_t turns = 0;
  for (unsigned int t = 0; t < end; t++) {
    if (!mirrored && t >= sym.getNumTransformations()) {
      break;
    }
    double m[9] = {};
    tray.getTransformMatrix(t, m);
    if (m[8] == 1.0) {
      turns |= std::uint64_t{1} << t;
    }
  }
  for (unsigned int part = 0; part < problem.getNumberOfParts(); part++) {
    if (!hasStamp(puzzle, problem, problem.getShapeIdOfPart(part))) {
      out[part] = turns;
    }
  }
  return out;
}

}  // namespace bt::sliding

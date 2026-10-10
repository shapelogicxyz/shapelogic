// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/sliding.cpp: the start/goal shapes, stamps, start
// locks, the older goal-map model and the checks (the first half of the
// file; the search is bt/solve/sliding_search.cpp).
#include "bt/model/sliding.h"

#include "bt/base/int_cast.h"
#include "bt/grid/voxel.h"
#include "bt/model/problem.h"
#include "bt/model/puzzle.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <sstream>

namespace bt::sliding {

namespace {

constexpr const char* kGoalShapeName = "__sliding_goal__";
constexpr const char* kStartGoalPrefix = "sg:";
constexpr const char* kHiddenPrefix = "__sggoal:";

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

// Top row first, as the grid editor shows it (y rises upwards), and left to
// right in a row: the first cell is the top-left one, where a click places
// a piece.
bool topLeftFirst(const std::pair<int, int>& a, const std::pair<int, int>& b) {
  return a.second != b.second ? a.second > b.second : a.first < b.first;
}

// The cells of a piece, all layers seen from above, top-left first.
std::vector<std::pair<int, int>> footprint(const Voxel& piece) {
  std::vector<std::pair<int, int>> cells;
  for (unsigned int y = 0; y < piece.getY(); y++) {
    for (unsigned int x = 0; x < piece.getX(); x++) {
      for (unsigned int z = 0; z < piece.getZ(); z++) {
        if (piece.getState(x, y, z) != Voxel::kEmpty) {
          cells.emplace_back(asInt(x), asInt(y));
          break;
        }
      }
    }
  }
  std::sort(cells.begin(), cells.end(), topLeftFirst);
  return cells;
}

// The cells moved so that the top-left one is at (0, 0).
std::vector<std::pair<int, int>> fromFirst(std::vector<std::pair<int, int>> cells) {
  std::sort(cells.begin(), cells.end(), topLeftFirst);
  if (!cells.empty()) {
    const std::pair<int, int> o = cells[0];
    for (std::pair<int, int>& c : cells) {
      c.first -= o.first;
      c.second -= o.second;
    }
  }
  return cells;
}

unsigned int stampOf(const Voxel& tray, unsigned int i, bool goal) {
  return goal ? tray.getGoalPiece(i) : tray.getColor(i);
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

std::string displayName(const Voxel* v) {
  if (v == nullptr) {
    return std::string();
  }
  if (!isStartGoalShape(v)) {
    return v->getName();
  }
  const std::string user = startGoalUserName(v);
  const std::string tag = "(START/GOAL " + std::to_string(startGoalNumber(v)) + ")";
  if (user.empty()) {
    return tag;
  }
  return tag + " " + user;
}

unsigned int addStartGoalShape(Puzzle& puzzle, unsigned int sx, unsigned int sy) {
  int next = 1;
  for (unsigned int i = 0; i < puzzle.getNumberOfShapes(); i++) {
    const int n = startGoalNumber(&puzzle.getShape(i));
    if (n >= next) {
      next = n + 1;
    }
  }
  sx = std::max(sx, 1U);
  sy = std::max(sy, 1U);
  const unsigned int id = puzzle.addShape(sx, sy, 1);
  Voxel& v = puzzle.getShape(id);
  for (unsigned int y = 0; y < v.getY(); y++) {
    for (unsigned int x = 0; x < v.getX(); x++) {
      if (v.validCoordinate(asInt(x), asInt(y), 0)) {
        v.setState(x, y, 0, Voxel::kFilled);
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

bool toggleCellMark(Voxel* tray, int x, int y, unsigned int shapeId, bool goal) {
  if (tray == nullptr || x < 0 || y < 0 || asUnsigned(x) >= tray->getX() ||
      asUnsigned(y) >= tray->getY()) {
    return false;
  }
  if (!tray->validCoordinate(x, y, 0)) {
    return false;
  }
  if (tray->getState(asUnsigned(x), asUnsigned(y), 0) == Voxel::kEmpty) {
    return false;
  }
  const unsigned int id = shapeId + 1;
  if (id > 63) {
    return false;
  }
  if (goal) {
    const unsigned int cur = tray->getGoalPiece(asUnsigned(x), asUnsigned(y), 0);
    tray->setGoalPiece(tray->getIndex(asUnsigned(x), asUnsigned(y), 0), cur == id ? 0 : id);
  } else {
    const unsigned int cur = tray->getColor(asUnsigned(x), asUnsigned(y), 0);
    tray->setColor(asUnsigned(x), asUnsigned(y), 0, cur == id ? 0 : id);
  }
  return true;
}

std::vector<std::pair<int, int>> stampCells(const Voxel* piece, int x, int y) {
  std::vector<std::pair<int, int>> cells;
  if (piece == nullptr) {
    return cells;
  }
  cells = fromFirst(footprint(*piece));
  for (std::pair<int, int>& c : cells) {
    c.first += x;
    c.second += y;
  }
  return cells;
}

std::string stampFits(const Voxel* tray, const Voxel* piece, unsigned int shapeId, int x, int y,
                      bool goal) {
  if (tray == nullptr || piece == nullptr) {
    return "No piece is chosen.";
  }
  const unsigned int id = shapeId + 1;
  if (id > 63) {
    return "Only the first 63 shapes can be placed.";
  }
  const std::vector<std::pair<int, int>> cells = stampCells(piece, x, y);
  if (cells.empty()) {
    return "The piece has no cells.";
  }
  for (const std::pair<int, int>& c : cells) {
    const int tx = c.first;
    const int ty = c.second;
    if (tx < 0 || ty < 0 || tx >= asInt(tray->getX()) || ty >= asInt(tray->getY()) ||
        tray->getState(asUnsigned(tx), asUnsigned(ty), 0) == Voxel::kEmpty) {
      return "The piece does not fit there: part of it would be off the floor.";
    }
    if (goal && tray->getState(asUnsigned(tx), asUnsigned(ty), 0) == Voxel::kVariable) {
      return "A goal cannot be on a variable cell: pieces may cross those, not stop on them.";
    }
    const unsigned int other =
        stampOf(*tray, tray->getIndex(asUnsigned(tx), asUnsigned(ty), 0), goal);
    if (other != 0 && other != id) {
      return std::string("The piece would cover the ") + (goal ? "goal" : "start") + " of S" +
             std::to_string(other) + ".";
    }
  }
  return std::string();
}

std::string stampPiece(Voxel* tray, const Voxel* piece, unsigned int shapeId, int x, int y,
                       bool goal) {
  std::string why = stampFits(tray, piece, shapeId, x, y, goal);
  if (!why.empty()) {
    return why;
  }
  const unsigned int id = shapeId + 1;
  clearStamp(tray, shapeId, goal);
  for (const std::pair<int, int>& c : stampCells(piece, x, y)) {
    const unsigned int i = tray->getIndex(asUnsigned(c.first), asUnsigned(c.second), 0);
    if (goal) {
      tray->setGoalPiece(i, id);
    } else {
      tray->setColor(i, id);
    }
  }
  return std::string();
}

std::vector<unsigned int> flattenPieces(Puzzle& puzzle) {
  std::vector<unsigned int> changed;
  for (unsigned int s = 0; s < puzzle.getNumberOfShapes(); s++) {
    Voxel& v = puzzle.getShape(s);
    if (isHiddenSlidingShape(&v) || v.getZ() <= 1) {
      continue;
    }
    v.resize(v.getX(), v.getY(), 1, Voxel::kEmpty);
    v.setHotspot(v.getHx(), v.getHy(), 0);
    changed.push_back(s);
  }
  return changed;
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
  const std::vector<std::pair<int, int>> shape = fromFirst(footprint(piece));
  const unsigned int id = shapeId + 1;
  for (unsigned int t = 0; t < puzzle.getNumberOfShapes(); t++) {
    Voxel& tray = puzzle.getShape(t);
    if (!isStartGoalShape(&tray)) {
      continue;
    }
    bool dropped = false;
    for (int goal = 0; goal < 2; goal++) {
      std::vector<std::pair<int, int>> cells;
      for (unsigned int y = 0; y < tray.getY(); y++) {
        for (unsigned int x = 0; x < tray.getX(); x++) {
          if (stampOf(tray, tray.getIndex(x, y, 0), goal != 0) == id) {
            cells.emplace_back(asInt(x), asInt(y));
          }
        }
      }
      if (!cells.empty() && fromFirst(cells) != shape) {
        dropped = clearStamp(&tray, shapeId, goal != 0) || dropped;
      }
    }
    if (dropped) {
      trays.push_back(t);
    }
  }
  return trays;
}

bool stampIsAt(const Voxel* tray, const Voxel* piece, unsigned int shapeId, int x, int y,
               bool goal) {
  if (tray == nullptr || piece == nullptr) {
    return false;
  }
  const unsigned int id = shapeId + 1;
  std::vector<std::pair<int, int>> now;
  for (unsigned int ty = 0; ty < tray->getY(); ty++) {
    for (unsigned int tx = 0; tx < tray->getX(); tx++) {
      if (stampOf(*tray, tray->getIndex(tx, ty, 0), goal) == id) {
        now.emplace_back(asInt(tx), asInt(ty));
      }
    }
  }
  std::vector<std::pair<int, int>> there = stampCells(piece, x, y);
  std::sort(now.begin(), now.end());
  std::sort(there.begin(), there.end());
  return !now.empty() && now == there;
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

unsigned int stampAt(const Voxel* tray, int x, int y, bool goal) {
  if (tray == nullptr || x < 0 || y < 0 || x >= asInt(tray->getX()) ||
      y >= asInt(tray->getY())) {
    return static_cast<unsigned int>(-1);
  }
  const unsigned int id = stampOf(*tray, tray->getIndex(asUnsigned(x), asUnsigned(y), 0), goal);
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
    const unsigned int id = s + 1;
    std::vector<std::pair<int, int>> starts;
    std::vector<std::pair<int, int>> goals;
    for (unsigned int y = 0; y < tray.getY(); y++) {
      for (unsigned int x = 0; x < tray.getX(); x++) {
        const unsigned int i = tray.getIndex(x, y, 0);
        if (tray.getColor(i) == id) {
          starts.emplace_back(asInt(x), asInt(y));
        }
        if (tray.getGoalPiece(i) == id) {
          goals.emplace_back(asInt(x), asInt(y));
        }
      }
    }
    const std::vector<std::pair<int, int>> shape = fromFirst(footprint(piece));
    const std::string name = "S" + std::to_string(id);
    if (!starts.empty() && fromFirst(starts) != shape) {
      return "The start of " + name + " is not the whole piece. Place it again.";
    }
    if (!goals.empty() && fromFirst(goals) != shape) {
      return "The goal of " + name + " is not the whole piece. Place it again.";
    }
    if (!goals.empty() && starts.empty()) {
      return name + " has a goal but no start.";
    }
    if (!starts.empty() && !goals.empty()) {
      both = true;
    }
  }
  if (!both) {
    return "Place at least one piece at its Start, and the same piece at its Goal.";
  }
  return std::string();
}

void refreshStartLocks(Puzzle& puzzle, const Problem& problem) {
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
      pr.setName("START/GOAL " + std::to_string(startGoalNumber(&puzzle.getShape(s))));
    } else if (puzzle.getProblem(found).getResultId() != s) {
      puzzle.getProblem(found).setResultId(puzzle, s);
    }

    Problem& pr = puzzle.getProblem(found);
    const Voxel& tray = puzzle.getShape(s);
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
        if (pr.getShapeMaximum(i) < 1) {
          pr.setShapeMaximum(puzzle, i, 1);
        }
        if (pr.getShapeMinimum(i) < 1) {
          pr.setShapeMinimum(puzzle, i, 1);
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
    if (starts > voxels) {
      msg << "S" << id << " has " << starts << " start cells but the piece only has " << voxels
          << (voxels == 1 ? " voxel.\n" : " voxels.\n");
    }
    if (goals > voxels) {
      msg << "S" << id << " has " << goals << " goal cells but the piece only has " << voxels
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
  const unsigned int colours = puzzle.colorNumber();
  std::vector<std::unique_ptr<Voxel>> shapes;
  shapes.reserve(puzzle.getNumberOfShapes());
  for (unsigned int s = 0; s < puzzle.getNumberOfShapes(); s++) {
    shapes.push_back(puzzle.getShape(s).clone());
  }

  syncMaxHoles(puzzle, problem);
  refreshStartLocks(puzzle, problem);

  bool changed = colours != puzzle.colorNumber() ||
                 hadMaxHoles != problem.maxHolesDefined() ||
                 (hadMaxHoles && maxHoles != problem.getMaxHoles());
  for (unsigned int s = 0; s < puzzle.getNumberOfShapes() && !changed; s++) {
    changed = !(*shapes[s] == puzzle.getShape(s));
  }
  return changed;
}

}  // namespace bt::sliding

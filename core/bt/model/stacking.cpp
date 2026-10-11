// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/stacking.cpp: the board's rules (totalRods, isPocket,
// rodPosition, the outside channel, rodName, rodHeight, rodCapacity), the
// discs (generateDisk, ensureHotspot, addDisk), the stacks (syncMaps,
// setupError, placeDisk, liftTop, dropInstance, moveDisk), layoutBoard,
// moveAllowed, searchInput, pathSeparation, logicalMoves, startAssembly and
// boardSpan, with their helpers (piecesOf, findPiece, capacityOf, depthOf,
// panexTooDeep, panexRaised, discHeights, spacingOf, diskLabel, checkStack,
// configFromMap, writeState, stateFor, drawnPegHeight). noteShapeRemoved,
// noteShapeSwap and trimStacks are Problem's (bt/model/problem.cpp); the
// file part is bt/io/puzzle_file.cpp; findStackPath is
// bt/solve/stack_search.cpp.
#include "bt/model/stacking.h"

#include "bt/base/assert.h"
#include "bt/base/clone_ptr.h"
#include "bt/grid/grid_type.h"
#include "bt/grid/voxel.h"
#include "bt/model/problem.h"
#include "bt/model/puzzle.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <map>
#include <optional>
#include <utility>

namespace bt::stacking {

namespace {

unsigned int diskSizeOf(const Voxel& v) {
  const unsigned int s = v.getDiskSize();
  return s == 0 ? 1 : s;
}

int findPiece(const std::vector<Disc>& pieces, const DiskRef& ref) {
  for (std::size_t i = 0; i < pieces.size(); i++) {
    if (pieces[i].shapeId == ref.shapeId && pieces[i].instance == ref.instance) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

const StackMap& mapOf(const Problem& prob, bool goal) {
  return goal ? prob.goalStacks() : prob.startStacks();
}

StackMap& mapOf(Problem& prob, bool goal) { return goal ? prob.editGoal() : prob.editStart(); }

bool refOnMap(const StackMap& map, const DiskRef& ref, unsigned int* rodOut) {
  for (std::size_t r = 0; r < map.rods.size(); r++) {
    for (const DiskRef& d : map.rods[r]) {
      if (d == ref) {
        if (rodOut != nullptr) {
          *rodOut = static_cast<unsigned int>(r);
        }
        return true;
      }
    }
  }
  return false;
}

unsigned int capacityOf(const RodSet& board, unsigned int pieceCount) {
  if (board.growHeight) {
    return pieceCount == 0 ? 1 : pieceCount;
  }
  return board.definedHeight;
}

// How deep a disc may go in a Panex column: its size, but no deeper than
// the column.
unsigned int depthOf(const RodSet& board, const Disc& p, unsigned int pieceCount) {
  return std::min(p.size, capacityOf(board, pieceCount));
}

// A Panex column, bottom to top, as depths. The k-th disc from the top sits
// at least k - 1 places down (k - 1 == 0 is the raised place in the
// bridge), so it needs a depth of k - 1 or more. The index of the first
// disc that cannot go that deep, or -1.
int panexTooDeep(const std::vector<unsigned int>& depth) {
  const std::size_t n = depth.size();
  for (std::size_t i = 0; i < n; i++) {
    if (depth[i] + 1 < n - i) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

// The column cannot keep every disc below the bridge: its top disc is
// raised into it, and blocks moves that pass over the column.
bool panexRaised(const std::vector<unsigned int>& depth) {
  const std::size_t n = depth.size();
  for (std::size_t i = 0; i < n; i++) {
    if (depth[i] < n - i) {
      return true;
    }
  }
  return false;
}

// Height above the base of each disc on one rod, bottom to top. Discs stack
// from the base, except in a Panex column, where each falls as far as its
// depth lets it and comes to rest on the V or on the disc below. The
// capacity is the bridge, so a raised top disc sits in it.
std::vector<int> discHeights(const RodSet& board, const std::vector<Disc>& pieces,
                             const std::vector<unsigned int>& column, unsigned int rod) {
  std::vector<int> z(column.size());
  const auto n = static_cast<unsigned int>(pieces.size());
  if (!board.panexColumns || isPocket(board, rod)) {
    for (std::size_t h = 0; h < column.size(); h++) {
      z[h] = static_cast<int>(h);
    }
    return z;
  }
  const auto bridge = static_cast<int>(capacityOf(board, n));
  for (std::size_t h = 0; h < column.size(); h++) {
    const int floor = bridge - static_cast<int>(depthOf(board, pieces[column[h]], n));
    z[h] = h == 0 ? floor : std::max(floor, z[h - 1] + 1);
  }
  return z;
}

int spacingOf(const std::vector<Disc>& pieces) {
  unsigned int maxR = 1;
  for (const Disc& p : pieces) {
    maxR = std::max(maxR, p.size);
  }
  return static_cast<int>(2 * maxR + 1);
}

std::string diskLabel(const Puzzle& puzzle, const Problem& prob, const DiskRef& ref) {
  std::string out = "S" + std::to_string(ref.shapeId + 1);
  if (ref.shapeId < puzzle.getNumberOfShapes()) {
    const std::string& name = puzzle.getShape(ref.shapeId).getName();
    if (!name.empty()) {
      out += " (" + name + ")";
    }
  }
  if (prob.getShapeMaximum(ref.shapeId) > 1) {
    out += " copy " + std::to_string(ref.instance + 1);
  }
  return out;
}

std::string checkStack(const Puzzle& puzzle, const Problem& prob, const RodSet& board,
                       const std::vector<Disc>& pieces, const std::vector<DiskRef>& rod,
                       unsigned int rodNo, const char* which) {
  const auto n = static_cast<unsigned int>(pieces.size());
  const unsigned int cap = rodCapacity(board, rodNo, n);
  if (rod.size() > cap) {
    std::string name = rodName(board, rodNo);
    name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    return name + " of " + which + " holds " + std::to_string(rod.size()) +
           " discs, but its height only allows " + std::to_string(cap) + ".";
  }
  if (isPocket(board, rodNo)) {
    return "";
  }

  if (board.panexColumns) {
    std::vector<unsigned int> depth;
    for (const DiskRef& d : rod) {
      const int idx = findPiece(pieces, d);
      depth.push_back(idx < 0 ? capacityOf(board, n)
                              : depthOf(board, pieces[static_cast<std::size_t>(idx)], n));
    }
    const int bad = panexTooDeep(depth);
    if (bad < 0) {
      return "";
    }
    const auto fromTop = static_cast<unsigned int>(rod.size() - static_cast<std::size_t>(bad));
    const unsigned int size = depth[static_cast<std::size_t>(bad)];
    return "On " + rodName(board, rodNo) + " of " + which + ", " +
           diskLabel(puzzle, prob, rod[static_cast<std::size_t>(bad)]) + " is disc " +
           std::to_string(fromTop) + " from the top, but in a Panex column a size " +
           std::to_string(size) + " disc can only be among the top " + std::to_string(size + 1) +
           ".";
  }

  if (!board.sizeMatters) {
    return "";
  }

  for (std::size_t i = 1; i < rod.size(); i++) {
    const int below = findPiece(pieces, rod[i - 1]);
    const int above = findPiece(pieces, rod[i]);
    if (below < 0 || above < 0) {
      continue;
    }
    const Disc& a = pieces[static_cast<std::size_t>(above)];
    const Disc& b = pieces[static_cast<std::size_t>(below)];
    if (a.size > b.size) {
      return "On " + rodName(board, rodNo) + " of " + which + ", " +
             diskLabel(puzzle, prob, rod[i]) + " (size " + std::to_string(a.size) +
             ") sits on the smaller " + diskLabel(puzzle, prob, rod[i - 1]) + " (size " +
             std::to_string(b.size) + "), but size matters is on.";
    }
  }
  return "";
}

// The stacking of a map in the searches' numbers; nullopt when the map does
// not have the board's rods or names a disc that is not a piece.
std::optional<Stacking> configFromMap(const Puzzle& puzzle, const Problem& prob, bool goal,
                                      const std::vector<Disc>& pieces) {
  const RodSet& board = puzzle.getRodSet(prob.getRodSetId());
  Stacking cfg(totalRods(board));
  const StackMap& map = mapOf(prob, goal);
  if (map.rods.size() != totalRods(board)) {
    return std::nullopt;
  }
  for (unsigned int r = 0; r < totalRods(board); r++) {
    for (const DiskRef& ref : map.rods[r]) {
      const int idx = findPiece(pieces, ref);
      if (idx < 0) {
        return std::nullopt;
      }
      cfg[r].push_back(static_cast<unsigned int>(idx));
    }
  }
  return cfg;
}

// What the animation needs to place a disc: the board, its discs and where
// each rod stands.
struct DrawBoard {
  const RodSet& board;
  const std::vector<Disc>& pieces;
  const std::vector<int>& rodX;
};

void writeState(State& st, const Stacking& cfg, const DrawBoard& draw) {
  for (std::size_t r = 0; r < cfg.size(); r++) {
    const std::vector<int> z =
        discHeights(draw.board, draw.pieces, cfg[r], static_cast<unsigned int>(r));
    for (std::size_t h = 0; h < cfg[r].size(); h++) {
      st.set(cfg[r][h], draw.rodX[r], 0, z[h], 0);
    }
  }
}

// One frame of a transfer: 0 the stacking before, 1 the moving disc lifted
// above its rod, 2 above the rod it goes to, 3 the stacking after.
State stateFor(const Stacking& before, const Stacking* after, int frame, const DrawBoard& draw,
               unsigned int n, int liftZ) {
  State st(n);
  if (after == nullptr || frame == 0) {
    writeState(st, before, draw);
    return st;
  }

  unsigned int mover = 0;
  std::size_t src = 0;
  std::size_t dst = 0;
  bool found = false;
  for (std::size_t r = 0; r < before.size() && !found; r++) {
    for (const unsigned int piece : before[r]) {
      bool still = false;
      for (const unsigned int q : (*after)[r]) {
        if (q == piece) {
          still = true;
        }
      }
      if (!still) {
        mover = piece;
        src = r;
        found = true;
        break;
      }
    }
  }
  for (std::size_t r = 0; r < after->size(); r++) {
    for (const unsigned int piece : (*after)[r]) {
      if (piece == mover) {
        dst = r;
      }
    }
  }

  Stacking shown = before;
  shown[src].pop_back();
  writeState(st, shown, draw);

  // Cruise in the first cell above the drawn peg so the disc clears every rod.
  const int z = std::max(liftZ, 1);
  if (frame == 1) {
    st.set(mover, draw.rodX[src], 0, z, 0);
  } else if (frame == 2) {
    st.set(mover, draw.rodX[dst], 0, z, 0);
  } else {
    writeState(st, *after, draw);
  }
  return st;
}

// Height used when drawing pegs. Grow has no fixed length, so an empty
// board is still five cells tall. The solver capacity is unchanged.
unsigned int drawnPegHeight(const RodSet& board, unsigned int pieceCount) {
  unsigned int h = capacityOf(board, pieceCount);
  if (board.growHeight && h < 5) {
    h = 5;
  }
  return std::max(h, 1U);
}

}  // namespace

bool isStacking(const Puzzle& puzzle) {
  return puzzle.getGridType().getType() == GridKind::Stacking;
}

unsigned int totalRods(const RodSet& board) {
  return board.rodCount + ((board.panexColumns && board.pocketColumn) ? 1 : 0);
}

bool isPocket(const RodSet& board, unsigned int rod) {
  return board.panexColumns && board.pocketColumn && rod == board.rodCount;
}

int rodPosition(const RodSet& board, unsigned int rod) {
  return isPocket(board, rod) ? -1 : static_cast<int>(rod);
}

bool hasOutsideChannel(const RodSet& board) {
  return board.panexColumns && board.outsideChannel && board.rodCount >= 3;
}

bool viaOutsideChannel(const RodSet& board, unsigned int from, unsigned int to) {
  const unsigned int last = board.rodCount - 1;
  return hasOutsideChannel(board) && ((from == 0 && to == last) || (from == last && to == 0));
}

std::string rodName(const RodSet& board, unsigned int rod) {
  return isPocket(board, rod) ? "the pocket column" : "rod " + std::to_string(rod + 1);
}

unsigned int rodHeight(const RodSet& board, unsigned int discCount) {
  return capacityOf(board, discCount);
}

unsigned int rodCapacity(const RodSet& board, unsigned int rod, unsigned int discCount) {
  if (isPocket(board, rod)) {
    return board.pocketHeight;
  }
  const unsigned int cap = capacityOf(board, discCount);
  return board.panexColumns ? cap + 1 : cap;
}

// ---- discs ------------------------------------------------------------------------

void generateDisk(Voxel& v, unsigned int size) {
  size = std::max(size, 1U);

  unsigned int color = 0;
  for (unsigned int i = 0; i < v.getXYZ(); i++) {
    if (v.getState(i) != Voxel::kEmpty) {
      color = v.getColor(i);
      break;
    }
  }

  const unsigned int d = 2 * size + 1;
  v.resize(d, d, 1, Voxel::kEmpty);
  for (unsigned int y = 0; y < d; y++) {
    for (unsigned int x = 0; x < d; x++) {
      if (x == size && y == size) {
        v.set(x, y, 0, Voxel::kEmpty);
        continue;
      }
      v.set(x, y, 0, Voxel::kFilled);
      if (color != 0) {
        v.setColor(x, y, 0, color);
      }
    }
  }
  v.setHotspot(static_cast<int>(size), static_cast<int>(size), 0);
  v.setDiskSize(size);
}

void ensureHotspot(Voxel& v) {
  if (v.getDiskSize() == 0) {
    return;
  }
  const auto r = static_cast<int>(v.getDiskSize());
  v.setHotspot(r, r, 0);
}

bool placeSolvingHotspot(Voxel& v) {
  if (v.getDiskSize() == 0) {
    if (v.hasInitialHotspot()) {
      return false;
    }
    v.initHotspot();
    return true;
  }
  const auto r = static_cast<int>(v.getDiskSize());
  if (v.getHx() == r && v.getHy() == r && v.getHz() == 0) {
    return false;
  }
  ensureHotspot(v);
  return true;
}

unsigned int addDisk(Puzzle& puzzle, unsigned int size) {
  size = std::max(size, 1U);
  const unsigned int d = 2 * size + 1;
  const unsigned int idx = puzzle.addShape(d, d, 1);
  generateDisk(puzzle.getShape(idx), size);
  return idx;
}

std::vector<Disc> discsOf(const Puzzle& puzzle, const Problem& problem) {
  std::vector<Disc> out;
  for (unsigned int p = 0; p < problem.getNumberOfParts(); p++) {
    const unsigned int n = problem.getPartMaximum(p);
    const unsigned int sz = diskSizeOf(problem.partShape(puzzle, p));
    const unsigned int sid = problem.getShapeIdOfPart(p);
    for (unsigned int i = 0; i < n; i++) {
      out.push_back(Disc{sid, i, sz});
    }
  }
  return out;
}

// ---- the stacks -----------------------------------------------------------------

void syncMaps(const Puzzle& puzzle, Problem& problem) {
  if (!problem.rodSetValid(puzzle)) {
    return;
  }
  const unsigned int n = totalRods(puzzle.getRodSet(problem.getRodSetId()));
  // Rods past the count keep their discs, so lowering the rod count and
  // raising it again gives them back. setupError reports them meanwhile.
  auto fit = [n](StackMap& map) {
    if (map.rods.size() < n) {
      map.rods.resize(n);
    }
    while (map.rods.size() > n && map.rods.back().empty()) {
      map.rods.pop_back();
    }
  };
  fit(problem.editStart());
  fit(problem.editGoal());
}

std::string setupError(const Puzzle& puzzle, const Problem& problem) {
  if (!isStacking(puzzle)) {
    return "This problem is not a stacking puzzle.";
  }
  if (!problem.rodSetValid(puzzle)) {
    return "Choose a rod set with Set Start/Goal Rods.";
  }
  const RodSet& board = puzzle.getRodSet(problem.getRodSetId());
  if (board.rodCount < 1) {
    return "The rod set needs at least one rod.";
  }

  const std::vector<Disc> pieces = discsOf(puzzle, problem);
  if (pieces.empty()) {
    return "Add the discs that move on the rods.";
  }

  // Rule breaks come first: they are what an Entities tab edit causes.
  auto checkRules = [&](bool goal, const char* which) -> std::string {
    const StackMap& map = mapOf(problem, goal);
    for (std::size_t r = totalRods(board); r < map.rods.size(); r++) {
      if (!map.rods[r].empty()) {
        return std::string("The ") + which + " has discs on rod " + std::to_string(r + 1) +
               ", but the rod set only has " + std::to_string(board.rodCount) +
               (board.rodCount == 1 ? " rod." : " rods.");
      }
    }
    for (std::size_t r = 0; r < map.rods.size() && r < totalRods(board); r++) {
      std::string bad = checkStack(puzzle, problem, board, pieces, map.rods[r],
                                   static_cast<unsigned int>(r), goal ? "the goal" : "the start");
      if (!bad.empty()) {
        return bad;
      }
    }
    return "";
  };

  std::string err = checkRules(false, "start");
  if (err.empty()) {
    err = checkRules(true, "goal");
  }
  if (!err.empty()) {
    return err;
  }

  for (unsigned int p = 0; p < problem.getNumberOfParts(); p++) {
    if (problem.getPartMinimum(p) != problem.getPartMaximum(p)) {
      return "S" + std::to_string(problem.getShapeIdOfPart(p) + 1) +
             " has a min/max range, but stacking uses a fixed count of each disc.";
    }
  }

  // Start and goal must use the same discs. Compare whole sets, so the
  // message can name every disc that is on one side only.
  auto countsOf = [&](bool goal, std::string* bad) {
    std::vector<int> seen(pieces.size(), 0);
    const StackMap& map = mapOf(problem, goal);
    for (const std::vector<DiskRef>& rod : map.rods) {
      for (const DiskRef& ref : rod) {
        const int idx = findPiece(pieces, ref);
        if (idx < 0) {
          if (bad->empty()) {
            *bad = std::string("The ") + (goal ? "goal" : "start") +
                   " names a disc that is not in the problem.";
          }
          continue;
        }
        seen[static_cast<std::size_t>(idx)]++;
      }
    }
    return seen;
  };
  for (const bool goal : {false, true}) {
    if (mapOf(problem, goal).rods.size() < totalRods(board)) {
      return std::string("The ") + (goal ? "goal" : "start") +
             " does not match the number of rods.";
    }
  }
  const std::vector<int> onStart = countsOf(false, &err);
  const std::vector<int> onGoal = countsOf(true, &err);
  if (!err.empty()) {
    return err;
  }

  for (std::size_t i = 0; i < pieces.size(); i++) {
    const DiskRef ref{pieces[i].shapeId, pieces[i].instance};
    if (onStart[i] > 1) {
      return diskLabel(puzzle, problem, ref) + " is on the start more than once.";
    }
    if (onGoal[i] > 1) {
      return diskLabel(puzzle, problem, ref) + " is on the goal more than once.";
    }
  }

  // Count by shape: copies of one shape are interchangeable to the user.
  std::map<unsigned int, int> startShapes;
  std::map<unsigned int, int> goalShapes;
  for (std::size_t i = 0; i < pieces.size(); i++) {
    startShapes[pieces[i].shapeId] += onStart[i];
    goalShapes[pieces[i].shapeId] += onGoal[i];
  }
  auto listMissing = [&](const std::map<unsigned int, int>& have,
                         const std::map<unsigned int, int>& want) {
    std::vector<std::string> names;
    for (const auto& [shape, count] : want) {
      const int lack = count - have.at(shape);
      if (lack <= 0) {
        continue;
      }
      std::string n = diskLabel(puzzle, problem, DiskRef{shape, 0});
      // diskLabel names copy 1 when there are several; here it is a count.
      const std::string::size_type copy = n.find(" copy ");
      if (copy != std::string::npos) {
        n.erase(copy);
      }
      if (lack > 1) {
        n += " x" + std::to_string(lack);
      }
      names.push_back(n);
    }
    std::string out;
    for (std::size_t i = 0; i < names.size(); i++) {
      if (i != 0) {
        out += (i + 1 == names.size()) ? " and " : ", ";
      }
      out += names[i];
    }
    return out;
  };
  const std::string goalLacks = listMissing(goalShapes, startShapes);
  const std::string startLacks = listMissing(startShapes, goalShapes);
  if (!goalLacks.empty() || !startLacks.empty()) {
    std::string msg = "The start and goal use different discs:";
    if (!goalLacks.empty()) {
      msg += " the goal is missing " + goalLacks;
    }
    if (!goalLacks.empty() && !startLacks.empty()) {
      msg += ";";
    }
    if (!startLacks.empty()) {
      msg += " the start is missing " + startLacks;
    }
    return msg + ".";
  }

  for (std::size_t i = 0; i < pieces.size(); i++) {
    const DiskRef ref{pieces[i].shapeId, pieces[i].instance};
    if (onStart[i] == 0 && onGoal[i] == 0) {
      return diskLabel(puzzle, problem, ref) +
             " is in the problem but on neither the start nor the goal.";
    }
    // Same count per shape but different copies: only an edited file does this.
    if (onStart[i] != onGoal[i]) {
      return diskLabel(puzzle, problem, ref) + " is on the " +
             (onStart[i] != 0 ? "start" : "goal") + " but not on the " +
             (onStart[i] != 0 ? "goal" : "start") + ".";
    }
  }
  return "";
}

std::string placeDisk(const Puzzle& puzzle, Problem& problem, bool goal, unsigned int shapeId,
                      unsigned int rod, bool enforceRules) {
  if (!problem.rodSetValid(puzzle)) {
    return "Set a rod set as the result";
  }
  syncMaps(puzzle, problem);
  const RodSet& board = puzzle.getRodSet(problem.getRodSetId());
  StackMap& map = mapOf(problem, goal);
  if (rod >= map.rods.size()) {
    return "That rod is not on this board";
  }

  const unsigned int count = problem.getShapeMaximum(shapeId);
  if (count == 0) {
    return "Add the disc to the problem first";
  }

  int instance = -1;
  for (unsigned int i = 0; i < count; i++) {
    if (!refOnMap(map, DiskRef{shapeId, i}, nullptr)) {
      instance = static_cast<int>(i);
      break;
    }
  }
  if (instance < 0) {
    return "Every copy of that disc is already on a rod";
  }
  const DiskRef placed{shapeId, static_cast<unsigned int>(instance)};

  if (!enforceRules) {
    map.rods[rod].push_back(placed);
    return "";
  }

  const std::vector<Disc> pieces = discsOf(puzzle, problem);
  const auto n = static_cast<unsigned int>(pieces.size());
  if (map.rods[rod].size() >= rodCapacity(board, rod, n)) {
    return "That rod is full";
  }

  if (board.panexColumns) {
    if (isPocket(board, rod)) {
      map.rods[rod].push_back(placed);
      return "";
    }
    std::vector<unsigned int> depth;
    for (const DiskRef& d : map.rods[rod]) {
      const int idx = findPiece(pieces, d);
      depth.push_back(idx < 0 ? capacityOf(board, n)
                              : depthOf(board, pieces[static_cast<std::size_t>(idx)], n));
    }
    const int mover = findPiece(pieces, placed);
    depth.push_back(mover < 0 ? capacityOf(board, n)
                              : depthOf(board, pieces[static_cast<std::size_t>(mover)], n));
    if (panexTooDeep(depth) >= 0) {
      return "A disc on that rod would be pushed deeper than its size allows";
    }
  } else if (board.sizeMatters && !map.rods[rod].empty()) {
    const int top = findPiece(pieces, map.rods[rod].back());
    const int mover = findPiece(pieces, placed);
    if (top >= 0 && mover >= 0 &&
        pieces[static_cast<std::size_t>(mover)].size > pieces[static_cast<std::size_t>(top)].size) {
      return "A larger disc cannot sit on a smaller one";
    }
  }

  map.rods[rod].push_back(placed);
  return "";
}

namespace {

void eraseAndRenumber(StackMap& map, unsigned int shapeId, unsigned int instance) {
  for (std::vector<DiskRef>& rod : map.rods) {
    for (auto it = rod.begin(); it != rod.end();) {
      if (it->shapeId == shapeId && it->instance == instance) {
        it = rod.erase(it);
      } else {
        if (it->shapeId == shapeId && it->instance > instance) {
          it->instance--;
        }
        ++it;
      }
    }
  }
}

}  // namespace

void dropInstance(Problem& problem, unsigned int shapeId, unsigned int instance) {
  eraseAndRenumber(problem.editStart(), shapeId, instance);
  eraseAndRenumber(problem.editGoal(), shapeId, instance);
}

bool moveDisk(const Puzzle& puzzle, Problem& problem, bool goal, unsigned int rod,
              unsigned int index, int delta) {
  syncMaps(puzzle, problem);
  StackMap& map = mapOf(problem, goal);
  if (rod >= map.rods.size()) {
    return false;
  }
  std::vector<DiskRef>& stack = map.rods[rod];
  if (index >= stack.size()) {
    return false;
  }
  const int next = static_cast<int>(index) + delta;
  if (next < 0 || next >= static_cast<int>(stack.size())) {
    return false;
  }
  std::swap(stack[index], stack[static_cast<std::size_t>(next)]);
  return true;
}

std::string liftTop(const Puzzle& puzzle, Problem& problem, bool goal, unsigned int rod) {
  syncMaps(puzzle, problem);
  StackMap& map = mapOf(problem, goal);
  if (rod >= map.rods.size()) {
    return "That rod is not on this board";
  }
  if (map.rods[rod].empty()) {
    return "That rod is empty";
  }
  map.rods[rod].pop_back();
  return "";
}

BoardLayout layoutBoard(const Puzzle& puzzle, const Problem& problem, bool goal) {
  BoardLayout lay;
  if (!problem.rodSetValid(puzzle)) {
    return lay;
  }
  const RodSet& board = puzzle.getRodSet(problem.getRodSetId());
  const std::vector<Disc> pieces = discsOf(puzzle, problem);
  lay.spacing = spacingOf(pieces);
  lay.rodCount = totalRods(board);
  lay.rodHeight = drawnPegHeight(board, static_cast<unsigned int>(pieces.size()));
  lay.panex = board.panexColumns;
  lay.pocket = board.panexColumns && board.pocketColumn;
  for (const Disc& p : pieces) {
    lay.maxSize = std::max(lay.maxSize, p.size);
  }
  // The pocket stands left of rod 1, so every x stays at 0 or more.
  const int shift = (board.panexColumns && board.pocketColumn) ? 1 : 0;
  lay.rodX.resize(lay.rodCount);
  lay.rodHeights.resize(lay.rodCount);
  for (unsigned int r = 0; r < lay.rodCount; r++) {
    lay.rodX[r] = (rodPosition(board, r) + shift) * lay.spacing;
    lay.rodHeights[r] = isPocket(board, r) ? std::max(1U, board.pocketHeight) : lay.rodHeight;
  }

  lay.disks.assign(pieces.size(), Hotspot{});
  const StackMap& map = mapOf(problem, goal);
  unsigned int unplaced = 0;
  for (std::size_t i = 0; i < pieces.size(); i++) {
    unsigned int rod = 0;
    if (refOnMap(map, DiskRef{pieces[i].shapeId, pieces[i].instance}, &rod) &&
        rod < lay.rodX.size()) {
      std::vector<unsigned int> column;
      std::size_t h = 0;
      for (const DiskRef& d : map.rods[rod]) {
        const int idx = findPiece(pieces, d);
        if (idx < 0) {
          continue;
        }
        if (static_cast<std::size_t>(idx) == i) {
          h = column.size();
        }
        column.push_back(static_cast<unsigned int>(idx));
      }
      lay.disks[i].x = lay.rodX[rod];
      lay.disks[i].y = 0;
      lay.disks[i].z = discHeights(board, pieces, column, rod)[h];
      lay.disks[i].placed = true;
    } else {
      lay.disks[i].x = static_cast<int>(unplaced) * lay.spacing;
      lay.disks[i].y = -2 * lay.spacing;
      lay.disks[i].z = 0;
      lay.disks[i].placed = false;
      unplaced++;
    }
  }
  return lay;
}

// ---- what the searches read and make ----------------------------------------------

bool moveAllowed(const RodSet& board, const std::vector<Disc>& discs, const Stacking& cfg,
                 unsigned int from, unsigned int to) {
  if (from == to || cfg[from].empty()) {
    return false;
  }
  const auto n = static_cast<unsigned int>(discs.size());
  if (cfg[to].size() >= rodCapacity(board, to, n)) {
    return false;
  }

  const unsigned int mover = cfg[from].back();
  const int a = rodPosition(board, from);
  const int b = rodPosition(board, to);
  if (board.panexColumns) {
    // A disc raised into the bridge blocks every move that passes over it,
    // except one that goes around through the outside channel.
    const int lo = std::min(a, b);
    const int hi = std::max(a, b);
    const bool channel = viaOutsideChannel(board, from, to);
    std::vector<unsigned int> depth;
    for (std::size_t r = 0; r < cfg.size() && !channel; r++) {
      const auto rod = static_cast<unsigned int>(r);
      const int p = rodPosition(board, rod);
      if (rod == from || rod == to || p <= lo || p >= hi || isPocket(board, rod)) {
        continue;
      }
      depth.clear();
      for (const unsigned int d : cfg[r]) {
        depth.push_back(depthOf(board, discs[d], n));
      }
      if (panexRaised(depth)) {
        return false;
      }
    }
    if (!isPocket(board, to)) {
      depth.clear();
      for (const unsigned int d : cfg[to]) {
        depth.push_back(depthOf(board, discs[d], n));
      }
      depth.push_back(depthOf(board, discs[mover], n));
      if (panexTooDeep(depth) >= 0) {
        return false;
      }
    }
  } else if (board.sizeMatters && !cfg[to].empty()) {
    const unsigned int top = cfg[to].back();
    if (discs[mover].size > discs[top].size) {
      return false;
    }
  }

  int gap = a - b;
  if (gap < 0) {
    gap = -gap;
  }
  return !(board.distanceMatters && !board.canMoveOver && gap != 1);
}

bool searchInput(const Puzzle& puzzle, const Problem& problem, Stacking& start, Stacking& goal,
                 std::vector<unsigned int>& sizes) {
  if (!setupError(puzzle, problem).empty()) {
    return false;
  }
  const std::vector<Disc> pieces = discsOf(puzzle, problem);
  std::optional<Stacking> s = configFromMap(puzzle, problem, false, pieces);
  if (!s) {
    return false;
  }
  std::optional<Stacking> g = configFromMap(puzzle, problem, true, pieces);
  if (!g) {
    return false;
  }
  start = std::move(*s);
  goal = std::move(*g);
  sizes.clear();
  for (const Disc& p : pieces) {
    sizes.push_back(p.size);
  }
  return true;
}

Separation pathSeparation(const Puzzle& puzzle, const Problem& problem,
                          const std::vector<Stacking>& path) {
  const RodSet& board = puzzle.getRodSet(problem.getRodSetId());
  const std::vector<Disc> pieces = discsOf(puzzle, problem);
  const auto n = static_cast<unsigned int>(pieces.size());
  std::vector<unsigned int> names(n);
  for (unsigned int i = 0; i < n; i++) {
    names[i] = i;
  }
  Separation sep(ClonePtr<Separation>(), ClonePtr<Separation>(), names);
  if (path.empty()) {
    return sep;
  }

  const BoardLayout lay = layoutBoard(puzzle, problem, false);
  // Cruise above the drawn pegs, and above a disc raised into the bridge.
  const int liftZ = static_cast<int>(lay.rodHeight) + (board.panexColumns ? 1 : 0);
  const DrawBoard draw{board, pieces, lay.rodX};
  std::vector<State> frames;
  frames.push_back(stateFor(path[0], nullptr, 0, draw, n, liftZ));
  for (std::size_t step = 0; step + 1 < path.size(); step++) {
    frames.push_back(stateFor(path[step], &path[step + 1], 1, draw, n, liftZ));
    frames.push_back(stateFor(path[step], &path[step + 1], 2, draw, n, liftZ));
    frames.push_back(stateFor(path[step], &path[step + 1], 3, draw, n, liftZ));
  }
  for (std::size_t i = frames.size(); i-- > 0;) {
    sep.addstate(std::move(frames[i]));
  }
  return sep;
}

unsigned int logicalMoves(const Separation& path) { return path.getMoves() / kStepsPerMove; }

Assembly startAssembly(const Puzzle& puzzle, const Problem& problem) {
  Assembly assm(puzzle.getGridType());
  const BoardLayout lay = layoutBoard(puzzle, problem, false);
  for (const Hotspot& h : lay.disks) {
    if (h.placed) {
      assm.addPlacement(0, h.x, h.y, h.z);
    } else {
      assm.addNonPlacement();
    }
  }
  return assm;
}

unsigned int boardSpan(const Puzzle& puzzle, const Problem& problem) {
  const BoardLayout lay = layoutBoard(puzzle, problem, false);
  if (lay.rodCount == 0) {
    return 4;
  }
  // The pocket is the last rod but stands first: take the widest.
  const auto span =
      static_cast<unsigned int>(*std::max_element(lay.rodX.begin(), lay.rodX.end()) + lay.spacing);
  return std::max(span, 4U);
}

}  // namespace bt::stacking

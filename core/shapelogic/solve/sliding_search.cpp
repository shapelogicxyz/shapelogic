// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/sliding.cpp: the slide search (goalCheck_c, the cells
// and outlines of the pieces, legacySearch over text keys, the position
// tables, the level search over the key sets of slidelevels.h),
// findSlidePath, slideRoute, applySlideRoutes and finalPlacementKey. The
// level search's streamed levels, its search from both ends, its thinned
// levels and its saved searches are new (see LevelSearch).
#include "shapelogic/solve/sliding_search.h"

#include "shapelogic/base/assert.h"
#include "shapelogic/base/int_cast.h"
#include "shapelogic/base/system.h"
#include "shapelogic/base/thread_pool.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/model/assembly.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/model/sliding.h"
#include "shapelogic/solve/disasm_to_moves.h"
#include "shapelogic/solve/slide_levels.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <set>
#include <sstream>
#include <tuple>

#if defined(__APPLE__)
#include <malloc/malloc.h>
#elif defined(__GLIBC__)
#include <malloc.h>
#endif

namespace bt::sliding {

namespace {

// The six steps a piece can take: in its layer first, then down and up.
// Down and up are taken only with vertical moves (SlideSearch); on a tray
// one layer deep they never fit, so a flat puzzle is searched exactly as
// the original searched it either way.
constexpr int kDirs = 6;
constexpr int kLayerDirs = 4;
constexpr int kDx[kDirs] = {0, 0, -1, 1, 0, 0};
constexpr int kDy[kDirs] = {-1, 1, 0, 0, 0, 0};
constexpr int kDz[kDirs] = {0, 0, 0, 0, -1, 1};

std::size_t idx(int v) { return static_cast<std::size_t>(v); }

// The steps of kDx/kDy/kDz a piece may take: all six with vertical moves,
// else the four in its layer.
int dirsFor(bool vertical) { return vertical ? kDirs : kLayerDirs; }

bool isFloor(const Voxel& tray, int x, int y, int z) {
  if (x < 0 || y < 0 || z < 0 || asUnsigned(x) >= tray.getX() || asUnsigned(y) >= tray.getY() ||
      asUnsigned(z) >= tray.getZ()) {
    return false;
  }
  return tray.getState(asUnsigned(x), asUnsigned(y), asUnsigned(z)) != Voxel::kEmpty;
}

struct Place {
  int x = 0;
  int y = 0;
  unsigned char trans = 0;
  // The layer the hotspot sits on: a piece may change layers on a tray
  // more than one layer deep, such as lifting a block out of a box.
  int z = 0;
};

struct SlideState {
  std::vector<Place> places;
};

std::string stateKey(const SlideState& s) {
  std::ostringstream os;
  for (std::size_t i = 0; i < s.places.size(); i++) {
    if (i != 0) {
      os << '|';
    }
    os << static_cast<int>(s.places[i].trans) << ':' << s.places[i].x << ',' << s.places[i].y
       << ',' << s.places[i].z;
  }
  return os.str();
}

// Whether each piece sits where the goal wants it. Every piece is judged on
// its own, so a position search can work the answer out once per position
// (at: 0 it cannot end there, 1 it can, 2 it fills one of its goals); a
// piece with goals for only some of its copies reaches the goal when as
// many copies fill its goals as it has goals (together they hold exactly
// the goals' cells), the others ending anywhere.
class GoalCheck {
 public:
  GoalCheck(const Puzzle& puzzle, const Problem& problem, const std::vector<const Voxel*>& pieces)
      : pieces_(pieces) {
    tray_ = problem.resultValid() ? &problem.resultShape(puzzle) : nullptr;
    startGoal_ = tray_ != nullptr && isStartGoalShape(tray_);
    goal_ = (!startGoal_ && problem.goalValid()) ? &puzzle.getShape(problem.getGoalId()) : nullptr;

    // each placement's part, to look up the colours
    std::vector<unsigned int> partOf(pieces.size());
    {
      unsigned int pc = 0;
      for (unsigned int part = 0; part < problem.getNumberOfParts(); part++) {
        for (unsigned int j = 0; j < problem.getPartMaximum(part); j++) {
          partOf[pc++] = part;
        }
      }
    }

    color_.assign(pieces.size(), 0);
    group_.assign(pieces.size(), -1);
    // each part's goals counted once: those with fewer than its copies
    std::vector<int> partGroup(problem.getNumberOfParts(), -1);
    for (std::size_t i = 0; i < pieces.size(); i++) {
      const unsigned int shapeId = problem.getShapeIdOfPart(partOf[i]);
      if (startGoal_) {
        const unsigned int col = shapeId + 1;
        unsigned int marked = 0;
        for (unsigned int gi = 0; gi < tray_->getXYZ(); gi++) {
          if (tray_->getGoalPiece(gi) == col) {
            color_[i] = col;
            marked++;
          }
        }
        const unsigned int part = partOf[i];
        const unsigned int copies = problem.getPartMaximum(part);
        const unsigned int size = cellCount(problem.partShape(puzzle, part));
        const unsigned int goals = size == 0 ? 0 : marked / size;
        if (marked > 0 && goals < copies) {
          if (partGroup[part] < 0) {
            partGroup[part] = static_cast<int>(need_.size());
            need_.push_back(goals);
          }
          group_[i] = partGroup[part];
        }
      } else if (goal_ != nullptr) {
        unsigned int col = pieceColor(puzzle, shapeId);
        if (col == 0) {
          col = partOf[i] + 1;
        }
        for (unsigned int gi = 0; gi < goal_->getXYZ(); gi++) {
          if (goal_->getColor(gi) == col) {
            color_[i] = col;
            break;
          }
        }
      }
    }
  }

  // Whether a piece has goals for only some of its copies (then the
  // arrangement's count decides, not each piece).
  [[nodiscard]] bool counts() const { return !need_.empty(); }
  // The group of piece i's part among those (-1: none), and how many of
  // its copies must fill its goals.
  [[nodiscard]] int group(std::size_t i) const { return group_[i]; }
  [[nodiscard]] const std::vector<unsigned int>& need() const { return need_; }

  // Piece i, placed at place, is where the goal wants it.
  [[nodiscard]] bool piece(std::size_t i, const Place& place) const { return at(i, place) != 0; }

  // 0: piece i cannot end at place; 1: it can; 2: it can, and fills one of
  // its goals (only for a piece of a group).
  [[nodiscard]] char at(std::size_t i, const Place& place) const {
    const Voxel* p = pieces_[i];
    if (startGoal_) {
      if (p == nullptr) {
        return 0;
      }
      // Empty cells are walls. A piece may slide across a variable cell,
      // and the finished position must leave every variable cell empty.
      // Only normal cells are places to stop, a goal stamp included.
      bool ok = true;
      bool onGoal = color_[i] != 0;
      forCells(*p, place, [&](int tx, int ty, int tz) {
        if (!isFloor(*tray_, tx, ty, tz) ||
            tray_->getState(asUnsigned(tx), asUnsigned(ty), asUnsigned(tz)) == Voxel::kVariable) {
          ok = false;
        } else if (color_[i] != 0 && tray_->getGoalPiece(asUnsigned(tx), asUnsigned(ty),
                                                         asUnsigned(tz)) != color_[i]) {
          onGoal = false;
        }
      });
      if (!ok) {
        return 0;
      }
      if (group_[i] >= 0) {
        return onGoal ? 2 : 1;
      }
      return color_[i] == 0 || onGoal ? 1 : 0;
    }
    if (goal_ == nullptr || color_[i] == 0) {
      return 1;
    }
    if (p == nullptr) {
      return 0;
    }
    bool ok = true;
    forCells(*p, place, [&](int tx, int ty, int tz) {
      if (!isFloor(*goal_, tx, ty, tz) ||
          goal_->getColor(asUnsigned(tx), asUnsigned(ty), asUnsigned(tz)) != color_[i]) {
        ok = false;
      }
    });
    return ok ? 1 : 0;
  }

  // Whether pieces whose `at` answers are given reach the goal.
  template <class At>
  [[nodiscard]] bool reached(At atOf) const {
    // a group is a shape's, and only the first 63 shapes have marks
    std::array<unsigned int, 64> filled{};
    for (std::size_t i = 0; i < pieces_.size(); i++) {
      const char a = atOf(i);
      if (a == 0) {
        return false;
      }
      if (a == 2) {
        filled[idx(group_[i])]++;
      }
    }
    for (std::size_t g = 0; g < need_.size(); g++) {
      if (filled[g] != need_[g]) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] bool all(const SlideState& s) const {
    return reached([&](std::size_t i) { return at(i, s.places[i]); });
  }

 private:
  template <class F>
  static void forCells(const Voxel& p, const Place& place, F f) {
    const int hx = p.getHx();
    const int hy = p.getHy();
    const int hz = p.getHz();
    for (unsigned int z = 0; z < p.getZ(); z++) {
      for (unsigned int y = 0; y < p.getY(); y++) {
        for (unsigned int x = 0; x < p.getX(); x++) {
          if (p.getState(x, y, z) != Voxel::kEmpty) {
            f(place.x + asInt(x) - hx, place.y + asInt(y) - hy, place.z + asInt(z) - hz);
          }
        }
      }
    }
  }

  const std::vector<const Voxel*>& pieces_;
  const Voxel* tray_ = nullptr;
  const Voxel* goal_ = nullptr;
  bool startGoal_ = false;
  // The goal colour of each piece; 0 when the goal does not place it.
  std::vector<unsigned int> color_;
  // Each piece's group (-1: none) and each group's count of goals.
  std::vector<int> group_;
  std::vector<unsigned int> need_;

  static unsigned int cellCount(const Voxel& v) {
    unsigned int n = 0;
    for (unsigned int i = 0; i < v.getXYZ(); i++) {
      n += v.getState(i) != Voxel::kEmpty ? 1 : 0;
    }
    return n;
  }
};

// One cell of a piece, relative to its hotspot.
struct Cell3 {
  int x;
  int y;
  int z;
};
using Cell3List = std::vector<Cell3>;
// Shifts of a piece or a group, (dx, dy, dz).
using ShiftList = std::vector<SlideShift>;

// The filled cells of a piece, every layer, relative to its hotspot. Pieces
// only ever translate in a sliding search, so these are worked out once.
Cell3List cellsOf(const Voxel& p) {
  Cell3List out;
  const int hx = p.getHx();
  const int hy = p.getHy();
  const int hz = p.getHz();
  for (unsigned int z = 0; z < p.getZ(); z++) {
    for (unsigned int y = 0; y < p.getY(); y++) {
      for (unsigned int x = 0; x < p.getX(); x++) {
        if (p.getState(x, y, z) != Voxel::kEmpty) {
          out.push_back({asInt(x) - hx, asInt(y) - hy, asInt(z) - hz});
        }
      }
    }
  }
  return out;
}

// A piece's outline: its own cells plus every empty cell that has cells of
// the piece on both sides of it in the same row or the same column of its
// layer, such as the inside of a pocket. Relative to the hotspot.
Cell3List outlineOf(const Voxel& p) {
  Cell3List out;
  const int hx = p.getHx();
  const int hy = p.getHy();
  const int hz = p.getHz();
  const int sx = asInt(p.getX());
  const int sy = asInt(p.getY());
  for (int z = 0; z < asInt(p.getZ()); z++) {
    auto filled = [&p, z](int x, int y) {
      return p.getState(asUnsigned(x), asUnsigned(y), asUnsigned(z)) != Voxel::kEmpty;
    };
    for (int y = 0; y < sy; y++) {
      for (int x = 0; x < sx; x++) {
        bool in = filled(x, y);
        if (!in) {
          bool left = false;
          bool right = false;
          bool up = false;
          bool down = false;
          for (int k = 0; k < x; k++) {
            left = left || filled(k, y);
          }
          for (int k = x + 1; k < sx; k++) {
            right = right || filled(k, y);
          }
          for (int k = 0; k < y; k++) {
            up = up || filled(x, k);
          }
          for (int k = y + 1; k < sy; k++) {
            down = down || filled(x, k);
          }
          in = (left && right) || (up && down);
        }
        if (in) {
          out.push_back({x - hx, y - hy, z - hz});
        }
      }
    }
  }
  return out;
}

// The floor cells, every layer, that no piece outside `movers` covers.
struct FreeGrid {
  int w = 0;
  int h = 0;
  int d = 0;
  std::vector<char> open;
  [[nodiscard]] bool at(int x, int y, int z) const {
    return x >= 0 && y >= 0 && z >= 0 && x < w && y < h && z < d &&
           open[idx((z * h + y) * w + x)] != 0;
  }
};

FreeGrid freeCells(const Voxel& tray, const std::vector<Cell3List>& cells, const SlideState& s,
                   const std::vector<unsigned int>& movers) {
  FreeGrid g;
  g.w = asInt(tray.getX());
  g.h = asInt(tray.getY());
  g.d = asInt(tray.getZ());
  g.open.assign(idx(g.w * g.h * g.d), 0);
  for (int z = 0; z < g.d; z++) {
    for (int y = 0; y < g.h; y++) {
      for (int x = 0; x < g.w; x++) {
        g.open[idx((z * g.h + y) * g.w + x)] = isFloor(tray, x, y, z) ? 1 : 0;
      }
    }
  }
  for (unsigned int i = 0; i < s.places.size(); i++) {
    if (std::find(movers.begin(), movers.end(), i) != movers.end()) {
      continue;
    }
    for (const Cell3& c : cells[i]) {
      const int x = s.places[i].x + c.x;
      const int y = s.places[i].y + c.y;
      const int z = s.places[i].z + c.z;
      if (x >= 0 && y >= 0 && z >= 0 && x < g.w && y < g.h && z < g.d) {
        g.open[idx((z * g.h + y) * g.w + x)] = 0;
      }
    }
  }
  return g;
}

// The movers fit when shifted by d as one rigid group.
bool groupFits(const FreeGrid& g, const std::vector<Cell3List>& cells, const SlideState& s,
               const std::vector<unsigned int>& movers, const SlideShift& d) {
  for (const unsigned int m : movers) {
    for (const Cell3& c : cells[m]) {
      if (!g.at(s.places[m].x + c.x + d[0], s.places[m].y + c.y + d[1],
                s.places[m].z + c.z + d[2])) {
        return false;
      }
    }
  }
  return true;
}

// Every shift the movers can reach as one group while the others stay put.
// One move of the solver is a jump to any of them, whatever the route.
ShiftList reachableShifts(const Voxel& tray, const std::vector<Cell3List>& cells,
                          const SlideState& s, const std::vector<unsigned int>& movers,
                          bool vertical) {
  const FreeGrid g = freeCells(tray, cells, s, movers);
  std::set<SlideShift> seen{{0, 0, 0}};
  ShiftList out;
  std::queue<SlideShift> q;
  q.push({0, 0, 0});
  while (!q.empty()) {
    const SlideShift c = q.front();
    q.pop();
    for (int d = 0; d < dirsFor(vertical); d++) {
      const SlideShift n{c[0] + kDx[d], c[1] + kDy[d], c[2] + kDz[d]};
      if (seen.count(n) != 0 || !groupFits(g, cells, s, movers, n)) {
        continue;
      }
      seen.insert(n);
      out.push_back(n);
      q.push(n);
    }
  }
  return out;
}

// The corner points, as shifts from the start, of a route with the fewest
// straight runs that takes the movers to shift `to`, start and end
// included, in their layer only unless `vertical`. Empty when unreachable.
ShiftList fewestTurns(const Voxel& tray, const std::vector<Cell3List>& cells, const SlideState& s,
                      const std::vector<unsigned int>& movers, const SlideShift& to,
                      bool vertical) {
  const FreeGrid g = freeCells(tray, cells, s, movers);
  const SlideShift from{0, 0, 0};
  std::map<SlideShift, SlideShift> came{{from, from}};
  std::queue<SlideShift> q;
  q.push(from);
  while (!q.empty() && came.count(to) == 0) {
    const SlideShift c = q.front();
    q.pop();
    for (int d = 0; d < dirsFor(vertical); d++) {
      SlideShift n = c;
      for (;;) {
        n[0] += kDx[d];
        n[1] += kDy[d];
        n[2] += kDz[d];
        if (!groupFits(g, cells, s, movers, n)) {
          break;
        }
        if (came.count(n) != 0) {
          continue;
        }
        came[n] = c;
        q.push(n);
      }
    }
  }
  ShiftList route;
  if (came.count(to) == 0) {
    return route;
  }
  for (SlideShift c = to;; c = came[c]) {
    route.push_back(c);
    if (c == from) {
      break;
    }
  }
  std::reverse(route.begin(), route.end());
  return route;
}

// `outer` and everything nested inside it: pieces whose cells all lie in
// the outline of a piece already in the group, or with `partial`, pieces
// with any cell there; repeated until no more join. Pieces that merely
// touch are not nested.
std::vector<unsigned int> nestedGroup(const std::vector<Cell3List>& cells,
                                      const std::vector<Cell3List>& outlines, const SlideState& s,
                                      unsigned int outer, bool partial) {
  std::vector<unsigned int> group{outer};
  for (std::size_t k = 0; k < group.size(); k++) {
    const unsigned int a = group[k];
    std::set<std::tuple<int, int, int>> inside;
    for (const Cell3& c : outlines[a]) {
      inside.insert({s.places[a].x + c.x, s.places[a].y + c.y, s.places[a].z + c.z});
    }
    for (unsigned int b = 0; b < s.places.size(); b++) {
      if (std::find(group.begin(), group.end(), b) != group.end() || cells[b].empty()) {
        continue;
      }
      bool all = true;
      bool any = false;
      for (const Cell3& c : cells[b]) {
        if (inside.count({s.places[b].x + c.x, s.places[b].y + c.y, s.places[b].z + c.z}) != 0) {
          any = true;
        } else {
          all = false;
        }
      }
      if (partial ? any : all) {
        group.push_back(b);
      }
    }
  }
  return group;
}

// The original slide search: an arrangement is a text key, and each move
// works out the free cells afresh. Kept for trays the position tables
// cannot pack into 128 bits, and for A/B runs (SlideSearch::legacy).
void legacySearch(const Voxel& tray, const std::vector<Cell3List>& cells,
                  const std::vector<Cell3List>& outlines, bool nested, const SlideState& initial,
                  const GoalCheck& goals, SlideSearch& search, std::vector<SlideState>& path) {
  const auto n = static_cast<unsigned int>(initial.places.size());

  std::queue<SlideState> q;
  // parent[key] = {the key it came from, the piece moved}; n for the root
  std::map<std::string, std::pair<std::string, unsigned int>> parent;

  const std::string startKey = stateKey(initial);

  // Each arrangement held costs a map node with two copies of its key, plus
  // its share of the queue; measured at about 115 + 2 bytes per key
  // character, so this leaves some room.
  const std::uint64_t memoryLimit =
      search.maxMemoryStates != 0
          ? search.maxMemoryStates
          : memoryStates(128 + 3 * std::uint64_t{startKey.size()}, search.highMemory,
                         search.memoryBytes);
  search.memoryStates = memoryLimit;
  q.push(initial);
  parent[startKey] = {"", n};

  std::uint64_t visited = 0;
  std::string goalKey;
  bool found = false;

  while (!q.empty()) {
    if (parent.size() >= memoryLimit) {
      search.outcome = SlideOutcome::Memory;
      break;
    }
    if ((visited & 1023) == 0) {
      if (search.progress != nullptr) {
        search.progress->store(visited, std::memory_order_relaxed);
      }
      if (search.stop.stopRequested()) {
        search.outcome = SlideOutcome::Stopped;
        break;
      }
    }
    const SlideState cur = q.front();
    q.pop();
    visited++;

    // One move is one piece going anywhere it can reach while the others
    // stay put, round corners included, so the search finds the fewest
    // moves. Counting single-cell steps or straight runs instead leaves
    // equally short paths that alternate pieces, or stop a piece at a
    // corner and come back to it after another piece has moved. With
    // nested slides a piece may also carry what is nested inside it.
    const std::string curKey = stateKey(cur);
    for (unsigned int pi = 0; pi < n && !found; pi++) {
      std::vector<std::vector<unsigned int>> moverSets{{pi}};
      if (nested) {
        // Partial nesting adds a larger group; it does not replace the
        // wholly nested one, or moves that leave a partly nested piece
        // behind would be lost.
        std::vector<unsigned int> group = nestedGroup(cells, outlines, cur, pi, false);
        const std::size_t whole = group.size();
        if (whole > 1) {
          moverSets.push_back(std::move(group));
        }
        if (search.partialNested) {
          std::vector<unsigned int> wider = nestedGroup(cells, outlines, cur, pi, true);
          if (wider.size() > whole) {
            moverSets.push_back(std::move(wider));
          }
        }
      }
      for (const std::vector<unsigned int>& movers : moverSets) {
        for (const SlideShift& shift :
             reachableShifts(tray, cells, cur, movers, search.verticalMoves)) {
          SlideState next = cur;
          for (const unsigned int m : movers) {
            next.places[m].x += shift[0];
            next.places[m].y += shift[1];
            next.places[m].z += shift[2];
          }
          std::string key = stateKey(next);
          if (parent.count(key) != 0) {
            continue;
          }
          parent[key] = {curKey, pi};
          if (goals.all(next)) {
            goalKey = key;
            found = true;
            q = {};
            break;
          }
          q.push(next);
        }
        if (found) {
          break;
        }
      }
    }
  }

  search.visited = visited;
  if (!found) {
    return;
  }

  // The path back from the goal. States keep each piece's placement so the
  // player can put them straight onto the board.
  std::string k = goalKey;
  for (;;) {
    SlideState st;
    st.places.resize(n);
    std::istringstream is(k);
    for (unsigned int i = 0; i < n; i++) {
      char colon = 0;
      char comma = 0;
      char bar = 0;
      int t = 0;
      int x = 0;
      int y = 0;
      int z = 0;
      if (i != 0) {
        is >> bar;
      }
      is >> t >> colon >> x >> comma >> y >> comma >> z;
      st.places[i].trans = static_cast<unsigned char>(t);
      st.places[i].x = x;
      st.places[i].y = y;
      st.places[i].z = z;
    }
    path.push_back(st);
    auto it = parent.find(k);
    if (it == parent.end() || it->second.second == n) {
      break;
    }
    k = it->second.first;
  }
  std::reverse(path.begin(), path.end());
}

#ifdef __SIZEOF_INT128__
// The key of a search whose arrangements do not fit 64 bits.
__extension__ typedef unsigned __int128 WideKey;
#endif

// Every position one piece can take in the empty tray. Pieces only ever
// translate, so this is worked out once per search: a piece keyed to a
// narrow slot, like the stepped back layer of a Panex tray, gets a short
// list.
struct PosTable {
  // The anchor box holding every position; index maps an anchor to its
  // position.
  int x0 = 0;
  int y0 = 0;
  int z0 = 0;
  int w = 0;
  int h = 0;
  int d = 0;
  std::vector<int> index;
  std::vector<int> px;
  std::vector<int> py;
  std::vector<int> pz;
  // The neighbouring position one step along kDx/kDy/kDz, or -1.
  std::vector<std::array<int, kDirs>> next;
  // The tray cells each position covers, `words` 64-bit words a position.
  std::vector<std::uint64_t> mask;
  // With nested slides: the cells of the piece's outline at each position,
  // laid out as mask is. A piece is nested in this one when its mask lies
  // wholly inside.
  std::vector<std::uint64_t> outline;
  std::vector<char> atGoal;
  // Whether a piece may stop at each position (SlideSearch::step).
  std::vector<char> rest;
  // Where this piece's position number sits in a packed arrangement.
  unsigned int shift = 0;
  std::uint64_t bits = 0;

  [[nodiscard]] int at(int x, int y, int z) const {
    x -= x0;
    y -= y0;
    z -= z0;
    if (x < 0 || y < 0 || z < 0 || x >= w || y >= h || z >= d) {
      return -1;
    }
    return index[idx((z * h + y) * w + x)];
  }
};

// Builds the position tables. False when they cannot hold this search: a
// piece with no cells or nowhere to go, a start off the floor, or more
// positions than keyBits bits can number; and (unlike the original, which
// overran its fixed arrays there) more than 64 pieces with nested slides.
bool buildTables(const Voxel& tray, const std::vector<Cell3List>& cells,
                 const std::vector<Cell3List>& outlines, bool nested, const SlideState& initial,
                 const GoalCheck& goals, unsigned int keyBits, bool vertical, int step,
                 std::size_t& words, std::vector<PosTable>& tables) {
  const int W = asInt(tray.getX());
  const int H = asInt(tray.getY());
  const int D = asInt(tray.getZ());
  words = (idx(W) * idx(H) * idx(D) + 63) / 64;
  const auto n = static_cast<unsigned int>(cells.size());
  if (nested && n > 64) {
    return false;
  }
  tables.assign(n, PosTable());
  unsigned int used = 0;

  for (unsigned int i = 0; i < n; i++) {
    if (cells[i].empty()) {
      return false;
    }
    int minX = cells[i][0].x;
    int maxX = minX;
    int minY = cells[i][0].y;
    int maxY = minY;
    int minZ = cells[i][0].z;
    int maxZ = minZ;
    for (const Cell3& c : cells[i]) {
      minX = std::min(minX, c.x);
      maxX = std::max(maxX, c.x);
      minY = std::min(minY, c.y);
      maxY = std::max(maxY, c.y);
      minZ = std::min(minZ, c.z);
      maxZ = std::max(maxZ, c.z);
    }
    PosTable& t = tables[i];
    t.x0 = -minX;
    t.y0 = -minY;
    t.z0 = -minZ;
    t.w = W - maxX - t.x0;
    t.h = H - maxY - t.y0;
    t.d = D - maxZ - t.z0;
    if (t.w <= 0 || t.h <= 0 || t.d <= 0) {
      return false;
    }
    t.index.assign(idx(t.w * t.h * t.d), -1);

    // layer by layer, each as the original numbered its one layer
    const Place& home = initial.places[i];
    for (int z = t.z0; z < t.z0 + t.d; z++) {
      for (int y = t.y0; y < t.y0 + t.h; y++) {
        for (int x = t.x0; x < t.x0 + t.w; x++) {
          bool fits = true;
          for (const Cell3& c : cells[i]) {
            if (!isFloor(tray, x + c.x, y + c.y, z + c.z)) {
              fits = false;
              break;
            }
          }
          if (!fits) {
            continue;
          }
          t.index[idx(((z - t.z0) * t.h + (y - t.y0)) * t.w + (x - t.x0))] = asInt(t.px.size());
          t.px.push_back(x);
          t.py.push_back(y);
          t.pz.push_back(z);
          t.mask.resize(t.mask.size() + words, 0);
          std::uint64_t* m = &t.mask[t.mask.size() - words];
          for (const Cell3& c : cells[i]) {
            const std::size_t bit =
                (idx(z + c.z) * idx(H) + idx(y + c.y)) * idx(W) + idx(x + c.x);
            m[bit / 64] |= std::uint64_t{1} << (bit % 64);
          }
          if (nested) {
            t.outline.resize(t.outline.size() + words, 0);
            std::uint64_t* o = &t.outline[t.outline.size() - words];
            for (const Cell3& c : outlines[i]) {
              const int ox = x + c.x;
              const int oy = y + c.y;
              const int oz = z + c.z;
              if (ox < 0 || oy < 0 || oz < 0 || ox >= W || oy >= H || oz >= D) {
                continue;
              }
              const std::size_t bit = (idx(oz) * idx(H) + idx(oy)) * idx(W) + idx(ox);
              o[bit / 64] |= std::uint64_t{1} << (bit % 64);
            }
          }
          t.atGoal.push_back(goals.at(i, Place{x, y, home.trans, z}));
          t.rest.push_back((x - home.x) % step == 0 && (y - home.y) % step == 0 ? 1 : 0);
        }
      }
    }

    const std::size_t count = t.px.size();
    if (count == 0 || t.at(home.x, home.y, home.z) < 0) {
      return false;
    }
    t.next.resize(count);
    for (std::size_t k = 0; k < count; k++) {
      for (int d = 0; d < kDirs; d++) {
        t.next[k][idx(d)] = d < dirsFor(vertical)
                                ? t.at(t.px[k] + kDx[d], t.py[k] + kDy[d], t.pz[k] + kDz[d])
                                : -1;
      }
    }

    unsigned int width = 1;
    while (width < 64 && (std::uint64_t{1} << width) < count) {
      width++;
    }
    if (used + width > keyBits) {
      return false;
    }
    t.shift = used;
    t.bits = width == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << width) - 1;
    used += width;
  }
  return true;
}

// What one arrangement costs the level search in memory, in key sizes, as
// the memory budget is told in arrangements (SlideSearch::maxMemoryStates,
// memoryStates): its place in a level and its share of the pieces and
// merges that make the next.
constexpr std::uint64_t kLevelStateKeys = 3;

// The arrangements of one slide search over position tables. An arrangement
// is one position number per piece packed into a Key (64 bits, or 128 for
// trays that need more). Read only once built, so every thread of a search
// shares one.
template <class Key>
struct SlideSpace {
  std::vector<PosTable> t;
  std::size_t words = 0;
  unsigned int n = 0;
  bool nested = false;
  // With nested: a piece partly in a member's outline joins a larger group
  // too, as a further move.
  bool partial = false;
  int trayW = 0;
  int trayH = 0;
  int trayD = 0;
  SlideState initial;
  // Groups of pieces that are copies of one another (see build).
  std::vector<std::vector<unsigned int>> copies;
  Key startKey = 0;
  Key startCanon = 0;
  // SlideSearch::verticalMoves and step.
  bool vertical = false;
  int step = 1;
  // The goal, for the pieces whose copies count together (GoalCheck::counts).
  const GoalCheck* goals = nullptr;

  // False when the tables cannot hold this search.
  bool build(const Voxel& tray, const std::vector<Cell3List>& cells,
             const std::vector<Cell3List>& outlines, bool nest, const SlideState& init,
             const GoalCheck& goalCheck, bool likePiecesApart, bool verticalMoves, int stopStep) {
    goals = &goalCheck;
    vertical = verticalMoves;
    step = std::max(1, stopStep);
    if (!buildTables(tray, cells, outlines, nest, init, goalCheck,
                     static_cast<unsigned int>(8 * sizeof(Key)), vertical, step, words, t)) {
      return false;
    }
    n = static_cast<unsigned int>(init.places.size());
    nested = nest;
    initial = init;
    trayW = asInt(tray.getX());
    trayH = asInt(tray.getY());
    trayD = asInt(tray.getZ());

    startKey = 0;
    for (unsigned int i = 0; i < n; i++) {
      const Place& p = init.places[i];
      startKey |= static_cast<Key>(t[i].at(p.x, p.y, p.z)) << t[i].shift;
    }

    // Pieces that are copies of one another (same cells, same places, same
    // goal) can swap places without changing the puzzle. Each such group
    // is kept with its positions in rising order, so the arrangements that
    // differ only by which copy sits where are searched as one.
    copies.clear();
    if (!likePiecesApart) {
      auto sameCells = [](const Cell3List& a, const Cell3List& b) {
        return a.size() == b.size() &&
               std::equal(a.begin(), a.end(), b.begin(), [](const Cell3& p, const Cell3& q) {
                 return p.x == q.x && p.y == q.y && p.z == q.z;
               });
      };
      std::vector<std::vector<unsigned int>> groups;
      for (unsigned int i = 0; i < n; i++) {
        bool placed = false;
        for (std::vector<unsigned int>& g : groups) {
          const unsigned int j = g[0];
          if (sameCells(cells[i], cells[j]) && t[i].px == t[j].px && t[i].py == t[j].py &&
              t[i].pz == t[j].pz && t[i].atGoal == t[j].atGoal) {
            g.push_back(i);
            placed = true;
            break;
          }
        }
        if (!placed) {
          groups.push_back({i});
        }
      }
      for (std::vector<unsigned int>& g : groups) {
        if (g.size() > 1) {
          copies.push_back(std::move(g));
        }
      }
    }
    startCanon = canon(startKey);
    return true;
  }

  [[nodiscard]] int posOf(Key key, unsigned int i) const {
    return static_cast<int>((key >> t[i].shift) & t[i].bits);
  }

  [[nodiscard]] SlideState decode(Key key) const {
    SlideState st = initial;
    for (unsigned int i = 0; i < n; i++) {
      const int p = posOf(key, i);
      st.places[i].x = t[i].px[idx(p)];
      st.places[i].y = t[i].py[idx(p)];
      st.places[i].z = t[i].pz[idx(p)];
    }
    return st;
  }

  // The arrangement standing for key and every one that differs from it
  // only by which copy sits where.
  [[nodiscard]] Key canon(Key key) const {
    for (const std::vector<unsigned int>& g : copies) {
      // a move changes one position, so this is nearly sorted already
      for (std::size_t a = 1; a < g.size(); a++) {
        for (std::size_t b = a; b > 0; b--) {
          const PosTable& lo = t[g[b - 1]];
          const PosTable& hi = t[g[b]];
          const Key pl = (key >> lo.shift) & lo.bits;
          const Key ph = (key >> hi.shift) & hi.bits;
          if (pl <= ph) {
            break;
          }
          key = (key & ~(static_cast<Key>(lo.bits) << lo.shift) &
                 ~(static_cast<Key>(hi.bits) << hi.shift)) |
                (ph << lo.shift) | (pl << hi.shift);
        }
      }
    }
    return key;
  }

  [[nodiscard]] bool isGoal(Key key) const {
    if (goals != nullptr && goals->counts()) {
      return goals->reached([&](std::size_t i) {
        return t[i].atGoal[idx(posOf(key, static_cast<unsigned int>(i)))];
      });
    }
    for (unsigned int i = 0; i < n; i++) {
      if (t[i].atGoal[idx(posOf(key, i))] == 0) {
        return false;
      }
    }
    return true;
  }
};

// The moves out of an arrangement. Holds the scratch space for working them
// out, so each thread has one of its own.
template <class Key>
class SlideMover {
 public:
  explicit SlideMover(const SlideSpace<Key>& space)
      : s_(space),
        occupied_(space.words),
        others_(space.words),
        pos_(space.n),
        seen_(space.n),
        shiftSeen_(space.nested ? idx((2 * space.trayW + 1) * (2 * space.trayH + 1) *
                                      (2 * space.trayD + 1))
                                : 0,
                   0) {
    for (unsigned int i = 0; i < s_.n; i++) {
      seen_[i].assign(s_.t[i].px.size(), 0);
    }
  }

  // Calls emit(next, group) with every arrangement one move from cur, until
  // emit returns true; then it returns true itself. group says the move
  // carried pieces nested in the mover; while emit runs, undoable(next) says
  // whether that same group can move back. Each piece alone, then with what
  // is nested in it; positions in the order a breadth-first fill finds
  // them, which is the order of legacySearch.
  template <class F>
  bool expand(const Key cur, F&& emit) {
    const std::vector<PosTable>& t = s_.t;
    const std::size_t words = s_.words;
    const unsigned int n = s_.n;
    std::fill(occupied_.begin(), occupied_.end(), 0);
    for (unsigned int i = 0; i < n; i++) {
      pos_[i] = s_.posOf(cur, i);
      const std::uint64_t* m = &t[i].mask[idx(pos_[i]) * words];
      for (std::size_t w = 0; w < words; w++) {
        occupied_[w] |= m[w];
      }
    }

    for (unsigned int pi = 0; pi < n; pi++) {
      const PosTable& tp = t[pi];
      const std::uint64_t* mine = &tp.mask[idx(pos_[pi]) * words];
      for (std::size_t w = 0; w < words; w++) {
        others_[w] = occupied_[w] & ~mine[w];
      }

      if (++epoch_ == 0) {
        for (std::vector<unsigned int>& v : seen_) {
          std::fill(v.begin(), v.end(), 0);
        }
        epoch_ = 1;
      }
      seen_[pi][idx(pos_[pi])] = epoch_;
      fill_.assign(1, pos_[pi]);
      const Key clear = cur & ~(static_cast<Key>(tp.bits) << tp.shift);
      for (std::size_t head = 0; head < fill_.size(); head++) {
        for (std::size_t d = 0; d < kDirs; d++) {
          const int nb = tp.next[idx(fill_[head])][d];
          if (nb < 0 || seen_[pi][idx(nb)] == epoch_ ||
              overlaps(&tp.mask[idx(nb) * words], others_.data(), words)) {
            continue;
          }
          seen_[pi][idx(nb)] = epoch_;
          fill_.push_back(nb);
          if (tp.rest[idx(nb)] == 0) {
            continue;
          }
          if (emit(clear | (static_cast<Key>(nb) << tp.shift), false)) {
            return true;
          }
        }
      }

      if (!s_.nested) {
        continue;
      }
      // The wholly nested group, then with partial nesting the larger group
      // that also takes the partly nested pieces: both are moves, so that
      // partial nesting only adds to what plain nesting can do.
      std::uint64_t whole = 0;
      for (const bool wide : {false, true}) {
        if (wide && !s_.partial) {
          break;
        }
        std::array<unsigned int, 64> group{};
        unsigned int members = 0;
        const std::uint64_t inGroup = groupAt(pos_.data(), pi, group.data(), members, wide);
        if (!wide) {
          whole = inGroup;
        } else if (inGroup == whole) {
          continue;
        }
        if (members < 2) {
          continue;
        }
        lastOuter_ = pi;
        lastGroup_ = inGroup;
        lastWide_ = wide;
        others_ = occupied_;
        for (unsigned int g = 0; g < members; g++) {
          const unsigned int m = group[g];
          const std::uint64_t* gm = &t[m].mask[idx(pos_[m]) * words];
          for (std::size_t w = 0; w < words; w++) {
            others_[w] &= ~gm[w];
          }
        }
        // The group's key at shift d, or false when it does not fit.
        auto shifted = [&](const SlideShift& d, Key& key) {
          key = cur;
          for (unsigned int g = 0; g < members; g++) {
            const unsigned int m = group[g];
            const std::size_t at = idx(pos_[m]);
            const int p = t[m].at(t[m].px[at] + d[0], t[m].py[at] + d[1], t[m].pz[at] + d[2]);
            if (p < 0 || overlaps(&t[m].mask[idx(p) * words], others_.data(), words)) {
              return false;
            }
            key = (key & ~(static_cast<Key>(t[m].bits) << t[m].shift)) |
                  (static_cast<Key>(p) << t[m].shift);
          }
          return true;
        };
        // a flood over the shifts the group can make, as for one piece
        if (++shiftEpoch_ == 0) {
          std::fill(shiftSeen_.begin(), shiftSeen_.end(), 0);
          shiftEpoch_ = 1;
        }
        const int trayW = s_.trayW;
        const int trayH = s_.trayH;
        const int trayD = s_.trayD;
        auto seenAt = [&](const SlideShift& d) -> unsigned int& {
          return shiftSeen_[idx(((d[2] + trayD) * (2 * trayH + 1) + (d[1] + trayH)) *
                                    (2 * trayW + 1) +
                                (d[0] + trayW))];
        };
        seenAt({0, 0, 0}) = shiftEpoch_;
        shiftQueue_.assign(1, {0, 0, 0});
        for (std::size_t head = 0; head < shiftQueue_.size(); head++) {
          const SlideShift c = shiftQueue_[head];
          for (int d = 0; d < dirsFor(s_.vertical); d++) {
            const SlideShift nb{c[0] + kDx[d], c[1] + kDy[d], c[2] + kDz[d]};
            // a shift as wide as the tray fits nothing
            if (nb[0] < -trayW || nb[0] > trayW || nb[1] < -trayH || nb[1] > trayH ||
                nb[2] < -trayD || nb[2] > trayD) {
              continue;
            }
            Key key = 0;
            if (seenAt(nb) == shiftEpoch_ || !shifted(nb, key)) {
              continue;
            }
            seenAt(nb) = shiftEpoch_;
            shiftQueue_.push_back(nb);
            if (nb[0] % s_.step != 0 || nb[1] % s_.step != 0) {
              continue;
            }
            if (emit(key, true)) {
              return true;
            }
          }
        }
      }
    }
    return false;
  }

  // For the group move expand is reporting: whether the same pieces are
  // nested in the mover at next, so that the move can be made backwards. A
  // group can close round a piece it passes, and then would take it along.
  bool undoable(Key next) {
    for (unsigned int i = 0; i < s_.n; i++) {
      backPos_[i] = s_.posOf(next, i);
    }
    std::array<unsigned int, 64> group{};
    unsigned int members = 0;
    return groupAt(backPos_.data(), lastOuter_, group.data(), members, lastWide_) == lastGroup_;
  }

 private:
  static bool overlaps(const std::uint64_t* a, const std::uint64_t* b, std::size_t words) {
    for (std::size_t w = 0; w < words; w++) {
      if ((a[w] & b[w]) != 0) {
        return true;
      }
    }
    return false;
  }

  // outer and everything nested inside it (see nestedGroup) with the
  // pieces at at[]: a piece is nested in a member when its cells all lie in
  // that member's outline, or with `partial`, when any of them does. Into
  // group, and as a bit set.
  std::uint64_t groupAt(const int* at, unsigned int outer, unsigned int* group,
                        unsigned int& members, bool partial) const {
    const std::vector<PosTable>& t = s_.t;
    members = 0;
    std::uint64_t inGroup = std::uint64_t{1} << outer;
    group[members++] = outer;
    for (unsigned int k = 0; k < members; k++) {
      const unsigned int a = group[k];
      const std::uint64_t* out = &t[a].outline[idx(at[a]) * s_.words];
      for (unsigned int b = 0; b < s_.n; b++) {
        if (((inGroup >> b) & 1) != 0) {
          continue;
        }
        const std::uint64_t* inner = &t[b].mask[idx(at[b]) * s_.words];
        bool joins = false;
        if (partial) {
          for (std::size_t w = 0; w < s_.words && !joins; w++) {
            joins = (inner[w] & out[w]) != 0;
          }
        } else {
          joins = true;
          for (std::size_t w = 0; w < s_.words && joins; w++) {
            joins = (inner[w] & ~out[w]) == 0;
          }
        }
        if (joins) {
          group[members++] = b;
          inGroup |= std::uint64_t{1} << b;
        }
      }
    }
    return inGroup;
  }

  const SlideSpace<Key>& s_;
  std::vector<std::uint64_t> occupied_;
  std::vector<std::uint64_t> others_;
  std::vector<int> pos_;
  std::array<int, 64> backPos_{};
  // Positions this flood fill has reached: seen_[i][p] == epoch_.
  std::vector<std::vector<unsigned int>> seen_;
  unsigned int epoch_ = 0;
  std::vector<int> fill_;
  // The same for the shifts of a nested group, (dx, dy, dz) from -tray to
  // +tray.
  std::vector<unsigned int> shiftSeen_;
  unsigned int shiftEpoch_ = 0;
  std::vector<SlideShift> shiftQueue_;
  // The group move being reported.
  unsigned int lastOuter_ = 0;
  std::uint64_t lastGroup_ = 0;
  // Whether that group takes partly nested pieces too.
  bool lastWide_ = false;
};

// The path for chain, the arrangements from start to goal as the search
// kept them. Where copies were swapped about, each step is the move from
// the arrangement at hand that gives the next one kept, so every piece
// keeps its own identity along the path.
template <class Key>
void chainToPath(const SlideSpace<Key>& s, const std::vector<Key>& chain,
                 std::vector<SlideState>& path) {
  SlideMover<Key> mover(s);
  Key real = s.startKey;
  path.push_back(s.decode(real));
  for (std::size_t i = 1; i < chain.size(); i++) {
    Key next = chain[i];
    if (!s.copies.empty()) {
      const bool reached = mover.expand(real, [&](Key key, bool) {
        if (s.canon(key) != chain[i]) {
          return false;
        }
        next = key;
        return true;
      });
      BT_ASSERT(reached);
    }
    real = next;
    path.push_back(s.decode(real));
  }
}

// Runs f(i) for every i in [0, n) on up to `threads` threads of the pool,
// each index once (the original's onThreads gave each index a thread of
// its own; the pool may lend fewer).
template <class F>
void forEachIndex(ThreadPool& pool, unsigned int threads, std::size_t n, F f) {
  std::atomic<std::size_t> next{0};
  pool.run(std::max(1U, std::min<unsigned int>(threads, static_cast<unsigned int>(n))),
           [&](unsigned int) {
             for (std::size_t i = next.fetch_add(1); i < n; i = next.fetch_add(1)) {
               f(i);
             }
           });
}

// Gives memory freed by a search level back to the system: the allocator
// keeps large blocks for reuse otherwise, and a level is large.
void releaseFreeMemory() {
#if defined(__APPLE__)
  malloc_zone_pressure_relief(nullptr, 0);
#elif defined(__GLIBC__)
  malloc_trim(0);
#endif
}

// FNV-1a over the bytes of values, as panex_search names its saved searches.
class Fingerprint {
 public:
  template <class T>
  void mix(const T& v) {
    const auto* p = reinterpret_cast<const unsigned char*>(&v);
    for (std::size_t i = 0; i < sizeof(T); i++) {
      h_ ^= p[i];
      h_ *= 1099511628211ULL;
    }
  }
  [[nodiscard]] std::uint64_t value() const { return h_; }

 private:
  std::uint64_t h_ = 14695981039346656037ULL;
};

// Names a problem's saved searches: the tray with its start and goal
// marks, and the pieces with their counts. Nothing of the start layout, so
// that the searches of every start of the problem are found together.
std::uint64_t problemKeyOf(const Puzzle& puzzle, const Problem& problem) {
  Fingerprint f;
  f.mix(2U);  // the saved format
  const Voxel& tray = problem.resultShape(puzzle);
  f.mix(tray.getX());
  f.mix(tray.getY());
  f.mix(tray.getZ());
  for (unsigned int i = 0; i < tray.getXYZ(); i++) {
    f.mix(tray.getState(i));
    f.mix(tray.getStartPiece(i));
    f.mix(tray.getGoalPiece(i));
  }
  for (unsigned int part = 0; part < problem.getNumberOfParts(); part++) {
    const Voxel& shape = problem.partShape(puzzle, part);
    f.mix(shape.getX());
    f.mix(shape.getY());
    f.mix(shape.getZ());
    for (unsigned int i = 0; i < shape.getXYZ(); i++) {
      f.mix(shape.getState(i));
    }
    f.mix(problem.getPartMinimum(part));
    f.mix(problem.getPartMaximum(part));
  }
  return f.value();
}

// The folder of one search's files in the work folder, named after the
// search so that a saved search is found again: made the first time it is
// needed, removed with everything in it when the search ends unless the
// search is saved there (keep). With no work folder there is none.
class SearchFolder {
 public:
  SearchFolder(const std::string& workDir, std::uint64_t key) {
    if (!workDir.empty()) {
      base_ = workDir;
      char name[40];
      std::snprintf(name, sizeof(name), "slide-%016llx", static_cast<unsigned long long>(key));
      name_ = name;
    }
  }
  ~SearchFolder() {
    if (!dir.empty() && !keep) {
      std::error_code ec;
      std::filesystem::remove_all(dir, ec);
    }
  }
  SearchFolder(const SearchFolder&) = delete;
  SearchFolder& operator=(const SearchFolder&) = delete;

  // Whether a folder of this search's name is there already (a saved search).
  [[nodiscard]] bool exists() const {
    std::error_code ec;
    return !base_.empty() && std::filesystem::is_directory(base_ / name_, ec);
  }
  [[nodiscard]] std::filesystem::path place() const { return base_ / name_; }
  [[nodiscard]] bool hasBase() const { return !base_.empty(); }

  // Makes the folder the first time it is needed; false when it cannot be.
  bool open() {
    if (!dir.empty()) {
      return true;
    }
    if (tried_ || base_.empty()) {
      return false;
    }
    tried_ = true;
    std::error_code ec;
    std::filesystem::create_directories(base_ / name_, ec);
    if (std::filesystem::is_directory(base_ / name_, ec)) {
      dir = base_ / name_;
    }
    return !dir.empty();
  }

  // Empties the folder (a saved search that cannot be carried on).
  void wipe() {
    std::error_code ec;
    std::filesystem::remove_all(base_ / name_, ec);
    dir.clear();
    tried_ = false;
  }

  // Empty till open() succeeds.
  std::filesystem::path dir;
  bool keep = false;

 private:
  std::filesystem::path base_;
  std::string name_;
  bool tried_ = false;
};

// ---------------------------------------------------------------------------
// The level search's sets: every level, run and merged set of the search is
// a sorted KeySet, in memory while the memory budget allows and in a file
// of the search's folder otherwise. The store decides where each new set
// goes and keeps count of what the live sets take.

template <class Key>
using SetPtr = std::shared_ptr<levels::KeySet<Key>>;

template <class Key>
class SetStore {
 public:
  using Set = levels::KeySet<Key>;

  // budget: the memory the sets and the threads' pieces may take together;
  // setCap: the memory the sets alone may take before they go to files.
  SetStore(SearchFolder& folder, std::uint64_t budget, std::uint64_t setCap)
      : folder_(folder), budget_(budget), setCap_(std::min(setCap, budget)) {}

  // Bytes the live sets take in memory, and on disk.
  std::pair<std::uint64_t, std::uint64_t> used() {
    const std::lock_guard<std::mutex> lock(m_);
    return usedLocked();
  }

  // A new set for about `estimate` bytes of keys: in memory when the budget
  // allows, else in a file (always a file with `file`). Null when neither
  // can be; failure says why.
  SetPtr<Key> make(std::uint64_t estimate, bool file = false) {
    const std::lock_guard<std::mutex> lock(m_);
    const std::uint64_t mem = usedLocked().first;
    const std::uint64_t held = mem + pieces.load(std::memory_order_relaxed) + estimate;
    bool toMemory = false;
    if (!file && mem + estimate <= setCap_ && held <= budget_) {
      toMemory = true;
    } else if (folder_.open()) {
      toMemory = false;
    } else if (!file && held <= budget_) {
      // nowhere to write: memory until the budget
      toMemory = true;
    } else {
      failure = SlideOutcome::Memory;
      return nullptr;
    }
    std::filesystem::path p;
    if (!toMemory) {
      char name[32];
      std::snprintf(name, sizeof(name), "%06u.keys", fileNumber++);
      p = folder_.dir / name;
    }
    auto set = std::make_shared<Set>();
    if (!set->create(p)) {
      ioFailed();
      return nullptr;
    }
    // counted as its estimate until it is finished
    live_.push_back({set, toMemory ? estimate : 0, 0});
    return set;
  }

  // Takes in a set opened from a saved search.
  void adopt(const SetPtr<Key>& set) {
    const std::lock_guard<std::mutex> lock(m_);
    live_.push_back({set, set->memoryBytes(), set->bytes()});
  }

  // Finishes a set this store made and takes note of what it came to. The
  // store reads a set only here and in refresh, from the thread that owns
  // it: the other threads' sets are being written meanwhile.
  bool finish(const SetPtr<Key>& set) {
    if (!set->finish()) {
      ioFailed();
      return false;
    }
    refresh(set);
    return true;
  }

  // Takes note of a finished set's size again (after release or reload).
  void refresh(const SetPtr<Key>& set) {
    const std::lock_guard<std::mutex> lock(m_);
    for (Entry& e : live_) {
      if (e.set.lock() == set) {
        e.memory = set->memoryBytes();
        e.disk = set->inMemory() ? 0 : set->bytes();
        return;
      }
    }
  }

  // The set written to a file instead (a saved search keeps only files);
  // false when it cannot be.
  bool toFile(SetPtr<Key>& set) {
    if (!set || !set->inMemory()) {
      return true;
    }
    SetPtr<Key> copy = make(set->bytes(), true);
    if (!copy) {
      return false;
    }
    typename Set::Cursor c(*set);
    Key k = 0;
    while (c.next(k)) {
      if (!copy->add(k)) {
        ioFailed();
        return false;
      }
    }
    if (c.failed() || !finish(copy)) {
      ioFailed();
      return false;
    }
    set = copy;
    return true;
  }

  // The write failed: the disk is full, or the folder gone.
  void ioFailed() {
    const std::lock_guard<std::mutex> lock(errorMutex_);
    failure = SlideOutcome::Disk;
    error = folder_.dir.empty()
                ? std::string("The search could not keep its levels.")
                : "Could not write the search's files in " + folder_.dir.string() +
                      ". The disk may be full.";
  }

  [[nodiscard]] std::uint64_t budget() const { return budget_; }
  [[nodiscard]] std::uint64_t setCap() const { return setCap_; }

  // Bytes of sorted pieces the expanding threads hold at the moment.
  std::atomic<std::uint64_t> pieces{0};
  // Numbers the files of the folder.
  unsigned int fileNumber = 0;
  // Why a set could not be made: Memory or Disk (NoPath for no failure).
  std::atomic<SlideOutcome> failure{SlideOutcome::NoPath};
  std::string error;

 private:
  // A live set and what it takes, as last noted.
  struct Entry {
    std::weak_ptr<Set> set;
    std::uint64_t memory = 0;
    std::uint64_t disk = 0;
  };

  std::pair<std::uint64_t, std::uint64_t> usedLocked() {
    std::uint64_t mem = 0;
    std::uint64_t disk = 0;
    std::size_t w = 0;
    for (std::size_t i = 0; i < live_.size(); i++) {
      if (live_[i].set.expired()) {
        continue;
      }
      live_[w++] = live_[i];
      mem += live_[i].memory;
      disk += live_[i].disk;
    }
    live_.resize(w);
    return {mem, disk};
  }

  SearchFolder& folder_;
  std::uint64_t budget_;
  std::uint64_t setCap_;
  std::mutex m_;
  std::mutex errorMutex_;
  std::vector<Entry> live_;
};

// Sets merged into one sorted stream, written as parts: a merge of
// `inputs` less what is in any of `subtract`; the first key also in
// `intersect` is reported (met); keys found in any of `filter` are dropped
// (the one-way arrangements found in the levels on disk). Each of the
// threads merges a range of keys, cut where the inputs' block index says,
// into a part of its own; the parts are sorted and disjoint, in order.
template <class Key>
struct Merge {
  std::vector<SetPtr<Key>> inputs;
  std::vector<SetPtr<Key>> subtract;
  SetPtr<Key> intersect;
  const std::vector<SetPtr<Key>>* filter = nullptr;

  std::vector<SetPtr<Key>> parts;
  std::optional<Key> met;
  std::uint64_t produced = 0;
  std::uint64_t dropped = 0;
};

// Inputs a merge takes at once: more are merged in stages first, so that a
// level of very many runs does not open them all together.
constexpr std::size_t kMaxMergeInputs = 32;
// Keys a merge thread looks up in the filter sets at a time.
constexpr std::size_t kFilterBatch = std::size_t{1} << 16;

template <class Key>
bool runMerge(Merge<Key>& m, SetStore<Key>& store, ThreadPool& pool, unsigned int threads,
              unsigned int maxParts = 0) {
  using Set = levels::KeySet<Key>;
  // Too many inputs: merged in groups into a few parts each first, so that
  // few files are open at once.
  while (m.inputs.size() > kMaxMergeInputs) {
    std::vector<SetPtr<Key>> merged;
    for (std::size_t i = 0; i < m.inputs.size(); i += kMaxMergeInputs) {
      Merge<Key> stage;
      const std::size_t end = std::min(i + kMaxMergeInputs, m.inputs.size());
      stage.inputs.assign(m.inputs.begin() + static_cast<std::ptrdiff_t>(i),
                          m.inputs.begin() + static_cast<std::ptrdiff_t>(end));
      if (!runMerge(stage, store, pool, threads, kMaxMergeInputs / 4)) {
        return false;
      }
      merged.insert(merged.end(), stage.parts.begin(), stage.parts.end());
    }
    m.inputs = std::move(merged);
  }
  std::uint64_t total = 0;
  std::uint64_t bytes = 0;
  std::uint64_t blocks = 0;
  for (const SetPtr<Key>& s : m.inputs) {
    if (!s->reload()) {
      store.ioFailed();
      return false;
    }
    total += s->size();
    bytes += s->bytes();
    blocks += s->blocks();
  }
  for (const SetPtr<Key>& s : m.subtract) {
    if (s && !s->reload()) {
      store.ioFailed();
      return false;
    }
  }
  if ((m.intersect && !m.intersect->reload())) {
    store.ioFailed();
    return false;
  }
  if (m.filter != nullptr) {
    for (const SetPtr<Key>& s : *m.filter) {
      if (!s->reload()) {
        store.ioFailed();
        return false;
      }
    }
  }
  if (total == 0) {
    return true;
  }
  const unsigned int parts = static_cast<unsigned int>(std::clamp<std::uint64_t>(
      total / (std::uint64_t{1} << 16), 1, maxParts != 0 ? std::min(maxParts, threads) : threads));
  // The cut points: quantiles of a sample of the inputs' block index.
  std::vector<Key> cuts;
  if (parts > 1) {
    std::vector<Key> sample;
    const std::size_t stride = static_cast<std::size_t>(std::max<std::uint64_t>(1, blocks / 4096));
    for (const SetPtr<Key>& s : m.inputs) {
      for (std::size_t b = 0; b < s->blocks(); b += stride) {
        sample.push_back(s->firstKey(b));
      }
    }
    std::sort(sample.begin(), sample.end());
    for (unsigned int p = 1; p < parts; p++) {
      cuts.push_back(sample[sample.size() * p / parts]);
    }
  }

  std::vector<SetPtr<Key>> out(parts);
  std::vector<std::optional<Key>> met(parts);
  std::vector<std::uint64_t> produced(parts, 0);
  std::vector<std::uint64_t> dropped(parts, 0);
  std::atomic<bool> failed{false};
  const std::uint64_t estimate = bytes / parts + 1;

  forEachIndex(pool, threads, parts, [&](std::size_t p) {
    const bool hasLo = p > 0;
    const bool hasHi = p + 1 < parts;
    const Key lo = hasLo ? cuts[p - 1] : Key{0};
    const Key hi = hasHi ? cuts[p] : Key{0};
    auto inRange = [&](Key k) { return !hasHi || k < hi; };

    // One cursor per input, the smallest key on top.
    struct In {
      std::unique_ptr<typename Set::Cursor> c;
      Key k = 0;
    };
    std::vector<In> ins(m.inputs.size());
    using Item = std::pair<Key, std::size_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<>> heap;
    for (std::size_t i = 0; i < m.inputs.size(); i++) {
      ins[i].c = hasLo ? std::make_unique<typename Set::Cursor>(*m.inputs[i], lo)
                       : std::make_unique<typename Set::Cursor>(*m.inputs[i]);
      if (ins[i].c->next(ins[i].k) && inRange(ins[i].k)) {
        heap.emplace(ins[i].k, i);
      }
    }
    // The streams to drop keys of, and the one to meet.
    struct Stream {
      std::unique_ptr<typename Set::Cursor> c;
      Key k = 0;
      bool has = false;
      void start(const Set& s, bool seek, Key from) {
        c = seek ? std::make_unique<typename Set::Cursor>(s, from)
                 : std::make_unique<typename Set::Cursor>(s);
        has = c->next(k);
      }
      // Whether k is in the stream, the stream moved on to it.
      bool holds(Key key) {
        while (has && k < key) {
          has = c->next(k);
        }
        return has && k == key;
      }
    };
    std::vector<Stream> subs;
    for (const SetPtr<Key>& s : m.subtract) {
      if (s && s->size() != 0) {
        subs.emplace_back();
        subs.back().start(*s, hasLo, lo);
      }
    }
    Stream inter;
    if (m.intersect && m.intersect->size() != 0) {
      inter.start(*m.intersect, hasLo, lo);
    }

    SetPtr<Key> set = store.make(estimate);
    if (!set) {
      failed.store(true);
      return;
    }
    // The filter: keys looked up in batches, so that each set is walked
    // once per batch.
    std::vector<std::unique_ptr<typename Set::Reader>> readers;
    if (m.filter != nullptr) {
      for (const SetPtr<Key>& s : *m.filter) {
        readers.push_back(std::make_unique<typename Set::Reader>(*s, std::size_t{1} << 12));
      }
    }
    std::vector<Key> batch;
    std::vector<char> found;
    auto flushBatch = [&]() {
      if (batch.empty()) {
        return true;
      }
      found.assign(batch.size(), 0);
      for (std::size_t f = 0; f < readers.size(); f++) {
        if (!readers[f]->ok() ||
            !(*m.filter)[f]->find(batch.data(), batch.size(), found.data(), *readers[f])) {
          return false;
        }
      }
      for (std::size_t i = 0; i < batch.size(); i++) {
        if (found[i] != 0) {
          dropped[p]++;
        } else {
          produced[p]++;
          if (!set->add(batch[i])) {
            return false;
          }
        }
      }
      batch.clear();
      return true;
    };

    bool ok = true;
    bool haveLast = false;
    Key last = 0;
    while (ok && !heap.empty()) {
      const Item top = heap.top();
      heap.pop();
      const std::size_t i = top.second;
      if (ins[i].c->next(ins[i].k) && inRange(ins[i].k)) {
        heap.emplace(ins[i].k, i);
      }
      const Key k = top.first;
      if (haveLast && k == last) {
        continue;
      }
      haveLast = true;
      last = k;
      bool drop = false;
      for (Stream& s : subs) {
        if (s.holds(k)) {
          drop = true;
          break;
        }
      }
      if (drop) {
        continue;
      }
      if (inter.c && !met[p] && inter.holds(k)) {
        met[p] = k;
      }
      if (m.filter != nullptr) {
        batch.push_back(k);
        if (batch.size() >= kFilterBatch) {
          ok = flushBatch();
        }
      } else {
        produced[p]++;
        ok = set->add(k);
      }
    }
    ok = ok && flushBatch();
    for (In& in : ins) {
      ok = ok && !in.c->failed();
    }
    for (Stream& s : subs) {
      ok = ok && !s.c->failed();
    }
    ok = ok && (!inter.c || !inter.c->failed()) && store.finish(set);
    if (!ok) {
      failed.store(true);
      return;
    }
    if (set->size() != 0) {
      set->release();
      store.refresh(set);
      out[p] = set;
    }
  });
  if (failed.load()) {
    if (store.failure == SlideOutcome::NoPath) {
      store.ioFailed();
    }
    return false;
  }
  for (unsigned int p = 0; p < parts; p++) {
    if (out[p]) {
      m.parts.push_back(out[p]);
    }
    if (met[p] && !m.met) {
      m.met = met[p];
    }
    m.produced += produced[p];
    m.dropped += dropped[p];
  }
  return true;
}

// The parts of a merge joined into one set (the parts freed); an empty set
// for none. Null when it cannot be made.
template <class Key>
SetPtr<Key> joinParts(std::vector<SetPtr<Key>>& parts, SetStore<Key>& store) {
  if (parts.size() == 1) {
    SetPtr<Key> one = std::move(parts[0]);
    parts.clear();
    return one;
  }
  std::uint64_t bytes = 0;
  for (const SetPtr<Key>& p : parts) {
    bytes += p->bytes();
  }
  SetPtr<Key> joined = store.make(bytes);
  if (!joined) {
    return nullptr;
  }
  for (SetPtr<Key>& p : parts) {
    if (!p->reload() || !joined->appendSet(*p)) {
      store.ioFailed();
      return nullptr;
    }
    p.reset();
  }
  parts.clear();
  if (!store.finish(joined)) {
    return nullptr;
  }
  return joined;
}

// ---------------------------------------------------------------------------
// Expanding a level: every thread takes blocks of the level, works out the
// moves of their arrangements and gathers what they reach in sorted pieces;
// the pieces are merged into runs (sets) as they outgrow the thread's share
// of the memory, and the runs are merged into the next level after.

// Keys a thread gathers before sorting them.
constexpr std::size_t kPiece = std::size_t{1} << 16;
// Blocks a thread takes from the level at a time.
constexpr std::size_t kBlocksPerGrab = 8;
// Arrangements in a level from which the search goes on from the goal too
// (SlideSearch::meetFrom 0).
constexpr std::uint64_t kDefaultMeetFrom = 1000000;

template <class Key>
class RunBuilder {
 public:
  using Set = levels::KeySet<Key>;

  RunBuilder(SetStore<Key>& store, std::uint64_t cap) : store_(store), cap_(cap) {
    raw_.reserve(kPiece);
  }
  ~RunBuilder() { store_.pieces.fetch_sub(bytes_, std::memory_order_relaxed); }
  RunBuilder(const RunBuilder&) = delete;
  RunBuilder& operator=(const RunBuilder&) = delete;

  bool emit(Key k) {
    raw_.push_back(k);
    return raw_.size() < kPiece || settle();
  }

  // The last pieces merged into a run.
  bool finishAll() { return settle() && flush(); }

  std::vector<SetPtr<Key>> runs;

 private:
  // Sorts the raw keys into a piece; merges the pieces into a run when they
  // fill the thread's share.
  bool settle() {
    if (raw_.empty()) {
      return true;
    }
    std::sort(raw_.begin(), raw_.end());
    raw_.erase(std::unique(raw_.begin(), raw_.end()), raw_.end());
    const std::uint64_t bytes = std::uint64_t{raw_.size()} * sizeof(Key);
    pieces_.emplace_back(raw_.begin(), raw_.end());
    raw_.clear();
    bytes_ += bytes;
    store_.pieces.fetch_add(bytes, std::memory_order_relaxed);
    return bytes_ < cap_ || flush();
  }

  bool flush() {
    if (pieces_.empty()) {
      return true;
    }
    std::uint64_t keys = 0;
    for (const std::vector<Key>& p : pieces_) {
      keys += p.size();
    }
    SetPtr<Key> set = store_.make(keys * 4);
    bool ok = set != nullptr;
    if (ok && pieces_.size() == 1) {
      ok = set->addAll(pieces_[0].begin(), pieces_[0].end());
    } else if (ok) {
      using Item = std::pair<Key, std::size_t>;
      std::priority_queue<Item, std::vector<Item>, std::greater<>> heap;
      std::vector<std::size_t> at(pieces_.size(), 0);
      for (std::size_t i = 0; i < pieces_.size(); i++) {
        heap.emplace(pieces_[i][0], i);
      }
      bool haveLast = false;
      Key last = 0;
      while (ok && !heap.empty()) {
        const Item top = heap.top();
        heap.pop();
        const std::size_t i = top.second;
        if (++at[i] < pieces_[i].size()) {
          heap.emplace(pieces_[i][at[i]], i);
        }
        if (!haveLast || top.first != last) {
          ok = set->add(top.first);
          last = top.first;
          haveLast = true;
        }
      }
    }
    ok = ok && store_.finish(set);
    if (ok) {
      set->release();
      store_.refresh(set);
    }
    pieces_.clear();
    store_.pieces.fetch_sub(bytes_, std::memory_order_relaxed);
    bytes_ = 0;
    if (!ok) {
      if (store_.failure == SlideOutcome::NoPath) {
        store_.ioFailed();
      }
      return false;
    }
    if (set->size() != 0) {
      runs.push_back(set);
    }
    return true;
  }

  SetStore<Key>& store_;
  std::uint64_t cap_;
  std::vector<Key> raw_;
  std::vector<std::vector<Key>> pieces_;
  std::uint64_t bytes_ = 0;
};

// What expanding a level came to.
template <class Key>
struct Expanded {
  std::vector<SetPtr<Key>> runs;
  std::vector<SetPtr<Key>> oneWay;
  bool hit = false;
  bool halted = false;
  bool failed = false;
};

// ---------------------------------------------------------------------------
// The saved search: the levels it stands on, in the folder, and state.bin
// naming them. A search stopped before its end is saved when it has a
// folder; a search of the same start carries it on.

constexpr std::array<char, 8> kStateMagic = {'B', 'T', 'S', 'L', 'S', 'V', '0', '1'};

struct SavedSide {
  unsigned int depth = 0;
  std::string prev;
  std::string cur;
  std::vector<std::pair<unsigned int, std::string>> stored;
};

struct SavedSearch {
  std::uint64_t problemKey = 0;
  std::uint64_t searchKey = 0;
  std::uint64_t visited = 0;
  std::uint64_t oneWayRepeats = 0;
  unsigned int interval = 1;
  unsigned int fileNumber = 0;
  bool nested = false;
  bool bidirectional = false;
  SavedSide side[2];
  std::vector<std::string> visitedSets;
};

template <class T>
void put(std::ofstream& out, const T& v) {
  out.write(reinterpret_cast<const char*>(&v), sizeof(T));
}
void putString(std::ofstream& out, const std::string& s) {
  put(out, static_cast<std::uint32_t>(s.size()));
  out.write(s.data(), static_cast<std::streamsize>(s.size()));
}
template <class T>
void get(std::ifstream& in, T& v) {
  in.read(reinterpret_cast<char*>(&v), sizeof(T));
}
bool getString(std::ifstream& in, std::string& s) {
  std::uint32_t n = 0;
  get(in, n);
  if (!in || n > 64) {
    return false;
  }
  s.resize(n);
  in.read(s.data(), n);
  return static_cast<bool>(in);
}

bool writeState(const std::filesystem::path& dir, const SavedSearch& st) {
  const std::filesystem::path tmp = dir / "state.tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out.write(kStateMagic.data(), kStateMagic.size());
    put(out, st.problemKey);
    put(out, st.searchKey);
    put(out, st.visited);
    put(out, st.oneWayRepeats);
    put(out, st.interval);
    put(out, st.fileNumber);
    put(out, static_cast<std::uint8_t>(st.nested ? 1 : 0));
    put(out, static_cast<std::uint8_t>(st.bidirectional ? 1 : 0));
    for (const SavedSide& s : st.side) {
      put(out, s.depth);
      putString(out, s.prev);
      putString(out, s.cur);
      put(out, static_cast<std::uint32_t>(s.stored.size()));
      for (const auto& [level, name] : s.stored) {
        put(out, level);
        putString(out, name);
      }
    }
    put(out, static_cast<std::uint32_t>(st.visitedSets.size()));
    for (const std::string& name : st.visitedSets) {
      putString(out, name);
    }
    if (!out) {
      return false;
    }
  }
  // Replace the old state only once the new one is whole.
  std::error_code ec;
  std::filesystem::rename(tmp, dir / "state.bin", ec);
  return !ec;
}

// With `whole` false only the keys, enough to tell whose search it is.
bool readState(const std::filesystem::path& dir, SavedSearch& st, bool whole) {
  std::ifstream in(dir / "state.bin", std::ios::binary);
  std::array<char, kStateMagic.size()> magic{};
  in.read(magic.data(), magic.size());
  if (!in || magic != kStateMagic) {
    return false;
  }
  get(in, st.problemKey);
  get(in, st.searchKey);
  if (!whole) {
    return static_cast<bool>(in);
  }
  get(in, st.visited);
  get(in, st.oneWayRepeats);
  get(in, st.interval);
  get(in, st.fileNumber);
  std::uint8_t nested = 0;
  std::uint8_t bidirectional = 0;
  get(in, nested);
  get(in, bidirectional);
  st.nested = nested != 0;
  st.bidirectional = bidirectional != 0;
  for (SavedSide& s : st.side) {
    get(in, s.depth);
    std::uint32_t n = 0;
    if (!getString(in, s.prev) || !getString(in, s.cur)) {
      return false;
    }
    get(in, n);
    if (!in || n > 1000000) {
      return false;
    }
    s.stored.resize(n);
    for (auto& [level, name] : s.stored) {
      get(in, level);
      if (!getString(in, name)) {
        return false;
      }
    }
  }
  std::uint32_t n = 0;
  get(in, n);
  if (!in || n > 100000) {
    return false;
  }
  st.visitedSets.resize(n);
  for (std::string& name : st.visitedSets) {
    if (!getString(in, name)) {
      return false;
    }
  }
  return static_cast<bool>(in) && st.interval != 0;
}

// ---------------------------------------------------------------------------
// The full slide search: breadth first, one level at a time, a level being
// every arrangement a given number of moves from the start.
//
//  - Every level is a sorted KeySet (slide_levels.h), delta coded: in
//    memory while the memory budget allows, else a file in the search's
//    folder, so the search is limited by the disk rather than by memory.
//    Only the level being expanded and the one before are needed as the
//    next is made; the threads gather what the moves reach in sorted runs
//    (sets too), and a merge of the runs less those two levels, each
//    thread a range of keys, writes the next level.
//  - A move that one piece makes can always be made backwards, and so can a
//    nested group's unless the group closes round a piece on its way. An
//    arrangement reached by a move that can be made backwards is new unless
//    it is in this level or the one before: had it been reached earlier,
//    the arrangement it came from would have been reached by now too. So
//    only the arrangements reached by the other moves are looked for in the
//    older levels, kept merged into a few sorted sets.
//  - When the goal is one arrangement and a level has grown wide, the
//    search goes on from the goal as well, always growing the narrower end,
//    until the two ends meet (the Panex search's way): the path is as
//    short, and far fewer arrangements are visited.
//  - The older levels are kept for tracing the path back. Without nested
//    slides, when they outgrow the disk every other one goes (the gaps are
//    filled, when the path is traced, by a small search out from the
//    arrangement at hand); with nested slides every level is needed.
//  - A search stopped before its end is saved in its folder, and a search
//    of the same start carries it on.
//
// It finds a path with the fewest moves, the same every run (legacySearch
// finds one as short, though not always the same one).

template <class Key>
class LevelSearch {
 public:
  using Set = levels::KeySet<Key>;

  LevelSearch(const SlideSpace<Key>& space, SlideSearch& search, std::uint64_t problemKey,
              std::uint64_t searchKey)
      : s_(space),
        search_(search),
        problemKey_(problemKey),
        searchKey_(searchKey),
        folder_(search.workDir, searchKey),
        budget_(search.maxMemoryStates != 0
                    ? search.maxMemoryStates * kLevelStateKeys * sizeof(Key)
                    : memoryBudget(search.memoryBytes, search.highMemory)),
        store_(folder_, budget_,
               search.memoryLevelBytes != 0 ? search.memoryLevelBytes : budget_) {}

  // The search; chain gets the path's arrangements, start first, when the
  // goal is found. search_.outcome says how it ended.
  void run(std::vector<Key>& chain) {
    if (search_.pool == nullptr) {
      ownPool_ = std::make_unique<ThreadPool>(
          search_.threads != 0 ? std::min(search_.threads, kMaxThreads) : hardwareThreads());
      pool_ = ownPool_.get();
    } else {
      pool_ = search_.pool;
    }
    threads_ = std::max(
        1U, search_.threads != 0 ? std::min(search_.threads, pool_->cores()) : pool_->cores());
    for (unsigned int i = 0; i <= threads_; i++) {
      movers_.push_back(std::make_unique<SlideMover<Key>>(s_));
    }
    search_.memoryStates =
        search_.maxMemoryStates != 0
            ? search_.maxMemoryStates
            : memoryStates(kLevelStateKeys * sizeof(Key), search_.highMemory, search_.memoryBytes);
    search_.depth = 0;
    search_.diskBytes = 0;
    search_.oneWayRepeats = 0;
    search_.levelInterval = 1;
    search_.saved = false;
    search_.resumed = false;
    search_.outcome = SlideOutcome::NoPath;
    goal_ = singleGoal();
    if (!s_.nested && search_.keepEvery > 1) {
      interval_ = search_.keepEvery;
    }

    if (!start()) {
      return;
    }
    const SlideOutcome end = loop(chain);
    search_.outcome = end;
    search_.depth = std::uint64_t{fwd_.depth} + bwd_.depth;
    search_.levelInterval = interval_;
    search_.visited = visited_;
    if (search_.progress != nullptr) {
      search_.progress->store(visited_, std::memory_order_relaxed);
    }
    search_.diskBytes = std::max(search_.diskBytes, store_.used().second);
    // A search that stopped short is saved for a search of the same start
    // to carry on: when paused, and when it filled its disk budget or the
    // disk (it stops with room left to save itself, so the user can raise
    // the budget or free space and carry on); not after a write failed.
    const bool save = end == SlideOutcome::Stopped ||
                      (end == SlideOutcome::Disk && diskChosen_ &&
                       store_.failure != SlideOutcome::Disk);
    folder_.keep = false;
    if (save && saveState()) {
      search_.saved = true;
      folder_.keep = true;
      // the files stay: the sets must not delete them as they go
      forEachLiveSet([](SetPtr<Key>& set) {
        if (set) {
          set->keepFile();
        }
      });
      pinned_.clear();
    }
  }

 private:
  // One end of the search: its two newest levels, how deep it is, and the
  // older levels kept for tracing the path back, by level.
  struct Side {
    SetPtr<Key> prev;
    SetPtr<Key> cur;
    unsigned int depth = 0;
    std::vector<std::pair<unsigned int, SetPtr<Key>>> stored;
  };

  enum class StepEnd : std::uint8_t { Next, Hit, Stopped, Failed };

  // The goal when it is one arrangement, canonical: then the search can
  // run from both ends. None with pieces that may end in several places,
  // copies that count together, or a goal where a piece may not stop.
  std::optional<Key> singleGoal() const {
    if (s_.goals != nullptr && s_.goals->counts()) {
      return std::nullopt;
    }
    Key g = 0;
    for (unsigned int i = 0; i < s_.n; i++) {
      const PosTable& t = s_.t[i];
      int at = -1;
      for (std::size_t p = 0; p < t.atGoal.size(); p++) {
        if (t.atGoal[p] != 0) {
          if (at >= 0) {
            return std::nullopt;
          }
          at = static_cast<int>(p);
        }
      }
      if (at < 0 || t.rest[idx(at)] == 0) {
        return std::nullopt;
      }
      g |= static_cast<Key>(at) << t.shift;
    }
    return s_.canon(g);
  }

  // A set of one key.
  SetPtr<Key> single(Key k) {
    SetPtr<Key> set = store_.make(sizeof(Key));
    if (!set || !set->add(k) || !store_.finish(set)) {
      if (store_.failure == SlideOutcome::NoPath) {
        store_.ioFailed();
      }
      return nullptr;
    }
    return set;
  }

  // The search's beginning: the saved search carried on, or the start.
  bool start() {
    if (search_.resume && folder_.exists()) {
      if (resume()) {
        search_.resumed = true;
        return true;
      }
      folder_.wipe();
    } else if (folder_.exists()) {
      folder_.wipe();
    }
    fwd_.cur = single(s_.startCanon);
    if (!fwd_.cur) {
      search_.outcome = store_.failure;
      search_.error = store_.error;
      return false;
    }
    fwd_.stored.emplace_back(0, fwd_.cur);
    return true;
  }

  // Takes up the saved search in the folder; false when it is not this
  // search's or is damaged.
  bool resume() {
    SavedSearch st;
    if (!readState(folder_.place(), st, true) || st.searchKey != searchKey_ ||
        st.problemKey != problemKey_ || st.nested != s_.nested) {
      return false;
    }
    if (!folder_.open()) {
      return false;
    }
    std::map<std::string, SetPtr<Key>> opened;
    auto openSet = [&](const std::string& name, SetPtr<Key>& into) {
      if (name.empty()) {
        into.reset();
        return true;
      }
      const auto it = opened.find(name);
      if (it != opened.end()) {
        into = it->second;
        return true;
      }
      auto set = std::make_shared<Set>();
      if (!set->open(folder_.dir / name)) {
        set->keepFile();
        return false;
      }
      store_.adopt(set);
      opened.emplace(name, set);
      into = set;
      return true;
    };
    Side* sides[2] = {&fwd_, &bwd_};
    for (int i = 0; i < 2; i++) {
      Side& side = *sides[i];
      const SavedSide& saved = st.side[i];
      side.depth = saved.depth;
      if (!openSet(saved.prev, side.prev) || !openSet(saved.cur, side.cur)) {
        return false;
      }
      for (const auto& [level, name] : saved.stored) {
        SetPtr<Key> set;
        if (!openSet(name, set)) {
          return false;
        }
        side.stored.emplace_back(level, set);
      }
    }
    for (const std::string& name : st.visitedSets) {
      SetPtr<Key> set;
      if (!openSet(name, set)) {
        return false;
      }
      visitedSets_.push_back(set);
    }
    if (!fwd_.cur || (st.bidirectional && !bwd_.cur)) {
      return false;
    }
    bidirectional_ = st.bidirectional;
    if (bidirectional_ && !goal_) {
      return false;
    }
    interval_ = st.interval;
    visited_ = st.visited;
    search_.oneWayRepeats = st.oneWayRepeats;
    store_.fileNumber = st.fileNumber;
    // files the state does not name are leftovers of the stopped run
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(folder_.dir, ec)) {
      const std::string name = e.path().filename().string();
      if (name != "state.bin" && opened.find(name) == opened.end()) {
        std::filesystem::remove(e.path(), ec);
      }
    }
    for (const SetPtr<Key>& set : visitedSets_) {
      (void)set->reload();
    }
    // chosen once the saved sets are open, so that they count as its own
    setDiskBudget();
    return true;
  }

  // Every set the search stands on, by reference.
  template <class F>
  void forEachLiveSet(F f) {
    for (Side* side : {&fwd_, &bwd_}) {
      f(side->prev);
      f(side->cur);
      for (auto& [level, set] : side->stored) {
        f(set);
      }
    }
    for (SetPtr<Key>& set : visitedSets_) {
      f(set);
    }
  }

  // Writes the search's state to its folder, every set a file; false when
  // it cannot be.
  bool saveState() {
    if (!folder_.open()) {
      return false;
    }
    bool ok = true;
    std::map<const Set*, SetPtr<Key>> written;
    forEachLiveSet([&](SetPtr<Key>& set) {
      if (!ok || !set) {
        return;
      }
      // a set shared between lists (a level kept and merged) is written once
      const auto it = written.find(set.get());
      if (it != written.end()) {
        set = it->second;
        return;
      }
      const Set* was = set.get();
      ok = store_.toFile(set);
      written.emplace(was, set);
    });
    if (!ok) {
      return false;
    }
    SavedSearch st;
    st.problemKey = problemKey_;
    st.searchKey = searchKey_;
    st.visited = visited_;
    st.oneWayRepeats = search_.oneWayRepeats;
    st.interval = interval_;
    st.fileNumber = store_.fileNumber;
    st.nested = s_.nested;
    st.bidirectional = bidirectional_;
    auto nameOf = [](const SetPtr<Key>& set) {
      return set ? set->path().filename().string() : std::string();
    };
    Side* sides[2] = {&fwd_, &bwd_};
    for (int i = 0; i < 2; i++) {
      st.side[i].depth = sides[i]->depth;
      st.side[i].prev = nameOf(sides[i]->prev);
      st.side[i].cur = nameOf(sides[i]->cur);
      for (const auto& [level, set] : sides[i]->stored) {
        st.side[i].stored.emplace_back(level, nameOf(set));
      }
    }
    for (const SetPtr<Key>& set : visitedSets_) {
      st.visitedSets.push_back(nameOf(set));
    }
    if (!writeState(folder_.dir, st)) {
      return false;
    }
    // The files the state names stay until the next save replaces them,
    // whatever the search does with the sets meanwhile.
    pinned_.clear();
    forEachLiveSet([&](SetPtr<Key>& set) {
      if (set) {
        pinned_.push_back(set);
      }
    });
    return true;
  }

  // The disk the levels may take, chosen when the folder is made, from the
  // space free there (diskBudgetFor): a budget given is held to it, and
  // none given is half of it.
  void setDiskBudget() {
    if (!diskChosen_ && !folder_.dir.empty()) {
      // the files there already (a saved search carried on) are the
      // search's own: they count as free space it may take again
      disk_ = diskBudgetFor(folder_.dir, search_.diskBudget, store_.used().second);
      diskChosen_ = true;
    }
  }

  // The outcome when a set could not be made.
  SlideOutcome failed() {
    search_.error = store_.error;
    const SlideOutcome why = store_.failure.load();
    return why == SlideOutcome::NoPath ? SlideOutcome::Memory : why;
  }

  // Expands `cur` on every thread; `extra` runs on one thread first (the
  // merge of the level that left memory into the visited sets).
  Expanded<Key> expand(Set& cur, bool checkGoal, bool mayStop,
                       const std::function<bool()>& extra) {
    Expanded<Key> e;
    if (!cur.reload()) {
      store_.ioFailed();
      e.failed = true;
      return e;
    }
    const std::uint64_t cap =
        std::max<std::uint64_t>(kPiece * sizeof(Key), budget_ / 2 / (threads_ + 1));
    std::vector<std::unique_ptr<RunBuilder<Key>>> back;
    std::vector<std::unique_ptr<RunBuilder<Key>>> oneWay;
    for (unsigned int i = 0; i <= threads_; i++) {
      back.push_back(std::make_unique<RunBuilder<Key>>(store_, cap));
      oneWay.push_back(std::make_unique<RunBuilder<Key>>(store_, cap));
    }
    std::atomic<std::size_t> nextBlock{0};
    std::atomic<bool> hit{false};
    std::atomic<bool> halt{false};
    std::atomic<bool> failed{false};
    const bool withExtra = static_cast<bool>(extra);
    pool_->run(threads_ + (withExtra ? 1 : 0), [&](unsigned int th) {
      if (th == 0 && withExtra && !extra()) {
        failed.store(true);
      }
      RunBuilder<Key>& b = *back[th];
      RunBuilder<Key>& o = *oneWay[th];
      SlideMover<Key>& mover = *movers_[th];
      typename Set::Reader reader(cur);
      std::vector<Key> keys;
      bool localFail = false;
      auto emit = [&](Key raw, bool group) {
        const Key k = s_.canon(raw);
        if (checkGoal && s_.isGoal(k)) {
          return true;
        }
        const bool ok = (group && !mover.undoable(raw)) ? o.emit(k) : b.emit(k);
        if (!ok) {
          localFail = true;
        }
        return !ok;
      };
      const std::size_t blocks = cur.blocks();
      while (!hit.load(std::memory_order_relaxed) && !halt.load(std::memory_order_relaxed) &&
             !failed.load(std::memory_order_relaxed)) {
        const std::size_t b0 = nextBlock.fetch_add(kBlocksPerGrab);
        if (b0 >= blocks) {
          break;
        }
        const std::size_t b1 = std::min(b0 + kBlocksPerGrab, blocks);
        std::uint64_t n = 0;
        for (std::size_t blk = b0; blk < b1; blk++) {
          if (!reader.block(blk, keys)) {
            store_.ioFailed();
            failed.store(true);
            break;
          }
          for (const Key k : keys) {
            if (mover.expand(k, emit)) {
              if (localFail) {
                failed.store(true);
              } else {
                hit.store(true);
              }
              break;
            }
          }
          n += keys.size();
          if (hit.load(std::memory_order_relaxed) || failed.load(std::memory_order_relaxed)) {
            break;
          }
        }
        const std::uint64_t v = visited_.fetch_add(n) + n;
        if (search_.progress != nullptr) {
          search_.progress->store(v, std::memory_order_relaxed);
        }
        if (mayStop && search_.stop.stopRequested()) {
          halt.store(true);
        }
      }
      if (!hit.load() && !halt.load() && !failed.load()) {
        if (!b.finishAll() || !o.finishAll()) {
          failed.store(true);
        }
      }
    });
    e.hit = hit.load();
    e.halted = halt.load();
    e.failed = failed.load();
    if (e.hit || e.halted || e.failed) {
      return e;
    }
    for (unsigned int i = 0; i <= threads_; i++) {
      for (SetPtr<Key>& r : back[i]->runs) {
        e.runs.push_back(std::move(r));
      }
      for (SetPtr<Key>& r : oneWay[i]->runs) {
        e.oneWay.push_back(std::move(r));
      }
    }
    return e;
  }

  // The level after side.cur into next: everything one move from it that is
  // in neither it nor side.prev (nor, by a one-way move, in any older
  // level). With `intersect`, the first arrangement of next also in it
  // (met). oneWayReached and oneWayNew count the level's one-way moves.
  StepEnd step(Side& side, const SetPtr<Key>& intersect, bool checkGoal, bool mayStop,
               const std::function<bool()>& extra, SetPtr<Key>& next, std::optional<Key>& met,
               std::uint64_t& oneWayReached, std::uint64_t& oneWayNew) {
    Expanded<Key> e = expand(*side.cur, checkGoal, mayStop, extra);
    if (e.failed) {
      return StepEnd::Failed;
    }
    if (e.hit) {
      return StepEnd::Hit;
    }
    if (e.halted) {
      return StepEnd::Stopped;
    }
    oneWayReached = 0;
    oneWayNew = 0;
    std::vector<SetPtr<Key>> runs = std::move(e.runs);
    if (!e.oneWay.empty()) {
      // The arrangements reached only by moves that cannot be made
      // backwards may be anywhere in the older levels.
      Merge<Key> one;
      one.inputs = std::move(e.oneWay);
      one.subtract = {side.cur, side.prev};
      one.filter = &visitedSets_;
      if (!runMerge(one, store_, *pool_, threads_)) {
        return StepEnd::Failed;
      }
      oneWayReached = one.produced + one.dropped;
      oneWayNew = one.produced;
      for (SetPtr<Key>& p : one.parts) {
        runs.push_back(std::move(p));
      }
    }
    Merge<Key> m;
    m.inputs = std::move(runs);
    m.subtract = {side.cur, side.prev};
    m.intersect = intersect;
    if (!runMerge(m, store_, *pool_, threads_)) {
      return StepEnd::Failed;
    }
    met = m.met;
    if (m.parts.empty()) {
      next = store_.make(0);
      if (!next || !store_.finish(next)) {
        return StepEnd::Failed;
      }
      return StepEnd::Next;
    }
    next = joinParts(m.parts, store_);
    return next ? StepEnd::Next : StepEnd::Failed;
  }

  // The level that left the two newest: kept for tracing the path back
  // when it is one the interval keeps (every level with nested slides,
  // which also go into the visited sets), else freed.
  void ageOut(Side& side, SetPtr<Key> old, unsigned int level) {
    if (!old || level == 0) {
      return;
    }
    if (s_.nested) {
      side.stored.emplace_back(level, old);
      pendingVisited_ = old;
    } else if (level % interval_ == 0) {
      // only kept for tracing the path back: no index till then
      old->release();
      store_.refresh(old);
      side.stored.emplace_back(level, old);
    }
  }

  // Merges a level into the visited sets: the newest sets while one is no
  // more than twice the size of the one after it, so there are only a few
  // to look in (runs on one thread while the others expand).
  bool insertVisited(const SetPtr<Key>& newest) {
    visitedSets_.push_back(newest);
    while (visitedSets_.size() >= 2 &&
           visitedSets_[visitedSets_.size() - 2]->size() <= 2 * visitedSets_.back()->size()) {
      const SetPtr<Key> a = visitedSets_[visitedSets_.size() - 2];
      const SetPtr<Key> b = visitedSets_.back();
      visitedSets_.resize(visitedSets_.size() - 2);
      if (!a->reload() || !b->reload()) {
        store_.ioFailed();
        return false;
      }
      SetPtr<Key> m = store_.make(a->bytes() + b->bytes());
      if (!m) {
        return false;
      }
      typename Set::Cursor ca(*a);
      typename Set::Cursor cb(*b);
      Key ka = 0;
      Key kb = 0;
      bool ha = ca.next(ka);
      bool hb = cb.next(kb);
      bool ok = true;
      while (ok && (ha || hb)) {
        if (!hb || (ha && ka < kb)) {
          ok = m->add(ka);
          ha = ca.next(ka);
        } else if (!ha || kb < ka) {
          ok = m->add(kb);
          hb = cb.next(kb);
        } else {
          ok = m->add(ka);
          ha = ca.next(ka);
          hb = cb.next(kb);
        }
      }
      if (!ok || ca.failed() || cb.failed() || !store_.finish(m)) {
        store_.ioFailed();
        return false;
      }
      visitedSets_.push_back(m);
    }
    // a level only kept for tracing the path needs no index till then
    for (Side* side : {&fwd_, &bwd_}) {
      for (auto& [level, set] : side->stored) {
        if (set.use_count() == 1) {
          set->release();
          store_.refresh(set);
        }
      }
    }
    return true;
  }

  // Keeps every other level kept for tracing back (the gaps are searched
  // when the path is traced): level 0 is a multiple of any interval.
  void thin() {
    interval_ *= 2;
    for (Side* side : {&fwd_, &bwd_}) {
      std::erase_if(side->stored, [&](const std::pair<unsigned int, SetPtr<Key>>& e) {
        return e.first % interval_ != 0;
      });
    }
  }

  // Bytes the levels kept for tracing back take on disk.
  std::uint64_t storedDiskBytes() {
    std::uint64_t bytes = 0;
    for (Side* side : {&fwd_, &bwd_}) {
      for (const auto& [level, set] : side->stored) {
        if (!set->inMemory()) {
          bytes += set->bytes();
        }
      }
    }
    return bytes;
  }

  // In-memory sets no longer worked on are written to files while the
  // sets take more than half the memory they may, so that the next level
  // has room (at a level's end, on one thread).
  bool evict() {
    for (;;) {
      if (store_.used().first <= store_.setCap() / 2) {
        return true;
      }
      // nowhere to write: the sets stay where they are
      if (!folder_.open()) {
        return true;
      }
      SetPtr<Key>* largest = nullptr;
      for (Side* side : {&fwd_, &bwd_}) {
        for (auto& [level, set] : side->stored) {
          if (set->inMemory() && set != side->cur && set != side->prev &&
              (largest == nullptr || set->bytes() > (*largest)->bytes())) {
            largest = &set;
          }
        }
      }
      for (SetPtr<Key>& set : visitedSets_) {
        if (set->inMemory() && (largest == nullptr || set->bytes() > (*largest)->bytes())) {
          largest = &set;
        }
      }
      if (largest == nullptr) {
        return true;
      }
      const Set* was = largest->get();
      if (!store_.toFile(*largest)) {
        return false;
      }
      // the same set may stand in a stored list and in the visited sets
      forEachLiveSet([&](SetPtr<Key>& set) {
        if (set.get() == was) {
          set = *largest;
        }
      });
    }
  }

  // The main loop: a level at a time, from the narrower end.
  SlideOutcome loop(std::vector<Key>& chain) {
    const auto started = std::chrono::steady_clock::now();
    auto lastSave = started;
    const std::uint64_t meetFrom = search_.meetFrom != 0 ? search_.meetFrom : kDefaultMeetFrom;
    const auto depthNow = [&]() { return std::uint64_t{fwd_.depth} + bwd_.depth; };
    if (search_.depthProgress != nullptr) {
      search_.depthProgress->store(depthNow(), std::memory_order_relaxed);
    }
    for (;;) {
      if (search_.stop.stopRequested()) {
        return SlideOutcome::Stopped;
      }
      Side& side = (bidirectional_ && bwd_.cur->size() < fwd_.cur->size()) ? bwd_ : fwd_;
      Side& other = (&side == &fwd_) ? bwd_ : fwd_;
      std::function<bool()> extra;
      if (pendingVisited_) {
        SetPtr<Key> level = pendingVisited_;
        pendingVisited_.reset();
        extra = [this, level]() { return insertVisited(level); };
      }
      SetPtr<Key> next;
      std::optional<Key> met;
      std::uint64_t oneWayReached = 0;
      std::uint64_t oneWayNew = 0;
      // a level cut short is expanded again when the search is carried on
      const std::uint64_t visitedBefore = visited_.load();
      const StepEnd end = step(side, bidirectional_ ? other.cur : nullptr, !bidirectional_, true,
                               extra, next, met, oneWayReached, oneWayNew);
      if (end == StepEnd::Failed) {
        visited_.store(visitedBefore);
        return failed();
      }
      if (end == StepEnd::Stopped) {
        visited_.store(visitedBefore);
        return SlideOutcome::Stopped;
      }
      if (end == StepEnd::Hit) {
        return goalFound(chain) ? SlideOutcome::Found : failed();
      }
      search_.oneWayRepeats += oneWayReached - oneWayNew;
      // prev <- cur <- next; the old prev leaves the newest two
      const SetPtr<Key> old = side.prev;
      side.prev = side.cur;
      side.cur = next;
      side.depth++;
      if (side.depth >= 2) {
        ageOut(side, old, side.depth - 2);
      }
      if (search_.depthProgress != nullptr) {
        search_.depthProgress->store(depthNow(), std::memory_order_relaxed);
      }
      if (next->size() == 0) {
        // Everything this end can reach is searched without finding the
        // goal, or meeting the other end.
        return SlideOutcome::NoPath;
      }
      if (met) {
        return meetingFound(*met, chain) ? SlideOutcome::Found : failed();
      }

      // The disk: when the levels kept for tracing back leave too little
      // for the next level's runs, every other one goes (not with nested
      // slides, which need them all); then what the search takes in all
      // must fit the budget.
      std::pair<std::uint64_t, std::uint64_t> used = store_.used();
      search_.diskBytes = std::max(search_.diskBytes, used.second);
      if (!folder_.dir.empty()) {
        setDiskBudget();
      }
      if (diskChosen_ && !s_.nested) {
        const std::uint64_t level = next->bytes();
        while (interval_ < (1U << 30) && storedDiskBytes() + 5 * level > disk_.bytes &&
               storedDiskBytes() > level) {
          thin();
          releaseFreeMemory();
        }
        used = store_.used();
      }
      if (diskChosen_ && used.second > disk_.bytes) {
        char gb[32];
        std::snprintf(gb, sizeof(gb), "%.1f GB", static_cast<double>(disk_.bytes) / 1e9);
        const std::string reason = diskBudgetReason(disk_);
        search_.diskHeldToFree = disk_.heldToFree;
        search_.error = "The search filled the disk space it may use (" + std::string(gb) +
                        " in " + folder_.dir.parent_path().string() +
                        (reason.empty() ? "" : ", " + reason) + ").";
        return SlideOutcome::Disk;
      }
      // The disk itself: other programs take from it too, so the search
      // stops while there is still room to save itself (its sets in memory
      // written out) and to finish the next level's writes.
      if (diskChosen_) {
        const std::optional<std::uint64_t> free = availableDiskBytes(folder_.dir);
        const std::uint64_t need = search_.diskReserve + used.first + 5 * next->bytes();
        if (free && *free < need) {
          char gb[32];
          std::snprintf(gb, sizeof(gb), "%.1f GB", static_cast<double>(*free) / 1e9);
          search_.diskHeldToFree = true;
          search_.error = "The disk is nearly full (" + std::string(gb) + " free in " +
                          folder_.dir.parent_path().string() +
                          "): the search stopped while it could still save itself.";
          return SlideOutcome::Disk;
        }
      }
      if (!evict()) {
        return failed();
      }

      // From the goal too, once a level is wide and the goal is one
      // arrangement: the goal cannot be any nearer than the levels so far.
      if (!bidirectional_ && goal_ && !s_.nested && fwd_.cur->size() >= meetFrom) {
        bwd_.cur = single(*goal_);
        if (!bwd_.cur) {
          return failed();
        }
        bwd_.stored.emplace_back(0, bwd_.cur);
        bidirectional_ = true;
      }

      if (search_.onLevel) {
        char line[320];
        std::snprintf(
            line, sizeof(line),
            "%s level %u: %zu arrangements, one-way %llu, %llu new, %zu sets, interval %u, "
            "disk %.1f MB, memory %.1f MB, %.1f s",
            &side == &fwd_ ? "start" : "goal", side.depth, next->size(),
            static_cast<unsigned long long>(oneWayReached),
            static_cast<unsigned long long>(oneWayNew), visitedSets_.size(), interval_,
            static_cast<double>(used.second) / 1e6, static_cast<double>(used.first) / 1e6,
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
        search_.onLevel(line);
      }
      releaseFreeMemory();

      // An autosave, so a crash loses no more than this.
      if (search_.autosaveMinutes != 0 && folder_.hasBase() &&
          std::chrono::steady_clock::now() - lastSave >=
              std::chrono::minutes(search_.autosaveMinutes)) {
        if (!saveState()) {
          return failed();
        }
        lastSave = std::chrono::steady_clock::now();
      }
    }
  }

  // The level `level` of a side, when the search still has it.
  SetPtr<Key> levelAt(const Side& side, unsigned int level) const {
    if (level == side.depth) {
      return side.cur;
    }
    if (level + 1 == side.depth) {
      return side.prev;
    }
    for (const auto& [l, set] : side.stored) {
      if (l == level) {
        return set;
      }
    }
    return nullptr;
  }

  // An arrangement of `before` one move from x, the first in the order the
  // moves are tried, by moving backwards from x; failing that (x reached
  // only by moves that cannot be made backwards) the first arrangement of
  // `before` that reaches x.
  bool stepBack(Key x, const SetPtr<Key>& before, Key& y) {
    if (!before->reload()) {
      store_.ioFailed();
      return false;
    }
    SlideMover<Key>& mover = *movers_[0];
    std::vector<Key> cand;
    mover.expand(x, [&](Key raw, bool group) {
      if (!group || mover.undoable(raw)) {
        cand.push_back(s_.canon(raw));
      }
      return false;
    });
    std::vector<Key> sorted(cand);
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    std::vector<char> found(sorted.size(), 0);
    typename Set::Reader reader(*before, std::size_t{1} << 12);
    if (!reader.ok() || !before->find(sorted.data(), sorted.size(), found.data(), reader)) {
      store_.ioFailed();
      return false;
    }
    for (const Key c : cand) {
      const auto at = static_cast<std::size_t>(
          std::lower_bound(sorted.begin(), sorted.end(), c) - sorted.begin());
      if (found[at] != 0) {
        y = c;
        return true;
      }
    }
    // Try every arrangement of the level before, keeping the first.
    std::mutex bestMutex;
    std::optional<Key> best;
    std::atomic<bool> readFailed{false};
    std::atomic<std::size_t> nextBlock{0};
    const std::size_t blocks = before->blocks();
    pool_->run(threads_, [&](unsigned int th) {
      SlideMover<Key>& m = *movers_[th];
      typename Set::Reader r(*before);
      std::vector<Key> keys;
      for (;;) {
        const std::size_t b = nextBlock.fetch_add(1);
        if (b >= blocks || readFailed.load()) {
          break;
        }
        {
          const std::lock_guard<std::mutex> lock(bestMutex);
          if (best && before->firstKey(b) > *best) {
            break;
          }
        }
        if (!r.block(b, keys)) {
          readFailed.store(true);
          break;
        }
        for (const Key k : keys) {
          if (m.expand(k, [&](Key raw, bool) { return s_.canon(raw) == x; })) {
            const std::lock_guard<std::mutex> lock(bestMutex);
            if (!best || k < *best) {
              best = k;
            }
            break;
          }
        }
      }
    });
    if (readFailed.load()) {
      store_.ioFailed();
      return false;
    }
    // there always is one; without it there is no path to give
    BT_ASSERT(best.has_value());
    y = *best;
    return true;
  }

  // The path from x, `level` moves from this end, back to the end: each
  // arrangement of the level before, through the levels kept, searching
  // out from x across the gaps where levels were thinned.
  bool traceSide(Side& side, Key x, unsigned int level, std::vector<Key>& chain) {
    while (level > 0) {
      if (const SetPtr<Key> before = levelAt(side, level - 1)) {
        Key y = 0;
        if (!stepBack(x, before, y)) {
          return false;
        }
        chain.push_back(y);
        x = y;
        level--;
        continue;
      }
      // The nearest level kept below: a search out from x as many moves
      // as the gap is wide finds, at its last level, an arrangement of
      // that level; then back through the search's levels to x.
      unsigned int c = 0;
      for (const auto& [l, set] : side.stored) {
        if (l < level - 1 && l > c) {
          c = l;
        }
      }
      const SetPtr<Key> target = levelAt(side, c);
      BT_ASSERT(target != nullptr);
      const unsigned int r = level - c;
      Side ball;
      ball.cur = single(x);
      if (!ball.cur) {
        return false;
      }
      std::vector<SetPtr<Key>> ballLevels{ball.cur};
      std::optional<Key> met;
      for (unsigned int i = 1; i <= r; i++) {
        SetPtr<Key> next;
        std::uint64_t reached = 0;
        std::uint64_t fresh = 0;
        const StepEnd end = step(ball, i == r ? target : nullptr, false, false, {}, next, met,
                                 reached, fresh);
        if (end != StepEnd::Next) {
          return false;
        }
        ball.prev = ball.cur;
        ball.cur = next;
        ball.depth++;
        ballLevels.push_back(next);
        if (search_.traced != nullptr) {
          search_.traced->fetch_add(1, std::memory_order_relaxed);
        }
      }
      BT_ASSERT(met.has_value());
      // from the meeting arrangement back through the search to x
      std::vector<Key> back;
      Key z = *met;
      for (unsigned int i = r - 1; i >= 1; i--) {
        Key y = 0;
        if (!stepBack(z, ballLevels[i], y)) {
          return false;
        }
        back.push_back(y);
        z = y;
      }
      chain.insert(chain.end(), back.rbegin(), back.rend());
      chain.push_back(*met);
      x = *met;
      level = c;
    }
    return true;
  }

  // The goal is one move from the level being expanded: the first
  // arrangement of that level it is one move from (so that every run finds
  // the same path), then the path back from there.
  bool goalFound(std::vector<Key>& chain) {
    if (!fwd_.cur->reload()) {
      store_.ioFailed();
      return false;
    }
    SlideMover<Key>& mover = *movers_[0];
    typename Set::Cursor c(*fwd_.cur);
    Key goalKey = 0;
    Key from = 0;
    Key k = 0;
    bool have = false;
    while (!have && c.next(k)) {
      if (mover.expand(k, [&](Key raw, bool) {
            const Key canon = s_.canon(raw);
            if (!s_.isGoal(canon)) {
              return false;
            }
            goalKey = canon;
            return true;
          })) {
        from = k;
        have = true;
      }
    }
    if (c.failed()) {
      store_.ioFailed();
      return false;
    }
    BT_ASSERT(have);
    chain.push_back(goalKey);
    chain.push_back(from);
    if (!traceSide(fwd_, from, fwd_.depth, chain)) {
      return false;
    }
    std::reverse(chain.begin(), chain.end());
    return true;
  }

  // The two ends met at m: the path from the start to m, then on to the
  // goal.
  bool meetingFound(Key m, std::vector<Key>& chain) {
    std::vector<Key> toStart{m};
    if (!traceSide(fwd_, m, fwd_.depth, toStart)) {
      return false;
    }
    std::vector<Key> toGoal{m};
    if (!traceSide(bwd_, m, bwd_.depth, toGoal)) {
      return false;
    }
    chain.assign(toStart.rbegin(), toStart.rend());
    chain.insert(chain.end(), toGoal.begin() + 1, toGoal.end());
    return true;
  }

  const SlideSpace<Key>& s_;
  SlideSearch& search_;
  std::uint64_t problemKey_;
  std::uint64_t searchKey_;
  SearchFolder folder_;
  std::uint64_t budget_;
  SetStore<Key> store_;
  std::unique_ptr<ThreadPool> ownPool_;
  ThreadPool* pool_ = nullptr;
  unsigned int threads_ = 1;
  // One mover per thread, and one more for the thread that merges the
  // visited sets while the others expand.
  std::vector<std::unique_ptr<SlideMover<Key>>> movers_;
  std::optional<Key> goal_;
  Side fwd_;
  Side bwd_;
  bool bidirectional_ = false;
  unsigned int interval_ = 1;
  std::atomic<std::uint64_t> visited_{0};
  // With nested slides: the older levels merged into a few sets, largest
  // first, to look in; and the level to merge in next.
  std::vector<SetPtr<Key>> visitedSets_;
  SetPtr<Key> pendingVisited_;
  // The sets the last save named (see saveState).
  std::vector<SetPtr<Key>> pinned_;
  DiskBudget disk_;
  bool diskChosen_ = false;
};

// The slide search over position tables: a move is a flood fill over the
// piece's own positions against a bit mask of the cells the others cover.
// It goes level by level (LevelSearch). False, having searched nothing,
// when the tables cannot hold this search.
template <class Key>
bool tableSearch(const Puzzle& puzzle, const Problem& problem, const Voxel& tray,
                 const std::vector<Cell3List>& cells, const std::vector<Cell3List>& outlines,
                 bool nested, const SlideState& initial, const GoalCheck& goals,
                 SlideSearch& search, std::vector<SlideState>& path) {
  SlideSpace<Key> space;
  if (!space.build(tray, cells, outlines, nested, initial, goals, search.likePiecesApart,
                   search.verticalMoves, search.step)) {
    return false;
  }
  space.partial = nested && search.partialNested;

  // Names the search: the problem, the start and everything the search
  // depends on, so a saved search is carried on only by its own.
  const std::uint64_t problemKey = problemKeyOf(puzzle, problem);
  Fingerprint f;
  f.mix(problemKey);
  f.mix(static_cast<unsigned int>(sizeof(Key)));
  f.mix(nested);
  f.mix(space.partial);
  f.mix(space.vertical);
  f.mix(space.step);
  f.mix(search.likePiecesApart);
  f.mix(space.n);
  f.mix(space.startKey);
  for (unsigned int i = 0; i < space.n; i++) {
    for (const Cell3& c : cells[i]) {
      f.mix(c.x);
      f.mix(c.y);
      f.mix(c.z);
    }
    const Place& p = initial.places[i];
    f.mix(p.x);
    f.mix(p.y);
    f.mix(p.z);
    f.mix(p.trans);
    for (const char g : space.t[i].atGoal) {
      f.mix(g);
    }
  }

  std::vector<Key> chain;
  LevelSearch<Key> level(space, search, problemKey, f.value());
  level.run(chain);
  if (!chain.empty()) {
    chainToPath(space, chain, path);
  }
  return true;
}

}  // namespace

void discardSaved(const Puzzle& puzzle, const Problem& problem, const std::string& workDir) {
  if (workDir.empty() || !problem.resultValid()) {
    return;
  }
  const std::uint64_t key = problemKeyOf(puzzle, problem);
  std::error_code ec;
  for (const auto& e : std::filesystem::directory_iterator(workDir, ec)) {
    if (!e.is_directory(ec) || !e.path().filename().string().starts_with("slide-")) {
      continue;
    }
    SavedSearch st;
    if (readState(e.path(), st, false) && st.problemKey == key) {
      std::filesystem::remove_all(e.path(), ec);
    }
  }
}

unsigned int sweepSaved(const std::string& workDir, unsigned int days) {
  if (workDir.empty()) {
    return 0;
  }
  unsigned int removed = 0;
  std::error_code ec;
  const auto now = std::filesystem::file_time_type::clock::now();
  const auto age = std::chrono::hours(24) * days;
  for (const auto& e : std::filesystem::directory_iterator(workDir, ec)) {
    if (!e.is_directory(ec) || !e.path().filename().string().starts_with("slide-")) {
      continue;
    }
    // the newest file in it says when the search last did anything
    std::filesystem::file_time_type newest{};
    for (const auto& f : std::filesystem::directory_iterator(e.path(), ec)) {
      const auto t = f.last_write_time(ec);
      if (!ec && t > newest) {
        newest = t;
      }
    }
    if (now - newest > age) {
      std::filesystem::remove_all(e.path(), ec);
      removed++;
    }
  }
  return removed;
}

namespace {

// The pieces of a path in problem order, each its part's shape turned as
// the state says, with their places: what slideRoute works on.
bool pathPieces(const Puzzle& puzzle, const Problem& problem, const State& a, unsigned int n,
                std::vector<Cell3List>& cells, SlideState& st) {
  unsigned int pc = 0;
  for (unsigned int part = 0; part < problem.getNumberOfParts() && pc < n; part++) {
    for (unsigned int j = 0; j < problem.getPartMaximum(part) && pc < n; j++, pc++) {
      std::unique_ptr<Voxel> v = problem.partShape(puzzle, part).clone();
      (void)v->transform(a.getOrient(pc));
      cells.push_back(cellsOf(*v));
      st.places.push_back(
          Place{a.getX(pc), a.getY(pc), static_cast<unsigned char>(a.getOrient(pc)), a.getZ(pc)});
    }
  }
  return pc == n;
}

}  // namespace

std::uint64_t memoryBudget(std::uint64_t bytes, bool high) {
  if (bytes != 0) {
    return bytes;
  }
  return high ? std::max(kSearchMemoryBytes, physicalMemoryBytes() / 2) : kSearchMemoryBytes;
}

std::uint64_t memoryStates(std::uint64_t stateBytes, bool high, std::uint64_t bytes) {
  if (stateBytes == 0) {
    stateBytes = 1;
  }
  return std::max<std::uint64_t>(memoryBudget(bytes, high) / stateBytes, 1);
}

SlideSearch slideSearchFor(const SlidingOptions& options) {
  SlideSearch s;
  s.highMemory = options.highMemory;
  s.memoryBytes = options.memoryBytes;
  s.nested = options.nested;
  s.partialNested = options.partialNested;
  s.workDir = options.workDir;
  s.diskBudget = options.diskBudget;
  s.legacy = options.legacy;
  s.likePiecesApart = options.likePiecesApart;
  s.verticalMoves = options.verticalMoves;
  s.step = options.step;
  return s;
}

std::optional<Separation> findSlidePath(const Puzzle& puzzle, const Problem& problem,
                                        const Assembly& start, bool nested) {
  SlideSearch search;
  search.nested = nested;
  return findSlidePath(puzzle, problem, start, search);
}

std::optional<Separation> findSlidePath(const Puzzle& puzzle, const Problem& problem,
                                        const Assembly& start, SlideSearch& search) {
  const bool nested = search.nested || search.partialNested;
  search.outcome = SlideOutcome::NoPath;
  search.visited = 0;
  if (!problem.resultValid()) {
    return std::nullopt;
  }

  const Voxel& tray = problem.resultShape(puzzle);
  const unsigned int n = start.placementCount();
  if (n == 0) {
    return std::nullopt;
  }

  // the pieces as placed at the start; owned here, seen through `pieces`
  std::vector<std::unique_ptr<Voxel>> owned(n);
  std::vector<const Voxel*> pieces(n, nullptr);
  SlideState initial;
  initial.places.resize(n);

  unsigned int pc = 0;
  for (unsigned int part = 0; part < problem.getNumberOfParts(); part++) {
    for (unsigned int j = 0; j < problem.getPartMaximum(part); j++) {
      if (pc >= n || !start.isPlaced(pc)) {
        return std::nullopt;
      }
      owned[pc] = problem.partShape(puzzle, part).clone();
      if (!owned[pc]->transform(start.getTransformation(pc))) {
        return std::nullopt;
      }
      pieces[pc] = owned[pc].get();
      initial.places[pc].x = start.getX(pc);
      initial.places[pc].y = start.getY(pc);
      initial.places[pc].trans = start.getTransformation(pc);
      initial.places[pc].z = start.getZ(pc);
      pc++;
    }
  }

  std::vector<unsigned int> pcs(n);
  for (unsigned int i = 0; i < n; i++) {
    pcs[i] = i;
  }

  const GoalCheck goals(puzzle, problem, pieces);
  if (goals.all(initial)) {
    search.outcome = SlideOutcome::Found;
    Separation sep(ClonePtr<Separation>(), ClonePtr<Separation>(), pcs);
    State st(n);
    for (unsigned int i = 0; i < n; i++) {
      st.set(i, initial.places[i].x, initial.places[i].y, start.getZ(i), initial.places[i].trans);
    }
    sep.addstate(std::move(st));
    return sep;
  }

  std::vector<Cell3List> cells(n);
  std::vector<Cell3List> outlines(n);
  for (unsigned int i = 0; i < n; i++) {
    cells[i] = cellsOf(*pieces[i]);
    if (nested) {
      outlines[i] = outlineOf(*pieces[i]);
    }
  }

  // start to goal, in order; empty when no path was found
  std::vector<SlideState> path;
  bool searched = false;
  if (!search.legacy) {
    searched = tableSearch<std::uint64_t>(puzzle, problem, tray, cells, outlines, nested, initial,
                                          goals, search, path);
#ifdef __SIZEOF_INT128__
    if (!searched) {
      searched = tableSearch<WideKey>(puzzle, problem, tray, cells, outlines, nested, initial,
                                      goals, search, path);
    }
#endif
  }
  if (!searched) {
    legacySearch(tray, cells, outlines, nested, initial, goals, search, path);
  }

  if (search.progress != nullptr) {
    search.progress->store(search.visited, std::memory_order_relaxed);
  }
  if (path.empty()) {
    return std::nullopt;
  }
  search.outcome = SlideOutcome::Found;

  Separation sep(ClonePtr<Separation>(), ClonePtr<Separation>(), pcs);

  // The player puts each piece at these coordinates, so they are the
  // assembly placement, not a shift from it.
  for (std::size_t si = path.size(); si-- > 0;) {
    State st(n);
    for (unsigned int i = 0; i < n; i++) {
      st.set(i, path[si].places[i].x, path[si].places[i].y, path[si].places[i].z,
             path[si].places[i].trans);
    }
    sep.addstate(std::move(st));
  }
  return sep;
}

std::vector<SlideShift> slideRoute(const Puzzle& puzzle, const Problem& problem,
                                   const Separation& path, unsigned int step,
                                   std::vector<unsigned int>* movers) {
  std::vector<SlideShift> none;
  if (movers != nullptr) {
    movers->clear();
  }
  if (!problem.resultValid() || step >= path.getMoves()) {
    return none;
  }
  const State& a = path.getState(step);
  const State& b = path.getState(step + 1);
  const unsigned int n = path.getPieceNumber();

  // The pieces that move all shift by the same amount: one piece, or an
  // outer piece and what is nested inside it.
  std::vector<unsigned int> moving;
  SlideShift shift{0, 0, 0};
  for (unsigned int i = 0; i < n; i++) {
    const SlideShift d{b.getX(i) - a.getX(i), b.getY(i) - a.getY(i), b.getZ(i) - a.getZ(i)};
    if (d == SlideShift{0, 0, 0}) {
      continue;
    }
    if (a.getOrient(i) != b.getOrient(i)) {
      return none;
    }
    if (!moving.empty() && d != shift) {
      return none;
    }
    shift = d;
    moving.push_back(i);
  }
  if (moving.empty()) {
    return none;
  }

  // the pieces in path order are the problem's parts, copy by copy
  std::vector<Cell3List> cells;
  SlideState st;
  if (!pathPieces(puzzle, problem, a, n, cells, st)) {
    return none;
  }
  if (movers != nullptr) {
    *movers = moving;
  }
  // a move that ends in its layer is drawn in it when it can be, which it
  // always can when the search kept the pieces in their layers
  const Voxel& tray = problem.resultShape(puzzle);
  if (shift[2] == 0) {
    ShiftList flat = fewestTurns(tray, cells, st, moving, shift, false);
    if (!flat.empty()) {
      return flat;
    }
  }
  return fewestTurns(tray, cells, st, moving, shift, true);
}

void applySlideRoutes(const Puzzle& puzzle, const Problem& problem, const Separation& path,
                      DisasmToMoves& anim) {
  for (unsigned int step = 0; step < path.getMoves(); step++) {
    std::vector<unsigned int> movers;
    const std::vector<SlideShift> route = slideRoute(puzzle, problem, path, step, &movers);
    // a straight run animates correctly without help
    if (route.size() < 3) {
      continue;
    }
    std::vector<std::array<float, 3>> offsets;
    offsets.reserve(route.size());
    for (const SlideShift& c : route) {
      offsets.push_back(
          {static_cast<float>(c[0]), static_cast<float>(c[1]), static_cast<float>(c[2])});
    }
    anim.setRoute(step, movers, offsets);
  }
}

std::string finalPlacementKey(const Separation& path) {
  const State& st = path.getState(path.getMoves());
  std::ostringstream os;
  for (unsigned int i = 0; i < path.getPieceNumber(); i++) {
    if (i != 0) {
      os << '|';
    }
    os << st.getX(i) << ',' << st.getY(i) << ',' << st.getZ(i) << ',' << st.getOrient(i);
  }
  return os.str();
}

}  // namespace bt::sliding

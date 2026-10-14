// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/sliding.cpp: the slide search (goalCheck_c, the cells
// and outlines of the pieces, legacySearch over text keys, the position
// tables, queueSearch, levelSearch over the key sets of slidelevels.h),
// findSlidePath, slideRoute, applySlideRoutes and finalPlacementKey.
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
#include <functional>
#include <map>
#include <memory>
#include <queue>
#include <random>
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

constexpr int kDx[4] = {0, 0, -1, 1};
constexpr int kDy[4] = {-1, 1, 0, 0};

std::size_t idx(int v) { return static_cast<std::size_t>(v); }

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
  // Pieces slide in x and y only. z is the layer their hotspot sits on,
  // fixed from the start, so a tray more than one layer deep works too.
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
    os << static_cast<int>(s.places[i].trans) << ':' << s.places[i].x << ',' << s.places[i].y;
  }
  return os.str();
}

// Whether each piece sits where the goal wants it. Every piece is judged on
// its own, so a position search can work the answer out once per position.
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
    for (std::size_t i = 0; i < pieces.size(); i++) {
      const unsigned int shapeId = problem.getShapeIdOfPart(partOf[i]);
      if (startGoal_) {
        const unsigned int col = shapeId + 1;
        for (unsigned int gi = 0; gi < tray_->getXYZ(); gi++) {
          if (tray_->getGoalPiece(gi) == col) {
            color_[i] = col;
            break;
          }
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

  // Piece i, placed at place, is where the goal wants it.
  [[nodiscard]] bool piece(std::size_t i, const Place& place) const {
    const Voxel* p = pieces_[i];
    if (startGoal_) {
      if (p == nullptr) {
        return false;
      }
      // Empty cells are walls. A piece may slide across a variable cell,
      // and the finished position must leave every variable cell empty.
      // Only normal cells are places to stop, a goal stamp included.
      bool ok = true;
      forCells(*p, place, [&](int tx, int ty, int tz) {
        if (!isFloor(*tray_, tx, ty, tz) ||
            tray_->getState(asUnsigned(tx), asUnsigned(ty), asUnsigned(tz)) == Voxel::kVariable ||
            (color_[i] != 0 &&
             tray_->getGoalPiece(asUnsigned(tx), asUnsigned(ty), asUnsigned(tz)) != color_[i])) {
          ok = false;
        }
      });
      return ok;
    }
    if (goal_ == nullptr || color_[i] == 0) {
      return true;
    }
    if (p == nullptr) {
      return false;
    }
    bool ok = true;
    forCells(*p, place, [&](int tx, int ty, int tz) {
      if (!isFloor(*goal_, tx, ty, tz) ||
          goal_->getColor(asUnsigned(tx), asUnsigned(ty), asUnsigned(tz)) != color_[i]) {
        ok = false;
      }
    });
    return ok;
  }

  [[nodiscard]] bool all(const SlideState& s) const {
    for (std::size_t i = 0; i < pieces_.size(); i++) {
      if (!piece(i, s.places[i])) {
        return false;
      }
    }
    return true;
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
};

// One cell of a piece, relative to its hotspot.
struct Cell3 {
  int x;
  int y;
  int z;
};
using Cell3List = std::vector<Cell3>;
using CellList = std::vector<std::pair<int, int>>;

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

// The movers fit when shifted by (dx, dy) as one rigid group.
bool groupFits(const FreeGrid& g, const std::vector<Cell3List>& cells, const SlideState& s,
               const std::vector<unsigned int>& movers, int dx, int dy) {
  for (const unsigned int m : movers) {
    for (const Cell3& c : cells[m]) {
      if (!g.at(s.places[m].x + c.x + dx, s.places[m].y + c.y + dy, s.places[m].z + c.z)) {
        return false;
      }
    }
  }
  return true;
}

// Every shift the movers can reach as one group while the others stay put.
// One move of the solver is a jump to any of them, whatever the route.
CellList reachableShifts(const Voxel& tray, const std::vector<Cell3List>& cells,
                         const SlideState& s, const std::vector<unsigned int>& movers) {
  const FreeGrid g = freeCells(tray, cells, s, movers);
  std::set<std::pair<int, int>> seen{{0, 0}};
  CellList out;
  std::queue<std::pair<int, int>> q;
  q.emplace(0, 0);
  while (!q.empty()) {
    const std::pair<int, int> c = q.front();
    q.pop();
    for (int d = 0; d < 4; d++) {
      const std::pair<int, int> n{c.first + kDx[d], c.second + kDy[d]};
      if (seen.count(n) != 0 || !groupFits(g, cells, s, movers, n.first, n.second)) {
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
// straight runs that takes the movers to shift (tx, ty), start and end
// included. Empty when unreachable.
CellList fewestTurns(const Voxel& tray, const std::vector<Cell3List>& cells, const SlideState& s,
                     const std::vector<unsigned int>& movers, int tx, int ty) {
  const FreeGrid g = freeCells(tray, cells, s, movers);
  const std::pair<int, int> from{0, 0};
  const std::pair<int, int> to{tx, ty};
  std::map<std::pair<int, int>, std::pair<int, int>> came{{from, from}};
  std::queue<std::pair<int, int>> q;
  q.push(from);
  while (!q.empty() && came.count(to) == 0) {
    const std::pair<int, int> c = q.front();
    q.pop();
    for (int d = 0; d < 4; d++) {
      std::pair<int, int> n = c;
      for (;;) {
        n.first += kDx[d];
        n.second += kDy[d];
        if (!groupFits(g, cells, s, movers, n.first, n.second)) {
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
  CellList route;
  if (came.count(to) == 0) {
    return route;
  }
  for (std::pair<int, int> c = to;; c = came[c]) {
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
  const std::uint64_t maxStates = search.maxStates;

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
          : memoryStates(128 + 3 * std::uint64_t{startKey.size()}, search.highMemory);
  search.memoryStates = memoryLimit;
  q.push(initial);
  parent[startKey] = {"", n};

  std::uint64_t visited = 0;
  std::string goalKey;
  bool found = false;

  while (!q.empty()) {
    if (maxStates != kFullSearch && visited >= maxStates) {
      search.outcome = SlideOutcome::Limit;
      break;
    }
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
        std::vector<unsigned int> group =
            nestedGroup(cells, outlines, cur, pi, search.partialNested);
        if (group.size() > 1) {
          moverSets.push_back(std::move(group));
        }
      }
      for (const std::vector<unsigned int>& movers : moverSets) {
        for (const std::pair<int, int>& shift : reachableShifts(tray, cells, cur, movers)) {
          SlideState next = cur;
          for (const unsigned int m : movers) {
            next.places[m].x += shift.first;
            next.places[m].y += shift.second;
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
      if (i != 0) {
        is >> bar;
      }
      is >> t >> colon >> x >> comma >> y;
      st.places[i].trans = static_cast<unsigned char>(t);
      st.places[i].x = x;
      st.places[i].y = y;
      st.places[i].z = initial.places[i].z;
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
  int w = 0;
  int h = 0;
  std::vector<int> index;
  std::vector<int> px;
  std::vector<int> py;
  // The neighbouring position one step along kDx/kDy, or -1.
  std::vector<std::array<int, 4>> next;
  // The tray cells each position covers, `words` 64-bit words a position.
  std::vector<std::uint64_t> mask;
  // With nested slides: the cells of the piece's outline at each position,
  // laid out as mask is. A piece is nested in this one when its mask lies
  // wholly inside.
  std::vector<std::uint64_t> outline;
  std::vector<char> atGoal;
  // Where this piece's position number sits in a packed arrangement.
  unsigned int shift = 0;
  std::uint64_t bits = 0;

  [[nodiscard]] int at(int x, int y) const {
    x -= x0;
    y -= y0;
    if (x < 0 || y < 0 || x >= w || y >= h) {
      return -1;
    }
    return index[idx(y * w + x)];
  }
};

// Builds the position tables. False when they cannot hold this search: a
// piece with no cells or nowhere to go, a start off the floor, or more
// positions than keyBits bits can number; and (unlike the original, which
// overran its fixed arrays there) more than 64 pieces with nested slides.
bool buildTables(const Voxel& tray, const std::vector<Cell3List>& cells,
                 const std::vector<Cell3List>& outlines, bool nested, const SlideState& initial,
                 const GoalCheck& goals, unsigned int keyBits, std::size_t& words,
                 std::vector<PosTable>& tables) {
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
    for (const Cell3& c : cells[i]) {
      minX = std::min(minX, c.x);
      maxX = std::max(maxX, c.x);
      minY = std::min(minY, c.y);
      maxY = std::max(maxY, c.y);
    }
    PosTable& t = tables[i];
    t.x0 = -minX;
    t.y0 = -minY;
    t.w = W - maxX - t.x0;
    t.h = H - maxY - t.y0;
    if (t.w <= 0 || t.h <= 0) {
      return false;
    }
    t.index.assign(idx(t.w * t.h), -1);

    const Place& home = initial.places[i];
    for (int y = t.y0; y < t.y0 + t.h; y++) {
      for (int x = t.x0; x < t.x0 + t.w; x++) {
        bool fits = true;
        for (const Cell3& c : cells[i]) {
          if (!isFloor(tray, x + c.x, y + c.y, home.z + c.z)) {
            fits = false;
            break;
          }
        }
        if (!fits) {
          continue;
        }
        t.index[idx((y - t.y0) * t.w + (x - t.x0))] = asInt(t.px.size());
        t.px.push_back(x);
        t.py.push_back(y);
        t.mask.resize(t.mask.size() + words, 0);
        std::uint64_t* m = &t.mask[t.mask.size() - words];
        for (const Cell3& c : cells[i]) {
          const std::size_t bit =
              (idx(home.z + c.z) * idx(H) + idx(y + c.y)) * idx(W) + idx(x + c.x);
          m[bit / 64] |= std::uint64_t{1} << (bit % 64);
        }
        if (nested) {
          t.outline.resize(t.outline.size() + words, 0);
          std::uint64_t* o = &t.outline[t.outline.size() - words];
          for (const Cell3& c : outlines[i]) {
            const int ox = x + c.x;
            const int oy = y + c.y;
            const int oz = home.z + c.z;
            if (ox < 0 || oy < 0 || oz < 0 || ox >= W || oy >= H || oz >= D) {
              continue;
            }
            const std::size_t bit = (idx(oz) * idx(H) + idx(oy)) * idx(W) + idx(ox);
            o[bit / 64] |= std::uint64_t{1} << (bit % 64);
          }
        }
        t.atGoal.push_back(goals.piece(i, Place{x, y, home.trans, home.z}) ? 1 : 0);
      }
    }

    const std::size_t count = t.px.size();
    if (count == 0 || t.at(home.x, home.y) < 0) {
      return false;
    }
    t.next.resize(count);
    for (std::size_t k = 0; k < count; k++) {
      for (int d = 0; d < 4; d++) {
        t.next[k][idx(d)] = t.at(t.px[k] + kDx[d], t.py[k] + kDy[d]);
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

// Arrangement -> the arrangement it was reached from, for the queue search.
// Open addressing in two flat arrays: no allocation per entry, unlike
// std::unordered_map, so about twice as fast.
template <class Key>
class ParentMap {
 public:
  ParentMap() { rehash(std::size_t{1} << 16); }

  // Adds key with its parent; false when key is there already.
  bool emplace(Key key, Key parent) {
    if (key == kEmpty) {
      if (hasEmptyKey_) {
        return false;
      }
      hasEmptyKey_ = true;
      emptyKeyParent_ = parent;
      count_++;
      return true;
    }
    std::size_t i = slot(key);
    while (keys_[i] != kEmpty) {
      if (keys_[i] == key) {
        return false;
      }
      i = (i + 1) & mask_;
    }
    keys_[i] = key;
    parents_[i] = parent;
    if (++count_ * 10 > keys_.size() * 7) {
      rehash(keys_.size() * 2);
    }
    return true;
  }

  // The parent of a key that is there.
  [[nodiscard]] Key parentOf(Key key) const {
    if (key == kEmpty) {
      return emptyKeyParent_;
    }
    std::size_t i = slot(key);
    while (keys_[i] != key) {
      i = (i + 1) & mask_;
    }
    return parents_[i];
  }

  [[nodiscard]] std::size_t size() const { return count_; }

 private:
  static constexpr Key kEmpty = ~Key(0);

  [[nodiscard]] std::size_t slot(Key key) const {
    return static_cast<std::size_t>((fold(key) * 0x9E3779B97F4A7C15ULL) >> shift_);
  }
  static std::uint64_t fold(std::uint64_t key) { return key; }
#ifdef __SIZEOF_INT128__
  static std::uint64_t fold(WideKey key) {
    return static_cast<std::uint64_t>(key) ^
           (static_cast<std::uint64_t>(key >> 64) * 0xC2B2AE3D27D4EB4FULL);
  }
#endif

  void rehash(std::size_t capacity) {
    std::vector<Key> oldKeys(capacity, kEmpty);
    std::vector<Key> oldParents(capacity);
    oldKeys.swap(keys_);
    oldParents.swap(parents_);
    mask_ = capacity - 1;
    shift_ = 64;
    for (std::size_t c = capacity; c > 1; c >>= 1) {
      shift_--;
    }
    for (std::size_t j = 0; j < oldKeys.size(); j++) {
      if (oldKeys[j] != kEmpty) {
        std::size_t i = slot(oldKeys[j]);
        while (keys_[i] != kEmpty) {
          i = (i + 1) & mask_;
        }
        keys_[i] = oldKeys[j];
        parents_[i] = oldParents[j];
      }
    }
  }

  std::vector<Key> keys_;
  std::vector<Key> parents_;
  std::size_t mask_ = 0;
  unsigned int shift_ = 64;
  std::size_t count_ = 0;
  // kEmpty marks a free slot, so that one key is kept aside.
  bool hasEmptyKey_ = false;
  Key emptyKeyParent_ = 0;
};

// The memory one arrangement takes in the queue search, in key sizes: two
// keys in ParentMap at a load between 35% and 70%, its share of the queue,
// and while the table doubles the old and the new table together. Measured
// at 51 bytes at peak with 8-byte keys on the 4x4 benchmark of the tests;
// 7 keys leaves some room.
constexpr std::uint64_t kTableStateKeys = 7;

// The memory one arrangement held in memory takes in the level search, in
// key sizes: its place in a level, and its share of the sorted pieces and
// the merges that make the next level. Older levels go to disk.
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
  // With nested: a piece partly in a member's outline joins its group too.
  bool partial = false;
  int trayW = 0;
  int trayH = 0;
  SlideState initial;
  // Groups of pieces that are copies of one another (see build).
  std::vector<std::vector<unsigned int>> copies;
  Key startKey = 0;
  Key startCanon = 0;

  // False when the tables cannot hold this search.
  bool build(const Voxel& tray, const std::vector<Cell3List>& cells,
             const std::vector<Cell3List>& outlines, bool nest, const SlideState& init,
             const GoalCheck& goals, bool likePiecesApart) {
    if (!buildTables(tray, cells, outlines, nest, init, goals,
                     static_cast<unsigned int>(8 * sizeof(Key)), words, t)) {
      return false;
    }
    n = static_cast<unsigned int>(init.places.size());
    nested = nest;
    initial = init;
    trayW = asInt(tray.getX());
    trayH = asInt(tray.getY());

    startKey = 0;
    for (unsigned int i = 0; i < n; i++) {
      startKey |= static_cast<Key>(t[i].at(init.places[i].x, init.places[i].y)) << t[i].shift;
    }

    // Pieces that are copies of one another (same cells, same layer, same
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
          if (init.places[i].z == init.places[j].z && sameCells(cells[i], cells[j]) &&
              t[i].px == t[j].px && t[i].py == t[j].py && t[i].atGoal == t[j].atGoal) {
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
        shiftSeen_(space.nested ? idx((2 * space.trayW + 1) * (2 * space.trayH + 1)) : 0, 0) {
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
        for (std::size_t d = 0; d < 4; d++) {
          const int nb = tp.next[idx(fill_[head])][d];
          if (nb < 0 || seen_[pi][idx(nb)] == epoch_ ||
              overlaps(&tp.mask[idx(nb) * words], others_.data(), words)) {
            continue;
          }
          seen_[pi][idx(nb)] = epoch_;
          fill_.push_back(nb);
          if (emit(clear | (static_cast<Key>(nb) << tp.shift), false)) {
            return true;
          }
        }
      }

      if (!s_.nested) {
        continue;
      }
      std::array<unsigned int, 64> group{};
      unsigned int members = 0;
      const std::uint64_t inGroup = groupAt(pos_.data(), pi, group.data(), members);
      if (members < 2) {
        continue;
      }
      lastOuter_ = pi;
      lastGroup_ = inGroup;
      others_ = occupied_;
      for (unsigned int g = 0; g < members; g++) {
        const unsigned int m = group[g];
        const std::uint64_t* gm = &t[m].mask[idx(pos_[m]) * words];
        for (std::size_t w = 0; w < words; w++) {
          others_[w] &= ~gm[w];
        }
      }
      // The group's key at shift (dx, dy), or false when it does not fit.
      auto shifted = [&](int dx, int dy, Key& key) {
        key = cur;
        for (unsigned int g = 0; g < members; g++) {
          const unsigned int m = group[g];
          const int p = t[m].at(t[m].px[idx(pos_[m])] + dx, t[m].py[idx(pos_[m])] + dy);
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
      auto seenAt = [&](int dx, int dy) -> unsigned int& {
        return shiftSeen_[idx((dy + trayH) * (2 * trayW + 1) + (dx + trayW))];
      };
      seenAt(0, 0) = shiftEpoch_;
      shiftQueue_.assign(1, {0, 0});
      for (std::size_t head = 0; head < shiftQueue_.size(); head++) {
        const std::pair<int, int> c = shiftQueue_[head];
        for (int d = 0; d < 4; d++) {
          const std::pair<int, int> nb{c.first + kDx[d], c.second + kDy[d]};
          // a shift as wide as the tray fits nothing
          if (nb.first < -trayW || nb.first > trayW || nb.second < -trayH || nb.second > trayH) {
            continue;
          }
          Key key = 0;
          if (seenAt(nb.first, nb.second) == shiftEpoch_ || !shifted(nb.first, nb.second, key)) {
            continue;
          }
          seenAt(nb.first, nb.second) = shiftEpoch_;
          shiftQueue_.push_back(nb);
          if (emit(key, true)) {
            return true;
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
    return groupAt(backPos_.data(), lastOuter_, group.data(), members) == lastGroup_;
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
  // that member's outline, or with s_.partial, when any of them does. Into
  // group, and as a bit set.
  std::uint64_t groupAt(const int* at, unsigned int outer, unsigned int* group,
                        unsigned int& members) const {
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
        if (s_.partial) {
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
  // The same for the shifts of a nested group, (dx, dy) from -tray to +tray.
  std::vector<unsigned int> shiftSeen_;
  unsigned int shiftEpoch_ = 0;
  std::vector<std::pair<int, int>> shiftQueue_;
  // The group move being reported.
  unsigned int lastOuter_ = 0;
  std::uint64_t lastGroup_ = 0;
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

// The slide search for a limited number of arrangements: breadth first
// through a queue, every arrangement kept with the one it was reached from.
// It visits arrangements in the same order as legacySearch, so both find
// the same path.
template <class Key>
void queueSearch(const SlideSpace<Key>& s, SlideSearch& search, std::vector<Key>& chain) {
  const std::uint64_t maxStates = search.maxStates;
  const std::uint64_t memoryLimit =
      search.maxMemoryStates != 0
          ? search.maxMemoryStates
          : memoryStates(kTableStateKeys * sizeof(Key), search.highMemory);
  search.memoryStates = memoryLimit;
  SlideMover<Key> mover(s);

  // Each arrangement and the one it was reached from; the start is its own.
  ParentMap<Key> parent;
  parent.emplace(s.startCanon, s.startCanon);
  std::queue<Key> q;
  q.push(s.startCanon);

  std::uint64_t visited = 0;
  Key goalKey = 0;
  bool found = false;

  // A new arrangement: remember it and queue it. True when it is the goal.
  auto reach = [&](Key key, Key from) {
    if (!parent.emplace(key, from)) {
      return false;
    }
    if (s.isGoal(key)) {
      goalKey = key;
      return true;
    }
    q.push(key);
    return false;
  };

  while (!q.empty() && !found) {
    if (maxStates != kFullSearch && visited >= maxStates) {
      search.outcome = SlideOutcome::Limit;
      break;
    }
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
    const Key cur = q.front();
    q.pop();
    visited++;
    found = mover.expand(cur, [&](Key key, bool) { return reach(s.canon(key), cur); });
  }

  search.visited = visited;
  if (!found) {
    return;
  }
  for (Key k = goalKey;; k = parent.parentOf(k)) {
    chain.push_back(k);
    if (k == s.startCanon) {
      break;
    }
  }
  std::reverse(chain.begin(), chain.end());
}

// Drops from sorted, unique a every key that is in sorted b. Each key of a
// is looked for from where the last was found, in doubling steps.
template <class Key>
void subtractSorted(std::vector<Key>& a, const std::vector<Key>& b) {
  if (b.empty() || a.empty()) {
    return;
  }
  std::size_t w = 0;
  auto j = b.begin();
  for (std::size_t i = 0; i < a.size(); i++) {
    const Key x = a[i];
    auto hi = j;
    std::size_t step = 1;
    while (hi != b.end() && *hi < x) {
      j = hi;
      hi += static_cast<std::ptrdiff_t>(
          std::min<std::size_t>(step, static_cast<std::size_t>(b.end() - hi)));
      step *= 2;
    }
    j = std::lower_bound(j, hi, x);
    if (j != b.end() && *j == x) {
      continue;
    }
    a[w++] = x;
  }
  a.resize(w);
}

// Runs f(i) for every i in [0, n) on up to `threads` threads of the pool,
// each index once (the original's onThreads gave each index a thread of
// its own; the pool may lend fewer).
template <class F>
void forEachIndex(ThreadPool& pool, unsigned int threads, std::size_t n, F f) {
  std::atomic<std::size_t> next{0};
  pool.run(threads, [&](unsigned int) {
    for (std::size_t i = next.fetch_add(1); i < n; i = next.fetch_add(1)) {
      f(i);
    }
  });
}

// Sorted, unique runs merged into out, sorted and unique, and the runs
// freed. Each of `threads` threads merges a range of keys, cut where a
// sample of the runs says. out keeps its memory from one call to the next.
template <class Key>
void mergeRuns(std::vector<std::vector<Key>>& runs, std::vector<Key>& out, ThreadPool& pool,
               unsigned int threads) {
  std::size_t total = 0;
  for (const std::vector<Key>& r : runs) {
    total += r.size();
  }
  out.clear();
  if (total == 0) {
    runs.clear();
    return;
  }
  std::vector<Key> sample;
  for (const std::vector<Key>& r : runs) {
    for (std::size_t i = 0; i < r.size(); i += std::max<std::size_t>(1, r.size() / 64)) {
      sample.push_back(r[i]);
    }
  }
  std::sort(sample.begin(), sample.end());
  const auto parts = static_cast<unsigned int>(
      std::max<std::size_t>(1, std::min<std::size_t>(threads, sample.size())));
  std::vector<Key> cut(parts - 1);
  for (unsigned int p = 1; p < parts; p++) {
    cut[p - 1] = sample[sample.size() * p / parts];
  }

  // Where each part's keys lie in each run, and where in out it may write:
  // as many places as it has keys, before duplicates go.
  std::vector<std::vector<std::pair<std::size_t, std::size_t>>> span(
      parts, std::vector<std::pair<std::size_t, std::size_t>>(runs.size()));
  std::vector<std::size_t> start(parts + 1, 0);
  for (unsigned int p = 0; p < parts; p++) {
    start[p + 1] = start[p];
    for (std::size_t r = 0; r < runs.size(); r++) {
      const std::vector<Key>& run = runs[r];
      const std::size_t lo =
          p == 0 ? 0
                 : static_cast<std::size_t>(std::lower_bound(run.begin(), run.end(), cut[p - 1]) -
                                            run.begin());
      const std::size_t hi =
          p + 1 == parts
              ? run.size()
              : static_cast<std::size_t>(std::lower_bound(run.begin(), run.end(), cut[p]) -
                                         run.begin());
      span[p][r] = {lo, hi};
      start[p + 1] += hi - lo;
    }
  }
  out.resize(total);
  std::vector<std::size_t> written(parts, 0);
  forEachIndex(pool, threads, parts, [&](std::size_t p) {
    using Item = std::pair<Key, std::size_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<>> heap;
    std::vector<std::size_t> at(runs.size());
    for (std::size_t r = 0; r < runs.size(); r++) {
      at[r] = span[p][r].first;
      if (at[r] < span[p][r].second) {
        heap.emplace(runs[r][at[r]], r);
      }
    }
    Key* o = out.data() + start[p];
    std::size_t w = 0;
    while (!heap.empty()) {
      const Item top = heap.top();
      heap.pop();
      if (w == 0 || o[w - 1] != top.first) {
        o[w++] = top.first;
      }
      const std::size_t r = top.second;
      if (++at[r] < span[p][r].second) {
        heap.emplace(runs[r][at[r]], r);
      }
    }
    written[p] = w;
  });
  std::size_t w = written[0];
  for (unsigned int p = 1; p < parts; p++) {
    std::memmove(out.data() + w, out.data() + start[p], written[p] * sizeof(Key));
    w += written[p];
  }
  out.resize(w);
  runs.clear();
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

// A folder of its own for one search's files, removed with everything in it
// when the search is done. With no work folder there is none.
class SearchFolder {
 public:
  explicit SearchFolder(const std::string& workDir) {
    if (!workDir.empty()) {
      base_ = workDir;
    }
  }
  ~SearchFolder() {
    if (!dir.empty()) {
      std::error_code ec;
      std::filesystem::remove_all(dir, ec);
    }
  }
  SearchFolder(const SearchFolder&) = delete;
  SearchFolder& operator=(const SearchFolder&) = delete;

  // Makes the folder the first time it is needed; false when it cannot be.
  bool open() {
    if (!dir.empty()) {
      return true;
    }
    if (tried_ || base_.empty()) {
      return false;
    }
    tried_ = true;
    std::random_device rd;
    const std::uint64_t r =
        (static_cast<std::uint64_t>(rd()) << 32) ^ rd() ^
        static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    char name[32];
    std::snprintf(name, sizeof(name), "search-%016llx", static_cast<unsigned long long>(r));
    std::error_code ec;
    std::filesystem::create_directories(base_ / name, ec);
    if (!ec) {
      dir = base_ / name;
    }
    return !dir.empty();
  }

  // Empty till open() succeeds.
  std::filesystem::path dir;

 private:
  std::filesystem::path base_;
  bool tried_ = false;
};

// The full slide search: breadth first, one level at a time, a level being
// every arrangement a given number of moves from the start.
//
//  - Only the level being expanded and the one before stay in memory, as
//    sorted arrays. Older levels are delta coded (slide_levels.h) and kept
//    in memory while they are small, then go to files, so the search is
//    limited by the disk rather than by memory. A level is written while
//    the next one is expanded.
//  - A move that one piece makes can always be made backwards, and so can a
//    nested group's unless the group closes round a piece on its way. An
//    arrangement reached by a move that can be made backwards is new unless
//    it is in this level or the one before: had it been reached earlier,
//    the arrangement it came from would have been reached by now too. So
//    only the arrangements reached by the other moves are looked for in the
//    levels on disk, kept merged into a few sorted sets.
//  - Each level is expanded by every thread at once.
//  - The path is traced back level by level: an arrangement one move before
//    is in the level before, found by moving backwards from it or, failing
//    that, by trying every arrangement of that level.
//
// It finds a path with the fewest moves, as queueSearch does, though not
// always the same one.
template <class Key>
void levelSearch(const SlideSpace<Key>& s, SlideSearch& search, std::vector<Key>& chain) {
  using Set = levels::KeySet<Key>;
  using SetPtr = std::shared_ptr<Set>;
  using Vec = std::vector<Key>;

  // The threads: the pool given, or one of the search's own.
  std::unique_ptr<ThreadPool> ownPool;
  ThreadPool* pool = search.pool;
  if (pool == nullptr) {
    ownPool = std::make_unique<ThreadPool>(search.threads != 0 ? std::min(search.threads, kMaxThreads)
                                                               : hardwareThreads());
    pool = ownPool.get();
  }
  const unsigned int threads =
      std::max(1U, search.threads != 0 ? std::min(search.threads, pool->cores()) : pool->cores());

  const std::uint64_t memoryLimit =
      search.maxMemoryStates != 0
          ? search.maxMemoryStates
          : memoryStates(kLevelStateKeys * sizeof(Key), search.highMemory);
  search.memoryStates = memoryLimit;
  search.depth = 0;
  search.diskBytes = 0;
  search.oneWayRepeats = 0;

  SearchFolder folder(search.workDir);
  const std::uint64_t memoryLevelBytes =
      search.memoryLevelBytes != 0 ? search.memoryLevelBytes : 256000000ULL;
  std::uint64_t diskBudget = search.diskBudget;
  // With no budget given, it is set when the folder is made: all the free
  // disk but a margin for everything else, and never less than a little of
  // what is free, so a nearly full disk still takes a modest search.
  auto setDiskBudget = [&]() {
    if (diskBudget != 0) {
      return;
    }
    std::error_code ec;
    const std::filesystem::space_info space = std::filesystem::space(folder.dir, ec);
    if (ec) {
      diskBudget = 1;
      return;
    }
    const std::uint64_t margin = std::max<std::uint64_t>(2000000000ULL, space.capacity / 20);
    const std::uint64_t least = std::min<std::uint64_t>(1000000000ULL, space.available / 2);
    diskBudget = std::max<std::uint64_t>(
        {space.available > margin ? space.available - margin : 0, least, 1});
  };

  // Every level that has left the three arrays, stored[k] for level k, for
  // tracing the path back; and with nested slides, the same levels merged
  // into a few sets, largest first, to look in.
  std::vector<SetPtr> stored;
  std::vector<SetPtr> visitedSets;
  // Bytes on disk, and in memory, the stored levels take.
  auto storedBytes = [&]() {
    std::uint64_t disk = 0;
    std::uint64_t memory = 0;
    std::vector<const Set*> seen;
    for (const std::vector<SetPtr>* list : {&stored, &visitedSets}) {
      for (const SetPtr& v : *list) {
        if (std::find(seen.begin(), seen.end(), v.get()) != seen.end()) {
          continue;
        }
        seen.push_back(v.get());
        memory += v->indexBytes();
        if (v->inMemory()) {
          memory += v->bytes();
        } else {
          disk += v->bytes();
        }
      }
    }
    return std::make_pair(disk, memory);
  };
  unsigned int fileNumber = 0;
  // A new key set of about the given size: in memory while the stored
  // levels there are small, else in the folder when there is one.
  auto newSet = [&](std::uint64_t bytes) {
    auto set = std::make_shared<Set>();
    std::filesystem::path p;
    if (storedBytes().second + bytes > memoryLevelBytes && folder.open()) {
      setDiskBudget();
      char name[32];
      std::snprintf(name, sizeof(name), "%06u.keys", fileNumber++);
      p = folder.dir / name;
    }
    return set->create(p) ? set : nullptr;
  };
  auto ioFailed = [&]() {
    search.outcome = SlideOutcome::Disk;
    search.error = folder.dir.empty() ? std::string("The search could not keep its levels.")
                                      : "Could not write the search's files in " +
                                            folder.dir.string() + ". The disk may be full.";
  };
  // Stores a level that leaves memory.
  auto saveLevel = [&](const Vec& level) {
    SetPtr set = newSet(std::uint64_t{level.size()} * sizeof(Key));
    if (!set || !set->addAll(level.begin(), level.end()) || !set->finish()) {
      return false;
    }
    stored.push_back(set);
    if (s.nested) {
      visitedSets.push_back(set);
      // Merge the newest sets while one is no more than twice the size of
      // the one after it, so there are only a few to look in.
      while (visitedSets.size() >= 2 &&
             visitedSets[visitedSets.size() - 2]->size() <= 2 * visitedSets.back()->size()) {
        const SetPtr a = visitedSets[visitedSets.size() - 2];
        const SetPtr b = visitedSets.back();
        visitedSets.resize(visitedSets.size() - 2);
        const SetPtr m = newSet(a->bytes() + b->bytes());
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
        if (!ok || ca.failed() || cb.failed() || !m->finish()) {
          return false;
        }
        visitedSets.push_back(m);
      }
    }
    set.reset();
    // a level only kept for tracing the path needs no index till then
    for (const SetPtr& st : stored) {
      if (st.use_count() == 1) {
        st->release();
      }
    }
    return true;
  };

  // The level before the one being expanded, that one, and room for the
  // next: three arrays that take turns, so that a level reuses the memory
  // of the one before last.
  Vec prev;
  Vec cur{s.startCanon};
  Vec next;
  std::uint64_t depth = 0;
  std::atomic<std::uint64_t> visited{0};
  // One mover per thread, and one more for the thread that stores the
  // level before while the others expand this one.
  std::vector<std::unique_ptr<SlideMover<Key>>> movers;
  for (unsigned int i = 0; i <= threads; i++) {
    movers.push_back(std::make_unique<SlideMover<Key>>(s));
  }

  // The sorted pieces of the next level, from each thread: those reached
  // by moves that can be made backwards, and the others.
  struct Work {
    Vec back;
    Vec oneWay;
    std::vector<Vec> backRuns;
    std::vector<Vec> oneWayRuns;
  };
  const std::size_t kPiece = std::size_t{1} << 18;
  // Sorts a piece and drops what is in this level or the one before.
  auto settle = [&](Vec& piece, std::vector<Vec>& runs) {
    if (piece.empty()) {
      return;
    }
    std::sort(piece.begin(), piece.end());
    piece.erase(std::unique(piece.begin(), piece.end()), piece.end());
    subtractSorted(piece, cur);
    subtractSorted(piece, prev);
    runs.emplace_back(piece.begin(), piece.end());
    piece.clear();
  };

  const auto started = std::chrono::steady_clock::now();

  bool goalNext = false;
  while (!cur.empty()) {
    search.depth = depth;
    if (search.depthProgress != nullptr) {
      search.depthProgress->store(depth, std::memory_order_relaxed);
    }

    // The level before is stored while this one is expanded: the first
    // thread stores it and then joins the others (the original stored it
    // on a thread of its own).
    bool saved = true;
    const bool saving = depth > 0;

    std::vector<Work> work(threads + 1);
    std::atomic<std::size_t> nextIndex{0};
    std::atomic<bool> hit{false};
    std::atomic<bool> halt{false};
    const std::size_t kBatch = 1024;
    pool->run(threads + (saving ? 1 : 0), [&](unsigned int th) {
      if (th == 0 && saving) {
        saved = saveLevel(prev);
      }
      Work& w = work[th];
      SlideMover<Key>& mover = *movers[th];
      auto emit = [&](Key raw, bool group) {
        const Key k = s.canon(raw);
        if (s.isGoal(k)) {
          return true;
        }
        if (group && !mover.undoable(raw)) {
          w.oneWay.push_back(k);
          if (w.oneWay.size() >= kPiece) {
            settle(w.oneWay, w.oneWayRuns);
          }
        } else {
          w.back.push_back(k);
          if (w.back.size() >= kPiece) {
            settle(w.back, w.backRuns);
          }
        }
        return false;
      };
      while (!hit.load(std::memory_order_relaxed) && !halt.load(std::memory_order_relaxed)) {
        const std::size_t b = nextIndex.fetch_add(kBatch);
        if (b >= cur.size()) {
          break;
        }
        const std::size_t e = std::min(b + kBatch, cur.size());
        for (std::size_t i = b; i < e; i++) {
          if (mover.expand(cur[i], emit)) {
            hit.store(true, std::memory_order_relaxed);
            break;
          }
        }
        const std::uint64_t v = visited.fetch_add(e - b) + (e - b);
        if (search.progress != nullptr) {
          search.progress->store(v, std::memory_order_relaxed);
        }
        if (search.stop.stopRequested()) {
          halt.store(true, std::memory_order_relaxed);
        }
      }
      settle(w.back, w.backRuns);
      settle(w.oneWay, w.oneWayRuns);
    });
    search.visited = visited.load();
    if (!saved) {
      ioFailed();
      return;
    }
    if (hit.load()) {
      goalNext = true;
      break;
    }
    if (halt.load()) {
      search.outcome = SlideOutcome::Stopped;
      return;
    }

    std::vector<Vec> runs;
    std::vector<Vec> oneWayRuns;
    std::size_t pieces = 0;
    for (Work& w : work) {
      for (Vec& r : w.backRuns) {
        pieces += r.size();
        runs.push_back(std::move(r));
      }
      for (Vec& r : w.oneWayRuns) {
        pieces += r.size();
        oneWayRuns.push_back(std::move(r));
      }
    }
    work.clear();
    Vec oneWay;
    mergeRuns(oneWayRuns, oneWay, *pool, threads);
    const std::size_t oneWayReached = oneWay.size();

    // The arrangements reached only by moves that cannot be made backwards
    // may be anywhere in the levels on disk.
    if (!oneWay.empty() && !visitedSets.empty()) {
      std::vector<char> found(oneWay.size(), 0);
      std::atomic<bool> readFailed{false};
      const std::size_t part = (oneWay.size() + threads - 1) / threads;
      forEachIndex(*pool, threads, threads, [&](std::size_t th) {
        const std::size_t b = std::min(oneWay.size(), th * part);
        const std::size_t e = std::min(oneWay.size(), b + part);
        if (b >= e) {
          return;
        }
        for (const SetPtr& v : visitedSets) {
          typename Set::Reader reader(*v);
          if (!reader.ok() || !v->find(oneWay.data() + b, e - b, found.data() + b, reader)) {
            readFailed.store(true);
          }
        }
      });
      if (readFailed.load()) {
        ioFailed();
        return;
      }
      std::size_t w = 0;
      for (std::size_t i = 0; i < oneWay.size(); i++) {
        if (found[i] == 0) {
          oneWay[w++] = oneWay[i];
        }
      }
      oneWay.resize(w);
    }
    const std::size_t oneWayNew = oneWay.size();
    search.oneWayRepeats += oneWayReached - oneWayNew;
    if (!oneWay.empty()) {
      runs.push_back(std::move(oneWay));
    }
    mergeRuns(runs, next, *pool, threads);

    const std::pair<std::uint64_t, std::uint64_t> bytes = storedBytes();
    search.diskBytes = std::max(search.diskBytes, bytes.first);
    if (search.onLevel) {
      char line[256];
      std::snprintf(
          line, sizeof(line),
          "level %llu: %zu arrangements (%zu in pieces), one-way %zu, %zu new, %zu sets, "
          "disk %.1f MB, index %.1f MB, %.1f s",
          static_cast<unsigned long long>(depth + 1), next.size(), pieces, oneWayReached,
          oneWayNew, visitedSets.size(), static_cast<double>(bytes.first) / 1e6,
          static_cast<double>(bytes.second) / 1e6,
          std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
      search.onLevel(line);
    }
    if (bytes.first > diskBudget && !folder.dir.empty()) {
      search.outcome = SlideOutcome::Disk;
      char gb[32];
      std::snprintf(gb, sizeof(gb), "%.1f GB", static_cast<double>(diskBudget) / 1e9);
      search.error = "The search filled the disk space it may use (" + std::string(gb) + " in " +
                     folder.dir.string() + ").";
      return;
    }

    // Whether the next level fits in memory: this one, the new one, and
    // the pieces and merge of the one after, which grows as this one did.
    const double grows = std::clamp(
        cur.empty() ? 1.0 : static_cast<double>(next.size()) / static_cast<double>(cur.size()),
        1.0, 4.0);
    const double piecesEach =
        next.empty() ? 1.0
                     : std::max(1.0, static_cast<double>(pieces) / static_cast<double>(next.size()));
    const double after = static_cast<double>(next.size()) * grows;
    const double held = static_cast<double>(cur.size()) + static_cast<double>(next.size()) +
                        2 * piecesEach * after +
                        static_cast<double>(bytes.second) / static_cast<double>(sizeof(Key));
    if (held * static_cast<double>(sizeof(Key)) >
        static_cast<double>(memoryLimit) * static_cast<double>(kLevelStateKeys * sizeof(Key))) {
      search.outcome = SlideOutcome::Memory;
      return;
    }

    // prev <- cur <- next, and the old prev is room for the next level
    prev.swap(cur);
    cur.swap(next);
    next.clear();
    depth++;
    releaseFreeMemory();
  }
  search.depth = depth;
  if (!goalNext) {
    return;
  }

  // The goal, and the arrangement of cur it is one move from: the first
  // there is, so that every run finds the same path.
  SlideMover<Key>& mover = *movers[0];
  Key goalKey = 0;
  Key from = 0;
  for (const Key c : cur) {
    if (mover.expand(c, [&](Key raw, bool) {
          const Key k = s.canon(raw);
          if (!s.isGoal(k)) {
            return false;
          }
          goalKey = k;
          return true;
        })) {
      from = c;
      break;
    }
  }
  chain.push_back(goalKey);
  chain.push_back(from);

  // Back level by level: x is at level k, find one of level k - 1 that
  // reaches it in one move.
  Vec level;
  for (std::uint64_t k = depth; k > 0; k--) {
    const Key x = chain.back();
    const Vec* before = &prev;
    if (k - 1 < depth - 1) {
      if (!stored[static_cast<std::size_t>(k - 1)]->load(level)) {
        chain.clear();
        ioFailed();
        return;
      }
      before = &level;
    }
    bool have = false;
    Key y = 0;
    mover.expand(x, [&](Key raw, bool group) {
      if (group && !mover.undoable(raw)) {
        return false;
      }
      const Key c = s.canon(raw);
      if (!std::binary_search(before->begin(), before->end(), c)) {
        return false;
      }
      y = c;
      have = true;
      return true;
    });
    if (!have) {
      // x is reached only by moves that cannot be made backwards: try every
      // arrangement of the level before, keeping the first.
      std::atomic<std::size_t> best{before->size()};
      std::atomic<std::size_t> nextIndex{0};
      pool->run(threads, [&](unsigned int th) {
        SlideMover<Key>& m = *movers[th];
        for (;;) {
          const std::size_t b = nextIndex.fetch_add(1024);
          if (b >= before->size() || b >= best.load()) {
            break;
          }
          const std::size_t e = std::min<std::size_t>(b + 1024, before->size());
          for (std::size_t i = b; i < e && i < best.load(); i++) {
            if (m.expand((*before)[i], [&](Key raw, bool) { return s.canon(raw) == x; })) {
              std::size_t had = best.load();
              while (i < had && !best.compare_exchange_weak(had, i)) {
              }
              break;
            }
          }
        }
      });
      // there always is one; without it there is no path to give
      BT_ASSERT(best.load() < before->size());
      y = (*before)[best.load()];
    }
    chain.push_back(y);
  }
  std::reverse(chain.begin(), chain.end());
}

// The slide search over position tables: a move is a flood fill over the
// piece's own positions against a bit mask of the cells the others cover.
// A full search goes level by level (levelSearch), a limited one through a
// queue (queueSearch); fullThroughQueue makes a full search use the queue
// too, for A/B runs. False, having searched nothing, when the tables cannot
// hold this search.
template <class Key>
bool tableSearch(const Voxel& tray, const std::vector<Cell3List>& cells,
                 const std::vector<Cell3List>& outlines, bool nested, const SlideState& initial,
                 const GoalCheck& goals, SlideSearch& search, std::vector<SlideState>& path) {
  SlideSpace<Key> space;
  if (!space.build(tray, cells, outlines, nested, initial, goals, search.likePiecesApart)) {
    return false;
  }
  space.partial = nested && search.partialNested;
  std::vector<Key> chain;
  if (search.maxStates == kFullSearch && !search.fullThroughQueue) {
    levelSearch(space, search, chain);
  } else {
    queueSearch(space, search, chain);
  }
  if (!chain.empty()) {
    chainToPath(space, chain, path);
  }
  return true;
}

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

std::uint64_t memoryStates(std::uint64_t stateBytes, bool high) {
  if (stateBytes == 0) {
    stateBytes = 1;
  }
  std::uint64_t states = kSearchMemoryBytes / stateBytes;
  if (high) {
    states = std::max(states, physicalMemoryBytes() / 2 / stateBytes);
  }
  return states;
}

std::uint64_t maxStatesOf(SlidingSolver solver) {
  switch (solver) {
    case SlidingSolver::Fast:
      return kSearchStates;
    case SlidingSolver::Deep:
      return kDeepSearchStates;
    case SlidingSolver::Full:
      return kFullSearch;
  }
  return kSearchStates;
}

SlideSearch slideSearchFor(const SlidingOptions& options) {
  SlideSearch s;
  s.maxStates = maxStatesOf(options.solver);
  s.highMemory = options.highMemory;
  s.nested = options.nested;
  s.partialNested = options.partialNested;
  s.workDir = options.workDir;
  s.diskBudget = options.diskBudget;
  s.legacy = options.legacy;
  s.likePiecesApart = options.likePiecesApart;
  s.fullThroughQueue = options.fullThroughQueue;
  return s;
}

std::optional<Separation> findSlidePath(const Puzzle& puzzle, const Problem& problem,
                                        const Assembly& start, std::uint64_t maxStates,
                                        bool nested) {
  SlideSearch search;
  search.maxStates = maxStates;
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
    searched =
        tableSearch<std::uint64_t>(tray, cells, outlines, nested, initial, goals, search, path);
#ifdef __SIZEOF_INT128__
    if (!searched) {
      searched =
          tableSearch<WideKey>(tray, cells, outlines, nested, initial, goals, search, path);
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
      st.set(i, path[si].places[i].x, path[si].places[i].y, start.getZ(i),
             path[si].places[i].trans);
    }
    sep.addstate(std::move(st));
  }
  return sep;
}

std::vector<std::pair<int, int>> slideRoute(const Puzzle& puzzle, const Problem& problem,
                                            const Separation& path, unsigned int step,
                                            std::vector<unsigned int>* movers) {
  std::vector<std::pair<int, int>> none;
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
  int sx = 0;
  int sy = 0;
  for (unsigned int i = 0; i < n; i++) {
    const int dx = b.getX(i) - a.getX(i);
    const int dy = b.getY(i) - a.getY(i);
    if (dx == 0 && dy == 0) {
      continue;
    }
    if (a.getOrient(i) != b.getOrient(i)) {
      return none;
    }
    if (!moving.empty() && (dx != sx || dy != sy)) {
      return none;
    }
    sx = dx;
    sy = dy;
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
  return fewestTurns(problem.resultShape(puzzle), cells, st, moving, sx, sy);
}

void applySlideRoutes(const Puzzle& puzzle, const Problem& problem, const Separation& path,
                      DisasmToMoves& anim) {
  for (unsigned int step = 0; step < path.getMoves(); step++) {
    std::vector<unsigned int> movers;
    const std::vector<std::pair<int, int>> route =
        slideRoute(puzzle, problem, path, step, &movers);
    // a straight run animates correctly without help
    if (route.size() < 3) {
      continue;
    }
    std::vector<std::pair<float, float>> offsets;
    offsets.reserve(route.size());
    for (const std::pair<int, int>& c : route) {
      offsets.emplace_back(static_cast<float>(c.first), static_cast<float>(c.second));
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

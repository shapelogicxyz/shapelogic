// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/sliding.cpp: goalCheck_c, the cells and outlines of
// the pieces, the free cells and reachable shifts, nested groups. The stop
// grid and the route with the fewest turns are new.
#include "shapelogic/solve/slide_board.h"

#include "shapelogic/solve/search_sample.h"

#include <algorithm>
#include <map>
#include <queue>
#include <set>
#include <sstream>

namespace sl::sliding::detail {

void putSample(SearchSample* sample, const SlideState& st) {
  if (sample == nullptr || !sample->claim()) {
    return;
  }
  SearchSnapshot snap;
  snap.placements.resize(st.places.size());
  for (std::size_t i = 0; i < st.places.size(); i++) {
    snap.placements[i].transformation = st.places[i].trans;
    snap.placements[i].xpos = st.places[i].x;
    snap.placements[i].ypos = st.places[i].y;
    snap.placements[i].zpos = st.places[i].z;
  }
  sample->put(std::move(snap));
}

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

bool sameCells(const Cell3List& a, const Cell3List& b) {
  return a.size() == b.size() &&
         std::equal(a.begin(), a.end(), b.begin(), [](const Cell3& p, const Cell3& q) {
           return p.x == q.x && p.y == q.y && p.z == q.z;
         });
}

unsigned int nestedBound(const std::vector<Cell3List>& cells,
                         const std::vector<Cell3List>& outlines) {
  const std::size_t n = cells.size();
  using Cell = std::array<int, 3>;
  std::vector<std::set<Cell>> holes(n);
  for (std::size_t i = 0; i < n; i++) {
    for (const Cell3& c : outlines[i]) {
      holes[i].insert({c.x, c.y, c.z});
    }
    for (const Cell3& c : cells[i]) {
      holes[i].erase({c.x, c.y, c.z});
    }
  }
  // fits[b]: the pieces that fit b's holes at some shift
  std::vector<std::vector<std::size_t>> fits(n);
  std::vector<std::size_t> room(n, 0);
  for (std::size_t b = 0; b < n; b++) {
    std::size_t smallest = 0;
    for (std::size_t a = 0; a < n; a++) {
      if (a == b || cells[a].empty() || cells[a].size() > holes[b].size()) {
        continue;
      }
      const Cell3& first = cells[a][0];
      const bool fitsIn = std::any_of(holes[b].begin(), holes[b].end(), [&](const Cell& h) {
        return std::all_of(cells[a].begin(), cells[a].end(), [&](const Cell3& c) {
          return holes[b].contains(
              {c.x - first.x + h[0], c.y - first.y + h[1], c.z - first.z + h[2]});
        });
      });
      if (fitsIn) {
        fits[b].push_back(a);
        smallest = smallest == 0 ? cells[a].size() : std::min(smallest, cells[a].size());
      }
    }
    room[b] = smallest == 0 ? 0 : holes[b].size() / smallest;
  }
  std::size_t most = 0;
  for (std::size_t outer = 0; outer < n; outer++) {
    // everything that could be in the group: what fits outer, what fits that
    std::vector<char> in(n, 0);
    std::vector<std::size_t> queue{outer};
    in[outer] = 1;
    std::size_t rooms = 0;
    for (std::size_t head = 0; head < queue.size(); head++) {
      rooms += room[queue[head]];
      for (const std::size_t a : fits[queue[head]]) {
        if (in[a] == 0) {
          in[a] = 1;
          queue.push_back(a);
        }
      }
    }
    most = std::max(most, std::min(queue.size() - 1, rooms));
  }
  return static_cast<unsigned int>(most);
}

StopGrid stopGridFor(int size, const std::vector<Cell3List>& cells,
                     const std::vector<Cell3List>& outlines,
                     const std::vector<std::vector<Place>>& known) {
  StopGrid g;
  g.size = size;
  const std::size_t n = cells.size();
  g.offsets.assign(n, {});
  if (size <= 1) {
    return g;
  }
  auto add = [&g](std::size_t i, int x, int y) {
    const std::array<int, 2> o{g.mod(x), g.mod(y)};
    std::vector<std::array<int, 2>>& v = g.offsets[i];
    if (std::find(v.begin(), v.end(), o) == v.end()) {
      v.push_back(o);
    }
  };
  for (std::size_t i = 0; i < n; i++) {
    for (std::size_t j = 0; j < n; j++) {
      if (sameCells(cells[i], cells[j])) {
        for (const Place& p : known[j]) {
          add(i, p.x, p.y);
        }
      }
    }
  }

  const std::vector<std::vector<std::array<int, 2>>> base = g.offsets;
  for (std::size_t j = 0; j < n; j++) {
    if (base[j].empty() || outlines[j].size() <= cells[j].size()) {
      continue;
    }
    std::set<std::array<int, 3>> own;
    for (const Cell3& c : cells[j]) {
      own.insert({c.x, c.y, c.z});
    }
    std::set<std::array<int, 3>> inside;
    for (const Cell3& c : outlines[j]) {
      if (own.count({c.x, c.y, c.z}) == 0) {
        inside.insert({c.x, c.y, c.z});
      }
    }
    for (std::size_t i = 0; i < n; i++) {
      if (i == j || cells[i].empty() || cells[i].size() > inside.size()) {
        continue;
      }
      // every shift that puts piece i's first cell on a cell of the hole
      for (const std::array<int, 3>& c : inside) {
        const int dx = c[0] - cells[i][0].x;
        const int dy = c[1] - cells[i][0].y;
        const int dz = c[2] - cells[i][0].z;
        const bool fits = std::all_of(cells[i].begin(), cells[i].end(), [&](const Cell3& k) {
          return inside.count({k.x + dx, k.y + dy, k.z + dz}) != 0;
        });
        if (fits) {
          for (const std::array<int, 2>& o : base[j]) {
            add(i, o[0] + dx, o[1] + dy);
          }
        }
      }
    }
  }
  for (std::vector<std::array<int, 2>>& v : g.offsets) {
    std::sort(v.begin(), v.end());
  }
  return g;
}

std::vector<std::vector<Place>> knownPlaces(const Voxel& tray, const std::vector<Cell3List>& cells,
                                            const SlideState& initial, const GoalCheck& goals) {
  const int W = asInt(tray.getX());
  const int H = asInt(tray.getY());
  const int D = asInt(tray.getZ());
  std::vector<std::vector<Place>> known(cells.size());
  for (std::size_t i = 0; i < cells.size(); i++) {
    const Place& home = initial.places[i];
    known[i].push_back(home);
    if (cells[i].empty()) {
      continue;
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
    for (int z = -minZ; z < D - maxZ; z++) {
      for (int y = -minY; y < H - maxY; y++) {
        for (int x = -minX; x < W - maxX; x++) {
          const Place p{x, y, home.trans, z};
          if (goals.fillsGoal(i, p)) {
            known[i].push_back(p);
          }
        }
      }
    }
  }
  return known;
}

namespace {

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

}  // namespace

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

}  // namespace sl::sliding::detail

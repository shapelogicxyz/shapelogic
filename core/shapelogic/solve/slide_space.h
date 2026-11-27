// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The slide search's position tables, keys and moves (internal to the
/// slide search, cleanup item 21.1): PosTable, SlideSpace (an arrangement
/// as a Key), SlideMover (the moves of an arrangement, forwards and
/// backwards) and the chain of arrangements turned back into a path.

#include "shapelogic/base/assert.h"
#include "shapelogic/base/int_cast.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/solve/slide_board.h"
#include "shapelogic/solve/slide_key.h"
#include "shapelogic/solve/sliding_search.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <type_traits>
#include <vector>

namespace sl::sliding::detail {

#ifdef __SIZEOF_INT128__
// The key of a search whose arrangements do not fit 64 bits. Those that do
// not fit 128 take a BigKey (slide_key.h; eachKey has the order).
__extension__ using WideKey = unsigned __int128;
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
  // The neighboring position one step along kDx/kDy/kDz, or -1.
  std::vector<std::array<int, kDirs>> next;
  // The tray cells each position covers, `words` 64-bit words a position.
  std::vector<std::uint64_t> mask;
  // With nested slides: the cells of the piece's outline at each position,
  // laid out as mask is. A piece is nested in this one when its mask lies
  // wholly inside.
  std::vector<std::uint64_t> outline;
  std::vector<char> atGoal;
  // Whether a piece may stop at each position (SlideSearch::grid).
  std::vector<char> rest;
  // Loose stops (see buildLoose). The first `real` positions are places;
  // each one after stands for the piece at every stop of one class, the
  // stops it has wholly inside one cell of the grid: its mask is the cells
  // any of them covers, and core[class] the cells all of them cover. cls
  // gives each position's class, -1 for none.
  std::size_t real = 0;
  std::vector<int> cls;
  std::vector<std::vector<int>> members;
  std::vector<std::uint64_t> core;
  // What a packed arrangement holds for the piece: the code of its
  // position. Only the positions it may stop at have one (-1 for the
  // others), counted in position order, so that the few stops a grid leaves
  // take few bits; row gives each code's position. Without a grid, and with
  // SlideSearch::plainKeys, every position's code is its own number.
  std::vector<int> code;
  std::vector<int> row;
  // The bits the codes take, and where the code sits in a packed
  // arrangement (SlideSpace::layout). A copy kept with the others as one
  // number (SlideSpace::Pack) has no field of its own.
  unsigned int width = 0;
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

// The loose stops of piece i (SlideSearch::grid with nested slides, unless
// gridStopsApart): its stops that lie wholly inside one cell of the grid,
// two or more, are a class, and the class gets a position number of its own
// after the real ones. A piece that reaches every stop of a class in one
// move is then one arrangement, not one for each stop: in a cell with
// nothing else in it, the stops differ only in which hole of a block the
// piece would fit, and that is settled when a block arrives. The cells are
// counted from (ox, oy), the corner of the floor. Only for a piece with
// offsets on the grid and nothing but its own cells in its outline (no
// piece nests in a loose one).
inline void buildLoose(PosTable& t, const Cell3List& cells, const Cell3List& outline,
                const StopGrid& stops, std::size_t i, int ox, int oy, std::size_t words) {
  if (stops.size <= 1 || stops.offsets[i].empty() || outline.size() != cells.size()) {
    return;
  }
  int minX = cells[0].x;
  int maxX = minX;
  int minY = cells[0].y;
  int maxY = minY;
  for (const Cell3& c : cells) {
    minX = std::min(minX, c.x);
    maxX = std::max(maxX, c.x);
    minY = std::min(minY, c.y);
    maxY = std::max(maxY, c.y);
  }
  auto cellOf = [&](int v) { return (v - stops.mod(v)) / stops.size; };
  std::map<std::array<int, 3>, std::vector<int>> byCell;
  for (std::size_t k = 0; k < t.real; k++) {
    if (t.rest[k] == 0) {
      continue;
    }
    const int cx = cellOf(t.px[k] + minX - ox);
    const int cy = cellOf(t.py[k] + minY - oy);
    if (cellOf(t.px[k] + maxX - ox) == cx && cellOf(t.py[k] + maxY - oy) == cy) {
      byCell[{t.pz[k], cy, cx}].push_back(asInt(k));
    }
  }
  for (auto& [cell, list] : byCell) {
    if (list.size() < 2) {
      continue;
    }
    const int c = asInt(t.members.size());
    const std::size_t first = idx(list[0]);
    t.px.push_back(t.px[first]);
    t.py.push_back(t.py[first]);
    t.pz.push_back(t.pz[first]);
    std::vector<std::uint64_t> any(words, 0);
    std::vector<std::uint64_t> all(words, ~std::uint64_t{0});
    char goal = 0;
    for (const int k : list) {
      t.cls[idx(k)] = c;
      const std::uint64_t* m = &t.mask[idx(k) * words];
      for (std::size_t w = 0; w < words; w++) {
        any[w] |= m[w];
        all[w] &= m[w];
      }
      goal = (goal != 0 || t.atGoal[idx(k)] != 0) ? 1 : 0;
    }
    t.mask.insert(t.mask.end(), any.begin(), any.end());
    if (!t.outline.empty()) {
      t.outline.insert(t.outline.end(), any.begin(), any.end());
    }
    t.core.insert(t.core.end(), all.begin(), all.end());
    t.atGoal.push_back(goal);
    t.rest.push_back(1);
    t.cls.push_back(-1);
    std::array<int, kDirs> none{};
    none.fill(-1);
    t.next.push_back(none);
    t.members.push_back(std::move(list));
  }
}

// The bits that number `count` values, at least one.
inline unsigned int bitsFor(std::uint64_t count) {
  unsigned int width = 1;
  while (width < 64 && (std::uint64_t{1} << width) < count) {
    width++;
  }
  return width;
}

// The low `width` bits set.
inline std::uint64_t maskFor(unsigned int width) {
  return width >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << width) - 1;
}

// Builds the position tables. False when they cannot hold this search: a
// piece with no cells or nowhere to go, or a start off the floor; and
// (unlike the original, which overran its fixed arrays there) more than 64
// pieces with nested slides. Whether the codes fit a key is the layout's to
// say (SlideSpace::layout). With `loose`, the pieces get their loose stops
// (buildLoose); with `plainKeys`, every position a code (PosTable::code).
inline bool buildTables(const Voxel& tray, const std::vector<Cell3List>& cells,
                 const std::vector<Cell3List>& outlines, bool nested, const SlideState& initial,
                 const GoalCheck& goals, bool vertical, const StopGrid& stops, bool loose,
                 bool plainKeys, std::size_t& words, std::vector<PosTable>& tables) {
  const int W = asInt(tray.getX());
  const int H = asInt(tray.getY());
  const int D = asInt(tray.getZ());
  words = (idx(W) * idx(H) * idx(D) + 63) / 64;
  const auto n = static_cast<unsigned int>(cells.size());
  if (nested && n > 64) {
    return false;
  }
  tables.assign(n, PosTable());

  // the corner of the floor, which the grid's cells are counted from
  int floorX = W;
  int floorY = H;
  if (loose) {
    for (int z = 0; z < D; z++) {
      for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
          if (isFloor(tray, x, y, z)) {
            floorX = std::min(floorX, x);
            floorY = std::min(floorY, y);
          }
        }
      }
    }
  }

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
          t.rest.push_back(stops.allows(i, x, y) ? 1 : 0);
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
    t.real = count;
    t.cls.assign(count, -1);
    if (loose && nested) {
      buildLoose(t, cells[i], outlines[i], stops, i, floorX, floorY, words);
    }

    t.code.assign(t.px.size(), -1);
    for (std::size_t k = 0; k < t.px.size(); k++) {
      if (plainKeys || t.rest[k] != 0) {
        t.code[k] = asInt(t.row.size());
        t.row.push_back(asInt(k));
      }
    }
    t.width = bitsFor(t.row.size());
  }
  return true;
}

// The arrangements of one slide search over position tables. An arrangement
// is one code per piece, that of its position (PosTable::code), packed into
// a Key: 64 bits, 128 for trays that need more, or a BigKey for those that
// need more still. positions and encode turn a key to the pieces' positions
// and back (posOf, lift and put for one piece): everything else works on
// positions. Read only once built, so every thread of a search shares one.
//
// Packed keys (decision 0037): the copies of a piece, which the search does
// not tell apart, are kept as one number that says which codes they hold
// (Pack), not as a code each. A key is then the same whichever copy sits
// where (canon has nothing to do), and takes fewer bits: four copies with
// 64 places each take 20 bits, not 24.
template <class Key>
struct SlideSpace {
  // Pieces of an arrangement that covers looks at: with more that could be
  // loose it misses some of the arrangements that stand for it.
  static constexpr unsigned int kMaxCovered = 6;

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
  // SlideSearch::verticalMoves and grid.
  bool vertical = false;
  StopGrid stops;
  // Whether any piece has loose stops (PosTable::members): then an
  // arrangement may hold loose positions, and stands for the piece at
  // every stop of the class (concretes).
  bool loose = false;
  // Whether some position of a piece has no code (PosTable::code): the keys
  // are not those of a search with a code for every position.
  bool compact = false;
  // With nested: the moves can be worked out backwards too (SlideMover::
  // movesBack), so the search may run from the goal as well (tableSearch).
  bool backMoves = false;
  // The goal, for the pieces whose copies count together (GoalCheck::counts).
  const GoalCheck* goals = nullptr;
  // With nested: whether a piece's outline has a cell that is not its own.
  // A piece without one holds nothing wherever the pieces are (no piece
  // shares a cell with it), so its group is itself and need not be looked
  // for (SlideMover::groupAt goes through every piece for it).
  std::vector<char> holds;

  // A run of copies of a piece kept in a key as one number (packed keys).
  // Copies never share a position, so the codes they hold are one of the
  // ways of choosing so many of the piece's codes, and the number is that
  // way's place among them all (the combinatorial number system): the sum
  // of C(c, j) over the codes c, the j-th smallest of them counted from 1.
  struct Pack {
    // The copies, in the order the rising codes go to them.
    std::vector<unsigned int> pieces;
    unsigned int shift = 0;
    std::uint64_t bits = 0;
    // choose[j * span + c] is C(c, j + 1), for the codes c and one more.
    std::vector<std::uint64_t> choose;
    std::size_t span = 0;
    // Whether a copy has a place that does not fill its goal.
    bool hasGoal = false;
  };
  // The most copies a pack holds: a group of more is kept as several, the
  // lowest codes in the first.
  static constexpr std::size_t kMaxPack = 16;
  // Whether the copies are packed (layout). Never with loose stops.
  bool packed = false;
  std::vector<Pack> packs;
  // The packs of each group of `copies`, in the order of its pieces.
  std::vector<std::vector<unsigned int>> groupPacks;
  // The group of `copies` a piece is packed with; -1 for a piece with a
  // field of its own, which is every piece when the keys are not packed.
  std::vector<int> packGroup;
  // A packed piece's pack, and its place among the pack's pieces.
  std::vector<unsigned int> packOf;
  std::vector<unsigned int> packAt;
  // The pieces with a field of their own.
  std::vector<unsigned int> singles;
  // Those of them with a place that does not fill their goal: the others
  // are at their goal wherever they are (a piece without one), and isGoal,
  // asked of every arrangement a search from the start alone reaches, need
  // not look where they are.
  std::vector<unsigned int> goalSingles;
  // The bits of a key the arrangements take.
  unsigned int keyBits = 0;
  // The start: each piece's position.
  std::vector<int> startRows;

  // False when the tables cannot hold this search, or the arrangements do
  // not fit the key. With looseStops the pieces get their loose stops
  // (buildLoose); with plainKeys every position has a code; with pack the
  // copies of a piece are packed (never with loose stops). partialNest
  // (with nest) sets `partial`; `backMoves` follows from the tables, so
  // every builder gets both (cleanup item 11.8).
  bool build(const Voxel& tray, const std::vector<Cell3List>& cells,
             const std::vector<Cell3List>& outlines, bool nest, bool partialNest,
             const SlideState& init,
             const GoalCheck& goalCheck, bool likePiecesApart, bool verticalMoves,
             const StopGrid& stopGrid, bool looseStops, bool plainKeys, bool pack) {
    goals = &goalCheck;
    vertical = verticalMoves;
    stops = stopGrid;
    if (!buildTables(tray, cells, outlines, nest, init, goalCheck, vertical, stops, looseStops,
                     plainKeys, words, t)) {
      return false;
    }
    loose = std::any_of(t.begin(), t.end(), [](const PosTable& p) { return !p.members.empty(); });
    compact = std::any_of(t.begin(), t.end(),
                          [](const PosTable& p) { return p.row.size() != p.px.size(); });
    n = static_cast<unsigned int>(init.places.size());
    nested = nest;
    holds.assign(n, 0);
    for (unsigned int i = 0; nest && i < n; i++) {
      holds[i] = outlines[i].size() > cells[i].size() ? 1 : 0;
    }
    initial = init;
    trayW = asInt(tray.getX());
    trayH = asInt(tray.getY());
    trayD = asInt(tray.getZ());

    // Pieces that are copies of one another (same cells, same places, same
    // goal) can swap places without changing the puzzle. Each such group
    // is kept with its positions in rising order, so the arrangements that
    // differ only by which copy sits where are searched as one.
    copies.clear();
    if (!likePiecesApart) {
      std::vector<std::vector<unsigned int>> groups;
      for (unsigned int i = 0; i < n; i++) {
        bool placed = false;
        for (std::vector<unsigned int>& g : groups) {
          const unsigned int j = g[0];
          if (sameCells(cells[i], cells[j]) && t[i].px == t[j].px && t[i].py == t[j].py &&
              t[i].pz == t[j].pz && t[i].atGoal == t[j].atGoal && t[i].code == t[j].code) {
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
    if (!layout(pack && !loose)) {
      return false;
    }

    // a piece's start is one of its stops (stopGridFor)
    startRows.resize(n);
    for (unsigned int i = 0; i < n; i++) {
      const Place& p = init.places[i];
      startRows[i] = t[i].at(p.x, p.y, p.z);
    }
    startKey = encode(startRows.data());
    startCanon = canon(startKey);
    partial = nest && partialNest;
    // the moves are made backwards only from arrangements without loose
    // stops or partly nested pieces
    backMoves = nested && !loose && !partial && nestedBound(cells, outlines) <= kMaxBackNested;
    return true;
  }

  // Gives the pieces their places in a key, in the pieces' order: a field
  // each, or with `pack` the copies of a piece as one number (Pack). False
  // when they take more bits than the key has.
  bool layout(bool pack) {
    const auto room = static_cast<unsigned int>(8 * sizeof(Key));
    packed = pack && !copies.empty();
    packs.clear();
    groupPacks.assign(copies.size(), {});
    packGroup.assign(n, -1);
    packOf.assign(n, 0);
    packAt.assign(n, 0);
    singles.clear();
    goalSingles.clear();
    if (packed) {
      for (std::size_t g = 0; g < copies.size(); g++) {
        for (const unsigned int i : copies[g]) {
          packGroup[i] = asInt(g);
        }
      }
    }
    unsigned int used = 0;
    const auto take = [&](unsigned int width, unsigned int& shift, std::uint64_t& bits) {
      if (width > room - used) {
        return false;
      }
      shift = used;
      bits = maskFor(width);
      used += width;
      return true;
    };
    for (unsigned int i = 0; i < n; i++) {
      const int g = packGroup[i];
      if (g < 0) {
        if (!take(t[i].width, t[i].shift, t[i].bits)) {
          return false;
        }
        singles.push_back(i);
        if (std::any_of(t[i].atGoal.begin(), t[i].atGoal.end(),
                        [](char at) { return at == 0; })) {
          goalSingles.push_back(i);
        }
        continue;
      }
      const std::vector<unsigned int>& group = copies[idx(g)];
      if (group[0] != i) {
        continue;
      }
      // C(c, j) for the codes c and one more, a row for each j from 1 up to
      // the copies a pack takes: as many as are numbered in 63 bits
      const std::size_t codes = t[i].row.size();
      const std::size_t span = codes + 1;
      const std::uint64_t limit = std::uint64_t{1} << 63;
      std::vector<std::uint64_t> choose(span);
      for (std::size_t c = 0; c < span; c++) {
        choose[c] = c;
      }
      std::size_t most = 1;
      while (most < std::min(kMaxPack, group.size())) {
        std::vector<std::uint64_t> row(span, 0);
        const std::uint64_t* above = &choose[(most - 1) * span];
        bool fits = true;
        for (std::size_t c = 1; c < span && fits; c++) {
          fits = above[c - 1] <= limit - row[c - 1];
          row[c] = row[c - 1] + above[c - 1];
        }
        if (!fits) {
          break;
        }
        choose.insert(choose.end(), row.begin(), row.end());
        most++;
      }
      const bool hasGoal = std::any_of(t[i].atGoal.begin(), t[i].atGoal.end(),
                                       [](char at) { return at == 0; });
      for (std::size_t from = 0; from < group.size(); from += most) {
        const std::size_t size = std::min(most, group.size() - from);
        Pack pk;
        pk.pieces.assign(group.begin() + static_cast<std::ptrdiff_t>(from),
                         group.begin() + static_cast<std::ptrdiff_t>(from + size));
        pk.span = span;
        pk.choose.assign(choose.begin(),
                         choose.begin() + static_cast<std::ptrdiff_t>(size * span));
        pk.hasGoal = hasGoal;
        // the ways of choosing `size` of the codes
        if (!take(bitsFor(choose[(size - 1) * span + codes]), pk.shift, pk.bits)) {
          return false;
        }
        for (std::size_t j = 0; j < size; j++) {
          packOf[pk.pieces[j]] = static_cast<unsigned int>(packs.size());
          packAt[pk.pieces[j]] = static_cast<unsigned int>(j);
        }
        groupPacks[idx(g)].push_back(static_cast<unsigned int>(packs.size()));
        packs.push_back(std::move(pk));
      }
    }
    keyBits = used;
    return true;
  }

  // The number of a pack whose copies hold codes[], rising.
  [[nodiscard]] static std::uint64_t rank(const Pack& pk, const int* codes) {
    std::uint64_t r = 0;
    for (std::size_t j = 0; j < pk.pieces.size(); j++) {
      SL_ASSERT(j == 0 || codes[j] > codes[j - 1]);
      r += pk.choose[j * pk.span + idx(codes[j])];
    }
    return r;
  }

  // The codes a pack's copies hold when its number is r, rising, into
  // codes[]: from the greatest, each the greatest code that adds no more
  // than is left of the number.
  static void unrank(const Pack& pk, std::uint64_t r, int* codes) {
    std::size_t below = pk.span - 1;
    for (std::size_t j = pk.pieces.size(); j-- > 0;) {
      if (below == 0) {
        // no key the search makes: one read from a damaged file
        codes[j] = 0;
        continue;
      }
      const std::uint64_t* row = &pk.choose[j * pk.span];
      const std::uint64_t* at = std::upper_bound(row, row + below, r) - 1;
      below = static_cast<std::size_t>(at - row);
      codes[j] = static_cast<int>(below);
      r -= *at;
    }
  }

  // The position of every piece of key, into rows[n]. With packed keys the
  // copies of a piece come in the order of their positions.
  void positions(const Key& key, int* rows) const {
    for (const unsigned int i : singles) {
      rows[i] = t[i].row[keyField(key, t[i].shift, t[i].bits)];
    }
    std::array<int, kMaxPack> codes{};
    for (const Pack& pk : packs) {
      unrank(pk, keyField(key, pk.shift, pk.bits), codes.data());
      for (std::size_t j = 0; j < pk.pieces.size(); j++) {
        const unsigned int i = pk.pieces[j];
        rows[i] = t[i].row[idx(codes[j])];
      }
    }
  }

  [[nodiscard]] int posOf(const Key& key, unsigned int i) const {
    if (packGroup[i] < 0) {
      return t[i].row[keyField(key, t[i].shift, t[i].bits)];
    }
    const Pack& pk = packs[packOf[i]];
    std::array<int, kMaxPack> codes{};
    unrank(pk, keyField(key, pk.shift, pk.bits), codes.data());
    return t[i].row[idx(codes[packAt[i]])];
  }

  // The key of the arrangement with the pieces at rows[n], each a position
  // its piece may stop at. Not packed, every piece is where rows has it
  // (the key is not canon'd); packed, the copies of a piece are one number
  // whichever of them sits where.
  [[nodiscard]] Key encode(const int* rows) const {
    Key key = 0;
    for (const unsigned int i : singles) {
      key |= own(i, rows[i]);
    }
    std::array<int, 64> few{};
    std::vector<int> many;
    for (std::size_t g = 0; g < groupPacks.size(); g++) {
      if (groupPacks[g].empty()) {
        continue;
      }
      const std::vector<unsigned int>& group = copies[g];
      int* codes = few.data();
      if (group.size() > few.size()) {
        many.resize(group.size());
        codes = many.data();
      }
      for (std::size_t k = 0; k < group.size(); k++) {
        codes[k] = t[group[k]].code[idx(rows[group[k]])];
        SL_ASSERT(codes[k] >= 0);
      }
      std::sort(codes, codes + group.size());
      const int* from = codes;
      for (const unsigned int at : groupPacks[g]) {
        const Pack& pk = packs[at];
        key |= keyAt<Key>(rank(pk, from), pk.shift);
        from += pk.pieces.size();
      }
    }
    return key;
  }

  // key without piece i, to put it down elsewhere (put). Packed, the piece
  // goes with its copies: key without their number, and the codes the
  // others hold, rising, into rest. rows[n] is key's positions.
  [[nodiscard]] Key lift(const Key& key, const int* rows, unsigned int i,
                         std::vector<int>& rest) const {
    const int g = packGroup[i];
    if (g < 0) {
      return keyClear(key, t[i].shift, t[i].bits);
    }
    rest.clear();
    for (const unsigned int m : copies[idx(g)]) {
      if (m == i) {
        continue;
      }
      // nearly rising already: the copies' positions come in order
      const int code = t[m].code[idx(rows[m])];
      std::size_t at = rest.size();
      rest.push_back(code);
      for (; at > 0 && rest[at - 1] > code; at--) {
        rest[at] = rest[at - 1];
      }
      rest[at] = code;
    }
    Key clear = key;
    for (const unsigned int at : groupPacks[idx(g)]) {
      clear = keyClear(clear, packs[at].shift, packs[at].bits);
    }
    return clear;
  }

  // lift's key with piece i put down at position p, one it may stop at.
  [[nodiscard]] Key put(const Key& clear, const std::vector<int>& rest, unsigned int i,
                        int p) const {
    const int g = packGroup[i];
    if (g < 0) {
      return clear | own(i, p);
    }
    const int code = t[i].code[idx(p)];
    SL_ASSERT(code >= 0);
    // the copies' numbers with the code in its place among the others
    Key key = clear;
    std::size_t next = 0;
    bool placed = false;
    for (const unsigned int at : groupPacks[idx(g)]) {
      const Pack& pk = packs[at];
      std::uint64_t r = 0;
      for (std::size_t j = 0; j < pk.pieces.size(); j++) {
        int c = code;
        if (placed || (next < rest.size() && rest[next] < code)) {
          c = rest[next++];
        } else {
          placed = true;
        }
        r += pk.choose[j * pk.span + idx(c)];
      }
      key |= keyAt<Key>(r, pk.shift);
    }
    return key;
  }

  // Piece i's part of a key with the piece at position p, one it may stop
  // at: the others have no code. For a piece with a field of its own.
  [[nodiscard]] Key own(unsigned int i, int p) const {
    const int code = t[i].code[idx(p)];
    SL_ASSERT(code >= 0);
    return keyAt<Key>(static_cast<std::uint64_t>(code), t[i].shift);
  }

  // The same, for the searches whose keys are not packed (those with loose
  // stops work on the fields themselves).
  [[nodiscard]] Key field(unsigned int i, int p) const {
    SL_ASSERT(!packed);
    return own(i, p);
  }

  // key with piece i at position p. Not for packed keys.
  [[nodiscard]] Key with(const Key& key, unsigned int i, int p) const {
    return keyClear(key, t[i].shift, t[i].bits) | field(i, p);
  }

  // The arrangement with the pieces at rows[n].
  [[nodiscard]] SlideState stateOf(const int* rows) const {
    SlideState st = initial;
    for (unsigned int i = 0; i < n; i++) {
      st.places[i].x = t[i].px[idx(rows[i])];
      st.places[i].y = t[i].py[idx(rows[i])];
      st.places[i].z = t[i].pz[idx(rows[i])];
    }
    return st;
  }

  // The arrangements key stands for: each loose piece at every stop of its
  // class. Key itself when no piece is loose.
  void concretes(Key key, std::vector<Key>& out) const {
    out.assign(1, key);
    for (unsigned int i = 0; i < n; i++) {
      const int p = posOf(key, i);
      if (idx(p) < t[i].real) {
        continue;
      }
      const std::vector<int>& stopsOf = t[i].members[idx(p) - t[i].real];
      const std::size_t have = out.size();
      for (std::size_t m = 1; m < stopsOf.size(); m++) {
        for (std::size_t k = 0; k < have; k++) {
          out.push_back(with(out[k], i, stopsOf[m]));
        }
      }
      for (std::size_t k = 0; k < have; k++) {
        out[k] = with(out[k], i, stopsOf[0]);
      }
    }
  }

  // The arrangements that stand for key and for more, canon'd, added to
  // out: key with pieces loose that are at a stop of a class, where no
  // other piece lies on the class's other stops (the arrangement could not
  // be otherwise) and no two of them could meet. occ is scratch.
  void covers(const Key key, std::vector<std::uint64_t>& occ, std::vector<Key>& out) const {
    std::array<unsigned int, kMaxCovered> piece{};
    std::array<int, kMaxCovered> code{};
    unsigned int count = 0;
    for (unsigned int i = 0; i < n && count < kMaxCovered; i++) {
      if (t[i].members.empty()) {
        continue;
      }
      const std::size_t at = idx(posOf(key, i));
      if (at < t[i].real && t[i].cls[at] >= 0) {
        piece[count] = i;
        code[count++] = asInt(t[i].real) + t[i].cls[at];
      }
    }
    if (count == 0) {
      return;
    }
    occ.assign(words, 0);
    for (unsigned int i = 0; i < n; i++) {
      const std::uint64_t* m = &t[i].mask[idx(posOf(key, i)) * words];
      for (std::size_t w = 0; w < words; w++) {
        occ[w] |= m[w];
      }
    }
    unsigned int kept = 0;
    for (unsigned int c = 0; c < count; c++) {
      const PosTable& tp = t[piece[c]];
      const std::uint64_t* own = &tp.mask[idx(posOf(key, piece[c])) * words];
      const std::uint64_t* all = &tp.mask[idx(code[c]) * words];
      bool clear = true;
      for (std::size_t w = 0; w < words && clear; w++) {
        clear = (all[w] & ~own[w] & occ[w]) == 0;
      }
      if (clear) {
        piece[kept] = piece[c];
        code[kept++] = code[c];
      }
    }
    // the pieces whose classes share a cell are not loose together
    std::array<unsigned int, kMaxCovered> meets{};
    for (unsigned int a = 0; a < kept; a++) {
      const std::uint64_t* ma = &t[piece[a]].mask[idx(code[a]) * words];
      for (unsigned int b = a + 1; b < kept; b++) {
        const std::uint64_t* mb = &t[piece[b]].mask[idx(code[b]) * words];
        for (std::size_t w = 0; w < words; w++) {
          if ((ma[w] & mb[w]) != 0) {
            meets[a] |= 1U << b;
            meets[b] |= 1U << a;
            break;
          }
        }
      }
    }
    for (unsigned int set = 1; set < (1U << kept); set++) {
      Key k = key;
      bool apart = true;
      for (unsigned int a = 0; a < kept && apart; a++) {
        if (((set >> a) & 1U) != 0) {
          apart = (meets[a] & set) == 0;
          k = with(k, piece[a], code[a]);
        }
      }
      if (apart) {
        out.push_back(canon(k));
      }
    }
  }

  // A loose piece is shown at the first stop of its class.
  [[nodiscard]] SlideState decode(const Key& key) const {
    std::vector<int> rows(n);
    positions(key, rows.data());
    return stateOf(rows.data());
  }

  // The arrangement standing for key and every one that differs from it
  // only by which copy sits where. Copies share their codes, which rise
  // with the positions, so the codes are sorted as they are. A packed key
  // is that arrangement already.
  [[nodiscard]] Key canon(Key key) const {
    if (packed) {
      return key;
    }
    for (const std::vector<unsigned int>& g : copies) {
      // a move changes one position, so this is nearly sorted already
      for (std::size_t a = 1; a < g.size(); a++) {
        for (std::size_t b = a; b > 0; b--) {
          const PosTable& lo = t[g[b - 1]];
          const PosTable& hi = t[g[b]];
          const std::uint64_t pl = keyField(key, lo.shift, lo.bits);
          const std::uint64_t ph = keyField(key, hi.shift, hi.bits);
          if (pl <= ph) {
            break;
          }
          key = keyClear(keyClear(key, lo.shift, lo.bits), hi.shift, hi.bits) |
                keyAt<Key>(ph, lo.shift) | keyAt<Key>(pl, hi.shift);
        }
      }
    }
    return key;
  }

  // The goal when it is one arrangement, canonical: then the search can
  // run from both ends. None with pieces that may end in several places,
  // copies that count together, or a goal where a piece may not stop.
  // Copies of a piece (see build) are one arrangement when there is one
  // way for them to take the places they may end and stop at, none lying
  // on another: a copy at each of as many places, or places that overlap
  // (two blocks to end side by side fit their goal's cells at every shift
  // between) but hold all the copies in one way only.
  [[nodiscard]] std::optional<Key> singleGoal() const {
    if (goals != nullptr && goals->counts()) {
      return std::nullopt;
    }
    std::vector<std::vector<unsigned int>> groups = copies;
    std::vector<char> grouped(n, 0);
    for (const std::vector<unsigned int>& g : copies) {
      for (const unsigned int i : g) {
        grouped[i] = 1;
      }
    }
    for (unsigned int i = 0; i < n; i++) {
      if (grouped[i] == 0) {
        groups.push_back({i});
      }
    }
    std::vector<int> goalRows(n, 0);
    std::vector<std::uint64_t> covered(words, 0);
    for (const std::vector<unsigned int>& group : groups) {
      const PosTable& tp = t[group[0]];
      std::vector<int> rows;
      for (std::size_t p = 0; p < tp.atGoal.size(); p++) {
        if (tp.atGoal[p] != 0 && tp.rest[p] != 0) {
          rows.push_back(static_cast<int>(p));
        }
      }
      std::vector<std::uint64_t> used(words, 0);
      std::vector<int> chosen;
      std::vector<int> only;
      int ways = 0;
      int tries = kMaxGoalTries;
      goalWays(tp, rows, 0, group.size(), used, chosen, only, ways, tries);
      if (ways != 1 || tries <= 0) {
        return std::nullopt;
      }
      for (std::size_t k = 0; k < group.size(); k++) {
        // pieces that would end on one another have no such arrangement
        const std::uint64_t* m = &tp.mask[idx(only[k]) * words];
        for (std::size_t w = 0; w < words; w++) {
          if ((covered[w] & m[w]) != 0) {
            return std::nullopt;
          }
          covered[w] |= m[w];
        }
        goalRows[group[k]] = only[k];
      }
    }
    return canon(encode(goalRows.data()));
  }

  // singleGoal: the ways `left` more copies take rows from `from` on, none
  // on another. `ways` is counted to 2, the first way kept in `only`;
  // `tries` bounds the work (0 or less: given up).
  static constexpr int kMaxGoalTries = 100000;
  void goalWays(const PosTable& tp, const std::vector<int>& rows, std::size_t from,
                std::size_t left, std::vector<std::uint64_t>& used, std::vector<int>& chosen,
                std::vector<int>& only, int& ways, int& tries) const {
    if (left == 0) {
      if (ways++ == 0) {
        only = chosen;
      }
      return;
    }
    for (std::size_t r = from; r + left <= rows.size() && ways < 2 && tries > 0; r++) {
      tries--;
      const std::uint64_t* m = &tp.mask[idx(rows[r]) * words];
      bool clash = false;
      for (std::size_t w = 0; w < words; w++) {
        clash = clash || (used[w] & m[w]) != 0;
      }
      if (clash) {
        continue;
      }
      for (std::size_t w = 0; w < words; w++) {
        used[w] |= m[w];
      }
      chosen.push_back(rows[r]);
      goalWays(tp, rows, r + 1, left - 1, used, chosen, only, ways, tries);
      chosen.pop_back();
      for (std::size_t w = 0; w < words; w++) {
        used[w] &= ~m[w];
      }
    }
  }

  [[nodiscard]] bool isGoal(const Key& key) const {
    if (goals != nullptr && goals->counts()) {
      if (!packed) {
        return goals->reached([&](std::size_t i) {
          return t[i].atGoal[idx(posOf(key, static_cast<unsigned int>(i)))];
        });
      }
      // every copy's position worked out once
      std::array<int, 64> few{};
      std::vector<int> many;
      int* rows = few.data();
      if (n > few.size()) {
        many.resize(n);
        rows = many.data();
      }
      positions(key, rows);
      return goals->reached([&](std::size_t i) { return t[i].atGoal[idx(rows[i])]; });
    }
    for (const unsigned int i : goalSingles) {
      if (t[i].atGoal[idx(t[i].row[keyField(key, t[i].shift, t[i].bits)])] == 0) {
        return false;
      }
    }
    std::array<int, kMaxPack> codes{};
    for (const Pack& pk : packs) {
      if (!pk.hasGoal) {
        continue;
      }
      unrank(pk, keyField(key, pk.shift, pk.bits), codes.data());
      for (std::size_t j = 0; j < pk.pieces.size(); j++) {
        const PosTable& tp = t[pk.pieces[j]];
        if (tp.atGoal[idx(tp.row[idx(codes[j])])] == 0) {
          return false;
        }
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
        next_(space.n),
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
  // them, which is the order of legacySearch. With solosOnly the pieces
  // move alone only.
  template <class F>
  bool expand(const Key cur, F&& emit, bool solosOnly = false) {
    s_.positions(cur, pos_.data());
    return expandAt(cur, emit, solosOnly);
  }

  // expand from the arrangement with the pieces at rows[n], every piece
  // where rows has it: with emitted, the moves of pieces that keep their
  // own places, which a packed key does not tell.
  template <class F>
  bool expandFrom(const int* rows, F&& emit) {
    std::copy(rows, rows + s_.n, pos_.begin());
    return expandAt(s_.encode(rows), emit, false);
  }

  // While emit runs: where every piece is in the arrangement being told,
  // each piece of the one expanded keeping its own place, into rows[n].
  void emitted(const Key& key, int* rows) const {
    if (!s_.packed) {
      s_.positions(key, rows);
      return;
    }
    if (groupMove_) {
      std::copy(next_.begin(), next_.end(), rows);
      return;
    }
    std::copy(pos_.begin(), pos_.end(), rows);
    rows[movedPiece_] = movedRow_;
  }

  // expand, with cur's positions in pos_ already.
  template <class F>
  bool expandAt(const Key cur, F& emit, bool solosOnly) {
    const std::vector<PosTable>& t = s_.t;
    const std::size_t words = s_.words;
    const unsigned int n = s_.n;
    std::fill(occupied_.begin(), occupied_.end(), 0);
    for (unsigned int i = 0; i < n; i++) {
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

      nextEpoch();
      seen_[pi][idx(pos_[pi])] = epoch_;
      fill_.assign(1, pos_[pi]);
      const Key clear = s_.lift(cur, pos_.data(), pi, rest_);
      movedPiece_ = pi;
      groupMove_ = false;
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
          movedRow_ = nb;
          if (emit(s_.put(clear, rest_, pi, nb), false)) {
            return true;
          }
        }
      }

      if (!s_.nested || solosOnly || s_.holds[pi] == 0) {
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
        othersWithout(group.data(), members);
        if (floodShifts(cur, group.data(), members, [&](Key key) { return emit(key, true); })) {
          return true;
        }
      }
    }
    return false;
  }

  // The moves made backwards, for the search from the goal with nested
  // slides (SlideSpace::backMoves): emit(before, oneWay) with every
  // arrangement that cur is one move from, until emit returns true; then it
  // returns true itself.
  //
  // A piece that moved alone goes back the way it came. A group that came
  // to cur was its outer piece and some of what is nested in it at cur: it
  // may have closed round the rest on its way. So the outer piece is slid
  // away with each part of what it holds, the rest left where it is, and
  // an arrangement counts where that part is all that is nested in the
  // outer piece there (from there the group's move leads to cur).
  //
  // oneWay as moves tells it, for a search backwards: set when no move of
  // cur leads to `before`, which is when the group left part of what it
  // holds behind (cur's own move takes all of it).
  template <class F>
  bool movesBack(const Key cur, F&& emit) {
    SL_ASSERT(s_.backMoves);
    if (expand(cur, [&](Key before, bool) { return emit(before, false); }, true)) {
      return true;
    }
    // expand left cur's positions in pos_ and its cells in occupied_
    for (unsigned int pi = 0; pi < s_.n; pi++) {
      if (s_.holds[pi] == 0) {
        continue;
      }
      std::array<unsigned int, 64> full{};
      unsigned int fullCount = 0;
      const std::uint64_t fullSet = groupAt(pos_.data(), pi, full.data(), fullCount, false);
      if (fullCount < 2) {
        continue;
      }
      // every part of what is nested, as the bits of a number
      const std::uint64_t parts = (std::uint64_t{1} << (fullCount - 1)) - 1;
      for (std::uint64_t part = 1; part <= parts; part++) {
        std::array<unsigned int, 64> group{};
        unsigned int members = 0;
        group[members++] = pi;
        std::uint64_t inGroup = std::uint64_t{1} << pi;
        for (unsigned int j = 1; j < fullCount; j++) {
          if (((part >> (j - 1)) & 1) != 0) {
            group[members++] = full[j];
            inGroup |= std::uint64_t{1} << full[j];
          }
        }
        othersWithout(group.data(), members);
        // undoable: whether the outer piece holds just this group there
        lastOuter_ = pi;
        lastGroup_ = inGroup;
        lastWide_ = false;
        const bool oneWay = inGroup != fullSet;
        if (floodShifts(cur, group.data(), members,
                        [&](Key before) { return undoable(before) && emit(before, oneWay); })) {
          return true;
        }
      }
    }
    return false;
  }

  // For the group move expand is reporting: whether the same pieces are
  // nested in the mover at next, so that the move can be made backwards. A
  // group can close round a piece it passes, and then would take it along.
  bool undoable(Key next) {
    // a packed key does not say which copy sits where: the flood's own
    // positions do (floodShifts)
    const int* at = next_.data();
    if (!s_.packed) {
      for (unsigned int i = 0; i < s_.n; i++) {
        backPos_[i] = s_.posOf(next, i);
      }
      at = backPos_.data();
    }
    std::array<unsigned int, 64> group{};
    unsigned int members = 0;
    return groupAt(at, lastOuter_, group.data(), members, lastWide_) == lastGroup_;
  }

  // The moves as the level search takes them: emit(next, oneWay), where
  // oneWay says the move made backwards may not lead to cur itself, so
  // that next may be an arrangement of any earlier level and not only of
  // the two newest. Without loose stops that is a group move that cannot
  // be undone; with them see expandLoose.
  template <class F>
  bool moves(const Key cur, F&& emit) {
    if (s_.loose) {
      return expandLoose(cur, emit);
    }
    return expand(cur, [&](Key next, bool group) { return emit(next, group && !undoable(next)); });
  }

  // expand for arrangements that may hold loose positions (SlideSpace::
  // loose): every arrangement one move from one of those cur stands for,
  // told in as few arrangements as the classes allow, each of which stands
  // only for arrangements so reached (or for cur's own).
  //
  // A piece that reaches every stop of a class is told once, loose. A
  // piece that moves clear of the cells the loose pieces may cover is
  // worked out once, whatever stops those are at; one that could touch
  // such cells is worked out for each stop of the loose pieces it could
  // touch, and where it gets to depends on them, is told with them in
  // place.
  //
  // oneWay is set for the moves that, made backwards, do not give cur
  // itself: those of a piece that can reach every other stop of the class
  // it stands in (backwards they give the loose arrangement), those told
  // with a loose piece of cur in place (backwards the piece stays in
  // place), and the group moves that cannot be undone.
  template <class F>
  bool expandLoose(const Key cur, F&& emit) {
    const std::vector<PosTable>& t = s_.t;
    const std::size_t words = s_.words;
    const unsigned int n = s_.n;
    std::fill(occupied_.begin(), occupied_.end(), 0);
    looseList_.clear();
    s_.positions(cur, pos_.data());
    for (unsigned int i = 0; i < n; i++) {
      const std::size_t at = idx(pos_[i]);
      const bool loose = at >= t[i].real;
      const std::uint64_t* m =
          loose ? &t[i].core[(at - t[i].real) * words] : &t[i].mask[at * words];
      for (std::size_t w = 0; w < words; w++) {
        occupied_[w] |= m[w];
      }
      if (loose) {
        looseList_.push_back(i);
      }
    }
    // the cells each loose piece covers at some of its stops but not all
    const std::size_t nl = looseList_.size();
    if (nl > 0) {
      soft_.assign(nl * words, 0);
      softAll_.assign(words, 0);
      looseAll_.assign(words, 0);
      for (std::size_t k = 0; k < nl; k++) {
        const unsigned int q = looseList_[k];
        const std::size_t at = idx(pos_[q]);
        const std::uint64_t* any = &t[q].mask[at * words];
        const std::uint64_t* all = &t[q].core[(at - t[q].real) * words];
        for (std::size_t w = 0; w < words; w++) {
          soft_[k * words + w] = any[w] & ~all[w];
          softAll_[w] |= soft_[k * words + w];
          looseAll_[w] |= any[w];
        }
      }
    }

    for (unsigned int pi = 0; pi < n; pi++) {
      const PosTable& tp = t[pi];
      const std::size_t at = idx(pos_[pi]);
      const bool moverLoose = at >= tp.real;
      const std::uint64_t* mine =
          moverLoose ? &tp.core[(at - tp.real) * words] : &tp.mask[at * words];
      for (std::size_t w = 0; w < words; w++) {
        others_[w] = occupied_[w] & ~mine[w];
      }
      const std::uint64_t* soft = nullptr;
      if (moverLoose && nl > 1) {
        softOthers_.assign(words, 0);
        for (std::size_t k = 0; k < nl; k++) {
          if (looseList_[k] != pi) {
            for (std::size_t w = 0; w < words; w++) {
              softOthers_[w] |= soft_[k * words + w];
            }
          }
        }
        soft = softOthers_.data();
      } else if (!moverLoose && nl > 0) {
        soft = softAll_.data();
      }
      // a loose piece moves from any of its stops: they are one move apart
      // (checked below), so from the first
      const int start = moverLoose ? tp.members[at - tp.real][0] : pos_[pi];
      const Key clear = keyClear(cur, tp.shift, tp.bits);
      const std::uint64_t touched = fillFrom(pi, start, others_.data(), soft);
      if (touched == 0) {
        if (moverLoose && !reachedAll(pi, asInt(at - tp.real))) {
          return expandConcretes(cur, emit);
        }
        if (emitSolo(clear, pi, fill_.data() + 1, fill_.size() - 1, false, emit)) {
          return true;
        }
      } else {
        // each way the loose pieces it could touch can be placed, one
        // after another
        const std::size_t places = setDependent(touched);
        if (places > kMaxPlaces) {
          return expandConcretes(cur, emit);
        }
        const int own = moverLoose ? asInt(at - tp.real) : -1;
        const int home = moverLoose ? -1 : tp.cls[at];
        bool settled = false;
        reachedAt_.assign(tp.real * places, 0);
        reachCount_.assign(tp.real, 0);
        reachOrder_.clear();
        for (std::size_t place = 0; place < places; place++) {
          blockAt(place, others_.data());
          fillFrom(pi, start, blocked_.data(), nullptr);
          if (moverLoose) {
            if (!reachedAll(pi, own)) {
              return expandConcretes(cur, emit);
            }
          } else if (home >= 0) {
            settled = settled || reachedAll(pi, home);
          }
          for (std::size_t f = 1; f < fill_.size(); f++) {
            const std::size_t r = idx(fill_[f]);
            if (tp.rest[r] != 0) {
              reachedAt_[r * places + place] = 1;
              if (reachCount_[r]++ == 0) {
                reachOrder_.push_back(fill_[f]);
              }
            }
          }
        }
        if (home >= 0) {
          // where it stands it is, however the others are placed
          std::fill_n(&reachedAt_[at * places], places, static_cast<unsigned char>(1));
        }
        classDone_.assign(tp.members.size(), 0);
        classRows_.resize(tp.members.size() * places);
        rowLeft_.resize(places);
        for (const int reached : reachOrder_) {
          const std::size_t r = idx(reached);
          const unsigned char* row = &reachedAt_[r * places];
          const Key to = clear | s_.field(pi, reached);
          const int c = tp.cls[r];
          if (c < 0) {
            if (emitPlaces(row, to, settled, emit)) {
              return true;
            }
            continue;
          }
          if (c == own) {
            continue;
          }
          // the places at which it reaches the whole class: loose there
          unsigned char* whole = &classRows_[idx(c) * places];
          if (classDone_[idx(c)] == 0) {
            classDone_[idx(c)] = 1;
            std::fill_n(whole, places, static_cast<unsigned char>(1));
            for (const int m : tp.members[idx(c)]) {
              for (std::size_t x = 0; x < places; x++) {
                whole[x] &= reachedAt_[idx(m) * places + x];
              }
            }
            if (emitPlaces(whole, clear | s_.field(pi, asInt(tp.real) + c), settled, emit)) {
              return true;
            }
          }
          for (std::size_t x = 0; x < places; x++) {
            rowLeft_[x] = row[x] & static_cast<unsigned char>(whole[x] == 0);
          }
          if (emitPlaces(rowLeft_.data(), to, settled, emit)) {
            return true;
          }
        }
      }

      // no piece nests in a loose one (buildLoose)
      if (!s_.nested || moverLoose) {
        continue;
      }
      std::uint64_t whole = 0;
      for (const bool wide : {false, true}) {
        if (wide && !s_.partial) {
          break;
        }
        std::array<unsigned int, 64> group{};
        unsigned int members = 0;
        const std::uint64_t inGroup = groupAt(pos_.data(), pi, group.data(), members, wide);
        // a loose piece in the group's outlines would go with it from some
        // of its stops and not from others
        if (nl > 0 && looseNear(pos_.data(), group.data(), members)) {
          return expandConcretes(cur, emit);
        }
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
        othersWithout(group.data(), members);
        const std::uint64_t near = floodGroup(cur, group.data(), members, others_.data(),
                                              nl > 0 ? softAll_.data() : nullptr);
        if (near == 0) {
          for (const GroupStop& stop : groupStops_) {
            if (emit(stop.key, !undoableLoose(stop.key))) {
              return true;
            }
          }
          continue;
        }
        const std::size_t places = setDependent(near);
        if (places > kMaxPlaces) {
          return expandConcretes(cur, emit);
        }
        groupAll_.clear();
        for (std::size_t place = 0; place < places; place++) {
          blockAt(place, others_.data());
          floodGroup(cur, group.data(), members, blocked_.data(), nullptr);
          for (GroupStop stop : groupStops_) {
            stop.place = place;
            groupAll_.push_back(stop);
          }
        }
        std::sort(groupAll_.begin(), groupAll_.end(), [](const GroupStop& a, const GroupStop& b) {
          return a.shift != b.shift ? a.shift < b.shift : a.place < b.place;
        });
        rowLeft_.resize(places);
        for (std::size_t a = 0; a < groupAll_.size();) {
          std::size_t b = a;
          std::fill(rowLeft_.begin(), rowLeft_.end(), static_cast<unsigned char>(0));
          for (; b < groupAll_.size() && groupAll_[b].shift == groupAll_[a].shift; b++) {
            rowLeft_[groupAll_[b].place] = 1;
          }
          const Key to = groupAll_[a].key;
          if (emitPlaces(rowLeft_.data(), to, b - a != places || !undoableLoose(to), emit)) {
            return true;
          }
          a = b;
        }
      }
    }
    return false;
  }

 private:
  // A shift at which every piece of a group may stop (floodGroup), the
  // arrangement there, and for expandLoose which stops the loose pieces
  // were at (setDependent).
  struct GroupStop {
    std::size_t shift = 0;
    std::size_t place = 0;
    Key key = 0;
  };

  // A flood fill of piece pi from `start` against `blocked`: the positions
  // reached into fill_, start first, and marked in seen_. `soft` is the
  // cells loose pieces may cover or not: they do not block, and the loose
  // pieces (bits over looseList_) whose such cells a reached position
  // covers are returned.
  std::uint64_t fillFrom(unsigned int pi, int start, const std::uint64_t* blocked,
                         const std::uint64_t* soft) {
    const PosTable& tp = s_.t[pi];
    const std::size_t words = s_.words;
    nextEpoch();
    seen_[pi][idx(start)] = epoch_;
    fill_.assign(1, start);
    std::uint64_t touched = 0;
    for (std::size_t head = 0; head < fill_.size(); head++) {
      for (std::size_t d = 0; d < kDirs; d++) {
        const int nb = tp.next[idx(fill_[head])][d];
        if (nb < 0 || seen_[pi][idx(nb)] == epoch_) {
          continue;
        }
        const std::uint64_t* m = &tp.mask[idx(nb) * words];
        if (overlaps(m, blocked, words)) {
          continue;
        }
        seen_[pi][idx(nb)] = epoch_;
        fill_.push_back(nb);
        if (soft != nullptr && overlaps(m, soft, words)) {
          touched |= softTouched(pi, m);
        }
      }
    }
    return touched;
  }

  // The loose pieces other than `mover` (bits over looseList_) with a cell
  // they may cover or not under m.
  std::uint64_t softTouched(unsigned int mover, const std::uint64_t* m) const {
    std::uint64_t touched = 0;
    for (std::size_t k = 0; k < looseList_.size(); k++) {
      if (looseList_[k] != mover && overlaps(m, &soft_[k * s_.words], s_.words)) {
        touched |= std::uint64_t{1} << k;
      }
    }
    return touched;
  }

  // Whether the last fill of piece pi reached every stop of its class c
  // (where it started counts).
  bool reachedAll(unsigned int pi, int c) const {
    for (const int m : s_.t[pi].members[idx(c)]) {
      if (seen_[pi][idx(m)] != epoch_) {
        return false;
      }
    }
    return true;
  }

  // The moves of piece pi alone to the stops in list, which it reaches
  // wherever the loose pieces are: a class reached whole is told once,
  // loose. clear is cur without the piece.
  template <class F>
  bool emitSolo(Key clear, unsigned int pi, const int* list, std::size_t count, bool oneWay,
                F& emit) {
    const PosTable& tp = s_.t[pi];
    if (tp.members.empty()) {
      for (std::size_t f = 0; f < count; f++) {
        if (tp.rest[idx(list[f])] != 0 && emit(clear | s_.field(pi, list[f]), oneWay)) {
          return true;
        }
      }
      return false;
    }
    const std::size_t at = idx(pos_[pi]);
    // the class the piece is loose in (it is there already), and the class
    // of the stop it stands at
    const int own = at >= tp.real ? asInt(at - tp.real) : -1;
    const int home = at >= tp.real ? -1 : tp.cls[at];
    classCount_.assign(tp.members.size(), 0);
    for (std::size_t f = 0; f < count; f++) {
      const std::size_t nb = idx(list[f]);
      if (tp.rest[nb] != 0 && tp.cls[nb] >= 0) {
        classCount_[idx(tp.cls[nb])]++;
      }
    }
    // from one stop of a class to all its others: the piece is loose there
    // from the next move on, and its moves made backwards give that loose
    // arrangement
    if (home >= 0 && classCount_[idx(home)] + 1 == tp.members[idx(home)].size()) {
      oneWay = true;
    }
    for (std::size_t f = 0; f < count; f++) {
      const std::size_t nb = idx(list[f]);
      if (tp.rest[nb] == 0) {
        continue;
      }
      std::size_t p = nb;
      const int c = tp.cls[nb];
      if (c >= 0) {
        if (c == own || classCount_[idx(c)] == kClassTold) {
          continue;
        }
        if (classCount_[idx(c)] + (c == home ? 1 : 0) == tp.members[idx(c)].size()) {
          classCount_[idx(c)] = kClassTold;
          p = tp.real + idx(c);
        }
      }
      if (emit(clear | s_.field(pi, asInt(p)), oneWay)) {
        return true;
      }
    }
    return false;
  }

  // The loose pieces of `touched` (bits over looseList_) into dep_, and how
  // many ways they can be placed: each a place, numbered from 0.
  std::size_t setDependent(std::uint64_t touched) {
    dep_.clear();
    depSize_.clear();
    depStride_.clear();
    std::size_t places = 1;
    for (std::size_t k = 0; k < looseList_.size(); k++) {
      if (((touched >> k) & 1) != 0) {
        const unsigned int q = looseList_[k];
        dep_.push_back(q);
        depSize_.push_back(s_.t[q].members[idx(pos_[q]) - s_.t[q].real].size());
        depStride_.push_back(places);
        // past kMaxPlaces the moves are worked out the long way
        places = std::min(places * depSize_.back(), kMaxPlaces + 1);
      }
    }
    places_ = places;
    return places;
  }

  // The stop of the j-th piece of dep_ at `place`; place is left as the
  // number for the pieces after.
  int stopAt(std::size_t j, std::size_t& place) const {
    const unsigned int q = dep_[j];
    const std::vector<int>& stops = s_.t[q].members[idx(pos_[q]) - s_.t[q].real];
    const int stop = stops[place % stops.size()];
    place /= stops.size();
    return stop;
  }

  // blocked_: `others` and the pieces of dep_ at `place`.
  void blockAt(std::size_t place, const std::uint64_t* others) {
    const std::size_t words = s_.words;
    blocked_.assign(others, others + words);
    for (std::size_t j = 0; j < dep_.size(); j++) {
      const int stop = stopAt(j, place);
      const std::uint64_t* m = &s_.t[dep_[j]].mask[idx(stop) * words];
      for (std::size_t w = 0; w < words; w++) {
        blocked_[w] |= m[w];
      }
    }
  }

  // Tells a move that can be made with the pieces of dep_ at the places
  // marked in row (a byte a place): key is the arrangement after it with
  // those pieces loose, as they were. Made at every place it is told once,
  // oneWayWhole; else once for each way of placing the pieces it depends
  // on, those in place and the others still loose, one way.
  template <class F>
  bool emitPlaces(const unsigned char* row, Key key, bool oneWayWhole, F& emit) {
    const std::size_t places = places_;
    std::size_t count = 0;
    for (std::size_t x = 0; x < places; x++) {
      count += row[x];
    }
    if (count == 0) {
      return false;
    }
    if (count == places) {
      return emit(key, oneWayWhole);
    }
    // a piece is not depended on when its every stop does as well as any
    const std::size_t deps = dep_.size();
    depFree_.assign(deps, 1);
    for (std::size_t x = 0; x < places; x++) {
      if (row[x] == 0) {
        continue;
      }
      for (std::size_t j = 0; j < deps; j++) {
        if (depFree_[j] == 0) {
          continue;
        }
        const std::size_t first = x - ((x / depStride_[j]) % depSize_[j]) * depStride_[j];
        for (std::size_t k = 0; k < depSize_[j]; k++) {
          if (row[first + k * depStride_[j]] == 0) {
            depFree_[j] = 0;
            break;
          }
        }
      }
    }
    for (std::size_t x = 0; x < places; x++) {
      if (row[x] == 0) {
        continue;
      }
      Key k = key;
      bool told = false;
      for (std::size_t j = 0; j < deps && !told; j++) {
        const std::size_t stop = (x / depStride_[j]) % depSize_[j];
        if (depFree_[j] != 0) {
          // told with the place where this piece is at its first stop
          told = stop != 0;
        } else {
          const unsigned int q = dep_[j];
          k = s_.with(k, q, s_.t[q].members[idx(pos_[q]) - s_.t[q].real][stop]);
        }
      }
      if (!told && emit(k, true)) {
        return true;
      }
    }
    return false;
  }

  // Whether a loose piece may cover a cell of the outlines of the group's
  // pieces at at[].
  bool looseNear(const int* at, const unsigned int* group, unsigned int members) const {
    for (unsigned int g = 0; g < members; g++) {
      const unsigned int a = group[g];
      if (overlaps(&s_.t[a].outline[idx(at[a]) * s_.words], looseAll_.data(), s_.words)) {
        return true;
      }
    }
    return false;
  }

  // A fill begins: nothing is seen yet (seen_[i][p] == epoch_).
  void nextEpoch() {
    if (++epoch_ == 0) {
      for (std::vector<unsigned int>& v : seen_) {
        std::fill(v.begin(), v.end(), 0);
      }
      epoch_ = 1;
    }
  }

  // others_: the cells of every piece but the group's, the pieces at pos_
  // (occupied_ has them all).
  void othersWithout(const unsigned int* group, unsigned int members) {
    const std::size_t words = s_.words;
    others_ = occupied_;
    for (unsigned int g = 0; g < members; g++) {
      const unsigned int m = group[g];
      const std::uint64_t* gm = &s_.t[m].mask[idx(pos_[m]) * words];
      for (std::size_t w = 0; w < words; w++) {
        others_[w] &= ~gm[w];
      }
    }
  }

  // A flood over the shifts the group can make from cur against the cells
  // in `blocked`, as for one piece: stop(shift, key) with the group's
  // arrangement at every shift where each of its pieces may stop (the
  // shift as its place in shiftSeen_), until stop returns true; then it
  // returns true itself. softOf(m, mask) is asked for each piece of the
  // group at each shift it fits (its number and cells there), and what it
  // answers for the shifts the whole group fits is or-ed together into
  // `touched`: the loose pieces the move could meet, for expandLoose
  // (floodGroup), nothing for a search without loose stops (floodShifts).
  // Both are this one fill, so that their moves come in one order. A
  // template: it runs for every arrangement with a group in it.
  template <class SoftOf, class Stop>
  bool floodOver(const Key cur, const unsigned int* group, unsigned int members,
                 const std::uint64_t* blocked, std::uint64_t& touched, SoftOf&& softOf,
                 Stop&& stop) {
    const std::vector<PosTable>& t = s_.t;
    const std::size_t words = s_.words;
    const bool packed = s_.packed;
    // Whether the group fits at shift d, and whether every piece of it
    // may stop there: then key is the group's arrangement (a place
    // where a piece may not stop has no code to put in one).
    auto shifted = [&](const SlideShift& d, Key& key, bool& rests) {
      key = cur;
      rests = true;
      std::uint64_t near = 0;
      for (unsigned int g = 0; g < members; g++) {
        const unsigned int m = group[g];
        const std::size_t at = idx(pos_[m]);
        const int p = t[m].at(t[m].px[at] + d[0], t[m].py[at] + d[1], t[m].pz[at] + d[2]);
        if (p < 0 || overlaps(&t[m].mask[idx(p) * words], blocked, words)) {
          return false;
        }
        near |= softOf(m, &t[m].mask[idx(p) * words]);
        moved_[g] = p;
        if (t[m].rest[idx(p)] == 0) {
          rests = false;
        } else if (rests && !packed) {
          key = s_.with(key, m, p);
        }
      }
      if (rests && packed) {
        // told from the positions: the group's own change, the others stay
        for (unsigned int g = 0; g < members; g++) {
          next_[group[g]] = moved_[g];
        }
        key = s_.encode(next_.data());
      }
      touched |= near;
      return true;
    };
    if (++shiftEpoch_ == 0) {
      std::fill(shiftSeen_.begin(), shiftSeen_.end(), 0);
      shiftEpoch_ = 1;
    }
    const int trayW = s_.trayW;
    const int trayH = s_.trayH;
    const int trayD = s_.trayD;
    auto indexOf = [&](const SlideShift& d) {
      return idx(((d[2] + trayD) * (2 * trayH + 1) + (d[1] + trayH)) * (2 * trayW + 1) +
                 (d[0] + trayW));
    };
    shiftSeen_[indexOf({0, 0, 0})] = shiftEpoch_;
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
        const std::size_t at = indexOf(nb);
        Key key = 0;
        bool rests = false;
        if (shiftSeen_[at] == shiftEpoch_ || !shifted(nb, key, rests)) {
          continue;
        }
        shiftSeen_[at] = shiftEpoch_;
        shiftQueue_.push_back(nb);
        // every piece of the group must be able to stop there
        if (rests && stop(at, key)) {
          return true;
        }
      }
    }
    return false;
  }

  // The group's moves from cur, the cells of the other pieces in others_:
  // each(key) with its arrangement at every shift where each of its pieces
  // may stop, until each returns true; then it returns true itself.
  template <class F>
  bool floodShifts(const Key cur, const unsigned int* group, unsigned int members, F&& each) {
    groupMove_ = true;
    if (s_.packed) {
      // the arrangement at each shift is told from the positions: the
      // group's own change in floodOver, the others stay
      next_.assign(pos_.begin(), pos_.end());
    }
    std::uint64_t none = 0;
    return floodOver(
        cur, group, members, others_.data(), none,
        [](unsigned int, const std::uint64_t*) { return std::uint64_t{0}; },
        [&](std::size_t, Key key) { return each(key); });
  }

  // undoable for expandLoose, of a move that left the loose pieces loose:
  // a loose piece the group's outlines reach at next could be taken along
  // on the way back.
  bool undoableLoose(Key next) {
    for (unsigned int i = 0; i < s_.n; i++) {
      backPos_[i] = s_.posOf(next, i);
    }
    std::array<unsigned int, 64> group{};
    unsigned int members = 0;
    if (groupAt(backPos_.data(), lastOuter_, group.data(), members, lastWide_) != lastGroup_) {
      return false;
    }
    return looseList_.empty() || !looseNear(backPos_.data(), group.data(), members);
  }

  // The group's moves from cur against `blocked`, as expand makes them: the
  // shifts at which every piece of the group may stop into groupStops_.
  // soft as for fillFrom. (Arrangements with loose stops are never packed.)
  std::uint64_t floodGroup(Key cur, const unsigned int* group, unsigned int members,
                           const std::uint64_t* blocked, const std::uint64_t* soft) {
    SL_ASSERT(!s_.packed);
    const std::size_t words = s_.words;
    std::uint64_t touched = 0;
    groupStops_.clear();
    floodOver(
        cur, group, members, blocked, touched,
        [&](unsigned int m, const std::uint64_t* mask) {
          return soft != nullptr && overlaps(mask, soft, words) ? softTouched(m, mask)
                                                                 : std::uint64_t{0};
        },
        [&](std::size_t shift, Key key) {
          groupStops_.push_back({shift, 0, key});
          return false;
        });
    return touched;
  }

  // Whether k is one of the arrangements `loose` stands for.
  bool standsFor(Key loose, Key k) const {
    for (unsigned int i = 0; i < s_.n; i++) {
      const int pl = s_.posOf(loose, i);
      const int pk = s_.posOf(k, i);
      if (pl == pk) {
        continue;
      }
      const PosTable& ti = s_.t[i];
      if (idx(pl) < ti.real || idx(pk) >= ti.real ||
          ti.cls[idx(pk)] != asInt(idx(pl) - ti.real)) {
        return false;
      }
    }
    return true;
  }

  // expandLoose the long way, for the arrangements it cannot take apart (a
  // loose piece in reach of a group's outlines, or one whose stops other
  // pieces have cut off from one another): the moves out of each
  // arrangement cur stands for, every piece in place and every move told
  // oneWay.
  template <class F>
  bool expandConcretes(const Key cur, F& emit) {
    s_.concretes(cur, concretes_);
    for (const Key c : concretes_) {
      if (expand(c, [&](Key next, bool) { return !standsFor(cur, next) && emit(next, true); })) {
        return true;
      }
    }
    return false;
  }

  static constexpr unsigned int kClassTold = ~0U;
  // The most ways of placing the loose pieces a move could touch that are
  // worked out one by one.
  static constexpr std::size_t kMaxPlaces = 1024;
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
  // The move being told, for emitted and (packed keys) undoable: a piece
  // alone to a position, or a group, whose arrangement is next_ (packed
  // keys only: pos_ with the group's pieces at the shift being told, where
  // moved_ has them).
  unsigned int movedPiece_ = 0;
  int movedRow_ = 0;
  bool groupMove_ = false;
  std::vector<int> next_;
  std::array<int, 64> moved_{};
  // The codes of the copies a lifted piece is packed with (SlideSpace::lift).
  std::vector<int> rest_;
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
  // expandLoose: the loose pieces of the arrangement; for each, the cells
  // it covers at some of its stops but not at all (soft_), those of all of
  // them together, and every cell a loose piece may cover.
  std::vector<unsigned int> looseList_;
  std::vector<std::uint64_t> soft_;
  std::vector<std::uint64_t> softAll_;
  std::vector<std::uint64_t> softOthers_;
  std::vector<std::uint64_t> looseAll_;
  // The loose pieces a mover could touch (setDependent): how many stops
  // each has, and how far apart its stops are in the numbering of the
  // places; the cells blocked with them at one place.
  std::vector<unsigned int> dep_;
  std::vector<std::size_t> depSize_;
  std::vector<std::size_t> depStride_;
  std::vector<unsigned char> depFree_;
  std::size_t places_ = 1;
  std::vector<std::uint64_t> blocked_;
  // The stops the mover reaches: at which places (a byte a stop and
  // place), at how many, and in the order first reached. The same for a
  // class reached whole, and a row to work in.
  std::vector<unsigned char> reachedAt_;
  std::vector<unsigned int> reachCount_;
  std::vector<int> reachOrder_;
  std::vector<unsigned char> classDone_;
  std::vector<unsigned char> classRows_;
  std::vector<unsigned char> rowLeft_;
  // How many stops of each class a mover reaches (emitSolo).
  std::vector<unsigned int> classCount_;
  std::vector<GroupStop> groupStops_;
  std::vector<GroupStop> groupAll_;
  std::vector<Key> concretes_;
};

// Whether k, every piece in place, is one of the arrangements `loose`
// stands for, give or take which copy sits where.
template <class Key>
bool standsForCopies(const SlideSpace<Key>& s, Key loose, Key k) {
  // each piece of k as loose would hold it: in place, or loose in the
  // class of its stop
  Key told = k;
  for (unsigned int i = 0; i < s.n; i++) {
    const PosTable& t = s.t[i];
    const int c = t.cls[idx(s.posOf(k, i))];
    if (c < 0) {
      continue;
    }
    const int code = asInt(t.real) + c;
    bool isLoose = s.posOf(loose, i) == code;
    for (const std::vector<unsigned int>& g : s.copies) {
      if (std::find(g.begin(), g.end(), i) != g.end()) {
        for (const unsigned int j : g) {
          isLoose = isLoose || s.posOf(loose, j) == code;
        }
      }
    }
    if (isLoose) {
      told = s.with(told, i, code);
    }
  }
  return s.canon(told) == s.canon(loose);
}

// A chain of the search's arrangements with every piece in place: from the
// goal backwards, each arrangement one of those the one kept stands for
// that is a move from the next. The search's moves give every arrangement
// a kept one stands for from one the kept one before stands for.
template <class Key>
void placeChain(const SlideSpace<Key>& s, SlideMover<Key>& mover, const std::vector<Key>& kept,
                std::vector<Key>& placed) {
  // the goal: a loose piece at a stop that fills its goal, when it has one
  Key at = kept.back();
  for (unsigned int i = 0; i < s.n; i++) {
    const PosTable& t = s.t[i];
    const std::size_t p = idx(s.posOf(at, i));
    if (p < t.real) {
      continue;
    }
    const std::vector<int>& stops = t.members[p - t.real];
    int stop = stops[0];
    for (const int m : stops) {
      if (t.atGoal[idx(m)] != 0) {
        stop = m;
        break;
      }
    }
    at = s.with(at, i, stop);
  }
  at = s.canon(at);
  std::vector<Key> all;
  placed.assign(1, at);
  for (std::size_t i = kept.size() - 1; i-- > 0;) {
    const Key before = kept[i];
    // a move back from `at`, where it can be made forwards again
    Key from = 0;
    bool have = mover.expand(at, [&](Key raw, bool group) {
      if ((group && !mover.undoable(raw)) || !standsForCopies(s, before, raw)) {
        return false;
      }
      from = s.canon(raw);
      return true;
    });
    if (!have && standsForCopies(s, before, at)) {
      // `before` stands for it already: no move
      continue;
    }
    if (!have) {
      // a group move that cannot be made backwards
      s.concretes(before, all);
      for (const Key c : all) {
        if (mover.expand(c, [&](Key raw, bool) { return s.canon(raw) == at; })) {
          from = s.canon(c);
          have = true;
          break;
        }
      }
    }
    SL_ASSERT(have);
    at = from;
    placed.push_back(at);
  }
  std::reverse(placed.begin(), placed.end());
}

// The path for chain, the arrangements from start to goal as the search
// kept them. Where copies were swapped about, each step is the move from
// the arrangement at hand that gives the next one kept, so every piece
// keeps its own identity along the path.
template <class Key>
void chainToPath(const SlideSpace<Key>& s, const std::vector<Key>& kept,
                 std::vector<SlideState>& path) {
  SlideMover<Key> mover(s);
  std::vector<Key> placed;
  if (s.loose) {
    placeChain(s, mover, kept, placed);
  }
  const std::vector<Key>& chain = s.loose ? placed : kept;
  // where every piece really is: a key kept by the search does not say
  // which copy sits where
  std::vector<int> real = s.startRows;
  std::vector<int> next(s.n);
  path.push_back(s.stateOf(real.data()));
  for (std::size_t i = 1; i < chain.size(); i++) {
    if (s.copies.empty()) {
      s.positions(chain[i], next.data());
    } else {
      const bool reached = mover.expandFrom(real.data(), [&](Key key, bool) {
        if (s.canon(key) != chain[i]) {
          return false;
        }
        mover.emitted(key, next.data());
        return true;
      });
      SL_ASSERT(reached);
    }
    real = next;
    path.push_back(s.stateOf(real.data()));
  }
}

}  // namespace sl::sliding::detail

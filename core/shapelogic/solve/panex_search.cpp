// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/panex.cpp: the keys (pkey_c, five bits a disc), the
// rules (rules_c, the stacking rules laid out for speed), the level
// operations (subtract, radixSort, mergeTwo, mergeRuns, mergeParts,
// parallelSort, meet), the saved search (store_c, savedState_c, BTPANEX3
// and BTPANEX4), the search (searcher_c: findSigma, shortestPath,
// nextLevel, traceStored, meetSearch), setup, classicTower, fingerprintOf,
// folderFor, shortCount, unsupported, searchFolder, savedSearch, savedMs,
// discardSaved and solve.
//
// The original ran each parallel step on threads of its own (parallel())
// and wrote a saved level on a thread of its own while the search went on
// to the next level. Here the steps run on a ThreadPool (decision 0004),
// which may lend fewer threads than asked: a step given in slices takes
// them from a counter (forEachIndex), and the level is written by the
// first job of the next level's expansion, or at once when something
// needs the file first (finishWrite). The files are the same either way.
#include "shapelogic/solve/panex_search.h"

#include "shapelogic/base/system.h"
#include "shapelogic/base/thread_pool.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/model/stacking.h"
#include "shapelogic/solve/sliding_search.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace bt::panex {

namespace {

// The saved files are the original's raw words, little-endian on every
// machine it ran on.
static_assert(std::endian::native == std::endian::little,
              "the saved Panex searches are little-endian");

constexpr unsigned int kMaxRods = 8;
// Five-bit symbols: discs 0..30, and 31 between rods.
constexpr unsigned int kMaxDiscs = 31;
constexpr unsigned int kSeparator = 31;
// 128 bits hold 25 symbols: the discs plus one separator per rod but one.
constexpr unsigned int kMaxSymbols = 25;

/// A stacking as a 128-bit key, the high word first in memory (the files
/// hold it so).
struct PKey {
  std::uint64_t hi = 0;
  std::uint64_t lo = 0;
};
static_assert(sizeof(PKey) == 16 && std::is_standard_layout_v<PKey>);

bool operator<(const PKey& a, const PKey& b) { return a.hi < b.hi || (a.hi == b.hi && a.lo < b.lo); }
bool operator==(const PKey& a, const PKey& b) { return a.hi == b.hi && a.lo == b.lo; }

using Layer = std::vector<PKey>;

// The discs on each rod, bottom to top.
struct PState {
  std::array<unsigned char, kMaxRods> count{};
  std::array<std::array<unsigned char, kMaxDiscs + 1>, kMaxRods> disc{};
};

// Runs f(i) for every i in [0, n) on up to `threads` threads of the pool,
// each index once (the original's parallel() gave each index a thread of
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

// The rod set's rules, laid out for speed. They are the stacking rules
// (stacking::moveAllowed): Panex columns, or plain rods with or without the
// size rule; the tests check this search and findStackPath agree.
class Rules {
 public:
  unsigned int rods = 0;
  unsigned int discs = 0;
  // Panex columns; otherwise plain rods, where sizeRule keeps a larger disc
  // off a smaller one.
  bool panex = true;
  bool sizeRule = false;
  std::array<unsigned char, kMaxDiscs> size{};
  // How deep each disc may sit in a Panex column: its size, at most the rod
  // height. On plain rods, its size.
  std::array<unsigned char, kMaxDiscs> depth{};
  std::array<unsigned char, kMaxRods> cap{};
  std::array<bool, kMaxRods> pocket{};
  std::array<int, kMaxRods> position{};
  bool distance = false;
  // The outside channel joins these two rods past every raised disc; -1
  // when there is none.
  int channelA = -1;
  int channelB = -1;
  // The rods mirror onto each other end to end: no pocket.
  bool mirrorable = false;

  // The column's top disc is raised into the bridge, blocking moves over it.
  [[nodiscard]] bool raised(const PState& s, unsigned int r) const {
    if (!panex || pocket[r]) {
      return false;
    }
    const unsigned int n = s.count[r];
    for (unsigned int i = 0; i < n; i++) {
      if (depth[s.disc[r][i]] < n - i) {
        return true;
      }
    }
    return false;
  }

  // Calls f with each stacking one transfer away; s is restored after.
  template <class F>
  void moves(PState& s, F f) const {
    std::array<bool, kMaxRods> up{};
    for (unsigned int r = 0; r < rods; r++) {
      up[r] = raised(s, r);
    }
    for (unsigned int from = 0; from < rods; from++) {
      if (s.count[from] == 0) {
        continue;
      }
      for (unsigned int to = 0; to < rods; to++) {
        if (to == from || s.count[to] >= cap[to]) {
          continue;
        }
        // Every disc there goes one deeper, which needs it not raised.
        if (!pocket[to] && up[to]) {
          continue;
        }
        if (sizeRule && s.count[to] != 0 &&
            size[s.disc[from][s.count[from] - 1U]] > size[s.disc[to][s.count[to] - 1U]]) {
          continue;
        }
        const int a = position[from];
        const int b = position[to];
        const int lo = std::min(a, b);
        const int hi = std::max(a, b);
        const bool channel =
            (static_cast<int>(from) == channelA && static_cast<int>(to) == channelB) ||
            (static_cast<int>(from) == channelB && static_cast<int>(to) == channelA);
        bool blocked = false;
        for (unsigned int r = 0; r < rods && !blocked && !channel; r++) {
          if (r != from && r != to && !pocket[r] && position[r] > lo && position[r] < hi && up[r]) {
            blocked = true;
          }
        }
        if (blocked || (distance && hi - lo != 1)) {
          continue;
        }
        const unsigned char d = s.disc[from][--s.count[from]];
        s.disc[to][s.count[to]++] = d;
        f(s);
        s.count[to]--;
        s.disc[from][s.count[from]++] = d;
      }
    }
  }

  [[nodiscard]] PKey encode(const PState& s) const {
    PKey k;
    auto push = [&k](unsigned int v) {
      k.hi = (k.hi << 5) | (k.lo >> 59);
      k.lo = (k.lo << 5) | v;
    };
    for (unsigned int r = 0; r < rods; r++) {
      if (r != 0) {
        push(kSeparator);
      }
      for (unsigned int i = 0; i < s.count[r]; i++) {
        push(s.disc[r][i]);
      }
    }
    return k;
  }

  void decode(PKey k, PState& s) const {
    for (unsigned int r = 0; r < rods; r++) {
      s.count[r] = 0;
    }
    unsigned int r = rods - 1;
    for (unsigned int i = 0; i < discs + rods - 1; i++) {
      const auto v = static_cast<unsigned int>(k.lo & 31);
      k.lo = (k.lo >> 5) | (k.hi << 59);
      k.hi >>= 5;
      if (v == kSeparator) {
        r--;
      } else {
        s.disc[r][s.count[r]++] = static_cast<unsigned char>(v);
      }
    }
    // Read top first: turn each rod back to bottom first.
    for (unsigned int q = 0; q < rods; q++) {
      std::reverse(s.disc[q].begin(), s.disc[q].begin() + s.count[q]);
    }
  }

  void mirrorState(const PState& s, PState& m) const {
    for (unsigned int r = 0; r < rods; r++) {
      const unsigned int q = rods - 1 - r;
      m.count[q] = s.count[r];
      std::copy(s.disc[r].begin(), s.disc[r].begin() + s.count[r], m.disc[q].begin());
    }
  }

  [[nodiscard]] PKey mirror(PKey k) const {
    PState s;
    PState m;
    decode(k, s);
    mirrorState(s, m);
    return encode(m);
  }

  [[nodiscard]] PState fromStacking(const stacking::Stacking& st) const {
    PState s;
    for (unsigned int r = 0; r < rods; r++) {
      s.count[r] = static_cast<unsigned char>(st[r].size());
      for (std::size_t i = 0; i < st[r].size(); i++) {
        s.disc[r][i] = static_cast<unsigned char>(st[r][i]);
      }
    }
    return s;
  }

  [[nodiscard]] stacking::Stacking toStacking(const PState& s) const {
    stacking::Stacking st(rods);
    for (unsigned int r = 0; r < rods; r++) {
      st[r].assign(s.disc[r].begin(), s.disc[r].begin() + s.count[r]);
    }
    return st;
  }
};

// Drop from sorted, unique a every key that is in sorted b. Both run
// upwards, so each key of a is looked for from where the last was found, in
// doubling steps and then by halving: a few steps close by, where a search
// of all the rest of b would jump about memory.
// (The original's steps formed pointers past the end of b, and from a null
// one for an empty b; these compare the steps with what is left instead.)
void subtract(Layer& a, const Layer& b) {
  if (b.empty()) {
    return;
  }
  std::size_t w = 0;
  const PKey* it = b.data();
  const PKey* const end = b.data() + b.size();
  for (std::size_t i = 0; i < a.size(); i++) {
    std::size_t step = 1;
    const PKey* lo = it;
    while (step < static_cast<std::size_t>(end - lo) && lo[step] < a[i]) {
      lo += step;
      step *= 2;
    }
    it = std::lower_bound(lo, lo + std::min(step + 1, static_cast<std::size_t>(end - lo)), a[i]);
    if (it == end || !(*it == a[i])) {
      a[w++] = a[i];
    }
  }
  a.resize(w);
}

// Sort keys of at most bits significant bits: by 11 bits at a time from the
// low end, each pass keeping the order of the one before. tmp is scratch.
// Several times quicker than a comparison sort on the hundreds of
// thousands of keys one batch of a level makes.
void radixSort(Layer& keys, Layer& tmp, unsigned int bits) {
  if (keys.size() < 2048) {
    std::sort(keys.begin(), keys.end());
    return;
  }
  constexpr unsigned int kDigit = 11;
  tmp.resize(keys.size());
  PKey* from = keys.data();
  PKey* to = tmp.data();
  const std::size_t n = keys.size();
  std::vector<std::size_t> count(std::size_t{1} << kDigit);
  for (unsigned int shift = 0; shift < bits; shift += kDigit) {
    std::fill(count.begin(), count.end(), 0);
    auto digit = [shift](const PKey& k) {
      const std::uint64_t v = shift >= 64 ? k.hi >> (shift - 64)
                              : shift == 0 ? k.lo
                                           : (k.lo >> shift) | (k.hi << (64 - shift));
      return static_cast<std::size_t>(v & ((1U << kDigit) - 1));
    };
    for (std::size_t i = 0; i < n; i++) {
      count[digit(from[i])]++;
    }
    // Every key alike in this digit: nothing to move.
    if (count[digit(from[0])] == n) {
      continue;
    }
    std::size_t at = 0;
    for (std::size_t& c : count) {
      const std::size_t here = c;
      c = at;
      at += here;
    }
    for (std::size_t i = 0; i < n; i++) {
      to[count[digit(from[i])]++] = from[i];
    }
    std::swap(from, to);
  }
  if (from != keys.data()) {
    keys.swap(tmp);
  }
}

// Merge two sorted, unique runs into one.
Layer mergeTwo(const PKey* a, const PKey* aEnd, const PKey* b, const PKey* bEnd) {
  Layer out(static_cast<std::size_t>(aEnd - a) + static_cast<std::size_t>(bEnd - b));
  auto end = std::merge(a, aEnd, b, bEnd, out.begin());
  out.resize(static_cast<std::size_t>(std::unique(out.begin(), end) - out.begin()));
  return out;
}

// Merge sorted, unique runs into one, pairwise, on this thread. Empties runs.
Layer mergeRuns(std::vector<Layer>& runs) {
  if (runs.empty()) {
    return {};
  }
  while (runs.size() > 1) {
    std::vector<Layer> next((runs.size() + 1) / 2);
    for (std::size_t i = 0; i < next.size(); i++) {
      if (2 * i + 1 >= runs.size()) {
        next[i].swap(runs[2 * i]);
        continue;
      }
      Layer& a = runs[2 * i];
      Layer& b = runs[2 * i + 1];
      next[i] = mergeTwo(a.data(), a.data() + a.size(), b.data(), b.data() + b.size());
      Layer().swap(a);
      Layer().swap(b);
    }
    runs.swap(next);
  }
  Layer out;
  out.swap(runs[0]);
  return out;
}

// Merge sorted, unique parts into one sorted, unique layer. Empties parts.
// With threads the keys are cut into as many stretches, by value, and each
// thread merges one stretch of every part: no step is left to one thread,
// as the last of a pairwise merge would be.
Layer mergeParts(std::vector<Layer>& parts, ThreadPool& pool, unsigned int threads) {
  std::size_t total = 0;
  for (const Layer& p : parts) {
    total += p.size();
  }
  if (threads <= 1 || parts.size() <= 1 || total < 200000) {
    return mergeRuns(parts);
  }

  // Cut points: evenly spaced keys of a sample of every part.
  Layer sample;
  for (const Layer& p : parts) {
    const std::size_t step = std::max<std::size_t>(1, p.size() / 64);
    for (std::size_t i = step / 2; i < p.size(); i += step) {
      sample.push_back(p[i]);
    }
  }
  std::sort(sample.begin(), sample.end());
  Layer cut(threads - 1);
  for (unsigned int t = 0; t + 1 < threads; t++) {
    cut[t] = sample[static_cast<std::size_t>(t + 1) * sample.size() / threads];
  }

  // The first round reads the parts and writes new runs; the parts go
  // before the later rounds, so no more is held than a pairwise merge holds.
  std::vector<std::vector<Layer>> runs(threads);
  forEachIndex(pool, threads, threads, [&](std::size_t t) {
    std::vector<std::pair<const PKey*, const PKey*>> mine;
    for (const Layer& p : parts) {
      const PKey* lo =
          t != 0 ? std::lower_bound(p.data(), p.data() + p.size(), cut[t - 1]) : p.data();
      const PKey* hi = t + 1 < threads ? std::lower_bound(p.data(), p.data() + p.size(), cut[t])
                                       : p.data() + p.size();
      if (lo < hi) {
        mine.emplace_back(lo, hi);
      }
    }
    for (std::size_t i = 0; i < mine.size(); i += 2) {
      if (i + 1 < mine.size()) {
        runs[t].push_back(
            mergeTwo(mine[i].first, mine[i].second, mine[i + 1].first, mine[i + 1].second));
      } else {
        runs[t].emplace_back(mine[i].first, mine[i].second);
      }
    }
  });
  std::vector<Layer>().swap(parts);

  std::vector<Layer> stretch(threads);
  forEachIndex(pool, threads, threads,
               [&](std::size_t t) { stretch[t] = mergeRuns(runs[t]); });

  total = 0;
  for (const Layer& p : stretch) {
    total += p.size();
  }
  Layer out;
  out.reserve(total);
  for (Layer& p : stretch) {
    out.insert(out.end(), p.begin(), p.end());
    Layer().swap(p);
  }
  return out;
}

// Sort and drop duplicates, on every thread.
void parallelSort(Layer& keys, ThreadPool& pool, unsigned int threads, unsigned int bits) {
  if (threads <= 1 || keys.size() < 100000) {
    Layer scratch;
    radixSort(keys, scratch, bits);
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    return;
  }
  std::vector<Layer> parts(threads);
  const std::size_t chunk = (keys.size() + threads - 1) / threads;
  forEachIndex(pool, threads, threads, [&](std::size_t t) {
    const std::size_t lo = std::min(keys.size(), t * chunk);
    const std::size_t hi = std::min(keys.size(), lo + chunk);
    parts[t].assign(keys.begin() + static_cast<std::ptrdiff_t>(lo),
                    keys.begin() + static_cast<std::ptrdiff_t>(hi));
    Layer scratch;
    radixSort(parts[t], scratch, bits);
    parts[t].erase(std::unique(parts[t].begin(), parts[t].end()), parts[t].end());
  });
  Layer().swap(keys);
  keys = mergeParts(parts, pool, threads);
}

// The first key in both sorted layers.
bool meet(const Layer& a, const Layer& b, PKey& at) {
  auto i = a.begin();
  auto j = b.begin();
  while (i != a.end() && j != b.end()) {
    if (*i < *j) {
      ++i;
    } else if (*j < *i) {
      ++j;
    } else {
      at = *i;
      return true;
    }
  }
  return false;
}

// One end of a search: its two newest levels, and every level while the
// search is small enough to keep them for tracing the path back.
struct Side {
  Layer prev;
  Layer cur;
  unsigned int depth = 0;
  std::vector<Layer> kept;
};

// Where two searches met: the stacking, how far it is from each end, and
// the whole path when it could be traced.
struct Meeting {
  PKey at;
  unsigned int fromStart = 0;
  unsigned int fromGoal = 0;
  std::vector<PKey> path;
};

// A search's frontier and progress, as saved for resume.
struct SavedState {
  std::uint64_t fingerprint = 0;
  std::uint32_t fwdDepth = 0;
  std::uint32_t bwdDepth = 0;
  std::uint32_t interval = 1;
  std::uint64_t found = 0;
  // Milliseconds spent solving so far, over every run of this search.
  std::uint64_t ms = 0;
  // Once the ends have met: where, and how far from each end. Then only
  // tracing the path back is left, and the frontier is not saved.
  std::uint32_t met = 0;
  std::uint32_t fromStart = 0;
  std::uint32_t fromGoal = 0;
  PKey meetAt;
  Layer fwdPrev;
  Layer fwdCur;
  Layer bwdPrev;
  Layer bwdCur;
  Layer mirrorPrev;
};

constexpr std::array<char, 8> kStateMagic = {'B', 'T', 'P', 'A', 'N', 'E', 'X', '4'};
// Version 3 was the same but kept whole seconds where 4 keeps ms.
constexpr std::array<char, 8> kStateMagicSeconds = {'B', 'T', 'P', 'A', 'N', 'E', 'X', '3'};

template <class T>
void put(std::ofstream& out, const T& v) {
  out.write(reinterpret_cast<const char*>(&v), sizeof(v));
}

template <class T>
void get(std::ifstream& in, T& v) {
  in.read(reinterpret_cast<char*>(&v), sizeof(v));
}

// One puzzle's saved search, in its own folder: every few levels of each
// end ("f12.lvl", "b0.lvl"), sorted keys, and the frontier ("state.bin").
// The levels kept are those a multiple of interval apart; when they outgrow
// the disk budget, interval doubles and every other one goes.
class Store {
 public:
  std::filesystem::path dir;
  unsigned int interval = 1;
  std::uint64_t budget = 0;
  std::uint64_t used = 0;

  bool open(const std::filesystem::path& d, std::uint64_t diskBudget) {
    dir = d;
    budget = diskBudget;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
      return false;
    }
    used = 0;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
      if (e.path().extension() == ".lvl") {
        used += e.file_size(ec);
      }
    }
    return true;
  }

  void wipe() {
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
      std::filesystem::remove(e.path(), ec);
    }
    used = 0;
    interval = 1;
  }

  void remove() const {
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
  }

  bool writeLevel(char side, unsigned int level, const Layer& keys) {
    if (!keepsLevel(level)) {
      return true;
    }
    if (!writeKeys(levelPath(side, level), keys.data(), keys.size())) {
      return false;
    }
    wrote(keys.size());
    return true;
  }

  // writeLevel in three steps, for writing a level later: keepsLevel says
  // whether it is one to save, writeKeys(levelFile(...)) writes it, and
  // wrote counts it against the budget.
  [[nodiscard]] bool keepsLevel(unsigned int level) const { return level % interval == 0; }

  [[nodiscard]] std::filesystem::path levelFile(char side, unsigned int level) const {
    return levelPath(side, level);
  }

  static bool writeKeys(const std::filesystem::path& path, const PKey* keys, std::size_t count) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (count != 0) {
      out.write(reinterpret_cast<const char*>(keys),
                static_cast<std::streamsize>(count * sizeof(PKey)));
    }
    return static_cast<bool>(out);
  }

  void wrote(std::size_t keys) {
    used += keys * sizeof(PKey);
    while (used > budget && interval < (1U << 30)) {
      thin();
    }
  }

  [[nodiscard]] bool hasLevel(char side, unsigned int level) const {
    std::error_code ec;
    return std::filesystem::exists(levelPath(side, level), ec);
  }

  // The first key of sorted probe that level holds, read as a stream.
  bool findIn(char side, unsigned int level, const Layer& probe, PKey& match) const {
    std::ifstream in(levelPath(side, level), std::ios::binary);
    if (!in) {
      return false;
    }
    Layer chunk(std::size_t{1} << 20);
    std::size_t p = 0;
    while (p < probe.size()) {
      in.read(reinterpret_cast<char*>(chunk.data()),
              static_cast<std::streamsize>(chunk.size() * sizeof(PKey)));
      const std::size_t n = static_cast<std::size_t>(in.gcount()) / sizeof(PKey);
      if (n == 0) {
        return false;
      }
      for (std::size_t i = 0; i < n && p < probe.size();) {
        if (chunk[i] < probe[p]) {
          i++;
        } else if (probe[p] < chunk[i]) {
          p++;
        } else {
          match = probe[p];
          return true;
        }
      }
    }
    return false;
  }

  [[nodiscard]] bool saveState(const SavedState& st) const {
    const std::filesystem::path tmp = dir / "state.tmp";
    {
      std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
      out.write(kStateMagic.data(), kStateMagic.size());
      put(out, st.fingerprint);
      put(out, st.fwdDepth);
      put(out, st.bwdDepth);
      put(out, st.interval);
      put(out, st.found);
      put(out, st.ms);
      put(out, st.met);
      put(out, st.fromStart);
      put(out, st.fromGoal);
      put(out, st.meetAt);
      for (const Layer* l : {&st.fwdPrev, &st.fwdCur, &st.bwdPrev, &st.bwdCur, &st.mirrorPrev}) {
        put(out, static_cast<std::uint64_t>(l->size()));
        if (!l->empty()) {
          out.write(reinterpret_cast<const char*>(l->data()),
                    static_cast<std::streamsize>(l->size() * sizeof(PKey)));
        }
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

  // With layers false, only the header: enough to describe the search.
  // A layer's count is checked against what the file holds before room is
  // made for it: a damaged or crafted file (one imported from a .btsolve,
  // say) could otherwise claim terabytes, which the original allocated and
  // filled (an audit finding). Such a state is no saved search.
  bool loadState(SavedState& st, std::uint64_t fingerprint, bool layers) const {
    std::error_code sizeError;
    const std::uint64_t fileBytes = std::filesystem::file_size(dir / "state.bin", sizeError);
    std::ifstream in(dir / "state.bin", std::ios::binary);
    std::array<char, kStateMagic.size()> magic{};
    in.read(magic.data(), magic.size());
    const bool inSeconds = magic == kStateMagicSeconds;
    if (!in || !(inSeconds || magic == kStateMagic)) {
      return false;
    }
    get(in, st.fingerprint);
    get(in, st.fwdDepth);
    get(in, st.bwdDepth);
    get(in, st.interval);
    get(in, st.found);
    get(in, st.ms);
    if (inSeconds) {
      st.ms *= 1000;
    }
    get(in, st.met);
    get(in, st.fromStart);
    get(in, st.fromGoal);
    get(in, st.meetAt);
    if (!in || st.fingerprint != fingerprint || st.interval == 0) {
      return false;
    }
    if (!layers) {
      return true;
    }
    for (Layer* l : {&st.fwdPrev, &st.fwdCur, &st.bwdPrev, &st.bwdCur, &st.mirrorPrev}) {
      std::uint64_t n = 0;
      get(in, n);
      if (!in || sizeError) {
        return false;
      }
      const auto at = static_cast<std::uint64_t>(in.tellg());
      if (at > fileBytes || n > (fileBytes - at) / sizeof(PKey)) {
        return false;
      }
      l->resize(static_cast<std::size_t>(n));
      if (n != 0) {
        in.read(reinterpret_cast<char*>(l->data()),
                static_cast<std::streamsize>(n * sizeof(PKey)));
      }
      if (!in) {
        return false;
      }
    }
    return true;
  }

 private:
  [[nodiscard]] std::filesystem::path levelPath(char side, unsigned int level) const {
    return dir / (std::string(1, side) + std::to_string(level) + ".lvl");
  }

  // Keep every other saved level. Level 0 is a multiple of any interval.
  void thin() {
    interval *= 2;
    used = 0;
    std::error_code ec;
    std::vector<std::filesystem::path> drop;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
      if (e.path().extension() != ".lvl") {
        continue;
      }
      const std::string name = e.path().stem().string();
      const unsigned long level = std::strtoul(name.c_str() + 1, nullptr, 10);
      if (level % interval != 0) {
        drop.push_back(e.path());
      } else {
        used += e.file_size(ec);
      }
    }
    for (const auto& d : drop) {
      std::filesystem::remove(d, ec);
    }
  }
};

// The newest level of a search, to be written to the store: started when
// the level is made, written by the first job of the next level's
// expansion (or at once by finish), counted against the budget by finish.
// The keys stay where they are until then (the level is only read from
// then on, and its vector changes only after finish).
struct LevelWrite {
  const PKey* keys = nullptr;
  std::size_t count = 0;
  std::filesystem::path file;
  bool pending = false;
  bool done = false;
  bool ok = true;

  void start(const PKey* k, std::size_t n, std::filesystem::path f) {
    keys = k;
    count = n;
    file = std::move(f);
    pending = true;
    done = false;
    ok = true;
  }
  [[nodiscard]] bool due() const { return pending && !done; }
  void perform() {
    ok = Store::writeKeys(file, keys, count);
    done = true;
  }
};

class Searcher {
 public:
  Searcher(const Rules& rules, PanexSearch& search, ThreadPool& pool, unsigned int threads,
           std::uint64_t budget)
      : rules_(rules), search_(search), pool_(pool), threads_(threads), budget_(budget) {}

  // Save the top search here, and carry on from resumeFrom when it is set.
  Store* store = nullptr;
  const SavedState* resumeFrom = nullptr;
  // The two ends, for levels the store does not hold.
  PKey startKey;
  PKey goalKey;
  std::uint64_t fingerprint = 0;
  // Milliseconds the saved search had used, and when this run began.
  std::uint64_t priorMs = 0;
  std::chrono::steady_clock::time_point began = std::chrono::steady_clock::now();

  [[nodiscard]] std::uint64_t msSoFar() const {
    return priorMs + static_cast<std::uint64_t>(
                         std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - began)
                             .count());
  }

  // For the classic swap: the board's mirror image with each disc swapped
  // for its partner of the same size maps the start to itself, and the goal
  // too. Then a level holds one stacking of each such pair.
  void findSigma(PKey start, PKey goal) {
    PState s;
    PState m;
    rules_.decode(start, s);
    rules_.decode(rules_.mirror(start), m);
    for (unsigned int d = 0; d < rules_.discs; d++) {
      perm_[d] = 0xff;
    }
    for (unsigned int r = 0; r < rules_.rods; r++) {
      for (unsigned int i = 0; i < s.count[r]; i++) {
        const unsigned char from = m.disc[r][i];
        const unsigned char to = s.disc[r][i];
        if (rules_.depth[from] != rules_.depth[to] || (perm_[from] != 0xff && perm_[from] != to)) {
          return;
        }
        perm_[from] = to;
      }
    }
    std::vector<bool> hit(rules_.discs, false);
    for (unsigned int d = 0; d < rules_.discs; d++) {
      if (perm_[d] == 0xff || hit[perm_[d]]) {
        return;
      }
      hit[perm_[d]] = true;
    }
    sigma_ = true;
    if (!(twin(start) == start) || !(twin(goal) == goal)) {
      sigma_ = false;
    }
  }

  // A shortest path from a to b, both ends included.
  bool shortestPath(PKey a, PKey b, bool top, std::vector<PKey>& out) {
    if (a == b) {
      out.assign(1, a);
      return true;
    }
    Meeting m;
    if (!meetSearch(a, b, top, m)) {
      return false;
    }
    if (!m.path.empty()) {
      out.swap(m.path);
      return true;
    }
    // Too big to keep the levels and nowhere to save them: solve the two
    // halves the same way.
    std::vector<PKey> left;
    std::vector<PKey> right;
    if (!shortestPath(a, m.at, false, left) || !shortestPath(m.at, b, false, right)) {
      return false;
    }
    out.swap(left);
    out.insert(out.end(), right.begin() + 1, right.end());
    return true;
  }

 private:
  const Rules& rules_;
  PanexSearch& search_;
  ThreadPool& pool_;
  const unsigned int threads_;
  const std::uint64_t budget_;
  bool sigma_ = false;
  std::array<unsigned char, kMaxDiscs> perm_{};

  [[nodiscard]] PKey twin(PKey k) const {
    PState s;
    rules_.decode(k, s);
    return twinOf(s);
  }

  // The twin's key, written straight from s: the rods end to end, each
  // disc swapped for its partner.
  [[nodiscard]] PKey twinOf(const PState& s) const {
    PKey k;
    auto push = [&k](unsigned int v) {
      k.hi = (k.hi << 5) | (k.lo >> 59);
      k.lo = (k.lo << 5) | v;
    };
    for (unsigned int q = 0; q < rules_.rods; q++) {
      const unsigned int r = rules_.rods - 1 - q;
      if (q != 0) {
        push(kSeparator);
      }
      for (unsigned int i = 0; i < s.count[r]; i++) {
        push(perm_[s.disc[r][i]]);
      }
    }
    return k;
  }

  [[nodiscard]] bool stopped() const { return search_.stop.stopRequested(); }

  // Starting threads costs more than a small level: about one per 20,000
  // stackings.
  [[nodiscard]] unsigned int threadsFor(std::size_t keys) const {
    return static_cast<unsigned int>(std::min<std::size_t>(threads_, keys / 20000 + 1));
  }

  // Everything one move from cur that is in neither cur nor prev, through
  // key(state); false when stopped or when room keys would not hold it.
  // A level due to be written goes to its file on the first job.
  template <class Key>
  bool nextLevel(const Layer& cur, const Layer& prev, Key key, std::size_t room, Layer& next,
                 LevelWrite* write = nullptr) {
    const unsigned int n = threadsFor(cur.size());
    const bool writing = write != nullptr && write->due();
    const unsigned int jobs = n + (writing ? 1 : 0);
    std::vector<std::vector<Layer>> runs(jobs);
    std::atomic<bool> abandon{false};
    std::atomic<std::size_t> runKeys{0};
    std::atomic<std::size_t> nextChunk{0};
    // Several batches a thread, so none waits on another; at most 65,536
    // stackings, so the raw moves stay small.
    const std::size_t batch =
        std::max<std::size_t>(256, std::min<std::size_t>(1 << 16, cur.size() / (8 * n) + 1));
    const unsigned int keyBits = 5 * (rules_.discs + rules_.rods - 1);
    pool_.run(jobs, [&](unsigned int t) {
      if (t == 0 && writing) {
        write->perform();
      }
      Layer out;
      Layer scratch;
      PState s;
      for (;;) {
        const std::size_t lo = nextChunk.fetch_add(batch);
        if (lo >= cur.size() || stopped() || abandon.load(std::memory_order_relaxed)) {
          break;
        }
        const std::size_t hi = std::min(cur.size(), lo + batch);
        out.clear();
        for (std::size_t i = lo; i < hi; i++) {
          rules_.decode(cur[i], s);
          rules_.moves(s, [&](const PState& st) { out.push_back(key(st)); });
        }
        radixSort(out, scratch, keyBits);
        out.erase(std::unique(out.begin(), out.end()), out.end());
        subtract(out, cur);
        subtract(out, prev);
        if (runKeys.fetch_add(out.size()) + out.size() > room) {
          abandon.store(true, std::memory_order_relaxed);
        }
        runs[t].emplace_back(out.begin(), out.end());
      }
    });
    if (stopped() || abandon.load()) {
      return false;
    }
    std::vector<Layer> parts;
    for (std::vector<Layer>& r : runs) {
      for (Layer& p : r) {
        parts.emplace_back(std::move(p));
      }
    }
    next = mergeParts(parts, pool_, n);
    return true;
  }

  // A shortest path from the saved end ('f' the start, 'b' the goal) to x,
  // depth moves from it, end first. Going back one saved level at a time:
  // every stacking within reach of the current one, as many moves out as
  // the saved level is nearer the end, until one of them is in that level.
  template <class Canon>
  bool traceStored(char side, PKey x, unsigned int depth, Canon canon, std::vector<PKey>& out) {
    std::vector<PKey> back{x};
    PKey cur = x;
    const auto room = static_cast<std::size_t>(budget_ / sizeof(PKey) / 2);
    auto identity = [&](const PState& st) { return rules_.encode(st); };
    for (unsigned int d = depth; d > 0;) {
      if (stopped()) {
        search_.outcome = Outcome::Stopped;
        return false;
      }
      const unsigned int c = ((d - 1) / store->interval) * store->interval;
      // The saved level is missing -- an export left the levels out, say:
      // solve the rest of the way to the end by halves instead.
      if (!store->hasLevel(side, c)) {
        std::vector<PKey> fromEnd;
        if (!shortestPath(side == 'f' ? startKey : goalKey, cur, false, fromEnd)) {
          return false;
        }
        back.insert(back.end(), fromEnd.rbegin() + 1, fromEnd.rend());
        break;
      }
      const unsigned int r = d - c;
      std::vector<Layer> ball{Layer{cur}};
      std::size_t held = 1;
      for (unsigned int i = 1; i <= r; i++) {
        Layer next;
        const Layer empty;
        if (!nextLevel(ball[i - 1], i >= 2 ? ball[i - 2] : empty, identity,
                       room > held ? room - held : 0, next)) {
          if (search_.outcome != Outcome::Stopped) {
            search_.outcome = stopped() ? Outcome::Stopped : Outcome::Memory;
          }
          return false;
        }
        held += next.size();
        ball.push_back(std::move(next));
      }
      // The ball's outer level, as the saved level holds it.
      std::vector<std::pair<PKey, PKey>> outer;
      outer.reserve(ball[r].size());
      for (const PKey& k : ball[r]) {
        outer.emplace_back(canon(k), k);
      }
      std::sort(outer.begin(), outer.end(),
                [](const std::pair<PKey, PKey>& a, const std::pair<PKey, PKey>& b) {
                  return a.first < b.first;
                });
      Layer probe;
      probe.reserve(outer.size());
      for (const auto& o : outer) {
        probe.push_back(o.first);
      }
      PKey hit;
      if (!store->findIn(side, c, probe, hit)) {
        return traceFailed();
      }
      const auto at = std::lower_bound(
          outer.begin(), outer.end(), std::make_pair(hit, hit),
          [](const std::pair<PKey, PKey>& a, const std::pair<PKey, PKey>& b) {
            return a.first < b.first;
          });
      // From that stacking back through the ball to cur.
      std::vector<PKey> chain{at->second};
      for (unsigned int i = r - 1; i >= 1; i--) {
        PState s;
        rules_.decode(chain.back(), s);
        bool ok = false;
        rules_.moves(s, [&](const PState& n) {
          if (ok) {
            return;
          }
          const PKey k = rules_.encode(n);
          if (std::binary_search(ball[i].begin(), ball[i].end(), k)) {
            chain.push_back(k);
            ok = true;
          }
        });
        if (!ok) {
          return traceFailed();
        }
      }
      back.insert(back.end(), chain.rbegin(), chain.rend());
      cur = at->second;
      d = c;
      if (search_.traced != nullptr) {
        search_.traced->fetch_add(r, std::memory_order_relaxed);
      }
    }
    out.assign(back.rbegin(), back.rend());
    return true;
  }

  // Searches from a and from b until they meet. With top set and b the
  // mirror image of a, only the search from a runs.
  bool meetSearch(PKey a, PKey b, bool top, Meeting& m) {
    const bool mirrored = top && rules_.mirrorable && rules_.mirror(a) == b;
    const bool useTwin = mirrored && sigma_;
    auto canon = [&](PKey k) {
      if (!useTwin) {
        return k;
      }
      const PKey t = twin(k);
      return t < k ? t : k;
    };
    // The same, for a stacking at hand: no decoding.
    auto canonOf = [&](const PState& st) {
      const PKey k = rules_.encode(st);
      if (!useTwin) {
        return k;
      }
      const PKey t = twinOf(st);
      return t < k ? t : k;
    };
    Store* saveTo = top ? store : nullptr;

    Side fwd;
    Side bwd;
    // The search from b, when it is the mirror image: its level before the
    // forward search's current one, as canonical keys.
    Layer mirrorPrev;
    // Keep every level for tracing back while they take a quarter of the
    // budget; then the saved levels on disk take over.
    bool keep = true;
    bool onDisk = false;
    std::size_t keptKeys = 0;
    const std::size_t keepCap = search_.keepLimit != 0
                                    ? static_cast<std::size_t>(search_.keepLimit)
                                    : static_cast<std::size_t>(budget_ / sizeof(PKey) / 4);

    bool found = false;
    if (top && resumeFrom != nullptr && resumeFrom->met != 0) {
      m.at = resumeFrom->meetAt;
      m.fromStart = resumeFrom->fromStart;
      m.fromGoal = resumeFrom->fromGoal;
      search_.found = resumeFrom->found;
      keep = false;
      onDisk = true;
      found = true;
      if (search_.depth != nullptr) {
        search_.depth->store(std::uint64_t{m.fromStart} + m.fromGoal, std::memory_order_relaxed);
      }
    } else if (top && resumeFrom != nullptr) {
      fwd.prev = resumeFrom->fwdPrev;
      fwd.cur = resumeFrom->fwdCur;
      fwd.depth = resumeFrom->fwdDepth;
      bwd.prev = resumeFrom->bwdPrev;
      bwd.cur = resumeFrom->bwdCur;
      bwd.depth = resumeFrom->bwdDepth;
      mirrorPrev = resumeFrom->mirrorPrev;
      search_.found = resumeFrom->found;
      keep = false;
      onDisk = true;
    } else {
      fwd.cur.push_back(canon(a));
      if (!mirrored) {
        bwd.cur.push_back(b);
      }
      fwd.kept.push_back(fwd.cur);
      if (!mirrored) {
        bwd.kept.push_back(bwd.cur);
      }
      if (mirrored) {
        mirrorPrev.push_back(canon(rules_.mirror(a)));
      }
    }

    auto inMemory = [&](std::size_t extra) {
      const std::size_t keys = fwd.prev.size() + fwd.cur.size() + bwd.prev.size() +
                               bwd.cur.size() + mirrorPrev.size() + keptKeys + extra;
      const std::uint64_t bytes = std::uint64_t{keys} * sizeof(PKey);
      search_.peakMemory = std::max(search_.peakMemory, bytes);
      return bytes;
    };

    // Write the kept levels to the store and go on from disk.
    auto toDisk = [&]() {
      if (onDisk) {
        return true;
      }
      for (std::size_t i = 0; i < fwd.kept.size(); i++) {
        if (!saveTo->writeLevel('f', static_cast<unsigned int>(i), fwd.kept[i])) {
          return false;
        }
      }
      for (std::size_t i = 0; i < bwd.kept.size(); i++) {
        if (!saveTo->writeLevel('b', static_cast<unsigned int>(i), bwd.kept[i])) {
          return false;
        }
      }
      std::vector<Layer>().swap(fwd.kept);
      std::vector<Layer>().swap(bwd.kept);
      keptKeys = 0;
      keep = false;
      onDisk = true;
      return true;
    };
    auto diskFailed = [&]() {
      search_.outcome = Outcome::Error;
      search_.error =
          "Could not save the search in " + saveTo->dir.string() + ". The disk may be full.";
      return false;
    };
    // The newest level, written while the search goes on to the next (see
    // LevelWrite); waited for before anything depends on the file.
    LevelWrite writer;
    auto finishWrite = [&]() {
      if (!writer.pending) {
        return true;
      }
      if (!writer.done) {
        writer.perform();
      }
      writer.pending = false;
      if (!writer.ok) {
        return false;
      }
      saveTo->wrote(writer.count);
      return true;
    };
    // Save where the search stands, so it can be carried on.
    auto saveState = [&]() {
      if (saveTo == nullptr || !finishWrite() || !toDisk()) {
        return false;
      }
      SavedState st;
      st.fingerprint = fingerprint;
      st.fwdDepth = fwd.depth;
      st.bwdDepth = bwd.depth;
      st.interval = saveTo->interval;
      st.found = search_.found;
      st.ms = msSoFar();
      st.fwdPrev = fwd.prev;
      st.fwdCur = fwd.cur;
      st.bwdPrev = bwd.prev;
      st.bwdCur = bwd.cur;
      st.mirrorPrev = mirrorPrev;
      return saveTo->saveState(st);
    };
    // How long a path both ends together have ruled out.
    auto deep = [&]() {
      return mirrored ? 2 * std::uint64_t{fwd.depth} : std::uint64_t{fwd.depth} + bwd.depth;
    };
    // The search stopped short: save it if it can be carried on.
    auto haltAndSave = [&](Outcome why) {
      search_.outcome = why;
      search_.depthReached = deep();
      if (top && saveTo != nullptr) {
        search_.saved = saveState();
      }
      return false;
    };
    auto lastSave = std::chrono::steady_clock::now();

    while (!found) {
      if (stopped() || (top && search_.stopAtDepth != 0 && deep() >= search_.stopAtDepth)) {
        return haltAndSave(Outcome::Stopped);
      }
      Side& side = (mirrored || fwd.cur.size() <= bwd.cur.size()) ? fwd : bwd;
      Layer next;
      const std::uint64_t base = inMemory(0);
      const std::size_t room =
          base >= budget_ ? 0 : static_cast<std::size_t>((budget_ - base) / sizeof(PKey) / 2);
      if (!nextLevel(side.cur, side.prev, canonOf, room, next, &writer)) {
        return haltAndSave(stopped() ? Outcome::Stopped : Outcome::Memory);
      }
      if (!finishWrite()) {
        return diskFailed();
      }
      if (next.empty()) {
        // Everything this end can reach is searched without meeting the other.
        search_.outcome = Outcome::NoPath;
        return false;
      }
      if (keep) {
        keptKeys += next.size();
        if (keptKeys > keepCap) {
          if (saveTo != nullptr) {
            if (!toDisk()) {
              return diskFailed();
            }
          } else {
            keep = false;
            keptKeys = 0;
            std::vector<Layer>().swap(fwd.kept);
            std::vector<Layer>().swap(bwd.kept);
          }
        } else {
          side.kept.push_back(next);
        }
      }
      Layer().swap(side.prev);
      side.prev.swap(side.cur);
      side.cur.swap(next);
      side.depth++;
      if (onDisk && saveTo->keepsLevel(side.depth)) {
        // The level's memory stays where it is until it is two levels old,
        // well after finishWrite, whichever vector then holds it.
        writer.start(side.cur.data(), side.cur.size(),
                     saveTo->levelFile(&side == &fwd ? 'f' : 'b', side.depth));
      }
      search_.found += side.cur.size() * (useTwin ? 2 : 1);
      if (search_.progress != nullptr) {
        search_.progress->store(search_.found, std::memory_order_relaxed);
      }
      if (search_.onLevel) {
        std::array<char, 160> line{};
        std::snprintf(line.data(), line.size(), "panex: %s level %u: %zu stackings, %llu in all",
                      top ? (&side == &fwd ? "start" : "goal")
                          : (&side == &fwd ? "part start" : "part goal"),
                      side.depth, side.cur.size(),
                      static_cast<unsigned long long>(search_.found));
        search_.onLevel(line.data());
      }

      if (mirrored) {
        // The mirrored search stands one level behind, then level with it.
        if (meet(fwd.cur, mirrorPrev, m.at)) {
          found = true;
          m.fromStart = fwd.depth;
          m.fromGoal = fwd.depth - 1;
          break;
        }
        Layer mirrorCur(fwd.cur.size());
        const unsigned int mt = threadsFor(fwd.cur.size());
        forEachIndex(pool_, mt, mt, [&](std::size_t t) {
          const std::size_t chunk = (fwd.cur.size() + mt - 1) / mt;
          const std::size_t lo = std::min(fwd.cur.size(), t * chunk);
          const std::size_t hi = std::min(fwd.cur.size(), lo + chunk);
          PState st;
          PState mi;
          for (std::size_t i = lo; i < hi; i++) {
            rules_.decode(fwd.cur[i], st);
            rules_.mirrorState(st, mi);
            mirrorCur[i] = canonOf(mi);
          }
        });
        parallelSort(mirrorCur, pool_, mt, 5 * (rules_.discs + rules_.rods - 1));
        if (top && search_.depth != nullptr) {
          search_.depth->store(2 * std::uint64_t{fwd.depth}, std::memory_order_relaxed);
        }
        if (meet(fwd.cur, mirrorCur, m.at)) {
          found = true;
          m.fromStart = fwd.depth;
          m.fromGoal = fwd.depth;
          break;
        }
        mirrorPrev.swap(mirrorCur);
      } else {
        if (top && search_.depth != nullptr) {
          search_.depth->store(std::uint64_t{fwd.depth} + bwd.depth, std::memory_order_relaxed);
        }
        const Side& other = (&side == &fwd) ? bwd : fwd;
        if (meet(side.cur, other.cur, m.at)) {
          found = true;
          m.fromStart = fwd.depth;
          m.fromGoal = bwd.depth;
        }
      }
      inMemory(0);

      // An autosave also moves levels still kept in memory to disk.
      if (saveTo != nullptr && search_.autosaveMinutes != 0 &&
          std::chrono::steady_clock::now() - lastSave >=
              std::chrono::minutes(search_.autosaveMinutes)) {
        if (!saveState()) {
          return diskFailed();
        }
        lastSave = std::chrono::steady_clock::now();
      }
    }
    if (!finishWrite()) {
      return diskFailed();
    }
    search_.depthReached = std::uint64_t{m.fromStart} + m.fromGoal;

    if (onDisk) {
      // Free the frontier, then trace back between the saved levels. The
      // mirrored half is the forward search's, read in the mirror.
      Layer().swap(fwd.prev);
      Layer().swap(fwd.cur);
      Layer().swap(bwd.prev);
      Layer().swap(bwd.cur);
      Layer().swap(mirrorPrev);
      // Note where the ends met, so a stop while tracing loses no search.
      SavedState st;
      st.fingerprint = fingerprint;
      st.fwdDepth = m.fromStart;
      st.bwdDepth = m.fromGoal;
      st.interval = saveTo->interval;
      st.found = search_.found;
      st.ms = msSoFar();
      st.met = 1;
      st.fromStart = m.fromStart;
      st.fromGoal = m.fromGoal;
      st.meetAt = m.at;
      if (!saveTo->saveState(st)) {
        return diskFailed();
      }
      auto stopTrace = [&]() {
        if (search_.outcome == Outcome::Stopped || search_.outcome == Outcome::Memory) {
          search_.saved = true;
        }
        return false;
      };
      auto same = [](PKey k) { return k; };
      std::vector<PKey> toStart;
      if (!traceStored('f', m.at, m.fromStart, canon, toStart)) {
        return stopTrace();
      }
      std::vector<PKey> toGoal;
      if (mirrored) {
        std::vector<PKey> fromMirror;
        if (!traceStored('f', rules_.mirror(m.at), m.fromGoal, canon, fromMirror)) {
          return stopTrace();
        }
        for (const PKey& k : fromMirror) {
          toGoal.push_back(rules_.mirror(k));
        }
      } else if (!traceStored('b', m.at, m.fromGoal, same, toGoal)) {
        return stopTrace();
      }
      // toStart runs start..meet, toGoal goal..meet.
      m.path.swap(toStart);
      m.path.insert(m.path.end(), toGoal.rbegin() + 1, toGoal.rend());
      return true;
    }

    if (!keep) {
      return true;
    }

    // Trace back through the kept levels: each step a neighbour one level
    // nearer the end.
    auto stepBack = [&](const Layer& level, bool viaMirror, PKey from, PKey& to) {
      PState s;
      rules_.decode(from, s);
      bool ok = false;
      rules_.moves(s, [&](const PState& n) {
        if (ok) {
          return;
        }
        const PKey k = rules_.encode(n);
        if (std::binary_search(level.begin(), level.end(), canon(viaMirror ? rules_.mirror(k) : k))) {
          to = k;
          ok = true;
        }
      });
      return ok;
    };
    std::vector<PKey> keys{m.at};
    for (unsigned int level = m.fromStart; level-- > 0;) {
      PKey prev;
      if (!stepBack(fwd.kept[level], false, keys.back(), prev)) {
        return traceFailed();
      }
      keys.push_back(prev);
    }
    std::reverse(keys.begin(), keys.end());
    for (unsigned int level = m.fromGoal; level-- > 0;) {
      PKey next;
      if (!stepBack(mirrored ? fwd.kept[level] : bwd.kept[level], mirrored, keys.back(), next)) {
        return traceFailed();
      }
      keys.push_back(next);
    }
    // With twins, the ends found may be the twins of a and b; they are a
    // and b themselves because both are their own twins.
    m.path.swap(keys);
    return true;
  }

  bool traceFailed() {
    search_.outcome = Outcome::Error;
    search_.error = "The Panex Solver could not trace its path back.";
    return false;
  }
};

// The rules, start and goal of a problem the Panex Solver can take.
bool setup(const Puzzle& puzzle, const Problem& prob, Rules& rules, PKey& startKey,
           PKey& goalKey) {
  if (!unsupported(puzzle, prob, true).empty()) {
    return false;
  }
  const stacking::RodSet& board = puzzle.getRodSet(prob.getRodSetId());
  stacking::Stacking startSt;
  stacking::Stacking goalSt;
  std::vector<unsigned int> sizes;
  if (!stacking::searchInput(puzzle, prob, startSt, goalSt, sizes)) {
    return false;
  }
  rules.rods = stacking::totalRods(board);
  rules.discs = static_cast<unsigned int>(sizes.size());
  const unsigned int height = stacking::rodHeight(board, rules.discs);
  rules.panex = board.panexColumns;
  rules.sizeRule = !board.panexColumns && board.sizeMatters;
  for (unsigned int d = 0; d < rules.discs; d++) {
    rules.size[d] = static_cast<unsigned char>(std::min(sizes[d], 255U));
    rules.depth[d] = rules.panex ? static_cast<unsigned char>(std::min(sizes[d], height))
                                 : rules.size[d];
  }
  rules.mirrorable = true;
  for (unsigned int r = 0; r < rules.rods; r++) {
    rules.cap[r] = static_cast<unsigned char>(
        std::min(stacking::rodCapacity(board, r, rules.discs), kMaxDiscs));
    rules.pocket[r] = stacking::isPocket(board, r);
    rules.position[r] = stacking::rodPosition(board, r);
    if (rules.pocket[r]) {
      rules.mirrorable = false;
    }
  }
  rules.distance = board.distanceMatters && !board.canMoveOver;
  if (stacking::hasOutsideChannel(board)) {
    rules.channelA = 0;
    rules.channelB = static_cast<int>(board.rodCount) - 1;
  }
  startKey = rules.encode(rules.fromStacking(startSt));
  goalKey = rules.encode(rules.fromStacking(goalSt));
  return true;
}

// The classic tower: three plain rods under the size rule, every disc a
// different size and room for them all on any rod, the whole tower on one
// rod at the start and on another at the goal. The well-known recursion --
// all but the largest disc to the spare rod, the largest across, the rest
// on top of it -- is then a shortest path, 2^n - 1 transfers, and needs no
// search. Fills path and returns true when the puzzle is that one and the
// path is not too long to hold.
bool classicTower(const Rules& rules, PKey startKey, PKey goalKey,
                  std::vector<stacking::Stacking>& path) {
  // 2^22 stackings in the path is about what the path's own memory allows.
  constexpr unsigned int kMostDiscs = 22;
  const unsigned int n = rules.discs;
  if (rules.panex || !rules.sizeRule || rules.distance || rules.rods != 3 || n == 0 ||
      n > kMostDiscs) {
    return false;
  }
  PState s;
  PState g;
  rules.decode(startKey, s);
  rules.decode(goalKey, g);
  int from = -1;
  int to = -1;
  for (unsigned int r = 0; r < 3; r++) {
    if (rules.cap[r] < n) {
      return false;
    }
    if (s.count[r] == n) {
      from = static_cast<int>(r);
    }
    if (g.count[r] == n) {
      to = static_cast<int>(r);
    }
  }
  if (from < 0 || to < 0 || from == to) {
    return false;
  }
  const auto f = static_cast<unsigned int>(from);
  const auto t = static_cast<unsigned int>(to);
  for (unsigned int i = 0; i + 1 < n; i++) {
    if (rules.size[s.disc[f][i]] <= rules.size[s.disc[f][i + 1]]) {
      return false;
    }
  }
  for (unsigned int i = 0; i < n; i++) {
    if (g.disc[t][i] != s.disc[f][i]) {
      return false;
    }
  }

  path.reserve(std::size_t{1} << n);
  path.push_back(rules.toStacking(s));
  auto move = [&](unsigned int a, unsigned int b) {
    s.disc[b][s.count[b]++] = s.disc[a][--s.count[a]];
    path.push_back(rules.toStacking(s));
  };
  // tower(k, a, b): the top k discs of rod a to rod b. Kept as a stack of
  // things still to do, so a tall tower does not nest calls.
  struct Todo {
    unsigned int k;
    unsigned int a;
    unsigned int b;
    bool single;
  };
  std::vector<Todo> todo{{n, f, t, false}};
  while (!todo.empty()) {
    const Todo d = todo.back();
    todo.pop_back();
    if (d.single || d.k == 1) {
      move(d.a, d.b);
      continue;
    }
    const unsigned int spare = 3 - d.a - d.b;
    todo.push_back({d.k - 1, spare, d.b, false});
    todo.push_back({1, d.a, d.b, true});
    todo.push_back({d.k - 1, d.a, spare, false});
  }
  return true;
}

// Names a puzzle's saved search: the rules, the start and the goal (FNV-1a
// over the bytes of each value, as the original mixed them).
std::uint64_t fingerprintOf(const Rules& rules, PKey start, PKey goal) {
  std::uint64_t h = 1469598103934665603ULL;
  auto mix = [&h](std::uint64_t v) {
    for (int i = 0; i < 8; i++) {
      h ^= (v >> (8 * i)) & 0xff;
      h *= 1099511628211ULL;
    }
  };
  mix(2);  // the saved format
  mix(rules.panex ? 1 : 0);
  mix(rules.sizeRule ? 1 : 0);
  mix(rules.rods);
  mix(rules.discs);
  for (unsigned int d = 0; d < rules.discs; d++) {
    mix(rules.depth[d]);
  }
  for (unsigned int r = 0; r < rules.rods; r++) {
    mix(rules.cap[r]);
    mix(rules.pocket[r] ? 1 : 0);
    mix(static_cast<std::uint64_t>(static_cast<std::int64_t>(rules.position[r])));
  }
  mix(rules.distance ? 1 : 0);
  // Only when on, so searches saved before the channel keep their folder.
  if (rules.channelA >= 0) {
    mix(0x6368616e6e656cULL);
  }
  mix(start.hi);
  mix(start.lo);
  mix(goal.hi);
  mix(goal.lo);
  return h;
}

// The folder of a saved search in workDir; empty when there is no workDir
// (the original fell back on SHAPELOGIC_PANEX_DIR and the user's cache
// folder, which the programs now pass in).
std::filesystem::path folderFor(std::uint64_t fingerprint, const std::string& workDir) {
  if (workDir.empty()) {
    return {};
  }
  std::array<char, 17> name{};
  std::snprintf(name.data(), name.size(), "%016llx", static_cast<unsigned long long>(fingerprint));
  return std::filesystem::path(workDir) / name.data();
}

std::string shortCount(double n) {
  std::array<char, 32> buf{};
  if (n >= 1e9) {
    std::snprintf(buf.data(), buf.size(), "%.1fB", n / 1e9);
  } else if (n >= 1e6) {
    std::snprintf(buf.data(), buf.size(), "%.1fM", n / 1e6);
  } else {
    std::snprintf(buf.data(), buf.size(), "%.0f", n);
  }
  return buf.data();
}

}  // namespace

std::string unsupported(const Puzzle& puzzle, const Problem& problem, bool anyRules) {
  if (!stacking::isStacking(puzzle)) {
    return "The Panex Solver is for stacking puzzles.";
  }
  std::string err = stacking::setupError(puzzle, problem);
  if (!err.empty()) {
    return err;
  }
  const stacking::RodSet& board = puzzle.getRodSet(problem.getRodSetId());
  if (!anyRules && !board.panexColumns) {
    return "The Panex Solver needs Panex Style Columns: turn it on for this rod set in the "
           "Entities tab.";
  }
  const unsigned int rods = stacking::totalRods(board);
  if (rods > kMaxRods) {
    return "The Panex Solver handles at most " + std::to_string(kMaxRods) + " rods.";
  }
  stacking::Stacking start;
  stacking::Stacking goal;
  std::vector<unsigned int> sizes;
  if (!stacking::searchInput(puzzle, problem, start, goal, sizes)) {
    return "The puzzle is not ready to solve.";
  }
  if (sizes.size() > kMaxDiscs || sizes.size() + rods - 1 > kMaxSymbols) {
    return "The Panex Solver handles at most " + std::to_string(kMaxSymbols + 1 - rods) +
           " discs on " + std::to_string(rods) + " rods.";
  }
  return "";
}

std::string searchFolder(const Puzzle& puzzle, const Problem& problem,
                         const std::string& workDir) {
  Rules rules;
  PKey start;
  PKey goal;
  if (!setup(puzzle, problem, rules, start, goal)) {
    return "";
  }
  return folderFor(fingerprintOf(rules, start, goal), workDir).string();
}

std::string savedSearch(const Puzzle& puzzle, const Problem& problem,
                        const std::string& workDir) {
  Rules rules;
  PKey start;
  PKey goal;
  if (!setup(puzzle, problem, rules, start, goal)) {
    return "";
  }
  const std::uint64_t fp = fingerprintOf(rules, start, goal);
  const std::filesystem::path dir = folderFor(fp, workDir);
  std::error_code ec;
  if (dir.empty() || !std::filesystem::exists(dir / "state.bin", ec)) {
    return "";
  }
  Store store;
  store.dir = dir;
  SavedState st;
  if (!store.loadState(st, fp, false)) {
    return "";
  }
  const bool mirrored = rules.mirrorable && rules.mirror(start) == goal && st.met == 0;
  const std::uint64_t deep = mirrored ? 2 * std::uint64_t{st.fwdDepth}
                                      : std::uint64_t{st.fwdDepth} + st.bwdDepth;
  return "a saved search " + std::to_string(deep) + " moves deep, " +
         shortCount(static_cast<double>(st.found)) + " stackings";
}

std::uint64_t savedMs(const Puzzle& puzzle, const Problem& problem, const std::string& workDir) {
  Rules rules;
  PKey start;
  PKey goal;
  if (!setup(puzzle, problem, rules, start, goal)) {
    return 0;
  }
  const std::uint64_t fp = fingerprintOf(rules, start, goal);
  Store store;
  store.dir = folderFor(fp, workDir);
  SavedState st;
  if (store.dir.empty() || !store.loadState(st, fp, false)) {
    return 0;
  }
  return st.ms;
}

void discardSaved(const Puzzle& puzzle, const Problem& problem, const std::string& workDir) {
  Rules rules;
  PKey start;
  PKey goal;
  if (!setup(puzzle, problem, rules, start, goal)) {
    return;
  }
  const std::filesystem::path dir = folderFor(fingerprintOf(rules, start, goal), workDir);
  std::error_code ec;
  if (!dir.empty()) {
    std::filesystem::remove_all(dir, ec);
  }
}

std::optional<Separation> solve(const Puzzle& puzzle, const Problem& problem,
                                PanexSearch& search) {
  search.outcome = Outcome::NoPath;
  search.error.clear();
  search.found = 0;
  search.peakMemory = 0;
  search.saved = false;
  search.diskBytes = 0;
  search.depthReached = 0;
  if (search.traced != nullptr) {
    search.traced->store(0);
  }

  search.error = unsupported(puzzle, problem, search.anyRules);
  Rules rules;
  PKey startKey;
  PKey goalKey;
  if (!search.error.empty() || !setup(puzzle, problem, rules, startKey, goalKey)) {
    search.outcome = Outcome::Error;
    if (search.error.empty()) {
      search.error = "The puzzle is not ready to solve.";
    }
    return std::nullopt;
  }

  if (search.towerRule) {
    std::vector<stacking::Stacking> tower;
    if (classicTower(rules, startKey, goalKey, tower)) {
      search.outcome = Outcome::Found;
      search.found = tower.size();
      search.depthReached = tower.size() - 1;
      if (search.depth != nullptr) {
        search.depth->store(search.depthReached, std::memory_order_relaxed);
      }
      if (search.progress != nullptr) {
        search.progress->store(search.found, std::memory_order_relaxed);
      }
      return stacking::pathSeparation(puzzle, problem, tower);
    }
  }

  std::uint64_t budget = sliding::kSearchMemoryBytes;
  if (search.highMemory) {
    budget = std::max(budget, physicalMemoryBytes() / 2);
  }

  // the threads: the pool given, or one of the search's own, with a core
  // more than the levels' threads for the saved level being written (the
  // original's writer thread)
  std::optional<ThreadPool> ownPool;
  ThreadPool* pool = search.pool;
  if (pool == nullptr) {
    const unsigned int levels =
        search.threads != 0 ? std::min(search.threads, kMaxThreads) : hardwareThreads();
    ownPool.emplace(std::min(levels + 1, kMaxThreads));
    pool = &*ownPool;
  }
  const unsigned int threads =
      search.threads != 0 ? std::min(search.threads, pool->cores())
                          : std::max(pool->cores() - 1, 1U);

  Searcher searcher(rules, search, *pool, threads, budget);
  searcher.startKey = startKey;
  searcher.goalKey = goalKey;
  if (rules.mirrorable && rules.mirror(startKey) == goalKey) {
    searcher.findSigma(startKey, goalKey);
  }

  // The folder for saving the search. Without one, it still solves: by
  // halves once the levels do not fit in memory, but it cannot be resumed.
  const std::uint64_t fp = fingerprintOf(rules, startKey, goalKey);
  searcher.fingerprint = fp;
  Store store;
  SavedState resumeState;
  const std::filesystem::path dir = folderFor(fp, search.workDir);
  std::uint64_t disk = search.diskBudget;
  if (!dir.empty() && store.open(dir, 1)) {
    if (disk == 0) {
      std::error_code ec;
      const std::filesystem::space_info space = std::filesystem::space(dir, ec);
      disk = ec ? 0 : std::min<std::uint64_t>(space.available / 2, 200000000000ULL);
    }
    store.budget = std::max<std::uint64_t>(disk, 1);
    if (search.resume && store.loadState(resumeState, fp, true)) {
      store.interval = resumeState.interval;
      searcher.resumeFrom = &resumeState;
      searcher.priorMs = resumeState.ms;
    } else {
      store.wipe();
    }
    searcher.store = &store;
  }

  std::vector<PKey> keys;
  const bool ok = searcher.shortestPath(startKey, goalKey, true, keys);
  search.diskBytes = store.used;
  search.levelInterval = store.interval;
  if (!ok) {
    // A finished search, found or not, needs nothing saved.
    if (searcher.store != nullptr && !search.saved && search.outcome != Outcome::Error) {
      store.remove();
    }
    return std::nullopt;
  }
  if (searcher.store != nullptr) {
    store.remove();
  }

  std::vector<stacking::Stacking> path;
  path.reserve(keys.size());
  for (const PKey& k : keys) {
    PState s;
    rules.decode(k, s);
    path.push_back(rules.toStacking(s));
  }
  search.outcome = Outcome::Found;
  return stacking::pathSeparation(puzzle, problem, path);
}

}  // namespace bt::panex

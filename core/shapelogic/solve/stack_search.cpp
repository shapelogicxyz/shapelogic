// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/stacking.cpp: findStackPath and stackBfs, with
// PACKED_STATE_BYTES and TEXT_STATE_BYTES.
#include "shapelogic/solve/stack_search.h"

#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/model/stacking.h"
#include "shapelogic/solve/sliding_search.h"

#include <algorithm>
#include <cstdint>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

namespace bt::stacking {

namespace {

// Memory one stacking takes in the search: a hash node holding its key and
// its parent's, its bucket and its share of the queue. A packed key is the
// same 8 bytes as a slide search's; a text key needs room for two strings.
constexpr std::uint64_t kPackedStateBytes = 64;
constexpr std::uint64_t kTextStateBytes = 160;

// Breadth-first search over stackings, each held as a Key that enc and dec
// turn to and from a Stacking. Fills path from start to goal when it finds
// one. Moves are tried rod by rod, so the path is the first shortest one.
template <class Key, class Encode, class Decode>
void stackBfs(const RodSet& board, const std::vector<Disc>& pieces, const Stacking& start,
              const Stacking& goal, Encode enc, Decode dec, std::uint64_t stateBytes,
              StackSearch& search, std::vector<Stacking>& path) {
  const std::uint64_t limit = search.maxStates != 0
                                  ? search.maxStates
                                  : sliding::memoryStates(stateBytes, search.highMemory);
  search.memoryStates = limit;
  const auto rods = static_cast<unsigned int>(start.size());

  const Key startKey = enc(start);
  const Key goalKey = enc(goal);
  std::unordered_map<Key, Key> parent;
  parent.emplace(startKey, startKey);
  std::queue<Key> q;
  q.push(startKey);

  std::uint64_t visited = 0;
  bool found = false;
  while (!q.empty() && !found) {
    if (parent.size() >= limit) {
      search.outcome = StackOutcome::Memory;
      break;
    }
    if ((visited & 1023) == 0) {
      if (search.progress != nullptr) {
        search.progress->store(visited, std::memory_order_relaxed);
      }
      if (search.stop.stopRequested()) {
        search.outcome = StackOutcome::Stopped;
        break;
      }
    }
    const Key cur = q.front();
    q.pop();
    visited++;
    Stacking cfg = dec(cur);
    for (unsigned int from = 0; from < rods && !found; from++) {
      for (unsigned int to = 0; to < rods; to++) {
        if (!moveAllowed(board, pieces, cfg, from, to)) {
          continue;
        }
        cfg[to].push_back(cfg[from].back());
        cfg[from].pop_back();
        const Key next = enc(cfg);
        cfg[from].push_back(cfg[to].back());
        cfg[to].pop_back();
        if (!parent.emplace(next, cur).second) {
          continue;
        }
        if (next == goalKey) {
          found = true;
          break;
        }
        q.push(next);
      }
    }
  }

  search.visited = visited;
  if (search.progress != nullptr) {
    search.progress->store(visited, std::memory_order_relaxed);
  }
  if (!found) {
    return;
  }
  for (Key k = goalKey;; k = parent[k]) {
    path.push_back(dec(k));
    if (k == startKey) {
      break;
    }
  }
  std::reverse(path.begin(), path.end());
}

}  // namespace

std::optional<Separation> findStackPath(const Puzzle& puzzle, const Problem& problem,
                                        std::uint64_t maxStates) {
  StackSearch search;
  search.maxStates = maxStates;
  return findStackPath(puzzle, problem, search);
}

std::optional<Separation> findStackPath(const Puzzle& puzzle, const Problem& problem,
                                        StackSearch& search) {
  search.outcome = StackOutcome::NoPath;
  search.visited = 0;
  Stacking start;
  Stacking goal;
  std::vector<unsigned int> sizes;
  if (!searchInput(puzzle, problem, start, goal, sizes)) {
    return std::nullopt;
  }

  const RodSet& board = puzzle.getRodSet(problem.getRodSetId());
  const std::vector<Disc> pieces = discsOf(puzzle, problem);
  const auto n = static_cast<unsigned int>(pieces.size());
  const auto rods = static_cast<unsigned int>(start.size());

  std::vector<Stacking> path;
  if (start == goal) {
    path.push_back(start);
  } else {
    // A stacking written out rod by rod: each rod's discs bottom to top,
    // then a separator, n. Packed into 64 bits when the symbols fit.
    unsigned int bits = 1;
    while ((1U << bits) <= n) {
      bits++;
    }
    const unsigned int symbols = n + rods - 1;
    if (std::uint64_t{bits} * symbols <= 64) {
      auto enc = [bits, n](const Stacking& cfg) {
        std::uint64_t key = 0;
        for (std::size_t r = 0; r < cfg.size(); r++) {
          if (r != 0) {
            key = (key << bits) | n;
          }
          for (const unsigned int d : cfg[r]) {
            key = (key << bits) | d;
          }
        }
        return key;
      };
      auto dec = [bits, n, rods, symbols](std::uint64_t key) {
        Stacking cfg(rods);
        unsigned int r = rods - 1;
        const std::uint64_t mask = (std::uint64_t{1} << bits) - 1;
        for (unsigned int i = 0; i < symbols; i++, key >>= bits) {
          const auto v = static_cast<unsigned int>(key & mask);
          if (v == n) {
            r--;
          } else {
            cfg[r].insert(cfg[r].begin(), v);
          }
        }
        return cfg;
      };
      stackBfs<std::uint64_t>(board, pieces, start, goal, enc, dec, kPackedStateBytes, search,
                              path);
    } else {
      auto enc = [n](const Stacking& cfg) {
        std::string key;
        for (std::size_t r = 0; r < cfg.size(); r++) {
          if (r != 0) {
            key += static_cast<char>(n);
          }
          for (const unsigned int d : cfg[r]) {
            key += static_cast<char>(d);
          }
        }
        return key;
      };
      auto dec = [n, rods](const std::string& key) {
        Stacking cfg(rods);
        unsigned int r = 0;
        for (const char c : key) {
          if (static_cast<unsigned char>(c) == n) {
            r++;
          } else {
            cfg[r].push_back(static_cast<unsigned char>(c));
          }
        }
        return cfg;
      };
      stackBfs<std::string>(board, pieces, start, goal, enc, dec, kTextStateBytes, search, path);
    }
    if (path.empty()) {
      return std::nullopt;
    }
  }
  search.outcome = StackOutcome::Found;
  return pathSeparation(puzzle, problem, path);
}

}  // namespace bt::stacking

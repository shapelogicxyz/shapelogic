// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The level text of a take-apart (movesText(): "5.1.3", with turns
/// "5R0.1R3") as burrTxt --json read it, and the best of a solve's levels.
/// Ported from parse_dotlevel, level_is_better and asm_cb::considerLevel
/// in src/burrTxt.cpp.

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bt::cli {

/// A level text, read.
struct DotLevel {
  int level = 0;       ///< slides and turns of the first removal
  int totalmoves = 0;  ///< all slides and turns
  int moves = 0;       ///< all slides
  int rotations = 0;   ///< all turns
};

/// Reads a level text; empty when it is not one.
[[nodiscard]] inline std::optional<DotLevel> parseDotLevel(std::string_view text) {
  int firstSlides = -1;
  int firstRots = 0;
  int moves = 0;
  int rotations = 0;
  int segments = 0;

  std::size_t p = 0;
  auto digit = [&text, &p] { return p < text.size() && text[p] >= '0' && text[p] <= '9'; };
  while (p < text.size()) {
    if (!digit()) {
      return std::nullopt;
    }
    int slides = 0;
    while (digit()) {
      slides = slides * 10 + (text[p++] - '0');
    }
    int rots = 0;
    if (p < text.size() && text[p] == 'R') {
      p++;
      if (!digit()) {
        return std::nullopt;
      }
      while (digit()) {
        rots = rots * 10 + (text[p++] - '0');
      }
    }
    if (segments == 0) {
      firstSlides = slides;
      firstRots = rots;
    }
    moves += slides;
    rotations += rots;
    segments++;

    if (p == text.size()) {
      break;
    }
    if (text[p] != '.') {
      return std::nullopt;
    }
    p++;
  }

  if (segments == 0 || firstSlides < 0) {
    return std::nullopt;
  }
  return DotLevel{firstSlides + firstRots, moves + rotations, moves, rotations};
}

/// The numbers of a level text in order ("5R0.1R3": 5, 0, 1, 3), to order
/// two texts of the same level and moves.
[[nodiscard]] inline std::vector<int> levelNumbers(std::string_view text) {
  std::vector<int> out;
  int n = -1;
  for (const char c : text) {
    if (c >= '0' && c <= '9') {
      n = (n < 0 ? 0 : n * 10) + (c - '0');
    } else if (n >= 0) {
      out.push_back(n);
      n = -1;
    }
  }
  if (n >= 0) {
    out.push_back(n);
  }
  return out;
}

/// The best level of a solve: the highest level, then the most moves, then
/// the greater text, number by number. burrTxt kept the first of equals,
/// which on several threads is whichever take-apart ended first; the last
/// rule makes the answer the same on any number of threads.
struct BestLevel {
  bool has = false;
  std::string text;
  DotLevel stats;

  void consider(const std::string& t) {
    // burrTxt copied the text into 200 bytes first
    const std::string lev = t.substr(0, 199);
    const std::optional<DotLevel> d = parseDotLevel(lev);
    if (!d) {
      return;
    }
    const bool tie = has && d->level == stats.level && d->totalmoves == stats.totalmoves;
    if (!has || d->level > stats.level ||
        (d->level == stats.level && d->totalmoves > stats.totalmoves) ||
        (tie && std::ranges::lexicographical_compare(levelNumbers(text), levelNumbers(lev)))) {
      has = true;
      text = lev;
      stats = *d;
    }
  }
};

}  // namespace bt::cli

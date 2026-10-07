// SPDX-License-Identifier: GPL-3.0-or-later
// bt_roundtrip <in> <out>: loads a puzzle file and saves it again. The
// counterpart of tests/oracle/roundtrip_old.cpp, which does the same with the
// original's library; `oracle.py roundtrip` compares the two.

#include "bt/io/puzzle_file.h"

#include <cstdio>

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fputs("usage: bt_roundtrip <in.xmpuzzle> <out.xmpuzzle>\n", stderr);
    return 2;
  }
  bt::Result<bt::Puzzle> puzzle = bt::io::loadPuzzle(argv[1]);
  if (!puzzle) {
    std::fprintf(stderr, "load failed: %s\n", puzzle.error().message.c_str());
    return 1;
  }
  bt::Result<void> saved = bt::io::savePuzzle(*puzzle, argv[2]);
  if (!saved) {
    std::fprintf(stderr, "save failed: %s\n", saved.error().message.c_str());
    return 1;
  }
  return 0;
}

// SPDX-License-Identifier: GPL-3.0-or-later
// roundtrip_old <in> <out>: loads a puzzle file with the original BurrTools
// library and saves it again. Built by `oracle.py roundtrip` against the
// original's build/libburr_core.a; tests/tools/roundtrip.cpp is the new side.

#include "lib/puzzle.h"

#include <cstdio>
#include <exception>

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fputs("usage: roundtrip_old <in.xmpuzzle> <out.xmpuzzle>\n", stderr);
    return 2;
  }
  try {
    std::unique_ptr<puzzle_c> p = puzzle_c::load(argv[1]);
    p->save(std::filesystem::path(argv[2]));
  } catch (const std::exception& e) {
    std::fprintf(stderr, "failed: %s\n", e.what());
    return 1;
  }
  return 0;
}

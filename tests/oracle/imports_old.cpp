// SPDX-License-Identifier: GPL-3.0-or-later
// imports_old: what the original's File ▸ Import, File ▸ Export ▸ Puzzlecad
// and Edit ▸ Convert brick grid type to other make, through its library:
//   imports_old ps3d <in.puz> <out.xmpuzzle>     loadPuzzlerSolver3D, then save
//   imports_old scad <in.scad> <out.xmpuzzle>    loadOpenScadPuzzle, then save
//   imports_old scad-export <puzzle> <problem> <name>
//                                               saveOpenScadPuzzle to stdout
//   imports_old convert <puzzle> <grid> <out.xmpuzzle>
//                                               doConvert to grid number <grid>, then save
// Exit 3 when the original refused (its "Could not load puzzle, sorry!",
// cb_Export_Scad's alerts, a grid canConvert refuses), with the reason on
// stderr. cb_Export_Scad's checks are GUI code, copied here. Built by
// `oracle.py imports` and `record-imports` against the original's library;
// tests/tools/imports.cpp (bt_imports) is the new side.

#include "lib/converter.h"
#include "lib/gridtype.h"
#include "lib/ps3dloader.h"
#include "lib/puzzle.h"
#include "lib/scadloader.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

namespace {

int refused(const char* why) {
  std::fprintf(stderr, "refused: %s\n", why);
  return 3;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 4 && argc != 5) {
    std::fputs("usage: imports_old ps3d|scad <in> <out> | scad-export <puzzle> <problem> <name> | "
               "convert <puzzle> <grid> <out>\n",
               stderr);
    return 2;
  }
  const std::string mode = argv[1];
  try {
    if ((mode == "ps3d" || mode == "scad") && argc == 4) {
      std::ifstream in(argv[2]);
      std::unique_ptr<puzzle_c> p =
          mode == "ps3d" ? loadPuzzlerSolver3D(&in) : loadOpenScadPuzzle(&in);
      if (!p) {
        return refused("Could not load puzzle, sorry!");
      }
      p->save(std::filesystem::path(argv[3]));
      return 0;
    }
    if (mode == "scad-export" && argc == 5) {
      std::unique_ptr<puzzle_c> p = puzzle_c::load(argv[2]);
      // cb_Export_Scad's checks
      if (p->getGridType()->getType() != gridType_c::GT_BRICKS) {
        return refused("Puzzlecad export is only available for cube-grid puzzles.");
      }
      if (p->getNumberOfShapes() == 0) {
        return refused("Nothing to export.");
      }
      const unsigned int prob = (unsigned int)std::strtoul(argv[3], nullptr, 10);
      if (!saveOpenScadPuzzle(std::cout, p.get(), argv[4], prob)) {
        return refused("Could not export puzzlecad file.");
      }
      std::cout.flush();
      return 0;
    }
    if (mode == "convert" && argc == 5) {
      std::unique_ptr<puzzle_c> p = puzzle_c::load(argv[2]);
      const auto target = (gridType_c::gridType)std::atoi(argv[3]);
      std::unique_ptr<puzzle_c> c(doConvert(p.get(), target));
      if (!c) {
        return refused("There are no grids that this puzzle can be converted to");
      }
      c->save(std::filesystem::path(argv[4]));
      return 0;
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "failed: %s\n", e.what());
    return 1;
  }
  std::fputs("bad arguments\n", stderr);
  return 2;
}

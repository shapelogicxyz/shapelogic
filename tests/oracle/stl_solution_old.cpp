// SPDX-License-Identifier: GPL-3.0-or-later
// stl_solution_old <puzzle> <problem> <solution> <folder>: what the
// original's Export Solution Pieces to STL writes (stlExportSolution_c::
// cb_Export, src/gui/stlexportsolution.cpp) with the default parameters
// and Binary STL: one file per distinct shape placed in the solution's
// assembly, named as the dialog names them, written by the original's STL
// exporter into <folder>, and pieces.txt there listing "shape count file"
// per piece (the dialog's "Pieces placed in solution" box). The dialog is
// GUI code, so placedShapeCounts, safeName and the naming loop are copied
// here unchanged; the files are written with their bare names from inside
// the folder (the binary header holds the name the writer was given).
// Built by `oracle.py stl-solution` against the original's library; its
// output is tests/data/stl_solution_by_original/<puzzle>/.

#include "lib/assembly.h"
#include "lib/gridtype.h"
#include "lib/problem.h"
#include "lib/puzzle.h"
#include "lib/solution.h"
#include "lib/stl.h"
#include "lib/voxel.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <unistd.h>

namespace {

// copied from src/gui/stlexportsolution.cpp
std::string safeName(const std::string& s) {
  std::string out = s;
  for (size_t i = 0; i < out.size(); i++) {
    char c = out[i];
    if (!isalnum((unsigned char)c) && c != '-' && c != '_') out[i] = '_';
  }
  return out;
}

// copied from src/gui/stlexportsolution.cpp
std::map<unsigned int, unsigned int> placedShapeCounts(const problem_c* pr, unsigned int sol) {
  std::map<unsigned int, unsigned int> counts;
  if (!pr) return counts;
  const solution_c* sav = pr->getSavedSolution(sol);
  if (!sav) return counts;
  const assembly_c* assm = sav->getAssembly();
  if (!assm) return counts;
  for (unsigned int i = 0; i < assm->placementCount(); i++) {
    if (!assm->isPlaced(i)) continue;
    unsigned int partId = pr->getPartIdToPieceId(i);
    unsigned int shapeId = pr->getShapeIdOfPart(partId);
    counts[shapeId]++;
  }
  return counts;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 5) {
    std::fprintf(stderr, "usage: stl_solution_old <puzzle> <problem> <solution> <folder>\n");
    return 2;
  }
  std::unique_ptr<puzzle_c> puzzle = puzzle_c::load(argv[1]);
  if (!puzzle) {
    return 1;
  }
  const unsigned int prob = static_cast<unsigned int>(std::atoi(argv[2]));
  const unsigned int sol = static_cast<unsigned int>(std::atoi(argv[3]));
  if (chdir(argv[4]) != 0) {
    return 1;
  }
  std::unique_ptr<stlExporter_c> stl(puzzle->getGridType()->getStlExporter());
  stl->setBinaryMode(true);
  problem_c* pr = puzzle->getProblem(prob);

  // the naming of cb_Export, with the folder left out ("./" for all)
  struct SolPieceEntry {
    unsigned int shapeId = 0;
    unsigned int count = 0;
    std::string fname;
  };
  std::vector<SolPieceEntry> entries;
  std::map<unsigned int, unsigned int> placed = placedShapeCounts(pr, sol);
  for (std::map<unsigned int, unsigned int>::const_iterator it = placed.begin(); it != placed.end();
       ++it) {
    unsigned int shapeId = it->first;
    unsigned int count = it->second;
    const voxel_c* v = puzzle->getShape(shapeId);
    std::string raw = v->getName();
    if (raw.empty()) {
      char buf[32];
      snprintf(buf, 32, "S%u", shapeId + 1);
      raw = buf;
    }
    std::string base = safeName(raw);
    for (const SolPieceEntry& other : entries)
      if (other.fname == base + ".stl") {
        base += "-S" + std::to_string(shapeId + 1);
        break;
      }
    SolPieceEntry e;
    e.shapeId = shapeId;
    e.count = count;
    e.fname = base + ".stl";
    entries.push_back(e);
  }

  FILE* list = std::fopen("pieces.txt", "w");
  for (const SolPieceEntry& e : entries) {
    std::fprintf(list, "%u %u %s\n", e.shapeId, e.count, e.fname.c_str());
    try {
      stl->write(e.fname.c_str(), *puzzle->getShape(e.shapeId));
    } catch (stlException_c ex) {
      std::fprintf(list, "error %s\n", ex.comment);
    }
  }
  std::fclose(list);
  return 0;
}

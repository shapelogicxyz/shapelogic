// SPDX-License-Identifier: GPL-3.0-or-later
// model_dump_old <files...>: what the original's model computes from puzzle
// files (level texts, move counts, sorting, createSpace, transform,
// smallerRotationExists), one line per fact. Built by `oracle.py model-dump`
// against the original's library; its output is tests/data/model_dump.txt,
// which tests/unit/model/test_model_dump.cpp compares with the same dump of
// the new core. Keep the two dumps line for line in step.

#include "lib/assembly.h"
#include "lib/disassembly.h"
#include "lib/gridtype.h"
#include "lib/problem.h"
#include "lib/puzzle.h"
#include "lib/solution.h"
#include "lib/symmetries.h"
#include "lib/voxel.h"

#include <cstdio>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr unsigned int kSolutions = 12;       // solutions dumped per problem
constexpr unsigned int kCompleteChecks = 3;   // of those, with complete=true

std::string placements(const assembly_c& a) {
  std::ostringstream s;
  for (unsigned int i = 0; i < a.placementCount(); i++) {
    if (i) s << ' ';
    if (a.isPlaced(i))
      s << a.getX(i) << ',' << a.getY(i) << ',' << a.getZ(i) << ',' << (int)a.getTransformation(i);
    else
      s << 'x';
  }
  return s.str();
}

template <class D>
void dumpDis(std::ostream& out, const char* what, const D& d) {
  out << ' ' << what << " text " << d.movesText() << " moves " << d.sumMoves() << " rots "
      << d.sumRotations() << " seqs " << d.getNumSequences() << " len";
  for (unsigned int i = 0; i <= d.getNumSequences(); i++) out << ' ' << d.getSequenceLength(i);
  out << '\n';
}

std::string asmNumbers(const problem_c& pr) {
  std::ostringstream s;
  for (unsigned int i = 0; i < pr.getNumberOfSavedSolutions(); i++)
    s << ' ' << pr.getSavedSolution(i)->getAssemblyNumber() << '/'
      << pr.getSavedSolution(i)->getSolutionNumber();
  return s.str();
}

void dumpProblem(std::ostream& out, puzzle_c& puz, unsigned int p) {
  problem_c& pr = *puz.getProblem(p);
  out << "problem " << p << " state " << (int)pr.getSolveState() << " parts "
      << pr.getNumberOfParts() << " pieces " << pr.getNumberOfPieces() << " solutions "
      << pr.getNumberOfSavedSolutions() << '\n';

  const symmetries_c* sym = puz.getGridType()->getSymmetries();
  const unsigned int n = std::min(pr.getNumberOfSavedSolutions(), kSolutions);
  for (unsigned int i = 0; i < n; i++) {
    const solution_c& sol = *pr.getSavedSolution(i);
    const assembly_c& a = *sol.getAssembly();
    out << "solution " << i << " asm " << sol.getAssemblyNumber() << " sol "
        << sol.getSolutionNumber() << '\n';
    out << " assembly " << placements(a) << '\n';
    out << " valid " << a.validSolution(pr) << '\n';

    if (const separation_c* sep = sol.getDisassembly()) {
      dumpDis(out, "separation", *sep);
      separationInfo_c info(sep);
      dumpDis(out, "info", info);
    } else if (const disassembly_c* d = sol.getDisassemblyInfo()) {
      dumpDis(out, "info", *static_cast<const separationInfo_c*>(d));
    }
    if (sol.getDisassemblyInfo() && pr.getSavedSolution(0)->getDisassemblyInfo())
      out << " compare " << sol.getDisassemblyInfo()->compare(pr.getSavedSolution(0)->getDisassemblyInfo())
          << '\n';

    if (!pr.resultValid()) continue;

    int ox = 0, oy = 0, oz = 0;
    std::unique_ptr<voxel_c> space = a.createSpace(pr, &ox, &oy, &oz);
    out << " space " << space->getX() << ' ' << space->getY() << ' ' << space->getZ() << " origin "
        << ox << ' ' << oy << ' ' << oz << " cells";
    for (unsigned int c = 0; c < space->getXYZ(); c++) out << ' ' << (int)space->get(c);
    out << '\n';

    for (unsigned int t = 0; t < sym->getNumTransformations(); t++) {
      assembly_c tmp(&a);
      const bool ok = tmp.transform(t, pr, nullptr);
      out << " transform " << t << ' ' << ok;
      if (ok) out << ' ' << placements(tmp);
      out << '\n';
    }

    out << " smaller " << a.smallerRotationExists(pr, 0, nullptr, false);
    if (i < kCompleteChecks) out << ' ' << a.smallerRotationExists(pr, 0, nullptr, true);
    out << '\n';
  }

  // sorting, on the whole list
  for (int by = 0; by < 4; by++) {
    pr.sortSolutions(by);
    out << "sort " << by << asmNumbers(pr) << '\n';
  }
  for (int m = 0; m < 4; m++) {
    pr.sortSolutionsBySolverMethod(m);
    out << "solver sort " << m << asmNumbers(pr) << '\n';
  }
}

}  // namespace

int main(int argc, char** argv) {
  std::ostringstream out;
  for (int f = 1; f < argc; f++) {
    std::unique_ptr<puzzle_c> puz = puzzle_c::load(argv[f]);
    out << "puzzle " << std::filesystem::path(argv[f]).filename().string() << " grid "
        << (int)puz->getGridType()->getType() << " shapes " << puz->getNumberOfShapes()
        << " problems " << puz->getNumberOfProblems() << " colors " << puz->colorNumber() << '\n';
    for (unsigned int p = 0; p < puz->getNumberOfProblems(); p++) dumpProblem(out, *puz, p);
  }
  std::fputs(out.str().c_str(), stdout);
  return 0;
}

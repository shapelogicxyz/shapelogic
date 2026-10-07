// SPDX-License-Identifier: GPL-3.0-or-later
// bt_solutions <file>: what a puzzle file holds of each problem's solving,
// as JSON, read with the new core: the state, the counts, and every saved
// solution with its assembly, level text, move counts and the whole
// separation tree (as text: pieces, every state, both branches). Both
// programs' solved files go through it for `oracle.py compare-files`.

#include "bt/io/puzzle_file.h"
#include "bt/model/assembly.h"
#include "bt/model/disassembly.h"
#include "bt/model/problem.h"
#include "bt/model/puzzle.h"
#include "bt/model/solution.h"

#include <cstdio>
#include <sstream>
#include <string>

namespace {

std::string assemblyText(const bt::Assembly& a) {
  std::ostringstream out;
  for (unsigned int i = 0; i < a.placementCount(); i++) {
    if (a.isPlaced(i)) {
      out << static_cast<unsigned int>(a.getTransformation(i)) << "," << a.getX(i) << ","
          << a.getY(i) << "," << a.getZ(i) << ";";
    } else {
      out << "-;";
    }
  }
  return out.str();
}

void treeText(const bt::Separation* s, std::ostringstream& out) {
  if (s == nullptr) {
    out << "-";
    return;
  }
  out << "[";
  for (const unsigned int p : s->getPieces()) {
    out << p << ",";
  }
  out << "|";
  for (unsigned int k = 0; k < s->getNumStates(); k++) {
    const bt::State& st = s->getState(k);
    for (unsigned int i = 0; i < st.getPiecenumber(); i++) {
      out << st.getX(i) << " " << st.getY(i) << " " << st.getZ(i) << " " << st.getOrient(i)
          << ";";
    }
    if (st.isRotationArrival()) {
      out << "R" << st.getRotPiece() << " " << st.getRotPivotX() << " " << st.getRotPivotY()
          << " " << st.getRotPivotZ() << " " << st.getRotAxis() << " " << st.getRotSense();
    }
    out << "/";
  }
  treeText(s->getRemoved(), out);
  treeText(s->getLeft(), out);
  out << "]";
}

const char* stateName(bt::ProblemState s) {
  switch (s) {
    case bt::ProblemState::Unsolved:
      return "unsolved";
    case bt::ProblemState::Solving:
      return "solving";
    case bt::ProblemState::Solved:
      return "solved";
    case bt::ProblemState::Unknown:
      return "unknown";
  }
  return "?";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fputs("usage: bt_solutions <file>\n", stderr);
    return 2;
  }
  const bt::Result<bt::Puzzle> loaded = bt::io::loadPuzzle(argv[1]);
  if (!loaded) {
    std::fprintf(stderr, "load failed: %s\n", loaded.error().message.c_str());
    return 1;
  }
  const bt::Puzzle& puzzle = *loaded;

  std::printf("{\"problems\":[");
  for (unsigned int p = 0; p < puzzle.getNumberOfProblems(); p++) {
    const bt::Problem& pr = puzzle.getProblem(p);
    std::printf("%s{\"state\":\"%s\"", p != 0 ? "," : "", stateName(pr.getSolveState()));
    if (pr.numAssembliesKnown()) {
      std::printf(",\"assemblies\":%llu,\"solutions\":%llu",
                  static_cast<unsigned long long>(pr.getNumAssemblies()),
                  static_cast<unsigned long long>(pr.getNumSolutions()));
    }
    std::printf(",\"saved\":[");
    for (unsigned int s = 0; s < pr.getNumberOfSavedSolutions(); s++) {
      const bt::Solution& sol = pr.getSavedSolution(s);
      std::printf("%s{\"assembly\":\"%s\"", s != 0 ? "," : "",
                  assemblyText(sol.getAssembly()).c_str());
      if (const bt::Disassembly* d = sol.getDisassemblyInfo(); d != nullptr) {
        std::printf(",\"level\":\"%s\",\"moves\":%u,\"rotations\":%u", d->movesText().c_str(),
                    d->sumMoves(), d->sumRotations());
      }
      if (const bt::Separation* t = sol.getDisassembly(); t != nullptr) {
        std::ostringstream tree;
        treeText(t, tree);
        std::printf(",\"tree\":\"%s\"", tree.str().c_str());
      }
      std::printf("}");
    }
    std::printf("]}");
  }
  std::printf("]}\n");
  return 0;
}

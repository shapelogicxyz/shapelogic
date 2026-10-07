// SPDX-License-Identifier: GPL-3.0-or-later
// The model dump of tests/oracle/model_dump_old.cpp, made with the new core,
// compared line by line with the original's recording
// (tests/data/model_dump.txt, `oracle.py model-dump`). Keep the two dumps in
// step.

#include "bt/io/puzzle_file.h"
#include "bt/model/puzzle.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace bt;

constexpr unsigned int kSolutions = 12;
constexpr unsigned int kCompleteChecks = 3;

std::string placements(const Assembly& a) {
  std::ostringstream s;
  for (unsigned int i = 0; i < a.placementCount(); i++) {
    if (i != 0) {
      s << ' ';
    }
    if (a.isPlaced(i)) {
      s << a.getX(i) << ',' << a.getY(i) << ',' << a.getZ(i) << ','
        << static_cast<int>(a.getTransformation(i));
    } else {
      s << 'x';
    }
  }
  return s.str();
}

void dumpDis(std::ostream& out, const char* what, const Disassembly& d) {
  out << ' ' << what << " text " << d.movesText() << " moves " << d.sumMoves() << " rots "
      << d.sumRotations() << " seqs " << d.getNumSequences() << " len";
  for (unsigned int i = 0; i <= d.getNumSequences(); i++) {
    out << ' ' << d.getSequenceLength(i);
  }
  out << '\n';
}

std::string asmNumbers(const Problem& pr) {
  std::ostringstream s;
  for (unsigned int i = 0; i < pr.getNumberOfSavedSolutions(); i++) {
    s << ' ' << pr.getSavedSolution(i).getAssemblyNumber() << '/'
      << pr.getSavedSolution(i).getSolutionNumber();
  }
  return s.str();
}

void dumpProblem(std::ostream& out, Puzzle& puz, unsigned int p) {
  Problem& pr = puz.getProblem(p);
  out << "problem " << p << " state " << static_cast<int>(pr.getSolveState()) << " parts "
      << pr.getNumberOfParts() << " pieces " << pr.getNumberOfPieces() << " solutions "
      << pr.getNumberOfSavedSolutions() << '\n';

  const Symmetries& sym = puz.getGridType().getSymmetries();
  const unsigned int n = std::min(pr.getNumberOfSavedSolutions(), kSolutions);
  for (unsigned int i = 0; i < n; i++) {
    const Solution& sol = pr.getSavedSolution(i);
    const Assembly& a = sol.getAssembly();
    out << "solution " << i << " asm " << sol.getAssemblyNumber() << " sol "
        << sol.getSolutionNumber() << '\n';
    out << " assembly " << placements(a) << '\n';
    out << " valid " << a.validSolution(pr) << '\n';

    if (const Separation* sep = sol.getDisassembly()) {
      dumpDis(out, "separation", *sep);
      dumpDis(out, "info", SeparationInfo(*sep));
    } else if (const Disassembly* d = sol.getDisassemblyInfo()) {
      dumpDis(out, "info", *d);
    }
    const Disassembly* first = pr.getSavedSolution(0).getDisassemblyInfo();
    if (sol.getDisassemblyInfo() != nullptr && first != nullptr) {
      out << " compare " << sol.getDisassemblyInfo()->compare(*first) << '\n';
    }

    if (!pr.resultValid()) {
      continue;
    }

    int ox = 0;
    int oy = 0;
    int oz = 0;
    const std::unique_ptr<Voxel> space = a.createSpace(puz, pr, &ox, &oy, &oz);
    out << " space " << space->getX() << ' ' << space->getY() << ' ' << space->getZ()
        << " origin " << ox << ' ' << oy << ' ' << oz << " cells";
    for (unsigned int c = 0; c < space->getXYZ(); c++) {
      out << ' ' << static_cast<int>(space->get(c));
    }
    out << '\n';

    for (unsigned int t = 0; t < sym.getNumTransformations(); t++) {
      Assembly tmp(a);
      const bool ok = tmp.transform(static_cast<unsigned char>(t), puz, pr, nullptr);
      out << " transform " << t << ' ' << ok;
      if (ok) {
        out << ' ' << placements(tmp);
      }
      out << '\n';
    }

    out << " smaller " << a.smallerRotationExists(puz, pr, 0, nullptr, false);
    if (i < kCompleteChecks) {
      out << ' ' << a.smallerRotationExists(puz, pr, 0, nullptr, true);
    }
    out << '\n';
  }

  for (int by = 0; by < 4; by++) {
    pr.sortSolutions(by);
    out << "sort " << by << asmNumbers(pr) << '\n';
  }
  for (int m = 0; m < 4; m++) {
    pr.sortSolutionsBySolverMethod(m);
    out << "solver sort " << m << asmNumbers(pr) << '\n';
  }
}

std::vector<std::string> lines(std::istream& in) {
  std::vector<std::string> out;
  std::string line;
  while (std::getline(in, line)) {
    out.push_back(line);
  }
  return out;
}

}  // namespace

TEST_CASE("model dump: the model computes what the original computes", "[model][dump]") {
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::directory_iterator("tests/data/puzzles")) {
    if (entry.path().extension() == ".xmpuzzle") {
      files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end());
  REQUIRE(files.size() >= 9);

  std::ostringstream out;
  for (const auto& f : files) {
    Result<Puzzle> puz = io::loadPuzzle(f);
    REQUIRE(puz.has_value());
    out << "puzzle " << f.filename().string() << " grid "
        << static_cast<int>(puz->getGridType().getType()) << " shapes "
        << puz->getNumberOfShapes() << " problems " << puz->getNumberOfProblems() << " colors "
        << puz->colorNumber() << '\n';
    for (unsigned int p = 0; p < puz->getNumberOfProblems(); p++) {
      dumpProblem(out, *puz, p);
    }
  }

  std::ifstream recorded("tests/data/model_dump.txt");
  REQUIRE(recorded.good());
  std::istringstream ours(out.str());
  const std::vector<std::string> want = lines(recorded);
  const std::vector<std::string> got = lines(ours);

  // report the first differing line, not thousands
  const std::size_t n = std::min(want.size(), got.size());
  std::size_t at = 0;
  while (at < n && want[at] == got[at]) {
    at++;
  }
  INFO("line " << (at + 1) << "\n  original: " << (at < want.size() ? want[at] : "<end>")
               << "\n  new:      " << (at < got.size() ? got[at] : "<end>"));
  CHECK(at == want.size());
  CHECK(want.size() == got.size());
}

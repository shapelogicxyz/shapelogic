// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from mainWindow_c::StatProblemInfo and StatPieceInfo
// (src/gui/mainwindow.cpp), statusWindow_c's table (src/gui/statuswindow.cpp)
// and src/lib/millable.cpp, the layout of voxelFrame_c::showRodSet
// (src/gui/voxelframe.cpp), and placedShapeCounts, safeName and the file
// names of cb_Export (src/gui/stlexportsolution.cpp). The verdict and the hole range of the fit are
// new (the web UI's fit summary); the status line is the original's.
#include "bt/app/queries.h"

#include "bt/base/int_cast.h"
#include "bt/grid/grid_type.h"
#include "bt/grid/symmetries.h"
#include "bt/grid/voxel.h"
#include "bt/grid/voxel_table.h"
#include "bt/model/assembly.h"
#include "bt/model/problem.h"
#include "bt/model/puzzle.h"
#include "bt/model/solution.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>

namespace bt::app {

FitStats problemFit(const Puzzle& puzzle, unsigned int problemIndex) {
  const Problem& pr = puzzle.getProblem(problemIndex);
  FitStats f;
  if (pr.maxHolesDefined()) {
    f.maxHoles = pr.getMaxHoles();
  }
  if (!pr.resultValid()) {
    return f;
  }

  for (unsigned int i = 0; i < pr.getNumberOfParts(); i++) {
    const unsigned int filled = pr.partShape(puzzle, i).countState(Voxel::kFilled);
    f.cellsMax += filled * pr.getPartMaximum(i);
    f.cellsMin += filled * pr.getPartMinimum(i);
    f.piecesMax += pr.getPartMaximum(i);
    f.piecesMin += pr.getPartMinimum(i);
  }
  const Voxel& result = pr.resultShape(puzzle);
  f.resultMin = result.countState(Voxel::kFilled);
  f.resultMax = f.resultMin + result.countState(Voxel::kVariable);

  if (f.cellsMin > f.resultMax) {
    f.verdict = FitVerdict::TooLarge;
  } else if (f.cellsMax < f.resultMin) {
    f.verdict = FitVerdict::TooSmall;
  } else {
    f.verdict = FitVerdict::Fits;
  }
  f.holesMin = f.resultMax > f.cellsMax ? f.resultMax - f.cellsMax : 0;
  f.holesMax = f.resultMax > f.cellsMin ? f.resultMax - f.cellsMin : 0;

  // the original's line, in its 100 character buffer
  char txt[100];
  if (f.cellsMax == f.cellsMin) {
    std::snprintf(txt, sizeof txt,
                  "Problem P%u result can contain %u - %u voxels, pieces (n = %u) contain %u voxels",
                  problemIndex + 1, f.resultMin, f.resultMax, pr.getNumberOfPieces(), f.cellsMax);
  } else {
    std::snprintf(
        txt, sizeof txt,
        "Problem P%u result can contain %u - %u voxels, pieces (n = %u) contain %u-%u voxels",
        problemIndex + 1, f.resultMin, f.resultMax, pr.getNumberOfPieces(), f.cellsMin,
        f.cellsMax);
  }
  f.statusLine = txt;
  return f;
}

std::vector<ShapeInfo> shapeTable(const Puzzle& puzzle) {
  std::vector<ShapeInfo> rows;
  VoxelTable tab([&puzzle](unsigned int i) -> const Voxel& { return puzzle.getShape(i); });
  const GridType& gt = puzzle.getGridType();
  const bool bricks = gt.getType() == GridKind::Bricks;
  const Symmetries& sym = gt.getSymmetries();

  for (unsigned int s = 0; s < puzzle.getNumberOfShapes(); s++) {
    const Voxel& v = puzzle.getShape(s);
    ShapeInfo r;
    r.fixed = v.countState(Voxel::kFilled);
    r.variable = v.countState(Voxel::kVariable);

    unsigned int idx = 0;
    unsigned char trans = 0;
    if (tab.getSpace(v, &idx, &trans, VoxelTable::kParMirror)) {
      r.identicalMirror = idx;
    }
    if (tab.getSpace(v, &idx, &trans, 0)) {
      r.identicalShape = idx;
    }
    if (tab.getSpace(v, &idx, &trans, VoxelTable::kParColour) &&
        trans < sym.getNumTransformations()) {
      r.identicalComplete = idx;
    }

    r.connectedFace = v.connected(0, true, 0);
    r.connectedEdge = v.connected(1, true, 0);
    r.connectedCorner = v.connected(2, true, 0);
    r.holes2D = !v.connected(0, false, 0, false);
    r.holes3D = !v.connected(0, false, 0);
    if (bricks) {
      r.notchable = isNotchable(v);
      r.millable = isMillable(v);
    }
    if (sym.symmetryKnown(v)) {
      r.symmetry = sym.calculateSymmetry(v);
    }
    rows.push_back(r);

    tab.addSpace(s, VoxelTable::kParMirror);
    tab.addSpace(s, VoxelTable::kParMirror | VoxelTable::kParColour);
  }
  return rows;
}

std::string shapeStatusLine(const Puzzle& puzzle, unsigned int shapeIndex) {
  const Voxel& v = puzzle.getShape(shapeIndex);
  const unsigned int fx = v.countState(Voxel::kFilled);
  const unsigned int vr = v.countState(Voxel::kVariable);
  char txt[160];
  std::snprintf(txt, sizeof txt, "Shape S%u has %u voxels (%u fixed, %u variable)", shapeIndex + 1,
                fx + vr, fx, vr);
  return txt;
}

bool isNotchable(const Voxel& v) {
  // the shape's projections on the three planes; it can be made by cuts
  // when it is exactly the cells inside all three
  const GridType gt(GridKind::Bricks);
  const std::unique_ptr<Voxel> vxy = gt.makeVoxel(v.getX(), v.getY(), 1, Voxel::kEmpty);
  const std::unique_ptr<Voxel> vxz = gt.makeVoxel(v.getX(), 1, v.getZ(), Voxel::kEmpty);
  const std::unique_ptr<Voxel> vyz = gt.makeVoxel(1, v.getY(), v.getZ(), Voxel::kEmpty);

  for (unsigned int x = 0; x < v.getX(); x++) {
    for (unsigned int y = 0; y < v.getY(); y++) {
      for (unsigned int z = 0; z < v.getZ(); z++) {
        if (v.isFilled(x, y, z)) {
          vxy->setState(x, y, 0, Voxel::kFilled);
          vxz->setState(x, 0, z, Voxel::kFilled);
          vyz->setState(0, y, z, Voxel::kFilled);
        }
      }
    }
  }

  for (unsigned int x = 0; x < v.getX(); x++) {
    for (unsigned int y = 0; y < v.getY(); y++) {
      for (unsigned int z = 0; z < v.getZ(); z++) {
        if (v.isFilled(x, y, z) !=
            (vxy->isFilled(x, y, 0) && vxz->isFilled(x, 0, z) && vyz->isFilled(0, y, z))) {
          return false;
        }
      }
    }
  }
  return true;
}

bool isMillable(const Voxel& v) {
  for (int x = 0; x < asInt(v.getX()); x++) {
    for (int y = 0; y < asInt(v.getY()); y++) {
      for (int z = 0; z < asInt(v.getZ()); z++) {
        if (v.isEmpty(asUnsigned(x), asUnsigned(y), asUnsigned(z)) &&
            (v.isFilled2(x - 1, y, z) || v.isFilled2(x + 1, y, z)) &&
            (v.isFilled2(x, y - 1, z) || v.isFilled2(x, y + 1, z)) &&
            (v.isFilled2(x, y, z - 1) || v.isFilled2(x, y, z + 1))) {
          return false;
        }
      }
    }
  }
  return true;
}

ProblemKind problemKind(const Puzzle& puzzle) {
  switch (puzzle.getGridType().getType()) {
    case GridKind::Sliding:
      return ProblemKind::Sliding;
    case GridKind::Stacking:
      return ProblemKind::Stacking;
    default:
      return ProblemKind::Brick;
  }
}

stacking::BoardLayout rodSetLayout(const Puzzle& puzzle, unsigned int rodSet) {
  const stacking::RodSet& board = puzzle.getRodSet(rodSet);
  const unsigned int height = std::max(board.growHeight ? 5U : board.definedHeight, 1U);
  int maxR = 1;
  for (unsigned int i = 0; i < puzzle.getNumberOfShapes(); i++) {
    maxR = std::max(maxR, asInt(puzzle.getShape(i).getDiskSize()));
  }
  stacking::BoardLayout lay;
  lay.spacing = 2 * maxR + 1;
  lay.rodCount = stacking::totalRods(board);
  lay.rodHeight = height;
  lay.panex = board.panexColumns;
  lay.pocket = lay.rodCount > board.rodCount;
  lay.maxSize = asUnsigned(maxR);
  // the pocket stands left of rod 1, so every x stays at 0 or more
  const int shift = lay.pocket ? 1 : 0;
  for (unsigned int r = 0; r < lay.rodCount; r++) {
    lay.rodX.push_back((stacking::rodPosition(board, r) + shift) * lay.spacing);
    lay.rodHeights.push_back(stacking::isPocket(board, r) ? board.pocketHeight : height);
  }
  return lay;
}

namespace {

// A file-system-safe version of a name (safeName): anything but ASCII
// letters, digits, '-' and '_' becomes '_' (isalnum of the C locale).
std::string safeName(const std::string& name) {
  std::string out = name;
  for (char& c : out) {
    const bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    if (!alnum && c != '-' && c != '_') {
      c = '_';
    }
  }
  return out;
}

}  // namespace

std::vector<StlPiece> stlPieces(const Puzzle& puzzle, const Problem& problem,
                                const Solution& solution) {
  // the shapes actually placed in the assembly, not the parts the problem
  // allows
  std::map<unsigned int, unsigned int> counts;
  const Assembly& assm = solution.getAssembly();
  for (unsigned int i = 0; i < assm.placementCount(); i++) {
    if (!assm.isPlaced(i)) {
      continue;
    }
    counts[problem.getShapeIdOfPart(problem.getPartIdToPieceId(i))]++;
  }
  std::vector<StlPiece> pieces;
  for (const auto& [shape, count] : counts) {
    std::string raw = puzzle.getShape(shape).getName();
    if (raw.empty()) {
      raw = "S" + std::to_string(shape + 1);
    }
    // Two pieces can share a name, or names that become the same file
    // name: a second file would overwrite the first. They are told apart.
    std::string base = safeName(raw);
    for (const StlPiece& other : pieces) {
      if (other.fileName == base + ".stl") {
        base += "-S" + std::to_string(shape + 1);
        break;
      }
    }
    pieces.push_back({shape, count, base + ".stl"});
  }
  return pieces;
}

}  // namespace bt::app

// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/assembler_0.cpp (generateFirstRow, AddPieceNode,
// AddVoxelNode, getPieceInformation, prepare, the unit checks of
// createMatrix, clumpify, reduce and the row and column operations it
// uses).
#include "shapelogic/solve/dlx_matrix.h"

#include "shapelogic/base/int_cast.h"
#include "shapelogic/grid/symmetries.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/model/sliding.h"
#include "shapelogic/solve/matrix_build.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <unordered_map>

namespace bt {

bool everyPartOnePiece(const Problem& problem) {
  for (unsigned int s = 0; s < problem.getNumberOfParts(); s++) {
    if ((problem.getPartMaximum(s) > 1) ||
        (problem.getPartMaximum(s) != problem.getPartMinimum(s))) {
      return false;
    }
  }
  return true;
}

void DlxMatrix::generateFirstRow() {
  for (unsigned int i = 0; i < varivoxelStart; i++) {
    nodes.right.push_back(i + 1);
    nodes.left.push_back(i - 1);
    nodes.upDown.push_back(i);
    nodes.upDown.push_back(i);
    nodes.colCount.push_back(0);
  }

  // make the list cyclic
  nodes.left[0] = varivoxelStart - 1;
  nodes.right[varivoxelStart - 1] = 0;

  // Column nodes for the variable voxels. They are not in the header list,
  // so they need not be filled, but otherwise behave like normal columns.
  for (unsigned int j = varivoxelStart; j <= varivoxelEnd; j++) {
    nodes.left.push_back(j - 1);
    nodes.right.push_back(j + 1);
    nodes.upDown.push_back(j);
    nodes.upDown.push_back(j);
    nodes.colCount.push_back(0);
  }
  nodes.left[varivoxelStart] = varivoxelEnd;
  nodes.right[varivoxelEnd] = varivoxelStart;
}

unsigned int DlxMatrix::addPieceNode(unsigned int piece, unsigned int rot, int x, int y, int z) {
  const auto piecenode = static_cast<unsigned int>(nodes.left.size());

  nodes.left.push_back(piecenode);
  nodes.right.push_back(piecenode);
  nodes.upDown.push_back(nodes.up(piece + 1));
  nodes.upDown.push_back(piece + 1);

  nodes.down(nodes.up(piece + 1)) = piecenode;
  nodes.up(piece + 1) = piecenode;

  nodes.colCount.push_back(piece + 1);
  nodes.colCount[piece + 1]++;

  placements.push_back(DlxPlacement{x, y, z, static_cast<unsigned char>(rot), piecenode, piece});

  return piecenode;
}

std::size_t DlxMatrix::rowIndexOf(unsigned int node) const {
  return bt::rowIndexOf(placements, node);
}

// Finds identical columns (contained in exactly the same rows) and removes
// all but one of each group. This saves no iterations but makes them cheaper.
// Columns are grouped by a hash of their row signature and only compared
// exactly within a group, which is linear in the number of nodes.
unsigned int DlxMatrix::clumpify() {
  const std::vector<ColumnTwin> twins = columnTwins(
      nodes, placements, [](unsigned int, std::vector<std::uint64_t>&) {},
      [](unsigned int, std::vector<std::uint64_t>&) {});
  for (const ColumnTwin& t : twins) {
    removeColumn(t.column);
  }
  return static_cast<unsigned int>(twins.size());
}

void DlxMatrix::addVoxelNode(unsigned int col, unsigned int piecenode) {
  const auto newnode = static_cast<unsigned int>(nodes.left.size());

  nodes.right.push_back(piecenode);
  nodes.left.push_back(nodes.left[piecenode]);
  nodes.right[nodes.left[piecenode]] = newnode;
  nodes.left[piecenode] = newnode;

  nodes.upDown.push_back(nodes.up(col));
  nodes.upDown.push_back(col);
  nodes.down(nodes.up(col)) = newnode;
  nodes.up(col) = newnode;

  nodes.colCount.push_back(col);
  nodes.colCount[col]++;
}

// Builds the rows. Additions to Knuth's algorithm: variable voxels (empty
// spaces in the result) are columns that are not in the header list, so the
// search never has to fill them but they can be used once at most.
//
// Returns kPrepared, kPrepareStopped when `stop` stopped it (the rows are
// then not all there), or minus the shape id of a part that has no
// placement.
int DlxMatrix::prepare(const Puzzle& puzzle, const Problem& problem, bool strictColors,
                       bool startLocks, const std::vector<bool>& freeStarts,
                       const std::vector<std::uint64_t>& partTransforms, RotationFilter& filter,
                       PrepareCounters& progress, const StopToken& stop) {
  const Voxel& result = problem.resultShape(puzzle);

  // the matrix column of every result voxel (a lookup, because computing it
  // from the counts of filled and variable voxels is slow)
  std::vector<unsigned int> columns(result.getXYZ());
  const unsigned int piecenumber = problem.getNumberOfPieces();

  {
    unsigned int v = 0;
    unsigned int c = 0;

    // A sliding tray does not have to be filled: normal cells are optional
    // columns as well, so that empty cells can be the space pieces slide in.
    const bool optionalFloor = sliding::isSliding(puzzle);

    for (unsigned int i = 0; i < result.getXYZ(); i++) {
      switch (result.getState(i)) {
        case Voxel::kVariable:
          columns[i] = varivoxelStart + v++;
          break;
        case Voxel::kFilled:
          if (optionalFloor) {
            columns[i] = varivoxelStart + v++;
          } else {
            columns[i] = 1 + piecenumber + c++;
          }
          break;
        default:
          columns[i] = 0;
          break;
      }
    }
  }

  // The symmetry breaker. To find few rotated duplicates in the first place,
  // one piece is not placed in all its orientations: the ones the result's
  // symmetries would map onto each other are left out. The piece chosen is
  // the one that shares the fewest symmetries with the result.
  const Symmetry resultSym = result.selfSymmetries();
  const GridType& gt = puzzle.getGridType();
  const Symmetries& sym = gt.getSymmetries();
  unsigned int symBreakerShape = 0xFFFFFFFF;

  // with only the identity in the result, everything must be tried; and
  // with orientation limits, a placement left out has no rotated twin
  if (!unSymmetric(resultSym) && partTransforms.empty()) {
    // FIXME (original): with several suitable pieces, the one with the most
    // placements would gain more
    unsigned int bestFound =
        sym.countSymmetryIntersection(resultSym, problem.partShape(puzzle, 0).selfSymmetries());
    symBreakerShape = 0;

    for (unsigned int i = 1; i < problem.getNumberOfParts(); i++) {
      const unsigned int cnt = sym.countSymmetryIntersection(
          resultSym, problem.partShape(puzzle, i).selfSymmetries());

      if (cnt < bestFound) {
        bestFound = cnt;
        symBreakerShape = i;
      }
    }

    filter.request(symBreakerShape, nullptr);

    if (sym.symmetryContainsMirror(resultSym)) {
      if (std::unique_ptr<MirrorInfo> mir = mirrorPairs(puzzle, problem, false)) {
        filter.request(symBreakerShape, std::move(mir));
      }
    }
  }

  // Node 0 is the start of everything, nodes 1..n are the column headers.
  // The variable voxels' columns are left out of the header ring so that
  // the search never chooses them to fill.
  generateFirstRow();

  // each placement a row: its piece's column and the voxels it fills
  struct Rows final : PlacementSink {
    Rows(DlxMatrix& matrix, const std::vector<unsigned int>& cols, PrepareCounters& counters)
        : m(matrix), columns(cols), progress(counters) {}

    void beginPart(unsigned int /*part*/) override {}
    void addRow(unsigned int part, unsigned int rot, int x, int y, int z, long base,
                const std::vector<long>& offsets) override {
      const unsigned int piecenode = m.addPieceNode(part, rot, x, y, z);
      for (const long off : offsets) {
        m.addVoxelNode(columns[static_cast<std::size_t>(base + off)], piecenode);
      }
    }
    void endOrientation() override {
      progress.placements.store(m.placements.size(), std::memory_order_relaxed);
    }

    DlxMatrix& m;
    const std::vector<unsigned int>& columns;
    PrepareCounters& progress;
  };
  Rows rows(*this, columns, progress);

  // every part is a piece that must be placed: one that fits nowhere ends
  // the build
  const PlaceParts how{strictColors,    startLocks, freeStarts, partTransforms,
                       symBreakerShape, resultSym, true};
  return placeParts(puzzle, problem, how, rows, progress.piece, stop);
}

AssemblerError DlxMatrix::build(const Puzzle& puzzle, const Problem& problem, bool strictColors,
                                bool startLocks, const std::vector<bool>& freeStarts,
                                const std::vector<std::uint64_t>& partTransforms,
                                RotationFilter& filter, PrepareCounters& progress,
                                int& param, const StopToken& stop) {
  pieces = problem.getNumberOfPieces();

  // the filled and variable units
  int resVari = asInt(problem.resultShape(puzzle).countState(Voxel::kVariable));
  const int resFilled = asInt(problem.resultShape(puzzle).countState(Voxel::kFilled)) + resVari;

  // Sliding lets the pieces leave normal cells empty: those cells are
  // optional, so falling short of the tray is no error.
  if (sliding::isSliding(puzzle)) {
    resVari = resFilled;
  }

  varivoxelStart = 1 + pieces + asUnsigned(resFilled - resVari);
  varivoxelEnd = 1 + pieces + asUnsigned(resFilled);

  // the pieces may not have more units than the result has voxels, and the
  // filled voxels of the result must be fillable by them
  int h = resFilled;

  for (unsigned int j = 0; j < problem.getNumberOfParts(); j++) {
    h -= asInt(problem.partShape(puzzle, j).countState(Voxel::kFilled));
  }

  if (h < 0) {
    param = -h;
    return AssemblerError::TooManyUnits;
  }

  if (h > resVari) {
    param = h - resVari;
    return AssemblerError::TooFewUnits;
  }

  holes = h;

  const int error =
      prepare(puzzle, problem, strictColors, startLocks, freeStarts, partTransforms, filter,
              progress, stop);

  if (error == kPrepareStopped) {
    return AssemblerError::Stopped;
  }
  // a piece that cannot be placed
  if (error <= 0) {
    param = -error;
    return AssemblerError::CannotPlace;
  }
  return AssemblerError::None;
}

// coverRow(), but gives up (undoing what it did) as soon as a column outside
// the row is left with no rows.
bool DlxMatrix::tryCoverRow(unsigned int r, unsigned int* columns) {
  std::memset(columns, 0, varivoxelEnd * sizeof(unsigned int));

  for (unsigned int j = nodes.right[r]; j != r; j = nodes.right[j]) {
    columns[nodes.colCount[j]] = 1;
  }

  for (unsigned int j = nodes.right[r]; j != r; j = nodes.right[j]) {
    nodes.cover(nodes.colCount[j]);

    for (unsigned int k = nodes.right[0]; k; k = nodes.right[k]) {
      if ((columns[k] == 0) && (nodes.colCount[k] == 0)) {
        do {
          nodes.uncover(nodes.colCount[j]);
          j = nodes.left[j];
        } while (j != r);

        return false;
      }
    }
  }

  return true;
}

void DlxMatrix::removeRow(unsigned int r) {
  unsigned int j = r;
  do {
    nodes.colCount[nodes.colCount[j]]--;

    const unsigned int u = nodes.up(j);
    const unsigned int d = nodes.down(j);

    nodes.up(d) = u;
    nodes.down(u) = d;

    j = nodes.right[j];
  } while (j != r);
}

void DlxMatrix::removeColumn(unsigned int c) {
  unsigned int j = c;
  do {
    nodes.right[nodes.left[j]] = nodes.right[j];
    nodes.left[nodes.right[j]] = nodes.left[j];

    j = nodes.down(j);
  } while (j != c);
}

// True when the position is a dead end: more variable voxels can no longer
// be filled than there may be holes.
bool DlxMatrix::checkmatrix() {
  auto count = asUnsigned(holes);
  for (unsigned int j = nodes.right[varivoxelEnd]; j != varivoxelEnd; j = nodes.right[j]) {
    if (nodes.colCount[j] == 0) {
      if (!count) {
        return true;
      }
      count--;
    }
  }

  return false;
}

std::uint64_t DlxMatrix::rowsFrom(unsigned int from) const {
  std::uint64_t rows = 0;
  for (unsigned int p = from; p < pieces; p++) {
    rows += nodes.colCount[p + 1];
  }
  return rows;
}

ReduceStats DlxMatrix::reduce(const StopToken& stop, PrepareCounters& progress) {
  // per-column scratch space
  std::vector<unsigned int> columns(varivoxelEnd);
  unsigned int removed = 0;
  unsigned int remCol = 0;
  bool remSth = false;

  // the rows to remove are collected first and removed after the check
  std::vector<unsigned int> rowsToRemove;

  // what a progress display shows: the placements left and removed
  const std::uint64_t startRows = rowsFrom(0);
  const auto told = [&] {
    progress.removed.store(removed, std::memory_order_relaxed);
    progress.placements.store(startRows - removed, std::memory_order_relaxed);
  };

  progress.piece.store(0, std::memory_order_relaxed);
  told();

  // With no hole to spare every variable voxel is filled as well, exactly
  // once, so the sweep holds for those columns too. On a tray of variable
  // cells that the pieces fill, it takes the placements that would leave a
  // gap no piece fits in before the check below has to try each of them
  // against the whole matrix (minutes a piece on a 65x65 tray).
  const bool variFilled = holes == 0 && varivoxelStart < varivoxelEnd;

  // the rows of the column being swept
  std::vector<unsigned char> inColumn(placements.size());
  std::vector<std::size_t> marked;

  // Sweeps `col`, a column that is filled exactly once: a voxel each of its
  // rows fills is filled by one of them, so the other rows there can go,
  // variable voxels too. Each of those rows fails the check below as well
  // (it leaves `col` without a row), and that check runs until nothing more
  // goes, so the matrix reduce() ends with does not depend on what is swept.
  const auto sweepColumn = [&](unsigned int col) {
    std::fill(columns.begin(), columns.end(), 0U);

    // columns[c] == count: c is in each of the `count` rows looked at so
    // far; `common` says how many such columns there are.
    unsigned int count = 0;
    unsigned int common = 0;
    const auto look = [&](unsigned int r) {
      const std::size_t row = rowIndexOf(r);
      inColumn[row] = 1;
      marked.push_back(row);

      common = 0;
      for (unsigned int j = nodes.right[r]; j != r; j = nodes.right[j]) {
        unsigned int& in = columns[nodes.colCount[j]];
        if (in == count) {
          in = count + 1;
          common++;
        }
      }
      count++;
    };

    // The first and the last row first: away from the walls their
    // placements share this voxel alone, which ends the column at once.
    const unsigned int first = nodes.down(col);
    const unsigned int last = nodes.up(col);
    if (first != col) {
      look(first);
      if (last != first) {
        look(last);
        for (unsigned int r = nodes.down(first); r != last && common != 0; r = nodes.down(r)) {
          look(r);
        }
      }
    }

    // A voxel filled by this column's rows in every one of them: no row of
    // another column can fill it, so those rows can go. (A column with no
    // row at all takes every row with it, as in the original.)
    const auto dropOthers = [&](unsigned int c) {
      if (columns[c] != count) {
        return;
      }
      rowsToRemove.clear();

      for (unsigned int r = nodes.down(c); r != c; r = nodes.down(r)) {
        if (inColumn[rowIndexOf(r)] == 0) {
          rowsToRemove.push_back(r);
        }
      }

      for (const unsigned int rem : rowsToRemove) {
        removeRow(rem);
      }

      removed += static_cast<unsigned int>(rowsToRemove.size());
      told();
    };

    // with rows and nothing common to them, there is nothing to look for
    if (count == 0 || common != 0) {
      for (unsigned int c = nodes.right[0]; c; c = nodes.right[c]) {
        dropOthers(c);
      }
      for (unsigned int c = nodes.right[varivoxelEnd]; c != varivoxelEnd; c = nodes.right[c]) {
        dropOthers(c);
      }
    }

    for (const std::size_t row : marked) {
      inColumn[row] = 0;
    }
    marked.clear();
  };

  unsigned int sweep = 0;
  for (unsigned int col = nodes.right[0]; col; col = nodes.right[col]) {
    sweep++;
  }
  if (variFilled) {
    sweep += varivoxelEnd - varivoxelStart;
  }
  progress.beginPass(0, sweep);

  for (unsigned int col = nodes.right[0]; col; col = nodes.right[col]) {
    if (stop.stopRequested()) {
      break;
    }
    sweepColumn(col);
    progress.done.fetch_add(1, std::memory_order_relaxed);
  }
  if (variFilled) {
    for (unsigned int col = nodes.right[varivoxelEnd]; col != varivoxelEnd;
         col = nodes.right[col]) {
      if (stop.stopRequested()) {
        break;
      }
      sweepColumn(col);
      progress.done.fetch_add(1, std::memory_order_relaxed);
    }
  }

  remCol += clumpify();

  unsigned int pass = 0;
  do {
    remSth = false;
    if (stop.stopRequested()) {
      break;
    }
    progress.beginPass(++pass, rowsFrom(0));
    std::uint64_t checked = 0;

    for (unsigned int p = 0; p < pieces; p++) {
      if (stop.stopRequested()) {
        break;
      }

      progress.piece.store(p, std::memory_order_relaxed);
      // the rows of the pieces before went while the pass checked them
      progress.total.store(checked + rowsFrom(p), std::memory_order_relaxed);

      // place the piece and see whether that leaves holes that cannot be
      // filled, pieces that cannot be placed, or other dead ends
      nodes.cover(p + 1);

      rowsToRemove.clear();

      for (unsigned int r = nodes.down(p + 1); r != p + 1; r = nodes.down(r)) {
        // On a large board one piece's rows take minutes: a stop ends the
        // piece here. The rows found dead so far are dead in any case.
        if (stop.stopRequested()) {
          break;
        }

        // a placement that already fails to cover needs no deeper check
        if (!tryCoverRow(r, columns.data())) {
          rowsToRemove.push_back(r);
        } else {
          if (checkmatrix()) {
            rowsToRemove.push_back(r);
          }

          nodes.uncoverRow(r);
        }

        progress.done.store(++checked, std::memory_order_relaxed);
      }

      nodes.uncover(p + 1);

      for (const unsigned int rem : rowsToRemove) {
        removeRow(rem);
      }

      remSth |= !rowsToRemove.empty();
      removed += static_cast<unsigned int>(rowsToRemove.size());
      told();

      // the voxels this piece fills in all its placements: the other pieces'
      // placements there can go
      std::fill(columns.begin(), columns.end(), 0U);
      unsigned int count = 0;
      for (unsigned int r = nodes.down(p + 1); r != p + 1; r = nodes.down(r)) {
        for (unsigned int j = nodes.right[r]; j != r; j = nodes.right[j]) {
          columns[nodes.colCount[j]]++;
        }
        count++;
      }

      for (unsigned int c = nodes.right[0]; c; c = nodes.right[c]) {
        if (columns[c] == count) {
          rowsToRemove.clear();

          std::size_t i = 0;
          for (unsigned int r = nodes.down(c); r != c; r = nodes.down(r)) {
            // the piece of this row
            while ((i + 1) < placements.size() && placements[i + 1].row <= r) {
              i++;
            }

            if (placements[i].piece != p) {
              rowsToRemove.push_back(r);
            }
          }

          for (const unsigned int rem : rowsToRemove) {
            removeRow(rem);
          }

          remSth |= !rowsToRemove.empty();
          removed += static_cast<unsigned int>(rowsToRemove.size());
          told();
        }
      }
    }
  } while (remSth);

  remCol += clumpify();

  return ReduceStats{removed, remCol};
}

Assembly DlxMatrix::assemblyFromRows(const GridType& gt, const unsigned int* rows,
                                     unsigned int count) const {
  Assembly assembly(gt);

  // the order the pieces are in
  std::vector<unsigned int> order(pieces, 0xFFFFFFFF);
  std::vector<unsigned char> trans(pieces);
  std::vector<int> xs(pieces);
  std::vector<int> ys(pieces);
  std::vector<int> zs(pieces);

  for (unsigned int i = 0; i < count; i++) {
    const DlxPlacement& pp = placementOf(rows[i]);

    order[pp.piece] = i;
    trans[pp.piece] = pp.transformation;
    xs[pp.piece] = pp.x;
    ys[pp.piece] = pp.y;
    zs[pp.piece] = pp.z;
  }

  for (unsigned int i = 0; i < pieces; i++) {
    if (order[i] >= count) {
      assembly.addNonPlacement();
    } else {
      assembly.addPlacement(trans[i], xs[i], ys[i], zs[i]);
    }
  }

  // no sort needed: there is only one of each piece
  return assembly;
}

Assembly DlxMatrix::emptyAssembly(const GridType& gt) const {
  Assembly assembly(gt);
  for (unsigned int i = 0; i < pieces; i++) {
    assembly.addNonPlacement();
  }
  return assembly;
}

unsigned int DlxMatrix::piecePlacement(unsigned int node, int delta, unsigned int piece,
                                       unsigned char* tran, int* x, int* y, int* z) const {
  if (!node) {
    node = nodes.down(piece + 1);
  }

  while (delta > 0) {
    node = nodes.down(node);
    delta--;
  }

  while (delta < 0) {
    node = nodes.up(node);
    delta++;
  }

  const DlxPlacement& pp = placementOf(node);
  *tran = pp.transformation;
  *x = pp.x;
  *y = pp.y;
  *z = pp.z;

  SL_ASSERT(pp.piece == piece);

  return node;
}

}  // namespace bt

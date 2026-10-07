// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/assembler_0.cpp (generateFirstRow, AddPieceNode,
// AddVoxelNode, getPieceInformation, prepare, the unit checks of
// createMatrix, clumpify, reduce and the row and column operations it
// uses), which src/lib/assembler_bt2.cpp had a copy of.
#include "bt/solve/dlx_matrix.h"

#include "bt/base/int_cast.h"
#include "bt/grid/symmetries.h"
#include "bt/grid/voxel.h"
#include "bt/model/problem.h"
#include "bt/model/puzzle.h"
#include "bt/solve/placement_finder.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <unordered_map>

namespace bt {

namespace {

/// Adds `piece` to the cache unless an identical shape is there. Returns it,
/// or null when it was a duplicate (and is dropped).
const Voxel* addToCache(std::vector<std::unique_ptr<Voxel>>& cache,
                        std::unique_ptr<Voxel> piece) {
  for (const auto& c : cache) {
    if (c->identicalInBB(*piece)) {
      return nullptr;
    }
  }
  cache.push_back(std::move(piece));
  return cache.back().get();
}

/// A sliding tray need not be filled: its normal cells are optional, like
/// variable ones (sliding::isSliding in the original).
bool isSliding(const Puzzle& puzzle) { return puzzle.getGridType().getType() == GridKind::Sliding; }

}  // namespace

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

const DlxPlacement& DlxMatrix::placementOf(unsigned int node) const {
  // placements is in row order (addPieceNode appends as the nodes are made),
  // so the last entry at or before the node is found by halving
  std::size_t lo = 0;
  std::size_t hi = placements.size();
  while (lo < hi) {
    const std::size_t mid = lo + (hi - lo) / 2;
    if (placements[mid].row <= node) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }

  BT_ASSERT_MSG(lo != 0, "getPieceInformation: no piece position for the node");

  return placements[lo - 1];
}

// Finds identical columns (contained in exactly the same rows) and removes
// all but one of each group. This saves no iterations but makes them cheaper.
// Columns are grouped by a hash of their row signature and only compared
// exactly within a group, which is linear in the number of nodes.
unsigned int DlxMatrix::clumpify() {
  // the columns in header ring order; the first of each group survives
  std::vector<unsigned int> cols;
  for (unsigned int c = nodes.right[0]; c; c = nodes.right[c]) {
    cols.push_back(c);
  }

  // all signatures back to back, with the start of each
  std::vector<unsigned long> sigData;
  std::vector<unsigned long> sigStart;
  std::vector<unsigned long long> sigHash;

  sigStart.reserve(cols.size() + 1);
  sigHash.reserve(cols.size());

  for (const unsigned int col : cols) {
    sigStart.push_back(sigData.size());

    unsigned long long h = 14695981039346656037ULL;
    std::size_t line = 0;

    for (unsigned int row = nodes.down(col); row != col; row = nodes.down(row)) {
      // the placement row of this node; nodes come in increasing order so
      // the line only ever advances
      while ((line + 1 < placements.size()) && (placements[line + 1].row <= row)) {
        line++;
      }

      sigData.push_back(line);

      h = (h ^ line) * 1099511628211ULL;
    }

    sigHash.push_back(h);
  }

  sigStart.push_back(sigData.size());

  std::unordered_map<unsigned long long, std::vector<std::size_t>> groups;

  unsigned int removed = 0;

  for (std::size_t i = 0; i < cols.size(); i++) {
    std::vector<std::size_t>& group = groups[sigHash[i]];

    bool dup = false;

    for (const std::size_t j : group) {
      const unsigned long leni = sigStart[i + 1] - sigStart[i];

      if (leni != sigStart[j + 1] - sigStart[j]) {
        continue;
      }

      if ((leni == 0) || (std::memcmp(&sigData[sigStart[i]], &sigData[sigStart[j]],
                                      leni * sizeof(unsigned long)) == 0)) {
        dup = true;
        break;
      }
    }

    if (dup) {
      removeColumn(cols[i]);
      removed++;
    } else {
      group.push_back(i);
    }
  }

  return removed;
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
// Returns 1, or minus the shape id of a part that has no placement.
int DlxMatrix::prepare(const Puzzle& puzzle, const Problem& problem, bool strictColors,
                       RotationFilter& filter, std::atomic<unsigned int>& piece) {
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
    const bool optionalFloor = isSliding(puzzle);

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

  // with only the identity in the result, everything must be tried
  if (!unSymmetric(resultSym)) {
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
      // The mirror check needs the mirror structure. Which case is this?
      // 1) all pieces have mirror symmetry: check mirrors, no pairs
      // 2) a piece has none:
      //   2a) each such piece has a mirror partner: check mirrors, with pairs
      //   2b) one has no partner: no mirror check
      struct Mm {
        unsigned int shape;   // the shape of this piece
        unsigned int mirror;  // the mirror shape of this piece
        unsigned int trans;
      };

      std::vector<Mm> mirror(problem.getNumberOfPieces());

      for (unsigned int i = 0; i < problem.getNumberOfParts(); i++) {
        mirror[i].shape = i;
        mirror[i].mirror = static_cast<unsigned int>(-1);
        mirror[i].trans = 255;
      }

      bool mirrorCheck = true;

      for (unsigned int i = 0; i < problem.getNumberOfPieces(); i++) {
        // the mirror of this one was found already
        if (mirror[i].mirror < problem.getNumberOfPieces()) {
          continue;
        }

        if (!sym.symmetryContainsMirror(
                problem.partShape(puzzle, mirror[i].shape).selfSymmetries())) {
          // not self-mirroring: look for a shape that is its mirror
          bool found = false;

          for (unsigned int j = i + 1; j < problem.getNumberOfPieces(); j++) {
            if (mirror[j].mirror < problem.getNumberOfPieces()) {
              continue;
            }

            const unsigned int trans =
                problem.partShape(puzzle, mirror[i].shape)
                    .getMirrorTransform(problem.partShape(puzzle, mirror[j].shape));

            if (trans > 0) {
              mirror[i].mirror = j;
              mirror[i].trans = trans;
              mirror[j].mirror = i;
              mirror[j].trans = problem.partShape(puzzle, mirror[j].shape)
                                    .getMirrorTransform(problem.partShape(puzzle, mirror[i].shape));

              found = true;
              break;
            }
          }

          // a piece without mirror symmetry and without a partner: no
          // mirror check
          if (!found) {
            mirrorCheck = false;
            break;
          }
        }
      }

      if (mirrorCheck) {
        auto mir = std::make_unique<MirrorInfo>();

        for (unsigned int i = 0; i < problem.getNumberOfPieces(); i++) {
          if (mirror[i].trans != 255) {
            mir->addPieces(i, mirror[i].mirror, static_cast<unsigned char>(mirror[i].trans));
          }
        }

        filter.request(symBreakerShape, std::move(mir));
      }
    }
  }

  // Node 0 is the start of everything, nodes 1..n are the column headers.
  // The variable voxels' columns are left out of the header ring so that
  // the search never chooses them to fill.
  generateFirstRow();

  std::vector<std::unique_ptr<Voxel>> cache;
  cache.reserve(sym.getNumTransformationsMirror());

  const PlacementFinder finder(puzzle, problem, result, strictColors);
  std::vector<long> voxelOffsets;
  std::vector<int> positions;

  const long rsx = result.getX();
  const long rsy = result.getY();

  for (unsigned int pc = 0; pc < problem.getNumberOfParts(); pc++) {
    piece.store(pc, std::memory_order_relaxed);

    // the orientations found so far, so that no orientation goes in twice
    cache.clear();
    unsigned int found = 0;

    for (unsigned int rot = 0; rot < sym.getNumTransformations(); rot++) {
      std::unique_ptr<Voxel> rotated = problem.partShape(puzzle, pc).clone();
      if (!rotated->transform(rot)) {
        continue;
      }

      const Voxel* rotation = addToCache(cache, std::move(rotated));

      if (rotation != nullptr) {
        finder.find(*rotation, voxelOffsets, positions);

        for (std::size_t p = 0; p < positions.size(); p += 3) {
          const int x = positions[p];
          const int y = positions[p + 1];
          const int z = positions[p + 2];

          const unsigned int piecenode = addPieceNode(
              pc, rot, x + rotation->getHx(), y + rotation->getHy(), z + rotation->getHz());
          found = 1;

          // the voxels the piece fills
          const long base = x + rsx * (y + rsy * static_cast<long>(z));
          for (const long off : voxelOffsets) {
            addVoxelNode(columns[static_cast<std::size_t>(base + off)], piecenode);
          }
        }

        // for the symmetry breaker also every symmetry of the result
        if (pc == symBreakerShape) {
          for (unsigned int r = 1; r < sym.getNumTransformations(); r++) {
            if (sym.symmetrieContainsTransformation(resultSym, r)) {
              std::unique_ptr<Voxel> vx = problem.partShape(puzzle, pc).clone();

              if (!vx->transform(rot) || !vx->transform(r)) {
                continue;
              }

              (void)addToCache(cache, std::move(vx));
            }
          }
        }
      }
    }

    // a piece with no placement at all
    if (found == 0) {
      return -asInt(problem.getShapeIdOfPart(pc));
    }
  }

  return 1;
}

AssemblerError DlxMatrix::build(const Puzzle& puzzle, const Problem& problem, bool strictColors,
                                RotationFilter& filter, std::atomic<unsigned int>& piece,
                                int& param) {
  pieces = problem.getNumberOfPieces();

  // the filled and variable units
  int resVari = asInt(problem.resultShape(puzzle).countState(Voxel::kVariable));
  const int resFilled = asInt(problem.resultShape(puzzle).countState(Voxel::kFilled)) + resVari;

  // Sliding lets the pieces leave normal cells empty: those cells are
  // optional, so falling short of the tray is no error.
  if (isSliding(puzzle)) {
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

  const int error = prepare(puzzle, problem, strictColors, filter, piece);

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

ReduceStats DlxMatrix::reduce(const StopToken& stop, std::atomic<unsigned int>& piece) {
  // per-column scratch space
  std::vector<unsigned int> columns(varivoxelEnd);
  unsigned int removed = 0;
  unsigned int remCol = 0;
  bool remSth = false;

  // the rows to remove are collected first and removed after the check
  std::vector<unsigned int> rowsToRemove;

  piece.store(0, std::memory_order_relaxed);

  for (unsigned int col = nodes.right[0]; col; col = nodes.right[col]) {
    if (stop.stopRequested()) {
      break;
    }

    std::fill(columns.begin(), columns.end(), 0U);

    unsigned int count = 0;
    for (unsigned int r = nodes.down(col); r != col; r = nodes.down(r)) {
      for (unsigned int j = nodes.right[r]; j != r; j = nodes.right[j]) {
        columns[nodes.colCount[j]]++;
      }
      count++;
    }

    // A voxel filled by this column's rows in every one of them: no row of
    // another column can fill it, so those rows can go.
    for (unsigned int c = nodes.right[0]; c; c = nodes.right[c]) {
      if (columns[c] == count) {
        rowsToRemove.clear();

        for (unsigned int r = nodes.down(c); r != c; r = nodes.down(r)) {
          // find the column col in the row
          unsigned int c2 = nodes.right[r];
          while ((nodes.colCount[c2] != col) && (c2 != r)) {
            c2 = nodes.right[c2];
          }

          if (c2 == r) {
            rowsToRemove.push_back(r);
          }
        }

        for (const unsigned int rem : rowsToRemove) {
          removeRow(rem);
        }

        removed += static_cast<unsigned int>(rowsToRemove.size());
      }
    }
  }

  remCol += clumpify();

  do {
    remSth = false;

    for (unsigned int p = 0; p < pieces; p++) {
      if (stop.stopRequested()) {
        break;
      }

      piece.store(p, std::memory_order_relaxed);

      // place the piece and see whether that leaves holes that cannot be
      // filled, pieces that cannot be placed, or other dead ends
      nodes.cover(p + 1);

      rowsToRemove.clear();

      for (unsigned int r = nodes.down(p + 1); r != p + 1; r = nodes.down(r)) {
        // a placement that already fails to cover needs no deeper check
        if (!tryCoverRow(r, columns.data())) {
          rowsToRemove.push_back(r);
        } else {
          if (checkmatrix()) {
            rowsToRemove.push_back(r);
          }

          nodes.uncoverRow(r);
        }
      }

      nodes.uncover(p + 1);

      for (const unsigned int rem : rowsToRemove) {
        removeRow(rem);
      }

      remSth |= !rowsToRemove.empty();
      removed += static_cast<unsigned int>(rowsToRemove.size());

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

  BT_ASSERT(pp.piece == piece);

  return node;
}

}  // namespace bt

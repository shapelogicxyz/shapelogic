// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/assembler_0.cpp. The search code is the original's;
// what changed is around it: the stop is the run's token, the threads come
// from the pool through runTasks(), the split and vector state is a
// SplitSearchState, and the saved position is a string (decision 0009).
#include "bt/solve/assembler_0.h"

#include "bt/base/int_cast.h"
#include "bt/grid/symmetries.h"
#include "bt/grid/voxel.h"
#include "bt/model/problem.h"
#include "bt/model/puzzle.h"
#include "bt/solve/placement_finder.h"
#include "bt/solve/simd_exact_cover.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
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

/// strtol from `s`, cast to unsigned int; returns the characters consumed
/// (the original's getInt, quirks included: no digits read 0).
unsigned int getInt(const char* s, unsigned int* i) {
  char* s2 = nullptr;
  *i = static_cast<unsigned int>(std::strtol(s, &s2, 10));
  return static_cast<unsigned int>(s2 - s);
}

/// The same for the iteration count (the original's getLong).
unsigned int getLong(const char* s, std::uint64_t* i) {
  char* s2 = nullptr;
  *i = static_cast<std::uint64_t>(std::strtol(s, &s2, 10));
  return static_cast<unsigned int>(s2 - s);
}

}  // namespace

// ---- the matrix ------------------------------------------------------------------

void Assembler0::generateFirstRow() {
  for (unsigned int i = 0; i < varivoxelStart_; i++) {
    right_.push_back(i + 1);
    left_.push_back(i - 1);
    upDown_.push_back(i);
    upDown_.push_back(i);
    colCount_.push_back(0);
  }

  // make the list cyclic
  left_[0] = varivoxelStart_ - 1;
  right_[varivoxelStart_ - 1] = 0;

  // Column nodes for the variable voxels. They are not in the header list,
  // so they need not be filled, but otherwise behave like normal columns.
  for (unsigned int j = varivoxelStart_; j <= varivoxelEnd_; j++) {
    left_.push_back(j - 1);
    right_.push_back(j + 1);
    upDown_.push_back(j);
    upDown_.push_back(j);
    colCount_.push_back(0);
  }
  left_[varivoxelStart_] = varivoxelEnd_;
  right_[varivoxelEnd_] = varivoxelStart_;
}

unsigned int Assembler0::addPieceNode(unsigned int piece, unsigned int rot, int x, int y, int z) {
  const auto piecenode = static_cast<unsigned int>(left_.size());

  left_.push_back(piecenode);
  right_.push_back(piecenode);
  upDown_.push_back(up(piece + 1));
  upDown_.push_back(piece + 1);

  down(up(piece + 1)) = piecenode;
  up(piece + 1) = piecenode;

  colCount_.push_back(piece + 1);
  colCount_[piece + 1]++;

  piecePositions_.push_back(
      PiecePosition{x, y, z, static_cast<unsigned char>(rot), piecenode, piece});

  return piecenode;
}

void Assembler0::getPieceInformation(unsigned int node, unsigned char* tran, int* x, int* y,
                                     int* z, unsigned int* piece) const {
  // piecePositions_ is in row order (addPieceNode appends as the nodes are
  // made), so the last entry at or before the node is found by halving.
  std::size_t lo = 0;
  std::size_t hi = piecePositions_.size();
  while (lo < hi) {
    const std::size_t mid = lo + (hi - lo) / 2;
    if (piecePositions_[mid].row <= node) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }

  BT_ASSERT_MSG(lo != 0, "getPieceInformation: no piece position for the node");

  const PiecePosition& pp = piecePositions_[lo - 1];
  *tran = pp.transformation;
  *x = pp.x;
  *y = pp.y;
  *z = pp.z;
  *piece = pp.piece;
}

// Finds identical columns (contained in exactly the same rows) and removes
// all but one of each group. This saves no iterations but makes them cheaper.
// Columns are grouped by a hash of their row signature and only compared
// exactly within a group, which is linear in the number of nodes.
unsigned int Assembler0::clumpify() {
  // the columns in header ring order; the first of each group survives
  std::vector<unsigned int> cols;
  for (unsigned int c = right_[0]; c; c = right_[c]) {
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

    for (unsigned int row = down(col); row != col; row = down(row)) {
      // the placement row of this node; nodes come in increasing order so
      // the line only ever advances
      while ((line + 1 < piecePositions_.size()) && (piecePositions_[line + 1].row <= row)) {
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

void Assembler0::addVoxelNode(unsigned int col, unsigned int piecenode) {
  const auto newnode = static_cast<unsigned int>(left_.size());

  right_.push_back(piecenode);
  left_.push_back(left_[piecenode]);
  right_[left_[piecenode]] = newnode;
  left_[piecenode] = newnode;

  upDown_.push_back(up(col));
  upDown_.push_back(col);
  down(up(col)) = newnode;
  up(col) = newnode;

  colCount_.push_back(col);
  colCount_[col]++;
}

Assembler0::Assembler0(const Puzzle& puzzle, const Problem& problem)
    : puzzle_(puzzle), problem_(problem) {}

Assembler0::~Assembler0() = default;

// Builds the matrix. Additions to Knuth's algorithm: variable voxels (empty
// spaces in the result) are columns that are not in the header list, so the
// search never has to fill them but they can be used once at most.
//
// Returns 1, or minus the shape id of a part that has no placement.
int Assembler0::prepare() {
  const Voxel& result = problem_.resultShape(puzzle_);

  // the matrix column of every result voxel (a lookup, because computing it
  // from the counts of filled and variable voxels is slow)
  std::vector<unsigned int> columns(result.getXYZ());
  const unsigned int piecenumber = problem_.getNumberOfPieces();

  {
    unsigned int v = 0;
    unsigned int c = 0;

    // A sliding tray does not have to be filled: normal cells are optional
    // columns as well, so that empty cells can be the space pieces slide in.
    const bool optionalFloor = isSliding(puzzle_);

    for (unsigned int i = 0; i < result.getXYZ(); i++) {
      switch (result.getState(i)) {
        case Voxel::kVariable:
          columns[i] = varivoxelStart_ + v++;
          break;
        case Voxel::kFilled:
          if (optionalFloor) {
            columns[i] = varivoxelStart_ + v++;
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
  const GridType& gt = puzzle_.getGridType();
  const Symmetries& sym = gt.getSymmetries();
  unsigned int symBreakerShape = 0xFFFFFFFF;

  // with only the identity in the result, everything must be tried
  if (!unSymmetric(resultSym)) {
    // FIXME (original): with several suitable pieces, the one with the most
    // placements would gain more
    unsigned int bestFound =
        sym.countSymmetryIntersection(resultSym, problem_.partShape(puzzle_, 0).selfSymmetries());
    symBreakerShape = 0;

    for (unsigned int i = 1; i < problem_.getNumberOfParts(); i++) {
      const unsigned int cnt = sym.countSymmetryIntersection(
          resultSym, problem_.partShape(puzzle_, i).selfSymmetries());

      if (cnt < bestFound) {
        bestFound = cnt;
        symBreakerShape = i;
      }
    }

    checkForTransformedAssemblies(symBreakerShape, nullptr);

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

      std::vector<Mm> mirror(problem_.getNumberOfPieces());

      for (unsigned int i = 0; i < problem_.getNumberOfParts(); i++) {
        mirror[i].shape = i;
        mirror[i].mirror = static_cast<unsigned int>(-1);
        mirror[i].trans = 255;
      }

      bool mirrorCheck = true;

      for (unsigned int i = 0; i < problem_.getNumberOfPieces(); i++) {
        // the mirror of this one was found already
        if (mirror[i].mirror < problem_.getNumberOfPieces()) {
          continue;
        }

        if (!sym.symmetryContainsMirror(
                problem_.partShape(puzzle_, mirror[i].shape).selfSymmetries())) {
          // not self-mirroring: look for a shape that is its mirror
          bool found = false;

          for (unsigned int j = i + 1; j < problem_.getNumberOfPieces(); j++) {
            if (mirror[j].mirror < problem_.getNumberOfPieces()) {
              continue;
            }

            const unsigned int trans =
                problem_.partShape(puzzle_, mirror[i].shape)
                    .getMirrorTransform(problem_.partShape(puzzle_, mirror[j].shape));

            if (trans > 0) {
              mirror[i].mirror = j;
              mirror[i].trans = trans;
              mirror[j].mirror = i;
              mirror[j].trans = problem_.partShape(puzzle_, mirror[j].shape)
                                    .getMirrorTransform(problem_.partShape(puzzle_, mirror[i].shape));

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

        for (unsigned int i = 0; i < problem_.getNumberOfPieces(); i++) {
          if (mirror[i].trans != 255) {
            mir->addPieces(i, mirror[i].mirror, static_cast<unsigned char>(mirror[i].trans));
          }
        }

        checkForTransformedAssemblies(symBreakerShape, std::move(mir));
      }
    }
  }

  // Node 0 is the start of everything, nodes 1..n are the column headers.
  // The variable voxels' columns are left out of the header ring so that
  // the search never chooses them to fill.
  generateFirstRow();

  std::vector<std::unique_ptr<Voxel>> cache;
  cache.reserve(sym.getNumTransformationsMirror());

  const PlacementFinder finder(puzzle_, problem_, result, strictColors_);
  std::vector<long> voxelOffsets;
  std::vector<int> positions;

  const long rsx = result.getX();
  const long rsy = result.getY();

  for (unsigned int pc = 0; pc < problem_.getNumberOfParts(); pc++) {
    reducePiece_.store(pc, std::memory_order_relaxed);

    // the orientations found so far, so that no orientation goes in twice
    cache.clear();
    unsigned int placements = 0;

    for (unsigned int rot = 0; rot < sym.getNumTransformations(); rot++) {
      std::unique_ptr<Voxel> rotated = problem_.partShape(puzzle_, pc).clone();
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
          placements = 1;

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
              std::unique_ptr<Voxel> vx = problem_.partShape(puzzle_, pc).clone();

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
    if (placements == 0) {
      return -asInt(problem_.getShapeIdOfPart(pc));
    }
  }

  return 1;
}

AssemblerError Assembler0::createMatrix(const MatrixOptions& options) {
  BT_ASSERT(problem_.resultValid());

  complete_ = options.completeRotations;
  strictColors_ = options.strictColors;

  if (!canHandle(problem_)) {
    return AssemblerError::PuzzleUnhandable;
  }

  piecenumber_ = problem_.getNumberOfPieces();

  // the filled and variable units
  int resVari = asInt(problem_.resultShape(puzzle_).countState(Voxel::kVariable));
  const int resFilled = asInt(problem_.resultShape(puzzle_).countState(Voxel::kFilled)) + resVari;

  // Sliding lets the pieces leave normal cells empty: those cells are
  // optional, so falling short of the tray is no error.
  if (isSliding(puzzle_)) {
    resVari = resFilled;
  }

  varivoxelStart_ = 1 + piecenumber_ + asUnsigned(resFilled - resVari);
  varivoxelEnd_ = 1 + piecenumber_ + asUnsigned(resFilled);

  // the pieces may not have more units than the result has voxels, and the
  // filled voxels of the result must be fillable by them
  int h = resFilled;

  for (unsigned int j = 0; j < problem_.getNumberOfParts(); j++) {
    h -= asInt(problem_.partShape(puzzle_, j).countState(Voxel::kFilled));
  }

  if (h < 0) {
    errorsState_ = AssemblerError::TooManyUnits;
    errorsParam_ = -h;
    return errorsState_;
  }

  if (h > resVari) {
    errorsState_ = AssemblerError::TooFewUnits;
    errorsParam_ = h - resVari;
    return errorsState_;
  }

  holes_ = h;

  // One more entry than pieces: a search stopped just after placing the last
  // piece stands at pos == piecenumber, and the position written then
  // includes that entry (the original read past its arrays there).
  rows_.assign(piecenumber_ + 1, 0);
  columns_.assign(piecenumber_ + 1, 0);

  const int error = prepare();

  // a piece that cannot be placed
  if (error <= 0) {
    errorsState_ = AssemblerError::CannotPlace;
    errorsParam_ = -error;
    return errorsState_;
  }
  pos_ = 0;
  iterations_.store(0, std::memory_order_relaxed);

  if (options.keepMirrors) {
    avoidTransformedMirror_.reset();
  }

  if (options.keepRotations) {
    avoidTransformedAssemblies_ = false;
  }

  errorsState_ = AssemblerError::None;
  publishSerialProgress();
  return errorsState_;
}

void Assembler0::applySolutionFilterFlags(bool keepMirrors, bool keepRotations, bool complete) {
  complete_ = complete;

  if (keepMirrors) {
    avoidTransformedMirror_.reset();
  }

  if (keepRotations) {
    avoidTransformedAssemblies_ = false;
  } else if (rotationFilterActive_) {
    avoidTransformedAssemblies_ = true;
  }
}

// Removes the column from the header list, and every row with a 1 in it.
void Assembler0::cover(unsigned int col) {
  {
    const unsigned int l = left_[col];
    const unsigned int r = right_[col];

    left_[r] = l;
    right_[l] = r;
  }

  for (unsigned int i = down(col); i != col; i = down(i)) {
    for (unsigned int j = right_[i]; j != i; j = right_[j]) {
      const unsigned int u = up(j);
      const unsigned int d = down(j);

      up(d) = u;
      down(u) = d;

      colCount_[colCount_[j]]--;
    }
  }
}

// The exact inverse of cover(): the matrix must be as cover() left it.
void Assembler0::uncover(unsigned int col) {
  for (unsigned int i = up(col); i != col; i = up(i)) {
    for (unsigned int j = left_[i]; j != i; j = left_[j]) {
      colCount_[colCount_[j]]++;

      up(down(j)) = j;
      down(up(j)) = j;
    }
  }

  left_[right_[col]] = col;
  right_[left_[col]] = col;
}

// Covers every column in which row r has a 1.
void Assembler0::coverRow(unsigned int r) {
  for (unsigned int j = right_[r]; j != r; j = right_[j]) {
    cover(colCount_[j]);
  }
}

// coverRow(), but gives up (undoing what it did) as soon as a column outside
// the row is left with no rows.
bool Assembler0::tryCoverRow(unsigned int r, unsigned int* columns) {
  std::memset(columns, 0, varivoxelEnd_ * sizeof(unsigned int));

  for (unsigned int j = right_[r]; j != r; j = right_[j]) {
    columns[colCount_[j]] = 1;
  }

  for (unsigned int j = right_[r]; j != r; j = right_[j]) {
    cover(colCount_[j]);

    for (unsigned int k = right_[0]; k; k = right_[k]) {
      if ((columns[k] == 0) && (colCount_[k] == 0)) {
        do {
          uncover(colCount_[j]);
          j = left_[j];
        } while (j != r);

        return false;
      }
    }
  }

  return true;
}

void Assembler0::uncoverRow(unsigned int r) {
  for (unsigned int j = left_[r]; j != r; j = left_[j]) {
    uncover(colCount_[j]);
  }
}

void Assembler0::removeRow(unsigned int r) {
  unsigned int j = r;
  do {
    colCount_[colCount_[j]]--;

    const unsigned int u = up(j);
    const unsigned int d = down(j);

    up(d) = u;
    down(u) = d;

    j = right_[j];
  } while (j != r);
}

void Assembler0::removeColumn(unsigned int c) {
  unsigned int j = c;
  do {
    right_[left_[j]] = right_[j];
    left_[right_[j]] = left_[j];

    j = down(j);
  } while (j != c);
}

// True when the position is a dead end: more variable voxels can no longer
// be filled than there may be holes.
bool Assembler0::checkmatrix() {
  auto count = asUnsigned(holes_);
  for (unsigned int j = right_[varivoxelEnd_]; j != varivoxelEnd_; j = right_[j]) {
    if (colCount_[j] == 0) {
      if (!count) {
        return true;
      }
      count--;
    }
  }

  return false;
}

ReduceStats Assembler0::reduce(const StopToken& stop) {
  // per-column scratch space
  std::vector<unsigned int> columns(varivoxelEnd_);
  unsigned int removed = 0;
  unsigned int remCol = 0;
  bool remSth = false;

  // the rows to remove are collected first and removed after the check
  std::vector<unsigned int> rowsToRemove;

  reducePiece_.store(0, std::memory_order_relaxed);

  for (unsigned int col = right_[0]; col; col = right_[col]) {
    if (stop.stopRequested()) {
      break;
    }

    std::fill(columns.begin(), columns.end(), 0U);

    unsigned int placements = 0;
    for (unsigned int r = down(col); r != col; r = down(r)) {
      for (unsigned int j = right_[r]; j != r; j = right_[j]) {
        columns[colCount_[j]]++;
      }
      placements++;
    }

    // A voxel filled by this column's rows in every one of them: no row of
    // another column can fill it, so those rows can go.
    for (unsigned int c = right_[0]; c; c = right_[c]) {
      if (columns[c] == placements) {
        rowsToRemove.clear();

        for (unsigned int r = down(c); r != c; r = down(r)) {
          // find the column col in the row
          unsigned int c2 = right_[r];
          while ((colCount_[c2] != col) && (c2 != r)) {
            c2 = right_[c2];
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

    for (unsigned int p = 0; p < piecenumber_; p++) {
      if (stop.stopRequested()) {
        break;
      }

      reducePiece_.store(p, std::memory_order_relaxed);

      // place the piece and see whether that leaves holes that cannot be
      // filled, pieces that cannot be placed, or other dead ends
      cover(p + 1);

      rowsToRemove.clear();

      for (unsigned int r = down(p + 1); r != p + 1; r = down(r)) {
        // a placement that already fails to cover needs no deeper check
        if (!tryCoverRow(r, columns.data())) {
          rowsToRemove.push_back(r);
        } else {
          if (checkmatrix()) {
            rowsToRemove.push_back(r);
          }

          uncoverRow(r);
        }
      }

      uncover(p + 1);

      for (const unsigned int rem : rowsToRemove) {
        removeRow(rem);
      }

      remSth |= !rowsToRemove.empty();
      removed += static_cast<unsigned int>(rowsToRemove.size());

      // the voxels this piece fills in all its placements: the other pieces'
      // placements there can go
      std::fill(columns.begin(), columns.end(), 0U);
      unsigned int placements = 0;
      for (unsigned int r = down(p + 1); r != p + 1; r = down(r)) {
        for (unsigned int j = right_[r]; j != r; j = right_[j]) {
          columns[colCount_[j]]++;
        }
        placements++;
      }

      for (unsigned int c = right_[0]; c; c = right_[c]) {
        if (columns[c] == placements) {
          rowsToRemove.clear();

          std::size_t i = 0;
          for (unsigned int r = down(c); r != c; r = down(r)) {
            // the piece of this row
            while ((i + 1) < piecePositions_.size() && piecePositions_[i + 1].row <= r) {
              i++;
            }

            if (piecePositions_[i].piece != p) {
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

Assembly Assembler0::assemblyFromRows(const unsigned int* rows, unsigned int count) const {
  Assembly assembly(puzzle_.getGridType());

  // the order the pieces are in
  std::vector<unsigned int> pieces(piecenumber_, 0xFFFFFFFF);
  std::vector<unsigned char> trans(piecenumber_);
  std::vector<int> xs(piecenumber_);
  std::vector<int> ys(piecenumber_);
  std::vector<int> zs(piecenumber_);

  for (unsigned int i = 0; i < count; i++) {
    unsigned char tran = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    unsigned int piece = 0;

    getPieceInformation(rows[i], &tran, &x, &y, &z, &piece);

    pieces[piece] = i;
    trans[piece] = tran;
    xs[piece] = x;
    ys[piece] = y;
    zs[piece] = z;
  }

  for (unsigned int i = 0; i < piecenumber_; i++) {
    if (pieces[i] >= count) {
      assembly.addNonPlacement();
    } else {
      assembly.addPlacement(trans[i], xs[i], ys[i], zs[i]);
    }
  }

  // no sort needed: there is only one of each piece
  return assembly;
}

Assembly Assembler0::currentAssembly() const {
  // nothing placed, or the search is through: an empty assembly
  if (pos_ > piecenumber_) {
    Assembly assembly(puzzle_.getGridType());
    for (unsigned int i = 0; i < piecenumber_; i++) {
      assembly.addNonPlacement();
    }
    return assembly;
  }

  return assemblyFromRows(rows_.data(), pos_);
}

void Assembler0::checkForTransformedAssemblies(unsigned int pivot,
                                               std::unique_ptr<MirrorInfo> mir) {
  avoidTransformedAssemblies_ = true;
  rotationFilterActive_ = true;
  avoidTransformedPivot_ = pivot;
  avoidTransformedMirror_ = std::move(mir);
}

// An assembly found by the plain or the vector search on the caller's
// thread: dropped when a rotation of it is smaller, else handed over.
void Assembler0::solution(SearchRun& run) {
  if (run.hasCallback()) {
    Assembly assembly = currentAssembly();

    if (avoidTransformedAssemblies_ &&
        assembly.smallerRotationExists(puzzle_, problem_, avoidTransformedPivot_,
                                       avoidTransformedMirror_.get(), complete_, strictColors_)) {
      return;
    }

    run.deliver(std::move(assembly));
  }
}

float Assembler0::serialFraction() const {
  if (rows_.empty() || columns_.empty() || upDown_.empty()) {
    return 0;
  }

  if (pos_ > piecenumber_) {
    return 1;
  }

  float erg = 0;

  for (int i = asInt(pos_) - 1; i >= 0; i--) {
    const auto ui = static_cast<std::size_t>(i);
    unsigned int r = rows_[ui];
    unsigned int l = colCount_[columns_[ui]];

    while (l && r && (r != down(columns_[ui]))) {
      erg += 1;
      r = up(r);
      l--;
    }

    erg /= static_cast<float>(colCount_[columns_[ui]]);
  }

  return erg;
}

// The plain search: dancing links, iteratively. To follow it, know the
// dancing links algorithm first.
void Assembler0::iterativeMultiSearch(SearchRun& run) {
  bool cont = false;
  unsigned int sincePublished = 0;

  while (!run.stopRequested()) {
    // through when pos went below 0 (wrapping past piecenumber)
    if (pos_ > piecenumber_) {
      break;
    }

    // all pieces placed and all voxels filled?
    if (!right_[0]) {
      solution(run);
    }

    // the debugger
    if (debug_) {
      if (debugLoops_ <= 0) {
        break;
      }

      debugLoops_--;
    }

    // with all pieces placed there is nothing more to choose: the remaining
    // columns are empty anyway, so backtrack once
    if (pos_ == piecenumber_) {
      pos_--;
      // a problem without pieces is through here (the original read past
      // its row list)
      if (pos_ > piecenumber_) {
        break;
      }
    }

    cont = false;
    iterations_.store(iterations_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    if (++sincePublished == 4096) {
      sincePublished = 0;
      publishSerialProgress();
    }

    if (!rows_[pos_]) {
      // a new column: the one with the fewest rows among the piece and
      // filled-voxel columns; a column with none ends this branch
      unsigned int c = right_[0];
      unsigned int s = colCount_[c];

      if (s) {
        unsigned int j = right_[c];

        while (j) {
          if (colCount_[j] < s) {
            c = j;
            s = colCount_[c];

            if (!s) {
              break;
            }
          }

          j = right_[j];
        }
      }

      // more variable voxels that can no longer be filled than there may be
      // holes: backtrack as well
      if (s) {
        auto currentHoles = asUnsigned(holes_);
        unsigned int j = right_[varivoxelEnd_];

        while (j != varivoxelEnd_) {
          if (colCount_[j] == 0) {
            if (currentHoles == 0) {
              s = 0;
              break;
            }
            currentHoles--;
          }
          j = right_[j];
        }
      }

      if (s) {
        // a column to search
        columns_[pos_] = c;
        rows_[pos_] = down(columns_[pos_]);

        cont = rows_[pos_] != columns_[pos_];
      }

      // no row, or a column with none
      if (!cont) {
        rows_[pos_] = 0;
        pos_--;
        continue;
      }

      cover(columns_[pos_]);

    } else {
      // back from the deeper level: uncover our row and take the next
      uncoverRow(rows_[pos_]);
      cont = true;

      rows_[pos_] = down(rows_[pos_]);

      if (rows_[pos_] == columns_[pos_]) {
        cont = false;
      }
    }

    if (cont) {
      coverRow(rows_[pos_]);

      pos_++;

    } else {
      // this column is done: uncover it and backtrack
      uncover(columns_[pos_]);

      rows_[pos_] = 0;
      pos_--;
    }
  }

  publishSerialProgress();
}

// ---- the search split over threads -----------------------------------------------

// One thread's search of tasks: a copy of the matrix it covers and uncovers
// on its own (assemblerWorker_c).
class Assembler0::Worker {
 public:
  Worker(Assembler0& parent, SearchRun& run, unsigned int slot)
      : parent_(parent),
        run_(run),
        left_(parent.left_),
        right_(parent.right_),
        upDown_(parent.upDown_),
        colCount_(parent.colCount_),
        rows_(parent.piecenumber_, 0),
        columns_(parent.piecenumber_, 0),
        slot_(slot) {}

  /// Searches task `taskIndex` to its end or to a stop.
  void operator()(std::size_t taskIndex) {
    taskIndex_ = taskIndex;
    searchSubtree(parent_.split_.tasks[taskIndex]);
    flushIterations();
  }

 private:
  unsigned int& up(unsigned int x) { return upDown_[2 * std::size_t{x}]; }
  unsigned int& down(unsigned int x) { return upDown_[2 * std::size_t{x} + 1]; }

  // How far through the task: at each step below the prefix, the choices
  // done out of all of them, every choice taken as alike.
  [[nodiscard]] float taskFraction(unsigned int prefixDepth) {
    float erg = 0;
    for (int i = asInt(pos_) - 1; i >= asInt(prefixDepth); i--) {
      const auto ui = static_cast<std::size_t>(i);
      const unsigned int col = columns_[ui];
      const unsigned int n = colCount_[col];
      if (!n) {
        continue;
      }
      unsigned int r = rows_[ui];
      unsigned int l = n;
      while (l && r && (r != down(col))) {
        erg += 1;
        r = up(r);
        l--;
      }
      erg /= static_cast<float>(n);
    }
    return erg;
  }

  void cover(unsigned int col) {
    right_[left_[col]] = right_[col];
    left_[right_[col]] = left_[col];

    for (unsigned int i = down(col); i != col; i = down(i)) {
      for (unsigned int j = right_[i]; j != i; j = right_[j]) {
        const unsigned int u = up(j);
        const unsigned int d = down(j);

        up(d) = u;
        down(u) = d;

        colCount_[colCount_[j]]--;
      }
    }
  }

  void uncover(unsigned int col) {
    for (unsigned int i = up(col); i != col; i = up(i)) {
      for (unsigned int j = left_[i]; j != i; j = left_[j]) {
        colCount_[colCount_[j]]++;

        up(down(j)) = j;
        down(up(j)) = j;
      }
    }

    left_[right_[col]] = col;
    right_[left_[col]] = col;
  }

  void coverRow(unsigned int r) {
    for (unsigned int j = right_[r]; j != r; j = right_[j]) {
      cover(colCount_[j]);
    }
  }

  void uncoverRow(unsigned int r) {
    for (unsigned int j = left_[r]; j != r; j = left_[j]) {
      uncover(colCount_[j]);
    }
  }

  void solution() {
    flushIterations();
    parent_.reportParallelSolution(run_, rows_.data(), pos_);
  }

  void searchSubtree(const SubtreeTask& task) {
    const auto prefixDepth = static_cast<unsigned int>(task.prefix.size());

    // apply the prefix
    for (unsigned int d = 0; d < prefixDepth; d++) {
      cover(task.prefix[d].col);
      coverRow(task.prefix[d].row);
      columns_[d] = task.prefix[d].col;
      rows_[d] = task.prefix[d].row;
    }
    pos_ = prefixDepth;

    bool cont = false;

    while (!run_.stopRequested()) {
      if (pos_ < prefixDepth || pos_ > parent_.piecenumber_) {
        break;
      }

      if (!right_[0]) {
        solution();
      }

      if (pos_ == parent_.piecenumber_) {
        pos_--;
        if (pos_ < prefixDepth || pos_ > parent_.piecenumber_) {
          break;
        }
      }

      cont = false;
      localIterations_++;
      if ((localIterations_ - flushedIterations_) >= 128) {
        parent_.iterations_.fetch_add(localIterations_ - flushedIterations_,
                                      std::memory_order_relaxed);
        flushedIterations_ = localIterations_;
        if ((++flushCount_ & 31) == 0) {
          parent_.taskProgress_.report(slot_, taskIndex_, taskFraction(prefixDepth));
        }
      }

      if (!rows_[pos_]) {
        // the best column
        unsigned int c = right_[0];
        unsigned int s = colCount_[c];

        if (s) {
          unsigned int j = right_[c];
          while (j) {
            if (colCount_[j] < s) {
              c = j;
              s = colCount_[c];
              if (!s) {
                break;
              }
            }
            j = right_[j];
          }
        }

        // holes among the variable voxels
        if (s) {
          auto currentHoles = asUnsigned(parent_.holes_);
          unsigned int j = right_[parent_.varivoxelEnd_];
          while (j != parent_.varivoxelEnd_) {
            if (colCount_[j] == 0) {
              if (currentHoles == 0) {
                s = 0;
                break;
              }
              currentHoles--;
            }
            j = right_[j];
          }
        }

        if (s) {
          columns_[pos_] = c;
          rows_[pos_] = down(columns_[pos_]);
          cont = true;
        }

        if (!cont) {
          rows_[pos_] = 0;
          pos_--;
          if (pos_ < prefixDepth) {
            break;
          }
          continue;
        }

        cover(columns_[pos_]);
      } else {
        uncoverRow(rows_[pos_]);
        cont = true;
        rows_[pos_] = down(rows_[pos_]);
        if (rows_[pos_] == columns_[pos_]) {
          cont = false;
        }
      }

      if (cont) {
        coverRow(rows_[pos_]);
        pos_++;
      } else {
        uncover(columns_[pos_]);
        rows_[pos_] = 0;
        pos_--;
        if (pos_ < prefixDepth) {
          break;
        }
      }
    }

    // unwind the prefix when the task was searched to its end; after a stop
    // the copy stays as it is (this thread takes no further task)
    if (!run_.stopRequested()) {
      for (int d = asInt(prefixDepth) - 1; d >= 0; d--) {
        const auto ud = static_cast<std::size_t>(d);
        uncoverRow(task.prefix[ud].row);
        uncover(task.prefix[ud].col);
        rows_[ud] = 0;
        columns_[ud] = 0;
      }
      pos_ = 0;
    }
  }

  void flushIterations() {
    const std::uint64_t unflushed = localIterations_ - flushedIterations_;
    if (unflushed > 0) {
      parent_.iterations_.fetch_add(unflushed, std::memory_order_relaxed);
      flushedIterations_ = localIterations_;
    }
  }

  Assembler0& parent_;
  SearchRun& run_;
  std::vector<unsigned int> left_;
  std::vector<unsigned int> right_;
  std::vector<unsigned int> upDown_;
  std::vector<unsigned int> colCount_;
  std::vector<unsigned int> rows_;
  std::vector<unsigned int> columns_;
  unsigned int pos_ = 0;
  std::uint64_t localIterations_ = 0;
  std::uint64_t flushedIterations_ = 0;
  /// The progress slot this thread reports to, the task it is in, and a
  /// count of flushes so that it reports only now and then.
  unsigned int slot_;
  std::size_t taskIndex_ = 0;
  unsigned int flushCount_ = 0;
};

// An assembly found by a search split over threads: built from the rows of
// the cover (in any order), checked against its rotated copies and against
// what was reported before, and handed over. Called by the worker threads,
// the plain ones and the vector ones alike.
void Assembler0::reportParallelSolution(SearchRun& run, const unsigned int* solutionRows,
                                        unsigned int count) {
  if (!run.hasCallback()) {
    return;
  }

  Assembly assembly = assemblyFromRows(solutionRows, count);

  if (avoidTransformedAssemblies_ &&
      assembly.smallerRotationExists(puzzle_, problem_, avoidTransformedPivot_,
                                     avoidTransformedMirror_.get(), complete_, strictColors_)) {
    return;
  }

  // the signature: what tells this assembly from the others; an unplaced
  // piece counts as transformation 0 at the origin, as in the original
  std::uint64_t sig = kFnvOffset;
  for (const Placement& p : assembly.getPlacements()) {
    const bool placed = p.transformation != kUnplacedTrans;
    sig = fnvMix(sig, placed ? p.transformation : 0U);
    sig = fnvMix(sig, static_cast<std::uint32_t>(placed ? p.xpos : 0));
    sig = fnvMix(sig, static_cast<std::uint32_t>(placed ? p.ypos : 0));
    sig = fnvMix(sig, static_cast<std::uint32_t>(placed ? p.zpos : 0));
  }

  run.deliverOnce(std::move(assembly), sig, split_.reported);
}

void Assembler0::generateSubtreeTasks(std::vector<SubtreeTask>& tasks, unsigned int targetTasks,
                                      unsigned int maxDepth) {
  tasks.clear();
  tasks.emplace_back();

  unsigned int currentDepth = 0;
  const unsigned int maxPrefix = piecenumber_ > 1 ? piecenumber_ - 1 : 1U;

  while (tasks.size() < targetTasks && currentDepth < maxDepth) {
    std::vector<SubtreeTask> nextTasks;
    bool anyExpanded = false;

    for (const SubtreeTask& t : tasks) {
      if (t.prefix.size() >= maxPrefix) {
        nextTasks.push_back(t);
        continue;
      }

      for (const PrefixStep& step : t.prefix) {
        cover(step.col);
        coverRow(step.row);
      }

      if (!right_[0]) {
        nextTasks.push_back(t);
        for (int d = asInt(t.prefix.size()) - 1; d >= 0; d--) {
          uncoverRow(t.prefix[static_cast<std::size_t>(d)].row);
          uncover(t.prefix[static_cast<std::size_t>(d)].col);
        }
        continue;
      }

      unsigned int c = right_[0];
      unsigned int s = colCount_[c];

      if (s) {
        unsigned int j = right_[c];
        while (j) {
          if (colCount_[j] < s) {
            c = j;
            s = colCount_[c];
            if (!s) {
              break;
            }
          }
          j = right_[j];
        }
      }

      if (s) {
        auto currentHoles = asUnsigned(holes_);
        unsigned int j = right_[varivoxelEnd_];
        while (j != varivoxelEnd_) {
          if (colCount_[j] == 0) {
            if (currentHoles == 0) {
              s = 0;
              break;
            }
            currentHoles--;
          }
          j = right_[j];
        }
      }

      if (s > 0) {
        anyExpanded = true;
        for (unsigned int r = down(c); r != c; r = down(r)) {
          SubtreeTask child = t;
          child.prefix.push_back(PrefixStep{c, r});
          child.weight = t.weight / static_cast<double>(s);
          nextTasks.push_back(std::move(child));
        }
      }

      for (int d = asInt(t.prefix.size()) - 1; d >= 0; d--) {
        uncoverRow(t.prefix[static_cast<std::size_t>(d)].row);
        uncover(t.prefix[static_cast<std::size_t>(d)].col);
      }
    }

    if (!anyExpanded) {
      break;
    }

    tasks = std::move(nextTasks);
    currentDepth++;
  }
}

void Assembler0::parallelMultiSearch(SearchRun& run, unsigned int workers,
                                     const SearchOptions& options) {
  // the workers read the shapes (smallerRotationExists): their lazy caches
  // must be filled before any worker exists
  warmShapeCaches(puzzle_, problem_);

  if (split_.tasks.empty()) {
    const unsigned int targetTasks = std::max(16U, workers * 4);
    const unsigned int maxDepth = std::min(piecenumber_ > 1 ? piecenumber_ - 1 : 1U, 3U);
    generateSubtreeTasks(split_.tasks, targetTasks, maxDepth);
    split_.completed.assign(split_.tasks.size(), 0);
  }

  std::vector<double> weights(split_.tasks.size());
  for (std::size_t i = 0; i < split_.tasks.size(); i++) {
    weights[i] = split_.tasks[i].weight;
  }
  taskProgress_.begin(weights, split_.completed);

  if (split_.tasks.empty()) {
    return;
  }

  if (std::find(split_.completed.begin(), split_.completed.end(), 0) ==
      split_.completed.end()) {
    pos_ = piecenumber_ + 1;
    return;
  }

  // When the matrix fits, every thread searches its tasks with the vector
  // search: one solver, read only, shared by all. A task is the rows chosen
  // so far, and the covers below it are the covers that hold those rows
  // whatever order the rest is chosen in, so the vector search finds the
  // same ones the plain search would. Tasks, completion and signatures stay
  // what they are, and with them stopping, continuing and saving.
  std::unique_ptr<SimdExactCoverBase> simdSolver;
  if (simdFits(options)) {
    simdSolver = createSimdSolver(options.kernels);
  }
  const SimdExactCoverBase* simd = simdSolver.get();

  struct TaskSearch {
    Assembler0* self;
    SearchRun* run;
    const SimdExactCoverBase* simd;
    unsigned int slot;
    std::unique_ptr<Worker> plain;
    std::vector<unsigned int> prefixRows;

    void operator()(std::size_t task) {
      if (plain) {
        (*plain)(task);
        return;
      }

      prefixRows.clear();
      for (const PrefixStep& step : self->split_.tasks[task].prefix) {
        prefixRows.push_back(step.row);
      }

      std::atomic<std::uint64_t> taskIter{0};
      auto drain = [this, &taskIter] {
        self->iterations_.fetch_add(taskIter.exchange(0, std::memory_order_relaxed),
                                    std::memory_order_relaxed);
      };
      simd->solveSubtree(
          prefixRows,
          [this, &drain](const std::vector<unsigned int>& nodes) -> bool {
            drain();
            self->reportParallelSolution(*run, nodes.data(),
                                         static_cast<unsigned int>(nodes.size()));
            return !run->stopRequested();
          },
          run->token(), taskIter,
          [this, &drain, task](float fraction) {
            drain();
            self->taskProgress_.report(slot, task, fraction);
          });
      drain();
    }
  };

  runTasks(split_.completed, workers, options.pool, run, taskProgress_,
           [this, &run, simd](unsigned int slot) {
             TaskSearch s{this, &run, simd, slot, nullptr, {}};
             if (simd == nullptr) {
               s.plain = std::make_unique<Worker>(*this, run, slot);
             }
             return s;
           });

  if (!run.stopRequested()) {
    pos_ = piecenumber_ + 1;
    split_.clearTasks();
    split_.interrupted = false;
  } else {
    // Stopped part way. Continuing in this session is fine: the tasks, their
    // completion and the signatures are all still here, so the remaining
    // tasks get picked up and what a half-searched task finds again is
    // suppressed; a saved position carries all three (flag 2).
    split_.interrupted = true;
  }
}

// ---- the vector search -----------------------------------------------------------

// Whether the matrix can go to the vector search at all. Holes and variable
// voxels do not keep it out: the variable columns are optional ones there
// and the holes a budget. The limit is 2048 columns, the variable ones
// included: above it the row masks, the masks passed down the recursion and
// the per-node column counts cost more than the plain search does.
bool Assembler0::simdFits(const SearchOptions& options) const {
  if (!options.vectorSearch) {
    return false;
  }
  if (debug_) {
    return false;
  }
  if (varivoxelEnd_ == 0 || varivoxelEnd_ - 1 > 2048) {
    return false;
  }
  return true;
}

bool Assembler0::canUseSimd(const SearchOptions& options) const {
  return pos_ == 0 && simdFits(options);
}

std::unique_ptr<SimdExactCoverBase> Assembler0::createSimdSolver(SimdKernels kernels) const {
  // Matrix column c is solver column c-1. The piece and filled-voxel columns
  // come first (1 .. varivoxelStart-1, the header list), the variable voxels
  // after them up to varivoxelEnd-1.
  const unsigned int maxCol = varivoxelEnd_ - 1;

  std::unique_ptr<SimdExactCoverBase> solver = makeSimdExactCover(maxCol, piecenumber_, kernels);

  for (unsigned int c = right_[0]; c != 0; c = right_[c]) {
    if (c <= maxCol) {
      solver->setRequiredColumn(c - 1);
    }
  }

  // the variable voxels are a list of their own, which the plain search
  // never branches on either
  for (unsigned int c = right_[varivoxelEnd_]; c != varivoxelEnd_; c = right_[c]) {
    solver->setOptionalColumn(c - 1);
  }
  solver->setHoleBudget(asUnsigned(holes_));

  for (unsigned int p = 1; p <= piecenumber_; p++) {
    for (unsigned int row = down(p); row != p; row = down(row)) {
      std::vector<unsigned int> cols;
      std::vector<unsigned int> nodesInRow;
      unsigned int curr = row;
      do {
        nodesInRow.push_back(curr);
        const unsigned int col = colCount_[curr];
        if (col > 0 && col <= maxCol) {
          cols.push_back(col - 1);
        }
        curr = right_[curr];
      } while (curr != row);

      const std::uint32_t rowIdx = solver->addRow(row, p - 1, cols);
      for (const unsigned int n : nodesInRow) {
        solver->registerNodeAlias(n, rowIdx);
      }
    }
  }

  return solver;
}

void Assembler0::simdSearch(SearchRun& run, const SearchOptions& options) {
  auto solver = createSimdSolver(options.kernels);
  std::atomic<std::uint64_t> simdIter{0};

  // reported as one task, so that finished() has something to say while it
  // runs; the iterations go over as it goes, not only at the end
  taskProgress_.begin({1.0}, {0});

  auto drain = [this, &simdIter] {
    iterations_.fetch_add(simdIter.exchange(0, std::memory_order_relaxed),
                          std::memory_order_relaxed);
  };

  split_.vectorDone = 0;
  solver->solve(
      [this, &run, &drain](const std::vector<unsigned int>& solutionNodes) -> bool {
        drain();
        // already reported before the search was stopped
        if (split_.vectorDone++ < split_.vectorSkip) {
          return !run.stopRequested();
        }
        if (solutionNodes.size() > rows_.size()) {
          rows_.resize(solutionNodes.size());
        }
        for (std::size_t i = 0; i < solutionNodes.size(); i++) {
          rows_[i] = solutionNodes[i];
        }
        pos_ = static_cast<unsigned int>(solutionNodes.size());
        solution(run);
        return !run.stopRequested();
      },
      run.token(), simdIter,
      [this, &drain](float fraction) {
        drain();
        taskProgress_.report(0, 0, fraction);
      });

  drain();
  if (!run.stopRequested()) {
    pos_ = piecenumber_ + 1;
    taskProgress_.finish(0, 0);
    split_.vectorSkip = 0;
    split_.interrupted = false;
  } else {
    // rows and pos held the last solution, not a place in the matrix: back
    // to the start, passing over what was reported on the next run. The rows
    // are cleared too: savePosition() writes rows[0], and restorePosition()
    // would cover it as if the search stood there.
    pos_ = 0;
    std::fill(rows_.begin(), rows_.end(), 0U);
    std::fill(columns_.begin(), columns_.end(), 0U);
    split_.vectorSkip = std::max(split_.vectorSkip, split_.vectorDone);
    split_.interrupted = true;
  }
}

// ---- runs, progress and positions --------------------------------------------------

RunEnd Assembler0::assemble(const AssemblyCallback& callback, const SearchOptions& options,
                            const StopToken& stop) {
  BT_ASSERT_MSG(errorsState_ == AssemblerError::None, "assemble() after a failed createMatrix()");

  debug_ = false;

  SearchRun run(stop, callback);
  const unsigned int threads = effectiveThreads(options.threads, options.pool);

  // A stopped search carries on the way it ran: the vector search passing
  // over what it reported (also when this run asks for no vector search:
  // the plain search cannot skip), the split search with its remaining
  // tasks, whatever the thread count.
  SearchOptions vectorAnyway = options;
  vectorAnyway.vectorSearch = true;
  if (split_.vectorSkip > 0 && canUseSimd(vectorAnyway)) {
    simdSearch(run, vectorAnyway);
  } else if (pos_ == 0 && (threads > 1 || !split_.tasks.empty())) {
    parallelMultiSearch(run, threads, options);
  } else if (canUseSimd(options)) {
    simdSearch(run, options);
  } else {
    // not the fraction of an earlier search that reported by tasks
    taskProgress_.reset();
    iterativeMultiSearch(run);
  }

  return pos_ > piecenumber_ ? RunEnd::Finished : RunEnd::Stopped;
}

float Assembler0::finished() const {
  if (taskProgress_.active()) {
    return taskProgress_.fraction();
  }
  return serialProgress_.load(std::memory_order_relaxed);
}

AssemblerError Assembler0::restorePosition(std::string_view text, std::string_view version) {
  // the matrix must stand at the start, otherwise the stack would need
  // cleaning
  BT_ASSERT(pos_ == 0);
  split_ = SplitSearchState<SubtreeTask>{};

  if (version != kVersion) {
    return AssemblerError::CannotRestoreVersion;
  }

  const std::string str(text);  // NUL-terminated for the number readers
  const char* string = str.c_str();
  const auto len = static_cast<unsigned int>(str.size());
  unsigned int spos = 0;

  // The leading flag: 0 a position of the plain search, 2 a stopped split
  // search with its tasks and the assemblies it reported, 3 a stopped vector
  // search with how many it reported. 1, from older versions, is a stopped
  // split search saved without those, which cannot be continued.
  unsigned int interrupted = 0;
  spos += getInt(string + spos, &interrupted);
  if (spos >= len) {
    return AssemblerError::CannotRestoreSyntax;
  }
  if (interrupted == 1) {
    return AssemblerError::CannotRestoreInterrupted;
  }
  if (interrupted > 3) {
    return AssemblerError::CannotRestoreSyntax;
  }

  unsigned int pos = 0;
  spos += getInt(string + spos, &pos);
  if (spos >= len) {
    return AssemblerError::CannotRestoreSyntax;
  }

  std::uint64_t it = 0;
  spos += getLong(string + spos, &it);
  if (spos >= len) {
    return AssemblerError::CannotRestoreSyntax;
  }

  if (pos <= piecenumber_) {
    for (unsigned int i = 0; i <= pos; i++) {
      while ((spos < len) && (string[spos] != '(')) {
        spos++;
      }
      spos++;
      if (spos >= len) {
        return AssemblerError::CannotRestoreSyntax;
      }

      spos += getInt(string + spos, &rows_[i]);
      if (spos >= len) {
        return AssemblerError::CannotRestoreSyntax;
      }
      spos += getInt(string + spos, &columns_[i]);
      if (spos >= len) {
        return AssemblerError::CannotRestoreSyntax;
      }

      while ((spos < len) && (string[spos] != ')')) {
        spos++;
      }
      spos++;
      // after the last ')' the text may end
      if ((i < pos) && (spos >= len)) {
        return AssemblerError::CannotRestoreSyntax;
      }
    }

    // integrity: the rows placed must be nodes (the last entry may only
    // prepare the next step and is not checked)
    for (unsigned int i = 0; i < pos; i++) {
      if (rows_[i] > left_.size()) {
        return AssemblerError::CannotRestoreSyntax;
      }
    }
  }
  // A position past the last piece is a finished search, which has no rows
  // to read (the original read past its row list here).

  if (interrupted >= 2) {
    if (pos != 0) {
      return AssemblerError::CannotRestoreSyntax;
    }
    SplitSearchState<SubtreeTask> tail;
    std::size_t done = 0;
    const std::size_t rowCount = left_.size();
    const bool ok = readResumeTail(
        string + spos, interrupted, tail, done, [rowCount](ResumeReader& r, SubtreeTask& t) {
          unsigned long long k = 0;
          if (!r.next(k)) {
            return false;
          }
          t.prefix.resize(static_cast<std::size_t>(k));
          for (PrefixStep& st : t.prefix) {
            if (!r.next(st.col) || !r.next(st.row) || st.col >= rowCount || st.row >= rowCount) {
              return false;
            }
          }
          return true;
        });
    if (!ok) {
      return AssemblerError::CannotRestoreSyntax;
    }
    split_ = std::move(tail);
    if (interrupted == 2) {
      taskProgress_.begin({}, split_.completed);
    }
    split_.interrupted = true;

    // Such a search starts from the untouched matrix. A file written before
    // the rows were cleared on a stop may still name a row here (the last
    // solution's first); covering it below would take the matrix apart.
    rows_[0] = 0;
    columns_[0] = 0;
  }

  pos_ = pos;
  iterations_.store(it, std::memory_order_relaxed);

  // bring the matrix into the position: cover the rows and columns in the
  // order the search did
  if (pos_ <= piecenumber_) {
    for (unsigned int p = 0; p <= pos_; p++) {
      if ((p < piecenumber_) && rows_[p]) {
        cover(columns_[p]);
        coverRow(rows_[p]);
      }
    }
  }

  publishSerialProgress();
  return AssemblerError::None;
}

SavedPosition Assembler0::savePosition() const {
  BT_ASSERT_MSG(pos_ > piecenumber_ || pos_ < rows_.size(), "savePosition() before createMatrix()");

  // the leading flag, see restorePosition()
  const unsigned int flag = !split_.interrupted ? 0 : split_.tasks.empty() ? 3 : 2;

  std::string out = std::to_string(flag) + " ";
  out += std::to_string(pos_) + " " + std::to_string(iterations_.load(std::memory_order_relaxed)) +
         " ";

  if (pos_ <= piecenumber_) {
    for (unsigned int j = 0; j <= pos_; j++) {
      out += "(" + std::to_string(rows_[j]) + " " + std::to_string(columns_[j]) + ")";

      if (j < pos_) {
        out += " ";
      }
    }
  }

  writeResumeTail(out, flag, std::max(split_.vectorSkip, split_.vectorDone), split_,
                  [](std::string& o, const SubtreeTask& t) {
                    o += " " + std::to_string(t.prefix.size());
                    for (const PrefixStep& st : t.prefix) {
                      o += " " + std::to_string(st.col) + " " + std::to_string(st.row);
                    }
                  });

  return SavedPosition{kVersion, std::move(out)};
}

unsigned int Assembler0::piecePlacement(unsigned int node, int delta, unsigned int piece,
                                        unsigned char* tran, int* x, int* y, int* z) const {
  unsigned int pi = 0;

  if (!node) {
    node = down(piece + 1);
  }

  while (delta > 0) {
    node = down(node);
    delta--;
  }

  while (delta < 0) {
    node = up(node);
    delta++;
  }

  getPieceInformation(node, tran, x, y, z, &pi);

  BT_ASSERT(pi == piece);

  return node;
}

unsigned int Assembler0::piecePlacementCount(unsigned int piece) const {
  return colCount_[piece + 1];
}

void Assembler0::debugStep(unsigned long steps) {
  debug_ = true;
  debugLoops_ = static_cast<long>(steps);
  const AssemblyCallback none;
  SearchRun run(StopToken{}, none);
  iterativeMultiSearch(run);
}

bool Assembler0::canHandle(const Problem& problem) {
  // every part must be exactly one piece
  for (unsigned int s = 0; s < problem.getNumberOfParts(); s++) {
    if ((problem.getPartMaximum(s) > 1) ||
        (problem.getPartMaximum(s) != problem.getPartMinimum(s))) {
      return false;
    }
  }

  return true;
}

}  // namespace bt

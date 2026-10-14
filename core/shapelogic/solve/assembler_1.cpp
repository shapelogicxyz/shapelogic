// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/assembler_1.cpp (and simdHuangMemoryEstimate, now in
// simd_huang_cover.cpp). The search code is the original's: the plain
// search (iterative), the worker's copy of it (assemblerWorker_1) and the
// task generation's copy (generateTasksAtDepth) stay three state machines,
// as there. What changed is around them: the stop is the run's token, the
// threads come from the pool through runTasks(), the split and vector state
// is a SplitSearchState, the saved position is a string and is checked
// before it is used, the progress of the plain search is published by its
// thread, and nothing is printed (the debug traces, the "range
// optimisation" and "removed ... rows" lines are gone).
#include "shapelogic/solve/assembler_1.h"

#include "shapelogic/base/int_cast.h"
#include "shapelogic/grid/symmetries.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/solve/placement_finder.h"
#include "shapelogic/solve/simd_huang_cover.h"

#include <algorithm>
#include <cstdlib>
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

/// A sliding tray need not be filled (sliding::isSliding in the original).
bool isSliding(const Puzzle& puzzle) { return puzzle.getGridType().getType() == GridKind::Sliding; }

/// The product as the original's int multiplication gives it: modulo 2^32,
/// read as signed.
int wrapMul(unsigned int a, unsigned int b) { return static_cast<int>(a * b); }

/// The column that will give the fewest ways to meet its condition: in the
/// original, its row count times the weight it may still take (the minimum
/// was passed but not used), in int.
bool betterParams(unsigned int nSum, unsigned int nMax, unsigned int oSum, unsigned int oMax) {
  return wrapMul(nSum, nMax) < wrapMul(oSum, oMax);
}

/// strtol from `s`, cast to unsigned int; returns the characters consumed
/// (the original's getInt: no digits read 0).
unsigned int getInt(const char* s, unsigned int* i) {
  char* s2 = nullptr;
  *i = static_cast<unsigned int>(std::strtol(s, &s2, 10));
  return static_cast<unsigned int>(s2 - s);
}

/// A vector as the original wrote it: its size and its values, each
/// followed by a space.
void vectorToString(const std::vector<unsigned int>& v, std::string& out) {
  out += std::to_string(v.size());
  out += ' ';
  for (const unsigned int x : v) {
    out += std::to_string(x);
    out += ' ';
  }
}

/// Reads a vector written by vectorToString (stringToVector); returns the
/// characters consumed. A count larger than the text that is left cannot
/// be right and reads nothing (the original read on past the text).
unsigned int stringToVector(const char* s, unsigned int left, std::vector<unsigned int>& v,
                            bool& ok) {
  unsigned int pos = 0;
  unsigned int count = 0;
  pos += getInt(s + pos, &count);
  v.clear();
  if (count > left) {
    ok = false;
    return pos;
  }
  for (unsigned int i = 0; i < count; i++) {
    unsigned int val = 0;
    pos += getInt(s + pos, &val);
    v.push_back(val);
  }
  return pos;
}

}  // namespace

// ---- the nodes ---------------------------------------------------------------------

void Assembler1::Links::coverColumnOnly(unsigned int col) {
  right[left[col]] = right[col];
  left[right[col]] = left[col];
}

void Assembler1::Links::uncoverColumnOnly(unsigned int col) {
  right[left[col]] = col;
  left[right[col]] = col;
}

void Assembler1::Links::coverColumnRows(unsigned int col) {
  for (unsigned int r = down[col]; r != col; r = down[r]) {
    colCount[col] -= weight[r];
    for (unsigned int c = right[r]; c != r; c = right[c]) {
      up[down[c]] = up[c];
      down[up[c]] = down[c];
      colCount[colCount[c]] -= weight[c];
    }
  }
}

void Assembler1::Links::uncoverColumnRows(unsigned int col) {
  for (unsigned int r = up[col]; r != col; r = up[r]) {
    for (unsigned int c = left[r]; c != r; c = left[c]) {
      colCount[colCount[c]] += weight[c];
      up[down[c]] = c;
      down[up[c]] = c;
    }
    colCount[col] += weight[r];
  }
}

void Assembler1::Links::hideRow(unsigned int r) {
  for (unsigned int rr = right[r]; rr != r; rr = right[rr]) {
    up[down[rr]] = up[rr];
    down[up[rr]] = down[rr];

    colCount[colCount[rr]] -= weight[rr];
  }
  up[down[r]] = up[r];
  down[up[r]] = down[r];

  colCount[colCount[r]] -= weight[r];
}

void Assembler1::Links::unhideRow(unsigned int r) {
  up[down[r]] = r;
  down[up[r]] = r;

  colCount[colCount[r]] += weight[r];

  for (unsigned int rr = left[r]; rr != r; rr = left[rr]) {
    up[down[rr]] = rr;
    down[up[rr]] = rr;

    colCount[colCount[rr]] += weight[rr];
  }
}

// Removes every row that would take an open column past its maximum when
// added to the weight it has. Only the columns of `row` changed their
// weight, so only those are looked at. The rows hidden are kept on `hidden`
// after a separator (0: the header row is never hidden), so that the
// batches can be stacked and brought back one at a time.
void Assembler1::Links::hideRows(unsigned int row, std::vector<unsigned int>& hidden,
                                 const std::vector<unsigned int>& max) {
  hidden.push_back(0);

  for (unsigned int r = right[row]; r != row; r = right[r]) {
    const unsigned int col = colCount[r];

    for (unsigned int rr = down[col]; rr != col; rr = down[rr]) {
      if (weight[rr] + weight[col] > max[col]) {
        hideRow(rr);
        hidden.push_back(rr);
      }
    }
  }
}

void Assembler1::Links::unhideRows(std::vector<unsigned int>& hidden) {
  while (hidden.back()) {
    unhideRow(hidden.back());
    hidden.pop_back();
  }

  // the separator
  hidden.pop_back();
}

// ---- the matrix ------------------------------------------------------------------

void Assembler1::generateFirstRow(unsigned int columns) {
  for (unsigned int i = 0; i < columns + 1; i++) {
    links_.right.push_back(i + 1);
    links_.left.push_back(i - 1);
    links_.up.push_back(i);
    links_.down.push_back(i);
    links_.colCount.push_back(0);
    min_.push_back(1);
    max_.push_back(1);
    links_.weight.push_back(0);
  }

  // make the list cyclic
  links_.left[0] = columns;
  links_.right[columns] = 0;

  headerNodes_ = columns + 1;
}

unsigned int Assembler1::addPieceNode(unsigned int piece, unsigned int rot, int x, int y, int z) {
  Links& n = links_;
  const auto piecenode = static_cast<unsigned int>(n.left.size());

  n.left.push_back(piecenode);
  n.right.push_back(piecenode);
  n.up.push_back(n.up[piece + 1]);
  n.down.push_back(piece + 1);
  n.weight.push_back(1);

  n.down[n.up[piece + 1]] = piecenode;
  n.up[piece + 1] = piecenode;

  n.colCount.push_back(piece + 1);
  n.colCount[piece + 1]++;

  piecePositions_.push_back(
      PiecePosition{x, y, z, static_cast<unsigned char>(rot), piecenode, piece});

  return piecenode;
}

const Assembler1::PiecePosition& Assembler1::placementOf(unsigned int node) const {
  // by halving, as DlxMatrix::placementOf
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

  return piecePositions_[lo - 1];
}

void Assembler1::addVoxelNode(unsigned int col, unsigned int piecenode) {
  Links& n = links_;
  BT_ASSERT(n.left.size() == n.right.size() && n.left.size() == n.up.size() &&
            n.left.size() == n.down.size() && n.left.size() == n.colCount.size() &&
            n.left.size() == n.weight.size());

  const auto newnode = static_cast<unsigned int>(n.left.size());

  n.right.push_back(piecenode);
  n.left.push_back(n.left[piecenode]);
  n.right[n.left[piecenode]] = newnode;
  n.left[piecenode] = newnode;

  n.up.push_back(n.up[col]);
  n.down.push_back(col);
  n.down[n.up[col]] = newnode;
  n.up[col] = newnode;

  n.colCount.push_back(col);

  n.weight.push_back(1);
  n.colCount[col]++;
}

void Assembler1::addRangeNode(unsigned int col, unsigned int piecenode, unsigned int weight) {
  Links& n = links_;
  const auto newnode = static_cast<unsigned int>(n.left.size());

  n.right.push_back(piecenode);
  n.left.push_back(n.left[piecenode]);
  n.right[n.left[piecenode]] = newnode;
  n.left[piecenode] = newnode;

  n.up.push_back(n.up[col]);
  n.down.push_back(col);
  n.down[n.up[col]] = newnode;
  n.up[col] = newnode;

  n.colCount.push_back(col);

  n.weight.push_back(weight);
  n.colCount[col] += weight;
}

Assembler1::Assembler1(const Puzzle& puzzle, const Problem& problem)
    : puzzle_(puzzle), problem_(problem) {
  stacks_.nextRowStack.push_back(0);
  stacks_.taskStack.push_back(0);
}

Assembler1::~Assembler1() = default;

// Builds the matrix. Additions to Knuth's algorithm: variable voxels (empty
// spaces in the result) are columns with a minimum of 0, and several pieces
// of one shape are one column whose weight must lie between the part's
// minimum and maximum. When pieces have ranges, one more column counts the
// voxels the range pieces fill, which must lie between what the result
// leaves to them at least and at most.
//
// Returns 1, or minus the shape id of a part that has no placement.
int Assembler1::prepare(bool hasRange, unsigned int rangeMin, unsigned int rangeMax) {
  const Voxel& result = problem_.resultShape(puzzle_);

  // nodes 1..n are the column headers
  generateFirstRow(result.countState(Voxel::kFilled) + result.countState(Voxel::kVariable) +
                   problem_.getNumberOfParts() + (hasRange ? 1 : 0));

  // the matrix column of every result voxel (a lookup, because computing it
  // from the counts of filled and variable voxels is slow)
  std::vector<unsigned int> columns(result.getXYZ());

  // the column of the range check (only used with hasRange)
  unsigned int rangeColumn = 0;

  {
    unsigned int c = 1 + problem_.getNumberOfParts();

    for (unsigned int i = 0; i < result.getXYZ(); i++) {
      switch (result.getState(i)) {
        case Voxel::kVariable:
          min_[c] = 0;
          holeColumns_.push_back(c);
          [[fallthrough]];
        case Voxel::kFilled:
          // Sliding does not require the tray to be filled: a normal cell
          // may stay empty, the same as a variable cell.
          if (result.getState(i) == Voxel::kFilled && isSliding(puzzle_)) {
            min_[c] = 0;
            holeColumns_.push_back(c);
          }
          columns[i] = c++;
          break;
        default:
          columns[i] = 0;
          break;
      }
    }

    if (hasRange) {
      min_[c] = rangeMin;
      max_[c] = rangeMax;
      rangeColumn = c;
    }
  }

  // The symmetry breaker: one piece is not placed in all its orientations,
  // so that few rotated duplicates are found in the first place. The piece
  // chosen is the one of the shape with the fewest pieces that shares the
  // fewest symmetries with the result. With several pieces of that shape, or
  // with ranges, it cannot be used, and every assembly is checked instead.
  const Symmetry resultSym = result.selfSymmetries();
  const GridType& gt = puzzle_.getGridType();
  const Symmetries& sym = gt.getSymmetries();
  unsigned int symBreakerShape = 0xFFFFFFFF;

  // with only the identity in the result, everything must be tried
  if (!unSymmetric(resultSym)) {
    // FIXME (original): with several suitable pieces, the one with the most
    // placements would gain more
    unsigned int symBreakerPiece = 0;
    unsigned int pc = problem_.getPartMaximum(0);
    unsigned int bestFound =
        sym.countSymmetryIntersection(resultSym, problem_.partShape(puzzle_, 0).selfSymmetries());
    symBreakerShape = 0;

    for (unsigned int i = 1; i < problem_.getNumberOfParts(); i++) {
      const unsigned int cnt = sym.countSymmetryIntersection(
          resultSym, problem_.partShape(puzzle_, i).selfSymmetries());

      if ((problem_.getPartMaximum(i) < problem_.getPartMaximum(symBreakerShape)) ||
          ((problem_.getPartMaximum(i) == problem_.getPartMaximum(symBreakerShape)) &&
           (cnt < bestFound))) {
        bestFound = cnt;
        symBreakerShape = i;
        symBreakerPiece = pc;
      }

      pc += problem_.getPartMaximum(i);
    }

    const bool tmp = sym.symmetriesLeft(
        resultSym, problem_.partShape(puzzle_, symBreakerShape).selfSymmetries());

    bool pieceRanges = false;
    for (unsigned int i = 0; i < problem_.getNumberOfParts(); i++) {
      if (problem_.getPartMinimum(i) != problem_.getPartMaximum(i)) {
        pieceRanges = true;
        break;
      }
    }

    if (tmp || (problem_.getPartMaximum(symBreakerShape) > 1) || pieceRanges) {
      // the symmetry breaker cannot be used with more than one piece of its
      // shape in the problem
      if (pieceRanges || problem_.getPartMaximum(symBreakerShape) > 1) {
        symBreakerShape = 0xFFFFFFFF;
        symBreakerPiece = 0xFFFFFFFF;
      }

      filter_.request(symBreakerPiece, nullptr);
    }

    if (sym.symmetryContainsMirror(resultSym)) {
      // The mirror check needs the mirror structure. Which case is this?
      // 1) all pieces have mirror symmetry: check mirrors, no pairs
      // 2) a piece has none:
      //   2a) each such piece has a mirror partner: check mirrors, with pairs
      //   2b) one has no partner: no mirror check
      pc = 0;

      struct Mm {
        unsigned int shape;   // the shape of this piece
        unsigned int mirror;  // the mirror shape of this piece
        unsigned int trans;
      };

      std::vector<Mm> mirror(problem_.getNumberOfPieces());

      for (unsigned int i = 0; i < problem_.getNumberOfParts(); i++) {
        for (unsigned int p = 0; p < problem_.getPartMaximum(i); p++) {
          mirror[pc].shape = i;
          mirror[pc].mirror = static_cast<unsigned int>(-1);
          mirror[pc].trans = 255;
          pc++;
        }
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

      // With ranges the structure is needed all the same: an assembly may
      // use only pieces that mirror themselves.
      if (mirrorCheck || pieceRanges) {
        auto mir = std::make_unique<MirrorInfo>();

        for (unsigned int i = 0; i < problem_.getNumberOfPieces(); i++) {
          if (mirror[i].trans != 255) {
            mir->addPieces(i, mirror[i].mirror, static_cast<unsigned char>(mirror[i].trans));
          }
        }

        filter_.request(symBreakerPiece, std::move(mir));
      }
    }
  }

  std::vector<std::unique_ptr<Voxel>> cache;
  cache.reserve(sym.getNumTransformationsMirror());

  const PlacementFinder finder(puzzle_, problem_, result, strictColors_);
  std::vector<long> voxelOffsets;
  std::vector<int> positions;

  const long rsx = result.getX();
  const long rsy = result.getY();

  for (unsigned int pc = 0; pc < problem_.getNumberOfParts(); pc++) {
    reducePiece_.store(pc, std::memory_order_relaxed);

    // the weights of the shape's column fit its number of pieces
    max_[pc + 1] = problem_.getPartMaximum(pc);
    min_[pc + 1] = problem_.getPartMinimum(pc);

    const unsigned int voxels = problem_.partShape(puzzle_, pc).countState(Voxel::kFilled);

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
          placements++;

          // the voxels the piece fills
          const long base = x + rsx * (y + rsy * static_cast<long>(z));
          for (const long off : voxelOffsets) {
            addVoxelNode(columns[static_cast<std::size_t>(base + off)], piecenode);
          }

          // a piece of a part with a range counts its voxels in the range
          // column
          if (hasRange && (min_[pc + 1] != max_[pc + 1])) {
            addRangeNode(rangeColumn, piecenode, voxels);
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

    // a part that must be used and fits nowhere
    if (placements == 0 && problem_.getPartMinimum(pc) > 0) {
      return -asInt(problem_.getShapeIdOfPart(pc));
    }
  }

  return 1;
}

AssemblerError Assembler1::createMatrix(const MatrixOptions& options) {
  BT_ASSERT(problem_.resultValid());

  filter_.complete = options.completeRotations;
  strictColors_ = options.strictColors;

  if (!canHandle(problem_)) {
    return AssemblerError::PuzzleUnhandable;
  }

  piecenumber_ = problem_.getNumberOfPieces();

  // the filled and variable units
  unsigned int resVari = problem_.resultShape(puzzle_).countState(Voxel::kVariable);
  const unsigned int resFilled =
      problem_.resultShape(puzzle_).countState(Voxel::kFilled) + resVari;

  // Sliding lets the pieces leave normal cells empty.
  if (isSliding(puzzle_)) {
    resVari = resFilled;
  }

  // the pieces may not have more units than the result has voxels, and the
  // filled voxels of the result must be fillable by them
  unsigned int min = 0;
  unsigned int max = 0;

  for (unsigned int j = 0; j < problem_.getNumberOfParts(); j++) {
    min += problem_.partShape(puzzle_, j).countState(Voxel::kFilled) * problem_.getPartMinimum(j);
    max += problem_.partShape(puzzle_, j).countState(Voxel::kFilled) * problem_.getPartMaximum(j);
  }

  if (min == max) {
    holes_ = resFilled - min;
  } else if (problem_.maxHolesDefined()) {
    holes_ = problem_.getMaxHoles();
  } else {
    holes_ = 0xFFFFFF;
  }

  if (min > resFilled) {
    errorsState_ = AssemblerError::TooManyUnits;
    errorsParam_ = asInt(min - resFilled);
    return errorsState_;
  }

  if (max < resFilled - resVari) {
    errorsState_ = AssemblerError::TooFewUnits;
    errorsParam_ = asInt(resFilled - resVari - max);
    return errorsState_;
  }

  // The voxels the range pieces must fill: the result holds between
  // resFilled - resVari and resFilled voxels, less those of the pieces
  // without a range. Checking that keeps the search from adding so many
  // range pieces that the others no longer fit.
  int rangeMin = asInt(resFilled - resVari);
  int rangeMax = asInt(resFilled);

  for (unsigned int j = 0; j < problem_.getNumberOfParts(); j++) {
    if (problem_.getPartMinimum(j) == problem_.getPartMaximum(j)) {
      const int v = asInt(problem_.partShape(puzzle_, j).countState(Voxel::kFilled) *
                          problem_.getPartMinimum(j));
      rangeMin -= v;
      rangeMax -= v;
    }
  }

  if (rangeMin < 0) {
    rangeMin = 0;
  }

  const int error = prepare(min != max, asUnsigned(rangeMin), static_cast<unsigned int>(rangeMax));

  // a piece that cannot be placed
  if (error <= 0) {
    errorsState_ = AssemblerError::CannotPlace;
    errorsParam_ = -error;
    return errorsState_;
  }

  filter_.apply(options.keepMirrors, options.keepRotations, options.completeRotations);

  errorsState_ = AssemblerError::None;
  publishSerialProgress();
  return errorsState_;
}

void Assembler1::applySolutionFilterFlags(bool keepMirrors, bool keepRotations, bool complete) {
  filter_.apply(keepMirrors, keepRotations, complete);
}

void Assembler1::removeColumn(unsigned int c) {
  Links& n = links_;
  unsigned int j = c;
  do {
    n.right[n.left[j]] = n.right[j];
    n.left[n.right[j]] = n.left[j];

    j = n.down[j];
  } while (j != c);
}

// Finds identical columns and removes all but one of each group: this saves
// no iterations but makes them cheaper. Two columns are identical when they
// have the same minimum and maximum and are in exactly the same placement
// rows with the same node weights. Columns are grouped by a hash of that
// signature and only compared exactly within a group, which is linear in the
// number of nodes. Returns the number of columns removed.
unsigned int Assembler1::clumpify() {
  const Links& n = links_;

  // the columns in header ring order; the first of each group survives
  std::vector<unsigned int> cols;
  for (unsigned int c = n.right[0]; c; c = n.right[c]) {
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
    h = (h ^ min_[col]) * 1099511628211ULL;
    h = (h ^ max_[col]) * 1099511628211ULL;

    unsigned int line = 0;

    for (unsigned int row = n.down[col]; row != col; row = n.down[row]) {
      // the placement row of this node; nodes come in increasing order so
      // the line only ever advances
      while ((line + 1 < piecePositions_.size()) && (piecePositions_[line + 1].row <= row)) {
        line++;
      }

      sigData.push_back(line);
      sigData.push_back(n.weight[row]);

      h = (h ^ line) * 1099511628211ULL;
      h = (h ^ static_cast<unsigned long long>(n.weight[row])) * 1099511628211ULL;
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
      if (min_[cols[i]] != min_[cols[j]] || max_[cols[i]] != max_[cols[j]]) {
        continue;
      }

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

void Assembler1::removeRow(unsigned int r) {
  Links& n = links_;
  unsigned int j = r;
  do {
    n.colCount[n.colCount[j]] -= n.weight[j];

    const unsigned int u = n.up[j];
    const unsigned int d = n.down[j];

    n.up[d] = u;
    n.down[u] = d;

    j = n.right[j];
  } while (j != r);
}

ReduceStats Assembler1::reduce(const StopToken& stop) {
  Links& n = links_;
  std::vector<unsigned int> toRemove;

  unsigned int rowRem = 0;
  unsigned int colRem = clumpify();

  // A quick removal of rows that are obviously impossible, not as thorough
  // as the one below but much faster. For each column that needs at least
  // one row (min > 0), count for each other column how many of its rows
  // take that column to its maximum. When all of them do, no row outside
  // this column can contribute there, and such rows can go. (From assembler
  // 0's reduce, which only knows columns of exactly one; this generalises it
  // to columns whose rows fill them at once.)
  std::vector<unsigned int> columns(headerNodes_);

  for (unsigned int col = n.right[0]; col; col = n.right[col]) {
    if (stop.stopRequested()) {
      break;
    }

    if (min_[col] == 0) {
      continue;
    }

    std::fill(columns.begin(), columns.end(), 0U);

    unsigned int placements = 0;
    for (unsigned int r = n.down[col]; r != col; r = n.down[r]) {
      for (unsigned int j = n.right[r]; j != r; j = n.right[j]) {
        if (n.weight[j] == max_[n.colCount[j]]) {
          columns[n.colCount[j]]++;
        }
      }
      placements++;
    }

    // A column that each of these rows fills: no other row can go there,
    // so those rows can be removed.
    for (unsigned int c = n.right[0]; c; c = n.right[c]) {
      if (columns[c] == placements) {
        toRemove.clear();

        for (unsigned int r = n.down[c]; r != c; r = n.down[r]) {
          // find the column col in the row
          unsigned int c2 = n.right[r];
          while ((n.colCount[c2] != col) && (c2 != r)) {
            c2 = n.right[c2];
          }

          if (c2 == r) {
            toRemove.push_back(r);
          }
        }

        rowRem += static_cast<unsigned int>(toRemove.size());

        for (const unsigned int rem : toRemove) {
          removeRow(rem);
        }
      }
    }
  }

  toRemove.clear();

  colRem += clumpify();

  bool dosth = false;

  do {
    if (stop.stopRequested()) {
      break;
    }

    for (const PiecePosition& pp : piecePositions_) {
      if (stop.stopRequested()) {
        break;
      }

      const unsigned int row = pp.row;
      reducePiece_.store(pp.piece, std::memory_order_relaxed);

      // a row no longer in the matrix
      if (n.up[n.down[row]] != row || n.down[n.up[row]] != row) {
        continue;
      }

      // add the row to the row set
      n.weight[n.colCount[row]] += n.weight[row];
      for (unsigned int r = n.right[row]; r != row; r = n.right[r]) {
        n.weight[n.colCount[r]] += n.weight[r];
      }

      std::vector<unsigned int> hidden;

      for (unsigned int r = n.right[row]; r != row; r = n.right[r]) {
        const unsigned int col = n.colCount[r];

        // Hide the rows that would take an open column past its maximum.
        // Only the columns of this row changed their weight.
        for (unsigned int rr = n.down[col]; rr != col; rr = n.down[rr]) {
          if (n.weight[rr] + n.weight[col] > max_[col]) {
            n.hideRow(rr);
            hidden.push_back(rr);
          }
        }
      }

      if (!openColumnConditionsFulfillable(n)) {
        toRemove.push_back(row);
      }

      while (!hidden.empty()) {
        n.unhideRow(hidden.back());
        hidden.pop_back();
      }

      // remove the row from the row set again, in the reverse order
      for (unsigned int r = n.left[row]; r != row; r = n.left[r]) {
        n.weight[n.colCount[r]] -= n.weight[r];
      }
      n.weight[n.colCount[row]] -= n.weight[row];
    }

    dosth = !toRemove.empty();
    rowRem += static_cast<unsigned int>(toRemove.size());

    while (!toRemove.empty()) {
      removeRow(toRemove.back());
      toRemove.pop_back();
    }

  } while (dosth);

  colRem += clumpify();

  return ReduceStats{rowRem, colRem};
}

// ---- column conditions -------------------------------------------------------------

bool Assembler1::columnConditionFulfilled(const Links& n, unsigned int col) const {
  return (n.weight[col] >= min_[col]) && (n.weight[col] <= max_[col]);
}

bool Assembler1::columnConditionFulfillable(const Links& n, unsigned int col) const {
  if (n.weight[col] > max_[col]) {
    return false;
  }
  return n.weight[col] + n.colCount[col] >= min_[col];
}

bool Assembler1::openColumnConditionsFulfillable(const Links& n) const {
  for (unsigned int col = n.right[0]; col; col = n.right[col]) {
    if (n.weight[col] > max_[col]) {
      return false;
    }
    if (n.weight[col] + n.colCount[col] < min_[col]) {
      return false;
    }
  }

  return true;
}

// The column to branch on next: the one leaving the fewest possibilities
// (betterParams), or at once one with no rows left. 0 when no column is
// open.
unsigned int Assembler1::findBestUnclosedColumn(const Links& n) const {
  unsigned int col = n.right[0];

  if (col == 0) {
    return 0;
  }

  // the first column is the best for a start
  unsigned int bestcol = col;
  col = n.right[col];

  while (col) {
    if (betterParams(n.colCount[col], max_[col] - n.weight[col], n.colCount[bestcol],
                     max_[bestcol] - n.weight[bestcol])) {
      bestcol = col;
    }

    if (n.colCount[col] == 0) {
      return col;
    }

    col = n.right[col];
  }

  return bestcol;
}

bool Assembler1::tooManyHoles(const Links& n) const {
  if (holes_ >= holeColumns_.size()) {
    return false;
  }
  unsigned int cnt = holes_;
  for (const unsigned int hc : holeColumns_) {
    if (n.colCount[hc] == 0 && n.weight[hc] == 0) {
      if (cnt == 0) {
        return true;
      }
      cnt--;
    }
  }
  return false;
}

// ---- the plain search ----------------------------------------------------------------

Assembly Assembler1::assemblyFromRows(const std::vector<unsigned int>& rows) const {
  Assembly assembly(puzzle_.getGridType());

  // the placement of each row
  std::vector<const PiecePosition*> placements(rows.size());
  for (std::size_t i = 0; i < rows.size(); i++) {
    placements[i] = &placementOf(rows[i]);
  }

  for (unsigned int pc = 0; pc < problem_.getNumberOfParts(); pc++) {
    unsigned int placed = 0;
    for (const PiecePosition* pp : placements) {
      if (pp->piece == pc) {
        assembly.addPlacement(pp->transformation, pp->x, pp->y, pp->z);
        placed++;
      }
    }

    while (placed < problem_.getPartMaximum(pc)) {
      assembly.addNonPlacement();
      placed++;
    }
  }

  assembly.sort(problem_);

  return assembly;
}

Assembly Assembler1::currentAssembly() const { return assemblyFromRows(stacks_.rows); }

// An assembly found by the plain or the vector search on the caller's
// thread: dropped when a rotation of it is smaller, else handed over.
void Assembler1::solution(SearchRun& run) {
  if (run.hasCallback()) {
    Assembly assembly = currentAssembly();

    if (filter_.rejects(assembly, puzzle_, problem_, strictColors_)) {
      return;
    }

    run.deliver(std::move(assembly));
  }
}

float Assembler1::serialFraction() const {
  if (searchDone()) {
    return 1;
  }

  float erg = 0;

  for (std::size_t r = finishedA_.size(); r-- > 0;) {
    erg += static_cast<float>(finishedA_[r]);
    erg /= static_cast<float>(finishedB_[r]);
  }

  return erg;
}

// The plain search, iteratively: the recursive form of the original
// (assembler_1_c::rec, kept there under #if 0) turned into a state machine
// over explicit stacks, so that it can stop and be saved anywhere. A frame
// is called with a row: 0 or a header means "choose a new column", a row
// node means "try the rows of this column from here". The states of a
// frame:
//   0  entered: check holes, choose a column, or start on its rows
//   1  a column was chosen and closed; back from the deeper frame, reopen it
//   2  the column's condition was met without a row; back, put its rows back
//   3  start on the rows: a separator for the rows hidden on the way
//   4  add the current row and go deeper if the conditions allow
//   5  back from the deeper frame: show the rows hidden for the current row
//   6  take the current row out, hide it for the rest, go on with the next
//   7  all rows tried: show the hidden rows, return
// Only states 1, 2 and 5 can be saved and restored; a stop waits for one.
void Assembler1::iterative(SearchRun& run) {
  Links& n = links_;
  SearchStacks& s = stacks_;
  unsigned int row = 0;
  unsigned int col = 0;
  unsigned int sincePublished = 0;

  while (!s.taskStack.empty()) {
    iterations_.store(iterations_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    if (++sincePublished == 4096) {
      sincePublished = 0;
      publishSerialProgress();
    }

    // a stop waits for a state that can be restored
    const unsigned int state = s.taskStack.back();
    const bool restorable = state == 1 || state == 2 || state == 5;
    if (restorable && run.stopRequested()) {
      break;
    }

    // the debugger
    if (debug_ && restorable) {
      if (debugLoops_ <= 0) {
        break;
      }

      debugLoops_--;
    }

    switch (s.taskStack.back()) {
      case 0:
        // too many holes: return
        if (tooManyHoles(n)) {
          s.nextRowStack.pop_back();
          s.taskStack.pop_back();
          break;
        }

        // called with a header node (0 included): all rows are done, choose
        // a new column
        if (s.nextRowStack.back() < headerNodes_) {
          // no column left: an assembly
          if (n.right[0] == 0) {
            solution(run);
            s.nextRowStack.pop_back();
            s.taskStack.pop_back();
            break;
          }

          // the column with the fewest possibilities, as in the dancing
          // links search
          const unsigned int c = findBestUnclosedColumn(n);

          if (c == 0) {
            s.nextRowStack.pop_back();
            s.taskStack.pop_back();
            break;
          }

          if (n.colCount[c] == 0) {
            // no rows in the column: nothing to choose, on to a new column
            // when its condition is met
            if (columnConditionFulfilled(n, c)) {
              n.coverColumnOnly(c);

              s.columnStack.push_back(c);
              s.taskStack.back() = 1;
              s.taskStack.push_back(0);
              s.nextRowStack.push_back(0);

              break;
            }

          } else {
            // Every column left can be met (each frame leaves only such),
            // so close this one now and find the row sets that meet it;
            // its rows are taken out just before going deeper.
            n.coverColumnOnly(c);

            s.columnStack.push_back(c);
            s.taskStack.back() = 1;
            s.taskStack.push_back(0);
            s.nextRowStack.push_back(n.down[c]);

            break;
          }

          s.nextRowStack.pop_back();
          s.taskStack.pop_back();
          break;
        }

        col = n.colCount[s.nextRowStack.back()];

        // checked before this frame was entered
        BT_ASSERT(columnConditionFulfillable(n, col));

        // The column's condition may already be met without another row:
        // then search on with its remaining rows taken out first.
        if (columnConditionFulfilled(n, col)) {
          finishedB_.push_back(n.colCount[n.colCount[s.nextRowStack.back()]] + 1);
          finishedA_.push_back(0);

          // the column's value must not change any more
          n.coverColumnRows(col);

          if (openColumnConditionsFulfillable(n)) {
            s.taskStack.back() = 2;
            s.taskStack.push_back(0);
            s.nextRowStack.push_back(0);
            break;
          }

          s.taskStack.back() = 2;
          break;
        }

        finishedB_.push_back(n.colCount[n.colCount[s.nextRowStack.back()]]);
        finishedA_.push_back(0);

        s.taskStack.back() = 3;
        break;

      case 1:
        // reopen the column
        n.uncoverColumnOnly(s.columnStack.back());

        s.columnStack.pop_back();
        s.nextRowStack.pop_back();
        s.taskStack.pop_back();
        break;

      case 2:
        // put the column's rows back
        n.uncoverColumnRows(n.colCount[s.nextRowStack.back()]);

        finishedA_.back()++;

        [[fallthrough]];
      case 3:
        // a separator, so that the rows hidden while trying the rows below
        // can be shown again
        s.hiddenRows.push_back(0);

        row = s.nextRowStack.back();

        // up of a row is a smaller node; up of the header is a larger one
        if (n.up[row] >= row) {
          s.taskStack.back() = 7;
          break;
        }
        s.rows.push_back(row);

        [[fallthrough]];
      case 4:
        row = s.rows.back();
        col = n.colCount[s.nextRowStack.back()];

        // add the row to the row set
        n.weight[n.colCount[row]] += n.weight[row];
        for (unsigned int r = n.right[row]; r != row; r = n.right[r]) {
          n.weight[n.colCount[r]] += n.weight[r];
        }

        // with a column that can no longer be met there is nothing to check
        if (openColumnConditionsFulfillable(n)) {
          // the rows that would now overflow a column
          n.hideRows(row, s.hiddenRows, max_);

          if (openColumnConditionsFulfillable(n)) {
            if (n.colCount[col] == 0) {
              // no rows left in this column: on to a new one, when its
              // condition is met
              if (columnConditionFulfilled(n, col)) {
                s.taskStack.back() = 5;
                s.taskStack.push_back(0);
                s.nextRowStack.push_back(0);
                break;
              }

            } else {
              // rows left and the condition can still be met: on with this
              // column (closed when it was chosen, so checked here)
              if (columnConditionFulfillable(n, col)) {
                // Down until a row still in the matrix: a hidden row still
                // points down to the row below it, but that one no longer
                // points back up.
                unsigned int newrow = row;
                while ((n.down[newrow] >= headerNodes_) && n.up[n.down[newrow]] != newrow) {
                  newrow = n.down[newrow];
                }

                s.taskStack.back() = 5;
                s.taskStack.push_back(0);
                s.nextRowStack.push_back(newrow);
                break;
              }
            }
          }

        } else {
          s.taskStack.back() = 6;
          break;
        }

        [[fallthrough]];
      case 5:
        n.unhideRows(s.hiddenRows);

        [[fallthrough]];
      case 6:
        // take the row out of the row set
        row = s.rows.back();

        for (unsigned int r = n.left[row]; r != row; r = n.left[r]) {
          n.weight[n.colCount[r]] -= n.weight[r];
        }
        n.weight[n.colCount[row]] -= n.weight[row];

        s.rows.pop_back();

        finishedA_.back()++;

        // done with this row: it is not used again here
        n.hideRow(row);
        s.hiddenRows.push_back(row);

        row = n.down[row];

        if (n.up[row] < row) {
          s.rows.push_back(row);
          s.taskStack.back() = 4;
          break;
        }

        [[fallthrough]];
      case 7:
        // show the rows hidden while trying the rows of this column
        n.unhideRows(s.hiddenRows);

        finishedA_.pop_back();
        finishedB_.pop_back();

        s.nextRowStack.pop_back();
        s.taskStack.pop_back();
        break;

      default:
        BT_ASSERT_MSG(false, "assembler 1: a state that does not exist");
        break;
    }
  }
}

// ---- the search split over threads -----------------------------------------------

// One thread's search of tasks: a copy of the matrix it re-seeds from the
// run's base for every task (assemblerWorker_1).
class Assembler1::Worker {
 public:
  Worker(Assembler1& parent, SearchRun& run) : parent_(parent), run_(run) {
    const std::size_t h = parent.headerNodes_;
    s_.rows.reserve(h);
    s_.hiddenRows.reserve(h * 4);
    s_.taskStack.reserve(h);
    s_.nextRowStack.reserve(h);
    s_.columnStack.reserve(h);
  }

  /// Searches task `taskIndex` to its end or to a stop.
  void operator()(std::size_t taskIndex) {
    const SearchStacks& task = parent_.split_.tasks[taskIndex];
    // the base is written once in assemble() and never touched while the
    // workers run
    n_ = parent_.base_;
    s_ = task;
    parent_.replay(n_, s_, true);
    search(task.taskStack.size());
    flushIterations();
  }

 private:
  // The plain search's state machine below `baseDepth`, without the debugger
  // and the progress stacks, stopping at once.
  void search(std::size_t baseDepth) {
    const Assembler1& p = parent_;
    Links& n = n_;
    SearchStacks& s = s_;
    unsigned int row = 0;
    unsigned int col = 0;

    while (s.taskStack.size() >= baseDepth && !run_.stopRequested()) {
      localIterations_++;
      if ((localIterations_ - flushedIterations_) >= 128) {
        flushIterations();
      }

      switch (s.taskStack.back()) {
        case 0:
          if (p.tooManyHoles(n)) {
            s.nextRowStack.pop_back();
            s.taskStack.pop_back();
            break;
          }

          if (s.nextRowStack.back() < p.headerNodes_) {
            if (n.right[0] == 0) {
              parent_.reportParallelSolution(run_, s.rows);
              s.nextRowStack.pop_back();
              s.taskStack.pop_back();
              break;
            }

            const unsigned int c = p.findBestUnclosedColumn(n);
            if (c == 0) {
              s.nextRowStack.pop_back();
              s.taskStack.pop_back();
              break;
            }

            if (n.colCount[c] == 0) {
              if (p.columnConditionFulfilled(n, c)) {
                n.coverColumnOnly(c);
                s.columnStack.push_back(c);
                s.taskStack.back() = 1;
                s.taskStack.push_back(0);
                s.nextRowStack.push_back(0);
                break;
              }
            } else {
              n.coverColumnOnly(c);
              s.columnStack.push_back(c);
              s.taskStack.back() = 1;
              s.taskStack.push_back(0);
              s.nextRowStack.push_back(n.down[c]);
              break;
            }

            s.nextRowStack.pop_back();
            s.taskStack.pop_back();
            break;
          }

          col = n.colCount[s.nextRowStack.back()];
          BT_ASSERT(p.columnConditionFulfillable(n, col));

          if (p.columnConditionFulfilled(n, col)) {
            n.coverColumnRows(col);
            if (p.openColumnConditionsFulfillable(n)) {
              s.taskStack.back() = 2;
              s.taskStack.push_back(0);
              s.nextRowStack.push_back(0);
              break;
            }
            s.taskStack.back() = 2;
            break;
          }

          s.taskStack.back() = 3;
          break;

        case 1:
          n.uncoverColumnOnly(s.columnStack.back());
          s.columnStack.pop_back();
          s.nextRowStack.pop_back();
          s.taskStack.pop_back();
          break;

        case 2:
          n.uncoverColumnRows(n.colCount[s.nextRowStack.back()]);
          [[fallthrough]];

        case 3:
          s.hiddenRows.push_back(0);
          row = s.nextRowStack.back();
          if (n.up[row] >= row) {
            s.taskStack.back() = 7;
            break;
          }
          s.rows.push_back(row);
          [[fallthrough]];

        case 4:
          row = s.rows.back();
          col = n.colCount[s.nextRowStack.back()];
          n.weight[n.colCount[row]] += n.weight[row];
          for (unsigned int r = n.right[row]; r != row; r = n.right[r]) {
            n.weight[n.colCount[r]] += n.weight[r];
          }

          if (p.openColumnConditionsFulfillable(n)) {
            n.hideRows(row, s.hiddenRows, p.max_);
            if (p.openColumnConditionsFulfillable(n)) {
              if (n.colCount[col] == 0) {
                if (p.columnConditionFulfilled(n, col)) {
                  s.taskStack.back() = 5;
                  s.taskStack.push_back(0);
                  s.nextRowStack.push_back(0);
                  break;
                }
              } else {
                if (p.columnConditionFulfillable(n, col)) {
                  unsigned int newrow = row;
                  while ((n.down[newrow] >= p.headerNodes_) && n.up[n.down[newrow]] != newrow) {
                    newrow = n.down[newrow];
                  }
                  s.taskStack.back() = 5;
                  s.taskStack.push_back(0);
                  s.nextRowStack.push_back(newrow);
                  break;
                }
              }
            }
          } else {
            s.taskStack.back() = 6;
            break;
          }
          [[fallthrough]];

        case 5:
          n.unhideRows(s.hiddenRows);
          [[fallthrough]];

        case 6:
          row = s.rows.back();
          for (unsigned int r = n.left[row]; r != row; r = n.left[r]) {
            n.weight[n.colCount[r]] -= n.weight[r];
          }
          n.weight[n.colCount[row]] -= n.weight[row];
          s.rows.pop_back();

          n.hideRow(row);
          s.hiddenRows.push_back(row);

          row = n.down[row];
          if (n.up[row] < row) {
            s.rows.push_back(row);
            s.taskStack.back() = 4;
            break;
          }
          [[fallthrough]];

        case 7:
          n.unhideRows(s.hiddenRows);
          s.nextRowStack.pop_back();
          s.taskStack.pop_back();
          break;

        default:
          BT_ASSERT_MSG(false, "assembler 1: a state that does not exist");
          break;
      }
    }
  }

  void flushIterations() {
    const std::uint64_t unflushed = localIterations_ - flushedIterations_;
    if (unflushed > 0) {
      parent_.iterations_.fetch_add(unflushed, std::memory_order_relaxed);
      flushedIterations_ = localIterations_;
    }
  }

  Assembler1& parent_;
  SearchRun& run_;
  /// This thread's copy of the matrix and where its search stands.
  Links n_;
  SearchStacks s_;
  std::uint64_t localIterations_ = 0;
  std::uint64_t flushedIterations_ = 0;
};

// An assembly found by a search split over threads: built from the rows
// placed (in any order), checked against its rotated copies and against
// what was reported before, and handed over. Called by the worker threads,
// the plain ones and the vector ones alike.
void Assembler1::reportParallelSolution(SearchRun& run,
                                        const std::vector<unsigned int>& solutionRows) {
  if (!run.hasCallback()) {
    return;
  }

  Assembly assembly = assemblyFromRows(solutionRows);

  if (filter_.rejects(assembly, puzzle_, problem_, strictColors_)) {
    return;
  }

  // What tells this assembly from the others, to report it once only. It is
  // taken from the sorted assembly, so that it is the same whatever order
  // the rows were found in: the plain and the vector search place them in
  // different orders, and so may two tasks that both hold it. A search
  // loaded from a file of an older version (rowOrderSignatures_) has
  // signatures taken from the rows in the order of the plain search, and
  // goes on with those.
  std::uint64_t sig = kFnvOffset;
  if (rowOrderSignatures_) {
    for (const unsigned int row : solutionRows) {
      const PiecePosition& pp = placementOf(row);
      sig = fnvMix(sig, pp.piece);
      sig = fnvMix(sig, pp.transformation);
      sig = fnvMix(sig, static_cast<std::uint32_t>(pp.x));
      sig = fnvMix(sig, static_cast<std::uint32_t>(pp.y));
      sig = fnvMix(sig, static_cast<std::uint32_t>(pp.z));
    }
  } else {
    for (const Placement& pl : assembly.getPlacements()) {
      if (pl.transformation != kUnplacedTrans) {
        sig = fnvMix(sig, pl.transformation);
        sig = fnvMix(sig, static_cast<std::uint32_t>(pl.xpos));
        sig = fnvMix(sig, static_cast<std::uint32_t>(pl.ypos));
        sig = fnvMix(sig, static_cast<std::uint32_t>(pl.zpos));
      } else {
        sig = fnvMix(sig, 0xFF);
      }
    }
  }

  run.deliverOnce(std::move(assembly), sig, split_.reported);
}

// The tasks of a split search: the plain search from its start, cut off
// `cutoffDepth` rows deep; each frame reached there (and each assembly
// found above it) becomes a task with the stacks as they stand. Works on the
// matrix and the stacks of this assembler and leaves both at the start.
void Assembler1::generateTasksAtDepth(SearchRun& run, unsigned int cutoffDepth,
                                      std::vector<SearchStacks>& tasks) {
  Links& n = links_;
  SearchStacks& s = stacks_;
  tasks.clear();

  n = base_;
  s = SearchStacks{};
  s.taskStack = {0};
  s.nextRowStack = {0};

  unsigned int row = 0;
  unsigned int col = 0;

  while (!s.taskStack.empty() && !run.stopRequested()) {
    iterations_.fetch_add(1, std::memory_order_relaxed);

    switch (s.taskStack.back()) {
      case 0:
        if (tooManyHoles(n)) {
          s.nextRowStack.pop_back();
          s.taskStack.pop_back();
          break;
        }

        if (s.nextRowStack.back() < headerNodes_ && n.right[0] == 0) {
          tasks.push_back(s);

          s.nextRowStack.pop_back();
          s.taskStack.pop_back();
          break;
        }

        if (s.rows.size() >= cutoffDepth) {
          tasks.push_back(s);

          s.nextRowStack.pop_back();
          s.taskStack.pop_back();
          break;
        }

        if (s.nextRowStack.back() < headerNodes_) {
          const unsigned int c = findBestUnclosedColumn(n);
          if (c == 0) {
            s.nextRowStack.pop_back();
            s.taskStack.pop_back();
            break;
          }

          if (n.colCount[c] == 0) {
            if (columnConditionFulfilled(n, c)) {
              n.coverColumnOnly(c);
              s.columnStack.push_back(c);
              s.taskStack.back() = 1;
              s.taskStack.push_back(0);
              s.nextRowStack.push_back(0);
              break;
            }
          } else {
            n.coverColumnOnly(c);
            s.columnStack.push_back(c);
            s.taskStack.back() = 1;
            s.taskStack.push_back(0);
            s.nextRowStack.push_back(n.down[c]);
            break;
          }

          s.nextRowStack.pop_back();
          s.taskStack.pop_back();
          break;
        }

        col = n.colCount[s.nextRowStack.back()];
        BT_ASSERT(columnConditionFulfillable(n, col));

        if (columnConditionFulfilled(n, col)) {
          n.coverColumnRows(col);
          if (openColumnConditionsFulfillable(n)) {
            s.taskStack.back() = 2;
            s.taskStack.push_back(0);
            s.nextRowStack.push_back(0);
            break;
          }
          s.taskStack.back() = 2;
          break;
        }

        s.taskStack.back() = 3;
        break;

      case 1:
        n.uncoverColumnOnly(s.columnStack.back());
        s.columnStack.pop_back();
        s.nextRowStack.pop_back();
        s.taskStack.pop_back();
        break;

      case 2:
        n.uncoverColumnRows(n.colCount[s.nextRowStack.back()]);
        [[fallthrough]];

      case 3:
        s.hiddenRows.push_back(0);
        row = s.nextRowStack.back();
        if (n.up[row] >= row) {
          s.taskStack.back() = 7;
          break;
        }
        s.rows.push_back(row);
        [[fallthrough]];

      case 4:
        row = s.rows.back();
        col = n.colCount[s.nextRowStack.back()];
        n.weight[n.colCount[row]] += n.weight[row];
        for (unsigned int r = n.right[row]; r != row; r = n.right[r]) {
          n.weight[n.colCount[r]] += n.weight[r];
        }

        if (openColumnConditionsFulfillable(n)) {
          n.hideRows(row, s.hiddenRows, max_);
          if (openColumnConditionsFulfillable(n)) {
            if (n.colCount[col] == 0) {
              if (columnConditionFulfilled(n, col)) {
                s.taskStack.back() = 5;
                s.taskStack.push_back(0);
                s.nextRowStack.push_back(0);
                break;
              }
            } else {
              if (columnConditionFulfillable(n, col)) {
                unsigned int newrow = row;
                while ((n.down[newrow] >= headerNodes_) && n.up[n.down[newrow]] != newrow) {
                  newrow = n.down[newrow];
                }
                s.taskStack.back() = 5;
                s.taskStack.push_back(0);
                s.nextRowStack.push_back(newrow);
                break;
              }
            }
          }
        } else {
          s.taskStack.back() = 6;
          break;
        }
        [[fallthrough]];

      case 5:
        n.unhideRows(s.hiddenRows);
        [[fallthrough]];

      case 6:
        row = s.rows.back();
        for (unsigned int r = n.left[row]; r != row; r = n.left[r]) {
          n.weight[n.colCount[r]] -= n.weight[r];
        }
        n.weight[n.colCount[row]] -= n.weight[row];
        s.rows.pop_back();

        n.hideRow(row);
        s.hiddenRows.push_back(row);

        row = n.down[row];
        if (n.up[row] < row) {
          s.rows.push_back(row);
          s.taskStack.back() = 4;
          break;
        }
        [[fallthrough]];

      case 7:
        n.unhideRows(s.hiddenRows);
        s.nextRowStack.pop_back();
        s.taskStack.pop_back();
        break;

      default:
        BT_ASSERT_MSG(false, "assembler 1: a state that does not exist");
        break;
    }
  }

  n = base_;
  s = SearchStacks{};
  s.taskStack = {0};
  s.nextRowStack = {0};
}

void Assembler1::generateSubtreeTasks(SearchRun& run, std::vector<SearchStacks>& tasks,
                                      unsigned int targetTasks, unsigned int maxDepth) {
  tasks.clear();
  unsigned int cutoffDepth = 1;
  if (maxDepth == 0) {
    maxDepth = 1;
  }

  while (cutoffDepth <= maxDepth && !run.stopRequested()) {
    generateTasksAtDepth(run, cutoffDepth, tasks);
    if (tasks.size() >= targetTasks || tasks.empty()) {
      break;
    }
    if (cutoffDepth == maxDepth) {
      break;
    }
    cutoffDepth++;
  }

  // A stop part way through leaves only some of the subtrees. Kept, the
  // next run would take them for the whole search and finish early with
  // most assemblies missing; empty, it makes them again.
  if (run.stopRequested()) {
    tasks.clear();
  }
}

void Assembler1::parallelMultiSearch(SearchRun& run, unsigned int workers,
                                     const SearchOptions& options) {
  // the workers read the shapes (smallerRotationExists): their lazy caches
  // must be filled before any worker exists
  warmShapeCaches(puzzle_, problem_);

  if (split_.tasks.empty()) {
    const unsigned int targetTasks = std::max(16U, workers * 4);
    const unsigned int maxDepth = std::min(piecenumber_, 3U);
    generateSubtreeTasks(run, split_.tasks, targetTasks, maxDepth);
    split_.completed.assign(split_.tasks.size(), 0);
  }

  // Also for the tasks a stopped run left, in this session or a loaded
  // file: assemble() has cleared the counts, and without them the whole run
  // would show nothing done.
  taskProgress_.begin({}, split_.completed);

  // nothing to split: the search is through (or was stopped while the
  // tasks were made, and makes them again)
  const bool allDone = std::find(split_.completed.begin(), split_.completed.end(), 0) ==
                       split_.completed.end();
  if (run.stopRequested() || allDone) {
    if (!run.stopRequested()) {
      // The original only reported this as finished through its progress;
      // the search is through, so its stacks say so too.
      taskProgress_.markAllDone();
      stacks_.nextRowStack.clear();
      stacks_.taskStack.clear();
      split_.clearTasks();
      split_.interrupted = false;
      rowOrderSignatures_ = false;
    }
    return;
  }

  // When the matrix fits, every thread searches its tasks with the vector
  // search: one solver, read only, shared by all of them. A task hands it
  // the rows placed and the rows hidden so far. What it then finds is every
  // assembly the plain search would find below the task, and now and then
  // one more that belongs to a sister task (the plain search can also close
  // a column early, which the two lists do not say); the signatures see to
  // it that such a one is reported once. The tasks, the list of the
  // finished ones and the signatures stay what they are, and with them
  // stopping, continuing and saving.
  std::unique_ptr<SimdHuangCoverBase> simdSolver;
  if (simdFits(options) && !rowOrderSignatures_) {
    simdSolver = createSimdSolver(options.kernels);
  }
  const SimdHuangCoverBase* simd = simdSolver.get();

  struct TaskSearch {
    Assembler1* self;
    SearchRun* run;
    const SimdHuangCoverBase* simd;
    unsigned int slot;
    std::unique_ptr<Worker> plain;

    void operator()(std::size_t task) {
      if (plain) {
        (*plain)(task);
        return;
      }

      const SearchStacks& t = self->split_.tasks[task];
      std::atomic<std::uint64_t> taskIter{0};
      auto drain = [this, &taskIter] {
        self->iterations_.fetch_add(taskIter.exchange(0, std::memory_order_relaxed),
                                    std::memory_order_relaxed);
      };
      simd->solveSubtree(
          t.rows, t.hiddenRows,
          [this, &drain](const std::vector<unsigned int>& nodes) -> bool {
            drain();
            self->reportParallelSolution(*run, nodes);
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
             TaskSearch s{this, &run, simd, slot, nullptr};
             if (simd == nullptr) {
               s.plain = std::make_unique<Worker>(*this, run);
             }
             return s;
           });

  if (!run.stopRequested()) {
    stacks_.nextRowStack.clear();
    stacks_.taskStack.clear();
    split_.clearTasks();
    split_.interrupted = false;
    rowOrderSignatures_ = false;
  } else {
    // Stopped part way. Continuing in this session is fine: the tasks, their
    // completion and the signatures are all still here; a saved position
    // carries all three (flag 4, or 2 for the older kind of signatures).
    split_.interrupted = true;
  }
}

// ---- the vector search -----------------------------------------------------------

// Whether the matrix can go to the vector search at all.
bool Assembler1::simdFits(const SearchOptions& options) const {
  if (!options.vectorSearch) {
    return false;
  }
  if (debug_) {
    return false;
  }

  if (headerNodes_ - 1 > 32768) {
    return false;
  }

  // Memory. Each row of the vector search holds a mask as wide as the
  // solver's size class (4 KB at 32768 columns), so a puzzle with many
  // columns and many placements would take hundreds of megabytes where the
  // plain search gets by on tens. The vector search only ever buys speed,
  // so past the budget the plain one is used.
  {
    const Links& n = links_;
    std::uint64_t estRows = 0;
    std::uint64_t estNodes = 0;
    const unsigned int nShapes = problem_.getNumberOfParts();
    for (unsigned int s = 1; s <= nShapes; s++) {
      for (unsigned int r = n.down[s]; r != s; r = n.down[r]) {
        estRows++;
        estNodes++;
        for (unsigned int c = n.right[r]; c != r; c = n.right[c]) {
          estNodes++;
        }
      }
    }
    if (!simdHuangFitsMemory(headerNodes_ - 1, estRows, estNodes, options.vectorMemoryBudget)) {
      return false;
    }
  }

  // Puzzles with a piece-count range are not kept out: the vector search has
  // the range column. Variable voxels together with ranges are.
  const unsigned int resVari = problem_.resultShape(puzzle_).countState(Voxel::kVariable);

  if (resVari > 0) {
    for (unsigned int s = 0; s < problem_.getNumberOfParts(); s++) {
      if (problem_.getPartMinimum(s) != problem_.getPartMaximum(s)) {
        return false;
      }
    }
  }

  return true;
}

bool Assembler1::canUseSimd(const SearchOptions& options) const {
  return atRoot() && simdFits(options);
}

std::unique_ptr<SimdHuangCoverBase> Assembler1::createSimdSolver(SimdKernels kernels) const {
  const Links& n = links_;
  const Voxel& result = problem_.resultShape(puzzle_);
  const unsigned int numCols = headerNodes_ - 1;
  const unsigned int numShapes = problem_.getNumberOfParts();
  const unsigned int resFilled = result.countState(Voxel::kFilled);
  const unsigned int resVari = result.countState(Voxel::kVariable);
  const bool hasRange = (numCols == (numShapes + resFilled + resVari + 1));
  const unsigned int rangeColumn = hasRange ? numCols : 0;

  std::unique_ptr<SimdHuangCoverBase> solver = makeSimdHuangCover(numCols, numShapes, kernels);
  solver->setHoles(holes_);

  // Only the columns still in the header list, not 1..numCols: reduce() ends
  // in clumpify(), which drops every column covered by the same rows with
  // the same weights as an earlier one, and removeColumn() takes those
  // columns' nodes out of the rows with it. The solver has to see the
  // matrix the rows describe: a column no row reaches is a condition that
  // cannot be met, and the search would end at the root with no assembly.
  for (unsigned int c = n.right[0]; c != 0; c = n.right[c]) {
    const bool isShape = (c <= numShapes);
    const bool isRange = (hasRange && c == rangeColumn);
    const bool isVoxel = (!isShape && !isRange);
    const bool isHole =
        std::find(holeColumns_.begin(), holeColumns_.end(), c) != holeColumns_.end();
    solver->setColumnBounds(c, min_[c], max_[c], isVoxel, isShape, isRange, isHole);
  }

  for (unsigned int pc = 0; pc < numShapes; pc++) {
    const unsigned int shapeCol = pc + 1;
    unsigned int shapeRowIdx = 0;
    for (unsigned int r = n.down[shapeCol]; r != shapeCol; r = n.down[r]) {
      std::vector<unsigned int> cols;
      std::vector<unsigned int> weights;
      std::vector<unsigned int> nodesInRow;
      unsigned int curr = r;
      unsigned int rangeW = 0;
      do {
        nodesInRow.push_back(curr);
        const unsigned int col = n.colCount[curr];
        const unsigned int w = n.weight[curr];
        cols.push_back(col);
        weights.push_back(w);
        if (hasRange && col == rangeColumn) {
          rangeW = w;
        }
        curr = n.right[curr];
      } while (curr != r);

      const std::uint32_t rowIdx =
          solver->addRow(r, pc, shapeCol, shapeRowIdx++, rangeW, cols, weights);
      for (const unsigned int node : nodesInRow) {
        solver->registerNodeAlias(node, rowIdx);
      }
    }
  }

  return solver;
}

void Assembler1::simdSearch(SearchRun& run, const SearchOptions& options) {
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
        stacks_.rows = solutionNodes;
        solution(run);
        return !run.stopRequested();
      },
      run.token(), simdIter,
      [this, &drain](float fraction) {
        drain();
        taskProgress_.report(0, 0, fraction);
      });

  drain();

  if (run.stopRequested()) {
    // the rows held the last solution, not a place in the matrix: back to
    // the start, passing over what was reported on the next run
    stacks_.rows.clear();
    split_.vectorSkip = std::max(split_.vectorSkip, split_.vectorDone);
    split_.interrupted = true;
  } else {
    taskProgress_.finish(0, 0);
    split_.vectorSkip = 0;
    split_.interrupted = false;
    // as the other searches leave it when they are through; the rows held
    // the last solution, which a saved position must not carry (no restore
    // accepts rows without frames)
    stacks_.nextRowStack.clear();
    stacks_.taskStack.clear();
    stacks_.rows.clear();
  }
}

// ---- runs, progress and positions --------------------------------------------------

RunEnd Assembler1::assemble(const AssemblyCallback& callback, const SearchOptions& options,
                            const StopToken& stop) {
  BT_ASSERT_MSG(errorsState_ == AssemblerError::None, "assemble() after a failed createMatrix()");

  SearchRun run(stop, callback);
  debug_ = false;

  // a previous split run leaves its tasks all done, which would make
  // finished() report 1 for this run before it has done anything
  taskProgress_.reset();

  base_ = links_;

  // run, when there is something to do
  if (!searchDone()) {
    const unsigned int threads = effectiveThreads(options.threads, options.pool);

    // A stopped search carries on the way it ran: the vector search passing
    // over what it reported (also when this run asks for no vector search,
    // or for less memory: the plain search cannot skip), the split search
    // with its remaining tasks, whatever the thread count.
    SearchOptions vectorAnyway = options;
    vectorAnyway.vectorSearch = true;
    vectorAnyway.vectorMemoryBudget = std::numeric_limits<std::uint64_t>::max();
    if (split_.vectorSkip > 0 && canUseSimd(vectorAnyway)) {
      simdSearch(run, vectorAnyway);
    } else if (atRoot() && (threads > 1 || !split_.tasks.empty())) {
      parallelMultiSearch(run, threads, options);
    } else if (canUseSimd(options)) {
      simdSearch(run, options);
    } else {
      // What a save writes from now on is the plain search's position. A
      // vector search stopped before it reported anything left the flag of
      // a stopped vector search behind; with it, the save would be a flag 3
      // position away from the start, which no restore accepts (the
      // original's bug too).
      split_.interrupted = false;
      iterative(run);
    }
  }

  publishSerialProgress();
  return searchDone() ? RunEnd::Finished : RunEnd::Stopped;
}

float Assembler1::finished() const {
  if (taskProgress_.active()) {
    return taskProgress_.fraction();
  }
  return serialProgress_.load(std::memory_order_relaxed);
}

void Assembler1::replay(Links& n, const SearchStacks& s, bool task) const {
  std::size_t columnPos = 0;
  std::size_t hiddenPos = 0;
  std::size_t rowPos = 0;

  const std::size_t end = task ? s.taskStack.size() - 1 : s.taskStack.size();
  for (std::size_t i = 0; i < end; i++) {
    switch (s.taskStack[i]) {
      case 0:
        // the step not started yet, as at the root: nothing to replay
        break;
      case 1:
        n.coverColumnOnly(s.columnStack[columnPos++]);
        break;
      case 2:
        n.coverColumnRows(s.columnStack[columnPos - 1]);
        break;
      case 5: {
        hiddenPos++;
        while (hiddenPos < s.hiddenRows.size() && s.hiddenRows[hiddenPos] > 0) {
          n.hideRow(s.hiddenRows[hiddenPos++]);
        }

        // add the row to the row set
        const unsigned int r = s.rows[rowPos++];
        n.weight[n.colCount[r]] += n.weight[r];
        for (unsigned int rc = n.right[r]; rc != r; rc = n.right[rc]) {
          n.weight[n.colCount[rc]] += n.weight[rc];
        }

        hiddenPos++;
        while (hiddenPos < s.hiddenRows.size() && s.hiddenRows[hiddenPos] > 0) {
          n.hideRow(s.hiddenRows[hiddenPos++]);
        }
        break;
      }
      default:
        BT_ASSERT_MSG(false, "assembler 1: a state that cannot be replayed");
        break;
    }
  }
}

// A replay from a damaged file may name a column or a row twice: covered or
// hidden a second time, the links would no longer be lists, and the search
// would go round in them for ever. With only removals made, a node is still
// in its list exactly when its neighbours point back at it.
bool Assembler1::replayChecked(Links& n, const SearchStacks& s, bool task,
                               std::vector<ReplayStep>& done) const {
  auto linked = [&n](unsigned int x) { return n.down[n.up[x]] == x && n.up[n.down[x]] == x; };
  auto hide = [&n, &done, &linked](unsigned int r) {
    if (!linked(r)) {
      return false;
    }
    for (unsigned int x = n.right[r]; x != r; x = n.right[x]) {
      if (!linked(x)) {
        return false;
      }
    }
    n.hideRow(r);
    done.push_back({ReplayStep::Kind::HideRow, r});
    return true;
  };
  std::vector<bool> rowsCovered(headerNodes_, false);

  std::size_t columnPos = 0;
  std::size_t hiddenPos = 0;
  std::size_t rowPos = 0;

  const std::size_t end = task ? s.taskStack.size() - 1 : s.taskStack.size();
  for (std::size_t i = 0; i < end; i++) {
    switch (s.taskStack[i]) {
      case 0:
        break;
      case 1: {
        const unsigned int c = s.columnStack[columnPos++];
        if (n.right[n.left[c]] != c || n.left[n.right[c]] != c) {
          return false;
        }
        n.coverColumnOnly(c);
        done.push_back({ReplayStep::Kind::CloseColumn, c});
        break;
      }
      case 2: {
        const unsigned int c = s.columnStack[columnPos - 1];
        if (rowsCovered[c]) {
          return false;
        }
        rowsCovered[c] = true;
        n.coverColumnRows(c);
        done.push_back({ReplayStep::Kind::CoverRows, c});
        break;
      }
      case 5: {
        hiddenPos++;
        while (hiddenPos < s.hiddenRows.size() && s.hiddenRows[hiddenPos] > 0) {
          if (!hide(s.hiddenRows[hiddenPos++])) {
            return false;
          }
        }

        const unsigned int r = s.rows[rowPos++];
        n.weight[n.colCount[r]] += n.weight[r];
        for (unsigned int rc = n.right[r]; rc != r; rc = n.right[rc]) {
          n.weight[n.colCount[rc]] += n.weight[rc];
        }
        done.push_back({ReplayStep::Kind::AddRow, r});

        hiddenPos++;
        while (hiddenPos < s.hiddenRows.size() && s.hiddenRows[hiddenPos] > 0) {
          if (!hide(s.hiddenRows[hiddenPos++])) {
            return false;
          }
        }
        break;
      }
      default:
        return false;
    }
  }
  return true;
}

void Assembler1::undoReplay(Links& n, std::vector<ReplayStep>& done) {
  while (!done.empty()) {
    const ReplayStep step = done.back();
    done.pop_back();
    switch (step.kind) {
      case ReplayStep::Kind::CloseColumn:
        n.uncoverColumnOnly(step.node);
        break;
      case ReplayStep::Kind::CoverRows:
        n.uncoverColumnRows(step.node);
        break;
      case ReplayStep::Kind::HideRow:
        n.unhideRow(step.node);
        break;
      case ReplayStep::Kind::AddRow: {
        const unsigned int r = step.node;
        for (unsigned int rc = n.left[r]; rc != r; rc = n.left[rc]) {
          n.weight[n.colCount[rc]] -= n.weight[rc];
        }
        n.weight[n.colCount[r]] -= n.weight[r];
        break;
      }
    }
  }
}

// Walks the stacks as replay() and the search would, without touching the
// matrix: every number must be a node, a column or a row where it is used
// as one, and every stack must hold what the frames on taskStack put there.
bool Assembler1::validStacks(const SearchStacks& s, bool task) const {
  const std::size_t nodes = links_.left.size();
  if (s.nextRowStack.size() != s.taskStack.size() || (task && s.taskStack.empty())) {
    return false;
  }
  for (const unsigned int v : s.nextRowStack) {
    if (v >= nodes) {
      return false;
    }
  }
  for (const unsigned int v : s.columnStack) {
    if (v == 0 || v >= headerNodes_) {
      return false;
    }
  }
  for (const unsigned int v : s.rows) {
    if (v < headerNodes_ || v >= nodes) {
      return false;
    }
  }
  for (const unsigned int v : s.hiddenRows) {
    if (v != 0 && (v < headerNodes_ || v >= nodes)) {
      return false;
    }
  }

  std::size_t columnPos = 0;
  std::size_t hiddenPos = 0;
  std::size_t rowPos = 0;
  // A frame past its column choice stands on a row of the column the last
  // state 1 frame below it closed (the search, the vector search and the
  // task generation all keep to this; the replay and the search read the
  // column from that row).
  auto rowOfColumn = [this, &s](unsigned int row, std::size_t columns) {
    return columns > 0 && row >= headerNodes_ &&
           links_.colCount[row] == s.columnStack[columns - 1];
  };
  // a separator, then rows up to the next one
  auto batch = [&s, &hiddenPos] {
    if (hiddenPos >= s.hiddenRows.size() || s.hiddenRows[hiddenPos] != 0) {
      return false;
    }
    hiddenPos++;
    while (hiddenPos < s.hiddenRows.size() && s.hiddenRows[hiddenPos] > 0) {
      hiddenPos++;
    }
    return true;
  };

  for (std::size_t i = 0; i < s.taskStack.size(); i++) {
    const bool last = i + 1 == s.taskStack.size();
    switch (s.taskStack[i]) {
      case 0:
        // only the frame not begun yet: at the top of the stack
        if (!last) {
          return false;
        }
        break;
      case 1:
        if (columnPos >= s.columnStack.size()) {
          return false;
        }
        columnPos++;
        break;
      case 2:
        // a frame on a row of the column the last state 1 frame closed
        if (!rowOfColumn(s.nextRowStack[i], columnPos)) {
          return false;
        }
        break;
      case 5:
        // the same, and the row it took is one of that column's too
        if (!rowOfColumn(s.nextRowStack[i], columnPos) || !batch() ||
            rowPos >= s.rows.size() || !rowOfColumn(s.rows[rowPos], columnPos)) {
          return false;
        }
        rowPos++;
        if (!batch()) {
          return false;
        }
        break;
      default:
        return false;
    }
    // a frame not begun goes on with a row of that column, or chooses one
    if (s.taskStack[i] == 0 && s.nextRowStack[i] >= headerNodes_ &&
        !rowOfColumn(s.nextRowStack[i], columnPos)) {
      return false;
    }
    if (task && last && s.taskStack[i] != 0) {
      return false;
    }
  }

  return columnPos == s.columnStack.size() && rowPos == s.rows.size() &&
         hiddenPos == s.hiddenRows.size();
}

AssemblerError Assembler1::restorePosition(std::string_view text, std::string_view version) {
  // As the original, the version is not looked at.
  (void)version;

  const std::string str(text);  // NUL-terminated for the number readers
  const char* string = str.c_str();
  const auto len = static_cast<unsigned int>(str.size());
  unsigned int pos = 0;
  bool ok = true;

  // The leading flag: 0 a position of the plain search, 4 a stopped split
  // search with its tasks and the assemblies it reported, 3 a stopped vector
  // search with how many it reported. 2, from older versions, is 4 with the
  // signatures of the assemblies taken from the rows in the order the plain
  // search placed them (see reportParallelSolution). 1, older still, is a
  // stopped split search saved without tasks and signatures, which cannot
  // be continued.
  unsigned int interrupted = 0;
  pos += getInt(string + pos, &interrupted);
  if (pos >= len) {
    return AssemblerError::CannotRestoreSyntax;
  }
  if (interrupted == 1) {
    return AssemblerError::CannotRestoreInterrupted;
  }
  if (interrupted > 4) {
    return AssemblerError::CannotRestoreSyntax;
  }

  SearchStacks s;
  std::vector<unsigned int> finishedA;
  std::vector<unsigned int> finishedB;
  for (std::vector<unsigned int>* v : {&s.rows, &s.taskStack, &s.nextRowStack, &s.columnStack,
                                       &s.hiddenRows, &finishedA}) {
    pos += stringToVector(string + pos, len - pos, *v, ok);
    if (!ok || pos >= len) {
      return AssemblerError::CannotRestoreSyntax;
    }
  }
  pos += stringToVector(string + pos, len - pos, finishedB, ok);
  if (!ok) {
    return AssemblerError::CannotRestoreSyntax;
  }

  SplitSearchState<SearchStacks> tail;
  if (interrupted >= 2) {
    std::size_t done = 0;
    if (!readResumeTail(string + pos, interrupted, tail, done,
                        [](ResumeReader& r, SearchStacks& t) {
                          for (std::vector<unsigned int>* v :
                               {&t.taskStack, &t.nextRowStack, &t.columnStack, &t.rows,
                                &t.hiddenRows}) {
                            unsigned long long k = 0;
                            if (!r.next(k) || k > r.remaining()) {
                              return false;
                            }
                            v->resize(static_cast<std::size_t>(k));
                            for (unsigned int& x : *v) {
                              if (!r.next(x)) {
                                return false;
                              }
                            }
                          }
                          return true;
                        })) {
      return AssemblerError::CannotRestoreSyntax;
    }
    tail.interrupted = true;
  }

  // Checked before anything is used (the original replayed what it read):
  // the stacks fit the matrix and each other, the levels have their
  // progress entries, and a stopped split or vector search stands at the
  // start, with tasks that fit too.
  if (!validStacks(s, false) || finishedA.size() != finishedB.size()) {
    return AssemblerError::CannotRestoreSyntax;
  }
  std::size_t levels = 0;
  for (const unsigned int state : s.taskStack) {
    levels += state >= 2 ? 1 : 0;
  }
  if (finishedA.size() != levels) {
    return AssemblerError::CannotRestoreSyntax;
  }
  if (interrupted >= 2) {
    const bool root = s.taskStack.size() == 1 && s.taskStack[0] == 0 && s.rows.empty() &&
                      s.columnStack.empty() && s.hiddenRows.empty();
    if (!root) {
      return AssemblerError::CannotRestoreSyntax;
    }
    for (const SearchStacks& t : tail.tasks) {
      if (!validStacks(t, true)) {
        return AssemblerError::CannotRestoreSyntax;
      }
    }
  }

  // Then replayed, each step checked: every task on the matrix and taken
  // back, then the position, which stays.
  std::vector<ReplayStep> done;
  for (const SearchStacks& t : tail.tasks) {
    const bool fits = replayChecked(links_, t, true, done);
    undoReplay(links_, done);
    if (!fits) {
      return AssemblerError::CannotRestoreSyntax;
    }
  }
  if (!replayChecked(links_, s, false, done)) {
    undoReplay(links_, done);
    return AssemblerError::CannotRestoreSyntax;
  }

  split_ = std::move(tail);
  rowOrderSignatures_ = interrupted == 2;
  stacks_ = std::move(s);
  finishedA_ = std::move(finishedA);
  finishedB_ = std::move(finishedB);

  // so that the loaded search shows how far it had come
  if (!split_.tasks.empty()) {
    taskProgress_.begin({}, split_.completed);
  }

  publishSerialProgress();
  return AssemblerError::None;
}

SavedPosition Assembler1::savePosition() const {
  // the leading flag, see restorePosition()
  const unsigned int flag = !split_.interrupted   ? 0
                            : split_.tasks.empty() ? 3
                            : rowOrderSignatures_  ? 2
                                                   : 4;

  std::string out = std::to_string(flag) + " ";

  vectorToString(stacks_.rows, out);
  vectorToString(stacks_.taskStack, out);
  vectorToString(stacks_.nextRowStack, out);
  vectorToString(stacks_.columnStack, out);
  vectorToString(stacks_.hiddenRows, out);
  vectorToString(finishedA_, out);
  vectorToString(finishedB_, out);

  writeResumeTail(out, flag, std::max(split_.vectorSkip, split_.vectorDone), split_,
                  [](std::string& o, const SearchStacks& t) {
                    o += ' ';
                    vectorToString(t.taskStack, o);
                    vectorToString(t.nextRowStack, o);
                    vectorToString(t.columnStack, o);
                    vectorToString(t.rows, o);
                    vectorToString(t.hiddenRows, o);
                  });

  return SavedPosition{kVersion, std::move(out)};
}

unsigned int Assembler1::piecePlacement(unsigned int node, int delta, unsigned int piece,
                                        unsigned char* tran, int* x, int* y, int* z) const {
  // the shape of the piece
  unsigned int pp = 0;
  unsigned int shape = 0;
  while (pp + problem_.getPartMaximum(shape) <= piece) {
    pp += problem_.getPartMaximum(shape);
    shape++;
  }

  if (!node) {
    node = links_.down[shape + 1];
  }

  while (delta > 0) {
    node = links_.down[node];
    delta--;
  }

  while (delta < 0) {
    node = links_.up[node];
    delta++;
  }

  const PiecePosition& p = placementOf(node);
  *tran = p.transformation;
  *x = p.x;
  *y = p.y;
  *z = p.z;

  BT_ASSERT(p.piece == shape);

  return node;
}

unsigned int Assembler1::piecePlacementCount(unsigned int piece) const {
  // the shape of the piece
  unsigned int pp = 0;
  unsigned int shape = 0;
  while (pp + problem_.getPartMaximum(shape) <= piece) {
    pp += problem_.getPartMaximum(shape);
    shape++;
  }

  return links_.colCount[shape + 1];
}

void Assembler1::debugStep(unsigned long steps) {
  debug_ = true;
  debugLoops_ = static_cast<long>(steps);
  const AssemblyCallback none;
  SearchRun run(StopToken{}, none);
  iterative(run);
  debug_ = false;
  publishSerialProgress();
}

bool Assembler1::canHandle(const Problem& problem) {
  // no limits
  (void)problem;
  return true;
}

}  // namespace bt

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The dancing links matrix of assembler 0: a row per piece
/// placement, a column per piece, per filled voxel of the result and per
/// variable voxel, built node for node as the original does.
/// Ported from src/lib/assembler_0.cpp (the matrix part).

#include "shapelogic/solve/assembler.h"
#include "shapelogic/solve/rotation_filter.h"

#include <atomic>
#include <cstddef>
#include <vector>

namespace bt {

class GridType;
class Problem;
class Puzzle;

/// True when every part of the problem is exactly one piece: the problems
/// the matrix below can describe (canHandle of assembler 0).
[[nodiscard]] bool everyPartOnePiece(const Problem& problem);

/// The nodes of a dancing links matrix, one array per member, and the
/// operations that cover and uncover columns and rows. A thread that
/// searches on its own takes a copy.
///
/// colCount is a column for normal nodes and the count for column headers;
/// upDown interleaves up (even) and down (odd), which saves a register in
/// the cover loops.
struct DlxNodes {
  /// The row (and header) ring of each node.
  std::vector<unsigned int> left;
  std::vector<unsigned int> right;
  /// The column ring of each node, up and down interleaved.
  std::vector<unsigned int> upDown;
  /// A node's column, or a header's count of rows.
  std::vector<unsigned int> colCount;

  /// The node above and below `x` in its column.
  unsigned int& up(unsigned int x) { return upDown[2 * std::size_t{x}]; }
  unsigned int& down(unsigned int x) { return upDown[2 * std::size_t{x} + 1]; }
  [[nodiscard]] unsigned int up(unsigned int x) const { return upDown[2 * std::size_t{x}]; }
  [[nodiscard]] unsigned int down(unsigned int x) const { return upDown[2 * std::size_t{x} + 1]; }

  /// Removes the column from the header list, and every row with a 1 in it.
  void cover(unsigned int col) {
    {
      const unsigned int l = left[col];
      const unsigned int r = right[col];

      left[r] = l;
      right[l] = r;
    }

    for (unsigned int i = down(col); i != col; i = down(i)) {
      for (unsigned int j = right[i]; j != i; j = right[j]) {
        const unsigned int u = up(j);
        const unsigned int d = down(j);

        up(d) = u;
        down(u) = d;

        colCount[colCount[j]]--;
      }
    }
  }

  /// The exact inverse of cover(): the matrix must be as cover() left it.
  void uncover(unsigned int col) {
    for (unsigned int i = up(col); i != col; i = up(i)) {
      for (unsigned int j = left[i]; j != i; j = left[j]) {
        colCount[colCount[j]]++;

        up(down(j)) = j;
        down(up(j)) = j;
      }
    }

    left[right[col]] = col;
    right[left[col]] = col;
  }

  /// Covers every column in which row r has a 1.
  void coverRow(unsigned int r) {
    for (unsigned int j = right[r]; j != r; j = right[j]) {
      cover(colCount[j]);
    }
  }

  /// The inverse of coverRow().
  void uncoverRow(unsigned int r) {
    for (unsigned int j = left[r]; j != r; j = left[j]) {
      uncover(colCount[j]);
    }
  }
};

/// The placement a row stands for.
struct DlxPlacement {
  int x, y, z;                   ///< the position of the piece's hotspot
  unsigned char transformation;  ///< its orientation
  unsigned int row;              ///< first node of the row
  unsigned int piece;            ///< the piece (each part is one piece)
};

/// The matrix of a problem where every part is exactly one piece. Rows are
/// piece placements; the columns are the pieces (1 .. pieces), the filled
/// voxels of the result (which must be covered, up to varivoxelStart - 1)
/// and the variable voxels (which may be, varivoxelStart .. varivoxelEnd -
/// 1; at most `holes` of them stay empty). The variable voxels' columns are
/// a ring of their own headed by varivoxelEnd, so a search never chooses
/// them to fill.
class DlxMatrix {
 public:
  /// Builds the matrix (createMatrix of the original, less what belongs to
  /// the search). Sets the rotation check `filter` asks for and writes the
  /// part being placed to `piece` as it goes. On an error `param` says more
  /// (the units too many or too few, the shape that fits nowhere).
  AssemblerError build(const Puzzle& puzzle, const Problem& problem, bool strictColors,
                       RotationFilter& filter, std::atomic<unsigned int>& piece, int& param);

  /// Removes placements that can never be part of an assembly; polls `stop`
  /// and leaves a valid (partly reduced) matrix when stopped. Writes the
  /// piece it is at to `piece`.
  ReduceStats reduce(const StopToken& stop, std::atomic<unsigned int>& piece);

  /// The placement of the row `node` belongs to.
  [[nodiscard]] const DlxPlacement& placementOf(unsigned int node) const;

  /// The assembly of the rows `rows[0 .. count)` (in any order), unplaced
  /// pieces included, on grid `gt`.
  [[nodiscard]] Assembly assemblyFromRows(const GridType& gt, const unsigned int* rows,
                                          unsigned int count) const;

  /// An assembly with no piece placed.
  [[nodiscard]] Assembly emptyAssembly(const GridType& gt) const;

  /// The placement browser (Assembler::piecePlacement and
  /// piecePlacementCount).
  unsigned int piecePlacement(unsigned int node, int delta, unsigned int piece,
                              unsigned char* tran, int* x, int* y, int* z) const;
  [[nodiscard]] unsigned int piecePlacementCount(unsigned int piece) const {
    return nodes.colCount[piece + 1];
  }

  /// The nodes.
  DlxNodes nodes;
  /// One entry per row, in row order.
  std::vector<DlxPlacement> placements;
  /// The number of pieces.
  unsigned int pieces = 0;
  /// The number of variable voxels left empty in every assembly.
  int holes = 0;
  /// First and one past the last column of the variable voxels.
  unsigned int varivoxelStart = 0;
  unsigned int varivoxelEnd = 0;

 private:
  void generateFirstRow();
  unsigned int addPieceNode(unsigned int piece, unsigned int rot, int x, int y, int z);
  void addVoxelNode(unsigned int col, unsigned int piecenode);
  int prepare(const Puzzle& puzzle, const Problem& problem, bool strictColors,
              RotationFilter& filter, std::atomic<unsigned int>& piece);
  unsigned int clumpify();

  bool tryCoverRow(unsigned int r, unsigned int* columns);
  void removeRow(unsigned int r);
  void removeColumn(unsigned int c);
  bool checkmatrix();
};

}  // namespace bt

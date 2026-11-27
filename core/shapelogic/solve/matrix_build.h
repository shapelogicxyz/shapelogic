// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// What the matrices of the two assemblers are built with: the placement a
/// row stands for, the walk through every part's placements, the pieces
/// that mirror each other, the columns that are in the same rows, and the
/// rotation check answered from a cover's rows. The original has a copy of
/// each in assembler_0.cpp and in assembler_1.cpp; here each is written
/// once, and the assemblers add what is their own (the nodes, the counts
/// and ranges of assembler 1).
/// Ported from src/lib/assembler_0.cpp and assembler_1.cpp (prepare,
/// getPieceInformation, clumpify).

#include "shapelogic/base/stop.h"
#include "shapelogic/grid/symmetries.h"
#include "shapelogic/model/assembly.h"
#include "shapelogic/solve/assembler.h"
#include "shapelogic/solve/row_rotation_check.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

namespace bt {

class Problem;
class Puzzle;
class Voxel;
struct RotationFilter;

/// The placement a row of a matrix stands for.
struct RowPlacement {
  int x, y, z;                   ///< the position of the piece's hotspot
  unsigned char transformation;  ///< its orientation
  unsigned int row;              ///< first node of the row
  unsigned int piece;            ///< assembler 0: the piece; assembler 1: the part
};

/// The index in `rows` (one entry a row, in row order) of the row `node`
/// belongs to (getPieceInformation).
[[nodiscard]] std::size_t rowIndexOf(const std::vector<RowPlacement>& rows, unsigned int node);

/// The pieces of the problem that are each other's mirror image, for the
/// check that drops mirrored assemblies. Null when a piece has no mirror
/// symmetry and no partner, so that no assembly's mirror image can be
/// built; with `evenWithoutPartner` (assembler 1 with ranges: an assembly
/// may use only pieces that mirror themselves) the pairs found come all the
/// same.
[[nodiscard]] std::unique_ptr<MirrorInfo> mirrorPairs(const Puzzle& puzzle, const Problem& problem,
                                                      bool evenWithoutPartner);

/// What a matrix does with the placements placeParts() finds.
class PlacementSink {
 public:
  virtual ~PlacementSink() = default;

  /// The placements of `part` come next.
  virtual void beginPart(unsigned int part) = 0;
  /// A placement of `part` in orientation `rot`, its hotspot at (x, y, z):
  /// it fills the result's cells `base` + each of `offsets` (indices into
  /// the result's voxels).
  virtual void addRow(unsigned int part, unsigned int rot, int x, int y, int z, long base,
                      const std::vector<long>& offsets) = 0;
  /// Every placement of one orientation was added.
  virtual void endOrientation() = 0;

 protected:
  PlacementSink() = default;
  PlacementSink(const PlacementSink&) = delete;
  PlacementSink& operator=(const PlacementSink&) = delete;
};

/// What placeParts() places, and how.
struct PlaceParts {
  bool strictColors = false;
  bool startLocks = false;
  /// Per part, whether its pieces may leave their starts (see
  /// MatrixOptions::freeStarts).
  const std::vector<bool>& freeStarts;
  /// The orientations each part may take (MatrixOptions::partTransforms).
  const std::vector<std::uint64_t>& partTransforms;
  /// The part that is not placed in the orientations the result's
  /// symmetries map onto each other; 0xFFFFFFFF for none.
  unsigned int symBreakerShape = 0xFFFFFFFF;
  /// The result's own symmetries.
  Symmetry resultSym = 0;
  /// A part without any placement ends the build (assembler 0, where every
  /// part is a piece that must be placed); off, only one that must be used
  /// (its minimum is above 0).
  bool everyPartNeeded = false;
};

/// Every placement of every part of `problem` in its result, part by part
/// and orientation by orientation, in the original's order, each handed to
/// `sink`; the part in hand is written to `part`. Returns kPrepared,
/// kPrepareStopped when `stop` ended it (the rows are then not all there),
/// or minus the shape id of a part that fits nowhere.
int placeParts(const Puzzle& puzzle, const Problem& problem, const PlaceParts& how,
               PlacementSink& sink, std::atomic<unsigned int>& part, const StopToken& stop);

/// A column that is in exactly the same rows as one before it in the
/// header ring.
struct ColumnTwin {
  unsigned int column;  ///< the later one, to be removed
  unsigned int first;   ///< the one that stays
};

/// The columns of the header ring of `n` (`right` from 0, `down` through a
/// column) that are twins of an earlier one: in the same rows of `rows`,
/// with the same `columnKey(column, out)` values (appended to `out`, a
/// vector of 64-bit numbers;
/// assembler 1: the column's minimum and maximum) and the same
/// `nodeKey(node, out)` values in each row (assembler 1: the node's
/// weight). The first of each group stays. Taking them out saves no
/// iterations but makes them cheaper. Columns are grouped by a hash of what
/// is compared and only compared exactly within a group, which is linear in
/// the number of nodes.
template <class Nodes, class ColumnKey, class NodeKey>
[[nodiscard]] std::vector<ColumnTwin> columnTwins(const Nodes& n,
                                                  const std::vector<RowPlacement>& rows,
                                                  ColumnKey&& columnKey, NodeKey&& nodeKey) {
  // the columns in header ring order; the first of each group survives
  std::vector<unsigned int> cols;
  for (unsigned int c = n.right[0]; c != 0; c = n.right[c]) {
    cols.push_back(c);
  }

  // all signatures back to back, with the start of each
  std::vector<std::uint64_t> sigData;
  std::vector<std::size_t> sigStart;
  std::vector<unsigned long long> sigHash;

  sigStart.reserve(cols.size() + 1);
  sigHash.reserve(cols.size());

  for (const unsigned int col : cols) {
    sigStart.push_back(sigData.size());
    columnKey(col, sigData);

    std::size_t line = 0;

    for (unsigned int row = n.down(col); row != col; row = n.down(row)) {
      // the placement row of this node; nodes come in increasing order so
      // the line only ever advances
      while ((line + 1 < rows.size()) && (rows[line + 1].row <= row)) {
        line++;
      }

      sigData.push_back(line);
      nodeKey(row, sigData);
    }

    unsigned long long h = 14695981039346656037ULL;
    for (std::size_t i = sigStart.back(); i < sigData.size(); i++) {
      h = (h ^ sigData[i]) * 1099511628211ULL;
    }
    sigHash.push_back(h);
  }

  sigStart.push_back(sigData.size());

  std::unordered_map<unsigned long long, std::vector<std::size_t>> groups;
  std::vector<ColumnTwin> twins;

  for (std::size_t i = 0; i < cols.size(); i++) {
    std::vector<std::size_t>& group = groups[sigHash[i]];

    bool dup = false;

    for (const std::size_t j : group) {
      const std::size_t leni = sigStart[i + 1] - sigStart[i];

      if (leni != sigStart[j + 1] - sigStart[j]) {
        continue;
      }

      if ((leni == 0) || (std::memcmp(&sigData[sigStart[i]], &sigData[sigStart[j]],
                                      leni * sizeof(std::uint64_t)) == 0)) {
        twins.push_back(ColumnTwin{cols[i], cols[j]});
        dup = true;
        break;
      }
    }

    if (!dup) {
      group.push_back(i);
    }
  }

  return twins;
}

/// The rotation check answered from a cover's rows (decision 0024) for a
/// matrix with the rows `rows` and `nodes` nodes; null when the options or
/// the filter leave it out, or it cannot be made. `rowOfNode` gets every
/// node's row (kept when it has them already).
[[nodiscard]] std::unique_ptr<RowRotationCheck> makeRowCheck(
    const SearchOptions& options, const Puzzle& puzzle, const Problem& problem,
    const RotationFilter& filter, const std::vector<RowPlacement>& rows, std::size_t nodes,
    std::vector<std::uint32_t>& rowOfNode);

/// True when `check` drops the cover of the rows `nodes[0 .. count)` (a
/// node of each row): a rotation of it is smaller.
[[nodiscard]] inline bool rowCheckRejects(const RowRotationCheck& check,
                                          const std::vector<std::uint32_t>& rowOfNode,
                                          const unsigned int* nodes, std::size_t count,
                                          RowRotationCheck::Scratch& scratch) {
  scratch.found.clear();
  for (std::size_t i = 0; i < count; i++) {
    scratch.found.push_back(rowOfNode[nodes[i]]);
  }
  return check.rejects(scratch);
}

/// strtol from `s`, cast to unsigned int; returns the characters consumed
/// (the original's getInt of the saved positions, quirks included: no
/// digits read 0).
unsigned int getInt(const char* s, unsigned int* i);

}  // namespace bt

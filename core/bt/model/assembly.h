// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Where every piece of a problem sits in one assembly.
/// Ported from src/lib/assembly.{h,cpp} (XML reading and writing lives in
/// bt/io).

#include "bt/base/assert.h"
#include "bt/grid/grid_type.h"
#include "bt/grid/symmetries.h"

#include <memory>
#include <vector>

namespace bt {

class Problem;
class Puzzle;
class Voxel;

/// The transformation value of a piece that is not placed.
inline constexpr unsigned char kUnplacedTrans = 0xFF;

/// The orientation and position of one piece in an assembly: the position is
/// where the piece's hotspot goes.
struct Placement {
  unsigned char transformation = kUnplacedTrans;  ///< kUnplacedTrans: not placed
  int xpos = 0;
  int ypos = 0;
  int zpos = 0;

  [[nodiscard]] int getX() const { return xpos; }
  [[nodiscard]] int getY() const { return ypos; }
  [[nodiscard]] int getZ() const { return zpos; }
  [[nodiscard]] unsigned char getTransformation() const { return transformation; }

  bool operator==(const Placement&) const = default;

  /// An arbitrary but fixed order: transformation, then x, y, z.
  bool operator<(const Placement& b) const {
    if (transformation != b.transformation) {
      return transformation < b.transformation;
    }
    if (xpos != b.xpos) {
      return xpos < b.xpos;
    }
    if (ypos != b.ypos) {
      return ypos < b.ypos;
    }
    return zpos < b.zpos;
  }
};

/// Pairs of pieces of a problem that are mirror images of each other, and
/// the transformation that turns the first into the second. The assemblers
/// fill it so a mirrored assembly can swap such pieces instead of mirroring
/// them.
class MirrorInfo {
 public:
  /// Records that `trans` turns piece p1 into piece p2.
  void addPieces(unsigned int p1, unsigned int p2, unsigned char trans);

  /// The mirror partner of piece p and the transformation to it; false when
  /// p has none.
  bool getPieceInfo(unsigned int p, unsigned int* pOut, unsigned char* trans) const;

 private:
  struct Entry {
    unsigned int pc1;
    unsigned int pc2;
    unsigned char trans;
  };
  std::vector<Entry> entries_;
};

/// One assembly: a Placement for every piece of the problem, in the order
/// of the problem's pieces (part by part, see Problem). A value type.
class Assembly {
 public:
  /// An empty assembly on grid `gt`.
  explicit Assembly(GridType gt) : gt_(gt) {}

  /// The grid the transformations belong to.
  [[nodiscard]] GridType getGridType() const { return gt_; }

  /// Appends a placed piece.
  void addPlacement(unsigned char tran, int x, int y, int z) {
    BT_ASSERT(tran < sym().getNumTransformations());
    BT_ASSERT(tran != kUnplacedTrans);
    placements_.push_back(Placement{tran, x, y, z});
  }

  /// Appends a piece that is not used in this assembly.
  void addNonPlacement() { placements_.push_back(Placement{kUnplacedTrans, 0, 0, 0}); }

  /// Appends a placement as it is, unplaced or mirrored included (the file
  /// reader; the checks are its own).
  void addRawPlacement(const Placement& p) { placements_.push_back(p); }

  /// The number of pieces.
  [[nodiscard]] unsigned int placementCount() const {
    return static_cast<unsigned int>(placements_.size());
  }

  /// All placements.
  [[nodiscard]] const std::vector<Placement>& getPlacements() const { return placements_; }

  [[nodiscard]] bool isPlaced(unsigned int num) const {
    BT_ASSERT(num < placements_.size());
    return placements_[num].transformation != kUnplacedTrans;
  }

  [[nodiscard]] unsigned char getTransformation(unsigned int num) const {
    return placed(num).transformation;
  }
  [[nodiscard]] int getX(unsigned int num) const { return placed(num).xpos; }
  [[nodiscard]] int getY(unsigned int num) const { return placed(num).ypos; }
  [[nodiscard]] int getZ(unsigned int num) const { return placed(num).zpos; }

  /// True when every part has at least its minimum number of pieces placed.
  [[nodiscard]] bool validSolution(const Problem& problem) const;

  /// Rotates the whole assembly by `trans` around the result shape, so that
  /// when the result looks the same in that orientation the assembly stays
  /// in place. Mirroring transformations need `mir`. False when the
  /// transformation is not possible; the assembly is then in an undefined
  /// state and must be thrown away.
  bool transform(unsigned char trans, const Puzzle& puzzle, const Problem& problem,
                 const MirrorInfo* mir);

  /// True when a rotation (or, with `mir`, a mirror) of this assembly is
  /// smaller in the order that compares the `pivot` piece first: the
  /// assemblers drop such assemblies as duplicates. `complete` tries every
  /// transformation and every shift instead of only the result's symmetries.
  [[nodiscard]] bool smallerRotationExists(const Puzzle& puzzle, const Problem& problem,
                                           unsigned int pivot, const MirrorInfo* mir,
                                           bool complete, bool strictColors = false) const;

  /// Swaps the placements of pieces s1 and s2.
  void exchangeShape(unsigned int s1, unsigned int s2);

  /// Compares the pieces used: 0 when both use the same pieces, 1 when this
  /// one places a piece first that `b` leaves out, -1 the other way round.
  [[nodiscard]] int comparePieces(const Assembly& b) const;

  /// Sorts the placed pieces of each part into ascending placement order.
  void sort(const Problem& problem);

  /// A voxel space with all pieces put in, with its minimum corner at index
  /// 0. The origin outputs receive that corner in result coordinates.
  [[nodiscard]] std::unique_ptr<Voxel> createSpace(const Puzzle& puzzle, const Problem& problem,
                                                   int* originX = nullptr, int* originY = nullptr,
                                                   int* originZ = nullptr) const;

  /// Removes `cnt` (unplaced) pieces starting at `from`.
  void removePieces(unsigned int from, unsigned int cnt);

  /// Inserts `cnt` unplaced pieces at `from`.
  void addNonPlacedPieces(unsigned int from, unsigned int cnt);

  /// Same placements (and grid).
  bool operator==(const Assembly& b) const = default;

 private:
  [[nodiscard]] const Symmetries& sym() const { return gt_.getSymmetries(); }
  [[nodiscard]] const Placement& placed(unsigned int num) const {
    BT_ASSERT(num < placements_.size());
    BT_ASSERT(placements_[num].transformation != kUnplacedTrans);
    return placements_[num];
  }

  /// True when this assembly is smaller than `b`, comparing the pivot piece
  /// first.
  [[nodiscard]] bool compare(const Assembly& b, unsigned int pivot) const;
  [[nodiscard]] bool containsMirroredPieces() const;

  GridType gt_;
  std::vector<Placement> placements_;
};

}  // namespace bt

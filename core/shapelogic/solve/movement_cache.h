// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// How far one piece can move against another: the values Bill Cutler's
/// movement analysis starts from, computed per pair of shapes, orientations
/// and offset and kept for the next time they are asked for.
/// Ported from src/lib/movementcache.{h,cpp}, movementcache_0.{h,cpp}
/// (cubes) and movementcache_1.{h,cpp} (triangular prisms), and
/// gridType_c::getMovementCache. The cache of the other grids
/// (MovementCacheDerived) is new: the original had none for them.

#include "shapelogic/model/movement_values.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace sl {

class GridType;
struct MovementChecks;
class Problem;
class Puzzle;
class Voxel;

/// The movement values of one problem's shapes. A cache belongs to one
/// thread; it keeps its own turned copies of the shapes and reads the
/// puzzle's for orientation 0.
class MovementCache {
 public:
  /// The value for two pieces with nothing of each other in the way: the
  /// largest a cache gives.
  static constexpr unsigned int kFree = movement::kFree;

  /// A cache for the pieces of `problem`; puzzle and problem must outlive it.
  MovementCache(const Puzzle& puzzle, const Problem& problem);
  virtual ~MovementCache();

  MovementCache(const MovementCache&) = delete;
  MovementCache& operator=(const MovementCache&) = delete;

  /// How far piece p2, at offset (dx, dy, dz) from piece p1, can move away
  /// from it in each direction, with the pieces in orientations t1 and t2;
  /// numDirections() values into `movements`.
  void getMoValue(int dx, int dy, int dz, unsigned char t1, unsigned char t2, unsigned int p1,
                  unsigned int p2, unsigned int* movements);

  /// The number of directions pieces move in on this grid (each both ways).
  [[nodiscard]] virtual unsigned int numDirections() const = 0;

  /// One step in direction `dir`.
  virtual void getDirection(unsigned int dir, int* x, int* y, int* z) const = 0;

  /// The part (shape) piece `piece` of the problem is.
  [[nodiscard]] unsigned int getShapeOfPiece(unsigned int piece) const { return pieces_[piece]; }

  /// Part s's shape in orientation t, made the first time it is asked for.
  [[nodiscard]] const Voxel& getTransformedShape(unsigned int s, unsigned char t);

 private:
  /// The values for two shapes as placed (sh2 offset by dx, dy, dz).
  virtual std::vector<unsigned int> moCalcValues(const Voxel& sh1, const Voxel& sh2, int dx, int dy,
                                                 int dz) = 0;

  /// The values found so far, in an open-addressed table (linear probing, a
  /// power of two of places, at most 70% full; the original's moTable). A
  /// place is kKeyWords words of key (the two shapes, the offset of the
  /// second, both orientations in one word) and the numDirections() values
  /// after them, so a hit reads one stretch of memory: a take-apart on
  /// several threads has a cache a thread, and they share the processor's.
  /// An empty place has kEmpty for its first shape.
  static constexpr std::size_t kKeyWords = 6;
  static constexpr unsigned int kEmpty = 0xFFFFFFFFU;
  /// The place of a key, or the empty place it goes to.
  [[nodiscard]] unsigned int* moFind(const unsigned int* key);
  void moGrow();

  std::vector<unsigned int> moTable_;
  std::size_t moStride_ = 0;  ///< words a place
  std::size_t moMask_ = 0;    ///< places - 1
  std::size_t moCount_ = 0;   ///< places in use

  const GridType& gridType_;
  /// The puzzle's shape of each part (orientation 0), and the turned copies
  /// made so far ([part][orientation], empty for 0).
  std::vector<const Voxel*> base_;
  std::vector<std::vector<std::unique_ptr<Voxel>>> turned_;
  /// The part of each piece.
  std::vector<unsigned int> pieces_;
};

/// The cache for cubes: three directions, along x, y and z.
class MovementCacheBricks final : public MovementCache {
 public:
  MovementCacheBricks(const Puzzle& puzzle, const Problem& problem);

  [[nodiscard]] unsigned int numDirections() const override { return 3; }
  void getDirection(unsigned int dir, int* x, int* y, int* z) const override;

 private:
  std::vector<unsigned int> moCalcValues(const Voxel& sh1, const Voxel& sh2, int dx, int dy,
                                         int dz) override;
};

/// The cache for triangular prisms: four directions (z and three in the
/// plane of the triangles).
class MovementCachePrisms final : public MovementCache {
 public:
  MovementCachePrisms(const Puzzle& puzzle, const Problem& problem);

  [[nodiscard]] unsigned int numDirections() const override { return 4; }
  void getDirection(unsigned int dir, int* x, int* y, int* z) const override;

 private:
  std::vector<unsigned int> moCalcValues(const Voxel& sh1, const Voxel& sh2, int dx, int dy,
                                         int dz) override;
};

/// The cache for the grids whose movement checks are derived from the
/// cells' geometry (movement_checks.h; decision 0025): the rhombic,
/// tetra-octa, hexagonal and cairo grids. The walk is the prisms' one,
/// over the grid's own directions and check cells. Not in the original.
class MovementCacheDerived final : public MovementCache {
 public:
  /// `checks` are the grid's (movementChecks) and must outlive the cache.
  MovementCacheDerived(const Puzzle& puzzle, const Problem& problem, const MovementChecks& checks);

  [[nodiscard]] unsigned int numDirections() const override;
  void getDirection(unsigned int dir, int* x, int* y, int* z) const override;

 private:
  std::vector<unsigned int> moCalcValues(const Voxel& sh1, const Voxel& sh2, int dx, int dy,
                                         int dz) override;

  const MovementChecks& checks_;
};

/// The cache for the grid of `puzzle`, or null for a grid without take-apart
/// (gridType_c::getMovementCache). With `derivedChecks`, cubes and
/// triangular prisms get the derived cache too (an A/B switch: the values
/// are the same).
[[nodiscard]] std::unique_ptr<MovementCache> makeMovementCache(const Puzzle& puzzle,
                                                               const Problem& problem,
                                                               bool derivedChecks = false);

}  // namespace sl

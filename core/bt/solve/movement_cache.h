// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// How far one piece can move against another: the values Bill Cutler's
/// movement analysis starts from, computed per pair of shapes, orientations
/// and offset and kept for the next time they are asked for.
/// Ported from src/lib/movementcache.{h,cpp}, movementcache_0.{h,cpp}
/// (cubes) and movementcache_1.{h,cpp} (triangular prisms), and
/// gridType_c::getMovementCache.

#include <cstddef>
#include <memory>
#include <unordered_map>
#include <vector>

namespace bt {

class GridType;
class Problem;
class Puzzle;
class Voxel;

/// The movement values of one problem's shapes. A cache belongs to one
/// thread; it keeps its own turned copies of the shapes and reads the
/// puzzle's for orientation 0.
class MovementCache {
 public:
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

  struct MoKey {
    unsigned int s1 = 0;  ///< the first shape
    unsigned int s2 = 0;  ///< the second shape
    int dx = 0;           ///< the offset of the second
    int dy = 0;
    int dz = 0;
    unsigned short t1 = 0;  ///< the orientations
    unsigned short t2 = 0;

    bool operator==(const MoKey& o) const = default;
  };

  struct MoKeyHash {
    std::size_t operator()(const MoKey& k) const noexcept;
  };

  std::unordered_map<MoKey, std::vector<unsigned int>, MoKeyHash> moCache_;

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

/// The cache for the grid of `puzzle`, or null for a grid without take-apart
/// (gridType_c::getMovementCache).
[[nodiscard]] std::unique_ptr<MovementCache> makeMovementCache(const Puzzle& puzzle,
                                                               const Problem& problem);

}  // namespace bt

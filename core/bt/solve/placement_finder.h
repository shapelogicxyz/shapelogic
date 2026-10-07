// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Finds every position of a rotated piece inside the result shape, for
/// building the assemblers' matrices.
/// Ported from placementFinder_c in src/lib/assembler.{h,cpp}.

#include <vector>

namespace bt {

class Problem;
class Puzzle;
class Voxel;

/// Enumerates the placements of rotated pieces inside a result shape.
///
/// Instead of trying every translation of the piece's bounding box inside
/// the result's and rescanning the whole piece each time, only translations
/// that put the piece's first filled voxel (the anchor) onto a non-empty
/// result voxel are candidates; the other voxels are then checked with
/// single indexed reads. The placements come out in the order the original
/// brute-force scan found them (x, then y, then z ascending), so the
/// matrices are built identically.
class PlacementFinder {
 public:
  /// A finder for `result`, with the colour rules of `problem`.
  PlacementFinder(const Puzzle& puzzle, const Problem& problem, const Voxel& result,
                  bool strictColors = false);

  /// All placements of `rotation`. `voxelOffsets` receives, for each filled
  /// voxel of the piece in matrix order, its result index offset relative to
  /// a placement (index = x + sx*(y + sy*z) + offset); `placements` receives
  /// (x, y, z) translation triples.
  void find(const Voxel& rotation, std::vector<long>& voxelOffsets,
            std::vector<int>& placements) const;

 private:
  struct ResultVoxel {
    int x, y, z;          ///< coordinates of the voxel
    unsigned long index;  ///< index within the result voxel space
    unsigned char color;  ///< colour of the result voxel
  };

  const Voxel& res_;
  std::vector<ResultVoxel> resultVoxels_;   ///< all non-empty voxels of the result
  unsigned int colorTableWidth_;            ///< number of colours + 1
  std::vector<unsigned char> colorAllowed_; ///< placementAllowed lookup table
};

}  // namespace bt

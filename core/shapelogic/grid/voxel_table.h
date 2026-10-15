// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// A hash table for finding shapes that are rotations of each other.
/// Ported from src/lib/voxeltable.{h,cpp}.

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace bt {

class Voxel;

/// Finds duplicates among shapes. Shapes are added in all their distinct
/// orientations; a lookup then finds a shape in any orientation that was
/// added. The table stores only indices: `findSpace` turns an index back
/// into the shape and must stay valid for the table's life.
class VoxelTable {
 public:
  /// Flags for addSpace() and getSpace(), to be or-ed.
  enum : unsigned int {
    kParMirror = 1,  ///< include the mirrored orientations
    kParColour = 2,  ///< differently coloured shapes are different
  };

  using FindSpace = std::function<const Voxel&(unsigned int index)>;

  explicit VoxelTable(FindSpace findSpace) : findSpace_(std::move(findSpace)) {}

  /// True when the shape is in the table. Optionally returns the index of
  /// the shape found and the orientation it was found in.
  /// - kParMirror: mirrored orientations may match (only those added with
  ///   kParMirror exist)
  /// - kParColour: hash with colours, so differently coloured shapes do not
  ///   match (the final comparison, as in the original, ignores colour)
  bool getSpace(const Voxel& v, unsigned int* index = nullptr, unsigned char* trans = nullptr,
                unsigned int params = 0) const;

  /// Adds every distinct orientation of shape `index` (mirrored ones too
  /// with kParMirror; colour-sensitive hashes with kParColour). To search
  /// both ways, add with kParMirror and again with kParMirror | kParColour.
  void addSpace(unsigned int index, unsigned int params = 0);

 private:
  struct Entry {
    unsigned int index;            ///< the shape, as given to findSpace
    unsigned char transformation;  ///< the orientation stored
  };

  FindSpace findSpace_;
  // The entries of one hash in the order they were added: getSpace returns
  // the first match, so that order is part of the answer. (An
  // unordered_multimap orders equal keys as its library likes: libc++ and
  // libstdc++ differ.)
  std::unordered_map<std::uint64_t, std::vector<Entry>> hashTable_;
};

}  // namespace bt

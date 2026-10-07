// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/voxeltable.cpp.

#include "bt/grid/voxel_table.h"

#include "bt/grid/symmetries.h"
#include "bt/grid/voxel.h"

namespace bt {

namespace {

// The original hashed into `unsigned long`, 32 bits on Windows; a fixed
// 64-bit value changes only which shapes share a bucket, not any answer.
std::uint64_t calcHashValue(const Voxel& v, bool withColour) {
  std::uint64_t res = 101;

  res *= v.boundX2() - v.boundX1();
  res += 11;
  res *= v.boundY2() - v.boundY1();
  res += 13;
  res *= v.boundZ2() - v.boundZ1();
  res += 17;

  for (unsigned int z = v.boundZ1(); z <= v.boundZ2(); z++) {
    for (unsigned int y = v.boundY1(); y <= v.boundY2(); y++) {
      for (unsigned int x = v.boundX1(); x <= v.boundX2(); x++) {
        res *= 101;
        res += static_cast<std::uint64_t>(v.getState(x, y, z));
        if (withColour && v.getState(x, y, z) != Voxel::kEmpty) {
          res *= 3;
          res += v.getColor(x, y, z);
        }
      }
    }
  }
  return res;
}

}  // namespace

bool VoxelTable::getSpace(const Voxel& v, unsigned int* index, unsigned char* trans,
                          unsigned int params) const {
  const std::uint64_t hash = calcHashValue(v, (params & kParColour) != 0);
  const unsigned int numPlain = v.getGridType().getSymmetries().getNumTransformations();

  auto range = hashTable_.equal_range(hash);
  for (auto it = range.first; it != range.second; ++it) {
    const Entry& n = it->second;
    if ((params & kParMirror) || n.transformation < numPlain) {
      auto v2 = findSpace_(n.index).clone();
      if (v2->transform(n.transformation) && v.identicalInBB(*v2, false)) {
        if (index) {
          *index = n.index;
        }
        if (trans) {
          *trans = n.transformation;
        }
        return true;
      }
    }
  }
  return false;
}

void VoxelTable::addSpace(unsigned int index, unsigned int params) {
  const Voxel& v = findSpace_(index);
  const Symmetries& symm = v.getGridType().getSymmetries();

  const unsigned int endTrans =
      (params & kParMirror) ? symm.getNumTransformationsMirror() : symm.getNumTransformations();
  const Symmetry sym = v.selfSymmetries();

  for (unsigned int trans = 0; trans < endTrans; trans++) {
    // only the orientations that actually differ
    if (symm.isTransformationUnique(sym, trans)) {
      auto v2 = v.clone();
      if (v2->transform(trans)) {
        hashTable_.emplace(calcHashValue(*v2, (params & kParColour) != 0),
                           Entry{index, static_cast<unsigned char>(trans)});
      }
    }
  }
}

}  // namespace bt

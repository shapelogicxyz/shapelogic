// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The table-driven Symmetries implementation shared by the cube and prism
/// table sets (internal to the grid module). Ported from
/// src/lib/symmetries_0.cpp and symmetries_1.cpp, which are the same code
/// over different tables. The sphere grid has its own (symmetries_2.cpp):
/// its table of groups grows at run time.

#include "shapelogic/base/assert.h"
#include "shapelogic/grid/symmetries.h"
#include "shapelogic/grid/voxel.h"

#include <bit>
#include <cstdint>

namespace bt::detail {

/// Bit operations over the tables' masks.
inline bool maskGet(std::uint64_t m, unsigned int t) { return (m & (std::uint64_t{1} << t)) != 0; }
inline unsigned int maskCount(std::uint64_t m) { return static_cast<unsigned int>(std::popcount(m)); }

/// The tables of one grid, as compile-time arrays. `Mask` is uint64_t: these
/// grids have at most 64 transformations.
template <class Mask, unsigned int kNumTrans, unsigned int kNumTransMirror, unsigned int kNumGroups,
          class TransMultEntry>
struct SymmetryTableSet {
  const TransMultEntry (&transMult)[kNumTransMirror][kNumTransMirror];
  const Mask (&symmetries)[kNumGroups];
  const Mask (&unifiedSymmetries)[kNumGroups];
  const unsigned char (&transformationMinimizer)[kNumGroups][kNumTransMirror];
  const Mask (&uniqueSymmetries)[kNumGroups];
};

/// Symmetries over a table set; `calc` is the grid's generated decision tree.
template <class Mask, unsigned int kNumTrans, unsigned int kNumTransMirror, unsigned int kNumGroups,
          class TransMultEntry>
class TableSymmetries final : public Symmetries {
 public:
  using Tables = SymmetryTableSet<Mask, kNumTrans, kNumTransMirror, kNumGroups, TransMultEntry>;
  using CalcFn = Symmetry (*)(const Voxel&);

  // constexpr: the instances are constant-initialized (constinit), so using
  // them needs no initialization guard (the assemblers' rotation checks call
  // them in their inner loops)
  constexpr TableSymmetries(Tables tables, CalcFn calc) : t_(tables), calc_(calc) {}

  unsigned int getNumTransformations() const override { return kNumTrans; }
  unsigned int getNumTransformationsMirror() const override { return kNumTransMirror; }

  bool symmetryContainsMirror(Symmetry sym) const override {
    BT_ASSERT(sym < kNumGroups);
    // any bit at or above kNumTrans is a mirror transformation
    for (unsigned int t = kNumTrans; t < kNumTransMirror; t++) {
      if (maskGet(t_.symmetries[sym], t)) {
        return true;
      }
    }
    return false;
  }

  unsigned char transAdd(unsigned char t1, unsigned char t2) const override {
    BT_ASSERT(t1 < kNumTransMirror);
    BT_ASSERT(t2 < kNumTransMirror);
    return static_cast<unsigned char>(t_.transMult[t1][t2]);
  }

  bool symmetrieContainsTransformation(Symmetry s, unsigned int t) const override {
    BT_ASSERT(s < kNumGroups);
    BT_ASSERT(t < kNumTransMirror);
    return maskGet(t_.symmetries[s], t);
  }

  bool isTransformationUnique(Symmetry s, unsigned int t) const override {
    BT_ASSERT(s < kNumGroups);
    BT_ASSERT(t < kNumTransMirror);
    return maskGet(t_.uniqueSymmetries[s], t);
  }

  unsigned char minimizeTransformation(Symmetry s, unsigned char trans) const override {
    BT_ASSERT(s < kNumGroups);
    BT_ASSERT(trans < kNumTransMirror);
    return t_.transformationMinimizer[s][trans];
  }

  unsigned int countSymmetryIntersection(Symmetry res, Symmetry s2) const override {
    BT_ASSERT(res < kNumGroups);
    BT_ASSERT(s2 < kNumGroups);
    return maskCount(t_.unifiedSymmetries[res] & t_.symmetries[s2]);
  }

  bool symmetriesLeft(Symmetry resultSym, Symmetry s2) const override {
    BT_ASSERT(resultSym < kNumGroups);
    BT_ASSERT(s2 < kNumGroups);
    const Mask s = t_.symmetries[resultSym] & t_.unifiedSymmetries[s2];
    // the identity (bit 0) does not count
    for (unsigned int t = 1; t < kNumTransMirror; t++) {
      if (maskGet(s, t)) {
        return true;
      }
    }
    return false;
  }

  bool symmetryKnown(const Voxel& pp) const override {
    Mask s{};
    setBit(s, 0);
    for (unsigned int j = 1; j < kNumTransMirror; j++) {
      auto v = pp.clone();
      if (v->transform(j) && pp.identicalInBB(*v)) {
        setBit(s, j);
      }
    }
    for (unsigned int i = 0; i < kNumGroups; i++) {
      if (t_.symmetries[i] == s) {
        return true;
      }
    }
    return false;
  }

  Symmetry calculateSymmetry(const Voxel& pp) const override {
    BT_ASSERT(symmetryKnown(pp));
    return calc_(pp);
  }

 private:
  static void setBit(std::uint64_t& m, unsigned int t) { m |= std::uint64_t{1} << t; }

  Tables t_;
  CalcFn calc_;
};

}  // namespace bt::detail

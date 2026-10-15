// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/symmetries_2.cpp: the symmetry tables for spheres,
// and the registry of the symmetry lists they lack (see symmetries_2.h).

#include "shapelogic/grid/symmetries_2.h"

#include "shapelogic/base/assert.h"
#include "shapelogic/grid/symmetries_tables.h"
#include "shapelogic/grid/types.h"
#include "shapelogic/grid/voxel.h"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace bt::detail {
namespace {

// "undefined": the tables' value for combinations that do not exist
constexpr unsigned char TND = kTransformUndefined;  // NOLINT(readability-identifier-naming)

#include "shapelogic/grid/tabs_2/tablesizes.inc"

static_assert(kSphereTransformations == NUM_TRANSFORMATIONS_MIRROR,
              "the sphere grid's transformation count is given twice");

constexpr unsigned char kTransMult[NUM_TRANSFORMATIONS_MIRROR][NUM_TRANSFORMATIONS_MIRROR] = {
#include "shapelogic/grid/tabs_2/transmult.inc"
};

constexpr SphereSymmetryList kSymmetries[NUM_SYMMETRY_GROUPS] = {
#include "shapelogic/grid/tabs_2/symmetries.inc"
};

constexpr SphereSymmetryList kUnifiedSymmetries[NUM_SYMMETRY_GROUPS] = {
#include "shapelogic/grid/tabs_2/unifiedsym.inc"
};

constexpr unsigned char kTransformationMinimizer[NUM_SYMMETRY_GROUPS][NUM_TRANSFORMATIONS_MIRROR] = {
#include "shapelogic/grid/tabs_2/transformmini.inc"
};

constexpr SphereSymmetryList kUniqueSymmetries[NUM_SYMMETRY_GROUPS] = {
#include "shapelogic/grid/tabs_2/uniquesym.inc"
};

/// One appended symmetry list with everything the queries need.
struct Group {
  SphereSymmetryList sym;
  SphereSymmetryList unified;
  SphereSymmetryList unique;
  unsigned char minimizer[NUM_TRANSFORMATIONS_MIRROR] = {};
};

/// A view of one list, generated or appended.
struct GroupRef {
  const SphereSymmetryList& sym;
  const SphereSymmetryList& unified;
  const SphereSymmetryList& unique;
  const unsigned char* minimizer;
};

/// The sphere grid's Symmetries: the generated tables, and the lists met at
/// run time appended after them. The registry is a memo of a pure function
/// (a shape's list), only ever appended to, so it is the one piece of
/// mutable state shared by every puzzle (decision 0020).
///
/// Lookups are lock free: an appended list is stored in a block that never
/// moves, and the count is published with a release store once the entry
/// is complete. Only appending takes the mutex. The generated lists, the
/// ones the rotation checks almost always meet, are read straight from the
/// tables.
class SphereSymmetries final : public Symmetries {
 public:
  // constexpr: the instance is constant-initialized (constinit), so using it
  // needs no initialization guard (the assemblers' rotation checks call it
  // in their inner loops)
  constexpr SphereSymmetries() = default;

  unsigned int getNumTransformations() const override { return NUM_TRANSFORMATIONS; }
  unsigned int getNumTransformationsMirror() const override { return NUM_TRANSFORMATIONS_MIRROR; }

  bool symmetryContainsMirror(Symmetry sym) const override {
    // any bit at or above NUM_TRANSFORMATIONS is a mirror transformation
    const SphereSymmetryList& s = group(sym).sym;
    for (unsigned int t = NUM_TRANSFORMATIONS; t < NUM_TRANSFORMATIONS_MIRROR; t++) {
      if (s.get(t)) {
        return true;
      }
    }
    return false;
  }

  unsigned char transAdd(unsigned char t1, unsigned char t2) const override {
    BT_ASSERT(t1 < NUM_TRANSFORMATIONS_MIRROR);
    BT_ASSERT(t2 < NUM_TRANSFORMATIONS_MIRROR);
    return kTransMult[t1][t2];
  }

  bool symmetrieContainsTransformation(Symmetry s, unsigned int t) const override {
    BT_ASSERT(t < NUM_TRANSFORMATIONS_MIRROR);
    return group(s).sym.get(t);
  }

  bool isTransformationUnique(Symmetry s, unsigned int t) const override {
    BT_ASSERT(t < NUM_TRANSFORMATIONS_MIRROR);
    return group(s).unique.get(t);
  }

  unsigned char minimizeTransformation(Symmetry s, unsigned char trans) const override {
    BT_ASSERT(trans < NUM_TRANSFORMATIONS_MIRROR);
    return group(s).minimizer[trans];
  }

  unsigned int countSymmetryIntersection(Symmetry res, Symmetry s2) const override {
    return (group(res).unified & group(s2).sym).countBits();
  }

  bool symmetriesLeft(Symmetry resultSym, Symmetry s2) const override {
    SphereSymmetryList s = group(resultSym).sym & group(s2).unified;
    // the identity does not count
    s.reset(0);
    return s.notNull();
  }

  /// Every list is known: one not in the registry is added by
  /// calculateSymmetry().
  bool symmetryKnown(const Voxel& /*pp*/) const override { return true; }

  /// The generated decision tree the other grids use would answer wrongly
  /// for a list outside the generated tables, so the whole list is worked
  /// out and looked up. Voxel keeps the result, so this runs once per shape
  /// and orientation.
  Symmetry calculateSymmetry(const Voxel& pp) const override {
    return find(sphereSymmetryList(pp));
  }

  /// The number of lists, generated and appended.
  unsigned int size() const { return count_.load(std::memory_order_acquire); }

 private:
  static constexpr unsigned int kBlock = 256;
  // enough blocks for every number below kSymmetryInvalid
  static constexpr unsigned int kBlocks = (kSymmetryInvalid + kBlock - 1) / kBlock;

  GroupRef group(Symmetry s) const {
    if (s < NUM_SYMMETRY_GROUPS) {
      return {kSymmetries[s], kUnifiedSymmetries[s], kUniqueSymmetries[s],
              kTransformationMinimizer[s]};
    }
    BT_ASSERT(s < size());
    const Group& g = appended(s);
    return {g.sym, g.unified, g.unique, g.minimizer};
  }

  const Group& appended(unsigned int s) const {
    const unsigned int i = s - NUM_SYMMETRY_GROUPS;
    return blocks_[i / kBlock].load(std::memory_order_acquire)[i % kBlock];
  }

  /// The number of list `s`, appended when it is new.
  Symmetry find(const SphereSymmetryList& s) const {
    for (unsigned int i = 0; i < NUM_SYMMETRY_GROUPS; i++) {
      if (kSymmetries[i] == s) {
        return static_cast<Symmetry>(i);
      }
    }
    const unsigned int n = size();
    unsigned int i = scan(s, NUM_SYMMETRY_GROUPS, n);
    if (i < n) {
      return static_cast<Symmetry>(i);
    }

    const std::lock_guard<std::mutex> lock(grow_);
    // another thread may have appended it in the meantime
    const unsigned int m = count_.load(std::memory_order_relaxed);
    i = scan(s, n, m);
    if (i < m) {
      return static_cast<Symmetry>(i);
    }
    BT_ASSERT(m < kSymmetryInvalid);
    const unsigned int slot = m - NUM_SYMMETRY_GROUPS;
    Group* block = blocks_[slot / kBlock].load(std::memory_order_relaxed);
    if (block == nullptr) {
      owners_.push_back(std::make_unique<Group[]>(kBlock));
      block = owners_.back().get();
      blocks_[slot / kBlock].store(block, std::memory_order_release);
    }
    Group& g = block[slot % kBlock];
    g.sym = s;
    deriveSphereTables(s, g.unified, g.unique, g.minimizer);
    count_.store(m + 1, std::memory_order_release);
    return static_cast<Symmetry>(m);
  }

  /// The first appended list in [from, to) equal to s, or to.
  unsigned int scan(const SphereSymmetryList& s, unsigned int from, unsigned int to) const {
    for (unsigned int i = from; i < to; i++) {
      if (appended(i).sym == s) {
        return i;
      }
    }
    return to;
  }

  // the blocks of appended lists, as kept by owners_ (guarded by grow_)
  mutable std::array<std::atomic<Group*>, kBlocks> blocks_{};
  mutable std::atomic<unsigned int> count_{NUM_SYMMETRY_GROUPS};
  mutable std::mutex grow_;
  mutable std::vector<std::unique_ptr<Group[]>> owners_;
};

constinit const SphereSymmetries kInstance{};

}  // namespace

void deriveSphereTables(const SphereSymmetryList& sym, SphereSymmetryList& unified,
                        SphereSymmetryList& unique, unsigned char* minimizer) {
  // the lowest transformation t with a symmetry followed by t equal to
  // trans gives the same orientation as trans
  for (unsigned int trans = 0; trans < NUM_TRANSFORMATIONS_MIRROR; trans++) {
    auto m = static_cast<unsigned char>(trans);
    for (unsigned int t = 0; t < trans && m == trans; t++) {
      for (unsigned int t2 = 0; t2 < NUM_TRANSFORMATIONS_MIRROR; t2++) {
        if (sym.get(t2) && kTransMult[t2][t] == trans) {
          m = static_cast<unsigned char>(t);
          break;
        }
      }
    }
    minimizer[trans] = m;
  }

  // if r, x, inverse of r is a symmetry of the shape, then x is a symmetry
  // of the shape rotated by r
  unified = sym;
  for (unsigned int r = 1; r < NUM_TRANSFORMATIONS_MIRROR; r++) {
    unsigned int rinv = 0;
    while (rinv < NUM_TRANSFORMATIONS_MIRROR && kTransMult[r][rinv] != 0) {
      rinv++;
    }
    if (rinv == NUM_TRANSFORMATIONS_MIRROR) {
      continue;
    }
    for (unsigned int x = 0; x < NUM_TRANSFORMATIONS_MIRROR; x++) {
      unsigned char res = kTransMult[r][x];
      if (res == TND) {
        continue;
      }
      res = kTransMult[res][rinv];
      if (res == TND) {
        continue;
      }
      if (sym.get(res)) {
        unified.set(x);
      }
    }
  }

  // one transformation for each orientation: once r is taken, every
  // symmetry followed by r gives the same orientation and is skipped
  unique = SphereSymmetryList{};
  SphereSymmetryList seen;
  for (unsigned int r = 0; r < NUM_TRANSFORMATIONS_MIRROR; r++) {
    if (seen.get(r)) {
      continue;
    }
    unique.set(r);
    for (unsigned int r2 = 0; r2 < NUM_TRANSFORMATIONS_MIRROR; r2++) {
      if (sym.get(r2) && kTransMult[r2][r] != TND) {
        seen.set(kTransMult[r2][r]);
      }
    }
  }
}

SphereSymmetryList sphereSymmetryList(const Voxel& pp) {
  // a failed transformation counts as "not the same" (as in the original)
  SphereSymmetryList s;
  s.set(0);
  for (unsigned int j = 1; j < NUM_TRANSFORMATIONS_MIRROR; j++) {
    auto v = pp.clone();
    if (v->transform(j) && pp.identicalInBB(*v)) {
      s.set(j);
    }
  }
  return s;
}

unsigned int numSphereSymmetryLists() { return kInstance.size(); }

constinit const Symmetries* const kSymmetriesTable2 = &kInstance;

}  // namespace bt::detail

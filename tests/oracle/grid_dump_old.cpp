// SPDX-License-Identifier: GPL-3.0-or-later
// Records tests/data/grid_dump.txt from the ORIGINAL BurrTools: the grid
// dump of tests/unit/grid/grid_dump.h, run against the original's library.
// Build and run with `python3 tests/oracle/oracle.py grid-dump` (needs the
// original built, so build/libburr_core.a exists there).

#include "lib/gridtype.h"
#include "lib/symmetries.h"
#include "lib/voxel.h"
#include "lib/voxeltable.h"

#include <iostream>
#include <memory>
#include <vector>

#include "../unit/grid/grid_dump.h"

namespace {

struct Old {
  using Voxel = voxel_c;
  static constexpr int kEmpty = voxel_c::VX_EMPTY;
  static constexpr int kFilled = voxel_c::VX_FILLED;
  static constexpr int kVariable = voxel_c::VX_VARIABLE;

  static const gridType_c& grid(int g) {
    static const gridType_c grids[] = {
        gridType_c(gridType_c::GT_BRICKS), gridType_c(gridType_c::GT_TRIANGULAR_PRISM),
        gridType_c(gridType_c::GT_SPHERES), gridType_c(gridType_c::GT_RHOMBIC),
        gridType_c(gridType_c::GT_TETRA_OCTA)};
    return grids[g];
  }
  static const symmetries_c* sym(int g) { return grid(g).getSymmetries(); }

  static std::unique_ptr<voxel_c> make(int g, unsigned x, unsigned y, unsigned z) {
    return std::unique_ptr<voxel_c>(grid(g).getVoxel(x, y, z, 0));
  }
  static std::unique_ptr<voxel_c> copy(const voxel_c& v) {
    return std::unique_ptr<voxel_c>(v.getGridType()->getVoxel(&v));
  }
  static unsigned numTrans(int g) { return sym(g)->getNumTransformations(); }
  static unsigned numTransMirror(int g) { return sym(g)->getNumTransformationsMirror(); }
  static unsigned numGroups(int g) { return g == 0 ? 98 : g == 1 ? 54 : g == 2 ? 241 : 98; }
  static bool contains(int g, unsigned s, unsigned t) {
    return sym(g)->symmetrieContainsTransformation(static_cast<symmetries_t>(s), t);
  }
  static bool unique(int g, unsigned s, unsigned t) {
    return sym(g)->isTransformationUnique(static_cast<symmetries_t>(s), t);
  }
  static unsigned minimize(int g, unsigned s, unsigned t) {
    return sym(g)->minimizeTransformation(static_cast<symmetries_t>(s),
                                          static_cast<unsigned char>(t));
  }
  static unsigned transAdd(int g, unsigned a, unsigned b) {
    return sym(g)->transAdd(static_cast<unsigned char>(a), static_cast<unsigned char>(b));
  }
  static unsigned countIntersection(int g, unsigned a, unsigned b) {
    return sym(g)->countSymmetryIntersection(static_cast<symmetries_t>(a),
                                             static_cast<symmetries_t>(b));
  }
  static bool symmetriesLeft(int g, unsigned a, unsigned b) {
    return sym(g)->symmetriesLeft(static_cast<symmetries_t>(a), static_cast<symmetries_t>(b));
  }
  static bool containsMirror(int g, unsigned s) {
    return sym(g)->symmetryContainsMirror(static_cast<symmetries_t>(s));
  }
  static bool identical(const voxel_c& a, const voxel_c& b, bool colors) {
    return a.identicalInBB(&b, colors);
  }
  static unsigned char mirrorTransform(const voxel_c& a, const voxel_c& b) {
    return a.getMirrorTransform(&b);
  }
  static void action(voxel_c& v, int kind, bool inside) {
    v.actionOnSpace(static_cast<voxel_c::VoxelAction>(kind), inside);
  }

  class Table : public voxelTable_c {
   public:
    explicit Table(const std::vector<std::unique_ptr<voxel_c>>& s) : shapes_(s) {}
    bool getSpace(const voxel_c& v, unsigned* index, unsigned char* trans, unsigned params) const {
      return voxelTable_c::getSpace(&v, index, trans, params);
    }

   protected:
    const voxel_c* findSpace(unsigned int index) const override { return shapes_[index].get(); }

   private:
    const std::vector<std::unique_ptr<voxel_c>>& shapes_;
  };
};

}  // namespace

int main() {
  std::cout << grid_dump::dumpAll<Old>();
  return 0;
}

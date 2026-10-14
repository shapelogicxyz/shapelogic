// SPDX-License-Identifier: GPL-3.0-or-later
// Compares the port with values recorded from the original BurrTools: the
// grid dump (grid_dump.h) of the original is in tests/data/grid_dump.txt,
// recorded by tests/oracle/grid_dump_old.cpp.

#include "shapelogic/grid/grid_type.h"
#include "shapelogic/grid/symmetries.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/grid/voxel_table.h"
#include "grid_dump.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>
#include <string>

namespace {

struct New {
  using Voxel = bt::Voxel;
  static constexpr int kEmpty = bt::Voxel::kEmpty;
  static constexpr int kFilled = bt::Voxel::kFilled;
  static constexpr int kVariable = bt::Voxel::kVariable;

  static bt::GridType grid(int g) { return bt::GridType(static_cast<bt::GridKind>(g)); }
  static const bt::Symmetries& sym(int g) { return grid(g).getSymmetries(); }

  static std::unique_ptr<bt::Voxel> make(int g, unsigned x, unsigned y, unsigned z) {
    return grid(g).makeVoxel(x, y, z, 0);
  }
  static std::unique_ptr<bt::Voxel> copy(const bt::Voxel& v) { return v.clone(); }
  static unsigned numTrans(int g) { return sym(g).getNumTransformations(); }
  static unsigned numTransMirror(int g) { return sym(g).getNumTransformationsMirror(); }
  static unsigned numGroups(int g) { return g == 0 ? 98 : g == 1 ? 54 : g == 2 ? 241 : 98; }
  static bt::Symmetry s8(unsigned s) { return static_cast<bt::Symmetry>(s); }
  static unsigned char t8(unsigned t) { return static_cast<unsigned char>(t); }
  static bool contains(int g, unsigned s, unsigned t) {
    return sym(g).symmetrieContainsTransformation(s8(s), t);
  }
  static bool unique(int g, unsigned s, unsigned t) {
    return sym(g).isTransformationUnique(s8(s), t);
  }
  static unsigned minimize(int g, unsigned s, unsigned t) {
    return sym(g).minimizeTransformation(s8(s), t8(t));
  }
  static unsigned transAdd(int g, unsigned a, unsigned b) {
    return sym(g).transAdd(t8(a), t8(b));
  }
  static unsigned countIntersection(int g, unsigned a, unsigned b) {
    return sym(g).countSymmetryIntersection(s8(a), s8(b));
  }
  static bool symmetriesLeft(int g, unsigned a, unsigned b) {
    return sym(g).symmetriesLeft(s8(a), s8(b));
  }
  static bool containsMirror(int g, unsigned s) { return sym(g).symmetryContainsMirror(s8(s)); }
  static bool identical(const bt::Voxel& a, const bt::Voxel& b, bool colors) {
    return a.identicalInBB(b, colors);
  }
  static unsigned char mirrorTransform(const bt::Voxel& a, const bt::Voxel& b) {
    return a.getMirrorTransform(b);
  }
  static void action(bt::Voxel& v, int kind, bool inside) {
    v.actionOnSpace(static_cast<bt::Voxel::Action>(kind), inside);
  }

  class Table {
   public:
    explicit Table(const std::vector<std::unique_ptr<bt::Voxel>>& s)
        : table_([&s](unsigned int i) -> const bt::Voxel& { return *s[i]; }) {}
    void addSpace(unsigned int index, unsigned int params) { table_.addSpace(index, params); }
    bool getSpace(const bt::Voxel& v, unsigned* index, unsigned char* trans,
                  unsigned params) const {
      return table_.getSpace(v, index, trans, params);
    }

   private:
    bt::VoxelTable table_;
  };
};

}  // namespace

TEST_CASE("grid dump: every grid computes what the original computes", "[grid][oracle]") {
  std::ifstream in("tests/data/grid_dump.txt");
  REQUIRE(in.good());
  std::stringstream expected;
  expected << in.rdbuf();

  std::istringstream want(expected.str());
  std::istringstream got(grid_dump::dumpAll<New>());

  std::string w;
  std::string g;
  unsigned int line = 0;
  unsigned int mismatches = 0;
  while (std::getline(want, w)) {
    line++;
    if (!std::getline(got, g)) {
      g = "<end of the port's dump>";
    }
    if (w != g && mismatches++ < 10) {
      UNSCOPED_INFO("line " << line << "\n  original: " << w << "\n  port:     " << g);
    }
  }
  if (std::getline(got, g)) {
    UNSCOPED_INFO("the port's dump has extra lines from: " << g);
    mismatches++;
  }
  INFO(line << " lines compared");
  CHECK(line > 7000);
  REQUIRE(mismatches == 0);
}

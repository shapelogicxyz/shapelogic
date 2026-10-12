// SPDX-License-Identifier: GPL-3.0-or-later
// The STL export through the document: a solution's pieces (the
// original's Export Solution Pieces to STL, src/gui/stlexportsolution.cpp)
// with their names and files compared with what the original writes for
// CubeInCage (tests/data/stl_solution_by_original/, `oracle.py
// stl-solution`), and the names made safe and distinct.
#include "app_test_helpers.h"

#include "bt/app/document.h"
#include "bt/mesh/stl.h"
#include "bt/model/problem.h"
#include "bt/model/solution.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

using namespace bt;
using namespace bt::app;

namespace {

const std::string kRecorded = "tests/data/stl_solution_by_original/CubeInCage/";

std::vector<std::uint8_t> readBytes(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

Id firstSolution(const Document& d) {
  return d.readProblem(0, [](const Problem& p) { return p.getSavedSolution(0).getId(); });
}

std::vector<double> defaults(const Document& d) {
  std::vector<double> values;
  for (const mesh::StlParameter& p : mesh::stlParameters(d.puzzle().getGridType().getType())) {
    values.push_back(p.defaultValue);
  }
  return values;
}

}  // namespace

TEST_CASE("STL export: a solution's pieces and files equal the original's for CubeInCage",
          "[app][stl]") {
  const Document d = test::openFixture("CubeInCage.xmpuzzle");
  const Result<std::vector<StlPiece>> pieces = d.stlPieces(d.problemIds()[0], firstSolution(d));
  REQUIRE(pieces);
  std::ostringstream list;
  for (const StlPiece& p : *pieces) {
    list << p.shape << ' ' << p.count << ' ' << p.fileName << '\n';
  }
  std::ifstream recorded(kRecorded + "pieces.txt");
  const std::string theirs{std::istreambuf_iterator<char>(recorded),
                           std::istreambuf_iterator<char>()};
  CHECK(list.str() == theirs);
  for (const StlPiece& p : *pieces) {
    INFO(p.fileName);
    const Result<mesh::Polyhedron> m = d.stlMesh(d.shapeIds()[p.shape], defaults(d));
    REQUIRE(m);
    CHECK(mesh::stlFile(*m, p.fileName, true) == readBytes(kRecorded + p.fileName));
  }
}

TEST_CASE("STL export: piece names are made safe and told apart", "[app][stl]") {
  Document d = test::openFixture("CubeInCage.xmpuzzle");
  const std::vector<Id> shapes = d.shapeIds();
  REQUIRE(d.renameShape(shapes[3], "Frame1"));
  REQUIRE(d.renameShape(shapes[4], "a b/c.d"));
  REQUIRE(d.renameShape(shapes[5], "\xc3\x96l"));  // UTF-8: two bytes, two '_'
  const Result<std::vector<StlPiece>> pieces = d.stlPieces(d.problemIds()[0], firstSolution(d));
  REQUIRE(pieces);
  std::vector<std::string> names;
  for (const StlPiece& p : *pieces) {
    names.push_back(p.fileName);
  }
  CHECK(names == std::vector<std::string>{"Frame1.stl", "Frame2.stl", "Frame1-S4.stl",
                                          "a_b_c_d.stl", "__l.stl"});
  // an unknown solution
  const Result<std::vector<StlPiece>> none = d.stlPieces(d.problemIds()[0], 999999);
  REQUIRE_FALSE(none.has_value());
  CHECK(none.error().code == Errc::NotFound);
}

TEST_CASE("STL export: a faulty mesh is refused as the original's dialog said", "[app][stl]") {
  Document d = test::openFixture("BallRoom.xmpuzzle");
  std::vector<double> values = defaults(d);
  values[4] = 6;  // recursions: the original's mesh fails an assertion
  const Result<mesh::Polyhedron> faulty = d.stlMesh(d.shapeIds()[1], values);
  REQUIRE_FALSE(faulty.has_value());
  CHECK(faulty.error().code == Errc::InvalidArgument);
  CHECK(faulty.error().message ==
        "The generated mesh is faulty in some way, try to tweak the parameter");
  values[4] = 7;  // not built at all
  const Result<mesh::Polyhedron> large = d.stlMesh(d.shapeIds()[1], values);
  REQUIRE_FALSE(large.has_value());
  CHECK(large.error().message == "More than 6 recursions make a mesh too large to build");
}

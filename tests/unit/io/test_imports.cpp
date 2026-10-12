// SPDX-License-Identifier: GPL-3.0-or-later
// The imports (PuzzleSolver3D, Puzzlecad), the Puzzlecad export and grid
// conversion (deferred part 6): the original's cases from
// test/test_puzzle_model.cpp ("ps3d loader", "scad loader"), then the
// committed inputs of tests/data/imports/ and the cube-grid fixtures
// against what the original made of them (tests/data/imports_by_original/,
// `oracle.py record-imports`), through the document as the engine calls it.
#include "bt/app/document.h"
#include "bt/grid/voxel.h"
#include "bt/io/gzip_file.h"
#include "bt/io/imports.h"
#include "bt/io/puzzle_file.h"
#include "bt/model/grid_convert.h"
#include "bt/model/problem.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

using namespace bt;
using namespace bt::app;

namespace {

const std::filesystem::path kInputs = "tests/data/imports";
const std::filesystem::path kRecorded = "tests/data/imports_by_original";

std::string readFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  REQUIRE(in.good());
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// The XML of a recorded save (the original wrote it gzip-compressed).
std::string recordedXml(const std::filesystem::path& path) {
  Result<std::string> xml = io::gunzipIfCompressed(readFile(path));
  REQUIRE(xml.has_value());
  return *xml;
}

std::string savedXml(const Document& d) {
  Result<std::string> xml = d.saveBytes();
  REQUIRE(xml.has_value());
  return *xml;
}

// The cube-grid fixtures (the ones the original converts and exports).
std::vector<std::filesystem::path> cubeFixtures() {
  std::vector<std::filesystem::path> out;
  for (const auto& e : std::filesystem::directory_iterator("tests/data/puzzles")) {
    if (e.path().extension() != ".xmpuzzle") {
      continue;
    }
    Result<Puzzle> p = io::loadPuzzle(e.path());
    REQUIRE(p.has_value());
    if (p->getGridType().getType() == GridKind::Bricks) {
      out.push_back(e.path());
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

}  // namespace

// ---- the original's cases ------------------------------------------------------

TEST_CASE("ps3d loader: reads a file, and turns down a broken one without crashing",
          "[io][imports]") {
  // each row holds the z layers side by side, sx cells and a separator
  {
    Result<Puzzle> p = io::readPuzzleSolver3D("PIECE 2x1x2\nXX X\nRESULT 2x1x2\nXX XX\n");
    REQUIRE(p.has_value());
    REQUIRE(p->getNumberOfShapes() == 2);
    // the short row is empty past its end
    CHECK(p->getShape(0).countState(Voxel::kFilled) == 3);
    CHECK(p->getShape(1).countState(Voxel::kFilled) == 4);
  }
  CHECK_FALSE(io::readPuzzleSolver3D("PIECE axbxc\nXX\n").has_value());
  CHECK_FALSE(io::readPuzzleSolver3D("PIECE 0x1x1\n\n").has_value());
  {
    Result<Puzzle> p = io::readPuzzleSolver3D("");
    REQUIRE(p.has_value());
    CHECK(p->getNumberOfShapes() == 0);
  }
}

TEST_CASE("scad loader: reads a plate, and refuses deep nesting without crashing",
          "[io][imports]") {
  {
    Result<Puzzle> p = io::readPuzzlecad("burr_plate([ \"xx|x.\", [\"x\", \"x\"] ]);");
    REQUIRE(p.has_value());
    CHECK(p->getNumberOfShapes() == 3);  // two pieces and the result
  }
  {
    const std::string deep = "burr_plate($x = " + std::string(100000, '[') +
                             std::string(100000, ']') + ", [\"x\"]);";
    // the named argument cannot be skipped, so the plate is not read
    CHECK_FALSE(io::readPuzzlecad(deep).has_value());
  }
}

// ---- against the original ------------------------------------------------------

TEST_CASE("imports: each input gives the puzzle the original's import saved", "[io][imports]") {
  unsigned int compared = 0;
  unsigned int refused = 0;
  for (const auto& e : std::filesystem::directory_iterator(kInputs)) {
    const std::filesystem::path& in = e.path();
    const std::string ext = in.extension().string();
    if (ext != ".puz" && ext != ".scad") {
      continue;
    }
    const ImportFormat format =
        ext == ".puz" ? ImportFormat::PuzzleSolver3D : ImportFormat::Puzzlecad;
    const std::string stem = in.stem().string() + "_" + ext.substr(1);
    INFO(in.string());

    Result<Document> d = Document::importFile(format, in);
    if (std::filesystem::exists(kRecorded / (stem + ".refused"))) {
      REQUIRE_FALSE(d.has_value());
      CHECK(d.error().code == Errc::ParseError);
      refused++;
      continue;
    }
    REQUIRE(d.has_value());
    CHECK(savedXml(*d) == recordedXml(kRecorded / (stem + ".xmpuzzle")));
    // a new, untitled puzzle: dirty, nothing to undo
    CHECK(d->dirty());
    CHECK_FALSE(d->canUndo());
    // the same from the bytes
    Result<Document> b = Document::importBytes(format, readFile(in));
    REQUIRE(b.has_value());
    CHECK(savedXml(*b) == savedXml(*d));
    compared++;
  }
  CHECK(compared == 7);
  CHECK(refused == 6);
}

TEST_CASE("imports: each cube-grid fixture converts to what the original made", "[io][imports]") {
  const std::vector<std::filesystem::path> fixtures = cubeFixtures();
  REQUIRE(fixtures.size() == 9);
  for (const std::filesystem::path& f : fixtures) {
    INFO(f.string());
    Result<Document> d = Document::open(f);
    REQUIRE(d.has_value());
    for (const auto& [grid, name] : {std::pair{GridKind::Rhombic, "rhombic"},
                                     std::pair{GridKind::TetraOcta, "tetra-octa"}}) {
      Result<Document> c = d->convertGrid(grid);
      REQUIRE(c.has_value());
      CHECK(savedXml(*c) ==
            recordedXml(kRecorded / (f.stem().string() + "." + name + ".xmpuzzle")));
      CHECK(c->dirty());
      CHECK_FALSE(c->canUndo());
      CHECK(c->puzzle().getGridType().getType() == grid);
    }
    // the document converted from is as it was
    CHECK_FALSE(d->dirty());
    Result<Document> other = d->convertGrid(GridKind::TriangularPrism);
    REQUIRE_FALSE(other.has_value());
    CHECK(other.error().code == Errc::Unsupported);
  }
}

TEST_CASE("imports: each cube-grid fixture exports the Puzzlecad file the original wrote",
          "[io][imports]") {
  for (const std::filesystem::path& f : cubeFixtures()) {
    INFO(f.string());
    Result<Document> d = Document::open(f);
    REQUIRE(d.has_value());
    const std::vector<Id>& problems = d->problemIds();
    // each problem, then none (the original's index past the last)
    for (std::size_t i = 0; i <= problems.size(); i++) {
      const std::optional<Id> problem =
          i < problems.size() ? std::optional<Id>(problems[i]) : std::nullopt;
      const Result<std::string> text = d->exportPuzzlecad(problem, f.filename().string());
      REQUIRE(text.has_value());
      CHECK(*text == readFile(kRecorded / (f.stem().string() + ".p" + std::to_string(i) +
                                           ".scad")));
      // and the original's export is read back to the same pieces
      Result<Puzzle> back = io::readPuzzlecad(*text);
      REQUIRE(back.has_value());
    }
  }
}

// ---- refusals and limits -------------------------------------------------------

TEST_CASE("imports: only cube-grid puzzles with shapes export to Puzzlecad", "[io][imports]") {
  Result<Document> prisms = Document::open("tests/data/puzzles/Bermuda.xmpuzzle");
  REQUIRE(prisms.has_value());
  Result<std::string> r = prisms->exportPuzzlecad(std::nullopt, "Bermuda.xmpuzzle");
  REQUIRE_FALSE(r.has_value());
  CHECK(r.error().message == "Puzzlecad export is only available for cube-grid puzzles.");

  const Document empty(GridKind::Bricks);
  r = empty.exportPuzzlecad(std::nullopt, "");
  REQUIRE_FALSE(r.has_value());
  CHECK(r.error().message == "Nothing to export.");

  // shapes without a cell to write
  Document blank(GridKind::Bricks);
  REQUIRE(blank.createShape().has_value());
  r = blank.exportPuzzlecad(std::nullopt, "");
  REQUIRE_FALSE(r.has_value());
  CHECK(r.error().message == "Could not export puzzlecad file.");

  // an unknown problem
  Result<Document> d = Document::open("tests/data/puzzles/PelikanBurr.xmpuzzle");
  REQUIRE(d.has_value());
  r = d->exportPuzzlecad(Id{999999}, "");
  REQUIRE_FALSE(r.has_value());
  CHECK(r.error().code == Errc::NotFound);
}

TEST_CASE("imports: the header names the file, without its folders", "[io][imports]") {
  Result<Document> d = Document::open("tests/data/puzzles/PelikanBurr.xmpuzzle");
  REQUIRE(d.has_value());
  const auto header = [&d](std::string_view name) {
    const Result<std::string> text = d->exportPuzzlecad(std::nullopt, name);
    REQUIRE(text.has_value());
    const std::size_t at = text->find("export for the file:\n// ") + 24;
    return text->substr(at, text->find('\n', at) - at);
  };
  CHECK(header("/a/b/Pelikan.xmpuzzle") == "Pelikan.xmpuzzle");
  CHECK(header("C:\\x\\Pelikan.xmpuzzle") == "Pelikan.xmpuzzle");
  CHECK(header("") == "unknown");
  CHECK(header("folder/") == "unknown");
}

TEST_CASE("imports: shapes past the cell budget are refused before they are made",
          "[io][imports]") {
  // a few bytes asked the original for 500x500x500 cells per line
  // (the line after a size line is the shape's first row)
  const std::string two = "PIECE 10x1x1\nXXXXXXXXXX\nPIECE 10x1x2\nX\n";
  CHECK(io::readPuzzleSolver3D(two, 30).has_value());
  Result<Puzzle> p = io::readPuzzleSolver3D(two, 29);
  REQUIRE_FALSE(p.has_value());
  CHECK(p.error().code == Errc::ParseError);
  // the largest shape the format allows fits the budget
  static_assert(std::uint64_t{500} * 500 * 500 <= io::kMaxImportCells);

  // Puzzlecad: the pieces, the box and the cube of variable cells count
  CHECK(io::readPuzzlecad(R"(burr_piece("xx|xx"); burr_piece("xxx");)", 4 + 3 + 27).has_value());
  CHECK_FALSE(io::readPuzzlecad(R"(burr_piece("xx|xx"); burr_piece("xxx");)", 4 + 3 + 26).has_value());
  CHECK_FALSE(io::readPuzzlecad(R"(burr_piece("xx|xx"); burr_piece("xxx");)", 5).has_value());
  CHECK_FALSE(io::readPuzzlecad(R"(burr_piece("xx"); packing_box("xxx|xxx");)", 7).has_value());

  // a conversion makes 125 cells of each cube
  Result<Puzzle> pelikan = io::loadPuzzle("tests/data/puzzles/PelikanBurr.xmpuzzle");
  REQUIRE(pelikan.has_value());
  std::uint64_t cubes = 0;
  for (unsigned int i = 0; i < pelikan->getNumberOfShapes(); i++) {
    cubes += pelikan->getShape(i).getXYZ();
  }
  CHECK(convertGrid(*pelikan, GridKind::Rhombic, cubes * 125).has_value());
  Result<Puzzle> c = convertGrid(*pelikan, GridKind::Rhombic, (cubes * 125) - 1);
  REQUIRE_FALSE(c.has_value());
  CHECK(c.error().code == Errc::InvalidArgument);
}

TEST_CASE("imports: the targets of a conversion", "[io][imports]") {
  CHECK(convertTargets(GridKind::Bricks) ==
        std::vector<GridKind>{GridKind::Rhombic, GridKind::TetraOcta});
  for (const GridKind g : {GridKind::TriangularPrism, GridKind::Spheres, GridKind::Rhombic,
                           GridKind::TetraOcta, GridKind::Sliding, GridKind::Stacking}) {
    CHECK(convertTargets(g).empty());
  }
}

TEST_CASE("imports: files are read as they are", "[io][imports]") {
  // a gzip-compressed Puzzlecad file is not one (std::ifstream read it so)
  const std::filesystem::path tmp =
      std::filesystem::temp_directory_path() /
      ("bt-import-" + std::to_string(std::random_device{}()) + ".scad");
  {
    std::ofstream out(tmp, std::ios::binary);
    out << io::gzipCompress(R"(burr_piece("xx");)");
  }
  Result<Document> d = Document::importFile(ImportFormat::Puzzlecad, tmp);
  std::filesystem::remove(tmp);
  REQUIRE_FALSE(d.has_value());
  CHECK(d.error().code == Errc::ParseError);

  // a missing file
  const Result<Document> missing =
      Document::importFile(ImportFormat::PuzzleSolver3D, kInputs / "no-such-file.puz");
  REQUIRE_FALSE(missing.has_value());
  CHECK(missing.error().code == Errc::IoError);
}

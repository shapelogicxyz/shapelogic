// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from test/test_roundtrip.cpp of the original. The gzstream cases
// test the zlib file functions that replace it.

#include "../grid/grid_test_helpers.h"
#include "../model/model_test_helpers.h"

#include "shapelogic/io/gzip_file.h"
#include "shapelogic/io/puzzle_file.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

using namespace bt;
using bt::test::fromLayers;
using bt::test::puzzlesRoundtripEqual;

namespace {

/// A scratch directory, removed when the guard goes.
class TempDir {
 public:
  TempDir() {
    static int counter = 0;
    path_ = std::filesystem::temp_directory_path() /
            ("bt-test-" + std::to_string(counter++) + "-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(path_);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;

  [[nodiscard]] std::filesystem::path file(const char* name) const { return path_ / name; }

 private:
  std::filesystem::path path_;
};

Puzzle reloaded(const Puzzle& p) {
  Result<Puzzle> r = io::parsePuzzle(io::writePuzzle(p));
  REQUIRE(r.has_value());
  return std::move(*r);
}

}  // namespace

TEST_CASE("gzip file: a plain text file is read back unchanged", "[io][gzip]") {
  TempDir dir;
  const auto name = dir.file("plain.txt");
  {
    std::ofstream out(name);
    out << "<puzzle/>";
  }
  const auto got = io::readMaybeGzipFile(name);
  REQUIRE(got.has_value());
  REQUIRE(*got == "<puzzle/>");
}

TEST_CASE("gzip file: a gzipped file is decompressed, and really compressed", "[io][gzip]") {
  TempDir dir;
  const auto name = dir.file("longer.gz");

  std::string payload;
  for (int i = 0; i < 500; i++) {
    payload += "<voxel>0123456789</voxel>";
  }
  REQUIRE(io::writeGzipFile(name, payload).has_value());

  // 12500 bytes of payload come to about a hundred on disk
  REQUIRE(std::filesystem::file_size(name) < payload.size() / 10);

  const auto got = io::readMaybeGzipFile(name);
  REQUIRE(got.has_value());
  REQUIRE(*got == payload);
}

TEST_CASE("gzip file: a missing file is an error, an empty file is empty", "[io][gzip]") {
  TempDir dir;
  const auto missing = io::readMaybeGzipFile(dir.file("does-not-exist.xmpuzzle"));
  REQUIRE_FALSE(missing.has_value());
  REQUIRE(missing.error().code == Errc::IoError);

  const auto name = dir.file("empty.xmpuzzle");
  { std::ofstream out(name); }
  const auto empty = io::readMaybeGzipFile(name);
  REQUIRE(empty.has_value());
  REQUIRE(empty->empty());

  // and an empty file is not a puzzle
  const auto puzzle = io::loadPuzzle(name);
  REQUIRE_FALSE(puzzle.has_value());
  REQUIRE(puzzle.error().code == Errc::ParseError);
}

TEST_CASE("puzzle: an in-memory puzzle survives a save and reload", "[io][roundtrip]") {
  const GridType gt(GridKind::Bricks);
  Puzzle original(gt);
  original.setComment("a test puzzle");
  original.setCommentPopup(true);
  original.addColor(10, 20, 30);
  original.addColor(40, 50, 60);

  auto lShape = fromLayers(gt, {{"##", "#."}});
  lShape->setColor(0, 0, 0, 1);
  original.addShape(std::move(lShape));
  original.addShape(fromLayers(gt, {{"###"}}));

  const unsigned int prob = original.addProblem();
  original.getProblem(prob).setName("a problem");

  REQUIRE(puzzlesRoundtripEqual(original, reloaded(original)));
}

TEST_CASE("puzzle: a shape's own colour survives a save and reload", "[io][roundtrip]") {
  Puzzle original;
  original.addShape(2, 1, 1);
  original.addShape(1, 1, 1);
  original.getShape(0).setShapeColor(0x12AB7F);

  const std::string saved = io::writePuzzle(original);
  CHECK(saved.find("color=\"#12AB7F\"") != std::string::npos);

  const Puzzle restored = reloaded(original);
  CHECK(restored.getShape(0).getShapeColor() == 0x12AB7F);
  CHECK(restored.getShape(1).getShapeColor() == -1);
}

TEST_CASE("problem: Solver tab settings are saved per problem", "[io][roundtrip]") {
  const GridType gt(GridKind::Bricks);
  Puzzle original(gt);
  original.addShape(fromLayers(gt, {{"###"}}));

  const unsigned int withOpts = original.addProblem();
  const unsigned int withoutOpts = original.addProblem();
  const std::map<std::string, std::string> opts = {
      {"disassemble", "1"}, {"justCount", "0"}, {"solverType", "crowell"}, {"sortBy", "level"}};
  original.getProblem(withOpts).setSolverOptions(opts);

  Puzzle restored = reloaded(original);
  REQUIRE(restored.getProblem(withOpts).getSolverOptions() == opts);
  REQUIRE(restored.getProblem(withoutOpts).getSolverOptions().empty());

  // a copied problem keeps the settings
  const unsigned int copy = restored.addProblem(restored.getProblem(withOpts));
  REQUIRE(restored.getProblem(copy).getSolverOptions() == opts);
}

TEST_CASE("puzzle: a comment made up only of XML-special characters survives", "[io][roundtrip]") {
  Puzzle original;
  original.setComment("<&>\"'");
  original.setCommentPopup(true);

  const Puzzle restored = reloaded(original);
  REQUIRE(restored.getComment() == "<&>\"'");
  REQUIRE(puzzlesRoundtripEqual(original, restored));
}

TEST_CASE("puzzle: saving a reloaded puzzle reproduces the same document", "[io][roundtrip]") {
  const GridType gt(GridKind::Bricks);
  Puzzle original(gt);
  original.setComment("fixpoint");
  original.setCommentPopup(true);
  original.addColor(10, 20, 30);
  original.addColor(40, 50, 60);

  auto shape = fromLayers(gt, {{"#.", ".#"}});
  shape->setName("fixpoint shape");
  shape->setWeight(7);
  shape->setHotspot(1, 1, 0);
  shape->setColor(0, 0, 0, 1);
  const unsigned int shapeIdx = original.addShape(std::move(shape));

  const unsigned int prob = original.addProblem();
  Problem& p = original.getProblem(prob);
  p.setName("fixpoint problem");
  p.setResultId(original, shapeIdx);
  p.setShapeMinimum(original, shapeIdx, 1);
  p.setShapeMaximum(original, shapeIdx, 2);
  p.setMaxHoles(3);
  p.allowPlacement(original, 1, 2);

  const std::string first = io::writePuzzle(original);
  const Puzzle restored = reloaded(original);
  REQUIRE(io::writePuzzle(restored) == first);

  // fields the narrow comparison does not look at
  const Voxel& restoredShape = restored.getShape(shapeIdx);
  REQUIRE(restoredShape.getName() == "fixpoint shape");
  REQUIRE(restoredShape.getWeight() == 7);
  int hx = 0;
  int hy = 0;
  int hz = 0;
  REQUIRE(restoredShape.getHotspot(0, &hx, &hy, &hz));
  CHECK(hx == 1);
  CHECK(hy == 1);
  CHECK(hz == 0);
  REQUIRE(restoredShape.getColor(0, 0, 0) == 1);

  const Problem& rp = restored.getProblem(0);
  REQUIRE(rp.getResultId() == shapeIdx);
  REQUIRE(rp.getShapeMinimum(shapeIdx) == 1);
  REQUIRE(rp.getShapeMaximum(shapeIdx) == 2);
  REQUIRE(rp.getMaxHoles() == 3);
  REQUIRE(rp.placementAllowed(restored, 1, 2));
}

TEST_CASE("puzzle: a round trip through a gzipped file preserves the puzzle",
          "[io][roundtrip][gzip]") {
  TempDir dir;
  const auto name = dir.file("roundtrip.xmpuzzle");
  const GridType gt(GridKind::Bricks);

  Puzzle original(gt);
  original.setComment("through a file");
  original.addShape(fromLayers(gt, {{"##", "##"}}));

  REQUIRE(io::savePuzzle(original, name).has_value());
  const auto restored = io::loadPuzzle(name);
  REQUIRE(restored.has_value());
  REQUIRE(puzzlesRoundtripEqual(original, *restored));
}

TEST_CASE("puzzle: the round trip holds on every grid", "[io][roundtrip]") {
  for (const GridKind k : bt::test::kAllGrids) {
    INFO("grid " << toString(k));
    const GridType gt(k);
    Puzzle original(gt);
    original.addShape(fromLayers(gt, {{"#.#", ".#.", "#.#"}}));
    REQUIRE(puzzlesRoundtripEqual(original, reloaded(original)));
  }
}

TEST_CASE("puzzlesRoundtripEqual detects a different result", "[io][roundtrip]") {
  auto build = [](unsigned int result, bool set) {
    const GridType gt(GridKind::Bricks);
    Puzzle p(gt);
    p.addShape(fromLayers(gt, {{"##"}}));
    p.addShape(fromLayers(gt, {{"#"}}));
    const unsigned int prob = p.addProblem();
    if (set) {
      p.getProblem(prob).setResultId(p, result);
    }
    return p;
  };
  REQUIRE_FALSE(puzzlesRoundtripEqual(build(0, false), build(0, true)));
  REQUIRE_FALSE(puzzlesRoundtripEqual(build(0, true), build(1, true)));
  REQUIRE(puzzlesRoundtripEqual(build(1, true), build(1, true)));
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from test/test_roundtrip.cpp of the original. The gzstream cases
// test the zlib file functions that replace it.

#include "../grid/grid_test_helpers.h"
#include "../model/model_test_helpers.h"
#include "../scratch_dir.h"

#include "shapelogic/io/gzip_file.h"
#include "shapelogic/io/puzzle_file.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace bt;
using bt::test::fromLayers;
using bt::test::puzzlesRoundtripEqual;
using bt::test::ScratchDir;

namespace {

Puzzle reloaded(const Puzzle& p) {
  Result<Puzzle> r = io::parsePuzzle(io::writePuzzle(p));
  REQUIRE(r.has_value());
  return std::move(*r);
}

}  // namespace

TEST_CASE("gzip file: a plain text file is read back unchanged", "[io][gzip]") {
  const ScratchDir dir("roundtrip");
  const auto name = dir.path() / "plain.txt";
  {
    std::ofstream out(name);
    out << "<puzzle/>";
  }
  const auto got = io::readMaybeGzipFile(name);
  REQUIRE(got.has_value());
  REQUIRE(*got == "<puzzle/>");
}

TEST_CASE("gzip file: a gzipped file is decompressed, and really compressed", "[io][gzip]") {
  const ScratchDir dir("roundtrip");
  const auto name = dir.path() / "longer.gz";

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
  const ScratchDir dir("roundtrip");
  const auto missing = io::readMaybeGzipFile(dir.path() / "does-not-exist.xmpuzzle");
  REQUIRE_FALSE(missing.has_value());
  REQUIRE(missing.error().code == Errc::IoError);

  const auto name = dir.path() / "empty.xmpuzzle";
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

TEST_CASE("puzzle: a shape's own color survives a save and reload", "[io][roundtrip]") {
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
  const ScratchDir dir("roundtrip");
  const auto name = dir.path() / "roundtrip.xmpuzzle";
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

// ---- the tilted tray (decision 0045) ---------------------------------------------

namespace {

/// An 8 by 8 tray at 1 down 2 over with an allowance, a custom shift whose
/// digits do not end, and two walls.
tilted::Tray sampleTray() {
  tilted::Tray t;
  t.width = 8;
  t.height = 8;
  t.over = 2;
  t.down = 1;
  t.allowance = 0.05;
  t.placement = tilted::Tray::Placement::Custom;
  t.shiftX = 1.0 / 3.0;
  t.shiftY = -0.6026311234992852;
  t.walls.assign(64, 0);
  t.walls[0] = 1;
  t.walls[63] = 1;
  return t;
}

}  // namespace

TEST_CASE("puzzle: a tilted tray survives a save and reload", "[io][roundtrip][tilted]") {
  Puzzle original;
  original.addShape(3, 3, 1);
  original.setTray(sampleTray());

  const std::string saved = io::writePuzzle(original);
  CHECK(saved.find("<tray size=\"8\" over=\"2\" down=\"1\" allowance=\"0.05\" "
                   "placement=\"custom\"") != std::string::npos);
  // after the grid type, before the colors
  CHECK(saved.find("<gridType") < saved.find("<tray"));
  CHECK(saved.find("<tray") < saved.find("<colors"));

  const Puzzle restored = reloaded(original);
  REQUIRE(restored.tray().has_value());
  // every setting, the shift to the last bit
  CHECK(*restored.tray() == sampleTray());
  // and the file is the same when written again
  CHECK(io::writePuzzle(restored) == saved);

  // the best placement and no walls: no walls attribute
  tilted::Tray plain;
  plain.width = 5;
  plain.height = 5;
  plain.over = 1;
  plain.down = 1;
  original.setTray(plain);
  const std::string plainSaved = io::writePuzzle(original);
  CHECK(plainSaved.find("placement=\"best\"") != std::string::npos);
  CHECK(plainSaved.find("walls=") == std::string::npos);
  // a square tray is written with its size, as before there were rectangles
  CHECK(plainSaved.find("<tray size=\"5\" over=\"1\" down=\"1\" allowance=\"0\" "
                        "placement=\"best\" shiftX=\"0\" shiftY=\"0\"/>") != std::string::npos);
  CHECK(plainSaved.find("width=") == std::string::npos);
  CHECK(plainSaved.find("height=") == std::string::npos);
  CHECK(*reloaded(original).tray() == plain);

  // a puzzle without a tray writes no such element
  original.setTray(std::nullopt);
  CHECK(io::writePuzzle(original).find("<tray") == std::string::npos);
  CHECK_FALSE(reloaded(original).tray().has_value());
}

TEST_CASE("puzzle: a rectangular tray survives a save and reload", "[io][roundtrip][tilted]") {
  // 6 wide and 8 high, a custom shift, walls at (5, 0), (0, 1) and (5, 7)
  tilted::Tray rect = sampleTray();
  rect.width = 6;
  rect.walls.assign(48, 0);
  rect.walls[5] = 1;
  rect.walls[6] = 1;
  rect.walls[47] = 1;
  REQUIRE(rect.valid());
  Puzzle original;
  original.addShape(3, 3, 1);
  original.setTray(rect);

  // width and height in place of size, the walls row by row along the width
  const std::string saved = io::writePuzzle(original);
  CHECK(saved.find("<tray width=\"6\" height=\"8\" over=\"2\" down=\"1\" allowance=\"0.05\" "
                   "placement=\"custom\"") != std::string::npos);
  CHECK(saved.find("size=") == std::string::npos);
  CHECK(saved.find("walls=\"_____##" + std::string(40, '_') + "#\"") != std::string::npos);

  const Puzzle restored = reloaded(original);
  REQUIRE(restored.tray().has_value());
  CHECK(*restored.tray() == rect);
  CHECK(restored.tray()->isWall(5, 0));
  CHECK(restored.tray()->isWall(0, 1));
  CHECK(restored.tray()->isWall(5, 7));
  CHECK_FALSE(restored.tray()->isWall(0, 5));
  CHECK(io::writePuzzle(restored) == saved);

  // the other way round is another tray
  tilted::Tray lying = sampleTray();
  lying.height = 6;
  lying.walls.assign(48, 0);
  lying.walls[7] = 1;
  original.setTray(lying);
  const std::string lyingSaved = io::writePuzzle(original);
  CHECK(lyingSaved.find("<tray width=\"8\" height=\"6\"") != std::string::npos);
  CHECK(*reloaded(original).tray() == lying);
  CHECK(reloaded(original).tray()->isWall(7, 0));
}

TEST_CASE("puzzle: the reader takes a tray's size or its width and height",
          "[io][roundtrip][tilted]") {
  const auto parsed = [](const std::string& sides, const std::string& walls = "") {
    return io::parsePuzzle("<?xml version=\"1.0\"?>\n<puzzle version=\"2\"><gridType type=\"0\"/>"
                           "<tray " + sides + " over=\"3\" down=\"1\" allowance=\"0\" "
                           "placement=\"best\" shiftX=\"0\" shiftY=\"0\"" + walls +
                           "/><colors/><shapes/><problems/><comment/></puzzle>");
  };
  const auto sidesOf = [&](const std::string& sides, const std::string& walls = "") {
    const Result<Puzzle> p = parsed(sides, walls);
    REQUIRE(p.has_value());
    REQUIRE(p->tray().has_value());
    return std::pair<unsigned int, unsigned int>{p->tray()->width, p->tray()->height};
  };
  CHECK(sidesOf("size=\"4\"") == std::pair<unsigned int, unsigned int>{4, 4});
  CHECK(sidesOf("width=\"3\" height=\"5\"") == std::pair<unsigned int, unsigned int>{3, 5});
  // a side given beside the size goes over it
  CHECK(sidesOf("size=\"4\" height=\"7\"") == std::pair<unsigned int, unsigned int>{4, 7});
  CHECK(sidesOf("size=\"4\" width=\"2\"") == std::pair<unsigned int, unsigned int>{2, 4});
  CHECK(sidesOf("size=\"9\" width=\"3\" height=\"5\"") ==
        std::pair<unsigned int, unsigned int>{3, 5});
  // the walls are width * height, row by row along the width
  {
    const Result<Puzzle> p = parsed("width=\"3\" height=\"2\"", " walls=\"__#_#_\"");
    REQUIRE(p.has_value());
    REQUIRE(p->tray().has_value());
    CHECK(p->tray()->isWall(2, 0));
    CHECK(p->tray()->isWall(1, 1));
    CHECK_FALSE(p->tray()->isWall(0, 1));
    // written as it was read
    CHECK(io::writePuzzle(*p).find("<tray width=\"3\" height=\"2\" over=\"3\" down=\"1\" "
                                   "allowance=\"0\" placement=\"best\" shiftX=\"0\" "
                                   "shiftY=\"0\" walls=\"__#_#_\"/>") != std::string::npos);
  }
  // equal sides are a square, and written with its size
  {
    const Result<Puzzle> p = parsed("width=\"5\" height=\"5\"");
    REQUIRE(p.has_value());
    REQUIRE(p->tray().has_value());
    CHECK(io::writePuzzle(*p).find("<tray size=\"5\" over=") != std::string::npos);
  }

  // one side alone, a side out of range or not a number, a bad size beside
  // good sides, walls for the square of a side: no tray, the element kept
  const std::vector<std::pair<std::string, std::string>> bad = {
      {"width=\"4\"", ""},
      {"height=\"4\"", ""},
      {"", ""},
      {"width=\"0\" height=\"4\"", ""},
      {"width=\"4\" height=\"65\"", ""},
      {"width=\"4\" height=\"x\"", ""},
      {"size=\"4\" width=\"-1\"", ""},
      {"size=\"400\" width=\"4\" height=\"4\"", ""},
      {"width=\"3\" height=\"2\"", " walls=\"_________\""},
      {"width=\"3\" height=\"2\"", " walls=\"____\""},
  };
  for (const auto& [sides, walls] : bad) {
    INFO(sides << walls);
    const Result<Puzzle> p = parsed(sides, walls);
    REQUIRE(p.has_value());
    CHECK_FALSE(p->tray().has_value());
    CHECK(io::writePuzzle(*p).find("<tray ") != std::string::npos);
  }
}

TEST_CASE("puzzle: a tray the reader cannot take is kept as the file has it",
          "[io][roundtrip][tilted]") {
  const auto fileWith = [](const std::string& tray, const std::string& grid = "0") {
    return "<?xml version=\"1.0\"?>\n<puzzle version=\"2\"><gridType type=\"" + grid + "\"/>" +
           tray + "<colors/><shapes/><problems/><comment/></puzzle>";
  };
  const std::string good =
      "<tray size=\"4\" over=\"3\" down=\"1\" allowance=\"0\" placement=\"best\" "
      "shiftX=\"0.25\" shiftY=\"0.5\" walls=\"#______________#\"/>";
  {
    const Result<Puzzle> p = io::parsePuzzle(fileWith(good));
    REQUIRE(p.has_value());
    REQUIRE(p->tray().has_value());
    CHECK(p->tray()->width == 4);
    CHECK(p->tray()->height == 4);
    CHECK(p->tray()->over == 3);
    CHECK(p->tray()->shiftX == 0.25);
    CHECK(p->tray()->isWall(0, 0));
    CHECK(p->tray()->isWall(3, 3));
    CHECK_FALSE(p->tray()->isWall(1, 0));
  }

  // out of range, not in lowest terms, a setting missing, walls of the
  // wrong length or with another character: no tray, and the element stays
  const auto trayOf = [](const std::string& size, const std::string& over, const std::string& down,
                         const std::string& allowance, const std::string& placement,
                         const std::string& rest) {
    return "<tray size=\"" + size + "\" over=\"" + over + "\" down=\"" + down + "\" allowance=\"" +
           allowance + "\" placement=\"" + placement + "\" " + rest + "/>";
  };
  const std::string shifts = "shiftX=\"0\" shiftY=\"0\"";
  const std::vector<std::string> bad = {
      trayOf("400", "3", "1", "0", "best", shifts),
      trayOf("4", "4", "2", "0", "best", shifts),
      trayOf("4", "3", "1", "0.9", "best", shifts),
      trayOf("4", "3", "1", "0", "best", "shiftX=\"nan\" shiftY=\"0\""),
      trayOf("4", "3", "1", "0", "best", "shiftX=\"1e300\" shiftY=\"0\""),
      trayOf("4", "3", "1", "0", "best", "shiftY=\"0\""),
      trayOf("4", "3", "1", "0", "nearest", shifts),
      trayOf("4x", "3", "1", "0", "best", shifts),
      trayOf("4", "3", "1", "0", "best", shifts + " walls=\"###\""),
      trayOf("2", "3", "1", "0", "best", shifts + " walls=\"#__x\""),
  };
  for (const std::string& tray : bad) {
    INFO(tray);
    const Result<Puzzle> p = io::parsePuzzle(fileWith(tray));
    REQUIRE(p.has_value());
    CHECK_FALSE(p->tray().has_value());
    CHECK(io::writePuzzle(*p).find("<tray size=") != std::string::npos);
  }

  // a second tray is kept beside the first, which is the puzzle's
  {
    const Result<Puzzle> p = io::parsePuzzle(fileWith(good + bad[0]));
    REQUIRE(p.has_value());
    REQUIRE(p->tray().has_value());
    CHECK(p->tray()->width == 4);
    CHECK(p->tray()->height == 4);
    const std::string again = io::writePuzzle(*p);
    CHECK(again.find("size=\"4\"") != std::string::npos);
    CHECK(again.find("size=\"400\"") != std::string::npos);
  }

  // only a cube-grid puzzle has a tray
  {
    const Result<Puzzle> p = io::parsePuzzle(fileWith(good, "1"));
    REQUIRE(p.has_value());
    CHECK_FALSE(p->tray().has_value());
  }
}

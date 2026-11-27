// SPDX-License-Identifier: GPL-3.0-or-later
// The .xmpuzzle reader and writer: what the original writes, what is kept
// although it is not modeled (decisions 0006 and 0008), and what is refused.

#include "../model/model_test_helpers.h"
#include "../scratch_dir.h"

#include "shapelogic/io/gzip_file.h"
#include "shapelogic/io/paused_solve.h"
#include "shapelogic/io/puzzle_file.h"
#include "shapelogic/io/xml_document.h"
#include "shapelogic/base/assert.h"
#include "shapelogic/model/sliding.h"
#include "shapelogic/solve/disasm_to_moves.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

using namespace bt;
using Catch::Matchers::ContainsSubstring;
using bt::test::ScratchDir;

namespace {

std::string readText(const std::filesystem::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::ostringstream s;
  s << in.rdbuf();
  return s.str();
}

Puzzle parse(const std::string& xml) {
  Result<Puzzle> p = io::parsePuzzle(xml);
  if (!p) {
    FAIL("parse failed: " << p.error().message);
  }
  return std::move(*p);
}

std::string parseError(const std::string& xml) {
  Result<Puzzle> p = io::parsePuzzle(xml);
  REQUIRE_FALSE(p.has_value());
  REQUIRE(p.error().code == Errc::ParseError);
  return p.error().message;
}

/// A small valid document around one problem's inner XML.
std::string withProblem(const std::string& problemAttrs, const std::string& inner) {
  return "<puzzle version=\"2\"><gridType type=\"0\"/><shapes>"
         "<voxel x=\"2\" y=\"1\" z=\"1\" type=\"0\">##</voxel>"
         "<voxel x=\"1\" y=\"1\" z=\"1\" type=\"0\">#</voxel></shapes>"
         "<problems><problem" +
         problemAttrs + "><shapes><shape id=\"1\" count=\"2\"/></shapes><result id=\"0\"/>" +
         inner + "</problem></problems></puzzle>";
}

}  // namespace

TEST_CASE("puzzle file: every fixture is saved byte for byte as the original saves it",
          "[io][file]") {
  int checked = 0;
  for (const auto& entry : std::filesystem::directory_iterator("tests/data/puzzles")) {
    const std::filesystem::path& path = entry.path();
    if (path.extension() != ".xmpuzzle") {
      continue;  // the sliding/ folder: the generated sliding puzzles
    }
    INFO(path.filename().string());
    const Puzzle p = test::loadFixture(path.filename().string());
    const std::string ours = io::writePuzzle(p);
    const std::string theirs =
        readText(std::filesystem::path("tests/data/saved_by_original") /
                 (path.stem().string() + ".xml"));
    REQUIRE_FALSE(theirs.empty());
    // a sliding puzzle's start marks were its cell colors: the original's
    // file is converted as it loads, so its save is the new form, not
    // the original's bytes
    if (p.getGridType().getType() != GridKind::Sliding) {
      CHECK(test::withoutUids(ours) == theirs);
    }

    // and the save reads back to itself
    CHECK(io::writePuzzle(parse(ours)) == ours);
    checked++;
  }
  REQUIRE(checked >= 9);
}

// The grids the original does not have (decision 0023) cannot be compared
// with it: their fixtures (tests/data/puzzles/new-grids, written by this
// engine) are saved as they were written, and read back to themselves.
TEST_CASE("puzzle file: the new grids' fixtures are saved byte for byte as they were written",
          "[io][file]") {
  std::set<GridKind> grids;
  for (const auto& entry : std::filesystem::directory_iterator("tests/data/puzzles/new-grids")) {
    const std::filesystem::path& path = entry.path();
    if (path.extension() != ".xmpuzzle") {
      continue;
    }
    INFO(path.filename().string());
    const Result<std::string> written = io::readMaybeGzipFile(path);
    REQUIRE(written.has_value());
    const Puzzle p = test::loadFixture("new-grids/" + path.filename().string());
    grids.insert(p.getGridType().getType());
    CHECK(p.getNumberOfProblems() == 2);
    const std::string ours = io::writePuzzle(p);
    CHECK(test::withoutUids(ours) == *written);
    CHECK(io::writePuzzle(parse(ours)) == ours);
  }
  CHECK(grids == std::set<GridKind>{GridKind::Hexagonal, GridKind::Cairo});
}

// The Tilted Square puzzles (decision 0045; tests/data/puzzles/tilted, made
// by its make_fixtures.py) are Brick files with a <tray> the original does
// not know: saved as they were written, and read back to themselves.
TEST_CASE("puzzle file: the tilted fixtures are saved byte for byte as they were written",
          "[io][file][tilted]") {
  unsigned int checked = 0;
  for (const auto& entry : std::filesystem::directory_iterator("tests/data/puzzles/tilted")) {
    const std::filesystem::path& path = entry.path();
    if (path.extension() != ".xmpuzzle") {
      continue;
    }
    INFO(path.filename().string());
    const Result<std::string> written = io::readMaybeGzipFile(path);
    REQUIRE(written.has_value());
    const Puzzle p = test::loadFixture("tilted/" + path.filename().string());
    CHECK(p.getGridType().getType() == GridKind::Bricks);
    CHECK(p.tray().has_value());
    const std::string ours = io::writePuzzle(p);
    CHECK(test::withoutUids(ours) == test::withoutUids(*written));
    CHECK(io::writePuzzle(parse(ours)) == ours);
    checked++;
  }
  CHECK(checked == 3);
}

TEST_CASE("puzzle file: content the model does not know is kept in place", "[io][file]") {
  const std::string in =
      "<puzzle version=\"2\" future=\"1\">"
      "<early x=\"1\"/>"
      "<gridType type=\"0\"/><colors/>"
      "<shapes><voxel x=\"1\" y=\"1\" z=\"1\" type=\"0\" bevel=\"0.5\">#</voxel></shapes>"
      "<later><deep a=\"&lt;\">text</deep></later>"
      "<problems><problem state=\"0\" mood=\"good\">"
      "<shapes><shape id=\"0\" count=\"1\"/></shapes>"
      "<result id=\"0\"/>"
      "<newThing/>"
      "<bitmap/>"
      "</problem></problems>"
      "<comment>c</comment>"
      "<last/>"
      "</puzzle>";
  const Puzzle p = parse(in);

  REQUIRE(p.preserved().attributes.size() == 1);
  REQUIRE(p.preserved().children.size() == 3);
  REQUIRE(p.getShape(0).getExtraAttributes().size() == 1);
  REQUIRE(p.getProblem(0).preserved().attributes.size() == 1);
  REQUIRE(p.getProblem(0).preserved().children.size() == 1);

  const std::string out = io::writePuzzle(p);
  CHECK_THAT(out, ContainsSubstring("<puzzle version=\"2\" future=\"1\">\n <early x=\"1\"/>\n"
                                    " <gridType type=\"0\"/>"));
  CHECK_THAT(out, ContainsSubstring("type=\"0\" bevel=\"0.5\">#</voxel>"));
  CHECK_THAT(out, ContainsSubstring(" </shapes>\n <later>\n  <deep a=\"&lt;\">text</deep>\n"
                                    " </later>\n <problems>"));
  CHECK_THAT(out, ContainsSubstring("\" state=\"0\" mood=\"good\">"));
  CHECK_THAT(out, ContainsSubstring("<result id=\"0\"/>\n   <newThing/>\n   <bitmap/>"));
  CHECK_THAT(out, ContainsSubstring("<comment>c</comment>\n <last/>\n</puzzle>"));

  // a second round changes nothing
  CHECK(io::writePuzzle(parse(out)) == out);
}

// The trap the writer's check is for (cleanup item 19.3): an attribute the
// writer emits and its reader's list of known ones lacks is kept by the
// reader as content it does not know, and the next save would write it
// beside the writer's own, a file the parser refuses. Each of the four
// lists holds what its writer emits today (the fixtures' saves above, byte
// for byte); a kept attribute of a known name stands in for one that does
// not. The save ends with the internal error, and nothing is written.
TEST_CASE("puzzle file: an attribute kept beside the writer's own is never written twice",
          "[io][file]") {
  Puzzle p = parse(withProblem(" state=\"0\"", ""));
  p.getShape(0).setWeight(3);
  const std::string out = io::writePuzzle(p);
  CHECK_THAT(out, ContainsSubstring(" weight=\"3\""));
  p.getShape(0).setExtraAttributes({{"bevel", "0.5"}});
  CHECK_THAT(io::writePuzzle(p), ContainsSubstring(" bevel=\"0.5\""));
  p.getShape(0).setExtraAttributes({{"weight", "3"}});
  CHECK_THROWS_AS(io::writePuzzle(p), InternalError);
  // the parser's side of it: such a file is not read
  std::string twice = out;
  twice.replace(twice.find(" weight=\"3\""), 0, " weight=\"3\"");
  CHECK_FALSE(io::parsePuzzle(twice).has_value());
}

TEST_CASE("puzzle file: unknown solution content is kept", "[io][file]") {
  const Puzzle p = parse(withProblem(
      " state=\"2\" assemblies=\"1\" solutions=\"1\"",
      "<solutions><solution asmNum=\"3\" note=\"n\"><assembly>0 0 0 0 1 0 0 0</assembly>"
      "<marker/></solution></solutions>"));
  const Solution& s = p.getProblem(0).getSavedSolution(0);
  REQUIRE(s.getAssemblyNumber() == 3);
  const std::string out = io::writePuzzle(p);
  CHECK_THAT(out, ContainsSubstring("<solution asmNum=\"3\" note=\"n\">\n"));
  CHECK_THAT(out, ContainsSubstring("</assembly>\n     <marker/>\n    </solution>"));
}

TEST_CASE("puzzle file: stacking rod sets and stacks are read, or the default rod set is made",
          "[io][file]") {
  const Puzzle hanoi = test::loadFixture("test_stacking_hanoi.xmpuzzle");
  REQUIRE(hanoi.preserved().findChild("rodSets") == nullptr);
  REQUIRE(hanoi.getProblem(0).preserved().findChild("stacking") == nullptr);
  REQUIRE(hanoi.rodSetCount() == 1);
  CHECK(hanoi.getRodSet(0).name == "Hanoi");
  CHECK(hanoi.getProblem(0).getRodSetId() == 0);
  REQUIRE(hanoi.getProblem(0).startStacks().rods.size() == 3);
  CHECK(hanoi.getProblem(0).startStacks().rods[0].size() == 3);
  CHECK(hanoi.getProblem(0).goalStacks().rods[2].size() == 3);
  CHECK_THAT(io::writePuzzle(hanoi),
             ContainsSubstring("   <stacking rodSet=\"0\">\n    <start>\n     <rod "
                               "pieces=\"0:0,1:0,2:0\"/>\n     <rod/>\n     <rod/>\n    "
                               "</start>"));

  // a stacking file without rod sets gets the default one when it is read
  const Result<Puzzle> bare =
      io::parsePuzzle("<puzzle version=\"2\"><gridType type=\"6\"/></puzzle>");
  REQUIRE(bare);
  CHECK(bare->rodSetCount() == 1);

  const Puzzle fresh{GridType(GridKind::Stacking)};
  CHECK_THAT(io::writePuzzle(fresh),
             ContainsSubstring(" <shapes/>\n <rodSets>\n  <rodSet rods=\"3\" grow=\"1\" "
                               "height=\"8\" sizeMatters=\"1\" distanceMatters=\"0\" "
                               "moveOver=\"0\"/>\n </rodSets>\n <problems/>"));
  CHECK_THAT(io::writePuzzle(Puzzle()), !ContainsSubstring("rodSets"));
}

TEST_CASE("puzzle file: a paused solve keeps its position and pending assemblies",
          "[io][file]") {
  const Puzzle p = parse(withProblem(
      " state=\"1\" assemblies=\"5\" solutions=\"2\" time=\"3\" timeMs=\"3456\" searched=\"99\""
      " searchDepth=\"7\" triedApart=\"4\" slideGrid=\"6\" slideLevelMs=\"0 12 340\"",
      "<assembler version=\"1.2\">abc def</assembler>"
      "<pending><item counted=\"1\" assembly=\"4\" solution=\"2\">"
      "<assembly>1 0 0 0 0 0 0 0</assembly></item>"
      "<item counted=\"0\"/></pending>"));
  const Problem& pr = p.getProblem(0);
  REQUIRE(pr.getSolveState() == ProblemState::Solving);
  CHECK(pr.getNumAssemblies() == 5);
  CHECK(pr.getNumSolutions() == 2);
  CHECK(pr.getUsedMs() == 3456);
  CHECK(pr.getSearched() == 99);
  CHECK(pr.getSearchDepth() == 7);
  CHECK(pr.getTriedApart() == 4);
  CHECK(pr.getSlideGrid() == 6);
  CHECK(pr.getSlideLevelMs() == std::vector<std::uint64_t>{0, 12, 340});
  CHECK(pr.getAssemblerVersion() == "1.2");
  CHECK(pr.getAssemblerState() == "abc def");
  REQUIRE(pr.getPending().size() == 1);  // the item without an assembly is dropped
  CHECK(pr.getPending()[0].counted);
  CHECK(pr.getPending()[0].assemblyNumber == 4);
  CHECK(pr.getPending()[0].assembly.getX(0) == 1);

  const std::string out = io::writePuzzle(p);
  CHECK_THAT(out, ContainsSubstring("<assembler version=\"1.2\">abc def</assembler>"));
  CHECK_THAT(out, ContainsSubstring("<item counted=\"1\" assembly=\"4\" solution=\"2\">"));
  CHECK_THAT(out, ContainsSubstring(" searched=\"99\" searchDepth=\"7\" triedApart=\"4\""
                                    " slideGrid=\"6\" slideLevelMs=\"0 12 340\""));
  CHECK(io::writePuzzle(parse(out)) == out);

  // a pending assembly's numbers are 64 bits, as the counts are (read
  // with strtoul they stopped at 2^32 - 1 on Windows)
  std::string big = out;
  const std::string item = "<item counted=\"1\" assembly=\"4\" solution=\"2\">";
  big.replace(big.find(item), item.size(),
              "<item counted=\"1\" assembly=\"4294967301\" solution=\"4294967302\">");
  const Puzzle bigPuzzle = parse(big);
  REQUIRE(bigPuzzle.getProblem(0).getPending().size() == 1);
  CHECK(bigPuzzle.getProblem(0).getPending()[0].assemblyNumber == (std::uint64_t{1} << 32U) + 5);
  CHECK(bigPuzzle.getProblem(0).getPending()[0].solutionNumber == (std::uint64_t{1} << 32U) + 6);
  CHECK(io::writePuzzle(bigPuzzle) == big);

  // once solved, neither is written
  Puzzle solved = p;
  solved.getProblem(0).finishedSolving();
  CHECK_THAT(io::writePuzzle(solved), !ContainsSubstring("assembler"));
  CHECK_THAT(io::writePuzzle(solved), !ContainsSubstring("pending"));
  // the grid goes with the results
  solved.getProblem(0).removeAllSolutions();
  CHECK_THAT(io::writePuzzle(solved), !ContainsSubstring("slideGrid"));
  CHECK_THAT(io::writePuzzle(solved), !ContainsSubstring("slideLevelMs"));
}

TEST_CASE("puzzle file: rotation solutions win over classic ones", "[io][file]") {
  const std::string classic =
      "<solutions><solution asmNum=\"1\"><assembly>0 0 0 0 1 0 0 0</assembly></solution>"
      "<solution asmNum=\"2\"><assembly>1 0 0 0 0 0 0 0</assembly></solution></solutions>";
  const std::string rot =
      "<solutionsWithRotations><solution asmNum=\"7\"><assembly>0 0 0 0 1 0 0 0</assembly>"
      "</solution></solutionsWithRotations>";
  for (const std::string& inner : {classic + rot, rot + classic}) {
    const Puzzle p = parse(withProblem(" state=\"2\"", inner));
    const Problem& pr = p.getProblem(0);
    REQUIRE(pr.getSolutionsWithRotations());
    REQUIRE(pr.getNumberOfSavedSolutions() == 1);
    CHECK(pr.getSavedSolution(0).getAssemblyNumber() == 7);
  }
}

TEST_CASE("puzzle file: numbers are read as the original reads them", "[io][file]") {
  // an unset result written as 4294967295, maxHoles likewise, and a state
  // the original does not accept
  const Puzzle p = parse(
      "<puzzle version=\"2\"><shapes><voxel x=\"1\" y=\"1\" z=\"1\" type=\"0\">#</voxel>"
      "</shapes><problems><problem state=\"7\" assemblies=\"3\" maxHoles=\"4294967295\">"
      "<result id=\"4294967295\"/></problem>"
      "<problem state=\"2\" assemblies=\"12abc\" time=\"2\"><result id=\"5\"/></problem>"
      "</problems></puzzle>");
  CHECK(p.getGridType().getType() == GridKind::Bricks);  // no gridType: cubes
  CHECK_FALSE(p.getProblem(0).resultValid());
  CHECK_FALSE(p.getProblem(0).maxHolesDefined());
  CHECK(p.getProblem(0).getSolveState() == ProblemState::Unsolved);
  CHECK(p.getProblem(1).getNumAssemblies() == 12);
  CHECK(p.getProblem(1).getUsedMs() == 2000);
  CHECK_FALSE(p.getProblem(1).resultValid());  // shape 5 does not exist
}

TEST_CASE("puzzle file: solution numbers past 2^32 are read and written whole", "[io][file]") {
  // a solve may find more than 2^32 assemblies: the numbers are 64 bits,
  // as the counts are, and written in decimal as before
  const Puzzle p = test::loadFixture("PelikanBurr.xmpuzzle");
  std::string xml = io::writePuzzle(p);
  // the first solution's tag, with both numbers (it has a take-apart)
  const std::size_t at = xml.find("<solution ");
  REQUIRE(at != std::string::npos);
  xml.replace(at, xml.find('>', at) - at, R"(<solution asmNum="4294967301" solNum="4294967302")");
  const Puzzle big = parse(xml);
  REQUIRE(big.getProblem(0).getSavedSolution(0).getDisassemblyInfo() != nullptr);
  const Solution& s = big.getProblem(0).getSavedSolution(0);
  CHECK(s.getAssemblyNumber() == (std::uint64_t{1} << 32U) + 5);
  CHECK(s.getSolutionNumber() == (std::uint64_t{1} << 32U) + 6);
  CHECK(io::writePuzzle(big) == xml);
}

TEST_CASE("puzzle file: voxel content, colors and goal marks", "[io][file]") {
  const Puzzle p = parse(
      "<puzzle version=\"1\"><gridType type=\"5\"/><colors><color red=\"1\" green=\"2\" "
      "blue=\"300\"/></colors><shapes>"
      "<voxel x=\"4\" y=\"1\" z=\"1\" hx=\"1\" weight=\"3\" name=\"n\" diskSize=\"2\" "
      "color=\"#00FF10\" goal=\"0,2,,1\" type=\"0\">#1+63_5#</voxel></shapes></puzzle>");
  REQUIRE(p.colorNumber() == 1);
  CHECK(p.getColor(0) == Rgb{1, 2, 300 & 0xFF});
  const Voxel& v = p.getShape(0);
  CHECK(v.getColor(0) == 1);
  CHECK(v.isVariable(1));
  CHECK(v.getColor(1) == 63);
  CHECK(v.isEmpty(2));
  CHECK(v.getColor(2) == 0);  // an empty cell has no color
  CHECK(v.getHx() == 1);
  CHECK(v.getWeight() == 3);
  CHECK(v.getDiskSize() == 2);
  CHECK(v.getShapeColor() == 0x00FF10);
  CHECK(v.getGoalPiece(1) == 2);
  CHECK(v.getGoalPiece(2) == 0);  // goal marks of empty cells go
  CHECK(v.getGoalPiece(3) == 1);
  // a shape's goal marks are read only to be given to the problems on a
  // start/goal shape (decision 0027): they are not written
  CHECK_THAT(io::writePuzzle(p),
             ContainsSubstring("<voxel x=\"4\" y=\"1\" z=\"1\" hx=\"1\" weight=\"3\" name=\"n\" "
                               "diskSize=\"2\" color=\"#00FF10\" "
                               "type=\"0\">#1+63_#</voxel>"));
}

TEST_CASE("puzzle file: what the original refuses is refused", "[io][file]") {
  const std::string head = "<puzzle version=\"2\"><gridType type=\"0\"/><shapes>";
  const std::string tail = "</shapes></puzzle>";
  auto voxel = [&](const std::string& v) { return head + v + tail; };

  CHECK_THAT(parseError("<puzzle/>"), ContainsSubstring("'version'"));
  CHECK_THAT(parseError("<puzzle version=\"3\"/>"), ContainsSubstring("version 1 and 2"));
  CHECK_THAT(parseError("<problem version=\"2\"/>"), ContainsSubstring("puzzle"));
  // 7 and 8 are this program's hexagonal and cairo grids (decision 0023)
  CHECK_THAT(parseError("<puzzle version=\"2\"><gridType type=\"9\"/></puzzle>"),
             ContainsSubstring("unknown grid type"));
  CHECK_THAT(parseError("<puzzle version=\"2\"><gridType/></puzzle>"),
             ContainsSubstring("'type'"));
  CHECK_THAT(parseError("<puzzle version=\"2\"><shapes/><gridType type=\"1\"/></puzzle>"),
             ContainsSubstring("only one gridtype"));

  CHECK_THAT(parseError(voxel("<voxel y=\"1\" z=\"1\" type=\"0\">#</voxel>")),
             ContainsSubstring("'x'"));
  CHECK_THAT(parseError(voxel("<voxel x=\"70000\" y=\"1\" z=\"1\" type=\"0\">#</voxel>")),
             ContainsSubstring("dimension too large"));
  CHECK_THAT(parseError(voxel("<voxel x=\"-1\" y=\"1\" z=\"1\" type=\"0\">#</voxel>")),
             ContainsSubstring("dimension too large"));
  CHECK_THAT(parseError(voxel("<voxel x=\"2048\" y=\"2048\" z=\"2048\" type=\"0\">#</voxel>")),
             ContainsSubstring("too large"));
  CHECK_THAT(parseError(voxel("<voxel x=\"1\" y=\"1\" z=\"1\" type=\"1\">#</voxel>")),
             ContainsSubstring("unknown voxel type"));
  CHECK_THAT(parseError(voxel("<voxel x=\"1\" y=\"1\" z=\"1\" type=\"0\"/>")),
             ContainsSubstring("requires content"));
  CHECK_THAT(parseError(voxel("<voxel x=\"2\" y=\"1\" z=\"1\" type=\"0\">#</voxel>")),
             ContainsSubstring("not enough voxels"));
  CHECK_THAT(parseError(voxel("<voxel x=\"1\" y=\"1\" z=\"1\" type=\"0\">##</voxel>")),
             ContainsSubstring("too many voxels"));
  CHECK_THAT(parseError(voxel("<voxel x=\"1\" y=\"1\" z=\"1\" type=\"0\">#64</voxel>")),
             ContainsSubstring("color too big"));
  CHECK_THAT(parseError(voxel("<voxel x=\"1\" y=\"1\" z=\"1\" type=\"0\">#a</voxel>")),
             ContainsSubstring("unrecognized character"));

  CHECK_THAT(parseError(withProblem("", "<solutions><solution><assembly>0 0 0</assembly>"
                                        "</solution></solutions>")),
             ContainsSubstring("not the right number of numbers"));
  CHECK_THAT(parseError(withProblem("", "<solutions><solution><assembly>0 0 0 0</assembly>"
                                        "</solution></solutions>")),
             ContainsSubstring("not the right number of placements"));
  CHECK_THAT(parseError(withProblem("", "<solutions><solution><assembly>0 0 0 99 x</assembly>"
                                        "</solution></solutions>")),
             ContainsSubstring("NUM_TRANSFORMATIONS"));
  CHECK_THAT(parseError(withProblem("", "<solutions><solution/></solutions>")),
             ContainsSubstring("no assembly"));
  CHECK_THAT(parseError(withProblem("", "<solutions><solution><separationInfo>3 2</separationInfo>"
                                        "<assembly>0 0 0 0 x</assembly></solution></solutions>")),
             ContainsSubstring("incomplete"));
  CHECK_THAT(parseError("<puzzle version=\"2\"><problems><problem><shapes><shape id=\"0\"/>"
                        "</shapes></problem></problems></puzzle>"),
             ContainsSubstring("valid shapes"));

  // the line of the offending element is named
  CHECK_THAT(parseError("<puzzle version=\"2\">\n<gridType\ntype=\"9\"/></puzzle>"),
             ContainsSubstring("(line 3)"));
}

// A file is untrusted (cleanup item 16.2): puzzles are passed between
// people. What a file's numbers ask for is bounded before it is used as a
// size, an index or a term of a sum, and each is refused with a message.
// Before, a number read into an int overflowed it (undefined), a
// placement or a hotspot could be any int (and was subtracted from
// others), a rotation's pivot of 1e300 was converted to int (undefined), a
// disassembly could name pieces its assembly does not have (the playback
// writes an array at four times the name) and turn them to orientations
// the grid does not have.
TEST_CASE("puzzle file: numbers no puzzle has are refused", "[io][file]") {
  const auto solution = [](const std::string& assembly, const std::string& rest) {
    return withProblem(" state=\"2\"", "<solutions><solution><assembly>" + assembly +
                                           "</assembly>" + rest + "</solution></solutions>");
  };
  const auto separation = [&](const std::string& pieces, const std::string& dx,
                              const std::string& more = "") {
    return solution("0 0 0 0 1 0 0 0", "<separation><pieces count=\"2\">" + pieces +
                                           "</pieces><state><dx>" + dx +
                                           "</dx><dy>0 0</dy><dz>0 0</dz>" + more +
                                           "</state></separation>");
  };
  // what is fine, to the last number that is
  REQUIRE(parse(separation("0 1", "0 20000")).getProblem(0).getNumberOfSavedSolutions() == 1);
  CHECK(parse(separation("1 0", "-1048576 1048576"))
            .getProblem(0)
            .getSavedSolution(0)
            .getDisassembly()
            ->getState(0)
            .getX(0) == -1048576);
  CHECK(parse(solution("1048576 -1048576 0 23 0 0 0 0", ""))
            .getProblem(0)
            .getSavedSolution(0)
            .getAssembly()
            .getY(0) == -1048576);

  // a state's offsets, the piece names and the orientations
  for (const char* big : {"1048577", "2147483647", "2147483648", "4294967296",
                          "99999999999999999999", "-1048577", "-99999999999999999999"}) {
    INFO(big);
    CHECK_THAT(parseError(separation("0 1", std::string("0 ") + big)),
               ContainsSubstring("number out of range in disassembly"));
    CHECK_THAT(parseError(separation("0 1", std::string(big) + " 0")),
               ContainsSubstring("number out of range in disassembly"));
  }
  CHECK_THAT(parseError(separation("0 2", "0 0")),
             ContainsSubstring("names a piece the assembly does not have"));
  CHECK_THAT(parseError(separation("1048576 0", "0 0")),
             ContainsSubstring("names a piece the assembly does not have"));
  CHECK_THAT(parseError(separation("0 99999999999", "0 0")),
             ContainsSubstring("number out of range in disassembly"));
  REQUIRE(parse(separation("0 1", "0 0", "<dt>0 47</dt>")).getNumberOfProblems() == 1);
  CHECK_THAT(parseError(separation("0 1", "0 0", "<dt>0 48</dt>")),
             ContainsSubstring("orientation out of range in disassembly"));
  CHECK_THAT(parseError(separation("0 1", "0 0", "<dt>1048576 0</dt>")),
             ContainsSubstring("orientation out of range in disassembly"));

  // a rotation's pivot
  REQUIRE(parse(separation("0 1", "0 0",
                           "<rotation piece=\"0\" px=\"1.5\" py=\"-2\" pz=\"0\" axis=\"1\" "
                           "sense=\"0\"/>"))
              .getProblem(0)
              .getSavedSolution(0)
              .getDisassembly()
              ->getState(0)
              .getRotPivotX() == 3);
  for (const char* pivot : {"1e300", "-1e300", "600000", "nan", "inf", "-inf"}) {
    INFO(pivot);
    CHECK_THAT(parseError(separation("0 1", "0 0",
                                     std::string("<rotation piece=\"0\" px=\"0\" py=\"") + pivot +
                                         "\" pz=\"0\" axis=\"1\" sense=\"0\"/>")),
               ContainsSubstring("rotation pivot out of range"));
  }

  // an assembly's placements
  for (const char* big : {"1048577", "2147483648", "99999999999999999999", "-1048577",
                          "-99999999999999999999"}) {
    INFO(big);
    for (int at = 0; at < 4; at++) {
      std::string assembly;
      for (int i = 0; i < 4; i++) {
        assembly += (i == at ? std::string(big) : std::string("0")) + " ";
      }
      CHECK_THAT(parseError(solution(assembly + "0 0 0 0", "")),
                 ContainsSubstring("assembly placement out of range"));
    }
  }

  // a shape's hotspot
  const auto hot = [](const std::string& attrs) {
    return "<puzzle version=\"2\"><gridType type=\"0\"/><shapes><voxel x=\"1\" y=\"1\" z=\"1\" " +
           attrs + " type=\"0\">#</voxel></shapes></puzzle>";
  };
  CHECK(parse(hot("hx=\"-1048576\" hy=\"1048576\"")).getShape(0).getHy() == 1048576);
  CHECK_THAT(parseError(hot("hx=\"1048577\"")), ContainsSubstring("hotspot out of range"));
  CHECK_THAT(parseError(hot("hy=\"-1048577\"")), ContainsSubstring("hotspot out of range"));
  CHECK_THAT(parseError(hot("hz=\"2147483647\"")), ContainsSubstring("hotspot out of range"));
  // a disc's size
  CHECK(parse(hot("diskSize=\"32768\"")).getShape(0).getDiskSize() == 32768);
  CHECK_THAT(parseError(hot("diskSize=\"32769\"")), ContainsSubstring("disc size out of range"));
  CHECK_THAT(parseError(hot("diskSize=\"-1\"")), ContainsSubstring("disc size out of range"));
  // on the sphere grid a hotspot is one of its places (the sum of its
  // coordinates even), which the grid's transformations assert
  const auto sphere = [&](const std::string& attrs) {
    std::string xml = hot(attrs);
    xml.replace(xml.find("type=\"0\"/>"), 9, "type=\"2\"/>");
    return xml;
  };
  CHECK(parse(sphere("hx=\"1\" hy=\"1\"")).getShape(0).getHx() == 1);
  CHECK(parse(sphere("hx=\"-1\" hy=\"2\" hz=\"1\"")).getShape(0).getHz() == 1);
  CHECK_THAT(parseError(sphere("hx=\"1\"")),
             ContainsSubstring("hotspot is not a place of the sphere grid"));
  CHECK_THAT(parseError(sphere("hx=\"1\" hy=\"1\" hz=\"-1\"")),
             ContainsSubstring("hotspot is not a place of the sphere grid"));

  // Parts after the solutions: each assembly was counted against the parts
  // before it, so these would have a piece no assembly places.
  CHECK_THAT(parseError(withProblem(
                 " state=\"2\"",
                 "<solutions><solution><assembly>0 0 0 0 1 0 0 0</assembly></solution></solutions>"
                 "<shapes><shape id=\"1\" count=\"1\"/></shapes>")),
             ContainsSubstring("not the right number of placements"));
  CHECK_THAT(parseError(withProblem(
                 " state=\"1\"",
                 "<pending><item counted=\"1\"><assembly>0 0 0 0 1 0 0 0</assembly></item></pending>"
                 "<shapes><shape id=\"1\" count=\"1\"/></shapes>")),
             ContainsSubstring("not the right number of placements"));
}

// Sizes a file asks for are checked against what it holds before room is
// made for them (cleanup item 16.2). A voxel of 1024 cells a side passed
// the size limit and had its gigabyte filled before its one character of
// text was looked at; a paused sliding problem's searchDepth made a list
// of that many times: 16 GiB for 2^31, an exception that left the reader
// for 2^61.
TEST_CASE("puzzle file: sizes are checked against what the file holds", "[io][file]") {
  // (the message is the one a short text always had; without the check
  // this case passes too, a gigabyte and seconds later)
  CHECK_THAT(parseError("<puzzle version=\"2\"><gridType type=\"0\"/><shapes>"
                        "<voxel x=\"1024\" y=\"1024\" z=\"1024\" type=\"0\">#</voxel>"
                        "</shapes></puzzle>"),
             ContainsSubstring("not enough voxels"));

  const auto paused = [](const std::string& depth) {
    return "<puzzle version=\"2\"><gridType type=\"5\"/><shapes>"
           "<voxel x=\"2\" y=\"1\" z=\"1\" type=\"0\">##</voxel>"
           "<voxel x=\"1\" y=\"1\" z=\"1\" type=\"0\">#</voxel></shapes>"
           "<problems><problem state=\"1\" searchDepth=\"" +
           depth +
           "\"><shapes><shape id=\"1\" count=\"1\"/></shapes><result id=\"0\"/></problem>"
           "</problems></puzzle>";
  };
  // a search paused by a version that kept no times: its moves, untimed
  const Puzzle few = parse(paused("12"));
  CHECK(few.getProblem(0).getSearchDepth() == 12);
  CHECK(few.getProblem(0).getSlideLevelMs() == std::vector<std::uint64_t>(12, kUntimedMs));
  CHECK(parse(paused("1048576")).getProblem(0).getSlideLevelMs().size() == 1048576);
  // past any depth a search reaches: the number is kept, no list is made
  // (2^31, the 16 GiB of the finding, is left out here: without the fix
  // this test would fill them)
  for (const char* depth : {"1048577", "18446744073709551615"}) {
    INFO(depth);
    const Puzzle deep = parse(paused(depth));
    CHECK(deep.getProblem(0).getSearchDepth() == std::strtoull(depth, nullptr, 10));
    CHECK(deep.getProblem(0).getSlideLevelMs().empty());
  }
}

namespace {

/// A puzzle of `pieces` unit cubes in a row, solved, with a disassembly
/// that takes one cube off at a time: its tree nests `pieces - 1` times.
std::string nestedDisassembly(unsigned int pieces) {
  const std::string n = std::to_string(pieces);
  std::string x = "<puzzle version=\"2\"><gridType type=\"0\"/><colors/><shapes>"
                  "<voxel x=\"1\" y=\"1\" z=\"1\" type=\"0\">#</voxel>"
                  "<voxel x=\"1\" y=\"1\" z=\"" + n + "\" type=\"0\">" + std::string(pieces, '#') +
                  "</voxel></shapes><problems><problem state=\"2\" assemblies=\"1\" "
                  "solutions=\"1\"><shapes><shape id=\"0\" count=\"" + n +
                  "\"/></shapes><result id=\"1\"/><solutions><solution><assembly>";
  for (unsigned int i = 0; i < pieces; i++) {
    x += (i != 0 ? " 0 0 " : "0 0 ") + std::to_string(i) + " 0";
  }
  x += "</assembly>";
  std::string close;
  for (unsigned int k = pieces; k >= 2; k--) {
    x += k == pieces ? "<separation>" : "<separation type=\"left\">";
    x += "<pieces count=\"" + std::to_string(k) + "\">";
    std::string zeros;
    for (unsigned int i = 0; i < k; i++) {
      x += (i != 0 ? " " : "") + std::to_string(pieces - k + i);
      zeros += i != 0 ? " 0" : "0";
    }
    x += "</pieces><state><dx>20000" + zeros.substr(1) + "</dx><dy>" + zeros + "</dy><dz>" +
         zeros + "</dz></state>";
    close += "</separation>";
  }
  return x + close + "</solution></solutions></problem></problems><comment/></puzzle>";
}

}  // namespace

// A disassembly's tree nests once per piece taken off, and what walks one
// recurses: this reader, the level text, the playback, the writer, the
// copy. The XML reader let 5000 levels through, which is megabytes of
// stack in each (cleanup item 16.2: 0.8 KiB a level in this reader, in a
// release build), more than some threads have. A tree is refused past
// 1000; up to there it reads, and is walked.
TEST_CASE("puzzle file: a disassembly nested too deeply is refused", "[io][file]") {
  {
    const Puzzle p = parse(nestedDisassembly(1001));
    const Solution& s = p.getProblem(0).getSavedSolution(0);
    REQUIRE(s.getDisassembly() != nullptr);
    CHECK(s.getDisassembly()->getNumSequences() == 1000);
    CHECK(s.getDisassembly()->sumMoves() == 0);
    CHECK(s.getDisassembly()->movesText().starts_with("0.0.0.0."));
    const Puzzle copy = p;
    const std::string written = io::writePuzzle(copy);
    CHECK(io::writePuzzle(parse(written)) == written);
  }
  CHECK_THAT(parseError(nestedDisassembly(1002)),
             ContainsSubstring("the disassembly nests too deeply"));

  // A disassembly's summary is a tree too, two characters a node, and its
  // level text is made by recursion as well: 20000 nodes each inside the
  // last ended it in the stack's guard page (and it asked at every node
  // whether any node turns, by going through all of them).
  const auto summary = [](unsigned int nested) {
    std::string values;
    for (unsigned int i = 0; i < nested; i++) {
      values += "2 0 ";
    }
    return withProblem(" state=\"2\"", "<solutions><solution><assembly>0 0 0 0 1 0 0 0</assembly>"
                                       "<separationInfo>" +
                                           values + "0</separationInfo></solution></solutions>");
  };
  {
    const Puzzle p = parse(summary(1000));
    const Disassembly* d = p.getProblem(0).getSavedSolution(0).getDisassemblyInfo();
    REQUIRE(d != nullptr);
    CHECK(d->getNumSequences() == 2001);
    CHECK(d->sumMoves() == 1000);
    CHECK(d->movesText().starts_with("1.1.1.1."));
  }
  CHECK_THAT(parseError(summary(1001)), ContainsSubstring("the disassembly nests too deeply"));
  CHECK_THAT(parseError(summary(20000)), ContainsSubstring("the disassembly nests too deeply"));
  // wide, not deep: read, however many nodes
  {
    std::string wide;
    for (int i = 0; i < 14; i++) {
      wide = "2 " + wide + (wide.empty() ? "0 0 " : wide);
    }
    wide.pop_back();
    const Puzzle p = parse(withProblem(
        " state=\"2\"", "<solutions><solution><assembly>0 0 0 0 1 0 0 0</assembly><separationInfo>" +
                            wide + "</separationInfo></solution></solutions>"));
    const Disassembly* d = p.getProblem(0).getSavedSolution(0).getDisassemblyInfo();
    REQUIRE(d != nullptr);
    CHECK(d->getNumSequences() == 32767);
    CHECK_FALSE(d->movesText().empty());
  }
}

namespace {

/// Where the numbers of a document stand: (offset, length) of each run of
/// digits (with its sign and decimals), keyed by the element and, inside a
/// tag, the attribute it stands in.
struct NumberSite {
  std::size_t at;
  std::size_t len;
  std::string key;
};
std::vector<NumberSite> numberSites(const std::string& xml) {
  std::vector<NumberSite> sites;
  std::string element;
  std::string attr;
  bool inTag = false;
  char quote = 0;
  for (std::size_t i = 0; i < xml.size(); i++) {
    const char ch = xml[i];
    if (!inTag && ch == '<') {
      inTag = true;
      std::size_t j = i + 1;
      if (j < xml.size() && xml[j] != '/') {
        element.clear();
        for (; j < xml.size() && xml[j] != ' ' && xml[j] != '>' && xml[j] != '/'; j++) {
          element += xml[j];
        }
        i = j - 1;
      }
    } else if (inTag && quote == 0 && ch == '>') {
      inTag = false;
    } else if (inTag && quote == 0 && (ch == '"' || ch == '\'')) {
      quote = ch;
      std::size_t j = i;
      while (j > 0 && xml[j - 1] != ' ' && xml[j - 1] != '<') {
        j--;
      }
      attr = xml.substr(j, i - j - 1);  // without its '='
    } else if (inTag && ch == quote) {
      quote = 0;
    } else if ((quote != 0 || !inTag) &&
               ((ch >= '0' && ch <= '9') ||
                (ch == '-' && i + 1 < xml.size() && xml[i + 1] >= '0' && xml[i + 1] <= '9'))) {
      std::size_t j = i + 1;
      while (j < xml.size() && ((xml[j] >= '0' && xml[j] <= '9') || xml[j] == '.')) {
        j++;
      }
      sites.push_back(NumberSite{i, j - i, quote != 0 ? element + " " + attr : element});
      i = j - 1;
    }
  }
  return sites;
}

/// What the app does with a puzzle it opened, as far as the model goes: it
/// is written, and each solution's assembly and disassembly are walked the
/// way the level text, the playback and "assemblies to shapes" walk them.
/// A broken invariant a SL_ASSERT catches (an InternalError) is an answer,
/// counted in `asserted`; what the sanitizers find is not.
void walkAsTheAppDoes(const Puzzle& p, std::size_t& asserted) {
  const std::string written = io::writePuzzle(p);
  const Result<Puzzle> again = io::parsePuzzle(written);
  REQUIRE(again.has_value());
  CHECK(io::writePuzzle(*again) == written);
  for (unsigned int i = 0; i < p.getNumberOfProblems(); i++) {
    const Problem& pr = p.getProblem(i);
    for (unsigned int s = 0; s < pr.getNumberOfSavedSolutions(); s++) {
      const Solution& sol = pr.getSavedSolution(s);
      REQUIRE(sol.getAssembly().placementCount() == pr.getNumberOfPieces());
      try {
        if (const Separation* tree = sol.getDisassembly()) {
          (void)tree->movesText();
          DisasmToMoves moves(tree, 10, pr.getNumberOfPieces());
          for (const unsigned int step : {0U, 1U, tree->sumSteps()}) {
            moves.setStep(static_cast<float>(step), false, true);
            moves.setStep(static_cast<float>(step) + 0.5F, true, false);
          }
        }
        if (pr.resultValid()) {
          (void)sol.getAssembly().createSpace(p, pr);
        }
      } catch (const InternalError&) {
        asserted++;
      }
    }
  }
}

}  // namespace

// Every kind of number the fixtures hold, replaced by numbers no puzzle
// has: the reader refuses the file or reads it, and what it reads can be
// written, read back and walked (cleanup item 16.2). The places are found
// in the files, by the element and attribute a number stands in, so a
// number a later version adds to the format is swept too once a fixture
// has it. The first and the last number of each place are taken (an
// assembly's first number is a position and its last a transformation).
// What this cannot see is what only a sanitizer sees: `just test-asan`
// runs it under them.
TEST_CASE("puzzle file: a fixture's numbers replaced by hostile ones are refused or read",
          "[io][file]") {
  const char* const fixtures[] = {
      "tests/data/puzzles/test_rotation_solvers_solved.xmpuzzle",        // turns, orientations
      "tests/data/puzzles/AlPackino.xmpuzzle",                           // the deepest tree
      "tests/data/puzzles/new-grids/CairoSlab.xmpuzzle",                 // a grid of 2023
      "tests/data/puzzles/tilted/TriplePlay.xmpuzzle",                   // a tray
      "tests/data/puzzles/stacking/stack_panex_swap_3.xmpuzzle",         // rod sets and stacks
      "tests/data/slide_solved_by_original/slide_pocket_nested.xmpuzzle",  // a slide path
      "tests/data/slide_paused_by_v1.8.1/cup_and_blocks.xmpuzzle",       // a paused search
  };
  const char* const values[] = {"-1", "65536", "1048577", "4294967296", "99999999999999999999"};
  std::size_t read = 0;
  std::size_t refused = 0;
  std::size_t asserted = 0;
  std::set<std::string> places;
  for (const char* fixture : fixtures) {
    const std::string xml = io::readMaybeGzipFile(fixture).value();
    walkAsTheAppDoes(parse(xml), asserted);
    // the fixtures as they are break nothing
    REQUIRE(asserted == 0);
    const std::vector<NumberSite> sites = numberSites(xml);
    // the first and the last number of each place
    std::map<std::string, std::pair<std::size_t, std::size_t>> byPlace;
    for (std::size_t i = 0; i < sites.size(); i++) {
      byPlace.try_emplace(sites[i].key, i, i).first->second.second = i;
    }
    for (const auto& [place, ends] : byPlace) {
      places.insert(place);
      for (const std::size_t at : {ends.first, ends.second}) {
        for (const char* value : values) {
          INFO(fixture << ": " << place << " at " << sites[at].at << " = " << value);
          std::string hostile = xml;
          hostile.replace(sites[at].at, sites[at].len, value);
          const Result<Puzzle> p = io::parsePuzzle(hostile);
          if (!p) {
            REQUIRE(p.error().code == Errc::ParseError);
            refused++;
          } else {
            walkAsTheAppDoes(*p, asserted);
            read++;
          }
          if (ends.first == ends.second) {
            break;
          }
        }
      }
    }
  }
  // the sweep found the format's numbers, and both answers were given
  for (const char* place :
       {"voxel x", "voxel hx", "voxel weight", "voxel diskSize", "assembly", "dx", "dt", "pieces",
        "pieces count", "rotation px", "rotation piece", "shape count", "shape min", "result id",
        "tray size", "tray allowance", "rodSet rods", "rod pieces", "stacking rodSet",
        "problem searchDepth", "problem slideLevelMs", "problem maxHoles", "solution asmNum",
        "item assembly", "assembler", "pair piece", "slideMarks start", "gridType type"}) {
    INFO(place);
    CHECK(places.contains(place));
  }
  CHECK(read > 400);
  CHECK(refused > 400);
  // (how many walks ended in an assertion is not held to a number: each is
  // a place where the reader could refuse the file itself)
  CHECK(asserted < read);
}

TEST_CASE("puzzle file: a saved disassembly reads back with its levels", "[io][file]") {
  const Puzzle p = test::loadFixture("PelikanBurr.xmpuzzle");
  const Solution& s = p.getProblem(0).getSavedSolution(0);
  REQUIRE(s.getDisassembly() != nullptr);
  CHECK(s.getDisassembly()->movesText() == s.getDisassemblyInfo()->movesText());

  const Puzzle rot = test::loadFixture("test_rotation_solvers_solved.xmpuzzle");
  bool sawRotation = false;
  for (unsigned int i = 0; i < rot.getProblem(0).getNumberOfSavedSolutions(); i++) {
    const Disassembly* d = rot.getProblem(0).getSavedSolution(i).getDisassemblyInfo();
    if (d != nullptr && d->sumRotations() > 0) {
      sawRotation = true;
      CHECK_THAT(d->movesText(), ContainsSubstring("R"));
    }
  }
  CHECK(sawRotation);
}

TEST_CASE("puzzle file: a save replaces the file only once the new one is whole", "[io][file]") {
  // the original's writePuzzleFile: written beside the file, renamed over it
  const ScratchDir scratch("safe_save");
  const std::filesystem::path& dir = scratch.path();
  const std::filesystem::path file = dir / "p.xmpuzzle";
  const Puzzle a = parse(withProblem("", ""));
  REQUIRE(io::savePuzzle(a, file).has_value());
  CHECK_FALSE(std::filesystem::exists(dir / "p.xmpuzzle.tmp"));
  const std::string before = readText(file);
  REQUIRE(io::loadPuzzle(file).has_value());

  // the new file cannot be written (its place is a folder): the old one stays
  std::filesystem::create_directories(dir / "p.xmpuzzle.tmp");
  const Result<void> r = io::savePuzzle(a, file);
  REQUIRE_FALSE(r.has_value());
  CHECK(r.error().code == Errc::IoError);
  CHECK(readText(file) == before);

  // a paused solve's file the same way
  const std::filesystem::path solve = dir / "s.slsolve";
  std::filesystem::create_directories(dir / "s.slsolve.tmp");
  {
    std::ofstream out(solve, std::ios::binary);
    out << "old";
  }
  CHECK_FALSE(io::savePausedSolve(a, 0, solve).has_value());
  CHECK(readText(solve) == "old");
  std::filesystem::remove_all(dir / "s.slsolve.tmp");
  REQUIRE(io::savePausedSolve(a, 0, solve).has_value());
  CHECK_FALSE(std::filesystem::exists(dir / "s.slsolve.tmp"));
  CHECK(io::loadPausedSolve(solve).has_value());
}

TEST_CASE("puzzle file: a save through a symbolic link writes the file it names", "[io][file]") {
  // a rename over the link would replace the link by a file and leave the
  // puzzle's own file as it was
  const ScratchDir scratch("save_link");
  const std::filesystem::path& dir = scratch.path();
  std::filesystem::create_directories(dir / "real");
  std::filesystem::create_directories(dir / "links");
  const std::filesystem::path real = dir / "real" / "p.xmpuzzle";
  const std::filesystem::path link = dir / "links" / "p.xmpuzzle";
  const Puzzle a = parse(withProblem("", ""));
  const Puzzle b = parse(withProblem(" name=\"b\"", ""));
  REQUIRE(io::writePuzzle(a) != io::writePuzzle(b));
  REQUIRE(io::savePuzzle(a, real).has_value());
  std::error_code ec;
  std::filesystem::create_symlink(std::filesystem::path("..") / "real" / "p.xmpuzzle", link, ec);
  if (ec) {
    SKIP("no symbolic links here: " << ec.message());
  }
  REQUIRE(io::savePuzzle(b, link).has_value());
  CHECK(std::filesystem::is_symlink(link));
  const Result<Puzzle> back = io::loadPuzzle(real);
  REQUIRE(back.has_value());
  CHECK(io::writePuzzle(*back) == io::writePuzzle(b));
  CHECK_FALSE(std::filesystem::exists(dir / "real" / "p.xmpuzzle.tmp"));
  CHECK_FALSE(std::filesystem::exists(dir / "links" / "p.xmpuzzle.tmp"));

  // a paused solve's file the same way
  const std::filesystem::path solve = dir / "real" / "s.slsolve";
  const std::filesystem::path solveLink = dir / "links" / "s.slsolve";
  REQUIRE(io::savePausedSolve(a, 0, solve).has_value());
  std::filesystem::create_symlink(std::filesystem::path("..") / "real" / "s.slsolve", solveLink);
  REQUIRE(io::savePausedSolve(b, 0, solveLink).has_value());
  CHECK(std::filesystem::is_symlink(solveLink));
  const Result<io::PausedSolve> ps = io::loadPausedSolve(solve);
  REQUIRE(ps.has_value());
  CHECK(io::writePuzzle(ps->puzzle) == io::writePuzzle(b));
}

TEST_CASE("puzzle file: a save never writes through a planted temporary file", "[io][file]") {
  // the temporary file is the save's own: a link left at <file>.tmp (by
  // whoever may write in the folder) is removed, not written through, and
  // the file is made exclusively; a stale file of a save that died goes too
  const ScratchDir scratch("save_tmp");
  const std::filesystem::path& dir = scratch.path();
  const std::filesystem::path file = dir / "p.xmpuzzle";
  const std::filesystem::path victim = dir / "victim";
  {
    std::ofstream out(victim, std::ios::binary);
    out << "untouched";
  }
  std::error_code ec;
  std::filesystem::create_symlink("victim", dir / "p.xmpuzzle.tmp", ec);
  if (ec) {
    SKIP("no symbolic links here: " << ec.message());
  }
  const Puzzle a = parse(withProblem("", ""));
  REQUIRE(io::savePuzzle(a, file).has_value());
  CHECK(readText(victim) == "untouched");
  CHECK(std::filesystem::is_regular_file(std::filesystem::symlink_status(file)));
  CHECK_FALSE(std::filesystem::exists(std::filesystem::symlink_status(dir / "p.xmpuzzle.tmp")));
  REQUIRE(io::loadPuzzle(file).has_value());
  {
    std::ofstream out(dir / "p.xmpuzzle.tmp", std::ios::binary);
    out << "stale";
  }
  REQUIRE(io::savePuzzle(a, file).has_value());
  CHECK_FALSE(std::filesystem::exists(dir / "p.xmpuzzle.tmp"));

  // a paused solve's file the same way
  const std::filesystem::path solve = dir / "s.slsolve";
  std::filesystem::create_symlink("victim", dir / "s.slsolve.tmp");
  REQUIRE(io::savePausedSolve(a, 0, solve).has_value());
  CHECK(readText(victim) == "untouched");
  CHECK(std::filesystem::is_regular_file(std::filesystem::symlink_status(solve)));
  CHECK_FALSE(std::filesystem::exists(std::filesystem::symlink_status(dir / "s.slsolve.tmp")));
  CHECK(io::loadPausedSolve(solve).has_value());
}

TEST_CASE("puzzle file: a save refuses a read-only file and leaves it as it was", "[io][file]") {
#ifndef _WIN32
  if (geteuid() == 0) {
    SKIP("root may write any file");
  }
#endif
  // a rename replaces a file whatever its permissions; writing it did not
  using std::filesystem::perms;
  const perms readOnly = perms::owner_read | perms::group_read | perms::others_read;
  const ScratchDir scratch("save_read_only");
  const std::filesystem::path& dir = scratch.path();
  const std::filesystem::path file = dir / "p.xmpuzzle";
  const Puzzle a = parse(withProblem("", ""));
  const Puzzle b = parse(withProblem(" name=\"b\"", ""));
  REQUIRE(io::savePuzzle(a, file).has_value());
  const std::string before = readText(file);
  std::filesystem::permissions(file, readOnly);
  const Result<void> r = io::savePuzzle(b, file);
  REQUIRE_FALSE(r.has_value());
  CHECK(r.error().code == Errc::IoError);
  CHECK_THAT(r.error().message, ContainsSubstring("read-only"));
  CHECK(readText(file) == before);
  CHECK_FALSE(std::filesystem::exists(dir / "p.xmpuzzle.tmp"));

  // a paused solve's file the same way
  const std::filesystem::path solve = dir / "s.slsolve";
  REQUIRE(io::savePausedSolve(a, 0, solve).has_value());
  const std::string solveBefore = readText(solve);
  std::filesystem::permissions(solve, readOnly);
  const Result<void> rs = io::savePausedSolve(b, 0, solve);
  REQUIRE_FALSE(rs.has_value());
  CHECK(rs.error().code == Errc::IoError);
  CHECK(readText(solve) == solveBefore);
  std::filesystem::permissions(file, perms::owner_all);
  std::filesystem::permissions(solve, perms::owner_all);
}

TEST_CASE("puzzle file: a write the system refuses says the system's reason", "[io][file]") {
#ifdef _WIN32
  SKIP("a folder Windows refuses to write needs its access control list set up");
#else
  if (geteuid() == 0) {
    SKIP("root may write any folder");
  }
  // cleanup item 2.5: "could not open … for writing: Permission denied (errno 13)",
  // as Windows says "Access is denied (5)" when Controlled Folder Access blocks it
  using std::filesystem::perms;
  const ScratchDir scratch("save_refused");
  const std::filesystem::path& dir = scratch.path();
  std::filesystem::permissions(dir, perms::owner_read | perms::owner_exec);
  const Puzzle a = parse(withProblem("", ""));
  const Result<void> r = io::savePuzzle(a, dir / "p.xmpuzzle");
  const Result<void> w = io::writeFileWhole(dir / "x.txt", "x");
  const Result<std::string> missing = io::readPlainFile(dir / "none.txt", 100);
  std::filesystem::permissions(dir, perms::owner_all);
  REQUIRE_FALSE(r.has_value());
  CHECK(r.error().code == Errc::IoError);
  CHECK_THAT(r.error().message, ContainsSubstring("for writing: "));
  CHECK_THAT(r.error().message, ContainsSubstring("(errno 13)"));
  // the plain file writer and the reader the same way
  REQUIRE_FALSE(w.has_value());
  CHECK_THAT(w.error().message, ContainsSubstring("(errno 13)"));
  REQUIRE_FALSE(missing.has_value());
  CHECK_THAT(missing.error().message, ContainsSubstring("(errno 2)"));
#endif
}

TEST_CASE("puzzle file: a save keeps the file's permissions", "[io][file]") {
#ifdef _WIN32
  SKIP("Windows keeps only a read-only flag");
#endif
  // a new file has the default permissions (0644 under the usual umask): a
  // private puzzle would become readable by everyone
  using std::filesystem::perms;
  const ScratchDir scratch("save_permissions");
  const std::filesystem::path& dir = scratch.path();
  const std::filesystem::path file = dir / "p.xmpuzzle";
  const Puzzle a = parse(withProblem("", ""));
  REQUIRE(io::savePuzzle(a, file).has_value());
  for (const perms mode : {perms::owner_read | perms::owner_write,
                           perms::owner_read | perms::owner_write | perms::group_read |
                               perms::group_write | perms::others_read}) {
    std::filesystem::permissions(file, mode);
    REQUIRE(io::savePuzzle(a, file).has_value());
    CHECK((std::filesystem::status(file).permissions() & perms::all) == mode);
  }
}

// A sliding puzzle's starts and goals are each problem's own (decision
// 0027), written in the problem (<slideMarks>). A file from when they were
// the start/goal shape's (the voxel's start= and goal=, decision 0021) has
// them given to the problems on that shape as it loads; a file of the
// original's keeps the starts as the cell colors of its start/goal
// shapes, with the pieces painted in their marks and a palette for the
// locks: converted as it loads. Both are written the new way.
TEST_CASE("file: sliding start marks are read, written and converted from colors",
          "[io][sliding]") {
  const Result<Puzzle> p = io::parsePuzzle(
      "<?xml version=\"1.0\"?><puzzle version=\"2\"><gridType type=\"5\"/><shapes>"
      "<voxel x=\"2\" y=\"2\" z=\"1\" name=\"sg:1:\" start=\"2,0,0,3\" goal=\"0,2,0,0\" "
      "type=\"0\">####</voxel>"
      "<voxel x=\"1\" y=\"1\" z=\"1\" type=\"0\">#</voxel>"
      "<voxel x=\"1\" y=\"1\" z=\"1\" type=\"0\">#</voxel></shapes>"
      "<problems><problem><result id=\"0\"/></problem><problem><result id=\"0\"/></problem>"
      "<problem/></problems></puzzle>");
  REQUIRE(p.has_value());
  const Voxel& v = p->getShape(0);
  // every problem on the shape gets them, and the shape keeps none
  for (const unsigned int problem : {0U, 1U}) {
    const SlideMarks& m = p->getProblem(problem).slideMarks();
    CHECK(m.start(0) == 2);
    CHECK(m.start(3) == 3);
    CHECK(m.goal(1) == 2);
  }
  CHECK(p->getProblem(2).slideMarks().empty());
  CHECK_FALSE(v.hasStartPieces());
  CHECK_FALSE(v.hasGoalPieces());
  CHECK(v.getColor(0) == 0);
  const std::string out = io::writePuzzle(*p);
  CHECK(out.find("slideMarks=\"problem\"") != std::string::npos);
  CHECK(out.find("<slideMarks start=\"2,0,0,3\" goal=\"0,2\"/>") != std::string::npos);
  CHECK(out.find("<voxel x=\"2\" y=\"2\" z=\"1\" name=\"sg:1:\" type=\"0\">") !=
        std::string::npos);
  // read again: each problem's own, and a change to one stays its own
  Result<Puzzle> again1 = io::parsePuzzle(out);
  REQUIRE(again1.has_value());
  CHECK(again1->getProblem(0).slideMarks() == p->getProblem(0).slideMarks());
  again1->getProblem(1).editSlideMarks().set(2, true, 3);
  const Result<Puzzle> again2 = io::parsePuzzle(io::writePuzzle(*again1));
  REQUIRE(again2.has_value());
  CHECK(again2->getProblem(0).slideMarks() == p->getProblem(0).slideMarks());
  CHECK(again2->getProblem(1).slideMarks() == again1->getProblem(1).slideMarks());
  CHECK(again2->getProblem(1).slideMarks().goal(2) == 3);

  // the original's Klotski: starts as colors, pieces painted, 11 colors
  Puzzle k = test::loadFixture("sliding/slide_klotski.xmpuzzle");
  const Voxel& tray = k.getShape(0);
  CHECK(sliding::isStartGoalShape(&tray));
  REQUIRE(k.getNumberOfProblems() > 0);
  CHECK(k.getProblem(0).slideMarks().start(tray.getIndex(1, 0, 0)) == 2);
  CHECK_FALSE(tray.hasStartPieces());
  CHECK(k.colorNumber() == 0);
  for (unsigned int s = 0; s < k.getNumberOfShapes(); s++) {
    for (unsigned int i = 0; i < k.getShape(s).getXYZ(); i++) {
      CHECK(k.getShape(s).getColor(i) == 0);
    }
  }
  // saved and read again, the marks are the same, and no colors came back
  const Result<Puzzle> again = io::parsePuzzle(io::writePuzzle(k));
  REQUIRE(again.has_value());
  CHECK(again->getShape(0) == tray);
  CHECK(again->getProblem(0).slideMarks() == k.getProblem(0).slideMarks());
  CHECK(again->colorNumber() == 0);
  CHECK(io::writePuzzle(k).find("<slideMarks start=") != std::string::npos);
}

// Start and goal marks belong to the sliding grid alone: only its grid
// class moves them with the cells, so marks read on another grid would be
// left the wrong length by a transform or a scale (cleanup item 11.4).
TEST_CASE("file: marks on a grid other than the sliding one are dropped as it loads",
          "[io][sliding]") {
  for (const char* grid : {"0", "7"}) {
    INFO("grid " << grid);
    const Result<Puzzle> p = io::parsePuzzle(
        std::string("<?xml version=\"1.0\"?><puzzle version=\"2\"><gridType type=\"") + grid +
        "\"/><shapes><voxel x=\"2\" y=\"2\" z=\"1\" start=\"2,0,0,3\" goal=\"0,2,0,0\" "
        "type=\"0\">####</voxel></shapes><problems><problem><result id=\"0\"/></problem>"
        "</problems></puzzle>");
    REQUIRE(p.has_value());
    const Voxel& v = p->getShape(0);
    CHECK_FALSE(v.hasStartPieces());
    CHECK_FALSE(v.hasGoalPieces());
    // every turn keeps the shape whole (recalcBoundingBox checks the marks)
    for (unsigned int t = 1; t < v.getGridType().getSymmetries().getNumTransformationsMirror(); t++) {
      const std::unique_ptr<Voxel> turned = v.clone();
      CHECK(turned->transform(t));
      CHECK(turned->countState(Voxel::kFilled) == 4);
    }
  }
}

// Decision 0044: a problem's uid names its saved searches, so it is kept
// through saves, renames and moves, and no two problems of a file share one.
TEST_CASE("puzzle file: every problem has a uid of its own, kept as it is saved",
          "[io][file][uid]") {
  const auto problems = [](const std::string& each, int n) {
    std::string xml =
        "<puzzle version=\"2\"><gridType type=\"0\"/><colors/>"
        "<shapes><voxel x=\"1\" y=\"1\" z=\"1\" type=\"0\">#</voxel></shapes><problems>";
    for (int i = 0; i < n; i++) {
      xml += each;
    }
    return xml + "</problems><comment/></puzzle>";
  };
  const std::string plain =
      "<problem state=\"0\"><shapes><shape id=\"0\" count=\"1\"/></shapes>"
      "<result id=\"0\"/><bitmap/></problem>";

  // read without uids, two problems alike in every cell are told apart, and
  // named the same each time the file is read
  const Puzzle a = parse(problems(plain, 2));
  CHECK(a.getProblem(0).getUid() != 0);
  CHECK(a.getProblem(1).getUid() != 0);
  CHECK(a.getProblem(0).getUid() != a.getProblem(1).getUid());
  const Puzzle b = parse(problems(plain, 2));
  CHECK(b.getProblem(0).getUid() == a.getProblem(0).getUid());
  CHECK(b.getProblem(1).getUid() == a.getProblem(1).getUid());

  // saved, renamed and moved, each keeps its own
  Puzzle c = parse(io::writePuzzle(a));
  CHECK(c.getProblem(0).getUid() == a.getProblem(0).getUid());
  c.getProblem(0).setName("renamed");
  c.exchangeProblems(0, 1);
  const Puzzle d = parse(io::writePuzzle(c));
  CHECK(d.getProblem(0).getUid() == a.getProblem(1).getUid());
  CHECK(d.getProblem(1).getUid() == a.getProblem(0).getUid());
  CHECK(d.getProblem(1).getName() == "renamed");
  CHECK_THAT(io::writePuzzle(d), ContainsSubstring("<problem name=\"renamed\" uid=\""));

  // a uid copied by hand in the file: the second problem gets another
  const std::string same =
      "<problem uid=\"00000000000000aa\" state=\"0\"><shapes><shape id=\"0\" count=\"1\"/>"
      "</shapes><result id=\"0\"/><bitmap/></problem>";
  const Puzzle e = parse(problems(same, 2));
  CHECK(e.getProblem(0).getUid() == 0xaa);
  CHECK(e.getProblem(1).getUid() != 0xaa);
  CHECK(e.getProblem(1).getUid() != 0);

  // a new problem and a copy of one have uids of their own
  Puzzle f = parse(problems(plain, 1));
  const unsigned int added = f.addProblem();
  const unsigned int copied = f.addProblem(f.getProblem(0));
  const std::set<std::uint64_t> uids{f.getProblem(0).getUid(), f.getProblem(added).getUid(),
                                     f.getProblem(copied).getUid()};
  CHECK(uids.size() == 3);
  CHECK_FALSE(uids.contains(0));
}

// A problem left paused by a version before uids keeps none, so that its
// saved search, named without one, is still found; an unpaused one, or a
// second paused one alike in every cell (they shared the folder), gets one.
TEST_CASE("puzzle file: a problem paused before uids keeps its saved search's name",
          "[io][file][uid]") {
  const std::string paused =
      "<problem state=\"1\"><shapes><shape id=\"0\" count=\"1\"/></shapes>"
      "<result id=\"0\"/><bitmap/></problem>";
  const std::string solved =
      "<problem state=\"2\"><shapes><shape id=\"0\" count=\"1\"/></shapes>"
      "<result id=\"0\"/><bitmap/></problem>";
  const Puzzle p = parse(
      "<puzzle version=\"2\"><gridType type=\"0\"/><colors/>"
      "<shapes><voxel x=\"1\" y=\"1\" z=\"1\" type=\"0\">#</voxel></shapes><problems>" +
      solved + paused + paused + "</problems><comment/></puzzle>");
  CHECK(p.getProblem(0).getUid() != 0);
  CHECK(p.getProblem(1).getUid() == 0);
  CHECK(p.getProblem(2).getUid() != 0);
  // saved without one, and read again the same
  const std::string out = io::writePuzzle(p);
  const Puzzle again = parse(out);
  CHECK(again.getProblem(1).getUid() == 0);
  CHECK(io::writePuzzle(again) == out);
}

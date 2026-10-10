// SPDX-License-Identifier: GPL-3.0-or-later
// The .xmpuzzle reader and writer: what the original writes, what is kept
// although it is not modelled (decisions 0006 and 0008), and what is refused.

#include "../model/model_test_helpers.h"

#include "bt/io/paused_solve.h"
#include "bt/io/puzzle_file.h"
#include "bt/io/xml_document.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#ifndef _WIN32
#include <unistd.h>
#endif

using namespace bt;
using Catch::Matchers::ContainsSubstring;

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

/// A directory of a test's own, empty.
std::filesystem::path freshDir(const std::string& name) {
  const std::filesystem::path dir = std::filesystem::temp_directory_path() / name;
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  return dir;
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
    INFO(path.filename().string());
    const Puzzle p = test::loadFixture(path.filename().string());
    const std::string ours = io::writePuzzle(p);
    const std::string theirs =
        readText(std::filesystem::path("tests/data/saved_by_original") /
                 (path.stem().string() + ".xml"));
    REQUIRE_FALSE(theirs.empty());
    CHECK(ours == theirs);

    // and the save reads back to itself
    CHECK(io::writePuzzle(parse(ours)) == ours);
    checked++;
  }
  REQUIRE(checked >= 9);
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
  CHECK_THAT(out, ContainsSubstring("<problem state=\"0\" mood=\"good\">"));
  CHECK_THAT(out, ContainsSubstring("<result id=\"0\"/>\n   <newThing/>\n   <bitmap/>"));
  CHECK_THAT(out, ContainsSubstring("<comment>c</comment>\n <last/>\n</puzzle>"));

  // a second round changes nothing
  CHECK(io::writePuzzle(parse(out)) == out);
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

TEST_CASE("puzzle file: stacking rod sets are kept, or the default one is written",
          "[io][file]") {
  const Puzzle hanoi = test::loadFixture("test_stacking_hanoi.xmpuzzle");
  REQUIRE(hanoi.preserved().findChild("rodSets") != nullptr);
  REQUIRE(hanoi.getProblem(0).preserved().findChild("stacking") != nullptr);

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
      " state=\"1\" assemblies=\"5\" solutions=\"2\" time=\"3\" timeMs=\"3456\" searched=\"99\"",
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
  CHECK(pr.getAssemblerVersion() == "1.2");
  CHECK(pr.getAssemblerState() == "abc def");
  REQUIRE(pr.getPending().size() == 1);  // the item without an assembly is dropped
  CHECK(pr.getPending()[0].counted);
  CHECK(pr.getPending()[0].assemblyNumber == 4);
  CHECK(pr.getPending()[0].assembly.getX(0) == 1);

  const std::string out = io::writePuzzle(p);
  CHECK_THAT(out, ContainsSubstring("<assembler version=\"1.2\">abc def</assembler>"));
  CHECK_THAT(out, ContainsSubstring("<item counted=\"1\" assembly=\"4\" solution=\"2\">"));
  CHECK(io::writePuzzle(parse(out)) == out);

  // once solved, neither is written
  Puzzle solved = p;
  solved.getProblem(0).finishedSolving();
  CHECK_THAT(io::writePuzzle(solved), !ContainsSubstring("assembler"));
  CHECK_THAT(io::writePuzzle(solved), !ContainsSubstring("pending"));
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

TEST_CASE("puzzle file: voxel content, colours and goal marks", "[io][file]") {
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
  CHECK(v.getColor(2) == 0);  // an empty cell has no colour
  CHECK(v.getHx() == 1);
  CHECK(v.getWeight() == 3);
  CHECK(v.getDiskSize() == 2);
  CHECK(v.getShapeColor() == 0x00FF10);
  CHECK(v.getGoalPiece(1) == 2);
  CHECK(v.getGoalPiece(2) == 0);  // goal marks of empty cells go
  CHECK(v.getGoalPiece(3) == 1);
  CHECK_THAT(io::writePuzzle(p),
             ContainsSubstring("<voxel x=\"4\" y=\"1\" z=\"1\" hx=\"1\" weight=\"3\" name=\"n\" "
                               "diskSize=\"2\" color=\"#00FF10\" goal=\"0,2,0,1\" "
                               "type=\"0\">#1+63_#</voxel>"));
}

TEST_CASE("puzzle file: what the original refuses is refused", "[io][file]") {
  const std::string head = "<puzzle version=\"2\"><gridType type=\"0\"/><shapes>";
  const std::string tail = "</shapes></puzzle>";
  auto voxel = [&](const std::string& v) { return head + v + tail; };

  CHECK_THAT(parseError("<puzzle/>"), ContainsSubstring("'version'"));
  CHECK_THAT(parseError("<puzzle version=\"3\"/>"), ContainsSubstring("version 1 and 2"));
  CHECK_THAT(parseError("<problem version=\"2\"/>"), ContainsSubstring("puzzle"));
  CHECK_THAT(parseError("<puzzle version=\"2\"><gridType type=\"7\"/></puzzle>"),
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
             ContainsSubstring("unrecognised character"));

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
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "bt_test_safe_save";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
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
  const std::filesystem::path solve = dir / "s.btsolve";
  std::filesystem::create_directories(dir / "s.btsolve.tmp");
  {
    std::ofstream out(solve, std::ios::binary);
    out << "old";
  }
  CHECK_FALSE(io::savePausedSolve(a, 0, solve).has_value());
  CHECK(readText(solve) == "old");
  std::filesystem::remove_all(dir / "s.btsolve.tmp");
  REQUIRE(io::savePausedSolve(a, 0, solve).has_value());
  CHECK_FALSE(std::filesystem::exists(dir / "s.btsolve.tmp"));
  CHECK(io::loadPausedSolve(solve).has_value());
  std::filesystem::remove_all(dir);
}

TEST_CASE("puzzle file: a save through a symbolic link writes the file it names", "[io][file]") {
  // a rename over the link would replace the link by a file and leave the
  // puzzle's own file as it was
  const std::filesystem::path dir = freshDir("bt_test_save_link");
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
    std::filesystem::remove_all(dir);
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
  const std::filesystem::path solve = dir / "real" / "s.btsolve";
  const std::filesystem::path solveLink = dir / "links" / "s.btsolve";
  REQUIRE(io::savePausedSolve(a, 0, solve).has_value());
  std::filesystem::create_symlink(std::filesystem::path("..") / "real" / "s.btsolve", solveLink);
  REQUIRE(io::savePausedSolve(b, 0, solveLink).has_value());
  CHECK(std::filesystem::is_symlink(solveLink));
  const Result<io::PausedSolve> ps = io::loadPausedSolve(solve);
  REQUIRE(ps.has_value());
  CHECK(io::writePuzzle(ps->puzzle) == io::writePuzzle(b));
  std::filesystem::remove_all(dir);
}

TEST_CASE("puzzle file: a save never writes through a planted temporary file", "[io][file]") {
  // the temporary file is the save's own: a link left at <file>.tmp (by
  // whoever may write in the folder) is removed, not written through, and
  // the file is made exclusively; a stale file of a save that died goes too
  const std::filesystem::path dir = freshDir("bt_test_save_tmp");
  const std::filesystem::path file = dir / "p.xmpuzzle";
  const std::filesystem::path victim = dir / "victim";
  {
    std::ofstream out(victim, std::ios::binary);
    out << "untouched";
  }
  std::error_code ec;
  std::filesystem::create_symlink("victim", dir / "p.xmpuzzle.tmp", ec);
  if (ec) {
    std::filesystem::remove_all(dir);
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
  const std::filesystem::path solve = dir / "s.btsolve";
  std::filesystem::create_symlink("victim", dir / "s.btsolve.tmp");
  REQUIRE(io::savePausedSolve(a, 0, solve).has_value());
  CHECK(readText(victim) == "untouched");
  CHECK(std::filesystem::is_regular_file(std::filesystem::symlink_status(solve)));
  CHECK_FALSE(std::filesystem::exists(std::filesystem::symlink_status(dir / "s.btsolve.tmp")));
  CHECK(io::loadPausedSolve(solve).has_value());
  std::filesystem::remove_all(dir);
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
  const std::filesystem::path dir = freshDir("bt_test_save_read_only");
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
  const std::filesystem::path solve = dir / "s.btsolve";
  REQUIRE(io::savePausedSolve(a, 0, solve).has_value());
  const std::string solveBefore = readText(solve);
  std::filesystem::permissions(solve, readOnly);
  const Result<void> rs = io::savePausedSolve(b, 0, solve);
  REQUIRE_FALSE(rs.has_value());
  CHECK(rs.error().code == Errc::IoError);
  CHECK(readText(solve) == solveBefore);
  std::filesystem::permissions(file, perms::owner_all);
  std::filesystem::permissions(solve, perms::owner_all);
  std::filesystem::remove_all(dir);
}

TEST_CASE("puzzle file: a save keeps the file's permissions", "[io][file]") {
#ifdef _WIN32
  SKIP("Windows keeps only a read-only flag");
#endif
  // a new file has the default permissions (0644 under the usual umask): a
  // private puzzle would become readable by everyone
  using std::filesystem::perms;
  const std::filesystem::path dir = freshDir("bt_test_save_permissions");
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
  std::filesystem::remove_all(dir);
}

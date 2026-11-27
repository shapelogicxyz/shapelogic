// SPDX-License-Identifier: GPL-3.0-or-later
// The change audit (cleanup item 19.2): what a command tells of its change
// against what it changed. The front end keeps mirrors of the engine's
// state and refreshes them by document.changed's `what` and id lists
// (AGENTS.md's rule), so a kind left out leaves a stale screen, with no
// test failing: each command wrote its own report by hand. Here every
// command of the Document makes a typical call on a fixture, and the file
// the document would save, taken apart by kind before and after, says what
// changed:
// - every kind that changed is told (a failure when not), and a change has
//   a revision;
// - an id list that is not empty names every shape or problem that changed,
//   and none was removed or moved ("empty: the list itself changed");
// - undo gives back every byte and tells at least as much, and so does redo;
// - a kind told that did not change is listed in the test's output (a
//   warning: a refresh too many, nothing stale).
// The commands are found in document.h and the headers of the commands of
// each kind of puzzle (sliding_edits.h, tray_edits.h, stacking_edits.h,
// cleanup item 21.6): one that is neither in the table nor left out with
// its reason fails the test.
#include "../scratch_dir.h"
#include "app_test_helpers.h"

#include "shapelogic/app/analysis.h"
#include "shapelogic/app/document.h"
#include "shapelogic/app/shape_tools.h"
#include "shapelogic/app/sliding_edits.h"
#include "shapelogic/app/stacking_edits.h"
#include "shapelogic/app/tray_edits.h"
#include "shapelogic/grid/grid_type.h"
#include "shapelogic/io/puzzle_file.h"
#include "shapelogic/io/xml_document.h"
#include "shapelogic/io/xml_writer.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/model/solution.h"
#include "shapelogic/model/stacking_types.h"
#include "shapelogic/model/tilted_tray.h"
#include "shapelogic/model/xml_element.h"
#include "shapelogic/solve/solve_session.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using namespace sl;
using namespace sl::app;
using sl::test::openFixture;

namespace {

// ---- what a document holds, by kind ----------------------------------------------

// A problem as the file holds it, in two parts: what defines it, and what
// a solve left in it.
struct ProblemText {
  std::string definition;
  std::string results;
  std::uint64_t version = 0;  // Problem::solutionsVersion()
};

struct Held {
  std::vector<Id> shapeIds;
  std::vector<Id> colorIds;
  std::vector<Id> problemIds;
  std::vector<Id> rodSetIds;
  std::map<Id, std::string> shapes;
  std::map<Id, ProblemText> problems;
  std::string images;  // the pictures the shapes show
  std::string colors;
  std::string notes;
  std::string rodSets;
  std::string tray;
  std::string rest;  // whatever else the file holds
};

std::string textOf(const XmlElement& e) {
  io::XmlWriter xml;
  xml.writeElement(e);
  return xml.str();
}

// The attributes and children of a <problem> that a solve writes: its
// state and counts, its position, its solutions. The rest defines it.
bool isResultAttribute(std::string_view name) {
  static const std::set<std::string_view> names = {
      "state",       "assemblies", "solutions", "time",         "timeMs",
      "searched",    "searchDepth", "triedApart", "slideGrid", "slideLevelMs"};
  return names.contains(name);
}
bool isResultChild(std::string_view name) {
  static const std::set<std::string_view> names = {"assembler", "pending", "solutions",
                                                   "solutionsWithRotations"};
  return names.contains(name);
}

Held hold(const Document& d) {
  Held h;
  h.shapeIds = d.shapeIds();
  h.colorIds = d.colorIds();
  h.problemIds = d.problemIds();
  h.rodSetIds = d.rodSetIds();
  const Result<XmlElement> root = io::parseXmlDocument(io::writePuzzle(d.puzzle()));
  REQUIRE(root.has_value());
  for (const XmlAttribute& a : root->attributes) {
    h.rest += a.name + "=" + a.value + "\n";
  }
  for (const XmlElement& e : root->children) {
    if (e.name == "shapes") {
      REQUIRE(e.children.size() == h.shapeIds.size());
      for (std::size_t i = 0; i < e.children.size(); i++) {
        h.shapes[h.shapeIds[i]] = textOf(e.children[i]);
      }
    } else if (e.name == "problems") {
      REQUIRE(e.children.size() == h.problemIds.size());
      for (std::size_t i = 0; i < e.children.size(); i++) {
        XmlElement definition;
        definition.name = "problem";
        XmlElement results;
        results.name = "results";
        for (const XmlAttribute& a : e.children[i].attributes) {
          (isResultAttribute(a.name) ? results : definition).attributes.push_back(a);
        }
        for (const XmlElement& c : e.children[i].children) {
          (isResultChild(c.name) ? results : definition).children.push_back(c);
        }
        ProblemText& p = h.problems[h.problemIds[i]];
        p.definition = textOf(definition);
        p.results = textOf(results);
        p.version = d.puzzle().getProblem(static_cast<unsigned int>(i)).solutionsVersion();
      }
    } else if (e.name == "images") {
      h.images = textOf(e);
    } else if (e.name == "colors") {
      h.colors = textOf(e);
    } else if (e.name == "comment") {
      h.notes = textOf(e);
    } else if (e.name == "rodSets") {
      h.rodSets = textOf(e);
    } else if (e.name == "tray") {
      h.tray = textOf(e);
    } else {
      h.rest += textOf(e);
    }
  }
  return h;
}

// Whether two documents hold the same, byte for byte. The solutions'
// versions are left out: they count on across undo (decision 0011,
// amended), so a list given back by undo has a new one.
bool same(const Held& a, const Held& b) {
  if (a.shapeIds != b.shapeIds || a.colorIds != b.colorIds || a.problemIds != b.problemIds ||
      a.rodSetIds != b.rodSetIds || a.shapes != b.shapes || a.images != b.images ||
      a.colors != b.colors || a.notes != b.notes || a.rodSets != b.rodSets || a.tray != b.tray ||
      a.rest != b.rest || a.problems.size() != b.problems.size()) {
    return false;
  }
  return std::ranges::all_of(a.problems, [&b](const auto& p) {
    const auto other = b.problems.find(p.first);
    return other != b.problems.end() && other->second.definition == p.second.definition &&
           other->second.results == p.second.results;
  });
}

// What differs between two states of a document.
struct Differs {
  std::uint32_t what = 0;  // ChangeFlag bits
  // the shapes and problems that are new or hold something else
  std::set<Id> shapes;
  std::set<Id> problems;
  // one was removed, or the ones that stayed stand in another order
  bool shapeListChanged = false;
  bool problemListChanged = false;
  bool rest = false;
};

// Whether the ids of `before` that `after` still has stand as they did,
// with none gone.
bool listChanged(const std::vector<Id>& before, const std::vector<Id>& after) {
  std::vector<Id> kept;
  for (const Id id : after) {
    if (std::ranges::find(before, id) != before.end()) {
      kept.push_back(id);
    }
  }
  return kept != before;
}

Differs differs(const Held& a, const Held& b) {
  Differs d;
  d.shapeListChanged = listChanged(a.shapeIds, b.shapeIds);
  d.problemListChanged = listChanged(a.problemIds, b.problemIds);
  if (a.shapeIds != b.shapeIds || a.images != b.images) {
    d.what |= kChangeShapes;
  }
  for (const auto& [id, text] : b.shapes) {
    const auto was = a.shapes.find(id);
    if (was == a.shapes.end() || was->second != text) {
      d.shapes.insert(id);
      d.what |= kChangeShapes;
    }
  }
  if (a.colorIds != b.colorIds || a.colors != b.colors) {
    d.what |= kChangeColors;
  }
  if (a.problemIds != b.problemIds) {
    d.what |= kChangeProblems;
  }
  for (const auto& [id, text] : b.problems) {
    const auto was = a.problems.find(id);
    if (was == a.problems.end()) {
      d.problems.insert(id);
      d.what |= kChangeProblems;
      continue;
    }
    if (was->second.definition != text.definition) {
      d.problems.insert(id);
      d.what |= kChangeProblems;
    }
    if (was->second.results != text.results || was->second.version != text.version) {
      d.problems.insert(id);
      d.what |= kChangeSolutions;
    }
  }
  if (a.notes != b.notes) {
    d.what |= kChangeNotes;
  }
  if (a.rodSetIds != b.rodSetIds || a.rodSets != b.rodSets) {
    d.what |= kChangeRodSets;
  }
  if (a.tray != b.tray) {
    d.what |= kChangeTray;
  }
  d.rest = a.rest != b.rest;
  return d;
}

std::string kinds(std::uint32_t what) {
  static const std::vector<std::pair<std::uint32_t, const char*>> names = {
      {kChangeShapes, "shapes"},     {kChangeColors, "colors"}, {kChangeProblems, "problems"},
      {kChangeSolutions, "solutions"}, {kChangeNotes, "notes"},   {kChangeRodSets, "rodSets"},
      {kChangeTray, "tray"}};
  std::string out;
  for (const auto& [bit, name] : names) {
    if ((what & bit) != 0) {
      out += (out.empty() ? "" : "|") + std::string(name);
    }
  }
  return out.empty() ? "nothing" : out;
}

// ---- the fixtures -----------------------------------------------------------------

enum class On : std::uint8_t { Cube, ManySolutions, Sliding, Stacking, Tilted };

Id piece(Document& d, unsigned int w, unsigned int h, const std::vector<CellEdit>& cells) {
  const Id s = d.createShape(std::array<unsigned int, 3>{w, h, 1}).value();
  REQUIRE(d.editCells(s, CellTool::Fixed, 0, 0, cells).has_value());
  return s;
}

// The start of a PNG file of w by h pixels: as much as the document reads.
std::string pngHeader(unsigned int w, unsigned int h) {
  std::string b = "\x89PNG\r\n\x1a\n";
  b += std::string("\0\0\0\x0d", 4);
  b += "IHDR";
  for (const unsigned int v : {w, h}) {
    b += static_cast<char>((v >> 24) & 0xFF);
    b += static_cast<char>((v >> 16) & 0xFF);
    b += static_cast<char>((v >> 8) & 0xFF);
    b += static_cast<char>(v & 0xFF);
  }
  b += std::string("\x08\x02\0\0\0", 5);
  return b;
}

// A cube puzzle with all a command can touch: PelikanBurr (a result, a
// cage and six pieces; one problem with its solution taken apart), two
// colors, a ninth shape no problem uses with two cells of the first color,
// and a copy of the problem (no solutions) whose first piece is in group 1.
std::string cubeFixture() {
  Document d = openFixture("PelikanBurr.xmpuzzle");
  const Id c1 = d.addColor(Rgb{200, 0, 0}).value();
  (void)d.addColor(Rgb{0, 0, 200}).value();
  const Id extra = d.createShape(std::array<unsigned int, 3>{2, 2, 2}).value();
  REQUIRE(d.editCells(extra, CellTool::Fixed, c1, 0, {{0, 0, 0}, {1, 0, 0}}).has_value());
  const Id p2 = d.duplicateProblem(d.problemIds()[0]).value();
  const unsigned int first = d.puzzle().getProblem(0).getShapeIdOfPart(0);
  REQUIRE(d.setGroup(p2, d.shapeIds()[first], 1).has_value());
  REQUIRE(d.puzzle().getProblem(0).getNumberOfSavedSolutions() == 1);
  return d.saveBytes().value();
}

// A problem with three solutions: the first three of SolidSixPieceBurrs.
std::string manySolutionsFixture() {
  Document d = openFixture("SolidSixPieceBurrs.xmpuzzle");
  const Id third = d.puzzle().getProblem(0).getSavedSolution(2).getId();
  REQUIRE(d.deleteSolutions(d.problemIds()[0], SolutionsToDelete::After, third).has_value());
  REQUIRE(d.puzzle().getProblem(0).getNumberOfSavedSolutions() == 3);
  return d.saveBytes().value();
}

// A sliding puzzle built through the commands: a board of 3 by 2 with its
// problem, a unit piece that starts at (0,0) and ends at (2,0), a domino
// that starts at (1,1), a unit piece without a start, and a picture on the
// domino; solved (one solution).
std::string slidingFixture() {
  Document d(GridKind::Sliding);
  const Id tray = SlidingEdits(d).addStartGoal({3, 2, 1}).value();
  const Id p = d.createProblem().value();
  REQUIRE(d.setResult(p, tray).has_value());
  const Id unit = piece(d, 1, 1, {{0, 0, 0}});
  const Id domino = piece(d, 2, 1, {{0, 0, 0}, {1, 0, 0}});
  (void)piece(d, 1, 1, {{0, 0, 0}});
  REQUIRE(SlidingEdits(d).placeStamp(tray, unit, 0, 0, 0, false).value());
  REQUIRE(SlidingEdits(d).placeStamp(tray, domino, 1, 1, 0, false).value());
  REQUIRE(SlidingEdits(d).placeStamp(tray, unit, 2, 0, 0, true).value());
  Document::ImageChoice picture;
  picture.bytes = pngHeader(8, 4);
  picture.src = {0, 0, 8, 4};
  REQUIRE(d.setShapeImage(domino, picture).has_value());
  SolveOptions o;
  o.disassemble = true;
  o.threads = 1;
  REQUIRE(d.solveStart(p, o).has_value());
  REQUIRE(d.solveWait(p).has_value());
  (void)d.poll();
  REQUIRE(d.puzzle().getProblem(0).getNumberOfSavedSolutions() == 1);
  return d.saveBytes().value();
}

// Two stacking problems on two rod sets (a tower of three discs and a
// Panex swap of six).
std::string stackingFixture() {
  return openFixture("stacking/stack_two_boards.xmpuzzle").saveBytes().value();
}

// Triple Play: a tilted tray, its board, six pieces, two solved problems.
std::string tiltedFixture() {
  return openFixture("tilted/TriplePlay.xmpuzzle").saveBytes().value();
}

Document fixture(On on) {
  static const std::map<On, std::string> bytes = {{On::Cube, cubeFixture()},
                                                  {On::ManySolutions, manySolutionsFixture()},
                                                  {On::Sliding, slidingFixture()},
                                                  {On::Stacking, stackingFixture()},
                                                  {On::Tilted, tiltedFixture()}};
  Result<Document> d = Document::openBytes(bytes.at(on));
  REQUIRE(d.has_value());
  return std::move(*d);
}

// ---- the commands -----------------------------------------------------------------

// The error of a command's answer; empty when it was done.
template <class T>
std::string refusal(const Result<T>& r) {
  return r ? std::string() : r.error().message;
}

struct Command {
  // the command, as document.h or a kind's header declares it (the
  // Document's method, or its SlidingEdits', TrayEdits' or StackingEdits')
  const char* method;
  // which call of it
  const char* call;
  On on;
  std::function<std::string(Document&)> run;
  // Done before the call, where the fixture lacks what the call needs;
  // not part of what is audited.
  std::function<void(Document&)> prepare;
  // Kinds whose text in the file changes though nothing of them does by
  // id (a place in a list that the file writes as a number), with the
  // reason; such a kind need not be told.
  std::uint32_t byPlaceOnly = 0;
  const char* why = "";
};

Id shapeAt(const Document& d, unsigned int i) { return d.shapeIds().at(i); }
Id problemAt(const Document& d, unsigned int i) { return d.problemIds().at(i); }
Id colorAt(const Document& d, unsigned int i) { return d.colorIds().at(i); }
Id rodSetAt(const Document& d, unsigned int i) { return d.rodSetIds().at(i); }
// the cube fixture: the first piece of the first problem, the ninth shape,
// a solution of the first problem
Id firstPiece(const Document& d) {
  return shapeAt(d, d.puzzle().getProblem(0).getShapeIdOfPart(0));
}
// (the first piece is the cage, which every turn leaves as it is)
Id burrPiece(const Document& d) {
  return shapeAt(d, d.puzzle().getProblem(0).getShapeIdOfPart(1));
}
Id spare(const Document& d) { return d.shapeIds().back(); }
Id solutionAt(const Document& d, unsigned int i) {
  return d.puzzle().getProblem(0).getSavedSolution(i).getId();
}
// a filled cell of a shape
CellEdit filledCell(const Document& d, Id shape) {
  const Voxel& v = d.puzzle().getShape(d.shapeIndex(shape).value());
  for (unsigned int z = 0; z < v.getZ(); z++) {
    for (unsigned int y = 0; y < v.getY(); y++) {
      for (unsigned int x = 0; x < v.getX(); x++) {
        if (v.getState(x, y, z) == Voxel::kFilled) {
          return CellEdit{static_cast<int>(x), static_cast<int>(y), static_cast<int>(z)};
        }
      }
    }
  }
  FAIL("the shape has no filled cell");
  return {};
}

std::vector<Command> commands() {
  std::vector<Command> c;
  const auto add = [&c](const char* method, const char* call, On on,
                        std::function<std::string(Document&)> run, std::uint32_t byPlaceOnly = 0,
                        const char* why = "") {
    c.push_back(Command{method, call, on, std::move(run), {}, byPlaceOnly, why});
  };

  // ---- notes
  add("setNotes", "another text", On::Cube,
      [](Document& d) { return refusal(d.setNotes("audit")); });

  // ---- shapes
  add("createShape", "of the default size", On::Cube,
      [](Document& d) { return refusal(d.createShape()); });
  add("createShape", "a disc", On::Stacking, [](Document& d) { return refusal(d.createShape()); });
  add("createShape", "on a tilted tray", On::Tilted,
      [](Document& d) { return refusal(d.createShape()); });
  add("duplicateShape", "a shape no problem uses", On::Cube,
      [](Document& d) { return refusal(d.duplicateShape(spare(d))); });
  add("duplicateShape", "a sliding board", On::Sliding,
      [](Document& d) { return refusal(d.duplicateShape(shapeAt(d, 0))); });
  add("deleteShape", "a piece of a solved problem", On::Cube,
      [](Document& d) { return refusal(d.deleteShape(firstPiece(d))); });
  add("deleteShape", "the last shape, which no problem uses", On::Cube,
      [](Document& d) { return refusal(d.deleteShape(spare(d))); });
  add("deleteShape", "a stamped sliding piece", On::Sliding,
      [](Document& d) { return refusal(d.deleteShape(shapeAt(d, 1))); });
  add("deleteShape", "a disc on a rod", On::Stacking,
      [](Document& d) { return refusal(d.deleteShape(shapeAt(d, 0))); });
  add("deleteShape", "a tilted puzzle's piece", On::Tilted,
      [](Document& d) { return refusal(d.deleteShape(shapeAt(d, 1))); });
  add("moveShape", "the last shape to the front", On::Cube,
      [](Document& d) { return refusal(d.moveShape(spare(d), 0)); });
  add("moveShape", "a sliding piece one place down", On::Sliding,
      [](Document& d) { return refusal(d.moveShape(shapeAt(d, 1), 2)); });
  add("renameShape", "a piece", On::Cube,
      [](Document& d) { return refusal(d.renameShape(firstPiece(d), "audit")); });
  add("renameShape", "a sliding board", On::Sliding,
      [](Document& d) { return refusal(d.renameShape(shapeAt(d, 0), "audit")); });
  add("setShapeWeight", "a piece", On::Cube,
      [](Document& d) { return refusal(d.setShapeWeight(firstPiece(d), 7)); });
  add("setShapeColor", "a piece", On::Cube,
      [](Document& d) { return refusal(d.setShapeColor(firstPiece(d), 0x112233)); });
  add("setShapeImage", "a new picture on a piece", On::Sliding, [](Document& d) {
    Document::ImageChoice picture;
    picture.bytes = pngHeader(16, 16);
    picture.src = {0, 0, 16, 16};
    return refusal(d.setShapeImage(shapeAt(d, 1), picture));
  });
  add("setShapeImages", "a stack of two on the board's border", On::Sliding, [](Document& d) {
    Document::ImageLayer one;
    one.bytes = pngHeader(16, 16);
    one.src = {0, 0, 16, 16};
    Document::ImageLayer two;
    two.id = static_cast<unsigned int>(d.puzzle().getShape(2).getImages().front().id);
    two.src = {0, 0, 8, 4};
    return refusal(d.setShapeImages(shapeAt(d, 0), {one, two}, true));
  });
  add("clearShapeImage", "the domino's picture", On::Sliding,
      [](Document& d) { return refusal(d.clearShapeImage(shapeAt(d, 2))); });
  add("setTrayBorder", "switched on, with a color", On::Sliding, [](Document& d) {
    Document::BorderChange change;
    change.enabled = true;
    change.color = std::optional<std::uint32_t>{0x334455};
    return refusal(d.setTrayBorder(shapeAt(d, 0), change));
  });
  add("editCells", "a shape no problem uses", On::Cube, [](Document& d) {
    return refusal(d.editCells(spare(d), CellTool::Fixed, 0, 0, {{1, 1, 1}}));
  });
  add("editCells", "a piece of a solved problem", On::Cube, [](Document& d) {
    const Id s = firstPiece(d);
    return refusal(d.editCells(s, CellTool::Erase, 0, 0, {filledCell(d, s)}));
  });
  add("editCells", "a cell of a sliding board", On::Sliding, [](Document& d) {
    return refusal(d.editCells(shapeAt(d, 0), CellTool::Erase, 0, 0, {{2, 1, 0}}));
  });
  add("editCells", "a stamped sliding piece", On::Sliding, [](Document& d) {
    return refusal(d.editCells(shapeAt(d, 2), CellTool::Erase, 0, 0, {{1, 0, 0}}));
  });
  add("editCells", "a tilted puzzle's piece", On::Tilted, [](Document& d) {
    const Id s = shapeAt(d, 1);
    return refusal(d.editCells(s, CellTool::Erase, 0, 0, {filledCell(d, s)}));
  });
  add("editBox", "a box of a shape no problem uses, painted", On::Cube, [](Document& d) {
    return refusal(d.editBox(spare(d), CellTool::Fixed, colorAt(d, 1), 0, CellBox{0, 0, 0, 1, 1, 0}));
  });
  add("resizeShape", "a shape no problem uses", On::Cube,
      [](Document& d) { return refusal(d.resizeShape(spare(d), 3, 3, 3, false)); });
  add("resizeShape", "every shape", On::Cube,
      [](Document& d) { return refusal(d.resizeShape(spare(d), 3, 3, 3, true)); });
  add("resizeShape", "a sliding board", On::Sliding,
      [](Document& d) { return refusal(d.resizeShape(shapeAt(d, 0), 2, 2, 1, false)); });
  add("shapeTask", "a shape no problem uses, turned", On::Cube,
      [](Document& d) { return refusal(d.shapeTask(spare(d), ShapeTask::Rotate6)); });
  add("shapeTask", "a piece of a solved problem, turned", On::Cube,
      [](Document& d) { return refusal(d.shapeTask(burrPiece(d), ShapeTask::Rotate6)); });
  add("shapeTask", "every shape, to the corner", On::Cube,
      [](Document& d) { return refusal(d.shapeTask(spare(d), ShapeTask::Double, true)); });

  // ---- colors
  add("addColor", "a third", On::Cube,
      [](Document& d) { return refusal(d.addColor(Rgb{1, 2, 3})); });
  add("editColor", "the first", On::Cube,
      [](Document& d) { return refusal(d.editColor(colorAt(d, 0), Rgb{1, 2, 3})); });
  add("deleteColor", "one a shape is painted with", On::Cube,
      [](Document& d) { return refusal(d.deleteColor(colorAt(d, 0))); });
  add("deleteColor", "the last, which no shape uses", On::Cube,
      [](Document& d) { return refusal(d.deleteColor(colorAt(d, 1))); });

  // ---- problems
  add("createProblem", "a new one", On::Cube,
      [](Document& d) { return refusal(d.createProblem()); });
  add("duplicateProblem", "the solved one", On::Cube,
      [](Document& d) { return refusal(d.duplicateProblem(problemAt(d, 0))); });
  add("deleteProblem", "the solved one", On::Cube,
      [](Document& d) { return refusal(d.deleteProblem(problemAt(d, 0))); });
  add("deleteProblem", "the last", On::Cube,
      [](Document& d) { return refusal(d.deleteProblem(problemAt(d, 1))); });
  add("moveProblem", "the first to the end", On::Cube,
      [](Document& d) { return refusal(d.moveProblem(problemAt(d, 0), 1)); });
  add("renameProblem", "the solved one", On::Cube,
      [](Document& d) { return refusal(d.renameProblem(problemAt(d, 0), "audit")); });
  add("setResult", "of the solved problem", On::Cube,
      [](Document& d) { return refusal(d.setResult(problemAt(d, 0), spare(d))); });
  add("setResult", "of a sliding problem: another board", On::Sliding,
      [](Document& d) { return refusal(d.setResult(problemAt(d, 0), shapeAt(d, 1))); });
  c.back().prepare = [](Document& d) {
    REQUIRE(SlidingEdits(d).addStartGoal({3, 2, 1}).has_value());
  };
  add("clearResult", "of the solved problem", On::Cube,
      [](Document& d) { return refusal(d.clearResult(problemAt(d, 0))); });
  add("setPieceCount", "a piece of the solved problem", On::Cube,
      [](Document& d) { return refusal(d.setPieceCount(problemAt(d, 0), firstPiece(d), 0, 2)); });
  add("setPieceCount", "a new piece of the unsolved problem", On::Cube,
      [](Document& d) { return refusal(d.setPieceCount(problemAt(d, 1), spare(d), 1, 1)); });
  add("setPieceCount", "a stamped sliding piece taken out", On::Sliding,
      [](Document& d) { return refusal(d.setPieceCount(problemAt(d, 0), shapeAt(d, 1), 0, 0)); });
  add("setPieceCount", "a sliding piece without a start added", On::Sliding,
      [](Document& d) { return refusal(d.setPieceCount(problemAt(d, 0), shapeAt(d, 3), 1, 1)); });
  add("setPieceCount", "a piece of a tilted problem", On::Tilted,
      [](Document& d) { return refusal(d.setPieceCount(problemAt(d, 0), shapeAt(d, 6), 1, 1)); });
  add("addPiece", "one more of a piece", On::Cube,
      [](Document& d) { return refusal(d.addPiece(problemAt(d, 0), firstPiece(d))); });
  add("removePiece", "a piece of the solved problem", On::Cube,
      [](Document& d) { return refusal(d.removePiece(problemAt(d, 0), firstPiece(d))); });
  add("setRangeAll", "of the solved problem", On::Cube,
      [](Document& d) { return refusal(d.setRangeAll(problemAt(d, 0), 0, 2)); });
  add("addOneOfEach", "to the solved problem", On::Cube,
      [](Document& d) { return refusal(d.addOneOfEach(problemAt(d, 0))); });
  add("removeAllPieces", "of the solved problem", On::Cube,
      [](Document& d) { return refusal(d.removeAllPieces(problemAt(d, 0))); });
  add("removeAllPieces", "of a sliding problem: the piece without a start goes", On::Sliding,
      [](Document& d) { return refusal(d.removeAllPieces(problemAt(d, 0))); });
  c.back().prepare = [](Document& d) {
    REQUIRE(d.setPieceCount(problemAt(d, 0), shapeAt(d, 3), 1, 1).has_value());
  };
  add("movePart", "the first piece down", On::Cube,
      [](Document& d) { return refusal(d.movePart(problemAt(d, 0), firstPiece(d), 1)); });
  add("setGroupCount", "a piece of the solved problem", On::Cube,
      [](Document& d) { return refusal(d.setGroupCount(problemAt(d, 0), firstPiece(d), 1, 1)); });
  add("setGroup", "a piece of the solved problem", On::Cube,
      [](Document& d) { return refusal(d.setGroup(problemAt(d, 0), firstPiece(d), 2)); });
  add("deleteGroup", "the copy's group", On::Cube,
      [](Document& d) { return refusal(d.deleteGroup(problemAt(d, 1), 1)); });
  add("setColorRule", "of the solved problem", On::Cube, [](Document& d) {
    return refusal(d.setColorRule(problemAt(d, 0), colorAt(d, 0), colorAt(d, 1), true));
  });
  add("setColorRules", "allow all, of the solved problem", On::Cube, [](Document& d) {
    return refusal(d.setColorRules(problemAt(d, 0), ColorPreset::AllowAll));
  });
  add("setSolverOptions", "of the solved problem", On::Cube, [](Document& d) {
    return refusal(d.setSolverOptions(problemAt(d, 0), {{"reduce", "0"}}));
  });

  // ---- solutions
  add("deleteSolutions", "one", On::Cube, [](Document& d) {
    return refusal(d.deleteSolutions(problemAt(d, 0), SolutionsToDelete::One, solutionAt(d, 0)));
  });
  add("deleteSolutions", "those before the second", On::ManySolutions, [](Document& d) {
    return refusal(
        d.deleteSolutions(problemAt(d, 0), SolutionsToDelete::Before, solutionAt(d, 1)));
  });
  add("deleteSolutions", "those after the first", On::ManySolutions, [](Document& d) {
    return refusal(d.deleteSolutions(problemAt(d, 0), SolutionsToDelete::After, solutionAt(d, 0)));
  });
  add("deleteSolutions", "all", On::Cube, [](Document& d) {
    return refusal(d.deleteSolutions(problemAt(d, 0), SolutionsToDelete::All));
  });
  add("deleteSolutions", "those not taken apart", On::Tilted,
      [](Document& d) {
        return refusal(d.deleteSolutions(problemAt(d, 0), SolutionsToDelete::WithoutDisassembly));
      });
  add("deleteDisassemblies", "of one solution", On::Cube, [](Document& d) {
    return refusal(d.deleteDisassemblies(problemAt(d, 0), solutionAt(d, 0)));
  });
  add("deleteDisassemblies", "of every solution", On::Cube, [](Document& d) {
    return refusal(d.deleteDisassemblies(problemAt(d, 0), std::nullopt));
  });
  add("assembliesToShapes", "as shapes alone", On::Cube, [](Document& d) {
    return refusal(d.assembliesToShapes(problemAt(d, 0), AssemblyImport{}));
  });
  add("assembliesToShapes", "as the pieces of a new problem", On::Cube, [](Document& d) {
    AssemblyImport o;
    o.addTo = AssemblyImport::AddTo::NewProblem;
    return refusal(d.assembliesToShapes(problemAt(d, 0), o));
  });
  add("assembliesToShapes", "as pieces of the solved problem itself", On::Cube, [](Document& d) {
    AssemblyImport o;
    o.addTo = AssemblyImport::AddTo::Problem;
    o.target = problemAt(d, 0);
    return refusal(d.assembliesToShapes(problemAt(d, 0), o));
  });
  add("solveReset", "a solved problem", On::Cube,
      [](Document& d) { return refusal(d.solveReset(problemAt(d, 0), "")); });

  // ---- sliding puzzles
  add("addStartGoal", "a second board", On::Sliding,
      [](Document& d) { return refusal(SlidingEdits(d).addStartGoal({2, 2, 1})); });
  add("placeStamp", "the domino's goal", On::Sliding, [](Document& d) {
    return refusal(SlidingEdits(d).placeStamp(shapeAt(d, 0), shapeAt(d, 2), 0, 1, 0, true));
  });
  add("placeStamp", "the start of the piece that had none", On::Sliding, [](Document& d) {
    return refusal(SlidingEdits(d).placeStamp(shapeAt(d, 0), shapeAt(d, 3), 2, 0, 0, false));
  });
  add("clearStampAt", "the unit piece's start", On::Sliding, [](Document& d) {
    return refusal(SlidingEdits(d).clearStampAt(shapeAt(d, 0), 0, 0, 0, false));
  });
  add("cloneStamps", "the starts as the goals", On::Sliding,
      [](Document& d) { return refusal(SlidingEdits(d).cloneStamps(shapeAt(d, 0), true)); });
  add("clearStamps", "the goals", On::Sliding,
      [](Document& d) { return refusal(SlidingEdits(d).clearStamps(shapeAt(d, 0), true)); });
  add("setSlideLayout", "another start", On::Sliding, [](Document& d) {
    return refusal(SlidingEdits(d).setSlideLayout(problemAt(d, 0), false, {{1, 0, 0}, {0, 1, 0}}));
  });

  // ---- tilted trays
  add("setTray", "another allowance", On::Tilted, [](Document& d) {
    tilted::Tray tray = *d.puzzle().tray();
    tray.allowance = 0.1;
    return refusal(TrayEdits(d).setTray(tray));
  });
  add("setTray", "a larger tray", On::Tilted, [](Document& d) {
    tilted::Tray tray = *d.puzzle().tray();
    tray.width = 7;
    tray.height = 7;
    tray.walls.clear();
    return refusal(TrayEdits(d).setTray(tray));
  });
  add("setTrayWalls", "a square walled off", On::Tilted,
      [](Document& d) { return refusal(TrayEdits(d).setTrayWalls({{2, 2}}, true)); });

  // ---- stacking puzzles
  add("addRodSet", "a third", On::Stacking,
      [](Document& d) { return refusal(StackingEdits(d).addRodSet()); });
  add("duplicateRodSet", "the first", On::Stacking,
      [](Document& d) { return refusal(StackingEdits(d).duplicateRodSet(rodSetAt(d, 0))); });
  add("deleteRodSet", "one a problem stands on", On::Stacking,
      [](Document& d) { return refusal(StackingEdits(d).deleteRodSet(rodSetAt(d, 1))); });
  add("renameRodSet", "the first", On::Stacking,
      [](Document& d) { return refusal(StackingEdits(d).renameRodSet(rodSetAt(d, 0), "audit")); });
  add(
      "moveRodSet", "the first to the end", On::Stacking,
      [](Document& d) { return refusal(StackingEdits(d).moveRodSet(rodSetAt(d, 0), 1)); },
      kChangeProblems,
      "a problem names its rod set by its place in the file; by id it stands on the one it did");
  add("setRodSetRules", "the size rule of one a problem stands on", On::Stacking,
      [](Document& d) {
        stacking::RodSet rules = d.puzzle().getRodSet(0);
        rules.sizeMatters = !rules.sizeMatters;
        return refusal(StackingEdits(d).setRodSetRules(rodSetAt(d, 0), rules));
      });
  add("setRodSetRules", "a fourth rod", On::Stacking, [](Document& d) {
    stacking::RodSet rules = d.puzzle().getRodSet(0);
    rules.rodCount = 4;
    return refusal(StackingEdits(d).setRodSetRules(rodSetAt(d, 0), rules));
  });
  add("setDiscSize", "a disc on a rod", On::Stacking,
      [](Document& d) { return refusal(StackingEdits(d).setDiscSize(shapeAt(d, 0), 5)); });
  add("setProblemRodSet", "the other rod set", On::Stacking, [](Document& d) {
    return refusal(StackingEdits(d).setProblemRodSet(problemAt(d, 0), rodSetAt(d, 1)));
  });
  add("placeDisc", "one more copy of a disc", On::Stacking, [](Document& d) {
    return refusal(StackingEdits(d).placeDisc(problemAt(d, 0), shapeAt(d, 0), 2, false));
  });
  add("placeAllDiscs", "on the third rod", On::Stacking, [](Document& d) {
    return refusal(StackingEdits(d).placeAllDiscs(problemAt(d, 0), 2, false));
  });
  add("removeDisc", "the bottom disc of the start", On::Stacking, [](Document& d) {
    return refusal(StackingEdits(d).removeDisc(problemAt(d, 0), false, 0, 0));
  });
  add("clearStacks", "the start", On::Stacking,
      [](Document& d) { return refusal(StackingEdits(d).clearStacks(problemAt(d, 0), false)); });
  add("moveDisc", "the bottom disc up", On::Stacking, [](Document& d) {
    return refusal(StackingEdits(d).moveDisc(problemAt(d, 0), false, 0, 0, 1));
  });
  add("setStacks", "another start", On::Stacking, [](Document& d) {
    return refusal(StackingEdits(d).setStacks(problemAt(d, 0), false, {{}, {}, {0, 1, 2}}));
  });
  return c;
}

// Where the commands are declared: document.h's Document, and the classes
// that hold each kind's commands (cleanup item 21.6).
struct CommandClass {
  const char* header;
  const char* name;
};
constexpr std::array<CommandClass, 4> kCommandClasses{{
    {"core/shapelogic/app/document.h", "Document"},
    {"core/shapelogic/app/sliding_edits.h", "SlidingEdits"},
    {"core/shapelogic/app/tray_edits.h", "TrayEdits"},
    {"core/shapelogic/app/stacking_edits.h", "StackingEdits"},
}};

// The commands one class declares: its public methods that are not static,
// not const, and answer with a Result.
std::set<std::string> declaredCommands(const CommandClass& cls) {
  std::ifstream in(cls.header);
  REQUIRE(in.good());
  std::stringstream all;
  all << in.rdbuf();
  std::string text = all.str();
  const std::size_t from = text.find(std::string("\nclass ") + cls.name + " {");
  const std::size_t to = text.find("\n private:", from);
  REQUIRE(from != std::string::npos);
  REQUIRE(to != std::string::npos);
  text = text.substr(from, to - from);
  // without the comments, one statement a line
  text = std::regex_replace(text, std::regex("//[^\n]*"), "");
  text = std::regex_replace(text, std::regex("\\s+"), " ");
  std::set<std::string> found;
  std::size_t at = 0;
  while (at < text.size()) {
    std::size_t end = text.find(';', at);
    if (end == std::string::npos) {
      end = text.size();
    }
    const std::string statement = text.substr(at, end - at);
    at = end + 1;
    const std::size_t result = statement.rfind("Result<");
    if (result == std::string::npos) {
      continue;
    }
    // (a struct's members before it end with their own statements; an
    // inline body is no declaration of a command either)
    const std::string decl = statement.substr(result);
    std::smatch m;
    if (!std::regex_search(decl, m, std::regex("> (\\w+)\\("))) {
      continue;
    }
    const bool isStatic = std::regex_search(statement.substr(0, result),
                                            std::regex("static (\\[\\[nodiscard\\]\\] )?$"));
    const bool isConst = std::regex_search(decl, std::regex("\\) const *$"));
    if (!isStatic && !isConst) {
      found.insert(m[1]);
    }
  }
  return found;
}

// The commands of every class of kCommandClasses, by name: the table names
// a command by its method alone, so no two classes may share a name.
std::set<std::string> declaredCommands() {
  std::set<std::string> found;
  for (const CommandClass& cls : kCommandClasses) {
    const std::set<std::string> mine = declaredCommands(cls);
    INFO(cls.name << " has commands of its own");
    CHECK_FALSE(mine.empty());
    for (const std::string& name : mine) {
      INFO(cls.name << "::" << name << " has the name of another class's command");
      CHECK(found.insert(name).second);
    }
  }
  return found;
}

}  // namespace

TEST_CASE("change audit: every command tells what it changed", "[app][audit]") {
  // Commands that are not audited by a call of the table, each with why.
  const std::map<std::string, std::string> leftOut = {
      {"save", "writes the file; the document's content stays (test_document.cpp)"},
      {"undo", "audited after every command below"},
      {"redo", "audited after every command below"},
      {"beginGroup", "opens a group; the commands inside tell their own changes"},
      {"endGroup", "a revision that names nothing, for the step (test_document.cpp)"},
      {"solveStart", "a run: audited in the case of the runs below"},
      {"solvePrepare", "a run that builds the matrix alone: launched as solveStart is"},
      {"solveResume", "a run: launched as solveStart is, which the case below audits"},
      {"solvePause", "asks a run to stop; the run's end tells (poll)"},
      {"solvePauseAtSavePoint", "asks a run to stop; the run's end tells (poll)"},
      {"solveAbort",
       "tells solutions and problems of its problem whatever it found: nothing worked out by "
       "hand (test_document_solving.cpp has its cases)"},
      {"solveWait", "waits; changes nothing"},
      {"analyseStart", "a job: audited in the case of the runs below"},
      {"analyseStop", "asks a job to stop; its end tells (poll)"},
      {"fitPlacementsStart", "a job on a puzzle of its own: the document's stays as it is"},
      {"fitPlacementsStop", "asks that job to stop"},
  };
  const std::vector<Command> table = commands();
  std::set<std::string> audited;
  for (const Command& c : table) {
    audited.insert(c.method);
  }
  // every command of the header is audited or left out with its reason,
  // and nothing here names a command the header does not have
  const std::set<std::string> declared = declaredCommands();
  // (the walk found them: 81 at the time of writing)
  CHECK(declared.size() > 60);
  for (const std::string& name : declared) {
    INFO(name << " is neither in the audit's table nor left out with a reason");
    CHECK((audited.contains(name) || leftOut.contains(name)));
  }
  for (const std::string& name : audited) {
    INFO("the audit's table names " << name << ", which no header of the commands declares");
    CHECK(declared.contains(name));
  }
  for (const auto& [name, why] : leftOut) {
    INFO("left out: " << name << ", which no header of the commands declares");
    CHECK(declared.contains(name));
    CHECK_FALSE(audited.contains(name));
  }

  std::vector<std::string> overstated;
  for (const Command& c : table) {
    INFO(c.method << ": " << c.call);
    Document d = fixture(c.on);
    std::size_t steps = 0;
    if (c.prepare) {
      c.prepare(d);
      steps = d.history().undo.size();
    }
    const Held before = hold(d);
    const std::uint64_t revision = d.revision();
    const std::string refused = c.run(d);
    INFO("refused: " << refused);
    CHECK(refused.empty());
    if (!refused.empty()) {
      continue;
    }
    const Held after = hold(d);
    const ChangeSet told = d.lastChange();
    const Differs changed = differs(before, after);

    // a typical call changes something (else the table's call is wrong),
    // and nothing no kind stands for
    CHECK_FALSE(same(before, after));
    CHECK_FALSE(changed.rest);
    CHECK(d.revision() > revision);

    // every kind that changed is told
    const std::uint32_t mustTell = changed.what & ~c.byPlaceOnly;
    {
      INFO("changed " << kinds(changed.what) << ", told " << kinds(told.what) << ", not told "
                      << kinds(mustTell & ~told.what));
      CHECK((mustTell & ~told.what) == 0);
    }
    if ((told.what & ~changed.what) != 0) {
      overstated.push_back(std::string(c.method) + " (" + c.call + "): told " +
                           kinds(told.what & ~changed.what) + ", which did not change");
    }

    // an id list that is not empty names all that changed
    if (!told.shapes.empty()) {
      CHECK((told.what & kChangeShapes) != 0);
      CHECK_FALSE(changed.shapeListChanged);
      for (const Id id : changed.shapes) {
        INFO("shape " << id << " changed and is not named");
        CHECK(std::ranges::find(told.shapes, id) != told.shapes.end());
      }
    }
    if (!told.problems.empty()) {
      CHECK((told.what & (kChangeProblems | kChangeSolutions)) != 0);
      CHECK_FALSE(changed.problemListChanged);
      if ((c.byPlaceOnly & (kChangeProblems | kChangeSolutions)) == 0) {
        for (const Id id : changed.problems) {
          INFO("problem " << id << " changed and is not named");
          CHECK(std::ranges::find(told.problems, id) != told.problems.end());
        }
      }
    }

    // undo gives every byte back and tells as much; so does redo
    CHECK(d.history().undo.size() == steps + 1);
    if (d.history().undo.size() != steps + 1) {
      continue;
    }
    REQUIRE(d.undo().has_value());
    {
      INFO("undo: changed " << kinds(changed.what) << ", told " << kinds(d.lastChange().what));
      CHECK(same(hold(d), before));
      CHECK((mustTell & ~d.lastChange().what) == 0);
    }
    REQUIRE(d.redo().has_value());
    {
      INFO("redo: changed " << kinds(changed.what) << ", told " << kinds(d.lastChange().what));
      CHECK(same(hold(d), after));
      CHECK((mustTell & ~d.lastChange().what) == 0);
    }
  }

  // What was told and did not change: a refresh more than needed, nothing
  // stale. Listed, not failed (cleanup item 19.2).
  if (!overstated.empty()) {
    std::string list = "told but unchanged (" + std::to_string(overstated.size()) + "):";
    for (const std::string& line : overstated) {
      list += "\n  " + line;
    }
    WARN(list);
  }
}

// A command that changes nothing leaves no undo step, no revision and no
// report, and the solutions stay. (The audit above found five that left a
// step, three of them after throwing the problem's solutions away: Remove
// all pieces with every piece stamped, a range and color rules that were
// set already; and the solutions "not taken apart" of a list that all
// were, and those before the first.)
TEST_CASE("change audit: a call that changes nothing leaves nothing behind", "[app][audit]") {
  struct Idle {
    const char* call;
    On on;
    std::function<std::string(Document&)> run;
    // done first, where the fixture is not yet as the call would leave it
    std::function<void(Document&)> prepare;
  };
  std::vector<Idle> calls;
  const auto idle = [&calls](const char* call, On on, std::function<std::string(Document&)> run,
                             std::function<void(Document&)> prepare = {}) {
    calls.push_back(Idle{call, on, std::move(run), std::move(prepare)});
  };
  idle("setNotes: the text it has", On::Cube,
       [](Document& d) { return refusal(d.setNotes(d.puzzle().getComment())); });
  idle("renameProblem: the name it has", On::Cube,
       [](Document& d) {
         return refusal(d.renameProblem(problemAt(d, 0), d.puzzle().getProblem(0).getName()));
       });
  idle("setShapeWeight: the weight it has", On::Cube,
       [](Document& d) {
         const Id s = firstPiece(d);
         return refusal(d.setShapeWeight(
             s, d.puzzle().getShape(d.shapeIndex(s).value()).getWeight()));
       });
  idle("editColor: the color it has", On::Cube,
       [](Document& d) { return refusal(d.editColor(colorAt(d, 0), Rgb{200, 0, 0})); });
  idle("moveShape: to its own place", On::Cube,
       [](Document& d) { return refusal(d.moveShape(shapeAt(d, 2), 2)); });
  idle("moveProblem: to its own place", On::Cube,
       [](Document& d) { return refusal(d.moveProblem(problemAt(d, 0), 0)); });
  idle("setPieceCount: the counts it has", On::Cube,
       [](Document& d) { return refusal(d.setPieceCount(problemAt(d, 0), firstPiece(d), 1, 1)); });
  idle("setRangeAll: the range every piece has", On::Cube,
       [](Document& d) { return refusal(d.setRangeAll(problemAt(d, 0), 1, 1)); });
  idle("setColorRules: the rules it has", On::Cube,
       [](Document& d) {
         return refusal(d.setColorRules(problemAt(d, 0), ColorPreset::EachOnItself));
       });
  idle("setColorRule: a rule it has", On::Cube,
       [](Document& d) {
         return refusal(d.setColorRule(problemAt(d, 0), colorAt(d, 0), colorAt(d, 0), true));
       });
  idle("setSolverOptions: the options it has", On::Cube,
       [](Document& d) {
         return refusal(
             d.setSolverOptions(problemAt(d, 0), d.puzzle().getProblem(0).getSolverOptions()));
       });
  idle("deleteGroup: a group no piece is in", On::Cube,
       [](Document& d) { return refusal(d.deleteGroup(problemAt(d, 0), 9)); });
  idle("deleteSolutions: those not taken apart, of solutions that all are", On::Cube,
       [](Document& d) {
         return refusal(
             d.deleteSolutions(problemAt(d, 0), SolutionsToDelete::WithoutDisassembly));
       });
  idle("deleteSolutions: those before the first", On::ManySolutions,
       [](Document& d) {
         return refusal(
             d.deleteSolutions(problemAt(d, 0), SolutionsToDelete::Before, solutionAt(d, 0)));
       });
  idle("deleteDisassemblies: of solutions that have none", On::Tilted,
       [](Document& d) { return refusal(d.deleteDisassemblies(problemAt(d, 0), std::nullopt)); });
  idle("removeAllPieces: of a sliding problem whose pieces all have a start", On::Sliding,
       [](Document& d) { return refusal(d.removeAllPieces(problemAt(d, 0))); });
  idle(
      "clearStamps: of a board without them", On::Sliding,
      [](Document& d) { return refusal(SlidingEdits(d).clearStamps(shapeAt(d, 0), true)); },
      [](Document& d) { REQUIRE(SlidingEdits(d).clearStamps(shapeAt(d, 0), true).value()); });
  idle("setSlideLayout: the start it has", On::Sliding, [](Document& d) {
    return refusal(SlidingEdits(d).setSlideLayout(problemAt(d, 0), false, {{0, 0, 0}, {1, 1, 0}}));
  });
  idle("setStacks: the start it has", On::Stacking, [](Document& d) {
    return refusal(StackingEdits(d).setStacks(problemAt(d, 0), false, {{0, 1, 2}, {}, {}}));
  });
  idle("setTray: the tray it has", On::Tilted,
       [](Document& d) { return refusal(TrayEdits(d).setTray(*d.puzzle().tray())); });
  idle("setProblemRodSet: the rod set it stands on", On::Stacking, [](Document& d) {
    return refusal(StackingEdits(d).setProblemRodSet(problemAt(d, 0), rodSetAt(d, 0)));
  });
  idle("setRodSetRules: the rules it has", On::Stacking, [](Document& d) {
    return refusal(StackingEdits(d).setRodSetRules(rodSetAt(d, 0), d.puzzle().getRodSet(0)));
  });
  idle("setDiscSize: the size it has", On::Stacking,
       [](Document& d) { return refusal(StackingEdits(d).setDiscSize(shapeAt(d, 0), 3)); });
  for (const Idle& c : calls) {
    INFO(c.call);
    Document d = fixture(c.on);
    if (c.prepare) {
      c.prepare(d);
    }
    const Held before = hold(d);
    const std::uint64_t revision = d.revision();
    const std::size_t steps = d.history().undo.size();
    const std::string refused = c.run(d);
    INFO("refused: " << refused);
    CHECK(refused.empty());
    const Held after = hold(d);
    CHECK(same(before, after));
    // (the solutions' lists as they were, not given back)
    CHECK(differs(before, after).what == 0);
    CHECK(d.revision() == revision);
    CHECK(d.history().undo.size() == steps);
  }
}

// A run and an analysis change the document on their own threads and tell
// of it twice: as they start, and when poll() sees their end. Together the
// two reports cover what changed, and undo gives every byte back.
TEST_CASE("change audit: a run and an analysis tell what they changed", "[app][audit][parallel]") {
  struct Job {
    const char* what;
    On on;
    std::function<void(Document&)> prepare;
    std::function<std::string(Document&)> start;
  };
  const std::vector<Job> jobs = {
      {"solveStart: a problem solved again from nothing", On::Tilted,
       [](Document& d) { REQUIRE(d.solveReset(problemAt(d, 0), "").has_value()); },
       [](Document& d) { return refusal(d.solveStart(problemAt(d, 0), SolveOptions{})); }},
      {"solveStart: a sliding problem solved again from nothing", On::Sliding,
       [](Document& d) { REQUIRE(d.solveReset(problemAt(d, 0), "").has_value()); },
       [](Document& d) {
         SolveOptions o;
         o.disassemble = true;
         o.threads = 1;
         return refusal(d.solveStart(problemAt(d, 0), o));
       }},
      {"analyseStart: every solution taken apart", On::ManySolutions, [](Document&) {},
       [](Document& d) {
         AnalyseOptions o;
         o.which = AnalyseWhich::All;
         return refusal(d.analyseStart(problemAt(d, 0), o));
       }},
  };
  for (const Job& job : jobs) {
    INFO(job.what);
    Document d = fixture(job.on);
    job.prepare(d);
    const std::size_t steps = d.history().undo.size();
    const Held before = hold(d);
    const std::string refused = job.start(d);
    INFO("refused: " << refused);
    REQUIRE(refused.empty());
    std::uint32_t told = d.lastChange().what;
    std::vector<Id> named = d.lastChange().problems;
    // the end, which poll() tells (a job stays the document's until a
    // poll() has put its results in)
    const auto tell = [&] {
      while (d.poll()) {
        told |= d.lastChange().what;
        named.insert(named.end(), d.lastChange().problems.begin(),
                     d.lastChange().problems.end());
      }
    };
    for (int tries = 0; tries < 6000 && d.solving(); tries++) {
      tell();
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    tell();
    REQUIRE_FALSE(d.solving());
    const Held after = hold(d);
    const Differs changed = differs(before, after);
    CHECK_FALSE(same(before, after));
    {
      INFO("changed " << kinds(changed.what) << ", told " << kinds(told));
      CHECK((changed.what & ~told) == 0);
    }
    for (const Id id : changed.problems) {
      CHECK(std::ranges::find(named, id) != named.end());
    }
    CHECK(d.history().undo.size() == steps + 1);
    REQUIRE(d.undo().has_value());
    {
      INFO("undo: changed " << kinds(changed.what) << ", told " << kinds(d.lastChange().what));
      CHECK(same(hold(d), before));
      CHECK((changed.what & ~d.lastChange().what) == 0);
    }
    REQUIRE(d.redo().has_value());
    CHECK(same(hold(d), after));
    CHECK((changed.what & ~d.lastChange().what) == 0);
  }
}

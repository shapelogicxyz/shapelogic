// SPDX-License-Identifier: GPL-3.0-or-later
// A document with a tilted tray (decision 0045, stage B of
// docs/TILTED-SQUARE-PLAN.md).

#include "shapelogic/app/document.h"
#include "shapelogic/base/solve_state.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/tilted_tray.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace bt;
using namespace bt::app;

namespace {

tilted::Tray trayOf(unsigned int size, unsigned int over, unsigned int down,
                    double allowance = 0.0) {
  tilted::Tray t;
  t.width = size;
  t.height = size;
  t.over = over;
  t.down = down;
  t.allowance = allowance;
  return t;
}

/// Triple Play's tray: 6 by 6, the corner squares (5, 0) and (0, 5) walls,
/// 1 down 3 over, an allowance of 0.05.
tilted::Tray triplePlayTray() {
  tilted::Tray t = trayOf(6, 3, 1, 0.05);
  t.walls.assign(36, 0);
  t.walls[5] = 1;
  t.walls[30] = 1;
  return t;
}

/// The cells of a 4 by 4 picture, rows from y = 0.
std::vector<std::array<int, 2>> cellsOf(const std::string& art) {
  std::vector<std::array<int, 2>> out;
  for (int k = 0; k < 16; k++) {
    if (art[static_cast<std::size_t>(k)] == '#') {
      out.push_back({k % 4, k / 4});
    }
  }
  return out;
}

/// A new piece with the picture's cells, drawn at the first place of the
/// board where they are all whole cells.
Id drawPiece(Document& d, const std::string& art) {
  const std::vector<std::array<int, 2>> cells = cellsOf(art);
  const tilted::Layout lay = tilted::layout(*d.puzzle().tray());
  const Id id = d.createShape().value();
  for (int oy = -3; oy < static_cast<int>(lay.sizeY); oy++) {
    for (int ox = -3; ox < static_cast<int>(lay.sizeX); ox++) {
      bool ok = true;
      std::vector<CellEdit> edit;
      for (const std::array<int, 2>& c : cells) {
        const int x = ox + c[0];
        const int y = oy + c[1];
        ok = ok && x >= 0 && y >= 0 && x < static_cast<int>(lay.sizeX) &&
             y < static_cast<int>(lay.sizeY) &&
             lay.at(static_cast<unsigned int>(x), static_cast<unsigned int>(y)) ==
                 tilted::Cell::Whole;
        edit.push_back({x, y, 0});
      }
      if (ok) {
        REQUIRE(d.editCells(id, CellTool::Fixed, 0, 0, edit).value() == cells.size());
        return id;
      }
    }
  }
  FAIL("the piece fits nowhere on the board");
  return id;
}

unsigned int filled(const Voxel& v) { return v.countState(Voxel::kFilled); }

}  // namespace

TEST_CASE("tilted document: a new one has its board and a problem", "[app][document][tilted]") {
  Document d = Document::newTilted(trayOf(8, 2, 1)).value();
  REQUIRE(d.tilted());
  const Puzzle& p = d.puzzle();
  CHECK(p.getGridType().getType() == GridKind::Bricks);
  // the tray has its best shift
  const tilted::Placed best = tilted::bestShift(trayOf(8, 2, 1));
  CHECK(best.whole == 45);
  CHECK(p.tray()->shiftX == best.shiftX);
  CHECK(p.tray()->shiftY == best.shiftY);
  // the board: the box of the tray, one layer, the whole cells variable
  REQUIRE(p.getNumberOfShapes() == 1);
  const tilted::Layout lay = tilted::layout(*p.tray());
  const Voxel& board = p.getShape(0);
  CHECK(board.getX() == lay.sizeX);
  CHECK(board.getY() == lay.sizeY);
  CHECK(board.getZ() == 1);
  CHECK(board.countState(Voxel::kVariable) == 45);
  CHECK(board.countState(Voxel::kFilled) == 0);
  CHECK(d.shapeLabelOf(0) == "B");
  // one problem, its result the board
  REQUIRE(p.getNumberOfProblems() == 1);
  CHECK(p.getProblem(0).resultValid());
  CHECK(p.getProblem(0).getResultId() == 0);
  CHECK_FALSE(d.dirty());
  CHECK_FALSE(d.canUndo());

  // a tray out of range is refused
  CHECK(Document::newTilted(trayOf(0, 1, 0)).error().code == Errc::InvalidArgument);
  CHECK(Document::newTilted(trayOf(8, 4, 2)).error().code == Errc::InvalidArgument);

  // a custom shift is kept, in its canonical form
  tilted::Tray custom = trayOf(8, 2, 1);
  custom.placement = tilted::Tray::Placement::Custom;
  custom.shiftX = 5.3;
  custom.shiftY = -2.1;
  const Document c = Document::newTilted(custom).value();
  const tilted::Point canon = tilted::canonicalShift(custom, {5.3, -2.1});
  CHECK(c.puzzle().tray()->shiftX == canon.x);
  CHECK(c.puzzle().tray()->shiftY == canon.y);

  // a plain puzzle is not tilted, and its tray cannot be set
  Document plain(GridKind::Bricks);
  CHECK_FALSE(plain.tilted());
  CHECK(plain.setTray(trayOf(4, 1, 0)).error().code == Errc::Unsupported);
  CHECK(plain.setTrayWalls({{0, 0}}, true).error().code == Errc::Unsupported);
}

TEST_CASE("tilted document: Triple Play is solved", "[app][document][tilted]") {
  // "Triple Play (IPP34)" by Goh Pit Khiam and Jerry Loo: three pieces of
  // seven cells that fit their tray only turned, 1 down 3 over
  Document d = Document::newTilted(triplePlayTray()).value();
  CHECK(d.puzzle().getShape(0).countState(Voxel::kVariable) == 24);
  const Id a = drawPiece(d, "_#___###_#__##__");
  const Id b = drawPiece(d, "_#___#___#####__");
  const Id c = drawPiece(d, "_#___#####___#__");
  CHECK(d.shapeLabelOf(1) == "S1");
  CHECK(d.shapeLabelOf(3) == "S3");
  const Id problem = d.problemIds()[0];
  for (const Id piece : {a, b, c}) {
    REQUIRE(d.setPieceCount(problem, piece, 1, 1).has_value());
  }
  REQUIRE(d.solveStart(problem, SolveOptions{}).has_value());
  REQUIRE(d.solveWait(problem).has_value());
  // 21 cells of pieces on a board of 24: three stay empty. The two ways
  // (one turned half round) are one assembly, the board being the same
  // turned half round.
  CHECK(d.puzzle().getProblem(0).getNumAssemblies() == 1);

  // strict, the pieces have nowhere to go: the tray change refuses nothing
  // (they fit the box) but they no longer fit the board, and no assembly
  // is found
  tilted::Tray strict = triplePlayTray();
  strict.allowance = 0.0;
  REQUIRE(d.setTray(strict).has_value());
  CHECK(d.puzzle().getShape(0).countState(Voxel::kVariable) == 22);
  // the solutions went with the old board
  CHECK(d.puzzle().getProblem(0).getSolveState() == ProblemState::Unsolved);
  REQUIRE(d.solveStart(problem, SolveOptions{}).has_value());
  REQUIRE(d.solveWait(problem).has_value());
  CHECK(d.puzzle().getProblem(0).getNumAssemblies() == 0);
}

TEST_CASE("tilted document: pieces that are not turned over", "[app][document][tilted]") {
  const auto assemblies = [](const std::vector<std::string>& pieces, bool flip) {
    Document d = Document::newTilted(triplePlayTray()).value();
    const Id problem = d.problemIds()[0];
    for (const std::string& art : pieces) {
      REQUIRE(d.setPieceCount(problem, drawPiece(d, art), 1, 1).has_value());
    }
    SolveOptions o;
    o.flipPieces = flip;
    REQUIRE(d.solveStart(problem, o).has_value());
    REQUIRE(d.solveWait(problem).has_value());
    return d.puzzle().getProblem(0).getNumAssemblies();
  };
  // Triple Play's pieces as its file draws them, and their mirror images
  // (each row read backwards)
  const std::vector<std::string> drawn = {"_#___###_#__##__", "_#___#___#####__",
                                          "_#___#####___#__"};
  const std::vector<std::string> mirrored = {"__#_###___#___##", "__#___#_###___##",
                                             "__#_###___##__#_"};
  // turned over, any mix of the two sets is the same puzzle
  CHECK(assemblies(drawn, true) == 1);
  CHECK(assemblies(mirrored, true) == 1);
  // not turned over, the one way the pieces go needs each a certain side
  // up: of the eight mixes exactly one has it
  unsigned int mixesSolved = 0;
  std::uint64_t found = 0;
  for (unsigned int mix = 0; mix < 8; mix++) {
    std::vector<std::string> pieces;
    for (unsigned int k = 0; k < 3; k++) {
      pieces.push_back(((mix >> k) & 1U) != 0 ? mirrored[k] : drawn[k]);
    }
    const std::uint64_t n = assemblies(pieces, false);
    INFO("mix " << mix << ": " << n);
    mixesSolved += n > 0 ? 1 : 0;
    found += n;
  }
  CHECK(mixesSolved == 1);
  // the way and the same turned half round are counted each: with the
  // pieces' turns limited the search has no symmetry breaker
  CHECK(found == 2);
}

TEST_CASE("tilted document: a tray change is one undo step", "[app][document][tilted]") {
  Document d = Document::newTilted(trayOf(8, 2, 1)).value();
  const Id piece = drawPiece(d, "##___#___#___#__");  // an L of five
  const Id other = drawPiece(d, "####____________");
  const std::string before = d.saveBytes().value();
  const tilted::Tray old = *d.puzzle().tray();
  CHECK(d.fitsTray(1));

  REQUIRE(d.setTray(trayOf(8, 3, 1, 0.05)).has_value());
  const Puzzle& p = d.puzzle();
  CHECK(p.tray()->over == 3);
  CHECK(p.tray()->allowance == 0.05);
  const tilted::Layout lay = tilted::layout(*p.tray());
  CHECK(p.getShape(0).countState(Voxel::kVariable) == lay.whole);
  // the pieces have the new size, their cells, and lie on whole cells
  for (unsigned int i = 1; i <= 2; i++) {
    CHECK(p.getShape(i).getX() == lay.sizeX);
    CHECK(p.getShape(i).getY() == lay.sizeY);
    CHECK(d.fitsTray(i));
  }
  CHECK(filled(p.getShape(1)) == 5);
  CHECK(filled(p.getShape(2)) == 4);
  CHECK(d.lastChange().what & kChangeTray);
  CHECK(d.lastChange().shapes.size() == 3);

  // the same settings again: nothing happens
  const std::uint64_t rev = d.revision();
  REQUIRE(d.setTray(trayOf(8, 3, 1, 0.05)).has_value());
  CHECK(d.revision() == rev);

  // one step back: the tray, the board and the pieces as they were
  REQUIRE(d.undo().has_value());
  CHECK(*d.puzzle().tray() == old);
  CHECK(d.saveBytes().value() == before);
  REQUIRE(d.redo().has_value());
  CHECK(d.puzzle().tray()->over == 3);
  REQUIRE(d.undo().has_value());

  // a smaller tray: a piece too large for the new board refuses, and
  // nothing changes
  const Result<void> r = d.setTray(trayOf(2, 2, 1));
  REQUIRE_FALSE(r.has_value());
  CHECK(r.error().code == Errc::InvalidArgument);
  CHECK(r.error().message.find("S") != std::string::npos);
  CHECK(d.saveBytes().value() == before);
  (void)piece;
  (void)other;

  // out of range
  CHECK(d.setTray(trayOf(8, 4, 2)).error().code == Errc::InvalidArgument);
}

TEST_CASE("tilted document: walls belong to the tray", "[app][document][tilted]") {
  Document d = Document::newTilted(trayOf(8, 1, 1)).value();
  CHECK(d.puzzle().getShape(0).countState(Voxel::kVariable) == 50);

  CHECK(d.setTrayWalls({{0, 0}, {7, 0}, {0, 7}, {7, 7}}, true).value() == 4);
  CHECK(d.puzzle().getShape(0).countState(Voxel::kVariable) == 44);
  CHECK(d.puzzle().tray()->isWall(7, 7));
  // again: nothing changes, no step
  const std::uint64_t rev = d.revision();
  CHECK(d.setTrayWalls({{0, 0}}, true).value() == 0);
  CHECK(d.revision() == rev);
  // outside the tray
  CHECK(d.setTrayWalls({{8, 0}}, true).error().code == Errc::InvalidArgument);

  // another tilt: the walls are where they were
  REQUIRE(d.setTray([&] {
             tilted::Tray t = *d.puzzle().tray();
             t.over = 3;
             return t;
           }())
              .has_value());
  CHECK(d.puzzle().tray()->walls.size() == 64);
  CHECK(d.puzzle().tray()->isWall(0, 0));
  CHECK(d.puzzle().tray()->isWall(7, 7));
  CHECK(d.puzzle().getShape(0).countState(Voxel::kVariable) == 45);

  // a smaller tray keeps the walls still inside it
  REQUIRE(d.setTray(tilted::withSize(*d.puzzle().tray(), 5, 5)).has_value());
  CHECK(d.puzzle().tray()->isWall(0, 0));
  CHECK_FALSE(d.puzzle().tray()->isWall(4, 4));

  // opened again one by one, the last leaves no walls
  CHECK(d.setTrayWalls({{0, 0}}, false).value() == 1);
  CHECK(d.puzzle().tray()->walls.empty());
  REQUIRE(d.undo().has_value());
  CHECK(d.puzzle().tray()->isWall(0, 0));
}

TEST_CASE("tilted document: the board and the sizes are the tray's", "[app][document][tilted]") {
  Document d = Document::newTilted(trayOf(8, 2, 1)).value();
  const Id board = d.shapeIds()[0];
  const Id piece = drawPiece(d, "###__#__________");
  const Id problem = d.problemIds()[0];
  const std::string before = d.saveBytes().value();
  const auto refused = [&](const Result<void>& r) {
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == Errc::InvalidArgument);
    CHECK(d.saveBytes().value() == before);
  };
  const auto drop = [](auto r) { return r ? Result<void>{} : std::unexpected(r.error()); };

  refused(d.deleteShape(board));
  refused(drop(d.duplicateShape(board)));
  refused(d.moveShape(board, 1));
  refused(d.moveShape(piece, 0));
  refused(d.resizeShape(board, 4, 4, 1, false));
  refused(d.resizeShape(piece, 4, 4, 1, false));
  refused(drop(d.editCells(board, CellTool::Fixed, 0, 0, {{3, 3, 0}})));
  refused(drop(d.editBox(board, CellTool::Erase, 0, 0, CellBox{0, 0, 0, 3, 3, 0})));
  refused(drop(d.shapeTask(board, ShapeTask::Nudge0)));
  refused(drop(d.shapeTask(piece, ShapeTask::Minimize)));
  refused(d.setResult(problem, piece));
  refused(d.clearResult(problem));
  refused(d.setPieceCount(problem, board, 1, 1));
  refused(drop(d.createShape(std::array<unsigned int, 3>{4, 4, 1})));

  // a new shape has the board's size; a new problem the board as result
  const Id made = d.createShape().value();
  const Voxel& v = d.puzzle().getShape(d.shapeIndex(made).value());
  CHECK(v.getX() == d.puzzle().getShape(0).getX());
  CHECK(v.getY() == d.puzzle().getShape(0).getY());
  CHECK(v.getZ() == 1);
  const Id second = d.createProblem().value();
  const Problem& pr = d.puzzle().getProblem(d.problemIndex(second).value());
  CHECK(pr.resultValid());
  CHECK(pr.getResultId() == 0);

  // the board may be renamed, a piece copied and deleted
  CHECK(d.renameShape(board, "Tray").has_value());
  const Id copy = d.duplicateShape(piece).value();
  CHECK(d.deleteShape(copy).has_value());
}

TEST_CASE("tilted document: a piece's cells stay on whole cells", "[app][document][tilted]") {
  Document d = Document::newTilted(trayOf(8, 2, 1)).value();
  const tilted::Layout lay = tilted::layout(*d.puzzle().tray());
  const Id piece = d.createShape().value();
  // every cell of the box painted: only the whole cells take
  std::vector<CellEdit> all;
  for (unsigned int y = 0; y < lay.sizeY; y++) {
    for (unsigned int x = 0; x < lay.sizeX; x++) {
      all.push_back({static_cast<int>(x), static_cast<int>(y), 0});
    }
  }
  CHECK(d.editCells(piece, CellTool::Fixed, 0, 0, all).value() == 45);
  CHECK(filled(d.puzzle().getShape(1)) == 45);
  CHECK(d.fitsTray(1));
  // only cut cells: nothing changes, no step
  std::vector<CellEdit> cut;
  for (unsigned int y = 0; y < lay.sizeY; y++) {
    for (unsigned int x = 0; x < lay.sizeX; x++) {
      if (lay.at(x, y) != tilted::Cell::Whole) {
        cut.push_back({static_cast<int>(x), static_cast<int>(y), 0});
      }
    }
  }
  const std::uint64_t rev = d.revision();
  CHECK(d.editCells(piece, CellTool::Fixed, 0, 0, cut).value() == 0);
  CHECK(d.revision() == rev);
  CHECK(d.editCells(piece, CellTool::Erase, 0, 0, all).value() == 45);

  // the tools: a small piece is nudged and turned, and stays on the board
  const Id small = drawPiece(d, "###__#__________");
  const unsigned int si = d.shapeIndex(small).value();
  for (const ShapeTask task : {ShapeTask::Nudge0, ShapeTask::Nudge2, ShapeTask::Rotate10,
                               ShapeTask::Flip12, ShapeTask::Nudge1, ShapeTask::Nudge3}) {
    const Result<TaskOutcome> o = d.shapeTask(small, task, true);
    if (o) {
      CHECK(filled(d.puzzle().getShape(si)) == 4);
      CHECK(d.fitsTray(si));
      CHECK(d.puzzle().getShape(si).getX() == lay.sizeX);
      CHECK(d.puzzle().getShape(si).getY() == lay.sizeY);
    } else {
      CHECK(o.error().code == Errc::InvalidArgument);
    }
  }
  // the preview is what the task does
  CHECK_FALSE(d.shapeTaskPreview(d.shapeIds()[0], ShapeTask::Nudge0).has_value());
  CHECK(d.shapeTaskPreview(small, ShapeTask::Rotate10).has_value());
  // "to all shapes" leaves the board alone
  CHECK(d.puzzle().getShape(0).countState(Voxel::kVariable) == 45);

  // a piece the whole board wide cannot be doubled
  REQUIRE(d.editCells(piece, CellTool::Fixed, 0, 0, all).value() == 45);
  CHECK(d.shapeTask(piece, ShapeTask::Double).error().code == Errc::InvalidArgument);
  CHECK(filled(d.puzzle().getShape(1)) == 45);
}

TEST_CASE("tilted document: saved and opened again", "[app][document][tilted]") {
  Document d = Document::newTilted(triplePlayTray()).value();
  const Id a = drawPiece(d, "_#___###_#__##__");
  (void)a;
  const std::string bytes = d.saveBytes().value();
  Document again = Document::openBytes(bytes).value();
  REQUIRE(again.tilted());
  CHECK(*again.puzzle().tray() == *d.puzzle().tray());
  CHECK(again.puzzle().getShape(0) == d.puzzle().getShape(0));
  CHECK(again.puzzle().getShape(1) == d.puzzle().getShape(1));
  CHECK(again.openNotice().empty());
  CHECK(again.saveBytes().value() == bytes);

  // a file whose board is not the tray's (edited by hand): made again
  const std::string tray =
      "<tray size=\"4\" over=\"1\" down=\"0\" allowance=\"0\" placement=\"best\" shiftX=\"0\" "
      "shiftY=\"0\"/>";
  const std::string wrongBoard =
      "<?xml version=\"1.0\"?>\n<puzzle version=\"2\"><gridType type=\"0\"/>" + tray +
      "<colors/><shapes><voxel x=\"2\" y=\"2\" z=\"1\" type=\"0\">####</voxel>"
      "<voxel x=\"2\" y=\"1\" z=\"1\" type=\"0\">##</voxel></shapes><problems/><comment/></puzzle>";
  Document fixed = Document::openBytes(wrongBoard).value();
  REQUIRE(fixed.tilted());
  CHECK(fixed.puzzle().getShape(0).getX() == 4);
  CHECK(fixed.puzzle().getShape(0).countState(Voxel::kVariable) == 16);
  // the piece has the board's size and its two cells
  CHECK(fixed.puzzle().getShape(1).getX() == 4);
  CHECK(fixed.puzzle().getShape(1).getY() == 4);
  CHECK(filled(fixed.puzzle().getShape(1)) == 2);
  CHECK(fixed.fitsTray(1));

  // no shapes at all: the board is added
  const std::string noShapes =
      "<?xml version=\"1.0\"?>\n<puzzle version=\"2\"><gridType type=\"0\"/>" + tray +
      "<colors/><shapes/><problems/><comment/></puzzle>";
  Document bare = Document::openBytes(noShapes).value();
  REQUIRE(bare.tilted());
  REQUIRE(bare.puzzle().getNumberOfShapes() == 1);
  CHECK(bare.shapeIds().size() == 1);

  // a shape that cannot lie on the board (two layers): opened as a brick
  // puzzle, with a notice
  const std::string thick =
      "<?xml version=\"1.0\"?>\n<puzzle version=\"2\"><gridType type=\"0\"/>" + tray +
      "<colors/><shapes><voxel x=\"1\" y=\"1\" z=\"1\" type=\"0\">#</voxel>"
      "<voxel x=\"1\" y=\"1\" z=\"2\" type=\"0\">##</voxel></shapes><problems/><comment/></puzzle>";
  Document brick = Document::openBytes(thick).value();
  CHECK_FALSE(brick.tilted());
  CHECK_FALSE(brick.openNotice().empty());
  CHECK(brick.puzzle().getShape(1).getZ() == 2);
}

// ---- rectangular trays (stage E) ---------------------------------------------------

namespace {

/// A tray `width` by `height` at slope down/over.
tilted::Tray rectOf(unsigned int width, unsigned int height, unsigned int over, unsigned int down,
                    double allowance = 0.0) {
  tilted::Tray t = trayOf(width, over, down, allowance);
  t.height = height;
  return t;
}

/// The assemblies of Triple Play's three pieces on the board of `d`.
std::uint64_t triplePlayOn(Document& d, bool flip = true) {
  const Id problem = d.problemIds()[0];
  for (const char* art : {"_#___###_#__##__", "_#___#___#####__", "_#___#####___#__"}) {
    REQUIRE(d.setPieceCount(problem, drawPiece(d, art), 1, 1).has_value());
  }
  SolveOptions o;
  o.flipPieces = flip;
  REQUIRE(d.solveStart(problem, o).has_value());
  REQUIRE(d.solveWait(problem).has_value());
  return d.puzzle().getProblem(0).getNumAssemblies();
}

}  // namespace

TEST_CASE("tilted document: a rectangular tray", "[app][document][tilted]") {
  // 6 wide and 8 high, 1 down 2 over
  const tilted::Tray rect = rectOf(6, 8, 2, 1);
  Document d = Document::newTilted(rect).value();
  REQUIRE(d.tilted());
  CHECK(d.puzzle().tray()->width == 6);
  CHECK(d.puzzle().tray()->height == 8);
  const tilted::Placed best = tilted::bestShift(rect);
  CHECK(d.puzzle().tray()->shiftX == best.shiftX);
  CHECK(d.puzzle().tray()->shiftY == best.shiftY);
  const tilted::Layout lay = tilted::layout(*d.puzzle().tray());
  CHECK(d.puzzle().getShape(0).getX() == lay.sizeX);
  CHECK(d.puzzle().getShape(0).getY() == lay.sizeY);
  CHECK(d.puzzle().getShape(0).countState(Voxel::kVariable) == best.whole);

  // the 8 by 8 tray with its last two columns walled has as many cells
  Document square = Document::newTilted(trayOf(8, 2, 1)).value();
  std::vector<std::array<unsigned int, 2>> columns;
  for (unsigned int y = 0; y < 8; y++) {
    columns.push_back({6, y});
    columns.push_back({7, y});
  }
  REQUIRE(square.setTrayWalls(columns, true).value() == 16);
  CHECK(square.puzzle().getShape(0).countState(Voxel::kVariable) == best.whole);

  // walls: the squares of the rectangle, and no others
  CHECK(d.setTrayWalls({{5, 7}, {0, 3}}, true).value() == 2);
  CHECK(d.puzzle().tray()->walls.size() == 48);
  CHECK(d.puzzle().tray()->isWall(5, 7));
  CHECK(d.puzzle().tray()->isWall(0, 3));
  CHECK_FALSE(d.puzzle().tray()->isWall(3, 0));
  CHECK(d.setTrayWalls({{6, 0}}, true).error().code == Errc::InvalidArgument);
  CHECK(d.setTrayWalls({{0, 8}}, true).error().code == Errc::InvalidArgument);
  CHECK(d.setTrayWalls({{7, 5}}, true).error().code == Errc::InvalidArgument);
  CHECK(tilted::layout(*d.puzzle().tray()).walls.size() == 2);

  // wider: the walls stay where they are; lower: the one in the last row goes
  REQUIRE(d.setTray(tilted::withSize(*d.puzzle().tray(), 9, 8)).has_value());
  CHECK(d.puzzle().tray()->width == 9);
  CHECK(d.puzzle().tray()->walls.size() == 72);
  CHECK(d.puzzle().tray()->isWall(5, 7));
  CHECK(d.puzzle().tray()->isWall(0, 3));
  REQUIRE(d.setTray(tilted::withSize(*d.puzzle().tray(), 9, 5)).has_value());
  CHECK(d.puzzle().tray()->isWall(0, 3));
  CHECK(std::count(d.puzzle().tray()->walls.begin(), d.puzzle().tray()->walls.end(), 1) == 1);
  CHECK(d.puzzle().getShape(0).countState(Voxel::kVariable) ==
        tilted::bestShift(*d.puzzle().tray()).whole);
  // each a step back
  REQUIRE(d.undo().has_value());
  CHECK(d.puzzle().tray()->height == 8);
  CHECK(d.puzzle().tray()->isWall(5, 7));
  REQUIRE(d.undo().has_value());
  CHECK(d.puzzle().tray()->width == 6);
  CHECK(d.puzzle().tray()->walls.size() == 48);
  REQUIRE(d.undo().has_value());
  CHECK(d.puzzle().tray()->walls.empty());
  CHECK(*d.puzzle().tray() == [&] {
    tilted::Tray t = rect;
    t.shiftX = best.shiftX;
    t.shiftY = best.shiftY;
    return t;
  }());
}

TEST_CASE("tilted document: the pieces follow a tray made a rectangle",
          "[app][document][tilted]") {
  Document d = Document::newTilted(trayOf(8, 2, 1)).value();
  const Id a = drawPiece(d, "_#___###_#__##__");
  const Id b = drawPiece(d, "##__##__________");
  // lower and wider, then narrow: every piece on the new board, its cells kept
  for (const auto& [width, height] :
       std::vector<std::pair<unsigned int, unsigned int>>{{10, 5}, {5, 9}, {11, 6}}) {
    INFO(width << " by " << height);
    REQUIRE(d.setTray(tilted::withSize(*d.puzzle().tray(), width, height)).has_value());
    const tilted::Layout lay = tilted::layout(*d.puzzle().tray());
    for (const Id id : {a, b}) {
      const unsigned int index = d.shapeIndex(id).value();
      const Voxel& v = d.puzzle().getShape(index);
      CHECK(v.getX() == lay.sizeX);
      CHECK(v.getY() == lay.sizeY);
      CHECK(filled(v) == (id == a ? 7U : 4U));
      CHECK(d.fitsTray(index));
    }
  }
  // a tray too low for the larger piece: it is kept and said not to fit;
  // the smaller one lies on whole cells
  REQUIRE(d.setTray(tilted::withSize(*d.puzzle().tray(), 12, 3)).has_value());
  CHECK(filled(d.puzzle().getShape(d.shapeIndex(a).value())) == 7);
  CHECK_FALSE(d.fitsTray(d.shapeIndex(a).value()));
  CHECK(filled(d.puzzle().getShape(d.shapeIndex(b).value())) == 4);
  CHECK(d.fitsTray(d.shapeIndex(b).value()));
  // a tray whose board is smaller than a piece refuses, and names the piece
  const Result<void> r = d.setTray(tilted::withSize(*d.puzzle().tray(), 1, 1));
  REQUIRE_FALSE(r.has_value());
  CHECK(r.error().code == Errc::InvalidArgument);
  CHECK(r.error().message.find("S1") != std::string::npos);
  CHECK(d.puzzle().tray()->width == 12);
  CHECK(d.puzzle().tray()->height == 3);
}

TEST_CASE("tilted document: a rectangular tray is solved", "[app][document][tilted]") {
  // Triple Play's tray as a rectangle: 6 by 8 with the two rows above the
  // 6 by 6 opening walled, and its two corners. The same board, the same
  // one assembly.
  tilted::Tray high = rectOf(6, 8, 3, 1, 0.05);
  high.walls.assign(48, 0);
  high.walls[5] = 1;
  high.walls[30] = 1;
  for (unsigned int k = 36; k < 48; k++) {
    high.walls[k] = 1;
  }
  Document walledRect = Document::newTilted(high).value();
  CHECK(walledRect.puzzle().getShape(0).countState(Voxel::kVariable) == 24);
  CHECK(triplePlayOn(walledRect) == 1);

  // The three pieces in an open 6 by 8 tray at 1 down 2 over, and in the 8
  // by 8 tray with two columns walled: the same board, so as many
  // assemblies, with the pieces turned over and not.
  for (const bool flip : {true, false}) {
    INFO("flip " << flip);
    Document rect = Document::newTilted(rectOf(6, 8, 2, 1)).value();
    Document square = Document::newTilted(trayOf(8, 2, 1)).value();
    std::vector<std::array<unsigned int, 2>> columns;
    for (unsigned int y = 0; y < 8; y++) {
      columns.push_back({6, y});
      columns.push_back({7, y});
    }
    REQUIRE(square.setTrayWalls(columns, true).value() == 16);
    REQUIRE(rect.puzzle().getShape(0).countState(Voxel::kVariable) ==
            square.puzzle().getShape(0).countState(Voxel::kVariable));
    const std::uint64_t inRect = triplePlayOn(rect, flip);
    const std::uint64_t inSquare = triplePlayOn(square, flip);
    CHECK(inRect > 0);
    CHECK(inRect == inSquare);
  }
}

TEST_CASE("tilted document: a rectangular tray is saved and opened",
          "[app][document][tilted]") {
  tilted::Tray rect = rectOf(7, 4, 3, 1, 0.05);
  rect.walls.assign(28, 0);
  rect.walls[6] = 1;
  rect.walls[21] = 1;
  Document d = Document::newTilted(rect).value();
  (void)drawPiece(d, "##__#___________");
  const std::string bytes = d.saveBytes().value();
  CHECK(bytes.find("<tray width=\"7\" height=\"4\"") != std::string::npos);
  Document again = Document::openBytes(bytes).value();
  REQUIRE(again.tilted());
  CHECK(*again.puzzle().tray() == *d.puzzle().tray());
  CHECK(again.puzzle().tray()->isWall(6, 0));
  CHECK(again.puzzle().tray()->isWall(0, 3));
  CHECK(again.puzzle().getShape(0) == d.puzzle().getShape(0));
  CHECK(again.puzzle().getShape(1) == d.puzzle().getShape(1));
  CHECK(again.openNotice().empty());
  CHECK(again.saveBytes().value() == bytes);
}

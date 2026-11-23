// SPDX-License-Identifier: GPL-3.0-or-later
// New code (decision 0045, stage B of docs/TILTED-SQUARE-PLAN.md): what a
// document does with a tilted tray. The geometry is model/tilted_tray.h.

#include "shapelogic/app/document.h"
#include "shapelogic/app/document_internal.h"
#include "shapelogic/base/assert.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/tilted_tray.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace bt::app {

namespace {

/// The tray with the shift it will use: the best one, or the custom one in
/// its canonical form.
tilted::Tray resolved(tilted::Tray tray) {
  if (tray.placement == tilted::Tray::Placement::Best) {
    const tilted::Placed best = tilted::bestShift(tray);
    tray.shiftX = best.shiftX;
    tray.shiftY = best.shiftY;
  } else {
    const tilted::Point at = tilted::canonicalShift(tray, {tray.shiftX, tray.shiftY});
    tray.shiftX = at.x;
    tray.shiftY = at.y;
  }
  return tray;
}

/// A point of a layout's box in the tray's own frame, and back.
tilted::Point boxToTray(const tilted::Tray& tray, const tilted::Layout& lay, tilted::Point b) {
  const double h = std::hypot(static_cast<double>(tray.over), static_cast<double>(tray.down));
  const double c = static_cast<double>(tray.over) / h;
  const double s = static_cast<double>(tray.down) / h;
  const double gx = b.x - lay.corners[0].x;
  const double gy = b.y - lay.corners[0].y;
  return {(gx * c) - (gy * s), (gx * s) + (gy * c)};
}
tilted::Point trayToBox(const tilted::Tray& tray, const tilted::Layout& lay, tilted::Point p) {
  const double h = std::hypot(static_cast<double>(tray.over), static_cast<double>(tray.down));
  const double c = static_cast<double>(tray.over) / h;
  const double s = static_cast<double>(tray.down) / h;
  return {(p.x * c) + (p.y * s) + lay.corners[0].x, (p.y * c) - (p.x * s) + lay.corners[0].y};
}

/// The tray of a puzzle that has one.
const tilted::Tray& trayOf(const Puzzle& puzzle) {
  const std::optional<tilted::Tray>& tray = puzzle.tray();
  BT_ASSERT(tray.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access): asserted on the line above
  return *tray;
}

/// True when shape `v` is the board `lay` gives.
bool isBoardOf(const Voxel& v, const tilted::Layout& lay) {
  if (v.getX() != lay.sizeX || v.getY() != lay.sizeY || v.getZ() != 1) {
    return false;
  }
  for (unsigned int i = 0; i < v.getXYZ(); i++) {
    const VoxelType want =
        lay.cells[i] == tilted::Cell::Whole ? Voxel::kVariable : Voxel::kEmpty;
    if (v.get(i) != want) {
      return false;
    }
  }
  return true;
}

}  // namespace

void fillTrayBoard(Voxel& board, const tilted::Layout& lay) {
  board.resize(lay.sizeX, lay.sizeY, 1, Voxel::kEmpty);
  for (unsigned int i = 0; i < board.getXYZ(); i++) {
    board.set(i, lay.cells[i] == tilted::Cell::Whole ? Voxel::kVariable : Voxel::kEmpty);
  }
  board.setHotspot(0, 0, 0);
}

bool onTray(const Voxel& v, const tilted::Layout& lay) {
  if (v.getX() != lay.sizeX || v.getY() != lay.sizeY || v.getZ() != 1) {
    return false;
  }
  for (unsigned int i = 0; i < v.getXYZ(); i++) {
    if (!v.isEmpty(i) && lay.cells[i] != tilted::Cell::Whole) {
      return false;
    }
  }
  return true;
}

unsigned int keepOnTray(Voxel& v, const Voxel& before, const tilted::Layout& lay) {
  BT_ASSERT(v.getXYZ() == before.getXYZ() && v.getXYZ() == lay.cells.size());
  unsigned int changed = 0;
  for (unsigned int i = 0; i < v.getXYZ(); i++) {
    if (lay.cells[i] != tilted::Cell::Whole) {
      // off the board an edit changes nothing: what was there stays
      v.set(i, before.get(i));
    }
    changed += v.get(i) != before.get(i) ? 1 : 0;
  }
  return changed;
}

std::optional<tilted::Point> cellsMiddle(const Voxel& v) {
  double sx = 0.0;
  double sy = 0.0;
  unsigned int n = 0;
  for (unsigned int y = 0; y < v.getY(); y++) {
    for (unsigned int x = 0; x < v.getX(); x++) {
      if (!v.isEmpty(x, y, 0)) {
        sx += static_cast<double>(x) + 0.5;
        sy += static_cast<double>(y) + 0.5;
        n++;
      }
    }
  }
  if (n == 0) {
    return std::nullopt;
  }
  return tilted::Point{sx / n, sy / n};
}

std::optional<TrayFit> fitToTray(const Voxel& piece, std::optional<tilted::Point> want,
                                 const tilted::Layout& lay) {
  struct At {
    unsigned int x;
    unsigned int y;
    VoxelType value;
  };
  std::vector<At> cells;
  unsigned int x0 = piece.getX();
  unsigned int y0 = piece.getY();
  unsigned int x1 = 0;
  unsigned int y1 = 0;
  for (unsigned int z = 0; z < piece.getZ(); z++) {
    for (unsigned int y = 0; y < piece.getY(); y++) {
      for (unsigned int x = 0; x < piece.getX(); x++) {
        if (piece.isEmpty(x, y, z)) {
          continue;
        }
        if (z > 0) {
          return std::nullopt;  // it does not lie flat
        }
        cells.push_back({x, y, piece.get(x, y, z)});
        x0 = std::min(x0, x);
        y0 = std::min(y0, y);
        x1 = std::max(x1, x);
        y1 = std::max(y1, y);
      }
    }
  }
  TrayFit out;
  out.shape = piece.clone();
  out.shape->resize(lay.sizeX, lay.sizeY, 1, Voxel::kEmpty);
  out.shape->setAll(Voxel::kEmpty);
  out.shape->setHotspot(0, 0, 0);
  out.fits = true;
  if (cells.empty()) {
    return out;
  }
  const unsigned int w = x1 - x0 + 1;
  const unsigned int h = y1 - y0 + 1;
  if (w > lay.sizeX || h > lay.sizeY) {
    return std::nullopt;
  }
  // the middle of the cells, from the low corner of their box
  double mx = 0.0;
  double my = 0.0;
  for (const At& c : cells) {
    mx += static_cast<double>(c.x - x0) + 0.5;
    my += static_cast<double>(c.y - y0) + 0.5;
  }
  mx /= static_cast<double>(cells.size());
  my /= static_cast<double>(cells.size());
  const tilted::Point to = want ? *want
                                : tilted::Point{static_cast<double>(lay.sizeX) / 2.0,
                                                static_cast<double>(lay.sizeY) / 2.0};

  // the place nearest `to` where every cell is whole; with none, the
  // nearest place
  bool bestWhole = false;
  double bestDist = std::numeric_limits<double>::infinity();
  unsigned int bestX = 0;
  unsigned int bestY = 0;
  for (unsigned int oy = 0; oy + h <= lay.sizeY; oy++) {
    for (unsigned int ox = 0; ox + w <= lay.sizeX; ox++) {
      const bool whole = std::all_of(cells.begin(), cells.end(), [&](const At& c) {
        return lay.at(ox + c.x - x0, oy + c.y - y0) == tilted::Cell::Whole;
      });
      const double dx = static_cast<double>(ox) + mx - to.x;
      const double dy = static_cast<double>(oy) + my - to.y;
      const double dist = (dx * dx) + (dy * dy);
      if ((whole && !bestWhole) || (whole == bestWhole && dist < bestDist - 1e-9)) {
        bestWhole = whole;
        bestDist = dist;
        bestX = ox;
        bestY = oy;
      }
    }
  }
  for (const At& c : cells) {
    out.shape->set(bestX + c.x - x0, bestY + c.y - y0, 0, c.value);
  }
  out.fits = bestWhole;
  return out;
}

// ---- the document -------------------------------------------------------------------

Result<Document> Document::newTilted(tilted::Tray tray, DocumentSettings settings) {
  if (!tray.valid()) {
    return fail(Errc::InvalidArgument, "the tray's settings are out of range");
  }
  tray = resolved(std::move(tray));
  auto puzzle = std::make_unique<Puzzle>(GridType(GridKind::Bricks));
  const tilted::Layout lay = tilted::layout(tray);
  puzzle->setTray(std::move(tray));
  const unsigned int board = puzzle->addShape(lay.sizeX, lay.sizeY, 1);
  fillTrayBoard(puzzle->getShape(board), lay);
  const unsigned int problem = puzzle->addProblem();
  puzzle->getProblem(problem).setResultId(*puzzle, board);
  return Document(std::move(puzzle), settings);
}

const tilted::Layout& Document::trayLayout() const {
  BT_ASSERT(tilted());
  if (!trayLayout_ || trayLayout_->first != trayOf(*puzzle_)) {
    trayLayout_ = std::make_unique<std::pair<tilted::Tray, tilted::Layout>>(
        trayOf(*puzzle_), tilted::layout(trayOf(*puzzle_)));
  }
  return trayLayout_->second;
}

bool Document::fitsTray(unsigned int index) const {
  if (!tilted() || index == 0) {
    return true;
  }
  return onTray(puzzle_->getShape(index), trayLayout());
}

Result<std::unique_ptr<Voxel>> Document::onTrayAfterTask(unsigned int shape, const Voxel& after,
                                                         ShapeTask task) const {
  const Voxel& before = puzzle_->getShape(shape);
  const int code = static_cast<int>(task);
  const bool nudge = code <= 5 || (code >= 27 && code <= 32);
  std::optional<TrayFit> fit = fitToTray(after, cellsMiddle(nudge ? after : before), trayLayout());
  if (!fit || !fit->fits) {
    return fail(Errc::InvalidArgument, "the piece does not fit the board that way");
  }
  return std::move(fit->shape);
}

Result<void> Document::refuseTiltedBoard(unsigned int shape, const char* why) const {
  if (tilted() && shape == 0) {
    return fail(Errc::InvalidArgument, why);
  }
  return {};
}

void Document::adoptTray() {
  const tilted::Tray& tray = trayOf(*puzzle_);
  const tilted::Layout lay = tilted::layout(tray);
  // every piece on the board's grid, before anything is changed
  std::vector<std::unique_ptr<Voxel>> fitted(puzzle_->getNumberOfShapes());
  for (unsigned int i = 1; i < puzzle_->getNumberOfShapes(); i++) {
    const Voxel& v = puzzle_->getShape(i);
    if (v.getX() == lay.sizeX && v.getY() == lay.sizeY && v.getZ() == 1) {
      continue;
    }
    std::optional<TrayFit> fit = fitToTray(v, cellsMiddle(v), lay);
    if (!fit) {
      puzzle_->setTray(std::nullopt);
      openNotice_ = "A shape of this puzzle does not fit its tilted tray: it was opened as a "
                    "Brick puzzle, without the tray.";
      return;
    }
    fitted[i] = std::move(fit->shape);
  }
  if (puzzle_->getNumberOfShapes() == 0) {
    (void)puzzle_->addShape(lay.sizeX, lay.sizeY, 1);
  }
  if (!isBoardOf(puzzle_->getShape(0), lay)) {
    fillTrayBoard(puzzle_->getShape(0), lay);
  }
  for (unsigned int i = 1; i < puzzle_->getNumberOfShapes(); i++) {
    if (fitted[i]) {
      (void)puzzle_->replaceShape(i, std::move(fitted[i]));
    }
  }
}

Result<void> Document::setTray(tilted::Tray tray) {
  if (!tilted()) {
    return fail(Errc::Unsupported, "the puzzle has no tilted tray");
  }
  if (Result<void> r = checkNotSolving(); !r) {
    return r;
  }
  if (!tray.valid()) {
    return fail(Errc::InvalidArgument, "the tray's settings are out of range");
  }
  return applyTray(resolved(std::move(tray)), "Tilt & tolerance");
}

Result<unsigned int> Document::setTrayWalls(
    const std::vector<std::array<unsigned int, 2>>& squares, bool wall) {
  if (!tilted()) {
    return fail(Errc::Unsupported, "the puzzle has no tilted tray");
  }
  if (Result<void> r = checkNotSolving(); !r) {
    return std::unexpected(r.error());
  }
  tilted::Tray next = trayOf(*puzzle_);
  const std::size_t all = static_cast<std::size_t>(next.size) * next.size;
  if (next.walls.empty()) {
    next.walls.assign(all, 0);
  }
  unsigned int changed = 0;
  for (const std::array<unsigned int, 2>& sq : squares) {
    if (sq[0] >= next.size || sq[1] >= next.size) {
      return fail(Errc::InvalidArgument, "a square is outside the tray");
    }
    std::uint8_t& at = next.walls[(static_cast<std::size_t>(sq[1]) * next.size) + sq[0]];
    const std::uint8_t to = wall ? 1 : 0;
    changed += at != to ? 1 : 0;
    at = to;
  }
  if (std::find(next.walls.begin(), next.walls.end(), 1) == next.walls.end()) {
    next.walls.clear();
  }
  if (changed == 0) {
    return 0U;
  }
  if (Result<void> r = applyTray(resolved(std::move(next)), "Walls"); !r) {
    return std::unexpected(r.error());
  }
  return changed;
}

Result<void> Document::applyTray(tilted::Tray next, const char* label) {
  const tilted::Tray old = trayOf(*puzzle_);
  if (next == old) {
    return {};
  }
  const tilted::Layout layOld = trayLayout();
  const tilted::Layout layNew = tilted::layout(next);

  // every piece moved to the new board, near where it lay in the tray,
  // before anything is changed: a piece too large for it refuses
  const unsigned int count = puzzle_->getNumberOfShapes();
  std::vector<std::unique_ptr<Voxel>> pieces(count);
  for (unsigned int i = 1; i < count; i++) {
    const Voxel& v = puzzle_->getShape(i);
    std::optional<tilted::Point> want = cellsMiddle(v);
    if (want && v.getX() == layOld.sizeX && v.getY() == layOld.sizeY) {
      want = trayToBox(next, layNew, boxToTray(old, layOld, *want));
    }
    std::optional<TrayFit> fit = fitToTray(v, want, layNew);
    if (!fit) {
      return fail(Errc::InvalidArgument,
                  shapeLabelOf(i) + " is too large for the new board: make it smaller or "
                                    "delete it first");
    }
    pieces[i] = std::move(fit->shape);
  }

  StepGuard g(*this, label);
  touchWhole();
  puzzle_->setTray(std::move(next));
  if (count == 0) {
    (void)puzzle_->addShape(layNew.sizeX, layNew.sizeY, 1);
    ids_.shapes.push_back(newId());
  }
  fillTrayBoard(puzzle_->getShape(0), layNew);
  for (unsigned int i = 1; i < count; i++) {
    (void)puzzle_->replaceShape(i, std::move(pieces[i]));
  }
  for (unsigned int i = 0; i < puzzle_->getNumberOfShapes(); i++) {
    shapeChanged(i);
  }
  lastChange_.shapes = ids_.shapes;
  lastChange_.problems = ids_.problems;
  g.commit(kChangeShapes | kChangeProblems | kChangeTray | lastChange_.what);
  return {};
}

}  // namespace bt::app

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A Tilted Square puzzle's board and the pieces on it (internal to
// document*.cpp and placement_fit.cpp; defined in document_tilted.cpp):
// apart from the undo steps of document_internal.h, so the background fit
// needs only these.

#include "shapelogic/grid/voxel.h"
#include "shapelogic/model/tilted_tray.h"

#include <memory>
#include <optional>

namespace sl::app {

/// Makes `board` the board of `lay`: the layout's size, one layer, its
/// whole cells variable and the rest empty.
void fillTrayBoard(Voxel& board, const tilted::Layout& lay);
/// True when `v` has the board's size and every cell of it lies on a whole
/// cell.
[[nodiscard]] bool onTray(const Voxel& v, const tilted::Layout& lay);
/// Puts back the cells of `v` that are not whole cells of the board as they
/// are in `before` (an edit changes nothing off the board); returns how
/// many cells of `v` then differ from `before`. Both have the board's size.
unsigned int keepOnTray(Voxel& v, const Voxel& before, const tilted::Layout& lay);
/// The middle of the cells of a shape's first layer, in its own grid;
/// nothing for an empty one.
[[nodiscard]] std::optional<tilted::Point> cellsMiddle(const Voxel& v);
/// A piece on the board's grid, and whether all its cells are whole cells.
struct TrayFit {
  std::unique_ptr<Voxel> shape;
  bool fits = true;
};
/// `piece` given the board's size and moved by whole cells so that the
/// middle of its cells is as near `want` (in the board's grid; its middle
/// when nothing) as it can be with every cell on a whole cell; with no
/// such place, as near as it can be. Its name, color and weight are kept.
/// Nothing when it does not lie in one layer or is larger than the board.
[[nodiscard]] std::optional<TrayFit> fitToTray(const Voxel& piece,
                                               std::optional<tilted::Point> want,
                                               const tilted::Layout& lay);

}  // namespace sl::app

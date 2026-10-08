// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// What the shape editor does to a voxel space: the buttons of the tool
/// tabs (transform, nudge, fit, scale, surface, fill holes) per grid, the
/// cell tools of the grid editor (fixed, variable, erase, paint, with
/// mirror and span) and resizing.
/// Ported from src/gui/tooltabs.cpp (ToolTab_0 to ToolTab_4: applyTask,
/// cb_transform, resizeSpace, the button lists of TransformButtons,
/// SizeButtons and ToolsButtons), src/gui/grideditor.cpp (setRecursive,
/// setLayer) and src/gui/guigridtype.cpp (defaultSize).

#include "bt/base/error.h"
#include "bt/grid/grid_type.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace bt {
class Voxel;
}

namespace bt::app {

/// A button of the tool tabs. The numbers are the original's task codes;
/// what a nudge or rotation does depends on the grid (taskSet() tells).
enum class ShapeTask : std::uint8_t {
  Nudge0 = 0,
  Nudge1 = 1,
  Nudge2 = 2,
  Nudge3 = 3,
  Nudge4 = 4,
  Nudge5 = 5,
  Rotate6 = 6,
  Rotate7 = 7,
  Rotate8 = 8,
  Rotate9 = 9,
  Rotate10 = 10,
  Rotate11 = 11,
  Flip12 = 12,
  Flip13 = 13,
  Flip14 = 14,
  /// Shrinks the grid to the shape (minimizePiece; "Prune" in the web UI).
  Prune = 15,
  InsideFixed = 16,
  OutsideFixed = 17,
  InsideVariable = 18,
  OutsideVariable = 19,
  InsideDecolour = 20,
  OutsideDecolour = 21,
  Double = 22,
  Triple = 23,
  /// Moves the shape to the corner of the grid.
  ToOrigin = 24,
  Centre = 25,
  /// Scales the shape down by the largest factor that keeps it.
  Minimize = 26,
  Nudge27 = 27,
  Nudge28 = 28,
  Nudge29 = 29,
  Nudge30 = 30,
  Nudge31 = 31,
  Nudge32 = 32,
  FillHoles = 40,
  /// Scales up keeping only the surface and the edges of each old cell.
  GridScale = 41,
};

/// How a task is shown: the original's button group and tooltip.
struct ShapeTaskInfo {
  ShapeTask task;
  /// "Flip", "Nudge", "Rotate", "Grid", "Shape", "Constrain" or "Tools".
  std::string_view group;
  std::string_view label;
  /// "Apply to all shapes" also applies it to every shape (the original's
  /// rule: the grid and scale buttons).
  bool toAll;
};

/// The tasks a grid offers, in the order of the original's buttons.
/// Buttons that did nothing on a grid, or only said "not implemented",
/// are left out.
[[nodiscard]] std::vector<ShapeTaskInfo> taskSet(GridKind grid);

/// Whether `task` is in the grid's set.
[[nodiscard]] bool taskOffered(GridKind grid, ShapeTask task);

/// What a task did to a shape.
struct TaskOutcome {
  /// Cells whose state or colour changed.
  unsigned int cellsChanged = 0;
  /// Whether the space changed at all (cells, size or hotspot).
  bool changed = false;
};

/// Applies a task to one voxel space and resets its hotspot (applyTask,
/// then initHotspot, as cb_transform). A nudge that would push filled
/// cells out of the grid is refused (InvalidArgument) and changes nothing
/// (the original dropped them). Minimize goes through `minimizeShapes`.
[[nodiscard]] Result<TaskOutcome> applyTask(Voxel& v, ShapeTask task);

/// Minimize for a set of shapes together (task 26 of the cube grid):
/// scales all of them down by each prime 2 to 19 for as long as every one
/// allows it, then resets their hotspots.
void minimizeShapes(const std::vector<Voxel*>& shapes);

/// The cell tools of the grid editor.
enum class CellTool : std::uint8_t {
  Fixed,     ///< the cell becomes filled (TSK_SET)
  Variable,  ///< the cell becomes variable (TSK_VAR)
  Erase,     ///< the cell becomes empty (TSK_RESET)
  Paint,     ///< only the colour of a non-empty cell (TSK_COLOR)
};

/// Mirror and span bits (TOOL_MIRROR_ and TOOL_STACK_ of the original):
/// mirror also edits the mirrored cell, span the whole row along an axis.
enum EditSymmetry : std::uint8_t {
  kMirrorX = 1,
  kMirrorY = 2,
  kMirrorZ = 4,
  kSpanX = 8,
  kSpanY = 16,
  kSpanZ = 32,
};

/// Applies a tool to cell (x, y, z) and, with `symmetry`, to its mirrored
/// cells and spans (setRecursive). The colour (0: none) goes on every cell
/// that is not empty afterwards; an erased cell loses its colour. Cells
/// outside the space or not valid in the grid are left out. Returns the
/// number of cells changed. The hotspot is not touched (see editCells).
unsigned int applyCellTool(Voxel& v, CellTool tool, unsigned int colour, std::uint8_t symmetry,
                           int x, int y, int z);

/// A cell to edit.
struct CellEdit {
  int x = 0;
  int y = 0;
  int z = 0;
};

/// Applies a tool to cells (each with the symmetry); resets the hotspot
/// when anything changed (setLayer). Returns the cells changed.
unsigned int editCells(Voxel& v, CellTool tool, unsigned int colour, std::uint8_t symmetry,
                       const std::vector<CellEdit>& cells);

/// The size a new shape gets on a grid (guiGridType_c::defaultSize).
[[nodiscard]] unsigned int defaultShapeSize(GridKind grid);

/// The editor's size unit of a grid in cells: rhombic sizes go in blocks
/// of 5, tetrahedra-octahedra in blocks of 3, the others in cells.
[[nodiscard]] unsigned int sizeUnit(GridKind grid);

}  // namespace bt::app

# 0046: A sliding tray's border

**Status:** accepted; `core/shapelogic/grid/tray_border.h` (`TrayBorder`,
`BorderCorners`, `BorderMaterial`), `core/shapelogic/grid/voxel.h`
(`getBorder`, `setBorder`; its picture turned by
`VoxelBricks::transform`), `core/shapelogic/io/puzzle_file.cpp`
(`readTrayBorder`, `writeVoxel`), `core/shapelogic/model/puzzle.cpp`
(`imageUsed`), `core/shapelogic/app/document_commands.cpp`
(`setTrayBorder`, `setShapeImage` and `clearShapeImage` with `border`),
`api/types.json` (`TrayBorder`, `ShapeSummary.border`), `api/shape.json`
(`shape.setBorder`, `border` on `shape.setImage` and `shape.clearImage`),
`web/src/renderer/shell/tray-border.ts`,
`web/src/renderer/entities/border-card.ts`,
`web/src/renderer/view3d/tray-border.ts` (`borderArrays`,
`TrayBorderMesh`), `view3d/view.ts` (`syncBorders`), `view3d/trays.ts`,
`play/board.ts` (`traySvg`), `entities/image-dialog.ts` (`frame`)

## Context
The Play tab draws a sliding puzzle's board as a tray: a rim round the
board's rectangle, the cells that are no floor part of the tray's body,
the floor sunk into it. The other views drew only the floor (the Shapes
tab and the Puzzle tab's two trays) or the walls as black lines (the
Solver). The user asked for a **Tray border** section on the Shapes tab
when a sliding puzzle's board is selected, above Transform (folded for a
board), where the border can be turned on and styled: rounded or sharp
outside corners, a color and an image as the tray's bottom has, and a
material (smooth, textured, wood grain, wood being a light brown unless a
color or image is set). Later the user asked for a **Thickness** below
Material: Thin (the rim as it was) or Thick, a rim one voxel wide, with a
selector for how many voxels thick it is.

Decided with the user: once on, the border is drawn in every view of the
board (the Shapes tab's 3D view, the Puzzle tab's trays, the Solver's
view, Play); with the switch off, Play keeps the tray it has always
drawn, so existing puzzles look as they did.

## Decision
**The model.** A `TrayBorder` on the board's `Voxel`: `enabled`,
`corners` (round, sharp), `material` (smooth, textured, wood), `color`
(-1: none), `thickness` (0: thin; else the rim's width in voxels, up to
`kMaxBorderThickness`, 10) and `image`, a `ShapeImage` (since decision
0047 `images`, a stack of them, the attributes `borderImage2…` after the
first). The look is kept while the border
is off. Only a sliding puzzle's start/goal shape takes one
(`setTrayBorder` and the border's picture refuse any other shape). It is
part of the shape, so undo, redo and Duplicate carry it.

**The picture** lies over the tray's whole outer rectangle, the rim
included, and shows on the border only (the floor covers it). Its `src`
and `turn` count; the turn follows the cells as a shape's picture does
(a quarter turn of the board turns it). Its `box` is the board's
rectangle when it was applied and is not used to draw: the views lay the
picture over the tray as it is now (`borderImage`), so a board resized
keeps its border's picture over all of it.

**The file.** Attributes of the board's `<voxel>`, written only where
they differ from none:

```xml
<voxel … border="1" borderCorners="sharp" borderMaterial="wood"
       borderColor="#336699" borderThickness="2" borderImage="0" borderImageFit="crop"
       borderImageSrc="0 0 60 40" borderImageBox="0 0 4 5"
       borderImageTurn="0" type="0">…</voxel>
```

The picture's bytes are in the puzzle's `<images>` table, shared with the
shapes' pictures (decision 0038). Versions before this one keep the
attributes as they keep any they do not know (decision 0008); the
original program passes over them (decision 0006).

**The API.** `ShapeSummary.border` (null for any shape but a sliding
puzzle's board); `shape.setBorder` changes the fields given, one undo
step; `shape.setImage` and `shape.clearImage` take `border: true` for the
border's picture.

**Drawing.** The border is the tray's body: a rim round the board's
rectangle and a block on every cell that is no floor on any layer. A thin
rim is `BORDER_RIM` (0.28 of a cell, Play's); a thick one is as many
cells wide as the border is voxels thick (`borderRim`). A thick border's
round corners are a quarter circle of its rim in every view, Play's too;
the Puzzle tab's two trays stand `TRAY_GAP` plus both rims apart; Play
fits its cells to the board with the wider tray round it.

- *Play* (`traySvg`): the tray's body takes the border's color and
  picture, its corners are square for sharp, and the surface lies on the
  body only: none for smooth, the tray's grain stronger for textured,
  streaks along x for wood. Without a border the tray is drawn as before.
- *The 3D views* (`view3d/tray-border.ts`): the geometry is built in the
  page from the board's size and its floor (the cells of the board's
  mesh, or `shape.get` for the Solver's result), as `tray3d.ts` builds a
  tilted tray. Round corners are a quarter circle of the rim's width. On
  the Shapes tab and in the Puzzle tab's trays the border stands from the
  floor's bottom to one layer above it, so the pieces stand in it; round
  the Solver's pieces it is as high as the result, and stands for the
  walls' lines. The material is the views' light over the color or the
  picture, with the surface as a factor on it (a fine noise, or wood's
  rings), so it lies on a picture too.
- *The 2D voxel editor* shows no border: it edits the floor.

Color: the border's own; else the wood's light brown (`#c9a26b`) for
wood; else the color the view draws the board in.

## Consequences
- A board's snapshot row grows by `border`; the fake engine's sessions
  were recorded again.
- The 3D border is drawn with a larger polygon offset than the faces, so
  a piece's face in a wall's plane always wins.
- The Solver's view has no floor under the pieces, with or without a
  border.

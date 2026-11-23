# 0045: A Tilted Square puzzle is a cube-grid puzzle with a tray

**Status:** accepted; stage A of `docs/TILTED-SQUARE-PLAN.md` is in the
code: `core/shapelogic/model/tilted_tray.h` (`tilted::Tray`, `layout`,
`bestShift`, `canonicalShift`), `core/shapelogic/model/puzzle.h`
(`Puzzle::tray`, `setTray`), `core/shapelogic/io/puzzle_file.cpp`
(`readTray`, `writeTray`, `kSlotTray`). The document rules, the engine and
the front end are stages B to E.

## Context
The user draws tray puzzles whose square cells are turned inside a square
tray ("1 down, n over"). The research page "Tilted Square Geometry"
(`site/public/research/tilted-square-geometry.html`) worked out which
cells stay whole, how many fit, and how a small allowance at the edge adds
cells. The user asked for a mode, File ▸ New ▸ Tilted Square, with one
board made from tilt settings, pieces drawn on the board's grid, and walls.

## Decision
**The grid is the cube grid.** In the grid's own frame a tilted tray is a
one-layer Bricks puzzle whose board is a generated set of cells. Nothing
in the assembler, the symmetries, the meshes or the exports changes. A new
grid kind was not made: it would have added a case to every switch over
`GridKind` and to every cube-grid test in the core and the front end, for
a grid that is the same grid. The mode is known by the puzzle's tray
(`Puzzle::tray()`), and by nothing else.

**The file is grid type 0 plus a `<tray>` element**, a ShapeLogic-only
element after `<gridType>`, written only when the puzzle has a tray. A
file without one is written as before, so the comparison corpus is
untouched. The original and older versions of this program open such a
file as a plain brick puzzle (older versions keep the element; the
original drops it). A tray the reader cannot take is kept as unknown
content, not refused: the file still opens.

**The tray is straight squares, and walls belong to it.** A tray of side
L is L by L unit squares, each open or a wall. A wall stays where it is at
every tilt. (An earlier idea, blocking turned cells, was dropped: a turned
cell has no counterpart at another tilt, so its block would have nowhere
to go.) The board's cells are always generated, never painted.

**A cell is whole when every point of it is within the allowance of an
open square**, in x and in y. With no walls this is the research page's
rule: every corner within the allowance of the tray. With walls it lets a
cell reach the allowance into a wall, as into the rim, and it leaves no
slit between two walls side by side (shrinking each wall on its own
would). The allowance is at most 0.25 of a cell.

**The best placement is found exactly, walls included.** The count of
whole cells changes only where a cell's corner meets an edge of the tray
or of a wall, or a wall's corner meets a cell's edge. Those are lines in
the plane of shifts, in four directions, and a best point lies where two
of them cross. `bestShift` takes the best point along every line of three
of the four directions, which covers every crossing; along a line each
cell is whole on a closed stretch less open stretches, so the best is at
the start of a stretch. A net of shifts would miss the cases that matter:
the best is often a single point (a straight tray's 64 cells; an 8 by 8
tray at 1 down 2 over with an allowance of 0.025, 52 cells against 45).

**Ties go to a placement symmetric about the tray's center** (the center
on a cell's middle, on a grid point, or on the middle of a cell's edge),
then to the first line that reaches the count. The shift is stored in the
file for either placement, so a file opens to the same board whatever a
later version computes.

## Consequences
- Lengths that are equal on paper differ by rounding, so a cell that
  reaches less than 1e-9 into a wall or past the rim is whole
  (`kEps`). The placement returned is the middle of the stretch that has
  the best count, never its end, so counting again at that shift gives the
  same number.
- The search costs a line sweep over the cells for each line: a few
  milliseconds for the trays people draw (8 by 8 with walls), and about a
  second in an unoptimized build for a 64 by 64 tray or a 32 by 32 one
  with a hundred walls.
- The tests hold the research page's table (trays 4 to 12, slopes 1:1 to
  1:6, five allowances) and walled trays whose counts were confirmed with
  a polygon library outside the code.

## What was not done
- No shape is made from the tray yet, and a tray does not limit what the
  shapes are: that is the document's work (stage B).
- Trays that are rectangles; a tray body in the STL or Puzzlecad export.

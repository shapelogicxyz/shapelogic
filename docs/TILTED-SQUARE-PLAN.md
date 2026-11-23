# Plan: the Tilted Square mode

## Context
Written on 2026-11-22 from the research page "Tilted Square Geometry"
(`site/public/research/tilted-square-geometry.html`) and a survey of how
the Sliding and Stacking modes are built. "Status" below says which
stages are in the code: all of them, since 2026-11-23.

**What the mode is.** A 2D tray packing puzzle: a square tray whose
square cells are turned inside it by arctan(down/over) ("1 down, n
over"). The cells the tray's edge cuts can hold no piece; the whole
cells are the board, and the pieces must fill it. The user chooses it
with **File ▸ New ▸ Tilted Square**.

**What the research page gives.** For a tray of side L and a turn θ:
- a turned cell needs an upright box of side w = cos θ + sin θ, so it
  is whole exactly when its center is at least w/2 from every edge;
- at slope down/over the turned grid repeats along the tray's edges
  every √(over² + down²), so the edge cuts the cells in a regular
  pattern;
- the number of whole cells depends on where the grid sits. The best
  and the worst placement are found exactly: each cell's allowed
  shifts make a rectangle, and the rectangles are swept
  (`tmp/tilted-squares/geom.py`, git-ignored: `extremes`);
- letting a corner poke past the edge by a few hundredths of a cell
  (the allowance) adds whole rows: an 8 by 8 tray at 1 down 2 over
  holds 45 cells strictly and 52 with an allowance of 0.025.

## Status
- [x] Stage A: geometry, model and file (2026-11-22; decision 0045).
  `core/shapelogic/model/tilted_tray.{h,cpp}` (namespace `tilted`),
  `Puzzle::tray`, `<tray>` in `io/puzzle_file.cpp`. Tests:
  `tests/unit/model/test_tilted_tray.cpp`, `tests/unit/io/test_roundtrip.cpp`.
  Gates: `just test-all` and `just test-asan` passed, `just check` on the
  changed sources passed. `just oracle-roundtrip` could not be passed on
  2026-11-22 for a reason outside this stage: the reference checkout
  (`../burr-tools`, master at d09cc299) had no build and, once built,
  refuses the sliding and stacking files ("unknown grid type") and writes
  no `timeMs`; none of its 342 mismatches names the tray. Run it again
  when the reference is back at the revision the harness expects.
- [x] Stage B: document rules and engine (2026-11-22; decision 0045).
  `core/shapelogic/app/document_tilted.cpp`, `SolveOptions::flipPieces`,
  `api/tray.json` with `engine/methods_tray.cpp`, `Snapshot.tray`. Tests:
  `tests/unit/app/test_document_tilted.cpp`, the contract test
  `tilted_tray` (Triple Play made, solved and changed). Two things differ
  from the stage's text below: with flipping off the search has no
  symmetry breaker (assemblies that are one another turned are counted
  each), and the cells' states are in `Snapshot.tray.cells`, not in
  `grid.geometry`. `solution.toShapes` is refused on a tilted puzzle.
  Gates: `just test-all` (the contract suite with it), `just test-asan`,
  `just check` on the changed sources, `just app-check` and `just app-test`
  passed; `just oracle-roundtrip` as under Stage A.
- [x] Stage C: the mode in the front end (2026-11-22). `state/tilted.ts`,
  `entities/tilt-card.ts`, `entities/tray-view.ts`, File ▸ New's Tilted
  Square, the Board section, the Solver's Pieces may be turned over.
  Tests: `web/tests/unit/renderer/tilted.test.ts`,
  `web/tests/e2e/tilted.e2e.ts` (written here, not in stage F). What
  differs from the text below: the board's views draw the tray in
  its own frame from the start, so stage D is left with the grid editor
  of a piece and the 3D views; a piece's
  grid already grays the cells the tray cuts; Pieces may be turned over is
  in the Solver's Search Options with the problem's other solver
  settings, not in the Puzzle workspace; and File ▸ New asks nothing (the
  user, 2026-11-22: simple like the other modes): it shows the default
  tray as its example and makes it, 8 by 8, 1 down 2 over, strict, and
  Tilt & tolerance changes it. At the user's ask the 3D view's turn
  (stage D, item 2's first part: `ViewRenderer.turn`, set by `View3D`'s
  `syncTurn` from the tray's angle) came early: the model lies in the 3D
  view as the grid lies in the tray, in every workspace whose scene is
  under the view's root. Two engine fixes came with stage C:
  `shape.taskPreview` shows where a task leaves a tilted piece (it showed
  the task alone), and `tray.preview` answers a whole `Tray` (the New
  dialog's picture). Later, at the user's ask: the board has two views,
  Tray and Cells, both the turned cells under the straight squares and
  both taking clicks for walls; Tray brings the squares out (bright
  lines, the cells muted), Cells the turned cells (vivid colors, strong
  lines, the squares faint). The Both view went.
- [x] Stage D: the tilted drawing (2026-11-22). A piece's grid editor
  turned so the tray stands straight, clipped to it, with its outline and
  walls (`Grid2D.buildTilted`, `GridInput.tilt`); the tray in 3D
  (`view3d/tray3d.ts`: the outline and each wall a dark block one layer
  high, in the shape view's model and around the Solver's pieces, redrawn
  with every wall change; the board as checker blocks in the Cells view's
  blue and green, in the shape view and as the Puzzle workspace's result); a straight-down home view for a tilted puzzle
  (`Camera.setHome`); the Puzzle workspace's pieces turned. The
  perspective is left as the Display menu sets it (the plan had it off).
  Tests: `tilted.test.ts` (the blocks' geometry), `tilted.e2e.ts` (the
  turned editor, the 3D walls). The entities, view, picking and tilted
  specs passed three times, puzzle, solver and sliding once; the stacking
  spec's player screenshot fails with and without this stage (its
  reference shows the Solver card before the foldable explorer).
- [x] Stage E: rectangular trays (2026-11-23). `Tray::width` and
  `height` in place of `size` (`withSize(tray, width, height)`), `<tray>`'s
  `width` and `height` for a rectangle (`size` kept for a square, so the
  files written before are written as they were; the reader takes either),
  `TraySettings.width` and `height` (`size` sets both), `Tray.width` and
  `height` in place of `size`. Front end: Width and Height in Tilt &
  tolerance with the Keep square switch (the plan's link switch; on at
  first for each document, off by itself for a tray that is not square;
  turned on, the height takes the width), the tray's size in the readout
  ("Tray: 8 by 6") and in the line under the steppers, the tray picture, the
  board's views and a piece's turned editor fitted to a rectangle. Tests:
  `test_tilted_tray.cpp` (the walled square as the oracle, widths and
  heights 3 to 10, slopes 1:1 to 1:4, allowances 0 and 0.05, cell for cell
  and in the best count; a rectangle with walls of its own; the quarter
  turn, which goes through no wall: a width by height tray holds as many
  cells as the height by width one), `test_roundtrip.cpp`,
  `test_document_tilted.cpp` (Triple Play's pieces in a 6 by 8 tray and in
  the 8 by 8 tray with two columns walled: as many assemblies), the
  contract test `tilted_tray` (the same through the engine),
  `tilted.test.ts`, `tilted.e2e.ts` (width and height set with Keep
  square off). Gates: `just test-all`, `just test-asan`, `just check` on
  the changed sources, `just app-check`, `just app-test` and the tilted e2e
  spec (three times) passed; `just oracle-roundtrip` as under Stage A.
- [x] Stage F: fixture, guide and finish (2026-11-23). Fixtures in
  `tests/data/puzzles/tilted/` made by its `make_fixtures.py` (which also
  writes the demo's example and the guide's download): Triple Play, with a
  second problem "One face up" (`flipPieces` off, the one mix of drawn and
  mirrored pieces that goes: 2 assemblies), and an 8 by 8 tray at 1 down 2
  over that five pieces of nine fill (2 assemblies). Triple Play turned
  over has 1 assembly, not the 2 item 1 below says (the two ways are one
  another turned half round, as stage B found). Checks in
  `test_puzzle_file.cpp`, `test_document_tilted.cpp`, `test_cli.py`, the
  engine test `web/tests/engine/tilted.test.ts` and a second test in
  `tilted.e2e.ts` (Triple Play opened and solved). The guide's page
  `puzzle-types/tilted-square.md` with screenshots, the demo's Tilted
  Square example (demo rebuilt), `docs/ARCHITECTURE.md`,
  `docs/webui-spec.md` section 13, `docs/handoff/tilted-square.md`,
  `RELEASE_NOTES.md`. Gates: `just gate build unit app site` passed (with
  `just site-demo --check`), `just check` on the changed C++ tests, the
  tilted e2e spec three times. Then three fixes (the handoff's "Fixed"):
  the CLI follows each problem's stored `flipPieces` and takes `--flip
  on|off` (`bt::trayPlaneTurns`, `bt::storedFlipPieces`, shared with
  `SolveSession`); a piece that fits nowhere is marked "Doesn't fit" and a
  tray change that leaves one says so; the disassembly player offers no
  analysis on a tilted or Spheres puzzle.
- [x] Stage G: every placement (2026-11-23). `tilted::placements()`
  (`model/tilted_tray.{h,cpp}`: the lines `bestShift` sweeps, swept for
  every stretch where the cells whole are more than on either side; equal
  boards once, parts of others dropped, `PlacementLimits`),
  `app/placement_fit.{h,cpp}` (`PlacementFit`, the job) with
  `Document::trayPlacements`, `fitPlacementsStart`, `Stop` and `Progress`,
  the engine's `tray.placements`, `tray.fitPlacements`,
  `tray.fitPlacementsStop`, `tray.fitPlacementsStatus` and the events
  `tray.fitProgress` and `tray.fitEnded`. Front end: the Placements dialog
  (`entities/placements-dialog.ts`, `state/placements.ts`), **Placements…**
  in Tilt & tolerance, **Try every placement** in the Solver's empty card.
  Fixture `tests/data/puzzles/tilted/OtherDiagonal.xmpuzzle` (Triple Play's
  pieces, the walls on the other diagonal): six boards, the best takes no
  pieces, one of the others takes them in 1 way. What differs from the
  text below: boards are compared as shapes, not after `canonicalShift`
  (the same cells moved by whole cells are one board, and so are boards
  turned by quarter turns, since a piece can be turned; mirror images
  stay apart, a problem may keep its pieces face up); the crossings are
  not taken pair by pair (each line is swept, which finds the same boards
  with far less work); the limit is on the work and on the boards kept,
  not on a number of crossings; the list is a method of its own
  (`tray.placements`, which the dialog shows before any count) and the job
  takes the list as it is; the job freezes the document while it runs (as
  an analysis does), so its counts stay true; a board too small for the
  pieces, or one a piece fits nowhere on, counts as taking none (a solve
  of it ends in an error). Tests: `test_tilted_tray.cpp` (the best first,
  each board its own, the board of every shift of a net part of a board
  listed, the limits and the stop), `test_document_tilted.cpp` (each
  board's count against a puzzle made at that placement, the options, the
  refusals, a stop, a small board), the contract test `tilted_placements`,
  `placements.test.ts`, `tests/engine/tilted.test.ts`, `tilted.e2e.ts`
  (the list, Try every placement, Use, Undo). Gates: see
  `docs/handoff/tilted-square.md`.

## Who builds each stage

| Stage | Work | Complexity | Model | State |
|---|---|---|---|---|
| A | Geometry, model, file (core) | Medium to high: the exact best-placement search with walls | Fable 5.2 High | Done (adbae15) |
| B | Document rules and engine | High: one-step tray changes that move every piece, the refusals, flipping in the solver | Fable 5.2 High | Done (1d7c352) |
| C | The mode in the front end | Medium: much work, most of it on patterns the code has | Opus 5.5 High | Done (dc30490) |
| D | The tilted drawing (a piece's 2D grid turned, the tray and its walls in 3D, the Puzzle pieces) | Medium to high: two grids drawn over each other, hit testing under the turn | Opus 5.5 High | Done |
| E | Rectangular trays | Medium: the exact search and the file format again, with the walled square as the oracle | Fable 5.2 High | Done |
| F | Fixture, guide and finish | Low to medium: tests, the Triple Play fixture, the guide page, docs | Opus 5.5 High | Done |
| G | Every placement | High: every board the sliding grid can make, exactly, and a job that solves each | Fable 5.2 High | Done |

The rule behind the choice: Fable for the stages where a wrong answer is
hard to see (counts and boards that tests must pin exactly), Opus for the
stages whose mistakes show on screen or in a failing test.

## Decisions

**Built on the Brick grid, not a new grid kind.** In the grid's own
frame a tilted tray is a one-layer Brick puzzle whose board is a
generated set of cells. The assembler, the symmetries, the meshes, the
STL export and every cube-grid check then work unchanged. A new
`GridKind` would mean 12 core switches, about 10 core sites and about
11 front-end sites that would silently take a default, and the grid
lists of the tests. The mode is known by a puzzle-level tray:
`snapshot.tray !== null` (one helper, `isTilted`, never a second test).
The file is grid type 0 plus a ShapeLogic-only `<tray>` element,
written only when present, so the comparison corpus round-trips as it
does today. Decision record: 0045 (written in Stage A).

**A board and pieces.** One shape is the board: always shape 0, listed
in its own Board section above the pieces, as the Sliding mode lists
its boards. With the board selected the inspector shows a section
**Tilt & tolerance** above Transform.

**One shared grid.** Every shape has the board's grid size: the box, in
grid cells, of every cell the tray touches, one layer deep. A piece is
drawn on the board's own canvas: the tray shown straight, the grid
turned inside it, the cut cells gray and inert. No size is set by hand.
(A piece does not need the board's grid to be solved; the shared canvas
is what lets it be drawn in place, at the tray's tilt.)

**Exactly one board.** It is made with the puzzle and is never added,
deleted or copied. The precedent is the Stacking mode's rod set
(`Puzzle::Puzzle` in `core/shapelogic/model/puzzle.cpp`, the loader's
"add one if the file has none" in `io/puzzle_file.cpp`, and
`document_stacking.cpp`'s "Keep at least one rod set"), not the Sliding
mode, whose boards are made by hand, in any number. One problem with
the board as its result is made too; a further problem always uses the
board.

**The tray's settings** (`tilted::Tray`, held by `Puzzle`):
- `size`: the tray's side L, in cells;
- `over`, `down`: the slope down/over in lowest terms ("1 down, n
  over" is down = 1; down = 0 is a straight tray);
- `allowance`: how far a corner may poke past an edge or into a wall,
  in cell widths; 0 is strict. In the interface it is a switch plus an
  amount, 0.05 when first switched on;
- `placement`: best, or custom; and the shift in use (`shiftX`,
  `shiftY`), stored either way so a file always opens to the same
  board;
- `walls`: see below.

**Walls belong to the tray.** The tray is an L by L grid of straight
unit squares, each open or a wall. A wall stays where it is at every
tilt; nothing is ever dropped when the tilt changes. A turned cell is
whole when it lies inside the tray and overlaps no wall (the allowance
applies to a wall as it does to the edge). The board's turned cells are
always generated, from the tilt, the allowance, the placement and the
walls, and are never painted by hand. At tilt 0 the walls are simply
the missing cells of an ordinary board. Walls on the rim give trays
that are not square: L shapes, crosses, notched corners. For an 8 by 8
tray with its four corner squares walled, a first search gives 44, 43,
45, 44, 45 and 45 whole cells at 1:1 to 1:6 (the open tray: 50, 45, 49,
48, 48, 48), and 60 when straight.

**The pieces must fit the board, not fill it.** A tray puzzle often
leaves cells empty: the user's example, Triple Play (below), puts 21
cells of pieces on a board of 24. So the board's whole cells are variable
cells (they may stay empty), as a Brick result's `+` cells are. When the
pieces have as many cells as the board they fill it anyway.

**Flipping is a choice of each problem.** A switch, on at first: the
pieces may be turned over, as on the Brick grid. Off, each piece keeps
to the four turns in the tray's plane (pieces with one printed face).

## The user's example: Triple Play
"Triple Play (IPP34)" by Goh Pit Khiam and Jerry Loo, which the user has
as a Brick file (three 7-cell pieces and a tray; kept outside the
repository). Its tray is a 6 by 6 opening with two opposite corner
squares filled, (5, 0) and (0, 5). Worked out with Stage A's geometry on
2026-11-22:
- the three pieces fit at 1 down 3 over only, of the straight tray and
  the slopes 1:1 to 1:6, and at no placement at all when strict. They
  need an allowance of 0.0042 of a cell or more (16 rows of centers are
  0.0083 too many for the tray);
- with an allowance of 0.05 the best placement has 24 whole cells and
  takes the pieces in 2 ways (one, and the same turned half round), with
  3 cells left empty. Without the corner walls it has 26 cells and 5 are
  left empty, as in the user's picture;
- with the walls on the other diagonal the best placement takes no
  pieces, though 96 of 8,100 other placements tried do. The placement
  with the most whole cells is not always one the pieces fit.

It is Stage F's fixture and demo example, and the test that the mode can
do a real puzzle. The last point is why Stage G tries every placement
(the user, 2026-11-22: "more results are better than missing some").

## Stage A: geometry, model and file

Core only.

1. **The geometry.** `core/shapelogic/model/tilted_tray.{h,cpp}`:
   `tilted::Tray` with its wall mask; `tilted::layout()`, which returns the
   box, every cell's state (whole, cut or outside), the tray's corners
   and its wall squares in grid coordinates, the shift and the count of
   whole cells. A cell is cut when it crosses the edge or overlaps a
   wall (a separating-axis test of the turned square against each wall
   square, less the allowance).
2. **The best placement.** `bestShift()`. Without walls it is a port of
   the rectangle sweep of `tmp/tilted-squares/geom.py`. With walls a
   cell's allowed shifts are a rectangle less the shapes the walls
   forbid, and the count is taken at the corners of that arrangement:
   still exact, and trays are small. One fixed epsilon and a written
   tie-break, so the same settings give the same board on every
   machine.
3. **The model and the file.** `Puzzle` holds
   `std::optional<tilted::Tray>`. `io/puzzle_file.cpp` reads and writes
   `<tray>` through a new `PuzzleSlot`, as it does `<images>`
   (`kSlotImages`).
4. **Tests** (`tests/unit/model/`, `tests/unit/io/`):
   - the best counts equal the research page's table: trays 4 to 12,
     slopes 1:1 to 1:6, allowances 0, 0.01, 0.02, 0.05 and 0.1;
   - the walled tray above, against a brute-force search over shifts;
   - the same settings always give the same cells;
   - a file with `<tray>` round-trips, and a file without one is
     written as before.
5. **Decision record** `docs/decisions/0045-tilted-square-trays.md`.
6. **Gates.** `just test-all`, `just test-asan`, `just oracle-roundtrip`,
   `just check`.

## Stage B: document rules and engine

1. **The document** (`core/shapelogic/app/document_tilted.cpp`):
   - a new tilted document has its board and one problem with the board
     as its result. The board's whole cells are variable (the pieces must
     fit, not fill); check first that the assemblers take a result with
     no fixed cell, and what `maxHoles` must be; on open, a board that disagrees with its `<tray>`
     is generated again;
   - `setTray(settings)`, one undo step (`StepGuard` with `touchWhole`,
     as `setRodSetRules`): generate the board, give every piece the new
     box, and move each piece by whole cells to the nearest place where
     all its cells are whole. A piece that fits nowhere is kept,
     centered, and reported as not fitting;
   - `setTrayWalls(squares, wall)`, one undo step: makes straight
     squares walls or open, then generates as `setTray` does. A change
     of tilt, allowance or placement leaves the walls alone; a change
     of size keeps the walls still inside the tray;
   - refused with `Errc::InvalidArgument` (never `InvalidState`, which
     the front end shows as "pause first"): deleting or copying the
     board, moving it from first place, resizing any shape, editing the
     board's cells or running a shape task on it, using it as a piece,
     another result. An edit of a piece touches whole cells only, and a
     Transform that cannot land on whole cells is refused.
2. **Flipping.** A solver setting of the problem, `flipPieces` (true at
   first), with the others in `SolverSettings`. When it is false the
   solve sets `MatrixOptions::partTransforms` (`solve/assembler.h`; the
   sliding solve sets it in `solve_session.cpp`) to the four turns
   about z, and the result's symmetries the breaker uses are limited to
   the same turns. Test: the assembly multisets against a brute-force
   count on small trays, flipping on and off, serial and parallel.
3. **The engine** (each addition its own `Engine:` commit, by
   `api/README.md`'s "Adding a method"):
   - `document.new` takes optional `tray` settings, on the Brick grid;
   - a new area `api/tray.json`: `tray.set`, `tray.setWalls`, and
     `tray.preview` (the count, the angle and the box for some
     settings, changing nothing: the New dialog's count);
   - `Snapshot.tray` (the settings, the walls, the angle in degrees,
     the counts of whole and cut cells, the corners, the board's
     index); `ShapeSummary.board` and `.fits`; the cells' states and
     the wall squares in `grid.geometry`; `ChangeKind` `tray`.
4. **After it.** Contract calls for the new methods,
   `python3 api/gen_api_md.py`, `just app-gen`, `npm run record`.
5. **Gates.** Stage A's, the contract suite, `just app-test`.

## Stage C: the mode in the front end

1. **File ▸ New.** A "Tilted Square" entry after Stacking
   (`shell/files.ts`, `shell/files-order.ts`, `shell/new-grids.ts` with
   its picture and description), with the size, the slope and the
   allowance, and the count from `tray.preview`. The type chip
   (`renderGridChip` in `shell/app.ts`) reads "Type: Tilted Square".
2. **The list** (`entities/shapes-list.ts`). A Board section of one row
   with no new, copy or delete; "+ New Piece"; Del and Ctrl+D do
   nothing on the board.
3. **Tilt & tolerance** (`#secTilt` before `#secTf` in `index.html`,
   filled by `entities/inspector.ts`; its values join the memo key of
   `renderInspector` in `entities/workspace.ts`). For the board only:
   the tray's size, the "down" and "over" steppers with the angle
   beside them, the allowance switch and amount, the placement, a line
   such as "44 whole cells, 4 walls, 45.00°", and a link to the
   research page. A typed value is one `mutate`, as in
   `entities/rods-card.ts`; a drag is one group (`edit.begin`,
   `edit.end`), as in `entities/stroke.ts`. For the board, Transform,
   Fit & scale and Repair & surface are hidden. The grid size steppers
   and the layer tabs are hidden in this mode.
4. **The board's views.** A switch on the board's editor card, Tray,
   Cells or Both:
   - *Tray*: the straight L by L squares alone. A click or a drag makes
     or clears walls (`tray.setWalls`; a drag is one undo step).
   - *Cells*: the turned cells that result, whole in color, cut in
     gray, walls dark. Nothing is painted here.
   - *Both*: the turned cells with the straight squares over them as
     faint lines. A click changes the straight square under the
     pointer.

   The user asked to try for all three. If Both proves hard to read or
   to hit at small tilts, it keeps the overlay and loses the editing.
   That is settled with the user at this stage's review, never
   silently.
5. **The Puzzle workspace.** The result is fixed (no Change…); the
   problem has a "Pieces may be turned over" switch; the Solver stays
   on assemble only, and take-apart is not offered.
6. **Gates.** `just app-check`, `just app-test`, and the e2e specs this
   touches (entities, files), each three times.

## Stage D: the tilted drawing

1. **A piece's 2D editor** (`entities/grid2d.ts`). The cells, the
   hints, the marks and the overlay go in one group turned by the tilt
   about the tray's center and clipped to the tray, with the tray's
   outline over it. Cut cells stay gray and take no pointer events (as
   stage C does them). The fit uses the tray's side. The angle joins
   `builtFor`. The rulers and the axis lines are off. `cellAt` finds a
   cell with `elementFromPoint`, which follows the turn, so painting,
   strokes and the box selection need no change. The tray's walls are
   drawn dark behind the piece's cells. (The board's own views are stage
   C's, in the tray's frame already.)
2. **The 3D views** (`view3d/renderer.ts`). The turn about z came early
   with stage C (`ViewRenderer.turn`); the shape view, the Solver's
   assemblies, their animation, the picking and the image exports turn
   together. Still to do:
   - **The tray in 3D.** Its outline and its walls (the user,
     2026-11-22): each wall square a block one layer high, straight in
     the tray's frame (so turned against the grid), in the walls' color
     of the 2D views, around the board in the shape view and around the
     pieces in the Solver. A wall made or opened in the board's views
     shows at once in 3D (the snapshot's `tray.wallPolygons` and
     `corners`, which change with every `tray.setWalls`). In the shape
     view the blocks are built in the renderer (`view3d/board.ts`'s
     `boxesGeometry` is the pattern) and the outline is fat lines
     (`view3d/overlays.ts`); in the Solver through
     `SolutionScene.setWalls` (`view3d/walls.ts` is the pattern) in the
     solution's centered frame. The SVG export includes them.
   - The home view straight down, without perspective, for a tilted
     puzzle.
   - The Puzzle workspace's pieces turned too (`view3d/pieces.ts`).
3. **Gates.** Stage C's, and the reference screenshots reviewed.

## Stage E: rectangular trays

A tray of any width and height, not only square (the user, 2026-11-22).
Before it, a rectangle was had with walls: a 6 by 8 tray is an 8 by 8
tray with two columns walled, which gives the same whole cells and the
same best count (a wall acts as the edge does, the allowance included).

1. **The geometry** (`model/tilted_tray.{h,cpp}`): `Tray::width` and
   `Tray::height` in place of `size`; the wall mask is width by height;
   `layout` and `bestShift` take the edges at x = width and y = height
   (the lines the sweep tries, the cells' range, the window); the
   symmetric tie-break takes the rectangle's center; `withSize(tray, w,
   h)` keeps the walls still inside. `valid()` bounds each side by
   `kMaxSize`.
2. **The file.** `<tray>` keeps `size` for a square tray, so the files
   written so far read and write byte for byte as before; a rectangle
   writes `width` and `height` instead. The reader takes either.
3. **The engine and the API.** `TraySettings` takes `width` and
   `height` (`size` stays, setting both); `Tray` answers `width` and
   `height` in place of `size`, and the front end follows. `tray.set`
   with a new width or height keeps the walls inside the new tray.
4. **The front end.** Tilt & tolerance: Width and Height steppers in
   place of Tray size, with a link switch (on at first) that keeps them
   equal; the readout says "8 by 6". The tray picture, the board's views
   and the New dialog's example draw a rectangle. File ▸ New still makes
   the 8 by 8 square.
5. **Tests.**
   - A rectangle equals the square with the extra rows or columns walled:
     the same whole cells, cell for cell, and the same best count, for
     widths and heights 3 to 10, slopes 1:1 to 1:4, allowances 0 and
     0.05 (the walled square is the oracle).
   - A square tray's file is written as before (the round trip tests of
     stage A unchanged); a rectangle's round-trips.
   - The contract test makes a 6 by 8 tray and solves a puzzle in it;
     the e2e spec sets width and height in Tilt & tolerance, the link off.
6. **Gates.** Stage A's and B's (`just test-all`, `just test-asan`, the
   contract suite, `just check` on the changed sources), then `just
   app-check`, `just app-test` and the tilted e2e spec.

## Stage F: fixture, guide and finish

1. **Fixture.** `tests/data/puzzles/tilted/`: Triple Play (a 6 by 6
   tray, two corner walls, 1 down 3 over, allowance 0.05: 2 assemblies)
   and an 8 by 8 tray at 1 down 2 over with pieces that fill it, with
   their own checks (as `new-grids/` has); an engine test in the web tests; one
   e2e spec (a new tilted puzzle, a change of tilt, the four corners
   walled, a piece drawn, a solve); an example in the demo
   (`web/src/demo/main.ts`).
2. **Documents.** A page of the user guide, `docs/ARCHITECTURE.md`,
   `docs/webui-spec.md`, a handoff note, `RELEASE_NOTES.md`.
3. **Gates.** The `just gate` groups for what changed, `just site-build`.

## Stage G: every placement

In a real tray the pieces may sit anywhere, so the true question is
whether some placement of the grid takes them. Stages A to F solve the one
placement in use (the best, or the user's own); this stage looks at all of
them.

1. **The boards there are** (core, `tilted::placements()`). As the grid
   slides, the set of whole cells changes only on the lines `bestShift`
   sweeps, and a cell stays whole on the lines that bound its stretch. So
   every board is part of the board at some crossing of two lines. The
   crossings are taken (all six pairs of the four directions), each one's
   board worked out, equal boards kept once (cells compared after
   `canonicalShift`), and a board that is part of another dropped: since
   the pieces need only fit, a board's part can hold nothing its whole
   cannot. What is left is every placement worth trying, each with its
   shift and its count. A written limit on the number of crossings keeps
   large trays with many walls from running long; past it the answer says
   it is not complete.
2. **Which of them take the pieces** (document and engine). A job,
   `tray.fitPlacements`, for one problem: for each board, the number of
   assemblies of the problem's pieces, found by the ordinary assembler on
   a scratch copy (the document is not changed). It takes a stop token,
   reports its progress as a solve does, and is refused while the problem
   is solving.
3. **The front end.** In Tilt & tolerance, "Placements…": a list of the
   boards, each with a small picture, its count of whole cells and, once
   the job has run, its assemblies for the problem shown. Choosing one
   sets the placement to custom at that shift (one undo step). In the
   Solver, a tilted problem that ends with no assembly offers "Try every
   placement", which runs the job and opens the list.
4. **Tests.** Triple Play with its walls on the other diagonal: the best
   placement takes no pieces, and the list holds placements that do. The
   list for an open tray holds the best placement. Every board of a net
   of shifts is part of a board in the list.
5. **Gates.** The core's (Stage A's), the contract suite, `just
   app-check`, `just app-test`, and the e2e spec for the list.

## Not in this plan
- A tray body in the Puzzlecad or STL export.
- The Play tab.
- Turning an existing Brick puzzle into a tilted one.
- Tilting the other way. The slope turns the grid one way only; the
  mirror image is had by mirroring the walls and flipping the pieces.

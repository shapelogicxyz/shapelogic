# bt-engine API

*Generated from `api/*.json` by `api/gen_api_md.py`; do not edit. The schema
is the contract (decision 0012); this page is its reference.*

`bt-engine` speaks JSON-RPC 2.0 over stdio: one request per line on stdin,
one response or notification per line on stdout, logs on stderr. Binary
data (meshes, poses) comes as a **frame**: the notification
`{"jsonrpc":"2.0","method":"frame","params":{"frame":N,"bytes":L}}`, then
exactly L raw bytes; the reply referring to frame N follows. A frame holds
little-endian typed arrays at offsets that are multiples of 4, described by
the reply's `Frame` object.

**Ids** are strings: shapes `s7`, colours `c2`, problems `p1`, solutions
`sol42` (unique within their problem), playbacks `pb1`, explorers `ax1` and
`mx1`. Labels (`S1`, `C1`, `P1`) follow the list order.

**Errors** carry `data.errc`:

| code | errc | meaning |
|---|---|---|
| -32700 | parse_error | the line is not JSON |
| -32600 | invalid_request | not a JSON-RPC request (batches are refused) |
| -32601 | method_not_found | no such method |
| -32602 | invalid_params | a parameter is missing, of the wrong type, or unknown |
| -32001 | invalid_argument | a value out of range or malformed |
| -32002 | not_found | no such shape, colour, problem, solution or handle |
| -32003 | parse_error | a file or bytes that are no puzzle |
| -32004 | io_error | reading or writing a file failed |
| -32005 | unsupported | valid, but not for this puzzle (what a grid or a solver lacks: a take-apart, an assembler, Panex columns) |
| -32006 | invalid_state | not now: pause the solve first, close the edit group... |
| -32603 | internal | a broken invariant in the engine or the core |

Every mutating method returns the document's new `revision` and is one undo
step (or part of the `edit.begin` / `edit.end` group); `document.changed`
follows it.

## Contents

- [Engine and settings](#engine-and-settings)
- [Document and history](#document-and-history)
- [Shapes and cells](#shapes-and-cells)
- [Colours](#colours)
- [Problems](#problems)
- [Sliding puzzles](#sliding-puzzles)
- [Stacking puzzles](#stacking-puzzles)
- [Solving](#solving)
- [Solutions](#solutions)
- [Playback](#playback)
- [Explorers](#explorers)
- [Exports](#exports)
- [Types](#types)

## Engine and settings

What the engine can do, its settings (kept across documents), and the frame announcement.

### `engine.hello`

Tells the engine which protocol the client speaks; a client calls it first. The engine answers with the protocol it will speak (this version: 1) or refuses one it does not know (unsupported), so that when the protocol changes, old and new clients can be told apart.

Params:

| name | type | | description |
|---|---|---|---|
| `protocol` | integer | required | The protocol the client speaks. |

Result:

| name | type | | description |
|---|---|---|---|
| `protocol` | integer | required |  |
| `name` | `"bt-engine"` | required |  |
| `version` | string | required |  |

Errors: `unsupported`

### `engine.info`

The engine's version, the grids and solvers it knows, the machine's threads, and its methods.

Params:

(none)

Result:

| name | type | | description |
|---|---|---|---|
| `name` | `"bt-engine"` | required |  |
| `version` | string | required |  |
| `protocol` | integer | required | Raised when a change breaks clients. |
| `grids` | object[] | required |  |
| `solvers` | object[] | required |  |
| `slidingSolvers` | object[] | required | The solvers of sliding puzzles (SolveOptions.sliding.solver), with the original's menu labels. |
| `stackingSolvers` | object[] | required | The solvers of stacking puzzles (SolveOptions.stacking.solver), with the original's menu labels. The original offered one, by the problem's rod set: the Panex Solver for Panex columns, else the Stacking Solver, which a solve without a solver named uses too. |
| `hardwareThreads` | integer | required |  |
| `maxThreads` | integer | required |  |
| `methods` | string[] | required |  |

`grids`:

| name | type | | description |
|---|---|---|---|
| `grids.grid` | [Grid](#grid) | required | The grid of a puzzle, fixed for its life. |
| `grids.label` | string | required |  |
| `grids.assemble` | boolean | required | It has an assembler. |
| `grids.disassemble` | boolean | required | It has a take-apart. |
| `grids.supported` | boolean | required | This version solves its puzzles: every grid (stacking since the deferred plan's part 3b). |
| `grids.defaultSize` | integer | required | The size of a new shape in each direction, in cells. |
| `grids.sizeUnit` | integer | required | shape.resize counts in blocks of this many cells. |
| `grids.convertsTo` | [Grid](#grid)[] | required | The grids document.convertGrid converts its puzzles to (the original's Convert brick grid type to other): rhombic and tetraOcta for bricks, none for the others. |

`solvers`:

| name | type | | description |
|---|---|---|---|
| `solvers.solver` | [Solver](#solver) | required | Complete (ShapeLogic), or Fast (Crowell): Andrew Crowell's take-apart. |
| `solvers.label` | string | required |  |

`slidingSolvers`:

| name | type | | description |
|---|---|---|---|
| `slidingSolvers.solver` | [SlidingSolver](#slidingsolver) | required | How far a sliding puzzle's search may go: 250,000 arrangements, 1,000,000, or no limit (the full search keeps its older levels on disk and spreads each over threads; the default). |
| `slidingSolvers.label` | string | required |  |

`stackingSolvers`:

| name | type | | description |
|---|---|---|---|
| `stackingSolvers.solver` | [StackingSolver](#stackingsolver) | required | The Stacking Solver (the fewest rod transfers: the Panex search whenever the puzzle fits it, which pauses, carries on and saves itself, else a search of its own) or the Panex Solver (the Panex search alone, for rod sets with Panex columns; a puzzle it cannot take is refused with its reason). |
| `stackingSolvers.label` | string | required |  |

### `engine.shutdown`

Replies, then ends the engine: its solves pause and are waited for (end of input does the same).

Params:

(none)

Result: [Empty](#empty)

### `settings.get`

The engine's settings.

Params:

(none)

Result: Settings

### `settings.set`

Changes some settings; the others stay. A smaller undo depth drops the oldest steps.

Params:

| name | type | | description |
|---|---|---|---|
| `undoDepth` | integer |  |  |
| `threads` | integer |  |  |
| `solver` | [Solver](#solver) |  | Complete (ShapeLogic), or Fast (Crowell): Andrew Crowell's take-apart. |
| `slideFolder` | string |  |  |
| `slideDiskGB` | number |  |  |
| `panexFolder` | string |  |  |
| `panexDiskGB` | number |  |  |

Result: Settings

### event `frame`

Announces a binary frame: exactly 'bytes' raw bytes follow this line at once; the reply that refers to it comes after.

| name | type | | description |
|---|---|---|---|
| `frame` | integer | required |  |
| `bytes` | integer | required |  |

#### Settings

| name | type | | description |
|---|---|---|---|
| `undoDepth` | integer | required | Undo steps kept (default 25). |
| `threads` | integer | required | The threads of a solve when its options do not say; 0 every hardware thread (default). |
| `solver` | [Solver](#solver) | required | The solver when a solve's or analysis's options do not say (default classic). |
| `slideFolder` | string | required | Where a full sliding search keeps its older levels while it runs, in a folder of its own that goes when it ends (the original's SHAPELOGIC_SLIDE_DIR); empty keeps them in memory. Default: the user's cache folder's sliding folder. |
| `slideDiskGB` | number | required | The disk those levels may take, in GB (SHAPELOGIC_SLIDE_DISK_GB); 0, the default: all the free space but a margin, and at least a little of it. |
| `panexFolder` | string | required | Where a stacking solve's Panex search saves itself, in a folder per puzzle (the original's SHAPELOGIC_PANEX_DIR): a paused stacking solve is its saved search there, which solve.continue carries on (also after the engine or the document ended), solve.start and solve.abort throw away, solve.status describes, and the paused solve's export and import carry. Empty saves nothing (a search that outgrows its memory then finds its path by solving halves again). Default: the user's cache folder's panex folder. |
| `panexDiskGB` | number | required | The disk a saved search's levels may take, in GB (SHAPELOGIC_PANEX_DISK_GB); 0, the default: half the free space, at most 200 GB. The fuller it gets, the fewer levels are kept and the longer finding the path at the end takes. |

## Document and history

The puzzle being edited (one per engine): new, open, import, save, the Puzzlecad export, grid conversion, the notes, undo and redo. A new document drops every playback and explorer handle.

### `document.new`

A new empty puzzle on a grid; the grid never changes. The open document goes (as with document.open, document.openBytes and solve.importPaused): its solves pause and are waited for, its analyses stop, its handles are dropped, and after the reply its problems whose work the events were reporting get a last solve.state idle and solution.analyseEnded (stopped); the revision counts on.

Params:

| name | type | | description |
|---|---|---|---|
| `grid` | [Grid](#grid) | required | The grid of a puzzle, fixed for its life. |

Result: [Snapshot](#snapshot)

### `document.open`

Opens a .xmpuzzle file (gzip or plain XML; one that inflates to more than 256 MiB is refused as parse_error); what this version does not model is kept. The open document goes as with document.new.

Params:

| name | type | | description |
|---|---|---|---|
| `path` | string | required |  |

Result: [Snapshot](#snapshot)

Errors: `io_error`, `parse_error`

### `document.openBytes`

Opens a puzzle from its file's bytes (gzip or plain XML; gzip that inflates to more than 256 MiB is refused as parse_error), base64; the document has no path unless one is given. The open document goes as with document.new.

Params:

| name | type | | description |
|---|---|---|---|
| `data` | string | required |  |
| `path` | string |  | The file these bytes stand for: the document's path, where document.save writes (a rescue copy of a puzzle that has a file). |
| `dirty` | boolean |  | The document starts with unsaved changes (a rescue copy): dirty until saved. |

Result: [Snapshot](#snapshot)

Errors: `invalid_argument`, `parse_error`

### `document.import`

Imports another program's puzzle file (File ▸ Import ▸ PuzzleSolver3D or Puzzlecad (OpenSCAD)) as a new, untitled document with unsaved changes: a cube-grid puzzle with one problem, its pieces one of each; Save then asks where to write the ShapeLogic file, never over the source. The file is read by its path or from its bytes, exactly one of the two, as it is (never decompressed), at most 256 MiB; one whose shapes would hold more than 2^27 cells together is refused (parse_error), as is one the format's reader cannot take (the original's "Could not load puzzle, sorry!"). The open document goes as with document.new.

Params:

| name | type | | description |
|---|---|---|---|
| `format` | `"puzzleSolver3D"` \| `"puzzlecad"` | required | puzzleSolver3D: a PuzzleSolver3D file (.puz): PIECE and RESULT size lines, each followed by the shape's rows, its layers side by side. puzzlecad: a puzzlecad OpenSCAD file (.scad): the pieces of its burr_plate and burr_piece calls, its first packing_box as the result (else a cube of variable cells). |
| `path` | string |  |  |
| `data` | string |  | The file's bytes, base64. |

Result: [Snapshot](#snapshot)

Errors: `invalid_argument`, `io_error`, `parse_error`

### `document.exportPuzzlecad`

The puzzlecad OpenSCAD file of a problem's pieces (File ▸ Export ▸ Puzzlecad (OpenSCAD)): the distinct shapes of its parts but the result (when that leaves none, every shape but the result), or every shape when no problem is given, each as the layers of its bounding box in one burr_plate, in the layout of puzzlecad's MagellanTIC.scad. The header comment names the document's file, or `name` when it has none ("unknown" without either). Only for cube-grid puzzles with shapes (the original's "Puzzlecad export is only available for cube-grid puzzles.", "Nothing to export."), and some shape must have a cell ("Could not export puzzlecad file."): invalid_argument. Allowed while solving.

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) |  | A problem; also names its solve and its analysis. |
| `name` | string |  | The file being written, for the header when the document has no file. At most 255 bytes, no control characters. |

Result:

| name | type | | description |
|---|---|---|---|
| `text` | string | required |  |

Errors: `invalid_argument`, `not_found`

### `document.convertGrid`

The puzzle on another grid (Edit ▸ Convert brick grid type to other): each cube of a cube-grid puzzle becomes the cells that fill it (5x5x5 on the rhombic grid, 3x3x3 on the tetra-octa grid), the shapes keep their names, the colours and the notes stay, and each problem's definition is copied with its name, without its solutions. As the original, the document is replaced: it has unsaved changes, nothing to undo, and keeps its path, so document.save writes the converted puzzle over the file it came from. The grids a puzzle converts to are engine.info's grids[].convertsTo; another is unsupported. Refused while solving (invalid_state), and when the converted shapes would hold more than 2^27 cells together (invalid_argument). The open document goes as with document.new.

Params:

| name | type | | description |
|---|---|---|---|
| `grid` | [Grid](#grid) | required | The grid of a puzzle, fixed for its life. |

Result: [Snapshot](#snapshot)

Errors: `unsupported`, `invalid_argument`, `invalid_state`

### `document.save`

Saves (gzip, as the original) to the path given, or to the document's path; the document is clean afterwards. Refused while a solve runs: save at a save point (solve.savePoint).

Params:

| name | type | | description |
|---|---|---|---|
| `path` | string |  |  |

Result:

| name | type | | description |
|---|---|---|---|
| `revision` | [Revision](#revision) | required | The document's revision; every change increments it. |
| `path` | string | required |  |

Errors: `invalid_state`, `io_error`, `invalid_argument`

### `document.saveBytes`

The file's bytes (plain XML), base64, without marking the document clean.

Params:

(none)

Result:

| name | type | | description |
|---|---|---|---|
| `data` | string | required |  |

Errors: `invalid_state`

### `document.get`

The document's lists and state.

Params:

(none)

Result: [Snapshot](#snapshot)

### `document.setNotes`

The puzzle's comment. Allowed while solving. Control characters other than tab, newline and carriage return are refused (the file is XML 1.0); a name (shape.rename, problem.rename) refuses line breaks as well, since an attribute would give them back as spaces.

Params:

| name | type | | description |
|---|---|---|---|
| `text` | string | required |  |

Result: [Changed](#changed)

Errors: `invalid_argument`

### `edit.undo`

Undoes the last step. Refused while solving or inside edit.begin / edit.end.

Params:

(none)

Result: [Changed](#changed)

Errors: `invalid_state`

### `edit.redo`

Redoes the last undone step.

Params:

(none)

Result: [Changed](#changed)

Errors: `invalid_state`

### `edit.history`

The labels of the undo and redo steps.

Params:

(none)

Result:

| name | type | | description |
|---|---|---|---|
| `undo` | string[] | required | Oldest first; the last is undone next. |
| `redo` | string[] | required | The first is redone next. |

### `edit.begin`

Groups the following calls into one undo step (a drag across cells); groups do not nest.

Params:

| name | type | | description |
|---|---|---|---|
| `label` | string |  |  |

Result: [Empty](#empty)

Errors: `invalid_state`

### `edit.end`

Ends the group; an empty group leaves no step.

Params:

(none)

Result: [Changed](#changed)

Errors: `invalid_state`

### event `document.changed`

The document changed: after a call that changed it, and when a solve run ended or an analysis was applied. Empty id lists mean the lists themselves changed (or anything of that kind).

| name | type | | description |
|---|---|---|---|
| `revision` | [Revision](#revision) | required | The document's revision; every change increments it. |
| `what` | [ChangeKind](#changekind)[] | required |  |
| `shapes` | [ShapeId](#shapeid)[] | required |  |
| `problems` | [ProblemId](#problemid)[] | required |  |
| `notice` | string |  | What the change did beyond what was asked, to tell the user: a sliding piece whose new shape lost its starts and goals. Only when there is something to tell. |

## Shapes and cells

The shapes of the puzzle, their cells, the tool tabs' operations, the status window, the 3D drawing mesh and the grid editor's layer drawing. Editing shapes is refused while a solve runs (invalid_state, decision 0011); an edit of a shape removes the solutions of the problems that use it.

### `shape.list`

The shapes in list order.

Params:

(none)

Result:

| name | type | | description |
|---|---|---|---|
| `shapes` | [ShapeSummary](#shapesummary)[] | required |  |

### `shape.get`

A shape with its non-empty cells (of one layer, when asked).

Params:

| name | type | | description |
|---|---|---|---|
| `shape` | [ShapeId](#shapeid) | required | A shape, stable while the document is open. |
| `layer` | integer |  | Only the cells with this z (the grid editor's XY plane; the cube editor's XZ and YZ planes are the client's: filter the cells by y or x). |

Result:

| name | type | | description |
|---|---|---|---|
| `shape` | [ShapeSummary](#shapesummary) | required |  |
| `cells` | object[] | required |  |

`cells`:

| name | type | | description |
|---|---|---|---|
| `cells.at` | [Cell](#cell) | required | A cell [x, y, z] in the grid's own coordinates. |
| `cells.state` | [CellState](#cellstate) | required |  |
| `cells.colour` | [ColourId](#colourid) |  | Absent: the shape's own colour. |

Errors: `not_found`

### `shape.create`

Adds an empty shape at the end: of a size in cells, or of the grid's default size. On a stacking puzzle a disc of size 1 (the original's New), and no size may be given (invalid_argument).

Params:

| name | type | | description |
|---|---|---|---|
| `size` | [Size](#size) |  | A size [x, y, z]. |

Result: Made

Errors: `invalid_argument`, `invalid_state`

### `shape.duplicate`

Adds a copy of a shape at the end.

Params:

| name | type | | description |
|---|---|---|---|
| `shape` | [ShapeId](#shapeid) | required | A shape, stable while the document is open. |

Result: Made

Errors: `not_found`, `invalid_state`

### `shape.delete`

Removes a shape; the problems drop it.

Params:

| name | type | | description |
|---|---|---|---|
| `shape` | [ShapeId](#shapeid) | required | A shape, stable while the document is open. |

Result: [Changed](#changed)

Errors: `not_found`, `invalid_state`

### `shape.move`

Moves a shape to a place in the list; labels renumber. On a sliding puzzle the starts and goals follow; a stamped piece moved past the 63rd place (a mark is six bits) loses them, told in document.changed.notice.

Params:

| name | type | | description |
|---|---|---|---|
| `shape` | [ShapeId](#shapeid) | required | A shape, stable while the document is open. |
| `toIndex` | integer | required |  |

Result: [Changed](#changed)

Errors: `not_found`, `invalid_argument`, `invalid_state`

### `shape.rename`

The shape's name (empty for none). On a sliding puzzle a start/goal shape's name is the user's part (its START/GOAL number stays), and a piece's name may not begin with "sg:" or "__sggoal:" (invalid_argument).

Params:

| name | type | | description |
|---|---|---|---|
| `shape` | [ShapeId](#shapeid) | required | A shape, stable while the document is open. |
| `name` | string | required |  |

Result: [Changed](#changed)

Errors: `not_found`, `invalid_argument`, `invalid_state`

### `shape.setWeight`

The weight: heavier shapes stay in place when taken apart.

Params:

| name | type | | description |
|---|---|---|---|
| `shape` | [ShapeId](#shapeid) | required | A shape, stable while the document is open. |
| `weight` | integer | required |  |

Result: [Changed](#changed)

Errors: `not_found`, `invalid_state`

### `shape.setColour`

The shape's own colour, or null for the default one.

Params:

| name | type | | description |
|---|---|---|---|
| `shape` | [ShapeId](#shapeid) | required | A shape, stable while the document is open. |
| `rgb` | [Rgb](#rgb) or null | required |  |

Result: [Changed](#changed)

Errors: `not_found`, `invalid_state`

### `shape.setCells`

Applies a tool to cells, each with the mirror and span options (the grid editor); cells outside the space or not cells of the grid are left out. Several calls inside edit.begin / edit.end are one undo step. Refused (invalid_argument) on a stacking puzzle, whose shapes are discs (stacking.setDiscSize).

Params:

| name | type | | description |
|---|---|---|---|
| `shape` | [ShapeId](#shapeid) | required | A shape, stable while the document is open. |
| `tool` | [CellTool](#celltool) | required | The grid editor's tools; paint changes only the colour of non-empty cells. |
| `colour` | [ColourId](#colourid) |  | The colour put on the cells; absent: none (the shape's own). |
| `mirror` | [Axes](#axes) |  | Mirror or span axes. |
| `span` | [Axes](#axes) |  | Mirror or span axes. |
| `cells` | [Cell](#cell)[] | required |  |

Result: CellsChanged

Errors: `not_found`, `invalid_argument`, `invalid_state`

### `shape.setBox`

The same for every cell of a box, corners included, clamped to the space (the box selection's fill and clear). Refused (invalid_argument) on a stacking puzzle, whose shapes are discs (stacking.setDiscSize).

Params:

| name | type | | description |
|---|---|---|---|
| `shape` | [ShapeId](#shapeid) | required | A shape, stable while the document is open. |
| `tool` | [CellTool](#celltool) | required | The grid editor's tools; paint changes only the colour of non-empty cells. |
| `colour` | [ColourId](#colourid) |  | A constraint colour. |
| `mirror` | [Axes](#axes) |  | Mirror or span axes. |
| `span` | [Axes](#axes) |  | Mirror or span axes. |
| `from` | [Cell](#cell) | required | A cell [x, y, z] in the grid's own coordinates. |
| `to` | [Cell](#cell) | required | A cell [x, y, z] in the grid's own coordinates. |

Result: CellsChanged

Errors: `not_found`, `invalid_argument`, `invalid_state`

### `shape.resize`

Sets the size in the editor's units (engine.info's sizeUnit); shrinking drops cells outside. With toAll the other shapes grow by as much (they never shrink). Sliding shapes are one layer deep: another Z is refused (invalid_argument). Refused (invalid_argument) on a stacking puzzle, whose shapes are discs (stacking.setDiscSize).

Params:

| name | type | | description |
|---|---|---|---|
| `shape` | [ShapeId](#shapeid) | required | A shape, stable while the document is open. |
| `size` | [Size](#size) | required | A size [x, y, z]. |
| `toAll` | boolean |  |  |

Result: [Changed](#changed)

Errors: `not_found`, `invalid_argument`, `invalid_state`

### `shape.taskSet`

The tool tabs' operations the document's grid offers, in the original's button order. The sliding grid leaves out what would give a shape more layers (the nudges along Z, the turns about X and Y, double, triple, the grid scale), as the original greyed them out.

Params:

(none)

Result:

| name | type | | description |
|---|---|---|---|
| `tasks` | object[] | required |  |

`tasks`:

| name | type | | description |
|---|---|---|---|
| `tasks.task` | [ShapeTask](#shapetask) | required | A button of the tool tabs; what the numbered ones do depends on the grid (shape.taskSet gives each one's group and label). |
| `tasks.group` | `"Flip"` \| `"Nudge"` \| `"Rotate"` \| `"Grid"` \| `"Shape"` \| `"Constrain"` \| `"Tools"` | required |  |
| `tasks.label` | string | required | The original's tooltip. |
| `tasks.toAll` | boolean | required | It can be applied to every shape at once. |

### `shape.task`

A tool tab operation on a shape (and, with toAll and a task that allows it, on every shape): flip, rotate, nudge (refused when cells would leave the grid), prune, centre, to origin, minimize, double, triple, fill holes, the surface. Refused (invalid_argument) on a stacking puzzle, whose shapes are discs (stacking.setDiscSize).

Params:

| name | type | | description |
|---|---|---|---|
| `shape` | [ShapeId](#shapeid) | required | A shape, stable while the document is open. |
| `task` | [ShapeTask](#shapetask) | required | A button of the tool tabs; what the numbered ones do depends on the grid (shape.taskSet gives each one's group and label). |
| `toAll` | boolean |  |  |

Result:

| name | type | | description |
|---|---|---|---|
| `revision` | [Revision](#revision) | required | The document's revision; every change increments it. |
| `changed` | boolean | required | The shape changed at all (cells, size or hotspot). |
| `cellsChanged` | integer | required | Cells whose state or colour changed (fill holes: the holes filled). |

Errors: `not_found`, `unsupported`, `invalid_argument`, `invalid_state`

### `shape.info`

The status window's row for one shape, with its status line.

Params:

| name | type | | description |
|---|---|---|---|
| `shape` | [ShapeId](#shapeid) | required | A shape, stable while the document is open. |

Result: [ShapeInfo](#shapeinfo)

Errors: `not_found`

### `shape.table`

The status window: a row per shape, in list order.

Params:

(none)

Result:

| name | type | | description |
|---|---|---|---|
| `rows` | [ShapeInfo](#shapeinfo)[] | required |  |

### `shape.mesh`

The shape's mesh as the original's 3D view drew it, as a binary frame: in its own orientation, or turned by a transformation of the grid (a Placement's), which is how an assembled solution, a placement of the assembly explorer or a position of the movement explorer is drawn: the mesh of the piece's transformation, at its cell minus the turned shape's hotspot (MeshFrame.hotspot). Allowed while solving.

Params:

| name | type | | description |
|---|---|---|---|
| `shape` | [ShapeId](#shapeid) | required | A shape, stable while the document is open. |
| `style` | [MeshStyle](#meshstyle) |  | Default drawing. |
| `transformation` | integer |  | A number of the grid's symmetry table, mirrors included (bricks 0 to 47); absent or 0 is the shape as it is. Beyond the table: invalid_argument. |

Result: [MeshFrame](#meshframe)

Errors: `not_found`, `invalid_argument`

### `grid.geometry`

The grid editor's drawing of one layer of a space of the document's grid (at most 65536 cells a layer): each cell's outline (a circle on the sphere grid) and the cell across each edge. Units: a cube 1 by 1, prism triangles of side 1, spheres 1 apart, rhombic 5 by 5 cells and tetrahedra-octahedra 3 by 3 cells to 1 by 1.

Params:

| name | type | | description |
|---|---|---|---|
| `size` | [Size](#size) | required | A size [x, y, z]. |
| `layer` | integer | required |  |

Result:

| name | type | | description |
|---|---|---|---|
| `width` | number | required |  |
| `height` | number | required |  |
| `cells` | object[] | required |  |

`cells`:

| name | type | | description |
|---|---|---|---|
| `cells.at` | [Cell](#cell) | required | A cell [x, y, z] in the grid's own coordinates. |
| `cells.polygon` | [Point2](#point2)[] |  | Counter-clockwise, y up. |
| `cells.neighbours` | integer[] |  | Per edge (corner i to i + 1): the index in cells of the cell across it, -1 for none. |
| `cells.circle` | object |  |  |

`cells.circle`:

| name | type | | description |
|---|---|---|---|
| `cells.circle.centre` | [Point2](#point2) | required | A point [x, y] of a layer drawing, y up. |
| `cells.circle.radius` | number | required |  |

Errors: `invalid_argument`

### `grid.neighbour`

The cell across a side of a cell, in a space of the document's grid (the 3D view's tools that add a cell at a face): the sides are numbered as a MeshFrame's voxelFace (Voxel::getNeighbor, faces). Null when the cell across is outside the space. A cell outside the space or not a cell of the grid, or a side the cell does not have: invalid_argument.

Params:

| name | type | | description |
|---|---|---|---|
| `size` | [Size](#size) | required | A size [x, y, z]. |
| `cell` | [Cell](#cell) | required | A cell [x, y, z] in the grid's own coordinates. |
| `face` | integer | required | The side, a MeshFrame's voxelFace. |

Result:

| name | type | | description |
|---|---|---|---|
| `cell` | [Cell](#cell) or null | required | The cell across the side, or null when it is outside the space. |

Errors: `invalid_argument`

#### ShapeParam

| name | type | | description |
|---|---|---|---|
| `shape` | [ShapeId](#shapeid) | required | A shape, stable while the document is open. |

#### Made

| name | type | | description |
|---|---|---|---|
| `shape` | [ShapeId](#shapeid) | required | A shape, stable while the document is open. |
| `revision` | [Revision](#revision) | required | The document's revision; every change increments it. |

#### CellsChanged

| name | type | | description |
|---|---|---|---|
| `revision` | [Revision](#revision) | required | The document's revision; every change increments it. |
| `changed` | integer | required | Cells changed; 0 leaves no undo step. |

## Colours

The constraint colours: cells carry them, problems have rules about which piece colour may sit on which result colour.

### `colour.list`

The colours in list order.

Params:

(none)

Result:

| name | type | | description |
|---|---|---|---|
| `colours` | [Colour](#colour)[] | required |  |

### `colour.add`

Adds a colour (at most 63); every problem allows it on itself.

Params:

| name | type | | description |
|---|---|---|---|
| `rgb` | [Rgb](#rgb) | required | A colour, #rrggbb (the engine writes lower case). |

Result:

| name | type | | description |
|---|---|---|---|
| `colour` | [ColourId](#colourid) | required | A constraint colour. |
| `revision` | [Revision](#revision) | required | The document's revision; every change increments it. |

Errors: `invalid_argument`, `invalid_state`

### `colour.edit`

Changes how a colour looks. Allowed while solving.

Params:

| name | type | | description |
|---|---|---|---|
| `colour` | [ColourId](#colourid) | required | A constraint colour. |
| `rgb` | [Rgb](#rgb) | required | A colour, #rrggbb (the engine writes lower case). |

Result: [Changed](#changed)

Errors: `not_found`

### `colour.delete`

Removes a colour: its cells revert to the shape's own colour, the colours after it move up, the problems with shapes using it lose their solutions. Refused (invalid_argument) on a sliding puzzle, whose cell colours are its start marks and locks.

Params:

| name | type | | description |
|---|---|---|---|
| `colour` | [ColourId](#colourid) | required | A constraint colour. |

Result: [Changed](#changed)

Errors: `not_found`, `invalid_argument`, `invalid_state`

## Problems

The problems: a result shape, pieces with counts or ranges, groups, colour rules. A problem being solved cannot be edited (invalid_state); editing one ends its paused solve, which carries on from its saved position (decision 0011). A sliding puzzle has one problem per start/goal shape, kept in step with its starts and goals (sliding.json): its result is not chosen (problem.setResult is refused), it has no groups (refused), and a piece with a start or goal keeps its count (Part.required). A stacking problem stands on a rod set and holds its discs on the rods at its start and goal (stacking.json; problem.get's stacks): its result, piece counts and groups are refused, the stacking commands set them.

### `problem.list`

The problems in list order.

Params:

(none)

Result:

| name | type | | description |
|---|---|---|---|
| `problems` | [ProblemSummary](#problemsummary)[] | required |  |

### `problem.get`

A problem in full (read under the solve's lock while it runs).

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |

Result: [ProblemDetail](#problemdetail)

Errors: `not_found`

### `problem.create`

Adds a problem; it allows each colour on itself. Refused (invalid_argument) on a sliding puzzle, whose problems follow its start/goal shapes (sliding.addStartGoal).

Params:

(none)

Result: Made

Errors: `invalid_argument`, `invalid_state`

### `problem.duplicate`

Adds a copy of a problem's definition (no name, no solutions). Refused (invalid_argument) on a sliding puzzle (shape.duplicate of the start/goal shape instead).

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |

Result: Made

Errors: `not_found`, `invalid_argument`, `invalid_state`

### `problem.delete`

Removes a problem. Refused (invalid_argument) on a sliding puzzle (shape.delete of the start/goal shape instead).

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |

Result: [Changed](#changed)

Errors: `not_found`, `invalid_argument`, `invalid_state`

### `problem.move`

Moves a problem to a place in the list.

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `toIndex` | integer | required |  |

Result: [Changed](#changed)

Errors: `not_found`, `invalid_argument`, `invalid_state`

### `problem.rename`

The problem's name.

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `name` | string | required |  |

Result: [Changed](#changed)

Errors: `not_found`, `invalid_argument`, `invalid_state`

### `problem.setResult`

Makes a shape the result (it stops being a piece); a different result removes the solutions. Refused (invalid_argument) on a stacking puzzle, whose problems stand on a rod set instead (stacking.setProblemRodSet).

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `shape` | [ShapeId](#shapeid) | required | A shape, stable while the document is open. |

Result: [Changed](#changed)

Errors: `not_found`, `invalid_argument`, `invalid_state`

### `problem.setPieceCount`

The count range of a shape as pieces; max 0 removes it. The solutions stay, the counts become unknown. Refused (invalid_argument) on a stacking puzzle, whose pieces follow its stacks (stacking.json).

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `shape` | [ShapeId](#shapeid) | required | A shape, stable while the document is open. |
| `min` | integer | required |  |
| `max` | integer | required |  |

Result: [Changed](#changed)

Errors: `not_found`, `invalid_argument`, `invalid_state`

### `problem.addPiece`

One more of a shape (minimum and maximum). Refused (invalid_argument) on a stacking puzzle, whose pieces follow its stacks (stacking.json).

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `shape` | [ShapeId](#shapeid) | required | A shape, stable while the document is open. |

Result: [Changed](#changed)

Errors: `not_found`, `invalid_argument`, `invalid_state`

### `problem.removePiece`

One fewer of a shape. Refused (invalid_argument) on a stacking puzzle, whose pieces follow its stacks (stacking.json).

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `shape` | [ShapeId](#shapeid) | required | A shape, stable while the document is open. |

Result: [Changed](#changed)

Errors: `not_found`, `invalid_argument`, `invalid_state`

### `problem.setRangeAll`

One count range for every piece (the bulk range; a maximum below the minimum is raised to it). Removes the solutions. Refused (invalid_argument) on a stacking puzzle, whose pieces follow its stacks (stacking.json).

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `min` | integer | required |  |
| `max` | integer | required |  |

Result: [Changed](#changed)

Errors: `not_found`, `invalid_argument`, `invalid_state`

### `problem.addOneOfEach`

One more of every shape but the result. Refused (invalid_argument) on a stacking puzzle, whose pieces follow its stacks (stacking.json).

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |

Result: [Changed](#changed)

Errors: `not_found`, `invalid_argument`, `invalid_state`

### `problem.removeAllPieces`

Removes every piece and the solutions. Refused (invalid_argument) on a stacking puzzle, whose pieces follow its stacks (stacking.json).

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |

Result: [Changed](#changed)

Errors: `not_found`, `invalid_argument`, `invalid_state`

### `problem.movePart`

Moves a shape's part one place earlier (-1) or later (+1): the pieces renumber. Refused (invalid_argument) on a stacking puzzle, whose pieces follow its stacks (stacking.json).

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `shape` | [ShapeId](#shapeid) | required | A shape, stable while the document is open. |
| `delta` | `-1` \| `1` | required |  |

Result: [Changed](#changed)

Errors: `not_found`, `invalid_argument`, `invalid_state`

### `problem.setGroup`

Puts all pieces of a shape in one exclusive group, or none (0). Refused (invalid_argument) on sliding and stacking puzzles, which have no groups.

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `shape` | [ShapeId](#shapeid) | required | A shape, stable while the document is open. |
| `group` | integer | required |  |

Result: [Changed](#changed)

Errors: `not_found`, `invalid_argument`, `invalid_state`

### `problem.setGroupCount`

Puts 'count' pieces of a shape in group 'group' (0 takes them out): the groups editor. Refused (invalid_argument) on sliding and stacking puzzles, which have no groups.

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `shape` | [ShapeId](#shapeid) | required | A shape, stable while the document is open. |
| `group` | integer | required |  |
| `count` | integer | required |  |

Result: [Changed](#changed)

Errors: `not_found`, `invalid_argument`, `invalid_state`

### `problem.deleteGroup`

Removes a group from every shape. Refused (invalid_argument) on sliding and stacking puzzles, which have no groups.

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `group` | integer | required |  |

Result: [Changed](#changed)

Errors: `not_found`, `invalid_argument`, `invalid_state`

### `problem.setColourRule`

Allows or forbids piece colour on result colour; removes the solutions.

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `pieceColour` | [ColourId](#colourid) | required | A constraint colour. |
| `resultColour` | [ColourId](#colourid) | required | A constraint colour. |
| `allowed` | boolean | required |  |

Result: [Changed](#changed)

Errors: `not_found`, `invalid_state`

### `problem.setColourRules`

Sets every colour rule at once: each colour on itself, all on all, or none.

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `preset` | `"eachOnItself"` \| `"allowAll"` \| `"clear"` | required |  |

Result: [Changed](#changed)

Errors: `not_found`, `invalid_state`

### `problem.fit`

Whether the pieces can fill the result, and the original's status line.

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |

Result: [FitStats](#fitstats)

Errors: `not_found`

### `problem.kind`

Brick, sliding or stacking; this version solves all three (supported is always true, kept for clients that read it).

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |

Result:

| name | type | | description |
|---|---|---|---|
| `kind` | `"brick"` \| `"sliding"` \| `"stacking"` | required |  |
| `supported` | boolean | required |  |

Errors: `not_found`

### `problem.setSolverSettings`

Stores the Solver tab's settings with the problem (saved in the file; the original's cb_SolverOptions). Replaces them all. Not while the problem is being solved.

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `settings` | [SolverSettings](#solversettings) | required |  |

Result: [Changed](#changed)

Errors: `not_found`, `invalid_state`

#### ProblemParam

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |

#### ProblemShape

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `shape` | [ShapeId](#shapeid) | required | A shape, stable while the document is open. |

#### Made

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `revision` | [Revision](#revision) | required | The document's revision; every change increments it. |

## Sliding puzzles

A sliding puzzle's start/goal shapes and the starts and goals stamped on them (decision 0014; the original's Start/Goal tab). A start/goal shape is a tray: its empty cells are walls, its variable cells corridors a piece may cross but not stop on (both edited with shape.setCells); each piece may have a start and a goal on it, the whole piece at a time. Each start/goal shape has a problem of its own (named START/GOAL n), whose parts and colour locks follow its stamps; the shape commands keep the stamps in step (shape.delete, shape.move, shape.duplicate, shape.rename, and an edit that changes a piece's shape takes its stamps off). Refused (unsupported) on other grids.

### `sliding.addStartGoal`

Adds a start/goal shape (the original's New Start/Goal): one layer of 'size' cells (default 6 by 6, the original took the selected shape's), all floor, numbered after the others, with its problem.

Params:

| name | type | | description |
|---|---|---|---|
| `size` | Size2 |  | A size [x, y] in cells. |

Result:

| name | type | | description |
|---|---|---|---|
| `shape` | [ShapeId](#shapeid) | required | A shape, stable while the document is open. |
| `revision` | [Revision](#revision) | required | The document's revision; every change increments it. |

Errors: `unsupported`, `invalid_argument`, `invalid_state`

### `sliding.place`

The start/goal editor's click: the whole of a piece as its start (goal false) or its goal, its top-left cell at 'at' (y rises upwards, as in the grid editor), moved there if it was elsewhere; a piece that is exactly there already is taken off. Refused (invalid_argument) with the original's reason when it does not fit: off the floor, on another piece's start or goal, a goal on a corridor. The problems of the start/goal shape lose what they had found. One undo step.

Params:

| name | type | | description |
|---|---|---|---|
| `tray` | [ShapeId](#shapeid) | required | A shape, stable while the document is open. |
| `piece` | [ShapeId](#shapeid) | required | A shape, stable while the document is open. |
| `at` | Cell2 | required | A cell [x, y] of a start/goal shape's layer, y up. |
| `goal` | boolean | required |  |

Result:

| name | type | | description |
|---|---|---|---|
| `placed` | boolean | required | The piece is there now (false: it was there and was taken off). |
| `revision` | [Revision](#revision) | required | The document's revision; every change increments it. |

Errors: `not_found`, `unsupported`, `invalid_argument`, `invalid_state`

### `sliding.clearAt`

Takes off the start or goal that covers a cell (the editor's right click). One undo step; nothing when none covers it.

Params:

| name | type | | description |
|---|---|---|---|
| `tray` | [ShapeId](#shapeid) | required | A shape, stable while the document is open. |
| `at` | Cell2 | required | A cell [x, y] of a start/goal shape's layer, y up. |
| `goal` | boolean | required |  |

Result:

| name | type | | description |
|---|---|---|---|
| `cleared` | boolean | required |  |
| `revision` | [Revision](#revision) | required | The document's revision; every change increments it. |

Errors: `not_found`, `unsupported`, `invalid_argument`, `invalid_state`

### `sliding.tray`

What a start/goal shape holds: its number and name, each cell's start and goal (the piece stamped there, cells indexed x + X*(y + Y*z)), whether it can be solved (the original's validity bar: null, or why not), the stamps larger than their pieces (null, or the message), and its floor cells.

Params:

| name | type | | description |
|---|---|---|---|
| `tray` | [ShapeId](#shapeid) | required | A shape, stable while the document is open. |

Result:

| name | type | | description |
|---|---|---|---|
| `number` | integer | required |  |
| `name` | string | required | The user's part of its name; shown as '(START/GOAL n) name'. |
| `size` | [Size](#size) | required | A size [x, y, z]. |
| `starts` | [ShapeId](#shapeid) or null[] | required |  |
| `goals` | [ShapeId](#shapeid) or null[] | required |  |
| `error` | string or null | required |  |
| `overflow` | string or null | required |  |
| `floor` | integer | required |  |

Errors: `not_found`, `unsupported`, `invalid_argument`

### `sliding.preview`

Where sliding.place would put a piece: the cells, whether it fits (and the original's reason when not), and whether it is exactly there already (then the click takes it off). Changes nothing.

Params:

| name | type | | description |
|---|---|---|---|
| `tray` | [ShapeId](#shapeid) | required | A shape, stable while the document is open. |
| `piece` | [ShapeId](#shapeid) | required | A shape, stable while the document is open. |
| `at` | Cell2 | required | A cell [x, y] of a start/goal shape's layer, y up. |
| `goal` | boolean | required |  |

Result:

| name | type | | description |
|---|---|---|---|
| `cells` | Cell2[] | required |  |
| `fits` | boolean | required |  |
| `why` | string or null | required |  |
| `there` | boolean | required |  |

Errors: `not_found`, `unsupported`, `invalid_argument`

## Stacking puzzles

A stacking puzzle's rod sets, discs and stacks (decision 0015; the original's Rods panel, disc list and the stacking branches of its Problems tab). Its shapes are discs: shape.create makes one of size 1, stacking.setDiscSize sizes it; the cell editor and the tool tabs are refused, as the original hid them. A puzzle has one rod set at least; a problem stands on one (its rodSet) and holds the discs of each rod at its start and its goal (problem.get's stacks); its pieces are the discs on them, so the Problems tab's brick commands (problem.setResult, the counts and ranges, problem.movePart, the groups) are refused and these are used instead. Each command is one undo step; a rod set is not changed while anything is solved, a problem's stacks not while it is. Refused (unsupported) on other grids.

### `stacking.addRodSet`

Adds a rod set with the default rules (the original's New: three rods that grow, size matters), unnamed, at the end. One undo step.

Params:

(none)

Result:

| name | type | | description |
|---|---|---|---|
| `rodSet` | [RodSetId](#rodsetid) | required | A stacking puzzle's rod set (a board), stable while the document is open. |
| `revision` | [Revision](#revision) | required | The document's revision; every change increments it. |

Errors: `unsupported`, `invalid_state`

### `stacking.duplicateRodSet`

Adds a copy of a rod set, its name and rules, at the end (the original's Copy).

Params:

| name | type | | description |
|---|---|---|---|
| `rodSet` | [RodSetId](#rodsetid) | required | A stacking puzzle's rod set (a board), stable while the document is open. |

Result:

| name | type | | description |
|---|---|---|---|
| `rodSet` | [RodSetId](#rodsetid) | required | A stacking puzzle's rod set (a board), stable while the document is open. |
| `revision` | [Revision](#revision) | required | The document's revision; every change increments it. |

Errors: `unsupported`, `not_found`, `invalid_state`

### `stacking.deleteRodSet`

Removes a rod set (the original's Delete); the problems on it stand on none afterwards, their stacks kept. The last one is kept (invalid_argument: 'Keep at least one rod set').

Params:

| name | type | | description |
|---|---|---|---|
| `rodSet` | [RodSetId](#rodsetid) | required | A stacking puzzle's rod set (a board), stable while the document is open. |

Result: [Changed](#changed)

Errors: `unsupported`, `not_found`, `invalid_argument`, `invalid_state`

### `stacking.renameRodSet`

Names a rod set (the original's Label); names cannot hold line breaks or control characters.

Params:

| name | type | | description |
|---|---|---|---|
| `rodSet` | [RodSetId](#rodsetid) | required | A stacking puzzle's rod set (a board), stable while the document is open. |
| `name` | string | required |  |

Result: [Changed](#changed)

Errors: `unsupported`, `not_found`, `invalid_argument`, `invalid_state`

### `stacking.moveRodSet`

Moves a rod set to a place in the list, one place at a time as the original's arrows did; labels renumber, the problems keep their board.

Params:

| name | type | | description |
|---|---|---|---|
| `rodSet` | [RodSetId](#rodsetid) | required | A stacking puzzle's rod set (a board), stable while the document is open. |
| `toIndex` | integer | required |  |

Result: [Changed](#changed)

Errors: `unsupported`, `not_found`, `invalid_argument`, `invalid_state`

### `stacking.setRules`

Sets a rod set's rules (the Rods panel's fields): the fields given, over the rod set's as shown (RodSet). The pocket column needs Panex columns, the outside channel Panex columns and three rods or more: without them they are off. The problems on the rod set get its rods in their stacks (discs on rods past the count are kept, setupError tells) and lose their solutions. Refused while anything is solved. One undo step.

Params:

| name | type | | description |
|---|---|---|---|
| `rodSet` | [RodSetId](#rodsetid) | required | A stacking puzzle's rod set (a board), stable while the document is open. |
| `rules` | Rules | required |  |

Result: [Changed](#changed)

Errors: `unsupported`, `not_found`, `invalid_state`

### `stacking.setDiscSize`

A disc's size (the disc list's size field, 1 to 100): the disc is made anew, a square of 2 size + 1 cells one cell thick with its centre empty, its colour kept. Solutions stay, as the original kept them. One undo step.

Params:

| name | type | | description |
|---|---|---|---|
| `shape` | [ShapeId](#shapeid) | required | A shape, stable while the document is open. |
| `size` | integer | required |  |

Result: [Changed](#changed)

Errors: `unsupported`, `not_found`, `invalid_argument`, `invalid_state`

### `stacking.setProblemRodSet`

The board a problem stands on (the original's Set Start/Goal Rods); its stacks get the board's rods. Its solutions stay.

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `rodSet` | [RodSetId](#rodsetid) | required | A stacking puzzle's rod set (a board), stable while the document is open. |

Result: [Changed](#changed)

Errors: `unsupported`, `not_found`, `invalid_state`

### `stacking.placeDisc`

Puts a copy of a disc on top of a rod of the start or the goal (the Problems tab's Add): the next copy not on that stacking, or one copy more when every copy is (the count of the disc follows). The rules are not checked here, so a stacking can be built step by step: stacking.board's error tells what breaks them. Refused (invalid_argument) for a problem without a rod set ('Choose a rod set with Set Start/Goal Rods') and a rod not on the board.

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `shape` | [ShapeId](#shapeid) | required | A shape, stable while the document is open. |
| `rod` | integer | required | A rod of the problem's stack map (the pocket column is the last). |
| `goal` | boolean | required | The goal's stacking; false the start's. |

Result: [Changed](#changed)

Errors: `unsupported`, `not_found`, `invalid_argument`, `invalid_state`

### `stacking.placeAll`

Puts a copy of every disc on a rod, largest first so a size rule can still stack them (the Problems tab's Add all); what went before a refusal stays.

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `rod` | integer | required | A rod of the problem's stack map (the pocket column is the last). |
| `goal` | boolean | required | The goal's stacking; false the start's. |

Result:

| name | type | | description |
|---|---|---|---|
| `placed` | integer | required |  |
| `error` | string or null | required | Why the next disc did not go; null when all went. |
| `revision` | [Revision](#revision) | required | The document's revision; every change increments it. |

Errors: `unsupported`, `not_found`, `invalid_argument`, `invalid_state`

### `stacking.removeDisc`

Takes a disc off (the Problems tab's Remove): that copy goes from the start and the goal, the later copies renumbered, one copy fewer. Refused (invalid_argument) on an empty rod ('That rod is empty').

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `goal` | boolean | required | The goal's stacking; false the start's. |
| `rod` | integer | required | A rod of the problem's stack map (the pocket column is the last). |
| `index` | integer | required | The disc's place on the rod, from the bottom (0). |

Result: [Changed](#changed)

Errors: `unsupported`, `not_found`, `invalid_argument`, `invalid_state`

### `stacking.clear`

Empties the start's or the goal's rods (the Problems tab's Remove all); the counts stay, shared by both. The problem loses its solutions. Nothing to do on an empty stacking.

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `goal` | boolean | required | The goal's stacking; false the start's. |

Result: [Changed](#changed)

Errors: `unsupported`, `not_found`, `invalid_state`

### `stacking.moveDisc`

Swaps a disc with the one above (delta 1) or below (-1) it on its rod (the Problems tab's arrows). Its solutions stay.

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `goal` | boolean | required | The goal's stacking; false the start's. |
| `rod` | integer | required | A rod of the problem's stack map (the pocket column is the last). |
| `index` | integer | required | The disc's place on the rod, from the bottom (0). |
| `delta` | integer | required |  |

Result:

| name | type | | description |
|---|---|---|---|
| `index` | integer | required | The disc's place now. |
| `revision` | [Revision](#revision) | required | The document's revision; every change increments it. |

Errors: `unsupported`, `not_found`, `invalid_argument`, `invalid_state`

### `stacking.board`

A problem's board as the Problems tab showed it: its rods (names, the pocket, capacities), the start's or the goal's stacks, where the rods and discs stand, the validity bar's sentence, and the centre a playback's poses are placed around (playback.json: a stacking solution is centred on its board).

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `goal` | boolean | required | The goal's stacking; false the start's. |

Result:

| name | type | | description |
|---|---|---|---|
| `rodSet` | [RodSetId](#rodsetid) or null | required |  |
| `rods` | Rod[] | required | The board's rods in the stack map's order; empty without a rod set. |
| `stacks` | [Stacks](#stacks) | required | Each rod of a stack map, its discs bottom to top; rods past the board's count are kept while they hold discs. |
| `layout` | [BoardLayout](#boardlayout) or null | required | Null without a rod set. |
| `error` | string or null | required | Why the problem cannot be solved as it stands (the original's setupError: a rule broken, a disc missing from the start or the goal, no rod set); null when it is ready. |
| `centre` | number[] or null | required | The middle of the board, half the rods' height up, in cells; null without a rod set. |

Errors: `unsupported`, `not_found`

### `stacking.rodSetLayout`

A rod set drawn alone, without discs (the Entities tab's view of it): rods that grow drawn five high, spaced for the puzzle's largest disc.

Params:

| name | type | | description |
|---|---|---|---|
| `rodSet` | [RodSetId](#rodsetid) | required | A stacking puzzle's rod set (a board), stable while the document is open. |

Result: [BoardLayout](#boardlayout)

Errors: `not_found`

#### Rules

A rod set's rules to change (RodSet's fields; absent ones stay).

| name | type | | description |
|---|---|---|---|
| `rodCount` | integer |  |  |
| `growHeight` | boolean |  |  |
| `definedHeight` | integer |  |  |
| `sizeMatters` | boolean |  |  |
| `distanceMatters` | boolean |  |  |
| `panexColumns` | boolean |  |  |
| `pocketColumn` | boolean |  |  |
| `pocketHeight` | integer |  |  |
| `outsideChannel` | boolean |  |  |

#### Rod

| name | type | | description |
|---|---|---|---|
| `name` | string | required | 'rod 2' or 'the pocket column', as the original's messages name it. |
| `pocket` | boolean | required |  |
| `position` | integer | required | Where it stands along the board: rod r at r, the pocket at -1. |
| `capacity` | integer | required | The discs it holds: a Panex column one more than its height. |

## Solving

One solve per problem (decision 0010), named by the problem's id. Starting calls return at once; what happens comes as events. A sliding problem is solved as the original's solve thread did: the assembler finds the start layouts, and each is searched to the goal (decision 0014). A stacking problem too (decision 0015): one search from its start stacking to its goal, its path the one solution; a paused stacking solve is its search saved in the engine's panexFolder, which outlives the run, the document and the engine: solve.continue carries it on (also on a reopened file), solve.start and solve.abort throw it away, solve.status describes it (savedSearch), and the paused solve's export and import carry it.

### `solve.start`

Solves from the start, dropping what the problem holds of earlier solving (a stacking problem's saved search too: the original's Solve asked first, solve.status.savedSearch tells it). Refused (invalid_argument) with the original's message when the problem cannot be solved as it stands (no result, no pieces; a stacking problem without a rod set, with neither disassemble nor justCount, or whose stacks break its rules), and (unsupported) when its grid or solver cannot take it.

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `options` | [SolveOptions](#solveoptions) |  |  |

Result: [Changed](#changed)

Errors: `not_found`, `invalid_argument`, `invalid_state`, `unsupported`

### `solve.prepare`

Drops what the problem holds, builds the search (placements, reduce) and pauses. A stacking search has nothing to build (unsupported).

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `options` | [SolveOptions](#solveoptions) |  |  |

Result: [Changed](#changed)

Errors: `not_found`, `invalid_argument`, `invalid_state`, `unsupported`

### `solve.continue`

Carries on a paused solve, one saved in the file, a stacking problem's saved search, or an unsolved problem from the start.

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `options` | [SolveOptions](#solveoptions) |  |  |

Result: [Changed](#changed)

Errors: `not_found`, `invalid_argument`, `invalid_state`, `unsupported`

### `solve.pause`

Asks the run to pause where it can be continued; nothing is lost. Does not wait: solve.state 'paused' follows. Refused when no run is under way.

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |

Result: [Empty](#empty)

Errors: `not_found`, `invalid_state`

### `solve.savePoint`

Asks the assembly search alone to pause where the solve can be saved; the take-aparts under way finish, then 'paused'. The autosave: save, then solve.continue. Refused (unsupported) on a stacking solve, which the original never autosaved: its search saves itself every 20 minutes (StackingOptions.autosave) and when it stops.

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |

Result: [Empty](#empty)

Errors: `not_found`, `invalid_state`, `unsupported`

### `solve.abort`

The hard stop of a solve under way (a run active, or paused in memory): throws it away (results, paused state, saved position) and waits for the run to end. Refused (invalid_state) when the problem has no such solve, so a finished run's solutions are never lost by a late click: remove them with solution.delete {which: all}, which can be undone; a paused solve from a file is carried on with solve.continue or started over with solve.start. Two document.changed follow: the abort's own, then the run's end from the poll. A stacking problem whose search is saved (solve.status.savedSearch) has a solve under way though no run is in memory, as the original's Abort judged it: the search and the problem's results go, and one document.changed follows.

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |

Result: [Changed](#changed)

Errors: `not_found`, `invalid_state`

### `solve.status`

Where the solve stands, its statistics, and why it failed.

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |

Result:

| name | type | | description |
|---|---|---|---|
| `state` | [SolveState](#solvestate) | required | decision 0004; 'stopping' is an active state with a stop requested (SolveProgress.stage). |
| `progress` | [SolveProgress](#solveprogress) | required |  |
| `stats` | [SolveStats](#solvestats) | required |  |
| `failure` | [SolveFailure](#solvefailure) or null | required |  |
| `note` | string or null | required | What a sliding or stacking solve's last run said when it was through, or a stacking search when it paused: whether a missing solution is proven, the search gave up at a limit, or where it was saved (the original's solver note, shown in a message box); null otherwise. |
| `savedSearch` | object or null | required | Null while the problem's run is active, when there is none, and on other grids. |

Errors: `not_found`

### `solve.exportPaused`

Writes a paused solve as a .btsolve file (the original's BTSOLVE2: the puzzle and the problem's number) to carry it on elsewhere. A stacking problem is paused when its search is saved (solve.status.savedSearch): the file carries the search, packed.

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `path` | string | required |  |
| `withLevels` | boolean |  | A stacking search's saved levels go into the file too (default true): left out, the file is far smaller, but finding the path at the end takes longer. |

Result: [Empty](#empty)

Errors: `not_found`, `invalid_state`, `io_error`

### `solve.importPaused`

Opens a .btsolve file as a new, untitled document (the open one goes as with document.new); solve.continue on the problem carries the solve on. A stacking search it carries goes into its folder in the engine's panexFolder, replacing what was there; without a panexFolder such a file is refused (parse_error), and so is any import (invalid_state) while the open document is a stacking puzzle being solved, whose search would write the same folder as it stops.

Params:

| name | type | | description |
|---|---|---|---|
| `path` | string | required |  |

Result:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `document` | [Snapshot](#snapshot) | required |  |

Errors: `io_error`, `parse_error`, `invalid_state`

### event `solve.state`

A solve's state changed, as the engine's poll saw it: states a run passes through within one poll (50 ms) are skipped, the last state of a run (finished, paused, error, or idle after an abort) is always sent, and it comes before the document.changed that gives the run's end its revision. A client waits for that revision, not the state, before reloading the lists. The problems of a document that is replaced get a last 'idle' after the replacing call's reply.

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `state` | [SolveState](#solvestate) | required | decision 0004; 'stopping' is an active state with a stop requested (SolveProgress.stage). |

### event `solve.progress`

Where a running solve stands; at most four times a second, and once when a run ends (after its last solve.state). The live counters of the Solver card (assemblies, solutions, time) come from here.

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `state` | [SolveState](#solvestate) | required | decision 0004; 'stopping' is an active state with a stop requested (SolveProgress.stage). |
| `progress` | [SolveProgress](#solveprogress) | required |  |

### event `solve.solutionsAdded`

The saved solutions changed while solving (new ones, or the list thinned or re-sorted): page them again. At most four times a second, and once when the run ends; the last one of a run carries the final count. The counts are those at the time; the live counters are solve.progress's.

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `saved` | integer | required |  |
| `solutionsVersion` | [SolutionsVersion](#solutionsversion) | required | A counter the problem moves on every change of its saved list (a solution added, removed, re-sorted or edited), kept while the document is open: solution.page and solve.solutionsAdded carry it, so pages read at one value belong together and a client knows when to page again. |
| `assemblies` | integer | required |  |
| `solutions` | integer | required |  |

### event `solve.error`

A run ended in error.

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `failure` | [SolveFailure](#solvefailure) | required |  |

#### ProblemParam

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |

#### Launch

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `options` | [SolveOptions](#solveoptions) |  |  |

## Solutions

A problem's saved solutions: paged and sorted by the engine (a view: the stored order does not change), removed, taken apart afterwards, turned into shapes.

### `solution.page`

A slice of the saved solutions, sorted: list (the stored order), assembly (found order), level, moves or pieces (the original's sort buttons); descending sorts by the swapped comparison, so ties keep their order.

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `sortBy` | `"list"` \| `"assembly"` \| `"level"` \| `"moves"` \| `"pieces"` |  |  |
| `descending` | boolean |  |  |
| `offset` | integer |  |  |
| `limit` | integer |  | Default 100. |

Result:

| name | type | | description |
|---|---|---|---|
| `total` | integer | required |  |
| `offset` | integer | required |  |
| `solutionsVersion` | [SolutionsVersion](#solutionsversion) | required | A counter the problem moves on every change of its saved list (a solution added, removed, re-sorted or edited), kept while the document is open: solution.page and solve.solutionsAdded carry it, so pages read at one value belong together and a client knows when to page again. |
| `rows` | [SolutionRow](#solutionrow)[] | required |  |

Errors: `not_found`

### `solution.get`

A solution: its row and the placement of every piece.

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `solution` | [SolutionId](#solutionid) | required | A saved solution, unique within its problem. |

Result:

| name | type | | description |
|---|---|---|---|
| `row` | [SolutionRow](#solutionrow) | required |  |
| `placements` | [Placement](#placement)[] | required |  |

Errors: `not_found`

### `solution.delete`

Removes saved solutions: one, all before or after one (in the stored order), all without a disassembly, or all.

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `which` | `"one"` \| `"before"` \| `"after"` \| `"withoutDisassembly"` \| `"all"` | required |  |
| `solution` | [SolutionId](#solutionid) |  | Needed for one, before and after. |

Result: [Changed](#changed)

Errors: `not_found`, `invalid_argument`, `invalid_state`

### `solution.deleteDisassembly`

Drops the moves of one solution (or of all), keeping their level summary.

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `solution` | [SolutionId](#solutionid) |  | Absent: every solution. |

Result: [Changed](#changed)

Errors: `not_found`, `invalid_state`

### `solution.toShapes`

Turns assemblies into new shapes (Convert assemblies to pieces), filtered, optionally added as pieces to a new or an existing problem.

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `solution` | [SolutionId](#solutionid) |  | Absent: every saved solution. |
| `dropDisconnected` | boolean |  |  |
| `dropMirror` | boolean |  | Shapes whose symmetry contains a mirror. |
| `dropSymmetric` | boolean |  |  |
| `dropNonMillable` | boolean |  |  |
| `dropNonNotchable` | boolean |  |  |
| `dropIdentical` | boolean |  |  |
| `minCells` | integer |  |  |
| `maxCells` | integer |  |  |
| `addTo` | `"nothing"` \| `"newProblem"` \| `"problem"` |  |  |
| `target` | [ProblemId](#problemid) |  | For addTo problem. |
| `min` | integer |  |  |
| `max` | integer |  |  |

Result:

| name | type | | description |
|---|---|---|---|
| `shapes` | [ShapeId](#shapeid)[] | required |  |
| `revision` | [Revision](#revision) | required | The document's revision; every change increments it. |

Errors: `not_found`, `invalid_argument`, `invalid_state`

### `solution.analyse`

Takes saved assemblies apart afterwards (Add disassembly): all, the ones without the moves stored, or one. A job: returns at once, reports through events, stops with solution.analyseStop; the results go in when it ends (kept when stopped). Refused while the problem is being solved; while it runs, the document is frozen as while solving.

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `which` | `"all"` \| `"missing"` \| `"one"` | required |  |
| `solution` | [SolutionId](#solutionid) |  | For one. |
| `solver` | [Solver](#solver) |  | Complete (ShapeLogic), or Fast (Crowell): Andrew Crowell's take-apart. |
| `rotations` | boolean |  | Try 90 degree turns. |
| `threads` | integer |  |  |

Result:

| name | type | | description |
|---|---|---|---|
| `total` | integer | required | Assemblies to take apart. |

Errors: `not_found`, `invalid_argument`, `invalid_state`, `unsupported`

### `solution.analyseStop`

Stops an analysis; what it finished is kept. Does not wait: solution.analyseEnded follows.

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |

Result: [Empty](#empty)

Errors: `not_found`, `invalid_state`

### `solution.analyseStatus`

Where an analysis stands.

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |

Result: AnalyseProgress

Errors: `not_found`

### event `solution.analyseProgress`

An analysis's progress; at most four times a second.

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `total` | integer | required |  |
| `done` | integer | required |  |
| `separated` | integer | required |  |
| `stopped` | boolean | required | Ended before it was through (stopped, or by an internal error). |
| `failure` | string or null | required | What an internal error on the analysis's thread said; null otherwise. |

### event `solution.analyseEnded`

An analysis ended (through, stopped, or ended by an internal error: failure says what); its results are in the problem (document.changed follows). The problems of a document that is replaced get one with stopped true after the replacing call's reply.

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `total` | integer | required |  |
| `done` | integer | required |  |
| `separated` | integer | required |  |
| `stopped` | boolean | required | Ended before it was through (stopped, or by an internal error). |
| `failure` | string or null | required | What an internal error on the analysis's thread said; null otherwise. |

#### SolutionParam

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `solution` | [SolutionId](#solutionid) | required | A saved solution, unique within its problem. |

#### AnalyseProgress

| name | type | | description |
|---|---|---|---|
| `active` | boolean | required |  |
| `total` | integer | required |  |
| `done` | integer | required |  |
| `separated` | integer | required | Taken apart so far (the others do not come apart). |
| `stopped` | boolean | required | Ended before it was through (stopped, or by an internal error). |
| `failure` | string or null | required | What an internal error on the analysis's thread said; what was done before it stays. |
| `current` | [TakeApartProgress](#takeapartprogress) or null | required |  |

#### AnalyseEvent

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `total` | integer | required |  |
| `done` | integer | required |  |
| `separated` | integer | required |  |
| `stopped` | boolean | required | Ended before it was through (stopped, or by an internal error). |
| `failure` | string or null | required | What an internal error on the analysis's thread said; null otherwise. |

## Playback

The disassembly player: a solution's pieces and their poses at any step (turns tumble about their pivots), as binary frames. A playback keeps copies of what it needs: it survives edits until closed or a new document.

### `playback.open`

Opens the playback of a saved solution. With its moves stored the poses follow the steps; without them (not taken apart, found not to come apart, or only the level summary kept) steps is 0 and every pose is the assembled one, so every solution is shown through one path.

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `solution` | [SolutionId](#solutionid) | required | A saved solution, unique within its problem. |

Result:

| name | type | | description |
|---|---|---|---|
| `handle` | [PlaybackHandle](#playbackhandle) | required | A playback opened by playback.open. |
| `steps` | integer | required | Moves and turns: step 0 is assembled, 'steps' apart; 0 without a disassembly. |
| `movesText` | string or null | required | The level text; null without a disassembly. |
| `pieces` | object[] | required |  |

`pieces`:

| name | type | | description |
|---|---|---|---|
| `pieces.piece` | integer | required | The problem's piece number. |
| `pieces.part` | integer | required |  |
| `pieces.shape` | [ShapeId](#shapeid) | required | A shape, stable while the document is open. |
| `pieces.name` | string | required | S1, or the shape's name, with ' #2' for the second piece of a part. |
| `pieces.orientation` | integer | required | The orientation of the piece's mesh (playback.mesh). |

Errors: `not_found`, `invalid_argument`, `unsupported`

### `playback.mesh`

The mesh of piece i (of playback.open's pieces) in its assembly orientation, centred as the poses expect; in any style (wireframe pieces, showing through solid ones).

Params:

| name | type | | description |
|---|---|---|---|
| `handle` | [PlaybackHandle](#playbackhandle) | required | A playback opened by playback.open. |
| `piece` | integer | required | An index into pieces. |
| `style` | [MeshStyle](#meshstyle) |  | Default drawing. |

Result: [MeshFrame](#meshframe)

Errors: `not_found`, `invalid_argument`

### `playback.poses`

Every piece's pose at steps from 'from' to 'to' (default the whole disassembly), samplesPerStep samples a step (default 8), as a frame: step (f32 per sample), translation (f32, xyz per sample and piece), rotation (f32, quaternion xyzw), visible (u8, padded).

Params:

| name | type | | description |
|---|---|---|---|
| `handle` | [PlaybackHandle](#playbackhandle) | required | A playback opened by playback.open. |
| `from` | integer |  |  |
| `to` | integer |  |  |
| `samplesPerStep` | integer |  |  |

Result: Poses

Errors: `not_found`, `invalid_argument`, `internal`

### `playback.track`

The keyframes of an animation (the glTF export's sampler): time (f32 seconds per keyframe), translation, rotation, visible as playback.poses; forward, holds, and back again with reassemble.

Params:

| name | type | | description |
|---|---|---|---|
| `handle` | [PlaybackHandle](#playbackhandle) | required | A playback opened by playback.open. |
| `samplesPerStep` | integer |  |  |
| `secondsPerStep` | number |  |  |
| `reassemble` | boolean |  |  |

Result: Poses

Errors: `not_found`, `internal`

### `playback.close`

Closes a playback.

Params:

| name | type | | description |
|---|---|---|---|
| `handle` | [PlaybackHandle](#playbackhandle) | required | A playback opened by playback.open. |

Result: [Empty](#empty)

Errors: `not_found`

#### Poses

| name | type | | description |
|---|---|---|---|
| `samples` | integer | required |  |
| `pieces` | integer | required |  |
| `frame` | [Frame](#frame) | required |  |

## Explorers

The step-by-step explorers of the solver: the placements of each piece and the assembly search one step at a time (on an assembler of their own, never a solve's), and the take-apart's positions as a tree. Each works on a copy of the puzzle behind a handle, until closed or a new document.

### `explore.openAssembly`

Prepares an assembly search of a problem to explore (Prepare in the original).

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `solver` | [Solver](#solver) |  | Complete (ShapeLogic), or Fast (Crowell): Andrew Crowell's take-apart. |
| `keepMirrors` | boolean |  |  |
| `keepRotations` | boolean |  |  |
| `completeRotations` | boolean |  |  |
| `strictColours` | boolean |  |  |
| `reduce` | boolean |  | Default true. |

Result:

| name | type | | description |
|---|---|---|---|
| `handle` | [AssemblyExplorerHandle](#assemblyexplorerhandle) | required | An assembly explorer opened by explore.openAssembly. |
| `pieces` | integer | required |  |
| `placementsSupported` | boolean | required |  |
| `finished` | number | required |  |

Errors: `not_found`, `invalid_argument`, `unsupported`

### `explore.placements`

Every placement of a piece in the result, in the assembler's order.

Params:

| name | type | | description |
|---|---|---|---|
| `handle` | [AssemblyExplorerHandle](#assemblyexplorerhandle) | required | An assembly explorer opened by explore.openAssembly. |
| `piece` | integer | required |  |

Result:

| name | type | | description |
|---|---|---|---|
| `placements` | object[] | required |  |

`placements`:

| name | type | | description |
|---|---|---|---|
| `placements.transformation` | integer | required |  |
| `placements.at` | [Cell](#cell) | required | A cell [x, y, z] in the grid's own coordinates. |

Errors: `not_found`, `invalid_argument`, `unsupported`

### `explore.step`

Advances the search 'steps' steps (default 1) and returns the pieces placed now.

Params:

| name | type | | description |
|---|---|---|---|
| `handle` | [AssemblyExplorerHandle](#assemblyexplorerhandle) | required | An assembly explorer opened by explore.openAssembly. |
| `steps` | integer |  |  |

Result:

| name | type | | description |
|---|---|---|---|
| `finished` | number | required | How far the search is, 0 to 1. |
| `placements` | [Placement](#placement)[] | required |  |

Errors: `not_found`

### `explore.openMovements`

Opens the movement browser on a saved solution's assembly: the root position.

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `solution` | [SolutionId](#solutionid) | required | A saved solution, unique within its problem. |
| `rotations` | boolean |  | The analysis also tries 90 degree turns. |

Result:

| name | type | | description |
|---|---|---|---|
| `handle` | [MovementExplorerHandle](#movementexplorerhandle) | required | A movement explorer opened by explore.openMovements. |
| `root` | [MovementNode](#movementnode) | required |  |
| `directions` | string[] | required | The grid's movement directions as labelled ('x', '-x', ...): direction d forward is label 2d, reversed 2d + 1. |
| `vectors` | [Cell](#cell)[] | required | Direction d's step in grid coordinates (reversed: negated), one per direction: what explore.findMatching's delta is one of (a label does not say how far a step goes: a prism's x step is 2). |

Errors: `not_found`, `unsupported`

### `explore.node`

A position of the tree.

Params:

| name | type | | description |
|---|---|---|---|
| `handle` | [MovementExplorerHandle](#movementexplorerhandle) | required | A movement explorer opened by explore.openMovements. |
| `node` | integer | required |  |

Result: [MovementNode](#movementnode)

Errors: `not_found`

### `explore.analyse`

Adds every position one move away from a node (Analyze); returns the new ones.

Params:

| name | type | | description |
|---|---|---|---|
| `handle` | [MovementExplorerHandle](#movementexplorerhandle) | required | A movement explorer opened by explore.openMovements. |
| `node` | integer | required |  |

Result:

| name | type | | description |
|---|---|---|---|
| `nodes` | [MovementNode](#movementnode)[] | required |  |

Errors: `not_found`

### `explore.addMove`

Adds the position where the given pieces moved one step in a direction (Add Movement), valid or not; returns it (or the child already there).

Params:

| name | type | | description |
|---|---|---|---|
| `handle` | [MovementExplorerHandle](#movementexplorerhandle) | required | A movement explorer opened by explore.openMovements. |
| `node` | integer | required |  |
| `pieces` | integer[] | required |  |
| `direction` | integer | required |  |
| `reverse` | boolean |  |  |

Result: [MovementNode](#movementnode)

Errors: `not_found`, `invalid_argument`

### `explore.findMatching`

The first valid move that moves a piece by a vector, taking as few others as needed; its position.

Params:

| name | type | | description |
|---|---|---|---|
| `handle` | [MovementExplorerHandle](#movementexplorerhandle) | required | A movement explorer opened by explore.openMovements. |
| `node` | integer | required |  |
| `piece` | integer | required |  |
| `delta` | [Cell](#cell) | required | A cell [x, y, z] in the grid's own coordinates. |

Result: [MovementNode](#movementnode)

Errors: `not_found`, `invalid_argument`

### `explore.prune`

Keeps only the path from the root to a node (Prune Tree).

Params:

| name | type | | description |
|---|---|---|---|
| `handle` | [MovementExplorerHandle](#movementexplorerhandle) | required | A movement explorer opened by explore.openMovements. |
| `node` | integer | required |  |

Result: [Empty](#empty)

Errors: `not_found`

### `explore.close`

Closes an explorer of either kind.

Params:

| name | type | | description |
|---|---|---|---|
| `handle` | [AssemblyExplorerHandle](#assemblyexplorerhandle) or [MovementExplorerHandle](#movementexplorerhandle) | required |  |

Result: [Empty](#empty)

Errors: `not_found`

#### NodeParam

| name | type | | description |
|---|---|---|---|
| `handle` | [MovementExplorerHandle](#movementexplorerhandle) | required | A movement explorer opened by explore.openMovements. |
| `node` | integer | required |  |

## Exports

The STL export (deferred part 5; the original's File ▸ Export STL and Export Solution Pieces to STL, src/lib/stl*.cpp, cubemesh, minkmesh): each grid's mesher parameters, a shape's STL mesh for the dialog's preview with its volume, the STL file as bytes (the client writes it where its user chose), and the pieces of a solution's export with their file names. The cube, prism, rhombic and tetra-octa grids share six parameters (the cube grids' mesh comes from a table of chamfered corners, the others' from Minkowski sums with the Manifold library, seconds for large shapes in a release build), the sphere grid has eight. Allowed while solving: they only read shapes.

### `export.stlParameters`

The STL parameters of a grid, in order, as the original's export dialog showed them: name, kind of value, default, tooltip. The values of export.stlMesh and export.stl follow this order.

Params:

| name | type | | description |
|---|---|---|---|
| `grid` | [Grid](#grid) |  | Default: the document's. |

Result:

| name | type | | description |
|---|---|---|---|
| `parameters` | StlParameter[] | required |  |

### `export.stlMesh`

A shape's STL mesh for the parameter values, as a binary frame with the arrays of a MeshFrame (the export dialog's preview: the mesh before its coplanar faces are merged for the file), with the volume it encloses (the dialog's 'Volume: x cubic-units'). Refused with the original's words for a shape with variable cells, a value out of range, more than 6 recursions on the sphere grid (not a limit of the original: its mesh failed from 6 on), or a mesh that comes out faulty ('The generated mesh is faulty in some way, try to tweak the parameter').

Params:

| name | type | | description |
|---|---|---|---|
| `shape` | [ShapeId](#shapeid) | required | A shape, stable while the document is open. |
| `parameters` | StlValues |  | One value per parameter of the document's grid, in export.stlParameters' order (another count: invalid_argument). Default: the defaults. |

Result:

| name | type | | description |
|---|---|---|---|
| `frame` | [Frame](#frame) | required |  |
| `triangles` | integer | required |  |
| `vertices` | integer | required |  |
| `lo` | number[] | required |  |
| `hi` | number[] | required |  |
| `hotspot` | [Cell](#cell) | required | A cell [x, y, z] in the grid's own coordinates. |
| `volume` | number | required | In cubic units of the unit size (the mesh's own units). |

Errors: `not_found`, `invalid_argument`

### `export.stl`

A shape's STL file, as a binary frame with one array, file (u8): the mesh for the parameter values with its coplanar faces merged and triangulated again, each face a fan of triangles, binary or text as the original wrote it. Refused as export.stlMesh.

Params:

| name | type | | description |
|---|---|---|---|
| `shape` | [ShapeId](#shapeid) | required | A shape, stable while the document is open. |
| `parameters` | StlValues |  | One value per parameter of the document's grid, in export.stlParameters' order (another count: invalid_argument). Default: the defaults. |
| `binary` | boolean |  | Binary STL (the dialog's 'Binary STL'); default true. Otherwise text. |
| `name` | string |  | What the file says its solid is: the binary header (its first 80 bytes) or the text's 'solid' line; the original wrote the file's name there. Default empty. No control characters, at most 255 bytes. |

Result:

| name | type | | description |
|---|---|---|---|
| `frame` | [Frame](#frame) | required |  |

Errors: `not_found`, `invalid_argument`

### `export.solutionPieces`

What the original's Export Solution Pieces to STL writes for a saved solution: the distinct shapes its assembly places (not the problem's counts), in shape order, with how many copies and their file's name: the shape's name, or S<n> without one, every byte but ASCII letters, digits, '-' and '_' made '_', '-S<n>' added when an earlier piece's file has that name, then '.stl'. Allowed while solving.

Params:

| name | type | | description |
|---|---|---|---|
| `problem` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `solution` | [SolutionId](#solutionid) | required | A saved solution, unique within its problem. |

Result:

| name | type | | description |
|---|---|---|---|
| `pieces` | object[] | required |  |

`pieces`:

| name | type | | description |
|---|---|---|---|
| `pieces.shape` | [ShapeId](#shapeid) | required | A shape, stable while the document is open. |
| `pieces.count` | integer | required |  |
| `pieces.fileName` | string | required |  |

Errors: `not_found`

#### StlParameter

One parameter of a grid's STL mesher (stlExporter_c's getParameterName, getParameterType, getParameter of a new exporter, getParameterTooltip).

| name | type | | description |
|---|---|---|---|
| `name` | string | required | The original's label: Unit Size, Unit Size Y, Unit Size Z, Bevel, Offset, Interior chamfers; on the sphere grid Sphere radius, Connection radius, Curvature radius, Offset, Recursions, Inner radius, Hole diameter, Square Hole. |
| `type` | `"number"` \| `"positiveNumber"` \| `"positiveInteger"` \| `"switch"` | required | The field: any number, a number of 0 or more, a whole number of 0 or more, or a check box (1 on, 0 off). |
| `default` | number | required |  |
| `tooltip` | string | required |  |

## Types

### ShapeId

A shape, stable while the document is open.

string matching `^s[1-9][0-9]*$`

### ColourId

A constraint colour.

string matching `^c[1-9][0-9]*$`

### ProblemId

A problem; also names its solve and its analysis.

string matching `^p[1-9][0-9]*$`

### RodSetId

A stacking puzzle's rod set (a board), stable while the document is open.

string matching `^r[1-9][0-9]*$`

### SolutionId

A saved solution, unique within its problem.

string matching `^sol[1-9][0-9]*$`

### PlaybackHandle

A playback opened by playback.open.

string matching `^pb[1-9][0-9]*$`

### AssemblyExplorerHandle

An assembly explorer opened by explore.openAssembly.

string matching `^ax[1-9][0-9]*$`

### MovementExplorerHandle

A movement explorer opened by explore.openMovements.

string matching `^mx[1-9][0-9]*$`

### Revision

The document's revision; every change increments it.

integer

### Changed

The result of a mutating call: the revision after it (unchanged when the call changed nothing).

| name | type | | description |
|---|---|---|---|
| `revision` | [Revision](#revision) | required | The document's revision; every change increments it. |

### Empty

No result.

(none)

### Rgb

A colour, #rrggbb (the engine writes lower case).

string matching `^#[0-9a-fA-F]{6}$`

### Cell

A cell [x, y, z] in the grid's own coordinates.

integer[]

### Size

A size [x, y, z].

integer[]

### Point2

A point [x, y] of a layer drawing, y up.

number[]

### Grid

The grid of a puzzle, fixed for its life.

`"bricks"` \| `"prisms"` \| `"spheres"` \| `"rhombic"` \| `"tetraOcta"` \| `"sliding"` \| `"stacking"`

### Solver

Complete (ShapeLogic), or Fast (Crowell): Andrew Crowell's take-apart.

`"classic"` \| `"crowell"`

### SlidingSolver

How far a sliding puzzle's search may go: 250,000 arrangements, 1,000,000, or no limit (the full search keeps its older levels on disk and spreads each over threads; the default).

`"fast"` \| `"deep"` \| `"full"`

### StackingSolver

The Stacking Solver (the fewest rod transfers: the Panex search whenever the puzzle fits it, which pauses, carries on and saves itself, else a search of its own) or the Panex Solver (the Panex search alone, for rod sets with Panex columns; a puzzle it cannot take is refused with its reason).

`"stacking"` \| `"panex"`

### SolveState

decision 0004; 'stopping' is an active state with a stop requested (SolveProgress.stage).

`"idle"` \| `"preparing"` \| `"assembling"` \| `"draining"` \| `"paused"` \| `"finished"` \| `"error"`

### SolveStage

What a solve is doing; search is a sliding puzzle's search from a start to the goal, or a stacking puzzle's from its start stacking (SolveProgress.searched, searchDepth, searchTraced, panexSearch).

`"idle"` \| `"prepare"` \| `"reduce"` \| `"assemble"` \| `"disassemble"` \| `"stopping"` \| `"paused"` \| `"done"` \| `"error"` \| `"search"`

### ProblemState

What the problem holds of solving: nothing, a started solve (maybe paused), a finished one, or counts lost by an edit.

`"unsolved"` \| `"solving"` \| `"solved"` \| `"unknown"`

### CellState

`"fixed"` \| `"variable"`

### CellTool

The grid editor's tools; paint changes only the colour of non-empty cells.

`"fixed"` \| `"variable"` \| `"erase"` \| `"paint"`

### Axes

Mirror or span axes.

`"x"` \| `"y"` \| `"z"`[]

### ShapeTask

A button of the tool tabs; what the numbered ones do depends on the grid (shape.taskSet gives each one's group and label).

`"nudge0"` \| `"nudge1"` \| `"nudge2"` \| `"nudge3"` \| `"nudge4"` \| `"nudge5"` \| `"rotate6"` \| `"rotate7"` \| `"rotate8"` \| `"rotate9"` \| `"rotate10"` \| `"rotate11"` \| `"flip12"` \| `"flip13"` \| `"flip14"` \| `"prune"` \| `"insideFixed"` \| `"outsideFixed"` \| `"insideVariable"` \| `"outsideVariable"` \| `"insideDecolour"` \| `"outsideDecolour"` \| `"double"` \| `"triple"` \| `"toOrigin"` \| `"centre"` \| `"minimize"` \| `"nudge27"` \| `"nudge28"` \| `"nudge29"` \| `"nudge30"` \| `"nudge31"` \| `"nudge32"` \| `"fillHoles"` \| `"gridScale"`

### MeshStyle

The 3D view's bevelled cells, the same closed with the wire-frame faces flagged, flat faces with edge lines, or the STL export's mesh at the view's own parameters (the original's "Draw pieces like STL export": gap 0.02 and bevel 0.05 of a cell with the interior chamfers, the sphere grid's balls at two recursions; closed; the flat mesh where the mesher fails). The STL style's cell faces carry the checker flag, as the others', and bevels are flagged 16; the original drew it in one colour. It is made with the Manifold library on the prism, rhombic and tetra-octa grids and takes a while for large shapes (seconds in a release build).

`"drawing"` \| `"wireframe"` \| `"flat"` \| `"stl"`

### ChangeKind

`"shapes"` \| `"colours"` \| `"problems"` \| `"solutions"` \| `"notes"` \| `"rodSets"`

### SolutionsVersion

A counter the problem moves on every change of its saved list (a solution added, removed, re-sorted or edited), kept while the document is open: solution.page and solve.solutionsAdded carry it, so pages read at one value belong together and a client knows when to page again.

integer

### SortBy

Where a new solution goes in the saved list while solving: the original's sort-by choice.

`"unsorted"` \| `"moves"` \| `"level"` \| `"rotations"`

### ArrayRef

A typed array inside a binary frame, little-endian, its offset a multiple of 4.

| name | type | | description |
|---|---|---|---|
| `type` | `"f32"` \| `"u32"` \| `"i32"` \| `"u8"` | required |  |
| `offset` | integer | required |  |
| `length` | integer | required | Elements, not bytes. |

### Frame

A binary frame sent before the reply (decision 0012): its number, its size, and the arrays in it.

| name | type | | description |
|---|---|---|---|
| `frame` | integer | required |  |
| `bytes` | integer | required |  |
| `arrays` | map of [ArrayRef](#arrayref) | required |  |

### MeshFrame

A drawing mesh: arrays positions and normals (f32, xyz per vertex), indices (u32, three per triangle), and per triangle voxel (u32, the cell index x + X*(y + Y*z)), voxelFace (i32, its side, -1 for bevels and gaps), flags (u32, FaceFlag bits: 1 light checker colour, 2 variable mark, 4 the variable marker face, 8 wire frame, 16 bevel, 32 gap, 128 inside), colour (u8, colour number, 0 none; padded), polygon (u32); and edgeLines (f32, two points per line, flat style only).

| name | type | | description |
|---|---|---|---|
| `frame` | [Frame](#frame) | required |  |
| `triangles` | integer | required |  |
| `vertices` | integer | required |  |
| `lo` | number[] | required |  |
| `hi` | number[] | required |  |
| `hotspot` | [Cell](#cell) | required | The hotspot of the shape in the mesh's orientation, in grid coordinates: an assembly puts it on a Placement's cell, so the mesh is drawn moved by the cell minus the hotspot (in mesh units: the grid's cell spacing). |

### RodSet

A stacking puzzle's board: a row of rods and the rules of a move (the original's rod set; its fields as the Rods panel showed them).

| name | type | | description |
|---|---|---|---|
| `id` | [RodSetId](#rodsetid) | required | A stacking puzzle's rod set (a board), stable while the document is open. |
| `label` | string | required | R1, R2, ... by place (shown as 'R1 - name' with a name). |
| `name` | string | required |  |
| `rodCount` | integer | required | How many rods (the pocket column not counted). |
| `rods` | integer | required | The rods of a stack map: rodCount, and one more with the pocket column (the last; it stands left of rod 1). |
| `growHeight` | boolean | required | Grow to the height of all pieces: a rod holds every disc of the problem. |
| `definedHeight` | integer | required | Defined height: the discs a rod holds when it does not grow. |
| `sizeMatters` | boolean | required | Disc size matters - no large on small (equal sizes stack); Panex columns ignore it. |
| `distanceMatters` | boolean | required | Disc can only move over 1 rod: to a neighbouring rod only. |
| `panexColumns` | boolean | required | Panex Style Columns: a disc of size s sits at most s places below the top, in any order; one disc more may wait raised into the bridge, blocking moves that pass over it. |
| `pocketColumn` | boolean | required | Add pocket column (with Panex columns): an extra rod beside rod 1 that holds any disc. |
| `pocketHeight` | integer | required | The discs the pocket column holds. |
| `outsideChannel` | boolean | required | Extra outside channel (with Panex columns and three rods or more): the first and last rods are joined outside the bridge. |

### DiscRef

One copy of a disc on a rod: the shape, and which of its copies (from 0).

| name | type | | description |
|---|---|---|---|
| `shape` | [ShapeId](#shapeid) or null | required | Null for a shape the file names that the puzzle does not have (only an edited file; setupError says so). |
| `instance` | integer | required |  |

### Stacks

Each rod of a stack map, its discs bottom to top; rods past the board's count are kept while they hold discs.

[DiscRef](#discref)[][]

### BoardLayout

Where the rods and the discs of a stacking stand (the original's layoutBoard), in cells: what the 3D view draws around the discs' meshes.

| name | type | | description |
|---|---|---|---|
| `spacing` | integer | required | The distance between neighbouring rods. |
| `rodCount` | integer | required | The rods drawn, the pocket column included. |
| `rodHeight` | integer | required | The rods' drawn height. |
| `rodX` | integer[] | required | x of each rod, in the stack map's order (the pocket last, standing left of rod 1). |
| `rodHeights` | integer[] | required | The drawn height of each rod; the pocket column is shorter. |
| `panex` | boolean | required | Panex columns: drawn with a V that narrows to the bottom. |
| `pocket` | boolean | required | The last rod is the pocket column, drawn as a plain peg. |
| `maxSize` | integer | required | The largest disc size, so the V stays inside the rod spacing. |
| `discs` | object[] | required | Each disc of the problem (its pieces: parts, then copies) and the cell its hotspot (its centre) stands on; null for a disc on no rod. Empty for a rod set drawn alone. |

`discs`:

| name | type | | description |
|---|---|---|---|
| `discs.shape` | [ShapeId](#shapeid) | required | A shape, stable while the document is open. |
| `discs.instance` | integer | required |  |
| `discs.size` | integer | required |  |
| `discs.at` | [Cell](#cell) or null | required |  |

### ShapeSummary

| name | type | | description |
|---|---|---|---|
| `id` | [ShapeId](#shapeid) | required | A shape, stable while the document is open. |
| `label` | string | required | S1, S2, ... by place. |
| `name` | string | required | A start/goal shape's is the user's part (shown as '(START/GOAL n) name'). |
| `size` | [Size](#size) | required | A size [x, y, z]. |
| `fixed` | integer | required |  |
| `variable` | integer | required |  |
| `weight` | integer | required |  |
| `colour` | [Rgb](#rgb) or null | required | The shape's own colour, or null for the default one. |
| `startGoal` | integer or null | required | A sliding puzzle's start/goal shape: its number n (sliding.json); null for any other shape. |
| `disc` | integer or null | required | A stacking puzzle's disc: its size, the radius, which the size rule compares (stacking.setDiscSize); null for any other shape. |

### Colour

| name | type | | description |
|---|---|---|---|
| `id` | [ColourId](#colourid) | required | A constraint colour. |
| `label` | string | required | C1, C2, ... by place. |
| `rgb` | [Rgb](#rgb) | required | A colour, #rrggbb (the engine writes lower case). |

### ProblemSummary

| name | type | | description |
|---|---|---|---|
| `id` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `label` | string | required | P1, P2, ... by place. |
| `name` | string | required |  |
| `result` | [ShapeId](#shapeid) or null | required |  |
| `rodSet` | [RodSetId](#rodsetid) or null | required | A stacking problem's board (stacking.setProblemRodSet, the original's Set Start/Goal Rods); null for none and on other grids. |
| `pieces` | integer | required | The number of pieces (the parts' maxima summed). |
| `state` | [ProblemState](#problemstate) | required | What the problem holds of solving: nothing, a started solve (maybe paused), a finished one, or counts lost by an edit. |
| `solve` | [SolveState](#solvestate) | required | decision 0004; 'stopping' is an active state with a stop requested (SolveProgress.stage). |
| `analysing` | boolean | required |  |
| `assemblies` | integer or null | required | Assemblies found, when known. |
| `solutions` | integer or null | required | Solutions found, when known. |
| `saved` | integer | required | Saved solutions. |
| `timeMs` | integer or null | required | Time spent solving, when known. |

### Snapshot

Everything a front end shows of the document's lists.

| name | type | | description |
|---|---|---|---|
| `revision` | [Revision](#revision) | required | The document's revision; every change increments it. |
| `dirty` | boolean | required |  |
| `grid` | [Grid](#grid) | required | The grid of a puzzle, fixed for its life. |
| `path` | string or null | required | The file it was opened from or last saved to. |
| `notes` | string | required |  |
| `canUndo` | boolean | required |  |
| `canRedo` | boolean | required |  |
| `solving` | boolean | required | A solve or an analysis is active: shapes and lists are frozen (decision 0011). |
| `shapes` | [ShapeSummary](#shapesummary)[] | required |  |
| `colours` | [Colour](#colour)[] | required |  |
| `problems` | [ProblemSummary](#problemsummary)[] | required |  |
| `rodSets` | [RodSet](#rodset)[] | required | A stacking puzzle's boards (at least one); empty on other grids. |
| `notice` | string |  | What opening the file changed, to tell the user (the original's load message: a sliding shape of more than one layer made flat, which leaves the document dirty). Only in the result of document.open, document.openBytes and solve.importPaused, and only when there is something to tell. |

### ShapeInfo

A row of the status window.

| name | type | | description |
|---|---|---|---|
| `shape` | [ShapeId](#shapeid) | required | A shape, stable while the document is open. |
| `fixed` | integer | required |  |
| `variable` | integer | required |  |
| `identicalMirror` | [ShapeId](#shapeid) or null | required | An earlier shape it is identical to, mirrors allowed. |
| `identicalShape` | [ShapeId](#shapeid) or null | required | ... in any orientation, not mirrored. |
| `identicalComplete` | [ShapeId](#shapeid) or null | required | ... colours included. |
| `connectedFace` | boolean | required |  |
| `connectedEdge` | boolean | required |  |
| `connectedCorner` | boolean | required |  |
| `holes2D` | boolean | required |  |
| `holes3D` | boolean | required |  |
| `notchable` | boolean or null | required | Cube grid only. |
| `millable` | boolean or null | required | Cube grid only. |
| `symmetry` | integer or null | required | The symmetry group number, when the grid's tables know it. |
| `statusLine` | string | required | The original's status line for the shape. |

### Part

| name | type | | description |
|---|---|---|---|
| `shape` | [ShapeId](#shapeid) | required | A shape, stable while the document is open. |
| `min` | integer | required |  |
| `max` | integer | required |  |
| `groups` | object[] | required |  |
| `required` | boolean | required | A sliding piece with a start or goal on the problem's start/goal shape: its count follows them (problem.setPieceCount refuses to change it; the range and all-pieces commands leave it). |

`groups`:

| name | type | | description |
|---|---|---|---|
| `groups.group` | integer | required |  |
| `groups.count` | integer | required |  |

### SolverSettings

The Solver tab's settings stored with a problem (saved in the file, the original's names; the front end decides what they mean). Settings of other versions are kept as strings. These names are the file's and stay: SolveOptions uses the API's (deepSymmetry is completeRotations, strictColors is strictColours, justLevels is justLevels; nestedSlides, partialNested and highMemory are sliding's nestedSlides, partialNested and highMemory, solverType fast, deep or full its solver). highMemory belongs to the stacking solvers too.

| name | type | | description |
|---|---|---|---|
| `disassemble` | boolean |  |  |
| `checkRotations` | boolean |  |  |
| `justCount` | boolean |  |  |
| `justLevels` | boolean |  |  |
| `deepSymmetry` | boolean |  |  |
| `keepMirrors` | boolean |  |  |
| `keepRotations` | boolean |  |  |
| `strictColors` | boolean |  |  |
| `nestedSlides` | boolean |  |  |
| `partialNested` | boolean |  |  |
| `highMemory` | boolean |  |  |
| `autosave` | boolean |  |  |
| `solverType` | string |  | classic, crowell (sliding: fast, deep, full). |
| `sortBy` | string |  | unsorted, moves, level, rotations. |
| *other* | string | | |

### ProblemDetail

| name | type | | description |
|---|---|---|---|
| `id` | [ProblemId](#problemid) | required | A problem; also names its solve and its analysis. |
| `label` | string | required |  |
| `name` | string | required |  |
| `result` | [ShapeId](#shapeid) or null | required |  |
| `rodSet` | [RodSetId](#rodsetid) or null | required | A stacking problem's board (stacking.setProblemRodSet, the original's Set Start/Goal Rods); null for none and on other grids. |
| `pieces` | integer | required |  |
| `state` | [ProblemState](#problemstate) | required | What the problem holds of solving: nothing, a started solve (maybe paused), a finished one, or counts lost by an edit. |
| `solve` | [SolveState](#solvestate) | required | decision 0004; 'stopping' is an active state with a stop requested (SolveProgress.stage). |
| `analysing` | boolean | required |  |
| `assemblies` | integer or null | required |  |
| `solutions` | integer or null | required |  |
| `saved` | integer | required |  |
| `timeMs` | integer or null | required |  |
| `parts` | [Part](#part)[] | required |  |
| `colourRules` | object[] | required | The allowed pairs: a piece cell of colour 'piece' may sit on a result cell of colour 'result'. |
| `maxHoles` | integer or null | required |  |
| `savedPosition` | boolean | required | A paused search position is stored (solve.continue carries on from it). |
| `solverSettings` | [SolverSettings](#solversettings) | required |  |
| `stacks` | object or null | required | Null on other grids. |

`colourRules`:

| name | type | | description |
|---|---|---|---|
| `colourRules.piece` | [ColourId](#colourid) | required | A constraint colour. |
| `colourRules.result` | [ColourId](#colourid) | required | A constraint colour. |

### FitStats

Whether the pieces can fill the result (the fit summary).

| name | type | | description |
|---|---|---|---|
| `verdict` | `"noResult"` \| `"fits"` \| `"tooLarge"` \| `"tooSmall"` | required |  |
| `resultMin` | integer | required | The result's fixed cells. |
| `resultMax` | integer | required | Fixed plus variable. |
| `piecesMin` | integer | required |  |
| `piecesMax` | integer | required |  |
| `cellsMin` | integer | required |  |
| `cellsMax` | integer | required |  |
| `holesMin` | integer | required |  |
| `holesMax` | integer | required |  |
| `maxHoles` | integer or null | required |  |
| `statusLine` | string | required |  |

### SolveOptions

What a solve does (the solve thread's flags); a missing field takes the engine's setting or the default.

| name | type | | description |
|---|---|---|---|
| `solver` | [Solver](#solver) |  | Complete (ShapeLogic), or Fast (Crowell): Andrew Crowell's take-apart. |
| `disassemble` | boolean |  | Take each assembly apart; only those that come apart are solutions. |
| `checkRotations` | boolean |  | Try 90 degree turns while taking apart. |
| `justCount` | boolean |  | Count, keep no solutions. |
| `justLevels` | boolean |  | Keep the level summary of each take-apart, not its moves. |
| `keepMirrors` | boolean |  |  |
| `keepRotations` | boolean |  |  |
| `completeRotations` | boolean |  | The thorough rotation check (the original's deep symmetry check). |
| `strictColours` | boolean |  |  |
| `reduce` | boolean |  | Drop placements that can never be part of an assembly first (default true). |
| `sortBy` | [SortBy](#sortby) |  | Where a new solution goes in the saved list while solving: the original's sort-by choice. |
| `keepAtMost` | integer |  | Keep at most this many solutions; 0 all (default 10). |
| `keepEach` | integer |  | Unsorted, keep every n-th (0 and 1: each). |
| `threads` | integer |  | Assembly and take-apart together; 0 every hardware thread. |
| `takeApartWorkers` | integer |  | 0 chooses. |
| `sliding` | [SlidingOptions](#slidingoptions) |  |  |
| `stacking` | [StackingOptions](#stackingoptions) |  |  |

### SlidingOptions

A sliding puzzle's search (the solve thread's sliding flags). A sliding solve always keeps mirrored and rotated starts and matches colours strictly (the start locks), whatever the brick fields say; disassemble (the GUI's Find Solutions) searches each start to the goal; solver, checkRotations, justLevels and sortBy play no part (a path found goes at the end of the list). Where a full search keeps its levels is the engine's setting (slideFolder, slideDiskGB).

| name | type | | description |
|---|---|---|---|
| `solver` | [SlidingSolver](#slidingsolver) |  | How far a sliding puzzle's search may go: 250,000 arrangements, 1,000,000, or no limit (the full search keeps its older levels on disk and spreads each over threads; the default). |
| `nestedSlides` | boolean |  | A piece may carry what is nested in its outline (Allow Nested Slides). |
| `partialNested` | boolean |  | And what is partly nested in it (Allow Partially Nested); implies nestedSlides. |
| `highMemory` | boolean |  | Hold up to half the machine's memory in arrangements, not about 2 GB (Enable High Memory). |

### StackingOptions

A stacking puzzle's search (the solve thread's stacking flags). A stacking solve has no assembler and no take-apart: one search from the start stacking to the goal, whose path is the one solution (its assembly the start stacking). disassemble (the GUI's Find Solutions) or justCount must be on; threads count; the brick fields play no part. solve.prepare is refused (unsupported), and so is solve.savePoint: the search saves itself. Where it is saved is the engine's setting (panexFolder, panexDiskGB).

| name | type | | description |
|---|---|---|---|
| `solver` | [StackingSolver](#stackingsolver) |  | Default: the Panex Solver for a problem on Panex columns, else the Stacking Solver (the original's menu offered only that one). |
| `highMemory` | boolean |  | Hold up to half the machine's memory in stackings (Enable High Memory). |
| `autosave` | boolean |  | The Panex search saves itself every 20 minutes (default true); off, only when it stops (the GUI's Autosave box). |

### TakeApartProgress

One take-apart under way.

| name | type | | description |
|---|---|---|---|
| `elapsedMs` | integer | required |  |
| `pieces` | integer | required |  |
| `searchPieces` | integer | required |  |
| `separations` | integer | required |  |
| `depth` | integer | required |  |
| `level` | integer | required |  |
| `levelDone` | integer | required |  |
| `levelSize` | integer | required |  |
| `nextLevelSize` | integer | required |  |
| `nodes` | integer | required |  |
| `threads` | integer | required |  |

### SolveProgress

| name | type | | description |
|---|---|---|---|
| `stage` | [SolveStage](#solvestage) | required | What a solve is doing; search is a sliding puzzle's search from a start to the goal, or a stacking puzzle's from its start stacking (SolveProgress.searched, searchDepth, searchTraced, panexSearch). |
| `elapsedMs` | integer | required |  |
| `piece` | integer | required |  |
| `pieces` | integer | required |  |
| `assemblyFraction` | number | required |  |
| `iterations` | integer | required |  |
| `assemblerThreads` | integer | required |  |
| `assemblies` | integer | required |  |
| `solutions` | integer | required |  |
| `disassembly` | boolean | required |  |
| `disasmCompleted` | integer | required |  |
| `disasmPending` | integer | required |  |
| `disasmWorkers` | integer | required |  |
| `overall` | number | required | The whole solve, 0 to 1; never goes back during a run. |
| `overallKnown` | boolean | required |  |
| `levelFraction` | number | required |  |
| `secondsLeft` | number or null | required |  |
| `activity` | string | required | The original's status text (empty in the search stage, which the original GUI told as 'Sliding search, N moves deep. Scanned N arrangements.', 'Panex search, N moves deep. Found N stackings.', 'Panex search, tracing the path. Traced N of M moves.' or 'Stacking search. Scanned N stackings.'). |
| `searched` | integer | required | The sliding search under way: the arrangements it has visited; the stacking search: the stackings it has searched (found, for the Panex search). |
| `searchDepth` | integer | required | How many moves deep a full sliding search is (0 before its first level, and for the other searches); how many moves deep both ends of the Panex search are together. |
| `searchTraced` | integer | required | Once the ends of the Panex search have met: how many moves of the path are traced back (of searchDepth); 0 before. |
| `panexSearch` | boolean | required | The Panex Solver runs (its words: 'Panex search'), not the Stacking Solver ('Stacking search'). |

### SolveStats

The debug statistics panel.

| name | type | | description |
|---|---|---|---|
| `state` | [SolveState](#solvestate) | required | decision 0004; 'stopping' is an active state with a stop requested (SolveProgress.stage). |
| `solver` | [Solver](#solver) | required | Complete (ShapeLogic), or Fast (Crowell): Andrew Crowell's take-apart. |
| `disassemblyEnabled` | boolean | required |  |
| `rotationsEnabled` | boolean | required |  |
| `hardwareThreads` | integer | required |  |
| `assemblerThreads` | integer | required |  |
| `disasmWorkers` | integer | required |  |
| `assembliesFound` | integer | required |  |
| `solutionsFound` | integer | required |  |
| `inseparable` | integer | required |  |
| `iterations` | integer | required |  |
| `assemblyProgress` | number | required |  |
| `pending` | integer | required |  |
| `peakPending` | integer | required |  |
| `disasmCompleted` | integer | required |  |
| `elapsedMs` | integer | required |  |
| `prepareMs` | integer | required |  |
| `reduceMs` | integer | required |  |
| `assemblyMs` | integer | required |  |
| `disasmWorkMs` | integer | required |  |
| `linearSearchMs` | integer | required |  |
| `rotationSearchMs` | integer | required |  |
| `drainMs` | integer | required |  |
| `avgDisasmSeconds` | number | required |  |
| `running` | [TakeApartProgress](#takeapartprogress)[] | required |  |

### SolveFailure

Why a run ended in error.

| name | type | | description |
|---|---|---|---|
| `assembler` | `"tooManyUnits"` \| `"tooFewUnits"` \| `"cannotPlace"` \| `"cannotRestoreVersion"` \| `"cannotRestoreSyntax"` \| `"cannotRestoreInterrupted"` \| `"puzzleUnhandable"` \| `null` | required | The preparation's or the saved position's error; null for an internal error. |
| `param` | integer | required | The piece or number the error names. |
| `message` | string | required |  |
| `file` | string | required |  |
| `line` | integer | required |  |

### Placement

Where a piece is in an assembly.

| name | type | | description |
|---|---|---|---|
| `piece` | integer | required | The problem's piece number (pieces are numbered part by part). |
| `placed` | boolean | required |  |
| `transformation` | integer | required |  |
| `at` | [Cell](#cell) | required | A cell [x, y, z] in the grid's own coordinates. |

### SolutionRow

| name | type | | description |
|---|---|---|---|
| `id` | [SolutionId](#solutionid) | required | A saved solution, unique within its problem. |
| `index` | integer | required | Its place in the saved list. |
| `assembly` | integer | required | The assembly number (the original shows it plus one). |
| `solution` | integer | required | The solution number (0 without a disassembly). |
| `pieces` | integer | required | Pieces placed. |
| `disassembly` | `"tree"` \| `"summary"` \| `"none"` | required | The moves are stored; only the level summary; not taken apart (or found not to come apart). |
| `moves` | integer or null | required | A stacking solution's: its rod transfers (each three frames of its path), as the original counted them; steps and movesText the same. |
| `rotations` | integer or null | required |  |
| `steps` | integer or null | required | Moves plus rotations. |
| `movesText` | string or null | required | The level text, e.g. 3.1.1. |

### MovementNode

A position of a movement explorer's tree.

| name | type | | description |
|---|---|---|---|
| `node` | integer | required |  |
| `parent` | integer | required | The root is its own parent. |
| `label` | string | required | The direction and the pieces moved, e.g. 'x: S1, S2.2'; empty for the root. |
| `pieces` | object[] | required |  |
| `children` | integer[] | required |  |

`pieces`:

| name | type | | description |
|---|---|---|---|
| `pieces.piece` | integer | required |  |
| `pieces.at` | [Cell](#cell) | required | A cell [x, y, z] in the grid's own coordinates. |

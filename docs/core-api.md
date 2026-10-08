# Core API (draft)

Every operation and data feed the web front end needs from the core, derived
from [webui-spec.md](webui-spec.md). Stages 1 to 6 build the C++ side of
these; stage 7 turns them into the JSON-RPC methods of `bt-engine`
(decision 0003) and replaces this draft with a JSON Schema in `api/` and a
generated `docs/API.md`. Names below are the planned JSON-RPC method names;
the C++ app layer mirrors them (`Document::setVoxels`…).

**Status:** superseded in stage 7 by the schema in `api/` and the
generated [API.md](API.md) (decision 0012), which are the contract. This
draft stays as the record of what the front end asked for. Where the API
differs: `shape.transform`, `shape.prune`, `shape.center`, `shape.toOrigin`,
`shape.minimize`, `shape.scale`, `shape.fillHoles` and `shape.surface` are
one `shape.task` (its names from `shape.taskSet`); `shape.selectionFill` /
`selectionClear` are `shape.setBox` with a tool; `solve.continue` keeps its
name, `solve.savePoint` is new (the autosave pause); the analysis is
`solution.analyse` / `analyseStop` / `analyseStatus` with its own events;
playback adds `playback.mesh` and `playback.track`; the explorers are
`explore.openAssembly` / `placements` / `step` and `explore.openMovements`
/ `node` / `analyse` / `addMove` / `findMatching` / `prune`, closed by
`explore.close`; the stored Solver tab settings are
`problem.setSolverSettings`. The mockup's "drop below level" does not
exist in the original: its "Drop" field keeps every n-th solution
(`keepEach`).

## Conventions

- **Stable ids.** Shapes, colours, problems and solutions are referred to by
  a stable id (`"s7"`, `"c2"`, `"p1"`, `"sol42"`) that survives reordering.
  Display labels (S1, P1, C2) follow the current order and are returned with
  each item. The file format still stores positions; ids are runtime only.
- **Revisions.** Every change increments `document.revision`. Each mutating
  call returns the new revision; `document.changed` carries it, so the front
  end can drop stale updates.
- **Undo.** Every mutating call is one undo step unless wrapped in
  `edit.begin` / `edit.end` (a drag across cells is one step). Undo depth is
  a setting.
- **Errors.** `Result<T>` errors map to JSON-RPC errors carrying the `Errc`
  name (`invalid_argument`, `not_found`, `parse_error`, `io_error`,
  `unsupported`, `invalid_state`, `internal`) and a message.
- **Long work** (solving, analysing) returns at once with a job id and
  reports through events; it is stopped through `solve.pause` / `solve.abort`
  (decision 0004).
- **Binary data** (meshes, pose tracks) is sent as binary frames referenced
  by id from the JSON result.
- **Cell addressing.** A cell is `[x, y, z]` in the grid's own coordinates.
  Non-cube grids use the same three integers (the original's encoding); the
  grid geometry call tells the front end how to draw each cell.

## 1. Session and document

| Method | Purpose | Ported from |
|---|---|---|
| `engine.info` | version, capabilities, grid types, solver types, thread count | new |
| `document.new {gridType}` | new puzzle; the grid type is fixed for its life | `puzzle_c` constructor, `gridType_c` |
| `document.open {path}` / `document.openBytes` | load `.xmpuzzle` (unknown content preserved) | `puzzle_c::load`, `xml.cpp`, `gzstream.cpp` |
| `document.save {path?}` / `document.saveBytes` | write `.xmpuzzle` | `puzzle_c::save` |
| `document.get` | full snapshot: grid type, shapes, colours, problems, notes, revision, dirty | new |
| `document.setNotes {text}` | puzzle comment | `puzzle_c::setComment` |
| `edit.undo` / `edit.redo` / `edit.history` | undo stack | `shapeHistory_c` (generalised to everything) |
| `edit.begin` / `edit.end` | group calls into one undo step | new |
| `settings.get` / `settings.set` | worker threads, undo depth, solver defaults | `configuration.cpp` (only the non-UI settings) |

Events: `document.changed {revision, what: [shapes|colours|problems|solutions|notes], ids}`.

## 2. Shapes and voxels

| Method | Purpose | Ported from |
|---|---|---|
| `shape.list` | ids, labels, size, cell counts (fixed/variable), weight, colour | `puzzle_c` shapes |
| `shape.create {size?}` / `shape.duplicate` / `shape.delete` | | `puzzle_c::addShape`, `removeShape` |
| `shape.move {id, toIndex}` | reorder (labels renumber) | `puzzle_c::exchangeShapes` |
| `shape.rename {id, label}` / `shape.setWeight` | | `voxel_c::setName`, `setWeight` |
| `shape.get {id}` | size and all cells (state, colour) | `voxel_c` |
| `shape.setCells {id, cells:[{x,y,z,state,colour}]}` | batch edit; the front end expands mirror/span itself or passes `mirror`/`span` options | `voxel_c::setState`, `setColor`; grid editor tools |
| `shape.selectionFill` / `shape.selectionClear` | box selection actions | grid editor |
| `shape.resize {ids, size}` | grow/shrink one or all shapes | `voxel_c::resize` |
| `shape.transform {id, op}` | flip / rotate / nudge, per grid; fails if the piece would leave the grid | `voxel_c::transform`, `translate` |
| `shape.transformSet {gridType}` | which transforms the grid offers (for the UI's buttons) | grid editors, `symmetries_*` |
| `shape.prune` / `shape.center` / `shape.toOrigin` | fit to grid | `voxel_c::minimizePiece`, `resize`, `translate` |
| `shape.minimize` / `shape.scale {factor}` | resolution down / up | `voxel_c::scaleDown`, `scale` |
| `shape.fillHoles` | returns the number filled | `voxel_c::fillHoles` |
| `shape.surface {part: inner|outer, action: fixed|variable|clearColour}` | returns the number changed | `voxel_c::actionOnSpace` |
| `shape.info {id}` | status window data: cells, symmetry, connectedness, identical shapes | `statuswindow.cpp`, `voxel_c` queries |
| `grid.geometry {gridType, size}` | per cell: polygon/circle for the 2D layer view, layer index, neighbours | `voxel_*` geometry, grid editors |
| `shape.mesh {id}` | binary drawing mesh (bevelled, as the original 3D view) | `voxel_c::getDrawingMesh`, `halfedge/`, `voxel_2_mesh.cpp` |

## 3. Colours

| Method | Purpose | Ported from |
|---|---|---|
| `colour.list` / `colour.add {rgb}` / `colour.edit` / `colour.delete` | constraint colours; deleting reverts cells to Default | `puzzle_c::addColor`, `changeColor`, `removeColor` |

## 4. Problems

| Method | Purpose | Ported from |
|---|---|---|
| `problem.list` / `problem.create` / `problem.duplicate` / `problem.delete` / `problem.move` / `problem.rename` | | `puzzle_c` problems |
| `problem.get {id}` | result, pieces with min/max, groups, colour rules, solve state, counts | `problem_c` |
| `problem.setResult {id, shape}` | | `problem_c::setResultId` |
| `problem.setPieceCount {id, shape, min, max}` | count or range; 0 removes | `problem_c::setShapeMinimum/Maximum` |
| `problem.setRangeAll {id, min, max}` / `problem.addOneOfEach` / `problem.removeAllPieces` | bulk actions | `bulkrangewindow.cpp` |
| `problem.setGroup {id, shape, group}` / `problem.deleteGroup` | exclusive groups | `problem_c` groups, `groupseditor.cpp` |
| `problem.setColourRule {id, pieceColour, resultColour, allowed}` / `problem.setColourRules {preset}` | | `problem_c::allowPlacement`, `disallowPlacement` |
| `problem.fit {id}` | result cells (min/max), piece count range, piece cell range, holes, verdict | `mainwindow.cpp` status line |
| `problem.kind {id}` | brick / sliding / stacking; sliding and stacking are `unsupported` for now | `sliding.cpp`, `stacking.cpp` |

## 5. Solving

| Method | Purpose | Ported from |
|---|---|---|
| `solve.start {problem, options}` | from the start; returns a job id | `solveThread_c::start` |
| `solve.prepare {problem, options}` | build the search structures only | `start(stop_after_prep)` |
| `solve.continue {problem}` | resume a paused search | `solveThread_c` with a saved assembler |
| `solve.pause {problem}` / `solve.abort {problem}` | stop where it can continue / stop and clear results | `stop()`, Abort button |
| `solve.status {problem}` | state, progress, stats | `getStats`, `solveStats_c` |
| `solve.exportPaused {problem}` / `solve.importPaused` | carry a paused solve between machines | `cb_ExportPaused`, BTSOLVE2 |

Options: mode (assemble / assemble+disassemble), solver (classic, crowell,
bt2), justCount, justLevels, keepMirror, keepRotated, completeRotations
(thorough rotation check), checkRotations (rotation take-apart),
strictColours, deepSymmetryCheck, highMemory, dropBelowLevel, keepAtMost,
dontKeepDisassemblies, sortBy, autosaveMinutes, threads.

Events: `solve.state {problem, state}` (SolveState), `solve.progress {problem,
fraction, assemblies, solutions, activity, elapsedMs}` (throttled to a few per
second), `solve.solutionsAdded {problem, count}`, `solve.error {problem, error}`.

## 6. Solutions

| Method | Purpose | Ported from |
|---|---|---|
| `solution.page {problem, sortBy, descending, offset, limit}` | rows: id, assembly number, solution number, level / moves text, moves, rotations, pieces used, disassembly status | `problem_c` saved solutions, sort callbacks |
| `solution.get {problem, id}` | assembly placements and disassembly summary | `solution_c`, `assembly_c` |
| `solution.delete {problem, which: one|before|after|withoutDisassembly|all, id?}` | | `mainwindow.cpp` delete callbacks |
| `solution.analyse {problem, ids|all}` | compute missing disassemblies (job, events as solving) | re-disassemble in `mainwindow_solve.cpp` |
| `solution.toShapes {problem, id}` | convert an assembly into new shapes | "Convert assemblies to pieces" |

## 7. Playback and explorers

| Method | Purpose | Ported from |
|---|---|---|
| `playback.open {problem, solution}` | piece list (shape, colour, mesh id), step count, moves text | `disasmToMoves_c` setup in `mainwindow.cpp` |
| `playback.poses {handle, from, to, samplesPerStep}` | binary pose track: per piece translation, rotation (quaternion), visibility per sample; includes rotation tumbles | `disasmToMoves_c`, the original's `gltfexport.cpp` sampler |
| `playback.close {handle}` | | |
| `explore.placements {problem, piece}` | placements of a piece in the result | `placementbrowser.cpp`, assembler placement API |
| `explore.movements {problem, solution}` | the disassembly search tree, node by node | `movementbrowser.cpp`, `disassemblerNode_c` |
| `explore.step {problem}` | advance the assembler one step and report its state | assembler single-step (GUI "Step") |
| `scene.puzzleLayout {problem}` | (optional) positions for the Puzzle workspace's piece grid | new; may stay in the front end |

## 8. Exports (later in this project)

`export.stl`, `export.glb`, `export.puzzlecad`; raster and vector images are
rendered by the front end from the meshes.

## What stays in the front end

Camera and navigation, view cube, display options, density, theme, layout
memory, selection highlighting, the 2D cell drawing (from `grid.geometry`),
keyboard handling, piece display mode (solid/wireframe/hidden), image
rendering. The engine never deals in pixels.

# Architecture

ShapeLogic is the core of the ShapeLogic puzzle designer and solver,
rebuilt so a web front end can drive it. This document describes **what
exists now** and the rules that hold everywhere. Each stage updates it.

*Current state: stage 8, the end of the plan (the assembler run layer
and all three assemblers; the take-apart with rotation moves, Classic and
Crowell; the solve session; the drawing meshes of every grid; the app
layer: the document with its undo history and commands, the queries, the
layer drawing of the grid editors, the analysis of saved assemblies, the
paused solve files, the playback sampler and the explorers; `shapelogic_engine`,
the JSON-RPC engine, with the schema of its API in `api/` and contract
tests; `shapelogic_cli`, the original's burrTxt and burrTxt2 for brick puzzles; every
benchmark as fast as the original or faster). The web front end
(docs/FRONTEND-PLAN.md) has all its stages, 1a to 8: the toolchain in
`web/`, the TypeScript types generated from `api/`, the engine client with
its host, bridge and proxy, the app's shell from the mockup, the 3D view in
three.js, the Entities workspace (the shapes list, the voxel editor on
every grid, the tool tabs' tasks, 3D editing, undo and redo), the Puzzle
workspace (problems, pieces, groups, colour rules, the pieces scene), the
Solver workspace (the search options, the runs, the solutions list,
analysis, the disassembly player, autosave), the explorers, the debug
panel and the status window, the menus, files and recovery, the exports,
and the packaged app for macOS, Windows and Linux with the Electron
security checklist, audited before the first release. The deferred
features (docs/DEFERRED-PLAN.md): part 1, sliding puzzles in the core, the
engine and `bt` (the start/goal model, the slide search, sliding solves
through the solve session, routes in the playback, the `sliding.*`
methods, decision 0014), audited; part 2, the sliding front end (the
start/goal editor, the sliding solver options, the trays and the walls in
3D, the notices, New offering the sliding grid; docs/webui-spec.md section
8); part 3a, stacking and Panex puzzles in the core and `bt` (rod sets and
stacks modelled, the Stacking Solver's search, the Panex search with its
saved searches, blockpack, stacking solves through the solve session, the
playback in transfers, the paused-solve file's saved searches, decision
0015); part 3b, stacking in the document's commands and the engine (rod
sets, discs and stacks as undoable commands, the `stacking.*` methods,
stacking solves in the engine with their saved searches in its Panex
folder), audited; part 4, the stacking front end (the disc list, the Rods
card, the board and its stacks, the stacking solver options, the saved
search's question, the player in transfers, every stacking view tilted as
the original's, the saved searches in the app's own data folder;
docs/webui-spec.md section 9); part 5, the STL meshes of every grid (the
cube grids' chamfer table and the Minkowski mesher on Manifold, the STL
file as the original wrote it, the view's STL-like style; `export.*`;
both STL dialogs; docs/webui-spec.md section 10); part 6, the imports
of PuzzleSolver3D and Puzzlecad files, the Puzzlecad export and the
conversion of cube-grid puzzles to the rhombic and tetra-octa grids
(`document.import`, `document.exportPuzzlecad`, `document.convertGrid`;
the File ▸ Import and Export items and Edit ▸ Convert; decision 0017,
docs/webui-spec.md section 11); part 7, the Python module (`python/`:
the original module's classes and names over the new core, sliding and
stacking solves through the solve session, part 6's imports and
conversion, a wheel; decision 0018). The deferred plan is done.*

## Big picture

```
 web front end (web/: Electron + TypeScript + three.js; the client, the shell, the 3D view)
        │  JSON-RPC 2.0 over stdio, binary frames for meshes   (decisions 0003, 0012)
        ▼
 shapelogic_engine  (engine/)        bt  (cli/, batch solving, burrTxt-compatible --json)    shapelogic  (python/, the Python module)
        │                         │                                                       │
        └──────────┬──────────────┴───────────────────────────────────────────────────────┘
                   ▼
            libbtcore (core/)
   app     Document, undo/redo, editing commands, queries, playback, explorer
   solve   assemblers, disassemblers, rotation rules, SolveSession
   mesh    half-edge meshes and drawing meshes for all grids
   io      XML, gzip, .xmpuzzle reader/writer
   model   Puzzle, Problem, Assembly, Disassembly, Solution
   grid    grid types, voxel spaces, symmetry tables
   base    StopToken, SolveState, Result/Error, BT_ASSERT, thread pool
```

Dependencies point downwards only: `app` may use everything below it, `base`
uses nothing. `core/` has no UI, does not print, and has no global state.

## Directory layout

| Path | Contents |
|---|---|
| `core/shapelogic/<module>/` | the library, headers and sources together; include as `"shapelogic/<module>/x.h"` |
| `engine/` | `shapelogic_engine`: JSON-RPC 2.0 over stdio around one `Document` (decision 0012); `btengine`, the same as a library for the unit tests |
| `cli/` | `shapelogic_cli`: the original's burrTxt for brick puzzles (the same options and text, `--json`), and burrTxt2's solve-and-save mode (`--save`) |
| `python/` | `shapelogic`, the Python module (pybind11, decision 0018): `src/` the bindings, `tests/` its tests, `pyproject.toml` its wheel; see `python/README.md` |
| `api/` | the JSON Schema of the engine's methods and events, its validator (`apischema.py`) and the generator of `docs/API.md`; see `api/README.md` |
| `web/` | the web front end (Electron + TypeScript + three.js, decision 0013): the engine client, the host and bridge, the generated types, the app (shell, 3D view), its tests (unit, engine, e2e); see `web/README.md` |
| `tests/unit/<module>/` | Catch2 tests, one binary `bt_tests` (`app`, `base`, `engine`, `grid`, `io`, `mesh`, `model`, `solve`) |
| `tests/contract/` | the engine's contract tests: `shapelogic_engine` driven as a subprocess, every message checked against `api/` |
| `tests/cli/` | `test_cli.py` (ctest `cli`): `shapelogic_cli`'s options, refusals and modes on the fixtures |
| `tests/oracle/` | the comparison harness against the original BurrTools |
| `tests/bench/` | `bench.py`: solve speed against the original, back to back |
| `tests/data/` | test fixtures: `puzzles/` (copied from the original), the original's recordings (`grid_dump.txt`, `model_dump.txt`, `app_dump.txt`, `saved_by_original/`, `positions_by_original.txt`, `analysed_by_original/`) |
| `tests/tools/` | small programs the harness drives (`bt_roundtrip`, `bt_resume`, `bt_solutions`) |
| `docs/` | this file, PLAN.md, FRONTEND-PLAN.md, API.md (generated), decisions/, handoff/, webui-spec.md, mockup/ (Girish Sharma's mockup, vendored), core-api.md (the draft API.md replaced) |
| `cmake/` | warnings, sanitizers, dependencies, whether the Python module is built (`Python.cmake`) |

## Modules

### base (exists)
- `stop.h`: `StopSource` / `StopToken` / `StopKind` (decision 0004). One source
  per run, requests are never withdrawn, child sources for stage-level stops.
- `solve_state.h`: `SolveState` and `SolveStateCell`, the transition table.
  Only a run's worker writes it.
- `error.h`: `Errc`, `Error`, `Result<T>` (`std::expected`), `fail()`.
- `assert.h`: `BT_ASSERT`, `BT_ASSERT_MSG`, `InternalError`; on in every build.
- `version.h`: `kVersion`, from CMake.
- `int_cast.h`: `asInt()` / `asUnsigned()`, the spelled-out int/unsigned
  conversions that ported code needs under `-Wsign-conversion`.
- `clone_ptr.h`: `ClonePtr<T>`, an owning pointer that copies its object
  (with `clone()` when T has one), so classes with polymorphic or recursive
  members keep `= default` copies.
- `thread_pool.h`: `ThreadPool`, the threads of a solve (from the original's
  `helperPool_c`): made by the owner of the solve (the solve session, or a
  tool) with its thread budget,
  `run(n, job)` runs a job on the caller and on lent threads, `addLoad()`
  for work on threads of its own; nesting cannot deadlock. `kMaxThreads`
  (256), `hardwareThreads()`, `splitThreads()` (assembly / take-apart share
  of a budget). No process-wide limit, no `SHAPELOGIC_THREADS` (decision 0009).
- `stop.h` also makes a source from a token (`StopSource(const StopToken&)`):
  each search run stops itself through one without a flag of its own.

### grid (exists)
Ported from the original `voxel*.cpp`, `symmetries_*.cpp`, `tabs_*`,
`gridtype.cpp`, `voxeltable.cpp`; checked value for value against the
original (`tests/data/grid_dump.txt`, see "Testing against the original").
- `grid_type.h`: `GridKind` (the numbers are the file format's) and
  `GridType`, a small value type: capabilities, `getSymmetries()`, and
  `makeVoxel()`, the factory for voxel spaces. The sliding and stacking
  grids are cube spaces; sliding puzzles are solved (model's `sliding.h`,
  solve's slide search), and stacking ones (model's `stacking.h`, solve's
  stacking and Panex searches).
- `voxel.h`: `Voxel`, the abstract voxel space: cells (state + colour per
  byte), bounding box, hotspot, name, weight, the per-transformation hotspot
  and bounding-box queries, comparison, connectivity, and the editing
  operations (`translate`, `resize`, `minimizePiece`, `fillHoles`,
  `actionOnSpace`, `scale`, `scaleDown`, `resizeInclude`, `unionintersect`).
  It also carries the sliding/stacking data (goal marks, disc size, shape
  colour) and the file attributes it does not know, so files round-trip.
  Copy with `clone()`; the grid is stored by value, so a voxel space never
  dangles.
- `voxel_bricks`, `voxel_prisms`, `voxel_spheres`, `voxel_rhombic`,
  `voxel_tetra_octa`: one class per grid (`voxel_0` … `voxel_4` in the
  original); rhombic and tetra-octa derive from bricks and lay a 5- or
  6-aligned super-grid over cubes.
- `symmetries.h`: `Symmetries`, the transformation and symmetry-group tables
  of a grid (`transAdd`, `minimizeTransformation`, `calculateSymmetry`, …).
  `symmetries_impl.h` is the table-driven implementation; `symmetries_0/1.cpp`
  instantiate it over the generated tables in `tabs_0/1/` (copied as they
  are; see `core/shapelogic/grid/TABLES.md`). Spheres have their own
  (`symmetries_2.h/.cpp`, decision 0020): the generated groups in `tabs_2/`
  are not every list a sphere shape can have, so a registry appends the
  ones met at run time (lock-free reads, `Symmetry` is 16 bits). The
  instances are constant-initialized statics (`constinit`: no guard on use, which the
  assemblers' rotation checks call in their inner loops), reached through
  constant-initialised pointers so that `GridType::getSymmetries()` is an
  inline load (stage 8: the rotation check of every assembly asks for it).
- `voxel_table.h`: `VoxelTable`, a hash table that finds a shape in any
  orientation; it takes a callback from index to shape.
- `bitfield.h`: `Bitfield<N>`, a constexpr bit set (the sphere tables).

**Caches and threads.** `Voxel` computes its symmetry group and the hotspot
and bounding box of each transformation lazily inside const functions. A
shape read by several threads must have `warmCaches()` called first (a
split search calls `warmShapeCaches()` on the result and the parts before
its threads start, and a disassembler when it is made); editing a shape
invalidates the caches.

**Differences from the original** (deliberate; listed in
docs/handoff/stage-1.md): `getHotspot` of a transformation the shape cannot
take reports failure regardless of call order; the cube transform drops the
transformation cache; `voxel_c::copy()` (unused) was not ported.

### model (exists)
Value types for a puzzle and what solving found. Ported from `puzzle.cpp`,
`problem.cpp`, `solution.cpp`, `assembly.cpp` and the data part of
`disassembly.cpp`; checked value for value against the original
(`tests/data/model_dump.txt`).
- `puzzle.h`: `Puzzle`: grid, shapes (`ClonePtr<Voxel>`), constraint
  colours, problems, comment, and a stacking puzzle's rod sets
  (`stacking::RodSet`; one made with the puzzle, or with its reading when
  the file has none; `removeRodSet` and `exchangeRodSets` renumber the
  problems'). Copying copies everything. Removing or
  swapping shapes goes through the puzzle, which updates every problem.
  `replaceShape` puts another voxel space in a shape's place (undo).
- `problem.h`: `Problem`: parts (`Part`: shape, min, max, groups), result
  and goal shape, colour rules, max holes, `ProblemState`, counts and time,
  the saved search position (`getAssemblerState()`/`Version()`,
  `setAssemblerState()`: the text an assembler wrote, see `solve`), pending
  assemblies, saved solutions with their sort orders, solver options, and
  a stacking problem's rod set and start and goal stacks
  (`stacking::StackMap`, kept in step when a shape goes, two swap or a
  count is lowered: `trimStacks`). A problem holds no reference to its puzzle: what needs
  shapes or colours takes the `Puzzle` (`resultShape(puzzle)`,
  `placementAllowed(puzzle, ...)`, `setShapeMaximum(puzzle, ...)`).
  Pieces are numbered part by part; assemblies and solutions follow that
  order and are renumbered when parts change.
- `assembly.h`: `Placement`, `MirrorInfo`, `Assembly` (with `transform`,
  `smallerRotationExists`, `createSpace`, `sort`, the editing helpers).
- `disassembly.h`: `Disassembly` (the level and move counts),
  `State`, `Separation` (the tree, copyable), `SeparationInfo` (the compact
  pre-order form). `movesText()` keeps the original's 256-byte cut-off.
- `problem.h` also gives `solutionSortOrder(by)`, the comparison
  `sortSolutions` sorts with, for a sorted view (the engine's paging).
- `solution.h`: `Solution`: an assembly with an optional `Separation` or
  `SeparationInfo`, its numbers, and a runtime id its problem gives it
  (not saved; the app layer's solution id).
- `xml_element.h`: `XmlElement` (the tree the reader builds) and
  `PreservedXml` (file content a model object keeps without modelling it).
- `sliding.h` (deferred part 1, decision 0014; the tray part of
  `sliding.{h,cpp}`): a sliding puzzle's start/goal shapes (names
  `sg:<n>:`; a tray whose empty cells are walls and variable cells
  corridors, each cell's colour the start mark of a piece and its goal
  mark the goal of one), the stamps (`stampPiece`, `stampFits`,
  `stampIsAt`, `clearStamp`, `stampAt`, `dropChangedStamps`),
  `syncSlidingProblems` (one problem per start/goal shape, its parts
  following the stamps), the start locks (`refreshStartLocks`: pieces
  painted in the colours of their starts, so the assembler finds only
  start layouts), `startGoalError`, `stampOverflowMessage`,
  the older goal-map model the solver still reads
  (`ensureSetup`, `placeStart`, `placeGoal`), and `prepareForSolving`
  (the maximum of holes and the start locks, what the solve thread set up
  as a run started).
- `stacking_types.h` and `stacking.h` (deferred part 3a, decision 0015;
  `stacking.{h,cpp}` but its search): a stacking puzzle's boards
  (`RodSet`: rods, height, the size and distance rules, Panex columns
  with a pocket and an outside channel; `totalRods`, `isPocket`,
  `rodPosition`, `rodCapacity`), its discs (shapes one cell thick whose
  size is their radius: `generateDisk`, `addDisk`, `ensureHotspot`,
  `placeSolvingHotspot`), the stacks (`DiskRef`, `StackMap`; `syncMaps`,
  `placeDisk`, `liftTop`, `moveDisk`, `dropInstance`), `setupError` (the
  validity bar's sentence), `layoutBoard` (where rods and discs stand),
  and what the searches read and make (`discsOf`, `moveAllowed`,
  `searchInput`, `pathSeparation`: three frames a transfer, lift, cross
  and drop; `logicalMoves`, `startAssembly`, `boardSpan`).

- `grid_convert.h` (deferred part 6; `converter.cpp`): `convertGrid`, a
  cube-grid puzzle on the rhombic (5x5x5 cells a cube) or tetra-octa
  (3x3x3) grid, the colours, the notes and each problem's definition
  copied (`copyDefinition`, with its name); `convertTargets` (what the
  original's dialog offered); a budget of cells (`kMaxConvertedCells`).

The model has no locks and no atomics (the original's problem had both for
its solver thread). While a `SolveSession` runs, it alone writes its
problem, under its own lock; others read through `readProblem()` (see
solve).

### io (exists)
`.xmpuzzle` files: gzip-compressed XML, plain XML read too (decision 0008).
- `puzzle_file.h`: `loadPuzzle` / `parsePuzzle` / `readPuzzle` and
  `savePuzzle` / `writePuzzle`. Errors are `Result` values (`ParseError`
  with the line, `IoError`).
- `xml_pull_parser.h`: the original's parser (internal; throws
  `XmlParseError`, which never leaves the module). `xml_document.h`: the
  tree built from it, and writing a tree back.
- `xml_writer.h`: `XmlWriter`, the original's output layout.
- `gzip_file.h`: whole-file reading and writing through zlib (level 9),
  and the same in memory (`gzipCompress`, `gunzipIfCompressed`).
- `paused_solve.h`: the paused solve file (`.slsolve`, the original's
  BTSOLVE2: the gzip-compressed puzzle, the paused problem's number, and a
  stacking solve's saved search as `searchz/<name>` sections packed by
  blockpack, in little-endian sections; other sections skipped), both
  ways, streamed (a saved search's levels can be gigabytes): written
  through `writeFileStreamed` (a seekable stream on the exclusive
  temporary file), read section by section into the folder an import is
  told.
- `blockpack.h` (deferred part 3a): files packed in chunks on a pool's
  threads, sorted 16-byte keys as variable-length gaps, byte for byte the
  original's packing.
- `imports.h` (deferred part 6, decision 0017): other programs' files,
  line for line the original's `ps3dloader.cpp` (`ps3d_file.cpp`:
  `readPuzzleSolver3D`) and `scadloader.cpp` (`scad_file.cpp`:
  `readPuzzlecad`, `writePuzzlecad`, puzzlecad's `burr_plate` of a
  problem's pieces); a cube-grid puzzle with one problem, and a budget of
  cells (`kMaxImportCells`) the original lacked. `gzip_file.h`'s
  `readPlainFile` reads such a file as it is, never decompressed.

Unknown attributes and elements of `<puzzle>`, `<problem>` and `<solution>`,
and unknown `<voxel>` attributes, are kept and written back in place.
Stacking rod sets and stacks are modelled (decision 0015) and written as
the original wrote them; what it dropped of them is dropped.
Saves are byte-identical to the original's. A file is written beside its
place (`<file>.tmp`) and renamed over it once whole, puzzle and paused
solve alike (`writeFileWhole`, `replaceWith`; the original's
writePuzzleFile), so a failed save leaves the old file; the temporary
file is the save's own, made exclusively after a file or link already at
its name is removed, so a link planted there is never written through
(the web stage 8 audit). The place is the file the path names
(`saveTarget`): a symbolic link is followed, so the link stays and the
file it names is written, as writing in place did; a read-only file is
refused (`IoError`), where a rename would replace it; and the new file
gets the permissions of the one it replaces (the web stage 6 audit; the
original's rename replaces a link and a read-only file, and gives the
defaults).

### solve (exists: stages 3 to 5)
The assemblers, on one run layer (decision 0009), and the take-apart. Ported from
`assembler.cpp`, `assembler_resume.h`, `assembler_0.cpp`, `assembler_1.cpp`,
`assembler_bt2.cpp`, `bt2_dancingcells.cpp`, `bt2_assemble.cpp`,
`simd_exact_cover.cpp`, `simd_huang_cover.cpp`, `solvertype.h` and the
assembler part of `gridtype.cpp`; checked against the original by the
harness (assembly counts on every tier, 1 and all threads; iteration counts of the deterministic searches equal; saved
positions continue in both programs).
- `assembler.h`: `Assembler`, the interface: `createMatrix(MatrixOptions)`,
  `reduce(stop)`, `assemble(callback, SearchOptions, stop) -> RunEnd`,
  `finished()` / `iterations()` (any thread), `savePosition()` /
  `restorePosition()` (the `<assembler>` text and version), the placement
  browser and the debugger (`debugStep`, `currentAssembly`).
  `AssemblerError` with the original's messages. `SearchOptions` carries the
  threads and pool and the A/B switches (`vectorSearch`, `kernels`,
  `vectorMemoryBudget`). The problem hooks: `beginSolving(problem,
  assembler)` starts or restores a solve with the original's failure rule (a
  position that cannot be restored drops all solutions and the position),
  `storePosition()` writes a paused one back, `canStartSolving()`.
  `warmShapeCaches()` before threads.
- `search_run.h`: the run layer. `SearchRun` (one `assemble()` call: its own
  stop source under the caller's token, the serialised hand-over to the
  callback: `deliver`, `deliverOnce` with signatures), `TaskProgress` (weighted
  task units, lock-free), `runTasks()` (unfinished tasks over the pool's
  threads; completed only when searched to the end without a stop; the
  first exception aborts the rest and comes back), `effectiveThreads()`.
- `resume_state.h`: `SplitSearchState<Task>` (tasks, completion, reported
  signatures, the vector search's skip count) and the saved-position tail
  (`K` / `T C S`), byte-compatible with the original.
- `rotation_filter.h`: `RotationFilter`, the check every assembler makes on
  the assemblies it finds (rotated or mirrored copies dropped), with the
  settings of a solve.
- `dlx_matrix.h`: `DlxMatrix` (the dancing links matrix of assembler 0,
  built node for node as the original: `build`, `reduce`,
  `clumpify`, the placement browser) and `DlxNodes` (the four node arrays
  with the cover operations, which a thread copies to search on its own).
  `everyPartOnePiece()`, the problems it can describe.
- `assembler_0.h`: `Assembler0` (dancing links; every part exactly one
  piece). Three searches: plain on the caller's thread (flag 0 positions),
  split into subtree tasks over threads (flag 2), and the vector search
  alone or inside each task (flag 3).
- `assembler_1.h`: `Assembler1` (Wei-Hwa Huang's cover with counts: columns
  with a minimum and maximum weight; piece counts and ranges, holes; every
  problem assembler 0 cannot take). The plain search is a state machine
  over explicit stacks (`SearchStacks`) that stops only at a state it can
  save (flag 0); split into tasks that are stacks too (flag 4, or 2 for the
  older signatures by row order); the vector search `SimdHuangCover`, alone
  or in each task, refused past `vectorMemoryBudget` (flag 3). A saved
  position is checked against the matrix before it is used. The plain
  search's progress is published by its thread every 4096 steps.
- `placement_finder.h`: `PlacementFinder`, the placements of a rotated piece
  in the result, in the original's order.
- `simd_bitset.h`, `simd_exact_cover.h`, `simd_huang_cover.h`: the vector
  covers (assembler 0's exact cover, assembler 1's cover with counts and
  ranges, `makeSimdHuangCover`, `simdHuangMemoryEstimate`), 256 to 32768
  columns, AVX2/AVX-512/NEON kernels behind runtime checks with a scalar
  fallback, chosen through `SimdKernels`. Read only during a search; threads
  search subtrees of one solver.
- `solver_type.h`: `SolverType` (Classic, Crowell; labelled Complete (ShapeLogic) and
  Fast (Crowell)) and its names, `chooseAssembler()` / `makeAssembler()`
  (findAssembler: assembler 0 when every part is one piece, else assembler 1).
  The original's BurrTools 2 solver (dancing cells) was not carried over.

**Rules of a run** (decision 0009): the search polls only its run's token;
a callback answering false pauses the run, an exception aborts it; nothing
is reset, the next `assemble()` carries on; `RunEnd::Finished` only from the
search itself; a stopped split or vector search continues the way it ran.
A search may hand over assemblies it reaches after the pause was asked for
(assembler 1's plain search until it stands where it can be saved): those are not found again. The assembler never writes into
the `Problem` (results go to the callback; the CLI or the session owns
the problem).

**The take-apart** (stage 4). Ported from `disassembler*.{h,cpp}`,
`disassemblernode`, `disassemblerhashes`, `movementcache*`,
`movementanalysator`, `grouping`, `rotationrules`, `rotationmoves_*` and
`disasmtomoves`; checked against the original by the harness (every field of
the summary on all 180 corpus puzzles, Classic and Crowell, 1
and all threads; the solved files solution by solution, `compare-files`).
- `disassembler.h`: `Disassembler`, the take-apart of one problem's
  assemblies: `disassemble(assembly, stop) -> TakeApartResult` (the
  `Separation`, or none, and whether it was stopped), `progress()`
  (`DisassemblyProgress`, from any thread), the search times.
  `DisassemblerOptions`: rotations, the solver (Crowell takes at most
  `kCrowellMaxSuccessors` new positions from each position and has its own
  turn generator; Classic is complete), the `ThreadPool`
  a level is spread over (`levelThreadCostUs`: only when what is left of a
  level is reckoned to take longer), and the A/B switches.
  `canDisassemble()`: cubes and prisms. The search is breadth first, level
  by level, in three fronts; spread over threads it gives exactly the
  result of one thread (positions of a level are expanded on several
  threads and put together in the order one thread would have found them).
  A separating position splits the problem into two parts, each taken
  apart in turn (`Grouping` for pieces that may stay together).
- `disassembler_node.h`: `DisassemblerNode` (the pieces' places and
  orientations, the position it came from, the move) and `NodeRef`, a
  counted reference (not atomic: a node's count is touched by one thread at
  a time, see `Disassembler`). `node_hash.h`: `NodeHash`, `CountingNodeHash`,
  sets of positions compared relatively (the whole puzzle shifted is the
  same position).
- `movement_analysator.h`: `MovementAnalysator`, Bill Cutler's moves from
  one position (removals, groups, slides with merging, then turns); one per
  thread. `movement_cache.h`: `MovementCache` with `MovementCacheBricks`
  and `MovementCachePrisms`, `makeMovementCache()`.
- `rotation_rules.h`: `RotationRules`, whether a 90 degree turn of a body is
  possible (sandwich, perpendicular planes, axis cross, the bevelled arc
  sweep with wiggles), bit for bit the original's answers.
  `rotation_moves.h`: the `RotationMoves` generator interface;
  `RotationMovesClassic` (complete: every group about every pivot, one
  position per arrangement with the anchor) and `RotationMovesCrowell` (the
  Fortran solver's heuristics; notes in `CROWELL.md`). A position of more
  than `kMaxTurnPieces` (31) pieces gets no turns (the pieces of a turn
  are a 32-bit mask; the original overflowed it).
- `grouping.h`: `Grouping`, groups given to sets of pieces, keyed by part;
  every take-apart starts with the full counts (the original keyed by
  shape id but asked by part, and kept the counts used up: stage 4
  audit).
- `disasm_to_moves.h`: `DisasmToMoves` (piece positions at any step of a
  tree, turns as angles about a pivot) and `FixedPositions`, behind
  `PiecePositions`; stage 6 builds the playback sampler from it. A state
  without orientations (a file without `<dt>`: every classic solution read
  back) gives none (`getTrans` "not given"), so a player keeps the
  assembly's orientations; the original turned such pieces to
  orientation 0 (decision 0013, item 7).

**Rules of a take-apart:** one `StopToken` per take-apart (polled by the
search, the analysators and the turn generators); a stopped take-apart
returns no separation and says so (`stopped`), which is not "cannot come
apart"; levels spread over the pool given, nested inside the assembler's
callback without deadlock; the shapes' caches are warmed when the
disassembler is made. Nothing is printed and nothing read from the
environment (the original's switches are options).

**The slide search** (deferred part 1, decision 0014). Ported from the
search part of `sliding.{h,cpp}` and the sliding run of `solvethread.cpp`;
checked by the harness (the `sliding` tier: the summaries, the solved
files state by state, the text, the routes; the full search, the only
one).
- `sliding_search.h`: `findSlidePath(puzzle, problem, start, SlideSearch&)`,
  the fewest moves from a start layout (an assembly) to the goal: a piece,
  or with nested slides the pieces it carries, slides cell by cell,
  crossing corridors, in its layer; with `verticalMoves`, on a tray of
  several layers, up and down too (lifted out and put back); positions keyed in 64 or 128 bits (the original's
  text keys with `legacy`; puzzles of more than 64 pieces with nested
  slides use those, where the original overran its tables); copies of
  like pieces searched as one. There is no limit on the arrangements
  visited (the original's Fast and Deep solvers, which stopped at 250,000
  and 1,000,000, and its queue search are not kept): `SlideSearch` holds
  the memory budget (`memoryStates`, high memory), the stop token, the
  progress counters, the pool, threads, work folder and disk budget, and
  answers with a `SlideOutcome` (Found, NoPath, Memory, Stopped, Disk) and
  its counts. The search goes level by level (one-way moves included),
  keeps the older levels in `KeySet` files (`slide_levels.h`) in its work
  folder and spreads each level over the pool; any number of threads
  finds the same path. `SlidingOptions` and `slideSearchFor()`: the GUI's
  boxes for its one solver, the Sliding Puzzle Solver
  (`kSlidingSolverName`). `slideRoute` and `applySlideRoutes`: a move's path
  through corridors (its corners) and the player's driver following it;
  `finalPlacementKey`: two solutions that end alike are one (the shorter
  kept, in its place).
- In `SolveSession`, a sliding problem runs as the solve thread ran it:
  the assembler (Classic; mirrors and rotations kept, colours strict, as
  the solve thread forced) finds the start layouts and each is searched
  in its callback on the session's slide pool; a stopped search puts its
  start back as pending, not counted; a save point lets the search under
  way finish; `note()` is the original's summary (starts the memory or disk cut,
  everything searched) and the progress has stage `Other` with `searched`
  and `searchDepth`. Take-apart workers are not used.

**The stacking searches** (deferred part 3a, decision 0015). Ported from
`stacking.cpp`'s search and `panex.{h,cpp}`; checked by the harness (the
`stacking` tier for both solvers: the summaries, the solved files state
by state, the text; `panex-resume`: searches stopped in one program at a
sweep of depths and carried on in the other, the saved folders byte for
byte alike).
- `stack_search.h`: `findStackPath` (the Stacking Solver's own breadth-
  first search over stackings, packed keys or text keys, up to a memory
  budget; `StackSearch`, `StackOutcome`), and the stacking solve's
  options (`StackingSolver`, `StackingOptions`: the solver, high memory,
  the search's autosave, the work folder and disk budget, the tower rule).
- `panex_search.h`: the Panex search (`panex::solve`, `PanexSearch`,
  `Outcome`): 128-bit keys, level by level from both ends (one end and
  its mirror image for a swap, twin stackings folded), the newest levels
  in memory as sorted arrays, every few levels on disk once they outgrow a
  quarter of the budget (thinned to a disk budget), the path traced back
  between them, or each half solved again when there is no folder; the
  classic tower answered by its recursion; a stop (or the memory) saves
  the search in its folder (`BTPANEX4`, the original's bytes), and
  `resume` carries it on; `unsupported`, `savedSearch`, `savedMs`,
  `searchFolder`, `discardSaved`. Its steps run on a `ThreadPool` (slices
  taken from a counter, the newest level written by the next level's first
  job, the pool a core larger than the levels' threads for it, as the
  original had a writer thread); any number of threads gives the same path.
- In `SolveSession`, a stacking problem runs as the solve thread's
  `runStacking` ran it: no assembler, one start searched (the Panex search
  when the Panex Solver is asked for or the puzzle fits it, else
  `findStackPath`), the path the one solution with the start stacking as
  its assembly, `note()` the original's notes, the progress stage `Other`
  with the stackings found, the depth and the moves traced back
  (`searchTraced`, `panexSearch`). Launch checks what Continue checked and
  sets the discs' hotspots (only those elsewhere); `start()` throws a
  saved search away first, `resume()` carries it on, `abort()` throws it
  away; a stopped search ends the run Paused (the problem written as the
  solve thread left it: solved, one start, no solution). `prepare()` is
  refused and a save point does nothing (the search saves itself).

**The solve session** (stage 5, decision 0010). Ported from `solvethread.{h,cpp}`,
`solveprogress.h` and the parts of the original GUI that drove the solve
thread (Solve, Prepare, Continue, Abort, the autosave pause,
recordSearchStats); checked by the harness (`compare-files` solves with it)
and by pause/continue stress tests.
- `solve_session.h`: `SolveSession`, the solve of one problem across all
  its runs: `start()` (from scratch), `prepare()` (matrix, reduce, then
  Paused), `resume()` (a paused run in memory, the problem's saved
  position, or an unsolved problem), `pause()`, `pauseAtSavePoint()` (the
  autosave pause: the assembly stops, the take-aparts finish, Paused with
  the position in the problem; the owner saves and resumes), `abort()`
  (stop and drop everything), `wait()`; `state()`, `progress()`, `stats()`,
  `failure()`, `readProblem(f)` and `setLiveSort()` from any thread.
  `SolveOptions` (the solve thread's PAR_ flags, `SolutionOrder`, the
  limit and drop of kept solutions, threads, take-apart workers, the
  assembly's and take-apart's A/B switches); `SolveFailure` (an
  `AssemblerError` of the preparation or the saved position, or an
  internal error with its message, file and line).
- `solve_progress.h`: `SolveProgress` (stage, counts, the take-aparts under
  way, the overall estimate that never goes back, time left, `activity()`
  text) and `SolveStats` (the debug panel's counts and times).

**Inside a run** (one thread of the session's own per run): moved
hotspots are reset (before the matrix, and on every run); Preparing makes the
assembler, `createMatrix`, `reduce`, `beginSolving` (start or restore);
Assembling takes the problem's pending assemblies first, then runs the
assembler on a pool of its threads; each assembly goes to a bounded queue
(at least 64) that take-apart workers (one `Disassembler` each, their
levels spread over a second pool that counts the assembly's and the busy
workers' load with `addLoad`) empty; an assembly thread waits at a full
queue and lends its cores meanwhile. Solutions go into the problem sorted
(by moves, level or turns; equals by assembly number) up to the limit, or
unsorted and thinned evenly. Draining waits for the queue; the run ends
Finished (the search said so and all is taken apart), Paused (the position
written into the problem, queued and stopped take-aparts back in the
pending list, counted), Idle (aborted: the problem cleared) or Error (the
results dropped).

**Rules of the session:** the owner's calls (`start`, `prepare`, `resume`,
`abort`, `wait`, `setOptions`) come one at a time; the first state move of
a run is made before its thread starts, so an active state is seen at once;
after that only the run's thread writes the state. One `StopSource` per
run with a child for the assembly (save points); waits on the queue wake
on a stop (requested under the queue's mutex). Assemblies handed over after
a pause are kept as pending, not counted. A paused session carries on with
the same assembler; a session on a reloaded file restores the position. A run that ends with counted assemblies still waiting (it
did not take apart) ends Paused, not Finished.

**Lock order** (outer before inner; nothing takes a session lock under an
assembler's or disassembler's, so there is no inversion): the owner's
control lock around the results, run and failure locks; the results lock
around `beginSolving` (an assembler 1 replay included, which holds up
`readProblem` and `progress()` for that time) and `storePosition`
the run lock around the published assembler's
`finished()`/`iterations()` and the disassemblers' progress and times
(their own locks); the queue lock around a stop request. Mirror and
rotation filter flags are applied to a kept assembler at every run.

### mesh (exists: stage 6)
The meshes of the shapes as the original's 3D view drew them. Ported from
`src/halfedge/` (polyhedron, face, half-edge, vertex, the hole filling of
`modifiers.cpp`), `voxel_c::getMeshInternal` / `getDrawingMesh` /
`getWireframeMesh` / `getFlatMesh`, the cells' face geometry of
`voxel_0` … `voxel_4` (`getConnectionFace`, `calculateSize`,
`recalcSpaceCoordinates`, with their `meshverts.inc` tables) and
`voxel_2_mesh.cpp`; checked against the original (`tests/data/app_dump.txt`:
the drawing and flat meshes of every fixture shape face by face, the wire
frame by its Euler characteristic).
- `polyhedron.h`: `Polyhedron`, the half-edge mesh by numbers instead of
  pointers (faces and half-edges keep the original's list order: erase
  moves the last into the gap), `finalize()` (twins, an even number of
  faces at an edge sorted by angle, holes closed), `VertexList`,
  `eraseFaces`; `FaceFlag` (the view's face flags). `vec3.h`: `Vec3`, with
  the original's float arithmetic.
- `modifiers.h`: `fillPolyhedronHoles` (the wire frame: bevel and gap strips
  merged, holes capped, never across a concave corner).
- `grid_geometry.h`: `connectionFace` (the corners of a cell's face towards
  a neighbour, or of a bevel face), `calculateSize`,
  `recalcSpaceCoordinates`, for every grid (the grid module has no
  geometry).
- `voxel_mesh.h`: `shapeMesh(voxel, MeshStyle)` (Drawing, Wireframe, Flat),
  `cellMesh` (getMeshInternal), `sphereMesh` (the balls of the sphere grid;
  its expressions are kept as the original's, float and double as they
  were, with the conversion warnings off for that file only).
- `draw_mesh.h`: `DrawMesh`, a mesh as plain arrays for a binary frame:
  triangles (the view's fans) with flat normals, and per triangle the cell
  and side, flags, colour and polygon; the edge lines of the "edges"
  style; the hotspot of the space it was made from (an assembly puts that
  on a placement's cell). `shapeDrawMesh`.
- `cube_mesh.h` (deferred part 5): the STL mesh of the cube grids
  (bricks, sliding, stacking; cubemesh.cpp, cubepoly.cpp):
  `cubeSurface` puts the chamfered surface inside the dual cell around
  each cell corner from the generated table (`tabs_cube/`, copied
  unchanged: per vertex body and r/g regime, polygons whose corners are
  affine in the gap and the bevel), welds it, merges coplanar polygons
  into faces and triangulates them (`triangulate.h`: ear clipping with
  holes bridged, Eberly's rule); `cubePolyhedron` tags each triangle with
  its cell and side as the view picks.
- `mink_mesh.h`: the STL mesh of the prism, rhombic and tetra-octa grids
  (minkmesh.cpp): the exact surface of the cells as a Manifold solid,
  shrunk and chamfered by Minkowski sums and differences with small
  convex bits (cube and cuboctahedron, or the hexagonal prism and
  bipyramid; one hull per convex piece of each planar face, a turn of
  1e-8 against coincident faces), every step put back onto its planes
  (edges contracted, vertices snapped, faces triangulated again), a step
  with a fold redone with the bit turned another way. Manifold 3.5.2 is
  the only third-party library of the core (cmake/Dependencies.cmake).
- `stl.h` (decision 0016): each grid's STL parameters as the original's dialog showed
  them (`stlParameters`: stl_0's six, the sphere grid's eight),
  `stlMesh` (the mesh for values, with the original's refusals; more than
  6 sphere recursions refused, unlike the original), `stlFile` (binary
  or text, in memory, after `mergeCoplanarFaces`). `modifiers.h` also has
  `scalePolyhedron`, `mergeCoplanarFaces` and `volume`.
  `MeshStyle::Stl` is the view's STL-like style (getSTLMesh: the meshers
  at gap 0.02, bevel 0.05 with interior chamfers; the flat mesh where
  they fail). All of it gives the original's bytes for every fixture
  shape (`tests/data/stl_dump.txt`).

### app (exists: stage 6)
What the front end does to a puzzle (decision 0011). Ported from the
original GUI (`mainwindow.cpp`'s callbacks, `shapehistory.cpp`,
`tooltabs.cpp`, `grideditor.cpp`, `statuswindow.cpp`,
`placementbrowser.cpp`, `movementbrowser.cpp`, `mainwindow_solve.cpp`,
`mainwindow_file.cpp`) and `gltfexport.cpp`'s sampler.
- `document.h`: `Document`: the puzzle, the ids of its shapes, colours and
  problems (a solution's id is `Solution::getId()`), the revision, the
  dirty flag, the undo history (each step keeps the parts it changed and
  swaps them back and forth; groups for a drag, in which every command
  that changes something still gets a revision of its own and its
  `lastChange`, though a part is saved once; a depth), every editing
  command of core-api.md sections 1 to 4 and the solution commands
  (delete, drop disassemblies, assemblies to shapes) and the stored
  Solver tab settings (`setSolverOptions`), each a `Result`, one
  step; the queries; one `SolveSession` per problem being solved (start,
  prepare, resume, pause, save point, abort, wait, state, progress, stats,
  failure, `readProblem`); the analyses; `poll()`, which puts in what
  background work did (an analysis's separations, the end of a run) as a
  revision of its own; the paused solve files (`exportPaused`,
  `importPaused`, a new untitled document); the imports (`importFile`,
  `importBytes`: a new untitled document with unsaved changes), the
  Puzzlecad export (`exportPuzzlecad`, the problem read through
  `readProblem`, allowed while solving) and grid conversion
  (`convertGrid`: a new document, refused while solving; deferred part
  6); playback and explorers. The
  rules for edits while solving are decision 0011; they hold while an
  analysis runs too (`solving()` covers both). Sliding puzzles (decision
  0014; `document_sliding.cpp`): `addStartGoal`, `placeStamp` (the
  editor's click), `clearStampAt`; the shape commands keep stamps and
  problems in step (deleting, moving, duplicating and renaming a
  start/goal shape; an edit that changes a stamped piece's shape takes its
  stamps off and says so in `ChangeSet::notice`, as does moving a stamped
  piece past the 63rd place, a mark being six bits); sliding shapes may
  have more than one layer and keep them on open (decision 0014); what
  the original greyed out or hid is refused (the result, groups, the count
  of a piece a stamp requires, new, copied and deleted problems, deleted
  colours); `solveLaunch` sets up the start locks as part of the run (no
  undo step), after its checks. Stacking puzzles (decision 0015) solve
  through the same calls with `SolveOptions.stacking` (the discs' hotspots
  put at their centres before the first run, with the others' reset); a
  paused stacking solve is a saved search in the work folder, which
  `exportPaused` (`PausedExport`: the folder, the levels or not) and
  `importPaused` (the folder) carry in the file; the analysis and the
  explorers refuse them in the grid's words (no take-apart, no
  assembler). Their editing commands (part 3b; `document_stacking.cpp`,
  from mainwindow_rods.cpp's rod sets, the disc list and the stacking
  branches of the Problems tab; each one undo step, the GUI's own rod
  history become the document's undo): rod sets with ids beside the
  puzzle's (`rodSetIds`, labels R1: `addRodSet`, `duplicateRodSet`,
  `deleteRodSet` keeping one, `renameRodSet`, `moveRodSet`,
  `setRodSetRules` with cb_RodField's clamps and Panex dependencies,
  dropping the solutions of the problems on the set), discs (a new shape
  is a disc of size 1, `setDiscSize`), a problem's board
  (`setProblemRodSet`), `placeDisc` (placeOneDisk: one more copy when
  every copy is on that stacking), `placeAllDiscs` (largest first),
  `removeDisc` (from both stackings, one copy fewer), `clearStacks`
  (changeProblem), `moveDisc`; what the GUI hid is refused (the cell
  editor, the tool tabs, a shape of a given size, the result, the counts
  and ranges, the parts' order, the groups). `savedSearch(problem,
  folder)` describes a saved search (`SavedSearchInfo`; none while the
  problem's run is active) and `solveAbort(problem, folder)` throws away
  one that no session holds, with the problem's results (cb_BtnAbort).
  Identical problems (a copy) share one saved search, as in the original:
  one run at a time works on it (`sharedSearchActive`; Solve, Continue and
  Abort of the other refused meanwhile, its saved search not told).
  The STL export (deferred part 5): `stlMesh(shape, values)` (a mesh that
  fails one of the meshers' assertions refused with the words of the
  original's dialog, which caught them) and `stlPieces(problem,
  solution)`, under the solve's lock.
- `shape_tools.h`: the tool tabs' tasks per grid (`ShapeTask`, `taskSet`,
  `applyTask`, `minimizeShapes`; a nudge out of the grid is refused), the
  grid editor's cell tools (`CellTool`, mirror and span, `editCells`),
  `defaultShapeSize`, `sizeUnit`.
- `analysis.h`: `Analysis`, saved assemblies taken apart afterwards
  (cb_AddDisasm / cb_AddAllDisasm: all, those without their moves, or one)
  on a thread of its own with one `Disassembler` and a pool for its
  levels; progress, a stop that keeps what was finished, the separations
  by solution id. The document runs one per problem (`analyseStart`,
  `analyseStop`, `analyseProgress`) and puts the results in at `poll()`.
  Checked against the original's own re-analysis of the fixtures
  (`tests/data/analysed_by_original/`, byte for byte).
- `layer_geometry.h`: `layerGeometry(space, layer)`, the grid editor's
  drawing of a layer (grideditor_0 to _4): each valid cell's outline,
  counter-clockwise (circles on the sphere grid), and the cell across each
  edge; in cell units (a rhombic block of 5 and a tetra-octa block of 3
  cells is one square). Checked against the original's corner formulas.
- `queries.h`: `problemFit` (`FitStats`: the result's and the pieces'
  cells, the piece counts, holes, the verdict, the original's status
  line), `shapeTable` (`ShapeInfo`: the status window's row),
  `shapeStatusLine`, `isNotchable`, `isMillable`, `problemKind`,
  `rodSetLayout` (a stacking puzzle's rod set drawn alone,
  voxelFrame_c::showRodSet), `stlPieces` (a solution's STL export: the
  shapes its assembly places, their counts and safe, distinct file
  names, stlexportsolution.cpp).
- `playback.h`: `PlaybackSampler` (from a solution: the pieces with their
  meshes in the assembly orientation, `sample(step)` giving each piece's
  translation, quaternion and visibility, turns tumbling about their
  pivots; `track()` the keyframes with holds and reassembly), over
  `DisasmToMoves`; a solution without its disassembly is the assembled
  pose alone (`stepCount()` 0), so every solution plays through one path.
  A sliding solution follows its routes (`applySlideRoutes`); a stacking
  solution's steps are its transfers (three frames each), its frames
  places, and its poses are centred on its board (`stackingCentre`).
  `orientedMesh(shape, transformation, style)`: a shape's mesh turned by
  a transformation of its grid (the piece meshes, and
  `Document::shapeMesh` with a transformation: how a placement is drawn:
  at its cell minus the turned mesh's hotspot, in mesh units).
- `explorers.h`: `AssemblyExplorer` (the placements of each piece, the
  assembly search one step at a time) and `MovementExplorer` (the tree of
  positions of a take-apart: analyse, add a move, find a matching one,
  prune; the grid's directions by label and by step); both work on a
  copy of the puzzle. A sliding problem's placements are those its solve
  sees (start locks, the solve's forced options).

**Rules of the app layer:** the owner calls the document from one thread;
a command checks first and changes after, so a refused one changes
nothing; every change is undoable except what solving and analysing do;
`solveAbort` is the hard stop of a solve under way (active or paused in
memory) and is refused otherwise, so a finished run's solutions go only
through the undoable `deleteSolutions`; names and notes refuse what the
file cannot hold (control characters; line breaks in names); while a run
or an analysis is active, shapes, lists and the solving problems are
frozen (decision 0011); runs and analyses only read shapes (the document
resets hotspots and warms the caches before the first one starts); a
solution is found by its id under the solve's lock (a run moves solutions
about); `Problem::solutionsVersion()` moves with every change of a saved
list, so a reader can tell two reads apart.

## engine (stage 7, decision 0012)
`shapelogic_engine` speaks JSON-RPC 2.0 over stdio: one request per line in, one
reply or notification per line out, binary frames (an announcement line,
then the bytes) for meshes and poses, logs on stderr. The methods are the
schema in `api/`, documented in the generated `docs/API.md`.
- `engine.h`: `Engine` (the dispatcher: `handleLine`, which parses with a
  depth cap of 64 and never trims a line in silence, `poll`, `run` with a
  reader thread that only queues lines, keeping at most 64 MiB of one and
  waiting when 1,000 lines or 64 MiB are queued; the main thread runs
  every request and owns the document and stdout), `State` (the document,
  its path, the settings, the handles of playbacks and explorers, what
  the events last told, the events queued for after a reply; a frame
  inside a notification is refused), `Settings`, the method table
  (`add…Methods` of each `methods_<area>.cpp`).
- `rpc.h`: the error codes (`Errc` → `-32001`…, protocol errors as
  JSON-RPC's, `data.errc` on every error), `Params` (reads a method's
  fields, refusing missing, mistyped and unknown ones as the schema does),
  the ids (`s7`, `c2`, `p1`, `sol42`, rod sets `r1`, handles `pb1`, `ax1`, `mx1`),
  `BTE_TRY` / `BTE_CHECK` (a core `Result` error becomes the reply).
- `output.h`: `Output` (lines and frames), `FrameBuilder` (typed arrays
  at multiples of four, described for the reply), base64.
- `convert.h`: the names of the schema's enums and the JSON of the items
  (`snapshot`, shape, colour and problem summaries, the status row, the
  fit, meshes as frames, the stored solver settings, solve options over
  the settings, progress, statistics, failures, solution rows,
  placements).
- `methods_<area>.cpp`: one per schema file. `engine.hello` (the
  protocol a client speaks), `document.openBytes` (with the file the bytes
  stand for and unsaved changes: a rescue copy, `Document::markUnsaved`),
  the imports and grid conversion (deferred part 6: `document.import` by
  path or bytes, without a path; `document.exportPuzzlecad`, the text,
  naming the document's file or the export's; `document.convertGrid`,
  which replaces the document and keeps its path, as the original kept
  its file name; `engine.info`'s `grids[].convertsTo`), solving (`solve.*` on the document's solve
  calls; a pause, a save point and an abort only for a solve under way),
  solutions (`solution.page` sorts the saved list's indices under
  `readProblem` with `solutionSortOrder`, a view, with the problem's
  `solutionsVersion`; the analysis, with its failure), shapes
  (`shape.mesh` in any transformation of the grid, with that orientation's
  hotspot in `MeshFrame.hotspot`; `grid.geometry`, the
  editor's drawing of a layer; `grid.neighbour`, the cell across a side,
  the sides numbered as a mesh's `voxelFace`), playback (samplers
  behind `pb` handles: poses and keyframe tracks as frames of typed
  arrays; a solution without a disassembly as a single pose; piece meshes
  in any style), explorers (`ax` and `mx` handles; the movement
  explorer names the grid's directions by label and by step, `vectors`:
  a prism's x step is 2), sliding puzzles (`sliding.*`: the start/goal
  editor's actions and reads; a sliding solve's options in
  `SolveOptions.sliding`, its note in `solve.status`; `Settings`'
  `slideFolder` and `slideDiskGB`, the original's SHAPELOGIC_SLIDE_DIR and
  SHAPELOGIC_SLIDE_DISK_GB, go into every sliding solve's options),
  stacking puzzles (`stacking.*`, deferred part 3b: the rod sets, discs
  and stacks commands and the reads of the board, `stacking.board` and
  `stacking.rodSetLayout`; rod sets in the snapshot, a problem's board
  and stacks in its summary and detail, a shape's disc size; a stacking
  solve's options in `SolveOptions.stacking`, the solver defaulting to the
  one the original's menu offered for the board; `Settings`'
  `panexFolder` and `panexDiskGB`, the original's SHAPELOGIC_PANEX_DIR and
  SHAPELOGIC_PANEX_DISK_GB, go into every stacking solve and into
  `solve.status.savedSearch`, `solve.abort` of a saved search no session
  holds, and the paused solve's export (`withLevels`) and import (refused
  while the open document is a stacking puzzle being solved: the import
  writes into the folder its search saves in); `solve.savePoint` is
  refused on a stacking solve, whose search saves itself; a stacking
  solution's row counts transfers; `Settings`' `searchMemoryGB`, half the
  machine's memory when 0, is every sliding and stacking solve's memory
  in place of its `highMemory`, the GUI's Search memory setting, and the
  notes of a search out of memory or disk name Settings ▸ Solver,
  `SolveOptions::settingsPlace`; the disk budgets go by the free space,
  `diskBudgetFor` in base/system.h), and the STL export (`export.*`,
  deferred part 5: `export.stlParameters`, `export.stlMesh` (a
  `MeshFrame` and the volume, the dialog's preview), `export.stl` (the
  file's bytes as a frame of one u8 array; the client writes them),
  `export.solutionPieces`; `shape.mesh` and `playback.mesh` take the
  `stl` style).
- `events.cpp`: `document.changed` after a request that changed the
  document (with the change's `notice` when it has one), and the poll of the background work: first `Document::poll()`
  takes in what has ended (a run's end, an analysis's results; the
  document keeps how each problem's last analysis ended); then per
  problem the solve's state as it is seen to change, progress at most
  four times a second (and when a run ends), `solve.solutionsAdded` when
  the problem's `solutionsVersion()` moved, `solve.error`, the analysis's
  progress and end; then the `document.changed` of what was taken in, so
  a run's last state and an analysis's end always come before it (web
  stage 4 found them the other way round when work ended between the
  telling and the taking in). A new document
  sends a last `solve.state idle` and `solution.analyseEnded` for the old
  one's watched problems after the reply (`State::replaceDocument` queues
  them).

**Rules of the engine:** requests run one at a time in order; a reply
comes before the events it caused, a frame before the reply that refers
to it (so a notification gets no frame); the last `solve.state` of a run
comes before the `document.changed` of its end; the revision clients see
never goes back (a new document counts on from the old one); nothing but
the main thread touches the document; handles hold copies and survive
edits, a new document drops them; a malformed or oversized line is
answered, never fatal (64 MiB, depth 64, gzip to 256 MiB); `core/` never
sees JSON. Tested in process (`tests/unit/engine/`; among the
`[parallel]` cases a solve polled while it runs, a stacking solve polled,
paused and carried on, a solve next to an analysis of another problem,
and the explorers, fit, paging and the table read while a solve runs
until a new document ends it) and as a subprocess (`tests/contract/`:
every reply and event checked against the schema, every error's errc one
the method declares, every method called with success at least once, the
scripted session of the brief, a Panex solve paused and carried on by
another engine from its saved search, Hanoi built through `stacking.*`;
run against the dev, ASan and TSan engines).

## cli (stage 8)
`shapelogic_cli` is the command line. Ported from `src/burrTxt.cpp` and
`src/burrTxt2.cpp`; checked by the harness (`just oracle` reads its
`--json`, `just oracle-text` compares its text with burrTxt's byte for
byte, `just oracle-files` solves with `--save`).
- `options.h`: `Options` and `parseArgs` (burrTxt's options and rules:
  combined short options, `-q` turning `-p` and `-s` off, `-o`/`-a` not
  combined, `--json` refusing `-a -x -o all -p -s`; burrTxt2's options as
  long ones that go with `--save`; the A/B switches), `usage(kind)`.
- `text_print.h`: the drawings of `src/lib/print.cpp` (a shape, the
  puzzle, an assembly, a take-apart). Below the first separation the
  original drew the wrong shapes; `bt` draws each piece as itself, and
  `--original-drawings` draws as burrTxt did (for the comparison).
- `dot_level.h`: the level text read as burrTxt `--json` read it, the best
  level of a solve.
- `main.cpp`: the modes. The solve (an `Assembler` per problem, each
  assembly taken apart in its callback by one `Disassembler` whose levels
  spread over a pool of their own, printing as asked; `--json` at the
  end), `-x` (the saved solutions taken apart again), `-a` (the comment,
  the saved solutions), `--save` (a `SolveSession` as burrTxt2 ran its
  solve thread: a paused solve carried on unless `--restart`, the
  solutions kept sorted by moves up to `--limit`, the puzzle written to the
  file given), `--progress` (a line a second on stderr from a ticker
  thread). Sliding puzzles solve as burrTxt solved them with its full
  search (the Sliding Puzzle Solver, the only one, `--solver sliding`;
  `--nested-slides`, `--partial-nested`, `--high-memory`;
  `-d` finds each start layout's slide path, with the original's notes
  after the summary; `-x` and `--save` too, through a `SolveSession`);
  the search's folder and disk budget come from
  `SHAPELOGIC_SLIDE_DIR` and `SHAPELOGIC_SLIDE_DISK_GB`, which the CLI reads
  (the core reads no environment). Stacking puzzles solve as burrTxt
  solved them (deferred part 3a: `--solver` stacking or panex,
  `--high-memory`, `--restart` throwing a saved search away,
  `--panex-dir` and `--panex-disk`, else `SHAPELOGIC_PANEX_DIR` and
  `SHAPELOGIC_PANEX_DISK_GB`, else the cache folder's `panex`; Ctrl-C stops
  a Panex search, which saves itself, and a second quits; the summary and
  search lines, `-p` the transfer plan, `-x`, `--save` through a session;
  the A/B switches `--no-tower-rule` and `--panex-log`).
  The core never prints: what the original's library said on stderr
  (the assembler chosen, what reduce removed) the CLI prints from what the
  core returns.

## python (deferred part 7, decision 0018)
`shapelogic`, the Python module: the original's `src/python` bindings
ported over the new core with its names, so its scripts run unchanged.
Built when `BT_PYTHON` is `ON`, or `AUTO` (the default) and CMake finds
Python's headers outside sanitizer and cross-architecture builds
(`cmake/Python.cmake`, which then makes the whole build position
independent); pybind11 3.0.0, the original's, fetched.
- `py_puzzle.h`, `py_shape.h`, `py_problem.h`: `PyPuzzle` (a
  `shared_ptr<Puzzle>`; load, save, the grid, shapes and problems, and
  part 6's imports, Puzzlecad export and grid conversion), `PyShape` and
  `PyProblem` (the puzzle and an index, read afresh at each call, so they
  see edits and raise `IndexError` once their shape or problem is gone).
  Cell accessors check the cell (the original's assertion became a
  `RuntimeError`, so that is what they raise).
- `solution_iterator.h`: `SolutionIterator`, the solve behind
  `Problem.solve`: a copy of the puzzle and a thread of its own. Brick
  puzzles run as the original's iterator ran them (the Classic
  assembler on every core, or `threads`; each assembly taken apart in the
  callback, its levels on a pool of their own); sliding and stacking
  puzzles through a `SolveSession` (the GUI's checks and set-up first),
  their solutions yielded when the run ends, a stacking one counted in
  transfers. Solutions pass through a queue of four; `next()` waits with
  the GIL let go (before taking the queue's lock), looks for signals
  every 50 ms (Ctrl-C stops the solve), and raises the solve's failure;
  `stop()` and the destructor stop the run through its `StopSource` and
  join the thread.
- `module.cpp`: the module's classes, names, docstrings and reprs as the
  original's; the new arguments are keywords.

**Rules of the module:** Python sees only copies (solutions, placements)
or the shared puzzle through an index; nothing the worker thread reads is
reachable from Python; the module reads no environment variable (a
search's folder is `work_dir`). Checked by the original's tests and new
ones (`python/tests/`, ctest `python`), by `oracle.py python` against the
original's module, and by hand under ThreadSanitizer and
AddressSanitizer (docs/handoff/deferred-7.md says how).

## web (front-end stages 1a to 8, decision 0013)
Electron + TypeScript in `web/`, built on Girish Sharma's mockup
(`docs/mockup/index.html`) in the stages of docs/FRONTEND-PLAN.md. Node 22;
the packages pinned in `web/package.json` (three.js 0.186.1, Playwright
1.64.0 among them; gltf-validator, Khronos's glTF validator, for the tests).
What exists after stage 8:
- `tools/gen-api.ts`: `src/engine/api.generated.ts` from `api/*.json`
  (every `$defs` entry a named type, `Methods` and `Events` by name,
  `METHOD_NAMES`, `EVENT_NAMES`, `ERROR_CODES`, `FRAME_PATHS`), committed;
  a test fails when it is stale.
- `src/engine/`, the client, platform-free but for `node-transport.ts`:
  `framing.ts` (`FrameParser`: stdout as lines and frames, in two states,
  frames copied once and handed out with the reply after them, protocol
  problems reported; `StderrRing`), `frames.ts` (`viewFrame`, typed-array
  views without copies; `attachFrame`; `meshArrays`, `poseArrays`),
  `client.ts` (`EngineClient`: `engine.hello` first, typed calls pipelined
  in order and bounded, replies matched in order, errors as `EngineError`,
  coalescing by key, typed events), `dispatch.ts` (`OrderedInbox`: a reply
  settles before the events behind it are dispatched; `makeDefer`, the
  macrotask it yields with on each platform), `client-types.ts`
  (`EngineApi`, `Reply<M>`), `errors.ts`, `transport.ts`, `bridge.ts` (the
  messages between host and renderer: calls and a status request one way,
  replies, events and statuses the other), `recorder.ts` (`RecordingTransport`),
  `node-transport.ts` (`NodeTransport`: the engine as a child process, its
  `--version`), `app-bridge.ts` (stage 6: what crosses between the main
  process and the renderer besides the engine: the menus' commands and
  state, the file dialogs, the stored state, the recent files, the rescue
  copy, the help links, the window's close, from stage 7 the exports'
  writes (`writeExport`) and the kinds of file (`FileKind`, `ExportKind`);
  `window.btApp`'s interface and the checks of what crosses).
- `src/main/`: `engine-host.ts` (`EngineHost`: one engine per window,
  restarted when it dies, its status told; calls fail while none is
  running; `stop()` ends an engine being started too), `engine-ipc.ts`
  (`serveEngine`: the host's side of the bridge; it answers the proxy's
  status request and posts a status after the errors of the calls it
  failed; with a log callback it tells how each call ended), `engine-path.ts`,
  `index.ts` (Electron's main: a window, its host, IPC; a main-frame
  navigation gets a new bridge server, so replies to the old page's calls
  are dropped, and any other navigation or new window is refused; the
  menus rebuilt from the focused window's state (a window destroyed
  meanwhile left alone: the e2e tests' code runs through the inspector and
  can come between two statements); the window's close held and asked of
  the renderer (`close`), which answers `closeWindow` after its question
  about unsaved changes, or the question asked natively when the renderer
  has not taken the close within 4 s; a quit held by a window's question
  goes on once it closes, a close of the window's own meanwhile does not
  give it up; the window's bounds kept;
  files the system opens with the app (macOS's `open-file`, a command
  line's `.xmpuzzle` or `.slsolve`); in development the renderer's
  warnings and errors are echoed to the terminal; `BT_USER_DATA` moves the
  app's data directory (the e2e tests give each launch its own); with
  `BT_CALL_LOG` set every call of the renderer is written to that file
  with its id and errc, which the e2e tests read; `BT_AUTOSAVE_SECONDS`
  shortens the Solver's autosave for them, passed in the page's address),
  `menu.ts` (stage 6: the menus' template from the menu state, Electron's
  types only: the original's macOS table (File with Import, Export and
  Close; Edit; View with the Color Theme, the display modes and render styles; Help; About
  and Settings in the application menu on macOS, in Help and Edit
  elsewhere), the not-yet-ported items greyed, the keys of the one table
  shown with `registerAccelerator: false`, so a press reaches the page and
  is handled once), `app-ipc.ts` (stage 6: `window.btApp`'s main side: the
  file dialogs (an extension added, a file that then exists asked about),
  the store, the recent files, the rescue copy, the help links by name,
  the menu state, the close; from stage 7 the exports' kinds (PNG, SVG,
  STL, glTF binary) and `bt:write-export`, which writes an export's bytes
  only to a path the save dialog gave the same window for that kind, once;
  every message checked, only the app's windows answered),
  `export-file.ts` (stage 7: `writeFileSafely`, an export written as the
  core writes a puzzle: beside the file and renamed over it, a symbolic
  link followed, a read-only file refused, the permissions kept, the
  temporary file made exclusively and never one already at its name (the
  stage 8 audit); `ExportGrants`, the paths each window may write,
  Electron-free),
  `state-store.ts` (`StateStore`: `state.json` in the app's
  data directory: the renderer's settings and layouts, the recent files,
  the window's bounds; read once, written a moment after a change to a
  temporary file renamed over it; what is not well formed read as absent),
  `rescue.ts` (`RescueStore`: `__rescue.xmpuzzle`, the document's bytes,
  and `__rescue.json`, its record with a hash of the bytes, written record
  first, each by rename; a copy whose record does not match is offered as
  a puzzle without a file).
  From stage 8, the security checklist: `security.ts` (Electron-free:
  the window's web preferences, sandboxed, context-isolated, no Node in
  the page, its workers or frames, no webview, no navigation by a drop;
  the page's own scheme (the stage 8 audit: `APP_SCHEME`,
  `bt-app://app/index.html`, `appUrl`; `appFile`, the file of the
  renderer's directory an app URL names, or null for any other scheme or
  host and for a path that leaves the directory; on a `file:` page the
  CSP's 'self' matched every `file:` URL and the request filter alone
  stood between markup in a puzzle's names and a script from disk, on its
  own scheme Chromium refuses every local file to the page itself);
  the environment's switches a run takes, `BT_USER_DATA` only when
  packaged, `BT_ENGINE`, `BT_CALL_LOG`, `BT_AUTOSAVE_SECONDS` and
  `ELECTRON_RENDERER_URL` in development too; `allowedRequest`, what the
  session lets through: the app's scheme naming a file inside the
  renderer's directory, `file:` URLs inside it (the scheme's handler reads
  them), `devtools:`, `data:`, `blob:`, the dev server's origin in
  development, nothing remote; no permission; `HELP_URLS`, the only
  addresses handed to the system's browser), which `index.ts` applies
  (the scheme registered as standard and secure before the app is ready
  and served by `protocol.handle` from the renderer's directory, a file
  outside it or missing a 404; the window's options,
  `webRequest.onBeforeRequest`, the permission handlers, no webview and no
  new window for any web contents); `path-grants.ts` (`PathGrants`: the
  files a window's engine may be given, which a call naming a path,
  `document.open`, `document.openBytes`, `document.save`,
  `solve.importPaused`, `solve.exportPaused`, must name: what the
  window's open and save dialogs gave for a puzzle or a paused solve, the
  rescue record's file, a file dropped on the window (the preload reports
  the path its `pathOf` read, which only a real dropped file has), a file
  the menus or the system opened with the app (every `openPath` the main
  process sends); `serveEngine`'s `admit` refuses any other as
  `invalid_argument` before the engine sees it). The project's version,
  CMakeLists.txt's, is put into the main bundle at build
  (`__BT_VERSION__`, electron.vite.config.ts): the version the app reports
  and the one `NodeTransport.start` requires of the engine.
  `engine-path.ts`: a packaged app runs `shapelogic_engine` from its resources
  (`process.resourcesPath`) and nothing else; development runs
  `BT_ENGINE`, else the repository's dev build, else its release build.
  `src/preload/index.ts` exposes `window.bt` and `window.btApp`; it keeps
  the commands that come before the page listens (a file given on the
  command line, sent when the page has loaded, while it still reads its
  stored settings) and hands them to the first handler a task later, in
  order; the renderer's menus hold a command that replaces the document
  until the first document is shown.
- Packaging (stage 8): `tools/dist.ts` (`just app-dist`) runs
  electron-builder over `out/` for the platform it runs on: the bundles
  and `package.json` only in `app.asar` (three.js is in the renderer's
  bundle; the app has no runtime package), `shapelogic_engine` from the release
  preset as an extra resource (on macOS the other architecture's from
  `release-x64` or `release-arm64`), checked first for its architecture
  (`binaryArch`, the Mach-O, ELF or PE header) and its `--version`; the
  version CMakeLists.txt's (`tools/project-version.ts`, given as
  `extraMetadata`; a unit test keeps `package.json`'s equal); the file
  types `.xmpuzzle` and `.slsolve`; Electron's fuses (no
  `ELECTRON_RUN_AS_NODE`, no `NODE_OPTIONS`, no `--inspect`, the app only
  from `app.asar`, checked against its hash, no extra privileges for
  `file:` pages: the page is on the app's scheme); macOS a .dmg and a .zip,
  signed ad hoc unless a certificate is named; Windows an NSIS installer;
  Linux an AppImage and a .deb; the icons the original's
  (`packaging/`). Signing and notarisation are documented
  (`web/README.md`, docs/handoff/web-8.md), not automated. On Windows the
  engine is linked statically (no MinGW DLL beside it), on Linux with
  libstdc++ and libgcc linked in (`engine/CMakeLists.txt`).
- `src/renderer/`, the app (sandboxed; it reaches nothing but `window.bt`):
  - `index.html`, `styles.css`: the mockup's body and stylesheet (the
    stylesheet in its compact form, left alone by Prettier; the Puzzle
    library's chip rules scoped to `.pz-lib`, since the stage's own
    collapsed class is `lc` too); the Entities, Puzzle and Solver cards
    are the mockup's, with the Solver's step-by-step explorer section and
    the explorer's drawer (`#sdrawer`) over the view; the mockup's menu
    placeholders are Electron's menus; `main.ts` (reads the stored
    settings and layouts, then starts the shell);
    `engine-proxy.ts` (`EngineProxy`, the client's interface over
    `window.bt`).
  - `state/store.ts`: `Store`, the mirror of the engine: its status (no
    call before `running`; everything loaded again when `restarts`
    grows), `engine.info`, the engine's settings, the `Snapshot` (reloaded
    on `document.changed`, coalesced by the key `document.get`, the newest
    load winning), the shape shown, `solving` (the frozen state), a
    `generation` that grows with every new document (`openBytes`,
    `newDocument`), so the old one's ids are not taken for the new one's,
    and `whenRevision(r)`, which a mutating call awaits before acting on
    what it made; the documents' replacements (`newDocument`, `openPath`,
    `openBytes`, `openRescue` with its file and unsaved changes,
    `importPaused`) and `save`, which reads the snapshot again and waits
    for the save's revision (a save sends no `document.changed`);
    `wantedSettings`, the engine settings each new engine is given (the
    stored undo depth and threads). `state/edits.ts`: `Edits`, shared by the workspaces: a
    mutating call awaited until the page shows its revision (`mutate`,
    with the workspace's own wait: the snapshot's for Entities, the
    problem's cards for Puzzle), `track`, `refuse` (a frozen part says
    "Pause the solve first" before any call), and the count of edits
    under way as `#stage[data-busy]`; `refuseList`: a change of the lists
    of shapes, colours and problems, and undo and redo, refused while
    anything solves. `state/stacking.ts` (deferred part 4, DOM-free):
    the disc list's bounds and message, a rod set's text and the fields
    its rules grey out, the rod slider's places (the pocket first) and
    captions, the validity bar's words, the paused export's size.
    `state/solves.ts`: `Solves`, the solve records,
    one per problem (several may run), DOM-free: the state as the events
    tell it, a run's phase (none, active, ending), its end at the
    `document.changed` after its last `solve.state` (a run started here
    and over between two polls too: `launch`; an abort's own change ends
    nothing, a paused solve aborted ends no run; a state told again is no
    run), `whenEnded`, the progress, the failure, the last
    `solutionsAdded`, an analysis, the solver a run used (`pausedBt2`);
    cleared when another document comes (its last idle and analyseEnded
    then meet no run, and whoever waits for a run's end, the autosave,
    is released).
  - `shell/`: the frame of the app, ported from the mockup's functions:
    `app.ts` (puts it together; the status line), `keys.ts` (**the one
    keyboard table**: the rows of webui-spec.md section 6 with their
    chords and actions; the help prints it), `keyboard.ts` (the keydown
    handler: an action from the table, a handler per action; keys go to
    text fields, a modal dialog keeps them; an action may have several
    handlers, one per workspace, tried in turn), `history.ts` (undo and
    redo, named by `edit.history`; refused while solving without asking
    the engine; a drag just released is waited for), `escape.ts` (Esc's step-back
    order), `layout.ts` (the rail, the cards' collapse and the 3D focus,
    the layout per workspace, kept across sessions: `layoutState`,
    `restoreLayout`), `settings.ts` and `prefs.ts` (the settings dialog;
    undo depth and threads go to `settings.set`, the rest are the
    client's; `validPrefs` reads stored ones back), `persist.ts` (stage 6:
    the settings and layouts stored whenever they change), `theme.ts` (theme, density,
    font size, the side cards' widths, the tab bar's place, tooltips), `status.ts` (the status bar, flashes, `reportError`:
    `invalid_state` as "pause first"), `popover.ts`, `confirm.ts`,
    `panel.ts` (a window of its own over the workspace, not modal: the
    status window's and the debug statistics'; Esc closes the newest),
    `topbar.ts` (the file's name, path and dirty mark, the window's
    title; `#stage[data-dirty]`, `[data-path]`), and from stage 6:
    `menus.ts` (`Menus`: the menu state sent when it changes (the
    document, dirty, path, Undo and Redo labelled from `edit.history`, the
    Solver's problem paused, solutions saved, the display mode and style,
    the 3D focus, the table's keys as accelerators); a command with a key
    of the table runs that key's handlers (`keyboard.ts`'s `runAction`),
    the others go to their handlers), `files.ts` (`Files`: New with the
    grid chosen in a dialog, Open, a path (recent, the system's, a drop;
    `.slsolve` imported), Save, Save As, the window's close, each asking
    about unsaved changes first (the original's `confirmDiscard`: Save,
    Don't Save, Cancel); while the engine is not running the close says
    the puzzle cannot be saved and keeps the rescue copy; another close
    while the close's save runs is not taken, so a save that never ends
    leaves the main process's native question (the window can always be
    closed); a save while a solve runs offered at a save point
    (`Autosave.saveAtSavePoint(path)`), an analysis told to finish first;
    the paused solver state exported and imported; a dropped file by its
    path, else by its bytes; a file opened or saved goes to Open Recent,
    a recent file that cannot be read leaves it), `rescue.ts` (`Rescue`:
    the rescue copy written by `document.saveBytes` 300 ms after a change,
    at most every 2 s, while the document is dirty and nothing solves,
    removed once it is clean; writes and removals in one queue; offered
    after an engine's restart, and at the start for a copy a session that
    died left (not on a page reload, whose document is newer); nothing
    written or removed while an offer waits; a copy whose offer was not
    answered (a newer offer after another death took the question's
    place, the window closed) stays, as does one a close could not save
    because the engine was not running: offered at the next start;
    `#stage[data-rescued]`), `imports.ts` (deferred part 6: `Imports`,
    File ▸ Import ▸ PuzzleSolver3D and Puzzlecad (`document.import` by
    the path the open dialog gave, the unsaved-changes question first,
    the puzzle shown by the file's name without its extension, the
    Entities workspace brought forward; the original's "Could not load
    puzzle, sorry!"), File ▸ Export ▸ Puzzlecad (the engine asked before
    the save dialog, so a refusal comes first in the original's words;
    the text written through `writeExport`) and Edit ▸ Convert Brick Grid
    Type to Other (`#convertGrid`: `engine.info`'s `convertsTo` as radio
    buttons, Abort and Continue; `document.convertGrid`)), `dialog.ts`
    (modal dialogs in the `.cfm` look: buttons, a default, Esc and a click
    beside cancel unless the question must be answered, Tab kept inside,
    the focus given back; `askChoice`), `help.ts` (the F1 dialog over the
    one table, searchable; About with the app's and the engine's
    versions; the Notes dialog, `document.setNotes`), `icons.ts`, `cursors.ts` (the tools'
    pictures and data-URL mouse cursors),
    `piece-colours.ts` (the original's `pieceColor` table and jitter,
    `ROD_CHIP`),
    `dom.ts` (with `setHtml`, which leaves a list's elements alone when a
    re-render would draw the same, so a double-click whose first click
    re-rendered still lands, and `markSelected`). `keys.ts` has the
    Puzzle rows' chords (+ and −, `piece.more` and `piece.fewer`; Del is
    the one action `delete`, which each workspace handles) and the
    Solver's (Space and Enter `activate`, ← → `move.back`/`move.forward`,
    with Shift `move.start`/`move.end`, End `end`); a key several
    workspaces use has one action named for its meaning (↑ ↓ `prev` and
    `next`, Home `home`, PgUp PgDn `page.up` and `page.down`), and a
    handler may go first (`onKey(…, first)`: the Solver's list before the
    view's Home). The confirmation gives the focus back. The status
    line is the workspace's: the shape's in Entities, the problem's fit
    (the original's line) in Puzzle, the solve's (the mockup's svStatus)
    in Solver.
  - `view3d/`: the 3D view. `view.ts` (`View3D`: the selected shape's mesh
    fetched, built and drawn; navigation; picking into the status bar;
    rendering only on change; its state as `data-*` attributes of the
    viewport), `renderer.ts` (`ViewRenderer`: three.js, the perspective and
    orthographic cameras framed as the mockup framed its picture, render
    on demand, `performance.measure` per render), `camera.ts` (one
    orientation quaternion; the mockup's Drag orbit, pan, smooth zoom,
    animated Home and Fit; the original's arc-ball), `viewcube.ts` (the
    mockup's canvas cube over that quaternion), `meshcache.ts` (meshes by
    shape, style and transformation with their revision, coalesced as
    `mesh:<shape>:<style>:<transformation>`, dropped by what
    `document.changed` names), `mesh.ts` (a `MeshFrame` through
    `meshArrays()` into a non-indexed `BufferGeometry` with the
    per-triangle flags, colour and cell per vertex; the wire-frame style's
    faces; the variable marks as the original's shrunk polygons),
    `material.ts` (the faces' shader: the original's colours and its
    fixed-function light; colour view, lighting, x-ray and the dimmed
    layers as uniforms), `scene.ts` (`ShapeScene`: one shape, its passes,
    marks, edge lines, overlays), `overlays.ts` (axes, bounds, the layer's
    slab), `picking.ts` (a raycast's triangle to its cell and side),
    `extent.ts` (a space's size in mesh units, the original's
    `calculateSize`), `toolbar.ts` (Orbit, Pan, Edit, Display, Focus; the
    Display menu; Minimal's hiding toolbar; the Edit button's tool),
    `svg.ts` (stage 7: the vector image: the flat meshes of what the view
    draws, asked again by each mesh's source (`MeshSource`, which
    `buildMesh` keeps in the geometry: a shape at a revision and
    transformation, or a playback's piece), placed as the view has them,
    the faces turned towards the eye in the colours the shader gives them
    (computed per triangle), the flat style's edge lines and the variable
    marks, painted in a BSP tree's order and written by three.js's
    SVGRenderer at the view's size over the view's background; a wire-frame
    piece as its lines; no overlays), `depth-order.ts` (`paintingOrder`:
    faces and lines back to front for an eye or a direction, faces split
    where a plane crosses them, a line after the face it lies on, as gl2ps
    sorted the original's vector export; DOM-free),
    `preview.ts` (the 3D edit's ghosts, drawn on top: boxes on the cube
    grid, a cell's or one side's triangles of the mesh on the others). The
    view takes an edit host (`View3D.edit`: the point under the mouse and a
    click that was no drag, with the modifiers), the editor's plane and
    layer (`setEditLayer`), a preview and a cursor; `#vp` says
    `data-revision` and `data-preview`. In the Puzzle workspace the view
    draws the pieces scene instead (`View3D.setPieces`, a puzzle host for
    the click, the tag and the empty message): `pieces.ts`
    (`PiecesScene`: one `shape.mesh` per shape, asked at the revision the
    shape last changed at, its geometry shared by every copy; the
    optional copies faded, their nearest surface only (a depth pass
    first); each item turned about its own centre by the camera's
    orientation while the layout stays put (`ViewRenderer.frameScene`:
    the mockup's scale, a far eye); the labels and selection boxes as
    HTML in `#pzScene`; a click picks the smallest box, never the
    result); `#vp` says `data-scene` (the items drawn: role, shape,
    copy, faded), `data-scene-meshes`, `data-scene-key`,
    `data-scene-style`. In the Solver workspace it draws a solution
    (`View3D.setSolution`, `poseSolution`, a solver host for the tag and
    the empty message): `solution.ts` (`SolutionScene`: each piece its own
    mesh and colour (the original's per copy), posed by a translation and
    a quaternion in mesh units centred on the result, framed on the
    result's extent; solid, wire frame or hidden; wire frames over the
    solids with x-ray; a piece taken away faded through its opacity);
    `#vp` says `data-solution`, `data-solution-pieces`, `data-poses`,
    `data-solution-view` (assembled or playback). The view keeps two
    layers of solution models: the player's, and an explorer's shown over
    it while it has one (`setSolution(model, layer)`, `poseSolution`;
    `#vp[data-solution-layer]`); `pieceOnScreen` (where a piece is on
    the screen and where a move by a vector would take it) and an
    Alt-drag on a piece that the Solver host takes (`movesPieces`,
    `pieceDragged`: the movement explorer's matching move) instead of
    orbiting; `SolutionScene.pick` (the piece a ray meets, not the result
    an explorer draws around them). `extent.ts` has `cellScale` too (grid
    coordinates to mesh units, the original's recalcSpaceCoordinates) and
    `placedAt` (a piece's mesh at its cell minus its turned hotspot,
    scaled, centred on the result: the assembled view, the explorers'
    placements and positions), checked against the engine's poses by an
    engine test.
  - `entities/`: the Entities workspace (stage 2), from the mockup's
    `renderShapes`, `renderInsp`, `renderTransform`, `renderTools`,
    `renderGrid`, `spaceGridSvg`, `renderLayers`, `renderPlane`,
    `renderColors`, `renderSelbar`, `applyTool`, `applyCell`, `hover2d`,
    `shapeAction` and their handlers. `workspace.ts` (`EntitiesWorkspace`:
    puts it together; the workspace's keys; the frozen state while
    `Snapshot.solving`, the cards disabled and every edit refused with
    "Pause the solve first" before any call; `mutate`, a call awaited to its
    revision; `data-busy`, the edits under way), `shapes-list.ts` (new,
    duplicate, delete but never the last, move by Alt+arrows, the menu or
    a drag, rename in place, weight, the shape's own colour, the collapsed
    card's chips), `inspector.ts` (Transform, Fit & scale, Repair &
    surface: the buttons of `shape.taskSet`, placed per grid as the mockup
    draws them, the rest as plain buttons of their group; `shape.task`, the
    counts and a blocked nudge as flash messages; "Apply to all shapes"),
    `editor-card.ts` (the tool strip, mirror and span, the planes and the
    box tool, the layer strip with its marks, the selection bar, the size
    steppers in `sizeUnit` blocks rounded up as the original's, the
    palette), `grid2d.ts` (`Grid2D`: one SVG renderer for every grid over
    a layer drawing; the cursor outlined by its edges towards cells not
    under it; a drag collects every cell the pointer passes over, the right
    button erases; the box selection), `layer-geometry.ts` (`grid.geometry`
    by grid, size and layer; the cube's XZ and YZ planes as the client's
    squares), `shape-cells.ts` (`ShapeCells`: the shape's cells, the XY
    layer by `shape.get {layer}` and the whole shape, reloaded once the
    snapshot after a change still has the shape; coalesced),
    `stroke.ts` (`Stroke`: a drag as one `edit.begin` … `edit.end`, one
    `shape.setCells` of a frame's cells at the engine at a time, the group
    closed whatever failed inside it), `edit3d.ts` (`Edit3D`: 3D editing,
    the cell across a face by `tools.ts`'s table on cubes and
    `grid.neighbour` elsewhere, the preview with mirror and span),
    `tools.ts` (which tool a press uses; the cube's faces), `editor-state.ts`
    (`EditorState`; `planeMap`; `expand`, mirror and span's cells as the
    core makes them, for the previews), `glyphs.ts` (the mockup's `G`),
    `status-window.ts` (`StatusWindow`, stage 5: the original's status
    window over `shape.table`: a row per shape with its cells, the earlier
    shapes it is identical to, connectivity, holes, notch and mill on
    cubes, symmetry, its status line as the tooltip; read again when a
    change names the shapes; Refresh, Remove selected (one undo step,
    refused while solving, never every shape) and the four selections).
  - `puzzle/`: the Puzzle workspace (stage 3), from the mockup's
    `renderPuzzle`, `renderPzSel`, `renderPzGroups`, `renderPzRules`,
    `pzAdd`, `pzBump`, `pzBulkRange`, `pzLibChip`, `pzUsedTile`,
    `pzDragStart`/`pzDragEnd`, `pzMenu`, `pzStats`, `renderPuzzleScene`,
    `puzLayout`, `puzPick`. `workspace.ts` (`PuzzleWorkspace`: the
    problem shown read after every change by `problem.get`,
    `problem.fit` and once `problem.kind`, coalesced, the newest load
    winning, `#leftP[data-problem]` and `[data-revision]` saying what is
    shown; every action a call awaited until the cards show its
    revision; the frozen state: the lists while anything solves
    (`Snapshot.solving`), a problem while it is solved or analysed
    (`ProblemSummary.solve`, `analysing`: `#stage.pzfrozen`), refused
    before any call; the keys + − Del; the status line
    `FitStats.statusLine`; the view's tag; the scene's model),
    `problems.ts` (`ProblemsList`: new, duplicate, delete but never the
    last, rename in place, reorder by drag), `pieces-card.ts`
    (`PiecesCard`: the result and Change…, the parts as chips or a
    table, "All pieces", drag and drop in, out and between tiles (a move
    as one undo step: `movePart` in an edit group), the More menu and
    the bulk range dialog, the right-click menus, the fit summary from
    `FitStats` (`#pzSummary[data-fit]`)), `piece-card.ts` (`PieceCard`: the selected piece's
    copies or range with the switch, its group and the copies in it, the
    groups and their members, the colour rules over the constraint
    colours with the presets), `counts.ts` (a part's count as the chips
    write it, `nextCount`: the count a field asks for, never a minimum
    above the maximum; the groups read off the parts; `solveActive`),
    `layout.ts` (`puzLayout`, the scene's items and positions, and their
    labels).
  - `solver/`: the Solver workspace (stage 4), from the mockup's
    `renderSolver`, `renderSolverDrawer`, `renderSvSel`,
    `renderSvSortAct`, `renderSolverPlayer`, `svSelect`, `svSeek`,
    `svTogglePlay`, `svPlayLoop`, `svManage`, `svDaCell`, `renderPcs`,
    `renderSolverScene`, `svKey` and the Solver keydown handlers.
    `workspace.ts` (`SolverWorkspace`: the problem shown read after every
    change (`problem.get`, `problem.kind` once), `#leftS[data-problem]`,
    `[data-revision]`, `[data-state]`, `[data-phase]`, `[data-end]`; its
    solve the record's; the options; Solve, Prepare and Continue (the
    record told of the run at the reply, the selection dropped first),
    Pause, Abort (asked; only while a run is under way or paused in
    memory); the selection, shown only while the Solver is shown and only
    from rows of the list's newest version, not chosen automatically
    while a run changes the list; remove (`solution.delete`, each asks;
    undoable), delete disassemblies, analyse (`solution.analyse`, its
    targets read first, so the ones it took that did not come apart are
    marked "Cannot be disassembled": the client remembers, the engine
    keeps no mark); the keys; the status line and the tag), `run-card.ts`
    (the left card: the problems list with each problem's solve, what to
    check, the search options, the run's buttons, progress and counters,
    a failure),
    `solutions-card.ts` (the right card: the list, virtual, its rows paged
    as it scrolls, sorted by a header's click (again the other way, a
    third time the saved order), or the slider; the player's controls; an
    analysis's progress and Stop; the Manage menu with its preview; the
    piece chips), `player.ts` (`Player`: a solution shown assembled at
    once (`solution.get`, `shape.mesh {transformation}` at the cell minus
    the hotspot), its playback opened once the choice rests and only if
    its row is still current (`playback.open`, `playback.mesh` per piece
    and style, the poses in chunks; a removal or an abort holds an open
    not yet asked for), play, pause, the moves, the jumps, the seek bar,
    the speed;
    `#splayer[data-ready]`, `[data-t]`, `[data-steps]`, `[data-playing]`),
    `poses.ts` (`PoseTrack`: the samples of `playback.poses` in chunks of
    32 steps, 8 a step, interpolated: translations straight, rotations by
    slerp; a piece taken away fades over its last step with the setting;
    DOM-free), `pager.ts` (`SolutionPager`: `solution.page` in pages of
    100 by `solutionsVersion`: a page of a newer version makes the others
    old, read again; `solutionsAdded` at the version it has is ignored;
    DOM-free), `settings.ts` (the options over the stored `SolverSettings`:
    the original's rules, the two spellings, `SolveOptions`, the limits
    not stored), `autosave.ts` (`Autosave`: every 20 minutes while a solve
    with the setting runs: `solve.savePoint` on every run under way, their
    ends waited for by revision, `document.save`, `solve.continue`, none
    of it once another document replaced the one it paused; a puzzle
    without a file is not saved, said once; `saveAtSavePoint(path)` is
    also File ▸ Save's path while a solve runs, the save the store's),
    `to-shapes.ts` and `to-shapes-form.ts` (stage 6: Edit ▸ Convert
    Assemblies to Pieces, the original's dialog over `solution.toShapes`:
    source, destination, range, filters with their defaults (millable and
    notchable on bricks only), the shapes' size; one undo step, refused
    while solving), `stats-panel.ts`
    (`StatsPanel`, stage 5: the debug statistics of the problem shown,
    `solve.status`'s `stats` read again with each event of its solve,
    held once a run stopped as the original's `freezeLive` (the session's
    wall time counts on from the run's start), until a run is under way
    again or Refresh).
  - `export/`: File ▸ Export (stage 7). `exports.ts` (`Exports`: Image,
    the view at its size or twice it as a PNG (`ViewRenderer.capture`:
    a render at that pixel ratio read back in the same task); Vector Image
    (`View3D.vectorImage`, svg.ts); STL (deferred part 5,
    `stl-dialogs.ts`: File ▸ Export ▸ STL, the grid's parameters from
    `export.stlParameters` with the original's labels, formats and
    tooltips, Binary STL, the shape, a preview of `export.stlMesh` in a
    small three.js view of its own (`stl-preview.ts`: the arc-ball, the
    wheel, Normal and Insides) with its volume or the engine's refusal,
    Export STL… keeping the dialog open; Solutions ▸ Export to STL, the
    pieces of `export.solutionPieces` into a folder the main process's
    folder dialog gave, written by `writeFolderExport`, with the
    original's overwrite question and closing line; `stl-form.ts` the
    fields and words, DOM-free); Solution Animation of the solution the Solver
    shows with its moves stored (the original's dialog: seconds per move,
    cell size, reassemble, bevelled edges; a playback of its own, closed
    after); each asks the file of the main process's dialog and has it
    written by `writeExport`; `#stage[data-exported]` counts what was
    written), `glb.ts` (`animationGlb`: gltfexport.cpp's scene, a root
    node "Puzzle" scaled to metres and a node per piece with its
    playback.mesh (indexed, flat shaded, no inside faces) in its solution
    colour, linear, metallic 0, roughness 0.6, both sides; the channels of
    playback.track thinned to the keys their interpolation does not
    reproduce (`keptKeys`), translation always, rotation when it turns,
    scale 0 or 1 as STEP when it hides; written by three.js's
    GLTFExporter; DOM-free), `stl.ts` (`stlBytes`: a shape's flat mesh,
    each triangle in the order its normal gives, scaled by the unit,
    binary or text through three.js's STLExporter; DOM-free), `form.ts`
    (the dialogs' numbers).
  - `explore/`: the step-by-step explorers (stage 5), from the mockup's
    `renderSolverDrawer`, `#sdrawer`, `svPrepare`, `svStep` and the
    Advanced section's buttons, the contents from the original's
    placement and movement browsers. `explorer.ts` (`Explorer`: the
    drawer with Placements (`explore.openAssembly`,
    `explore.placements`: each piece's placements counted, the one chosen
    drawn in the result's wire frame; ↑ ↓ ← → Home End), Step (an
    assembler of its own stepped 1, 10, 100, 1000 steps; the pieces
    placed now drawn; `finished`; Reset) and Movements
    (`explore.openMovements` on the selected solution: the tree,
    Analyse, Add move with the grid's directions, a matching move by an
    Alt-drag of a piece in the view (the drag compared with each
    direction's `vectors` step as it looks on the screen), Step back,
    Prune tree; the position chosen drawn with the solution's
    orientations); the Advanced buttons (Placements and Step need a
    prepared or paused solve, Movements a solution with its moves); the
    handles closed with the drawer, a change of problem or another
    document (whose handles the engine dropped); the pieces drawn by
    `placedAt` in the view's explorer layer; `#sdrawer[data-mode]`,
    `[data-handle]`, `[data-piece]`, `[data-placement]`, `[data-node]`,
    `[data-steps]`, `[data-finished]`, `[data-placed]`), `tree.ts`
    (`MovementTree`, the tree as the engine's replies grow it and Prune
    Tree as the engine prunes; `dragDirection`, the direction an Alt-drag
    asks for; DOM-free), `debug-stats.ts` (the debug panel's report: the
    original's `formatReport`, idle rows and analysis notes; DOM-free).
  - Sliding puzzles (deferred part 2, docs/webui-spec.md section 8, the
    original GUI's sliding branches): `entities/tray.ts` (`TrayState`:
    `sliding.tray` of the board shown, read again at each revision; the
    marks in the pieces' colours, the copies placed (`copiesOf`), the
    outline's colours of `drawStampPreview`, the validity bar's words;
    DOM-free); a board drawn in the Shapes workspace with the ordinary
    tools (floor, corridor, wall) on its dark tiles, no cell colours, no
    Paint tool or palette and the XZ/YZ planes off on sliding puzzles;
    `puzzle/slide-card.ts` (`SlideCard`: the Puzzle workspace's **Start
    and goal** card in the selected piece's place, `#stage.sld`: the
    board's Start and Goal grids, two `Grid2D`s in stamping mode
    (`GridInput.tray`, the grid's stamping host calls), the problem's
    pieces with the copies placed of each, a hint, the layers; the piece
    selected in "In this puzzle" outlined under the mouse from
    `sliding.preview`, a click `sliding.place`, a right click
    `sliding.clearAt`, a refusal's reason in the status bar);
    the shapes list's "(START/GOAL n)" rows with their black chips and
    the header's + New Piece and + New Board (`#btnNewBoard`,
    `sliding.addStartGoal` of the selected shape's size), a tray deleted
    with its problem by the list's Delete, its colour greyed;
    the validity bar under the Puzzle view (`#slideValid`);
    `view3d/trays.ts` (`TrayView`: the START and GOAL trays and the
    copies standing on them as the original's `showSingleShape`, through
    a `SolutionScene` in a group of the view's root, in the Puzzle view in
    the pieces scene's place, the frame widened to both, the START and
    GOAL titles); `view3d/walls.ts` (`wallOutline`, `wallSegments`: the
    original's `buildSlidingWalls`, drawn by `SolutionScene.setWalls`
    around the player's result, `SolutionModel.result`); the colours of
    a tray (`piece-colours.ts`: `TRAY_LIST`, `TRAY_VIEW`, `TRAY_EDIT`,
    `chipColour`, `viewColour`, `isTray`); the Puzzle's sliding rules (no
    New, Copy or Delete problem, no Change…, no groups or colour rules, a
    `Part.required` piece kept, with no fewer copies than `Part.placed`); the Solver's sliding options
    (`settings.ts`: `nestedSlides`, `partialNested`, `highMemory`,
    `DEFAULT_SLIDING` with Find Solutions checked, the one solver shown
    and not stored,
    `normalize`'s sliding rules, `SolveOptions.sliding`; `shortCount`,
    `slidingDetails` for the Details line), Solve greyed while the
    tray is invalid (`SolverWorkspace.slideError`), the solver's note in
    a message box when a run is through (`tellNote`, also after an
    autosave's continue), Movements and Analyse not offered on a grid
    without a take-apart; the notices (`Store.onNotice`:
    `document.changed.notice` and an opened file's `Snapshot.notice`) in
    message boxes (`dialog.ts`'s `tell`, one at a time); File ▸ New's
    grids in the original's order (`shell/files-order.ts`).
  - Stacking puzzles (deferred part 4, docs/webui-spec.md section 9, the
    original GUI's stacking branches): the shapes list's rows are the disc
    list on a stacking puzzle (`shapes-list.ts`: "size" with its field, +
    and −, `stacking.setDiscSize`, the original's refusal of a size out of
    1 to 100; no weight); `entities/rods-card.ts` (`RodsCard`, the Rods
    panel in the voxel editor's card: the rod sets with New, Delete, Copy,
    Label in a dialog and the arrows, the selected set's rules as
    `stacking.setRules` takes them, greyed as `syncPanexFields` greyed
    them, frozen while anything solves); the inspector and the voxel
    editor hidden (`#stage.stk`); the view shows the disc chosen or the
    rod set chosen alone (`stacking.rodSetLayout`); `puzzle/stacks-card.ts`
    (`StacksCard`: the board in the result's place, "Result: R1 - name" or
    "No Start/Goal Rods", the rod sets and Set Start/Goal Rods; in the
    selected piece's card Start and Goal, the rod slider taken on release,
    Stack Order top first with ↑ ↓ +1 −1 All +1 Clear All as the
    `stacking.*` commands, refusals' reasons in the status bar, the
    validity bar), the Puzzle workspace reading `stacking.board` for the
    stacking edited (shown only once read for it: `boardGoal`) instead of
    the fit, the library's + putting a disc on the rod, + − Del on the
    stack; `view3d/board.ts` (`boardBoxes`: the original's pegs, base and
    Panex Vs as lit boxes, `boardCentre` the engine's stackingCentre,
    `discAt` a disc by its centre, `boardExtent`; `BoardScene`, `BoardView`
    with the discs' meshes), drawn by `View3D.setBoard` in the Entities
    and Puzzle workspaces (the Puzzle's pieces scene not used there) and
    around the player's discs (`SolutionModel.board`); `ViewRenderer.tilt`,
    the original's `tiltPlates` (-90° about X before the camera's turn) on
    every stacking view; the Solver's stacking options (`settings.ts`'s
    `Family`, `DEFAULT_STACKING` with Find Solutions, `normalize`'s
    stacking rules, `stackingSolverOf` the board's one solver,
    `SolveOptions.stacking`, `stackingDetails`), no Prepare, Solve and
    Continue greyed without a board or with invalid stacks, Solve's
    question about a saved search (Continue, Start Over), Continue and
    Abort for a saved search (`solve.status.savedSearch`, read with the
    problem), the note in a message box at a run's end; the player's
    assembled view placed by the discs' centres around the board's middle
    (`Player.frame`); no 20-minute save point on a stacking puzzle and a
    save while its search runs pausing it (`autosave.ts`); the paused
    export asking about levels beyond 1 GB (`files.ts`). The main process
    gives each engine the app's saved-search folder before the page may
    call it (`EngineHostOptions.setup`: `panex` in the app's data
    directory) and refuses a page's `settings.set` naming a folder
    (`path-grants.ts`'s `settingsRefusal`).
- `fake-engine/`: `FakeEngine` replays a recorded session as a transport;
  `recordings/` holds the sessions of `tools/sessions.ts` (recorded with
  `npm run record`) and what the client saw in them; `solver-end` and
  `solver` drive the solve records and the pager over the real engine's
  output (the event-order rules of decision 0012).

**Rules of the front end** (AGENTS.md): the engine is the truth; every
mutating call waits for its revision; the renderer never sees Node and
never parses frames by hand; `src/engine/` stays platform-free (a
TypeScript project without Node's types and ESLint check it); keys come
from the one table; the client computes only what the engine cannot be
asked for in time (the cube planes' squares, the cube's faces, the
preview's mirror and span cells: each checked against the engine by a
test). Tested by Vitest: `tests/unit/` (the parser at every
chunk size over recordings of the real engine, the client against a
scripted engine, the inbox and its deferral, the fake engine, the host,
the bridge; `unit/renderer/`: the keyboard table against webui-spec.md
section 6, the piece colours against the original's, the mesh
translation, the camera, the store and the mesh cache, a stroke's
protocol, the planes, the quick tools, mirror and span; the Puzzle's
counts, groups, layout and labels; the Solver's options and rules, the
poses' interpolation and fade, the pager, the solve records; the stacking
options, Details lines, board boxes and helpers (`stacking.test.ts`); the movement
tree, the Alt-drag's direction, the debug report; and the
replays of the recorded Solver sessions: a run's end at its revision,
one between two polls (the active states taken out), an abort's two
changes, a paused abort's one, a new document's last events,
`solutionsAdded` at the version paged ignored) and
`tests/engine/` (the real engine: every fixture opened and drawn in every
style, stage 7's scripted session, pause, save point, continue and abort,
the event order of decision 0012, a killed engine restarted; the
preview's mirror and span cells on every grid, the cube's faces against
`grid.neighbour`, a stroke as one step with an event per batch; every
count the Puzzle's fields ask for taken by the engine, the groups as the
UI reads them, every fixture's problems with a fit and a scene; the assembled view's
placement (cellScale, gridExtent, the hotspot) against the player's
first pose on every fixture of the five grids, the pager in every sort
against one `solution.page`, the chunked poses against the engine's
samples, a stacking solution's discs placed by the boards and the player
as the engine's first and last poses; the explorers' placements in the solutions' frame on the five
grids, the stepped assembler's in the placements' frame, the movement
tree against `explore.node`); by
Playwright's Electron driver (`tests/e2e/`, the built app with the
release engine: the IPC path, a killed engine, a reload with calls in
flight, every fixture shape in every style, reference screenshots of each
grid, picking on every fixture, the shell's keys, the settings, the
view's speed; the editor on each of the five grids, the shapes list, the
frozen state, every shape of Bermuda and Prisgon rebuilt through the
editor; the problems of CubeInCage and DemoPieceGenerator rebuilt
through the Puzzle's actions, the chips, the table, the fit and the
scene checked against the engine after each, a click and an orbit in
the scene, the problems list, the colour rules, the frozen problem, the
stacking board and stacks in the result's place; the Solver: Bermuda solved with take-apart
with `bt --json`'s counts, played, stepped, jumped, the poses at every
whole step the engine's, the list sorted every way and paged, its keys,
every Remove variant with its confirmation, Analyse with the assemblies
that do not come apart marked and a stop that keeps what was done,
pause, continue, a save at a save point, abort, Prepare then Continue,
an error run with its failure, two problems at once, a sliding puzzle solved with `bt
--json`'s counts and played along its routes, a stacking one solved as bt; the
explorers on Bermuda: every piece's placements counted and drawn where
the engine says, the assembler stepped to its complete set as the
engine's own, the movement tree's root, Analyse, Add move, an Alt-drag's
matching move and Prune tree equal to an explorer of the test's own, the
handles closed with the drawer; the debug panel's numbers
`solve.status`'s; the status window's rows `shape.table`'s on every
fixture, a selection removed as one undo step; the call log checked for
refusals; stage 6: File ▸ New on every grid, every fixture opened by
File ▸ Open with no `document.changed` and saved as a copy whose bytes are
the engine's own save, an engine killed after edits and the rescue copy
reopened with them (its file, its unsaved changes), a copy left by a
session that died offered at the start, the window's close asking (Cancel
keeps it, Save saves, Don't Save closes), the settings and layouts across
a restart, a save while solving at a save point, the paused solver state
exported and imported, the notes, Convert Assemblies to Pieces, About,
the help links, the greyed items, a dropped file without a path; the
audit's: the rescue copy kept until its offer is answered (another
death, the window closed, at the next start), a close while the engine
cannot run, a quit and a close of the window's own, a close whose save
does not end (asked natively), a window destroyed while the menus are
built; and one
test over the keyboard table (`keymap.e2e.ts`): every row with keys
pressed and checked, every row of mouse actions named with the test that
drives it). Unit tests of stage 6: the store, the rescue copy's record,
the menus' template (platforms, greyed items, keys shown and not
registered, every command one the renderer takes), the bridge's checks,
the stored settings read back, the convert dialog's fields, the table's
accelerators. Stage 7: an export's file (written whole, through a link,
a read-only file refused and left, the permissions kept, a failed write
leaving the old file), the grants (only a path the window's dialog gave,
once, with its kind's extension), the menus' export items, the
keyframes a channel keeps, the painting order (behind first, a face
split, a line after its face, the eye turned round); the engine tests
(`tests/engine/exports.test.ts`): the .glb of every fixture solution
with its moves stored passes Khronos's validator and, read back by a
reader of the tests' own, puts each piece where the player's poses do
at sampled times of the holds, the moves (between keyframes too) and the
moves backwards, its meshes the playback's; the engine's STL of a
one-cell cube a closed surface facing out, every fixture shape's STL
(but the four slow fixtures') loaded by three.js's STL loader with its
triangles and size, each a closed, consistently oriented surface; e2e
(`exports.e2e.ts`): Image at
1x and 2x the view's picture (against a screenshot of the view) and a
reference image, Vector Image in the flat style drawn by the page like
the view and a reference image, STL (deferred part 5) with the
original's labels and values, the preview's volume, a refusal in the
original's words, the file equal to the original's byte for byte, binary
and text, a read-only file refused and left, a path no dialog gave
refused, Solution Animation greyed without a solution and then valid
with the playback's pieces, Solutions ▸ Export to STL of CubeInCage's
first solution equal to the original's files and names, its overwrite
question, names and folders no dialog gave refused; `view.e2e.ts` has a
reference screenshot of the STL-like style for each grid. Stage 8: the security checklist
(`tests/unit/security.test.ts`: the window's options and that nothing in
`src/` turns them off, the switches a packaged app ignores, the requests
the session lets through, no permission, the CSP as written and as built,
nothing remote named by the renderer, the two bridges, the help links as
the only external addresses, the path-taking methods of the schema all
checked, the grants per window, a refused call never sent; the IPC
handlers with Electron stood in for: only the app's windows answered,
only what a dialog, the rescue record or a drop gave granted, an
export's path not granted to the engine and written once, the help by
name only), the preload (`tests/unit/preload.test.ts`: the first commands
kept for the first handler, a drop's path reported), the packaging
(`tests/unit/dist.test.ts`: one version in CMakeLists.txt, package.json,
the release engine and the main bundle; the configuration; the
architecture from a header; where the app finds its engine); e2e: a path
the window was not handed refused, a file given on the command line
opened at the start; the stage 8 audit's `security.e2e.ts`: a puzzle
file whose names hold markup (an element, a quote that would close an
attribute, a style sheet; the notes a closing `</textarea>`) shown as
text in every card, menu, dialog and explorer, nothing injected; and the
smoke test of the packaged app (`tests/e2e/packaged.smoke.ts`, `just
app-smoke`, over Chromium's DevTools protocol: the fuses, the archive, the
engine's version, the development switches and Node's overrides ignored,
the page on the app's scheme, sandboxed, with its CSP, a script and an
image from `file:` refused by Chromium itself, no file outside the
renderer read and nothing remote, a path no dialog gave refused, a
fixture from the command line solved with bt's counts). Deferred part 2:
`tests/unit/renderer/sliding.test.ts` (the sliding options over the
stored settings, their rules and `SolveOptions.sliding`, the Details
line's counts and saved counts, the Activity line's solver, the tray's marks and stamps, the outline's colours, the
validity's words, New's order, the walls' outline and segments) and
`tests/e2e/sliding.e2e.ts` (the 81-move puzzle built from File ▸ New:
the start/goal shape, the pieces drawn and duplicated, every start and
the goal stamped, a refusal with the engine's reason, `sliding.tray` and
the problem equal to the fixture's; solved by the Sliding Puzzle Solver,
the only one, with `bt --json`'s counts and moves, the problem then the
fixture's; played with the walls drawn, every whole step's poses the
engine's; the nested fixture with and without nested slides and a
partially nested pocket as `bt`; the notices; reference screenshots of
the board drawn, its Start and goal, the two trays and the walls; copies
of a piece placed one by one, a third refused, one taken off, solved). Deferred
part 4: `tests/e2e/stacking.e2e.ts` (the Tower of Hanoi and a Panex swap
of three-disc towers built from File ▸ New through the disc list, the
Rods card and the Stacks card, saved byte for byte as the fixtures;
solved by the Stacking and the Panex Solver as `bt --json`, played in
transfers on the engine's poses with the board; a seven-disc Panex search
paused, the app restarted on the same data, Solve's question answered
Continue and the search carried on to its 2,189 transfers; the rod sets'
commands, the rules' dependencies, frozen while solving, Abort of a
saved search; reference screenshots of the board, the rod set and the
player).

## Testing against the original

- `tests/oracle/` runs the original programs (decision 0007): the summary
  comparison of `bt` with `burrTxt` (`just oracle`; `--fields`, `--threads`,
  `--solver crowell` against the original's own baselines for those),
  the text comparison (`just oracle-text`: `bt` and burrTxt run the same
  ways on every corpus puzzle, stdout, stderr and the exit code compared
  byte for byte against the original's recorded runs),
  the solved-file comparison (`compare-files`: `bt --save` solves as
  burrTxt2 does, both programs' files are read with the new core by
  `bt_solutions` and compared solution by solution: assembly, separation
  tree, level text, move counts), the
  saved-position check (`just oracle-resume`: assembler 0 and 1 searches
  paused by one program continue in the other, single-thread pauses save the
  same text), the
  file round trip
  (`just oracle-roundtrip`: every corpus file, the sliding/stacking/rotation
  fixtures and the original's solved files are loaded and saved by both
  programs; the saves must be the same XML), and recorders that compile a
  dump tool against the original's library (`oracle.py grid-dump`,
  `model-dump`, `app-dump`, `record-saves`, `record-positions`,
  `record-analysed`, `record-slide-routes`, writing into `tests/data/`).
  The imports (deferred part 6): `oracle.py imports` runs the original's
  loaders, Puzzlecad export and converter (`imports_old.cpp`) and the new
  document's (`bt_imports`) on the committed inputs of
  `tests/data/imports/`, the original's Puzzlecad files, and every corpus
  puzzle (converted, exported for each problem, the exports imported
  again, its shapes as a PuzzleSolver3D file), comparing the saves byte
  for byte; `record-imports` keeps the original's results for the unit
  tests in `tests/data/imports_by_original/`.
  The `sliding` tier (deferred part 1) has the sliding fixtures and
  puzzles built by the original's code (`oracle.py sliding-puzzles`, from
  its `test_sliding.cpp`), compared with the full search, bt's one
  sliding solver, in the summary, the solved files (the original's solve
  thread through `slide_solve_old`) and the text; puzzles whose count
  depends on the order of their starts are compared on one thread (flag
  `O`: the original is not deterministic there).
  The `stacking` tier (deferred part 3a) has the stacking fixtures and
  puzzles built by the original's code (`oracle.py stacking-puzzles`, from
  its `test_stacking.cpp`), compared for both solvers (the default
  Stacking Solver, `--solver panex`) in the summary, the solved files (the
  original's solve thread through `stack_solve_old`) and the text; each run
  gets a Panex folder of its own (never the user's cache).
  The STL meshes (deferred part 5) are compared through recordings:
  `oracle.py stl-dump` (the original's STL files of every fixture shape
  with the default parameters, read back into their vertices and
  triangles, its view meshes, one text file) and `oracle.py stl-solution`
  (its solution export of CubeInCage, the dialog's naming copied from
  GUI code); see tests/oracle/README.md.
  `oracle.py panex-resume` stops Panex searches in one program at a sweep
  of depths (`panex_resume_old` and `bt_panex_resume`, `stopAtDepth`) and
  carries them on in both (the saved folders byte for byte the same, the
  solving time aside); `oracle.py blockpack-check` packs with one
  program's blockpack and unpacks with the other's.
- `oracle.py python` (deferred part 7): the Python module's transcript
  (`python_transcript.py`: error cases, puzzles built through the API,
  and each corpus puzzle read and solved, on one thread in order and on
  all threads as sorted lists) against the original's module's, line for
  line.
- `tests/bench/bench.py` times `bt` against the original's `burrTxt`, back
  to back (decision 0007's speed rule; stage 8 is the gate).
- The unit tests compare with those recordings (`test_grid_dump.cpp`,
  `test_model_dump.cpp`, `test_app_dump.cpp` (meshes, the status table,
  the fit line, playback tracks), `test_analysis_files.cpp` (the
  re-analysis, byte for byte), `test_layer_geometry.cpp` (the grid
  editors' corner formulas, copied into the test), `test_slide_routes.cpp`
  (each sliding move's route and the player's positions at quarter
  steps), `test_stl_dump.cpp` (the STL files and view meshes of every
  fixture shape, positions within 1e-5), the byte-for-byte save check in
  `test_puzzle_file.cpp`, the paused positions in `test_positions.cpp`);
  they never read the original repo.

## Rules that hold everywhere

See AGENTS.md for the full list. The load-bearing ones:
1. Cancellation only through `StopToken`; nothing resets a stop request.
2. Only a run's worker writes its `SolveState`.
3. Recoverable errors are `Result<T>`; broken invariants are `BT_ASSERT`.
4. No owning raw pointers; value types and `unique_ptr`.
5. No global state or environment switches in `core/`.
6. Ported code matches the original on the harness before it is reshaped.

## Build and test

`just build` / `just test` / `just test-all` / `just test-asan` /
`just test-tsan` / `just oracle` / `just oracle-resume` /
`just oracle-roundtrip` / `just oracle-files` / `just oracle-text` /
`just oracle-imports` / `just oracle-python` / `just check`; the Python
module's `just python-test` / `just python-wheel`; the front end's `just app-install` / `just app-gen` /
`just app-check` / `just app-test` / `just app-dev` / `just app-e2e` /
`just app-dist` / `just app-smoke`. ctest also runs `api-schema` (the schema checks itself),
`api-docs` (`docs/API.md` is current), `contract` (the engine as a
subprocess), `cli` (`bt` as a subprocess) and, when the module is built,
`python` (its tests).
Presets in `CMakePresets.json`; strict warnings are errors in `dev` and
`release` (and `release-x64`, `release-arm64`: macOS's release for one
architecture, the packaged app's engine for the other kind of Mac). zlib comes from the system, or is built from a pinned release
(`cmake/Dependencies.cmake`); nlohmann/json 3.12.0 (pinned, the engine only),
pybind11 3.0.0 (pinned, the Python module only), Manifold 3.5.2 (pinned, the core's STL meshes; built optimised and without
its cross sections or parallel backend, as the original builds it; left
uninstrumented under ASan, decision 0016) and Catch2 are fetched.

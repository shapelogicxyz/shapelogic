# Architecture

BurrTools Alpha is the core of the BurrTools puzzle designer and solver,
rebuilt so a web front end can drive it. This document describes **what
exists now** and the rules that hold everywhere. Each stage updates it.

*Current state: stage 8, the end of the plan (the assembler run layer
and all three assemblers; the take-apart with rotation moves, Classic and
Crowell; the solve session; the drawing meshes of every grid; the app
layer: the document with its undo history and commands, the queries, the
layer drawing of the grid editors, the analysis of saved assemblies, the
paused solve files, the playback sampler and the explorers; `bt-engine`,
the JSON-RPC engine, with the schema of its API in `api/` and contract
tests; `bt`, the original's burrTxt and burrTxt2 for brick puzzles; every
benchmark as fast as the original or faster). The web front end
(docs/FRONTEND-PLAN.md) has its stages 1a, 1b and 2: the toolchain in
`web/`, the TypeScript types generated from `api/`, the engine client with
its host, bridge and proxy, the app's shell from the mockup, the 3D view in
three.js, and the Entities workspace (the shapes list, the voxel editor on
every grid, the tool tabs' tasks, 3D editing, undo and redo). Next: its
stage 3, the Puzzle workspace.*

## Big picture

```
 web front end (web/: Electron + TypeScript + three.js; the client, the shell, the 3D view)
        │  JSON-RPC 2.0 over stdio, binary frames for meshes   (decisions 0003, 0012)
        ▼
 bt-engine  (engine/)        bt  (cli/, batch solving, burrTxt-compatible --json)
        │                         │
        └──────────┬──────────────┘
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
| `core/bt/<module>/` | the library, headers and sources together; include as `"bt/<module>/x.h"` |
| `engine/` | `bt-engine`: JSON-RPC 2.0 over stdio around one `Document` (decision 0012); `btengine`, the same as a library for the unit tests |
| `cli/` | `bt`: the original's burrTxt for brick puzzles (the same options and text, `--json`), and burrTxt2's solve-and-save mode (`--save`) |
| `api/` | the JSON Schema of the engine's methods and events, its validator (`apischema.py`) and the generator of `docs/API.md`; see `api/README.md` |
| `web/` | the web front end (Electron + TypeScript + three.js, decision 0013): the engine client, the host and bridge, the generated types, the app (shell, 3D view), its tests (unit, engine, e2e); see `web/README.md` |
| `tests/unit/<module>/` | Catch2 tests, one binary `bt_tests` (`app`, `base`, `engine`, `grid`, `io`, `mesh`, `model`, `solve`) |
| `tests/contract/` | the engine's contract tests: `bt-engine` driven as a subprocess, every message checked against `api/` |
| `tests/cli/` | `test_cli.py` (ctest `cli`): `bt`'s options, refusals and modes on the fixtures |
| `tests/oracle/` | the comparison harness against the original BurrTools |
| `tests/bench/` | `bench.py`: solve speed against the original, back to back |
| `tests/data/` | test fixtures: `puzzles/` (copied from the original), the original's recordings (`grid_dump.txt`, `model_dump.txt`, `app_dump.txt`, `saved_by_original/`, `positions_by_original.txt`, `analysed_by_original/`) |
| `tests/tools/` | small programs the harness drives (`bt_roundtrip`, `bt_resume`, `bt_solutions`) |
| `docs/` | this file, PLAN.md, FRONTEND-PLAN.md, API.md (generated), decisions/, handoff/, webui-spec.md, mockup/ (Girish Sharma's mockup, vendored), core-api.md (the draft API.md replaced) |
| `cmake/` | warnings, sanitizers, dependencies |

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
  of a budget). No process-wide limit, no `BURRTOOLS_THREADS` (decision 0009).
- `stop.h` also makes a source from a token (`StopSource(const StopToken&)`):
  each search run stops itself through one without a flag of its own.

### grid (exists)
Ported from the original `voxel*.cpp`, `symmetries_*.cpp`, `tabs_*`,
`gridtype.cpp`, `voxeltable.cpp`; checked value for value against the
original (`tests/data/grid_dump.txt`, see "Testing against the original").
- `grid_type.h`: `GridKind` (the numbers are the file format's) and
  `GridType`, a small value type: capabilities, `getSymmetries()`, and
  `makeVoxel()`, the factory for voxel spaces. Sliding and stacking grids
  exist as kinds (cube spaces) so their files load; their solvers are later.
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
  `symmetries_impl.h` is the one table-driven implementation; `symmetries_0/1/2.cpp`
  instantiate it over the generated tables in `tabs_0/1/2/` (copied as they
  are; see `core/bt/grid/TABLES.md`). The instances are immutable,
  constant-initialized statics (`constinit`: no guard on use, which the
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
  colours, problems, comment. Copying copies everything. Removing or
  swapping shapes goes through the puzzle, which updates every problem.
  `replaceShape` puts another voxel space in a shape's place (undo).
- `problem.h`: `Problem`: parts (`Part`: shape, min, max, groups), result
  and goal shape, colour rules, max holes, `ProblemState`, counts and time,
  the saved search position (`getAssemblerState()`/`Version()`,
  `setAssemblerState()`: the text an assembler wrote, see `solve`), pending
  assemblies, saved solutions with their sort orders, solver options. A problem holds no reference to its puzzle: what needs
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
- `paused_solve.h`: the paused solve file (`.btsolve`, the original's
  BTSOLVE2: the gzip-compressed puzzle and the paused problem's number in
  little-endian sections; other sections skipped), both ways.

Unknown attributes and elements of `<puzzle>`, `<problem>` and `<solution>`,
and unknown `<voxel>` attributes, are kept and written back in place;
stacking rod sets and maps are kept this way until stacking is ported.
Saves are byte-identical to the original's.

### solve (exists: stages 3 to 5)
The assemblers, on one run layer (decision 0009), and the take-apart. Ported from
`assembler.cpp`, `assembler_resume.h`, `assembler_0.cpp`, `assembler_1.cpp`,
`assembler_bt2.cpp`, `bt2_dancingcells.cpp`, `bt2_assemble.cpp`,
`simd_exact_cover.cpp`, `simd_huang_cover.cpp`, `solvertype.h` and the
assembler part of `gridtype.cpp`; checked against the original by the
harness (assembly counts on every tier, 1 and all threads, Classic and
BurrTools 2; iteration counts of the deterministic searches equal; saved
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
  callback: `deliver`, `deliverOnce` with signatures, `deliverFound` for
  searches that never find an assembly twice), `TaskProgress` (weighted
  task units, lock-free), `runTasks()` (unfinished tasks over the pool's
  threads; completed only when searched to the end without a stop; the
  first exception aborts the rest and comes back), `effectiveThreads()`.
- `work_stealing.h`: the second driver, `runWorkStealing()` (BurrTools 2's):
  a search split into branches searched a slice at a time, busy threads
  splitting off work for waiting ones, unfinished branches parked across a
  stop. The search provides the `WorkStealingSearch` concept.
- `resume_state.h`: `SplitSearchState<Task>` (tasks, completion, reported
  signatures, the vector search's skip count) and the saved-position tail
  (`K` / `T C S`), byte-compatible with the original.
- `rotation_filter.h`: `RotationFilter`, the check every assembler makes on
  the assemblies it finds (rotated or mirrored copies dropped), with the
  settings of a solve.
- `dlx_matrix.h`: `DlxMatrix` (the dancing links matrix of assembler 0 and
  BurrTools 2, built node for node as the original: `build`, `reduce`,
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
- `dancing_cells.h`: `DancingCells`, Knuth's dancing cells (exact cover with
  secondary items over sparse sets), searched in slices and split.
- `assembler_bt2.h`: `AssemblerBt2` (BurrTools 2: `DlxMatrix` searched by
  `DancingCells`; one thread searches the root in slices, more split it
  through `runWorkStealing`). `bt2Threads()`, its thread count. Its saved
  position is the original's start position; a started search is refused
  on restore (decision 0009).
- `placement_finder.h`: `PlacementFinder`, the placements of a rotated piece
  in the result, in the original's order.
- `simd_bitset.h`, `simd_exact_cover.h`, `simd_huang_cover.h`: the vector
  covers (assembler 0's exact cover, assembler 1's cover with counts and
  ranges, `makeSimdHuangCover`, `simdHuangMemoryEstimate`), 256 to 32768
  columns, AVX2/AVX-512/NEON kernels behind runtime checks with a scalar
  fallback, chosen through `SimdKernels`. Read only during a search; threads
  search subtrees of one solver.
- `solver_type.h`: `SolverType` (Classic, Crowell, BT2) and its names,
  `chooseAssembler()` / `makeAssembler()` (findAssembler: assembler 0, or
  BurrTools 2's with BT2, when every part is one piece, else assembler 1).

**Rules of a run** (decision 0009): the search polls only its run's token;
a callback answering false pauses the run, an exception aborts it; nothing
is reset, the next `assemble()` carries on; `RunEnd::Finished` only from the
search itself; a stopped split or vector search continues the way it ran.
A search may hand over assemblies it reaches after the pause was asked for
(assembler 1's plain search until it stands where it can be saved, BT2's
other threads): those are not found again. The assembler never writes into
the `Problem` (results go to the callback; the CLI or the session owns
the problem).

**The take-apart** (stage 4). Ported from `disassembler*.{h,cpp}`,
`disassemblernode`, `disassemblerhashes`, `movementcache*`,
`movementanalysator`, `grouping`, `rotationrules`, `rotationmoves_*` and
`disasmtomoves`; checked against the original by the harness (every field of
the summary on all 180 corpus puzzles, Classic, Crowell and BurrTools 2, 1
and all threads; the solved files solution by solution, `compare-files`).
- `disassembler.h`: `Disassembler`, the take-apart of one problem's
  assemblies: `disassemble(assembly, stop) -> TakeApartResult` (the
  `Separation`, or none, and whether it was stopped), `progress()`
  (`DisassemblyProgress`, from any thread), the search times.
  `DisassemblerOptions`: rotations, the solver (Crowell takes at most
  `kCrowellMaxSuccessors` new positions from each position and has its own
  turn generator; Classic and BurrTools 2 are complete), the `ThreadPool`
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
  `PiecePositions`; stage 6 builds the playback sampler from it.

**Rules of a take-apart:** one `StopToken` per take-apart (polled by the
search, the analysators and the turn generators); a stopped take-apart
returns no separation and says so (`stopped`), which is not "cannot come
apart"; levels spread over the pool given, nested inside the assembler's
callback without deadlock; the shapes' caches are warmed when the
disassembler is made. Nothing is printed and nothing read from the
environment (the original's switches are options).

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
the same assembler; a session on a reloaded file restores the position
(BurrTools 2 cannot, except for a search that is through: Error, then a
fresh start). A run that ends with counted assemblies still waiting (it
did not take apart) ends Paused, not Finished.

**Lock order** (outer before inner; nothing takes a session lock under an
assembler's or disassembler's, so there is no inversion): the owner's
control lock around the results, run and failure locks; the results lock
around `beginSolving` (an assembler 1 replay included, which holds up
`readProblem` and `progress()` for that time) and `storePosition`
(BurrTools 2's peer lock); the run lock around the published assembler's
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
  style. `shapeDrawMesh`.
The STL meshes (`cubemesh.cpp`, `minkmesh.cpp` with manifold, the view's
"STL style") belong to the exports, later.

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
  `importPaused`, a new untitled document); playback and explorers. The
  rules for edits while solving are decision 0011; they hold while an
  analysis runs too (`solving()` covers both).
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
  `shapeStatusLine`, `isNotchable`, `isMillable`, `problemKind`.
- `playback.h`: `PlaybackSampler` (from a solution: the pieces with their
  meshes in the assembly orientation, `sample(step)` giving each piece's
  translation, quaternion and visibility, turns tumbling about their
  pivots; `track()` the keyframes with holds and reassembly), over
  `DisasmToMoves`; a solution without its disassembly is the assembled
  pose alone (`stepCount()` 0), so every solution plays through one path.
  `orientedMesh(shape, transformation, style)`: a shape's mesh turned by
  a transformation of its grid (the piece meshes, and
  `Document::shapeMesh` with a transformation: how a placement is drawn).
- `explorers.h`: `AssemblyExplorer` (the placements of each piece, the
  assembly search one step at a time) and `MovementExplorer` (the tree of
  positions of a take-apart: analyse, add a move, find a matching one,
  prune); both work on a copy of the puzzle.

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
`bt-engine` speaks JSON-RPC 2.0 over stdio: one request per line in, one
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
  the ids (`s7`, `c2`, `p1`, `sol42`, handles `pb1`, `ax1`, `mx1`),
  `BTE_TRY` / `BTE_CHECK` (a core `Result` error becomes the reply).
- `output.h`: `Output` (lines and frames), `FrameBuilder` (typed arrays
  at multiples of four, described for the reply), base64.
- `convert.h`: the names of the schema's enums and the JSON of the items
  (`snapshot`, shape, colour and problem summaries, the status row, the
  fit, meshes as frames, the stored solver settings, solve options over
  the settings, progress, statistics, failures, solution rows,
  placements).
- `methods_<area>.cpp`: one per schema file. `engine.hello` (the
  protocol a client speaks), solving (`solve.*` on the document's solve
  calls; a pause, a save point and an abort only for a solve under way),
  solutions (`solution.page` sorts the saved list's indices under
  `readProblem` with `solutionSortOrder`, a view, with the problem's
  `solutionsVersion`; the analysis, with its failure), shapes
  (`shape.mesh` in any transformation of the grid; `grid.geometry`, the
  editor's drawing of a layer; `grid.neighbour`, the cell across a side,
  the sides numbered as a mesh's `voxelFace`), playback (samplers
  behind `pb` handles: poses and keyframe tracks as frames of typed
  arrays; a solution without a disassembly as a single pose; piece meshes
  in any style), explorers (`ax` and `mx` handles).
- `events.cpp`: `document.changed` after a request that changed the
  document, and the poll of the background work: per problem, the solve's
  state as it is seen to change, progress at most four times a second
  (and when a run ends), `solve.solutionsAdded` when the problem's
  `solutionsVersion()` moved, `solve.error`, the analysis's progress and
  end; then `Document::poll()` and its `document.changed`. A new document
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
`[parallel]` cases a solve polled while it runs, a solve next to an
analysis of another problem, and the explorers, fit, paging and the
table read while a solve runs until a new document ends it) and as a
subprocess (`tests/contract/`: every reply and event checked against the
schema, every error's errc one the method declares, every method called
with success at least once, the scripted session of the brief; run
against the dev, ASan and TSan engines).

## cli (stage 8)
`bt` is the command line. Ported from `src/burrTxt.cpp` and
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
  thread). Sliding and stacking puzzles are refused with burrTxt's message
  for a puzzle it cannot handle (their files load, and `-a` reads them).
  The core never prints: what the original's library said on stderr
  (the assembler chosen, what reduce removed) the CLI prints from what the
  core returns.

## web (front-end stages 1a, 1b, 2 and 3, decision 0013)
Electron + TypeScript in `web/`, built on Girish Sharma's mockup
(`docs/mockup/index.html`) in the stages of docs/FRONTEND-PLAN.md. Node 22;
the packages pinned in `web/package.json` (three.js 0.186.1, Playwright
1.64.0 among them). What exists after stage 3:
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
  `--version`).
- `src/main/`: `engine-host.ts` (`EngineHost`: one engine per window,
  restarted when it dies, its status told; calls fail while none is
  running; `stop()` ends an engine being started too), `engine-ipc.ts`
  (`serveEngine`: the host's side of the bridge; it answers the proxy's
  status request and posts a status after the errors of the calls it
  failed; with a log callback it tells how each call ended), `engine-path.ts`,
  `index.ts` (Electron's main: a window, its host, IPC; a main-frame
  navigation gets a new bridge server, so replies to the old page's calls
  are dropped; in development the renderer's warnings and errors are
  echoed to the terminal; with `BT_CALL_LOG` set every call of the
  renderer is written to that file with its id and errc, which the e2e
  tests read).
  `src/preload/index.ts` exposes `window.bt`.
- `src/renderer/`, the app (sandboxed; it reaches nothing but `window.bt`):
  - `index.html`, `styles.css`: the mockup's body and stylesheet (the
    stylesheet in its compact form, left alone by Prettier; the Puzzle
    library's chip rules scoped to `.pz-lib`, since the stage's own
    collapsed class is `lc` too); the Entities and Puzzle cards are the
    mockup's, the Solver cards still placeholders; `main.ts`;
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
    what it made. `state/edits.ts`: `Edits`, shared by the workspaces: a
    mutating call awaited until the page shows its revision (`mutate`,
    with the workspace's own wait: the snapshot's for Entities, the
    problem's cards for Puzzle), `track`, `refuse` (a frozen part says
    "Pause the solve first" before any call), and the count of edits
    under way as `#stage[data-busy]`.
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
    the layout per workspace), `settings.ts` and `prefs.ts` (the settings
    dialog; undo depth and threads go to `settings.set`, the rest are the
    client's, in memory until stage 6), `theme.ts` (theme, density,
    tooltips), `status.ts` (the status bar, flashes, `reportError`:
    `invalid_state` as "pause first"), `popover.ts`, `confirm.ts`,
    `topbar.ts` (the file's name, path and dirty mark; the temporary New…
    with the grid chosen, by `document.new`, and the temporary Open and
    drop, by `document.openBytes`), `icons.ts`, `cursors.ts` (the tools'
    pictures and data-URL mouse cursors),
    `piece-colours.ts` (the original's `pieceColor` table and jitter),
    `dom.ts` (with `setHtml`, which leaves a list's elements alone when a
    re-render would draw the same, so a double-click whose first click
    re-rendered still lands, and `markSelected`). `keys.ts` has the
    Puzzle rows' chords (+ and −, `piece.more` and `piece.fewer`; Del is
    the one action `delete`, which each workspace handles). The status
    line is the workspace's: the shape's in Entities, the problem's fit
    (the original's line) in Puzzle.
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
    `data-scene-style`.
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
    core makes them, for the previews), `glyphs.ts` (the mockup's `G`).
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
    `FitStats` (`#pzSummary[data-fit]`), the notice for the sliding and
    stacking puzzles), `piece-card.ts` (`PieceCard`: the selected piece's
    copies or range with the switch, its group and the copies in it, the
    groups and their members, the colour rules over the constraint
    colours with the presets), `counts.ts` (a part's count as the chips
    write it, `nextCount`: the count a field asks for, never a minimum
    above the maximum; the groups read off the parts; `solveActive`),
    `layout.ts` (`puzLayout`, the scene's items and positions, and their
    labels).
- `fake-engine/`: `FakeEngine` replays a recorded session as a transport;
  `recordings/` holds the sessions of `tools/sessions.ts` (recorded with
  `npm run record`) and what the client saw in them.

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
counts, groups, layout and labels) and
`tests/engine/` (the real engine: every fixture opened and drawn in every
style, stage 7's scripted session, pause, save point, continue and abort,
the event order of decision 0012, a killed engine restarted; the
preview's mirror and span cells on every grid, the cube's faces against
`grid.neighbour`, a stroke as one step with an event per batch; every
count the Puzzle's fields ask for taken by the engine, the groups as the
UI reads them, every fixture's problems with a fit and a scene); by
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
sliding and stacking notice; the call log checked for refusals).

## Testing against the original

- `tests/oracle/` runs the original programs (decision 0007): the summary
  comparison of `bt` with `burrTxt` (`just oracle`; `--fields`, `--threads`,
  `--solver crowell|bt2` against the original's own baselines for those),
  the text comparison (`just oracle-text`: `bt` and burrTxt run the same
  ways on every corpus puzzle, stdout, stderr and the exit code compared
  byte for byte against the original's recorded runs),
  the solved-file comparison (`compare-files`: `bt --save` solves as
  burrTxt2 does, both programs' files are read with the new core by
  `bt_solutions` and compared solution by solution: assembly, separation
  tree, level text, move counts), the
  saved-position check (`just oracle-resume`: assembler 0 and 1 searches
  paused by one program continue in the other, single-thread pauses save the
  same text; BurrTools 2 pauses save the same text and are refused), the
  file round trip
  (`just oracle-roundtrip`: every corpus file, the sliding/stacking/rotation
  fixtures and the original's solved files are loaded and saved by both
  programs; the saves must be the same XML), and recorders that compile a
  dump tool against the original's library (`oracle.py grid-dump`,
  `model-dump`, `app-dump`, `record-saves`, `record-positions`,
  `record-analysed`, writing into `tests/data/`).
- `tests/bench/bench.py` times `bt` against the original's `burrTxt`, back
  to back (decision 0007's speed rule; stage 8 is the gate).
- The unit tests compare with those recordings (`test_grid_dump.cpp`,
  `test_model_dump.cpp`, `test_app_dump.cpp` (meshes, the status table,
  the fit line, playback tracks), `test_analysis_files.cpp` (the
  re-analysis, byte for byte), `test_layer_geometry.cpp` (the grid
  editors' corner formulas, copied into the test), the byte-for-byte save check in
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
`just check`; the front end's `just web-install` / `just web-gen` /
`just web-check` / `just web-test` / `just web-dev` / `just web-e2e`. ctest also runs `api-schema` (the schema checks itself),
`api-docs` (`docs/API.md` is current), `contract` (the engine as a
subprocess) and `cli` (`bt` as a subprocess).
Presets in `CMakePresets.json`; strict warnings are errors in `dev` and
`release`. zlib comes from the system, or is built from a pinned release
(`cmake/Dependencies.cmake`); nlohmann/json 3.12.0 (pinned, the engine only) and
Catch2 are fetched.

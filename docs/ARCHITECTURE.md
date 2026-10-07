# Architecture

BurrTools Alpha is the core of the BurrTools puzzle designer and solver,
rebuilt so a web front end can drive it. This document describes **what
exists now** and the rules that hold everywhere. Each stage updates it.

*Current state: stage 5 (the assembler run layer and all three
assemblers; the take-apart with rotation moves, Classic and Crowell; the
solve session; `bt` solving and taking apart). `core/bt/base`, `grid`,
`model`, `io` and `solve` have code; the app layer follows in stage 6.*

## Big picture

```
 web front end (later: Electron + TypeScript + three.js)
        │  JSON-RPC 2.0 over stdio, binary frames for meshes   (decision 0003)
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
| `engine/` | `bt-engine` (stage 7) |
| `cli/` | `bt`: solves and takes apart with burrTxt-compatible `--json` (full CLI in stage 8) |
| `api/` | JSON Schema of engine methods and events (stage 7); see `api/README.md` |
| `tests/unit/<module>/` | Catch2 tests, one binary `bt_tests` |
| `tests/oracle/` | the comparison harness against the original BurrTools |
| `tests/bench/` | `bench.py`: solve speed against the original, back to back |
| `tests/data/` | test fixtures: `puzzles/` (copied from the original), the original's recordings (`grid_dump.txt`, `model_dump.txt`, `saved_by_original/`, `positions_by_original.txt`) |
| `tests/tools/` | small programs the harness drives (`bt_roundtrip`, `bt_resume`, `bt_solve_file`, `bt_solutions`) |
| `docs/` | this file, PLAN.md, decisions/, handoff/, webui-spec.md, core-api.md |
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
  assemblers' rotation checks call in their inner loops).
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
- `solution.h`: `Solution`: an assembly with an optional `Separation` or
  `SeparationInfo`, and its numbers.
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
- `gzip_file.h`: whole-file reading and writing through zlib (level 9).

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
fresh start). Lock order: no lock is held while another is taken, except
the owner's control lock around the results and run locks.

### mesh and app (stage 6)
Drawing meshes for every grid; `Document` with transactional undo/redo;
editing commands; fit statistics; the playback sampler (meshes plus piece
poses at any step); explorer data.

## Testing against the original

- `tests/oracle/` runs the original programs (decision 0007): the summary
  comparison of `bt` with `burrTxt` (`just oracle`; `--fields`, `--threads`,
  `--solver crowell|bt2` against the original's own baselines for those),
  the solved-file comparison (`compare-files`: `bt_solve_file` solves as
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
  `model-dump`, `record-saves`, `record-positions`, writing into
  `tests/data/`).
- `tests/bench/bench.py` times `bt` against the original's `burrTxt`, back
  to back (decision 0007's speed rule; stage 8 is the gate).
- The unit tests compare with those recordings (`test_grid_dump.cpp`,
  `test_model_dump.cpp`, the byte-for-byte save check in
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
`just oracle-roundtrip` / `just check`.
Presets in `CMakePresets.json`; strict warnings are errors in `dev` and
`release`. zlib comes from the system, or is built from a pinned release
(`cmake/Dependencies.cmake`).

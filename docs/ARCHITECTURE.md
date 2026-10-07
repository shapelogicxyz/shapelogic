# Architecture

BurrTools Next is the core of the BurrTools puzzle designer and solver,
rebuilt so a web front end can drive it. This document describes **what
exists now** and the rules that hold everywhere. Each stage updates it.

*Current state: stage 3a (the assembler run layer, assembler 0, the vector
covers, `bt` solving). `core/bt/base`, `grid`, `model`, `io` and `solve`
have code; assembler 1 and BT2 follow in stage 3b.*

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
| `cli/` | `bt`: solves with burrTxt-compatible `--json` (assemblies only until stage 4; full CLI in stage 8) |
| `api/` | JSON Schema of engine methods and events (stage 7); see `api/README.md` |
| `tests/unit/<module>/` | Catch2 tests, one binary `bt_tests` |
| `tests/oracle/` | the comparison harness against the original BurrTools |
| `tests/bench/` | `bench.py`: solve speed against the original, back to back |
| `tests/data/` | test fixtures: `puzzles/` (copied from the original), the original's recordings (`grid_dump.txt`, `model_dump.txt`, `saved_by_original/`, `positions_by_original.txt`) |
| `tests/tools/` | small programs the harness drives (`bt_roundtrip`, `bt_resume`) |
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
  `helperPool_c`): made by the owner of the solve with its thread budget,
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
its threads start); editing a shape invalidates the caches.

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
its solver thread); how a running solve hands results over is stage 5's
design.

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

### solve (stage 3a exists; 3b, 4 and 5 add to it)
The assemblers, on one run layer (decision 0009). Ported from
`assembler.cpp`, `assembler_resume.h`, `assembler_0.cpp`,
`simd_exact_cover.cpp`, `simd_huang_cover.cpp`, `solvertype.h` and the
assembler part of `gridtype.cpp`; checked against the original by the
harness (assembly counts on every tier, 1 and all threads; iteration counts
of the deterministic searches equal; saved positions continue in both
programs).
- `assembler.h`: `Assembler`, the interface: `createMatrix(MatrixOptions)`,
  `reduce(stop)`, `assemble(callback, SearchOptions, stop) -> RunEnd`,
  `finished()` / `iterations()` (any thread), `savePosition()` /
  `restorePosition()` (the `<assembler>` text and version), the placement
  browser and the debugger (`debugStep`, `currentAssembly`).
  `AssemblerError` with the original's messages. The problem hooks:
  `beginSolving(problem, assembler)` starts or restores a solve with the
  original's failure rule (a position that cannot be restored drops all
  solutions and the position), `storePosition()` writes a paused one back,
  `canStartSolving()`. `warmShapeCaches()` before threads.
- `search_run.h`: the run layer. `SearchRun` (one `assemble()` call: its own
  stop source under the caller's token, the serialised hand-over to the
  callback, `deliverOnce` with signatures), `TaskProgress` (weighted task
  units, lock-free), `runTasks()` (unfinished tasks over the pool's
  threads; completed only when searched to the end without a stop; the
  first exception aborts the rest and comes back), `effectiveThreads()`.
- `resume_state.h`: `SplitSearchState<Task>` (tasks, completion, reported
  signatures, the vector search's skip count) and the saved-position tail
  (`K` / `T C S`), byte-compatible with the original.
- `assembler_0.h`: `Assembler0` (dancing links; every part exactly one
  piece). Three searches: plain on the caller's thread (flag 0 positions),
  split into subtree tasks over threads (flag 2), and the vector search
  alone or inside each task (flag 3). The matrix (`prepare`, `reduce`,
  `clumpify`) is the original's node for node.
- `placement_finder.h`: `PlacementFinder`, the placements of a rotated piece
  in the result, in the original's order.
- `simd_bitset.h`, `simd_exact_cover.h`, `simd_huang_cover.h`: the vector
  covers (assembler 0's exact cover, assembler 1's cover with counts and
  ranges), 256 to 32768 columns, AVX2/AVX-512/NEON kernels behind runtime
  checks with a scalar fallback, chosen through `SimdKernels`. Read only
  during a search; threads search subtrees of one solver.
- `solver_type.h`: `SolverType` (Classic, Crowell, BT2) and its names,
  `chooseAssembler()` / `makeAssembler()` (null for assembler 1 and BT2
  until stage 3b).

**Rules of a run** (decision 0009): the search polls only its run's token;
a callback answering false pauses the run, an exception aborts it; nothing
is reset, the next `assemble()` carries on; `RunEnd::Finished` only from the
search itself; a stopped split or vector search continues the way it ran.
The assembler never writes into the `Problem` (results go to the callback;
the CLI, later the session, owns the problem).

Planned: assembler 1 and BT2 with its work-stealing driver (3b); the
disassemblers, movement caches and rotation rules (4); `SolveSession`
replacing `solveThread_c` (5).

### mesh and app (stage 6)
Drawing meshes for every grid; `Document` with transactional undo/redo;
editing commands; fit statistics; the playback sampler (meshes plus piece
poses at any step); explorer data.

## Testing against the original

- `tests/oracle/` runs the original programs (decision 0007): the summary
  comparison of `bt` with `burrTxt` (`just oracle`; `--fields`, `--threads`,
  `--allow-unported` for what is not ported yet), the saved-position check
  (`just oracle-resume`: searches paused by one program continue in the
  other; single-thread pauses save the same text), the file round trip
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

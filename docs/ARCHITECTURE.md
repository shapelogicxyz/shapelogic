# Architecture

What exists, and the rules that keep it right. Each module has a map (one
line per file: what it is for) and its rules (invariants, threads, file
formats, what must stay identical with the original), with the decisions
that govern it. Read the sections of the modules a piece of work changes
(AGENTS.md); the history is in docs/PLAN.md, docs/FRONTEND-PLAN.md,
docs/DEFERRED-PLAN.md and docs/handoff/.

A map is a list under a line "Files in `<dir>/`:", one item per file or
group of files (`x.{h,cpp}`), each one line. `tests/docs/test_architecture_map.py`
(ctest `architecture-map`, and the web unit test `architecture-map.test.ts`)
fails when a source file (`.h`, `.cpp`, `.ts`, `.html`, `.css` under
`core/`, `engine/`, `cli/`, `python/src/`, `web/src/`) has no map line, when
a map line names a path that does not exist, and when the lines besides the
map pass 500. A new file costs a map line; a new invariant goes into its
module's rules, shortly. No prose sections, no stage labels.

## Big picture

```
 web front end (web/: Electron + TypeScript + three.js; the client, the shell, the 3D view)
        │  JSON-RPC 2.0 over stdio, binary frames for meshes   (decisions 0003, 0012)
        ▼
 shapelogic_engine  (engine/)        shapelogic_cli  (cli/, burrTxt-compatible)    shapelogic  (python/, the Python module)
        │                         │                                                       │
        └──────────┬──────────────┴───────────────────────────────────────────────────────┘
                   ▼
            libbtcore (core/)
   app     Document, undo/redo, editing commands, queries, playback, explorers
   solve   assemblers, take-apart, rotation rules, slide and stacking searches, SolveSession
   mesh    half-edge meshes, drawing and STL meshes for all grids
   io      XML, gzip, .xmpuzzle reader/writer, paused solves, imports
   model   Puzzle, Problem, Assembly, Disassembly, Solution, sliding, stacking, tilted trays
   grid    grid types, voxel spaces, symmetry tables
   base    StopToken, SolveState, Result/Error, BT_ASSERT, thread pool
```

Dependencies point downwards only: `app` may use everything below it, `base`
uses nothing. `core/` has no UI, does not print, has no global state (but
the sphere grid's registry of symmetry lists, decision 0020) and reads no
environment variable (`base/system.h`'s `userCacheDirectory()` does, for the
programs: nothing in the core calls it).

## Directory layout

| Path | Contents |
|---|---|
| `core/shapelogic/<module>/` | the library, headers and sources together; include as `"shapelogic/<module>/x.h"` |
| `engine/` | `shapelogic_engine`: JSON-RPC 2.0 over stdio around one `Document` (decision 0012); `btengine`, the same as a library for the unit tests; the WebAssembly build for the site's demo |
| `cli/` | `shapelogic_cli`: the original's burrTxt (the same options and text, `--json`) and burrTxt2's solve-and-save mode (`--save`) |
| `python/` | `shapelogic`, the Python module (pybind11, decision 0018): `src/` the bindings, `tests/` its tests, `pyproject.toml` its wheel; see `python/README.md` |
| `api/` | the JSON Schema of the engine's methods and events, its validator (`apischema.py`) and the generator of `docs/API.md`; see `api/README.md` |
| `web/` | the front end (decision 0013): `src/` (the map below), `tools/` (the API types' generator, packaging, the demo's build, the recorded sessions), `fake-engine/` (recorded sessions replayed as a transport), `tests/` (unit, engine, e2e, docs); see `web/README.md` |
| `tests/unit/<module>/` | Catch2 tests, one binary `bt_tests` (`app`, `base`, `engine`, `grid`, `io`, `mesh`, `model`, `solve`): every `.cpp` under `tests/unit` is built (a glob), the helpers are headers; `tests/unit/scratch_dir.h`'s `ScratchDir` is the tests' one way to a temp folder (a name of the process's own) |
| `tests/contract/` | the engine's contract tests: `shapelogic_engine` driven as a subprocess, every message checked against `api/` |
| `tests/cli/` | `test_cli.py` (ctest `cli`): `shapelogic_cli`'s options, refusals and modes on the fixtures |
| `tests/docs/` | `test_architecture_map.py` (ctest `architecture-map`): this file's map against the tree |
| `tests/oracle/` | the comparison harness against the original BurrTools (`tests/oracle/README.md`) |
| `tests/bench/` | `bench.py`: solve speed against the original, back to back |
| `tests/tools/` | small programs the harness drives (`bt_roundtrip`, `bt_resume`, `bt_solutions`, `bt_imports`, `bt_panex_resume`, the blockpack check) |
| `tests/data/` | fixtures: `puzzles/` (copied from the original; `new-grids/`, the hexagonal and cairo puzzles made by this engine; `tilted/`, Tilted Square puzzles made through the engine by its `make_fixtures.py`, which also writes the site demo's example: decision 0045); the original's recordings (`grid_dump.txt`, `model_dump.txt`, `app_dump.txt`, `stl_dump.txt`, `saved_by_original/`, `positions_by_original.txt`, `analysed_by_original/`, `imports_by_original/`, `slide_routes.txt`, `slide_solved_by_original/`, `stl_solution_by_original/`); `imports/` (other programs' files); `movement_checks_by_v2.txt` (BurrTools V2's); `slide_paused_by_v1.8.1/` (a sliding solve paused by v1.8.1's engine, with the script that made it: decision 0033) |
| `docs/` | this file, the plans (`*-PLAN.md`), API.md (generated), decisions/, handoff/, webui-spec.md, mockup/ (vendored), core-api.md (the draft API.md replaced) |
| `cmake/` | warnings, sanitizers, dependencies, the version from the git tag (`ProjectVersion.cmake`), whether the Python module is built (`Python.cmake`) |

## base

Files in `core/shapelogic/base/`:
- `assert.{h,cpp}`: `BT_ASSERT`, `BT_ASSERT_MSG`, `InternalError`; on in every build.
- `base64.{h,cpp}`: base64 for the engine's JSON and a puzzle file's images (decision 0038).
- `clone_ptr.h`: `ClonePtr<T>`, an owning pointer that copies its object (`clone()` when T has one), so classes with polymorphic or recursive members keep `= default` copies.
- `compress.{h,cpp}`: zstd blocks for the slide search's level files (decision 0040); the only code that sees the library.
- `error.{h,cpp}`: `Errc`, `Error`, `Result<T>` (`std::expected`), `fail()`.
- `file_lock.{h,cpp}`: `FileLock`, a lock two holders (programs or threads) cannot have at once: one solve per search folder (decision 0035).
- `int_cast.h`: `asInt()` / `asUnsigned()`, the spelled-out conversions ported code needs under `-Wsign-conversion`.
- `log_line.h`: `LogLine`, a line a search or solve tells for the app's log.
- `memory_share.h`: `MemoryShare`, the memory the sliding and stacking searches running at once share (decision 0028).
- `save_cadence.h`: `SaveCadence`, when a long search saves itself as it runs.
- `solve_state.{h,cpp}`: `SolveState`, `SolveStateCell` and the transition table (decision 0004).
- `stop.h`: `StopSource`, `StopToken`, `StopKind` (decision 0004).
- `system.{h,cpp}`: the machine: memory, free disk, `diskBudgetFor`, `gbToBytes`, `folderWriteError`, the cache folder.
- `thread_pool.{h,cpp}`: `ThreadPool`, the threads of one solve; `kMaxThreads` (256), `hardwareThreads()`, `splitThreads()`, `takeApartThreads()`.
- `version.{h,cpp}`: `versionString()`, from CMake; only version.cpp sees the define, so a new tag recompiles one file.

Rules (decisions 0004, 0009):
- One `StopSource` per run; nothing resets a source; a search that must end
  its own run makes a source from the token it was given, never a flag.
- A `SolveStateCell` has one writer at a time: the run's worker while it
  runs, the owner under the session's control lock before the run's thread
  starts and after it is joined. Others read it and stop runs by the source.
  `advance()` asserts that no other thread moved it meanwhile.
- `ThreadPool`s are made by a solve's owner with its budget, never global (no
  process-wide limit, no `SHAPELOGIC_THREADS`); nesting cannot deadlock.
- `SaveCadence`'s rule is also the front end journal's (`solver/cadence.ts`).

## grid

Files in `core/shapelogic/grid/`:
- `bitfield.h`: `Bitfield<N>`, a constexpr bit set (the sphere tables).
- `cairo_lattice.{h,cpp}`, `hex_lattice.h`: the cairo and hexagonal prism lattices (decision 0023): where a cell is and which cell holds a point.
- `grid_traits.h`: `GridTraits`, one row per grid kind (`kGridTraits`): the API name and label, capabilities, mesher, cube-like, scale buttons, default size and size unit; what differs by kind and is no geometry goes here, and a switch over `GridKind` lists every kind with no `default:` (`-Wswitch` finds the switches a new kind must join).
- `grid_type.{h,cpp}`: `GridKind` (the file format's numbers; `kNumGridKinds` checked against the enum), `GridType` (capabilities, `getSymmetries()`, `makeVoxel()`, the voxel factory).
- `shape_image.h`: `ShapeImage`, a shape's picture (decision 0038), moved with its cells.
- `symmetries.h`: `Symmetries`, a grid's transformation and symmetry-group tables (`transAdd`, `minimizeTransformation`, `calculateSymmetry`, …).
- `symmetries_impl.h`, `symmetries_0.cpp`, `symmetries_1.cpp`: the table-driven implementation over `tabs_0/` (cubes, rhombic, tetra-octa) and `tabs_1/` (triangular prisms).
- `symmetries_2.{h,cpp}`: the sphere grid's lists (decision 0020).
- `symmetries_derived.{h,cpp}`: the hexagonal and cairo grids' tables, derived at first use from their matrices (decision 0023).
- `symmetries_tables.h`: the three table sets, as constant-initialized pointers.
- `tabs_0/`, `tabs_1/`, `tabs_2/`: the original's generated tables, copied as they are.
- `TABLES.md`: where the tables come from, and the one syntax change.
- `tray_border.h`: `TrayBorder`, a sliding tray's border (decision 0046), kept on the `Voxel`.
- `types.h`: the module's basic types and the symmetry macros.
- `voxel.{h,cpp}`: `Voxel`, the abstract voxel space: cells (state and color per byte), box, hotspot, name, weight, per-transformation queries, comparison, connectivity, editing (`set` works the box out again, a pass over the space: a loop of `set`, `setState` or `setColor` runs inside a `Voxel::BatchEdit`, which does it once at its end; `clearMarks()` for a whole shape's marks).
- `voxel_{bricks,prisms,spheres,rhombic,tetra_octa}.{h,cpp}`: one class per grid of the original (`voxel_0` … `voxel_4`); rhombic and tetra-octa derive from bricks, a 5- or 6-aligned super-grid over cubes.
- `voxel_{hex,cairo}.{h,cpp}`: the hexagonal and cairo prism grids (decision 0023, after BurrTools V2).
- `voxel_table.{h,cpp}`: `VoxelTable`, a shape found in any orientation through a callback from index to shape.

Rules (decisions 0020, 0023, 0025, 0027, 0038, 0046):
- Checked value for value against the original (`tests/data/grid_dump.txt`)
  and against the cells' geometry (`tests/unit/grid/test_grid_oracle.cpp` on
  `mesh/cell_geometry.h`: neighbors, transformations and groups rebuilt
  from the face polygons). The original's grids have no geometry here
  (`mesh/` has it); the hexagonal and cairo lattices do.
- `GridKind` numbers are the file format's (7 hexagonal and 8 cairo are this
  program's own). Cubes, prisms, rhombic, tetra-octa, hexagonal and cairo
  assemble and take apart (the last four by derived movement checks);
  spheres only assemble; sliding assembles (the slide search replaces the
  take-apart); stacking does neither. Sliding and stacking are cube spaces.
- The table sets' symmetry instances are `constinit` statics behind
  constant-initialized pointers: `getSymmetries()` is an inline load with no
  guard, called in the assemblers' inner loops (the hexagonal and cairo
  grids' are derived at first use, behind a call).
- A `Voxel` holds its grid by value and is copied with `clone()`. It keeps
  the file attributes it does not know, so files round-trip; its start and
  goal marks only carry a sliding file's or an edited board's marks to the
  problems (0027), on the sliding grid alone; rebuilds move them (`moveMarks`).
- **Caches and threads.** `Voxel` computes its symmetry group and each
  transformation's hotspot and box lazily inside const functions: a shape
  read by several threads has `warmCaches()` called first (a split search
  calls `warmShapeCaches()` before its threads start, a disassembler when it
  is made); editing invalidates them.
- Deliberate differences from the original (docs/handoff/stage-1.md):
  `getHotspot` of a transformation the shape cannot take fails whatever the
  call order; the cube transform drops the transformation cache.

## model

Files in `core/shapelogic/model/`:
- `assembly.{h,cpp}`: `Placement`, `MirrorInfo`, `Assembly` (`transform`, `smallerRotationExists`, `createSpace`, `sort`, the editing helpers).
- `disassembly.{h,cpp}`: `Disassembly` (level and move counts), `State`, `Separation` (the tree), `SeparationInfo` (the compact pre-order form), `movesText()` (cut at the original's 256 bytes: the text oracle compares it); `kMaxSeparationDepth`, how deep a tree may nest (the reader refuses a deeper one, the take-apart makes none): no walk of a tree but its copy depends on its depth.
- `grid_convert.{h,cpp}`: `convertGrid`: a cube-grid puzzle on the rhombic (5x5x5 cells a cube) or tetra-octa (3x3x3) grid, with its colors, notes and problems; `convertTargets`; `kMaxConvertedCells`.
- `movement_values.h`: the movement analysis' special values (`movement::kFree`, the removal step, the states' mark of a removed piece, the original's), with `static_assert`s of how they relate.
- `problem.{h,cpp}`: `Problem`: parts (at most `kMaxPartCount` pieces each), result, goal, color rules, holes, state, counts, the saved position, pending assemblies, solutions and their sort orders (`solutionSortOrder`), stored solver options, stacks, slide marks, the uid (decision 0044).
- `puzzle.{h,cpp}`: `Puzzle`: grid, shapes (`ClonePtr<Voxel>`), constraint colors, problems, comment, rod sets, images, a tilted tray.
- `puzzle_image.{h,cpp}`: the puzzle's table of pictures (decision 0038).
- `slide_marks.h`: per cell of a sliding board a start mark and a goal mark (at most `SlideMarks::kMaxMark`), per problem (decisions 0021, 0027).
- `sliding.{h,cpp}`: sliding puzzles (decision 0014): start/goal shapes, stamps, the problems kept in step, start locks, layouts, `prepareForSolving` (a solve's own problem's maximum of holes, nothing else).
- `solution.{h,cpp}`: `Solution`: an assembly, an optional `Separation` or `SeparationInfo`, its numbers, a runtime id.
- `stacking.{h,cpp}`: stacking puzzles (decision 0015): discs, stacks, `setupError`, `layoutBoard`, what the searches read and make; `stacking_play.h`'s functions at its end.
- `stacking_play.h`: a stacking puzzle played by hand (decision 0042): `playStart`, `playMoves`, `playReplay`, `positionRefusal`, `positionMap`.
- `stacking_types.h`: `RodSet` (rods, height, size and distance rules, Panex columns with a pocket and an outside channel), `DiskRef`, `StackMap`.
- `tilted_tray.{h,cpp}`: a Tilted Square tray (decision 0045, namespace `tilted`): `Tray`, `layout`, `bestShift`, `canonicalShift`, `placements`.
- `xml_element.h`: `XmlElement` (the tree the reader builds) and `PreservedXml` (file content kept without being modeled).

Rules (decisions 0014, 0015, 0021, 0027, 0042, 0044, 0045):
- Checked value for value against the original (`tests/data/model_dump.txt`).
- No locks and no atomics (the original's problem had both). While a
  `SolveSession` runs it alone writes its problem, under its own lock;
  others read through `readProblem()`.
- A problem holds no reference to its puzzle: what needs shapes or colors
  takes the `Puzzle`. Removing or swapping shapes and rod sets goes through
  the puzzle, which updates every problem (and the stacks: `trimStacks`).
  Pieces are numbered part by part; assemblies and solutions follow that
  order and are renumbered when parts change.
- Sliding: starts and goals are each problem's own, apart from the colors,
  so problems share a board; the assembler holds each piece to its start
  marks (start locks), so it finds only start layouts; a file of the
  original's, its starts in cell colors, is converted as it loads
  (`convertLegacyStarts`). A layout is, per piece, the lowest corner of its
  cells on the board, which no hotspot moves.
- Played by hand (`stacking_play.h`, `solve/sliding_play.h`): asked one
  position at a time, no game kept.
- A Tilted Square rectangle is by rule the square with the rows or columns
  beyond it walled; the tests hold the two equal, cell for cell and in the
  best count. The board's shape is made by `app/document_tilted.cpp`.

## io

Files in `core/shapelogic/io/`:
- `blockpack.{h,cpp}`: files packed in chunks on a pool's threads, sorted 16-byte keys as variable-length gaps: byte for byte the original's packing.
- `gzip_file.{h,cpp}`: whole files through zlib (level 9) and the same in memory (`gzipCompress`, `gunzipIfCompressed`: the one inflate, to 256 MiB at most, a stream cut short refused; `readMaybeGzipFile` is `readPlainFile` and it); `writeFileWhole`, `writeFileStreamed`, `replaceWith`, `saveTarget`, `syncDescriptor`.
- `imports.h`, `ps3d_file.cpp`, `scad_file.cpp`: other programs' files (decision 0017): `readPuzzleSolver3D`, `readPuzzlecad`, `writePuzzlecad` (puzzlecad's `burr_plate`) and `puzzlecadRefusal` (every caller's refusals), line for line the original's loaders; `kMaxImportCells`.
- `paused_solve.{h,cpp}`: the paused solve file (`.slsolve`, the original's BTSOLVE2), both ways, streamed; `readPausedSolve`, the one reader (a file's stream or bytes'), each section held to what is left of the file.
- `puzzle_file.{h,cpp}`: `loadPuzzle`, `parsePuzzle`, `readPuzzle`, `savePuzzle`, `writePuzzle`; `giveUids`; the bounds on a file's numbers at the top of the .cpp (`kMaxFileCoordinate`, `kMaxSlideDepth`; a tree's depth is the model's `kMaxSeparationDepth`).
- `xml_document.{h,cpp}`: the tree built from the parser, within `XmlLimits` (16 million elements, 4096 attributes an element, 5000 levels), and a tree written back.
- `xml_pull_parser.{h,cpp}`: the original's parser (internal; its `XmlParseError` never leaves the module).
- `xml_writer.{h,cpp}`: `XmlWriter`, the original's output layout.

Rules (decisions 0006, 0008, 0017, 0038, 0044, 0045, 0046):
- `.xmpuzzle` is gzip-compressed XML; plain XML is read too. Errors are
  `Result` values (`ParseError` with the line, `IoError`). **A file is
  untrusted**, a saved search's too: what is read from one is bounded where
  it is read (the map names the bounds); no reader of a puzzle throws.
- **Compatibility is one way** (0006, amended): every file the original wrote
  opens, converted on load; the original need not read our saves. Unknown
  attributes and elements of `<puzzle>`, `<problem>` and `<solution>`, and
  unknown attributes of `<voxel>`, are kept and written back in place.
  Stacking rod sets and stacks are written as the original wrote them.
- Saves are the original's bytes but for what this program adds; each
  problem's `uid` (0044) is one, so tests compare saves `withoutUids`
  (`tests/unit/io/test_puzzle_file.cpp`). `giveUids` gives a problem without
  one a uid from its place and definition, the same at every open, except
  the first of each definition paused by a version before uids, which keeps
  none so that its saved search, named without one, is still found.
- What this program adds is written only when present, so other files are
  written as before: pictures (`<images>`, a PNG or JPEG in base64 each, and
  `image…` attributes on a `<voxel>`), a tray's border (`border…` attributes
  on the board's `<voxel>`, only where they differ from none), the tilted
  tray (`<tray>` after `<gridType>`: `size` for a square, else `width` and
  `height`, `over`, `down`, `allowance`, `placement`, `shiftX`, `shiftY` in
  as few digits as read back alike, `walls` row by row, `#` a wall). A
  `<tray>` the reader cannot take is kept as it is; another grid's is dropped.
- **Safe writes** (`gzip_file.h`): beside the file, checked once closed,
  synced, then renamed over it (the folder synced), puzzle, paused solve and
  saved searches' `state.bin` alike, so a failed save leaves the old file;
  a saved search's level files are synced plainly before the `state.bin`
  naming them is synced fully (macOS `F_FULLFSYNC` empties the drive's cache,
  theirs too); the temporary file made exclusively; a link followed, a
  read-only file refused, the permissions kept (the original's rename
  replaced the link and the read-only file). The main process's writes also
  go through a temporary file.
- A paused solve file is streamed both ways (a saved search's levels can be
  gigabytes); imports read the file as it is (`readPlainFile`, never
  decompressed) within a budget of cells the original lacked.

## solve

Files in `core/shapelogic/solve/`:
- `assembler.{h,cpp}`: `Assembler`, the interface (`createMatrix` with its stop token, `reduce`, `assemble -> RunEnd`, `savePosition` / `restorePosition`, the placement browser, the debugger); `SearchOptions`; `AssemblerError`; the problem hooks `beginSolving`, `storePosition`, `canStartSolving`; `warmShapeCaches()`.
- `assembler_0.{h,cpp}`: `Assembler0`, dancing links (every part exactly one piece): plain (flag 0), split into subtree tasks (flag 2), vector (flag 3).
- `assembler_1.{h,cpp}`: `Assembler1`, Wei-Hwa Huang's cover with counts (every other problem): a state machine over explicit stacks (flag 0), tasks (flags 4, 2, 5, 6), vector (flag 3).
- `CROWELL.md`: Crowell's solver, feature by feature against the Fortran original.
- `disasm_to_moves.{h,cpp}`: `DisasmToMoves`, piece positions at any step of a tree (turns as angles about a pivot), and `FixedPositions`, behind `PiecePositions`.
- `disassembler.{h,cpp}`: `Disassembler`, the take-apart of one problem's assemblies (the parts still to take apart wait on the heap, not on the stack); `DisassemblerOptions`; `TakeApartResult` (a tree, stopped, or `tooDeep` past `maxDepth`) and `tooDeepMessage()`; `canDisassemble()`, the one rule for whether a puzzle has a take-apart.
- `disassembler_node.{h,cpp}`: `DisassemblerNode` (places, orientations, the position it came from, the move) and `NodeRef`, a counted reference.
- `dlx_matrix.{h,cpp}`: `DlxMatrix`, assembler 0's matrix (`build`, `reduce`, `clumpify`, the placement browser), and `DlxNodes`, the node arrays a thread copies.
- `grouping.{h,cpp}`: `Grouping`, the groups of pieces that may stay together, keyed by part.
- `movement_analysator.{h,cpp}`: `MovementAnalysator`, Bill Cutler's moves from one position; one per thread.
- `movement_cache.{h,cpp}`, `movement_cache_{bricks,prisms,derived}.cpp`: `MovementCache` and its grids' kinds, `makeMovementCache()`.
- `movement_checks.{h,cpp}`: the movement checks derived from the cells (decision 0025): `movementDirections`, `deriveMovementChecks`, `movementChecks`.
- `node_hash.{h,cpp}`: `NodeHash`, `CountingNodeHash`: sets of take-apart positions.
- `panex_search.{h,cpp}`: the Panex search (`panex::solve`, `PanexSearch`, `Outcome`), `runsOnPanexSearch` (which stacking solves run on it) and its saved searches (decision 0015), a `state.bin`'s keys checked (`Rules::holds`) before one is decoded.
- `placement_finder.{h,cpp}`: `PlacementFinder`, a rotated piece's placements in the result, in the original's order.
- `prepare_progress.h`: `PrepareCounters` / `PrepareProgress`, the preparation's counts for progress displays.
- `resume_state.h`: `SplitSearchState<Task>` and the saved position's tail (`K` / `T C S`, then the weights `W`).
- `rotation_filter.h`: `RotationFilter`, the check that drops rotated or mirrored copies of assemblies.
- `rotation_moves.h`: `RotationMoves`, the 90 degree turns' generator interface.
- `rotation_moves_classic.{h,cpp}`: `RotationMovesClassic`, complete: every group about every pivot.
- `rotation_moves_crowell.{h,cpp}`: `RotationMovesCrowell`, the Fortran solver's heuristics (`CROWELL.md`).
- `rotation_rules.{h,cpp}`: `RotationRules`, whether a 90 degree turn is possible: bit for bit the original's answers.
- `row_rotation_check.{h,cpp}`: `RowRotationCheck`, the rotation check answered from a cover's rows (decision 0024).
- `search_run.{h,cpp}`: the run layer (decision 0009): `SearchRun`, `TaskProgress`, `SplitTasks<Task>`, `runSplitTasks()`, `effectiveThreads()`.
- `search_sample.h`: `SearchSample`, the live preview's meeting point (`request`, `claim`, `put`, `take`).
- `simd_bitset.h`, `simd_exact_cover.{h,cpp}`, `simd_huang_cover.{h,cpp}`: the vector covers of assemblers 0 and 1 (256 to 32768 columns), `SimdKernels`.
- `slide_forecast.{h,cpp}`: `slideForecast`, the Performance Analysis from each depth's time (decision 0030).
- `slide_key.h`: `BigKey`, the slide search's keys of 192, 256 and 512 bits (decision 0037).
- `slide_levels.h`: `KeySet`, `ReadFile`, `WriteGate`: the slide search's sorted, delta-coded sets, in memory or in files; a file's index checked as it opens (`tablesHold`: `SetFault::Damaged`), each block as it is read.
- `slide_phase.h`: the parts of a slide move that visit no arrangement (the merges, the writes, the saves, the pass for the first move to the goal).
- `sliding_play.h`: a sliding puzzle played by hand (decision 0039): `playStart`, `playReach`, `playReplay`.
- `sliding_search.{h,cpp}`: the slide search (decision 0014): `findSlidePath`, `SlideSearch`, `SlideOutcome`, its saved searches; `slideRoute`, `applySlideRoutes`, `finalPlacementKey`; `sliding_play.h`'s functions at its end.
- `solve_progress.{h,cpp}`: `SolveProgress` and `SolveStats`.
- `solve_session.{h,cpp}`: `SolveSession`, the solve of one problem across its runs (decision 0010); `SolveOptions`, `SolveFailure`; `solveSetup`, the rules that build a solve's matrix (a sliding puzzle's, a tilted tray's face-up pieces), with `trayPlaneTurns` and `storedFlipPieces`.
- `solver_type.{h,cpp}`: `SolverType` (Classic and Crowell, labeled Complete (ShapeLogic) and Fast (Crowell)), `chooseAssembler()`, `makeAssembler()` (decision 0019: the original's BurrTools 2 solver is not kept; where decisions 0009 to 0011 describe it, 0019 holds).
- `stack_search.{h,cpp}`: `findStackPath`, the Stacking Solver's own search, and `StackingOptions`.
- `subtree_split.h`: `SubtreeSplitter`, through which the vector searches hand branches on.
- `take_apart_pool.{h,cpp}`: `TakeApartPool`, a search's assemblies taken apart side by side (the CLI's).

Rules of the assemblers (decisions 0009, 0024, 0041, 0048):
- Checked by the harness: assembly counts on every tier, one and all
  threads; the deterministic searches' iteration counts equal (the harness
  only: tests never assert them); saved positions carry on in both programs.
- A run polls only its run's token; a callback answering false pauses it,
  an exception aborts it; nothing is reset, the next `assemble()` carries on;
  `RunEnd::Finished` only from the search itself; a stopped split or vector
  search continues the way it ran. Assemblies handed over after a pause was
  asked for are not found again. The assembler never writes the `Problem`.
- A position that cannot be restored is an error; the problem keeps it and
  what was found (0041: Solve starts over).
- The vector searches recurse a level for each piece: past
  `SearchOptions::vectorStackBudget` (a level's stack is measured,
  `stackPerLevel()`) the plain search runs, which keeps its own stacks.
- `DlxMatrix::reduce` removes only rows the placement check after it
  removes, so the reduced matrix is the original's (0048).

Rules of the take-apart (decision 0025):
- Checked by the harness: every summary field on all 180 corpus puzzles,
  Classic and Crowell, one and all threads; the solved files solution by
  solution. The rhombic, tetra-octa, hexagonal and cairo grids' take-apart
  is not the original's (it has none): derived from the cells, after
  BurrTools V2, and the same derivation gives the original's answers on
  cubes and prisms (`--derived-checks`). Spheres are assembled only, and so is a puzzle with a tilted tray (its walls are not cells): `canDisassemble()` is the one rule, for the session, the analysis, the explorer, the CLI and Python.
- Spread over threads it gives exactly one thread's result: a level's
  positions are expanded on several threads and put together in one
  thread's order. Levels nest inside the assembler's callback without
  deadlock; the shapes' caches are warmed when a disassembler is made.
- A stopped take-apart returns no separation and says so (`stopped`), which
  is not "cannot come apart".

Rules of the slide search (decisions 0014, 0029-0034, 0037, 0040, 0041,
0044; the `sliding` tier compares summaries, solved files state by state,
text and routes):
- One solver, no limit on arrangements visited (the original's Fast and
  Deep solvers and its queue search are not kept); any number of threads
  finds the same path; memory stays flat and the disk is the limit.
- **Saved searches must stay readable by later versions**: the folder names
  (`slide-<fingerprint>`, the fingerprint from `problemKeyOf`, which mixes in
  the uid) and `state.bin`. A saved search is never deleted by its age or
  because it cannot be carried on (0041: Disk with `savedUnread`, the solve
  pauses, Solve throws it away); one saved from the start alone by v1.8.1
  or earlier goes on that way (`sliding.resumeAdvice`); level files up to
  1.9 (`BTSLKEY2`) are read and converted under their own names.

Rules of the stacking searches (decision 0015; the `stacking` tier compares
both solvers; `panex-resume` stops searches at a sweep of depths in one
program and carries them on in the other, the saved folders byte for byte
alike):
- The Panex search's saved folder is the original's bytes (`BTPANEX4`),
  named by the rules, the stacks and the uid (none for a problem paused
  before uids); its steps run on a pool a core larger than the levels'
  threads, as the original had a writer thread. It saves itself by
  `SaveCadence` (`StackingOptions::autosave`) and when it stops, but for an
  abort; a save that fails is a disk error, the folder kept as last saved.

Rules of the solve session (decision 0010; checked by `compare-files` and
pause/continue stress tests):
- A sliding problem: the assembler (Classic, mirrors and rotations kept,
  pieces held to their starts) finds the start layouts, each searched in its
  callback. A stopped search puts its start back pending, not counted; one
  out of disk or unable to write ends the run. At a save point the search
  stops at the end of its level (0034). A stacking problem: no assembler,
  one start searched; `prepare()` refused, a save point does nothing.
- A solve that searches holds `FileLock` on the problem's search folders for
  its run; a second solve of the same problem, here or in another program,
  is refused with `IoError` before anything is dropped (0035). A solve that
  starts afresh drops the problem's saved searches first (0033).
- The owner's calls come one at a time; the first state move of a run is
  made before its thread starts. One `StopSource` per run, a child for the
  assembly. A run with counted assemblies still waiting ends Paused.
- **Lock order** (outer before inner; no session lock is taken under an
  assembler's or disassembler's): the control lock around the results, run
  and failure locks; the results lock around `beginSolving` (an assembler 1
  replay holds up `readProblem` and `progress()` meanwhile) and
  `storePosition`; the run lock around the assembler's `finished()` /
  `iterations()` and the disassemblers' progress and times (outside their
  own locks); the queue lock around a stop request.

## mesh

Files in `core/shapelogic/mesh/`:
- `cell_geometry.{h,cpp}`: the cells of any grid as convex polyhedra (`CellPolyhedron`) and `CellGeometry`, a space's cells with the walks between them (`run`, `derivedNeighbors`).
- `cell_sweep.{h,cpp}`: `sweepEnters`, `sweptCells`: the cells a moved cell passes through (decision 0025).
- `cube_mesh.{h,cpp}`: the cube grids' STL mesh (`cubeSurface`, `cubePolyhedron`) from the chamfer table.
- `draw_mesh.{h,cpp}`: `DrawMesh`, a mesh as plain arrays for a binary frame; `shapeDrawMesh`.
- `grid_geometry.{h,cpp}`, `grid_geometry_tables.h`: `connectionFace`, `calculateSize`, `recalcSpaceCoordinates` for every grid; `spaceRule`, the last two as linear rules (engine.info's `cellToMesh` and `extent`), held to them by a test.
- `grid_geometry_{0,1,3,4}.cpp`: the original's grids' faces, each with its `meshverts.inc`.
- `grid_geometry_{7,8}.cpp`: the hexagonal and cairo cells' faces (decision 0023).
- `mink_mesh.{h,cpp}`: the prism, rhombic and tetra-octa grids' STL mesh: Minkowski sums and differences on Manifold.
- `modifiers.{h,cpp}`: `fillPolyhedronHoles`, `scalePolyhedron`, `mergeCoplanarFaces`, `volume`.
- `polyhedron.{h,cpp}`: `Polyhedron`, the half-edge mesh by numbers; `VertexList`, `FaceFlag`, `finalize()`.
- `sphere_mesh.cpp`: the sphere grid's meshes (`sphereMesh`).
- `stl.{h,cpp}`: the STL export (decision 0016): `stlParameters`, `stlMesh`, `stlFile`.
- `tabs_{0,1,3,4}/`, `tabs_cube/`: the original's tables, copied unchanged.
- `triangulate.{h,cpp}`: ear clipping with holes bridged (Eberly's rule), for the cube mesher.
- `uniform_mesh.{h,cpp}`: the uniform mesher for every polyhedral grid (decision 0022).
- `vec3.{h,cpp}`: `Vec3`, with the original's float arithmetic.
- `voxel_mesh.{h,cpp}`: `shapeMesh(voxel, MeshStyle)`, `cellMesh`.

Rules (decisions 0016, 0022, 0023, 0025):
- The drawing meshes are the original 3D view's (`tests/data/app_dump.txt`:
  every fixture shape face by face, the wire frame by its Euler
  characteristic). The STL meshes are the original's shapes for every
  fixture shape (`tests/data/stl_dump.txt`): compared by box, area and
  volume within 1e-5, not by triangles, which differ between compilers
  (`tests/unit/mesh/stl_invariants.h`); each closed and manifold.
  `Polyhedron` keeps the original's list order; the sphere mesher its float
  and double expressions as they were (the conversion warnings off for
  that file).
- Manifold 3.5.2 is the meshers' library; the core's others are zlib (`io`)
  and zstd (`base/compress`).
- The uniform mesher's STL switches are off by default, so the defaults
  stay the original's; more than 6 sphere recursions are refused.

## app

Files in `core/shapelogic/app/`:
- `analysis.{h,cpp}`: `Analysis`, saved assemblies taken apart afterwards on a thread of its own.
- `background_job.{h,cpp}`: `BackgroundJob`, what `Analysis`, `PlacementFit` and `AssemblyExplorerBuild` share (the thread, the stop, active, the done count, the failure); the document keeps the first two in one map by (problem, kind), the engine the third by its handle.
- `document.{h,cpp}`: `Document`: the puzzle, ids, revision, dirty flag, undo history, the queries, solving, analyses, files, playback and explorers.
- `document_commands.cpp`: the editing commands, from the original GUI's callbacks.
- `document_internal.h`: the undo steps, a run's among them (internal to `document*.cpp`): a step holds what its command changed (`touchProblemSettings` where no solution changes: the problem without them), and the steps are kept to the depth and to `DocumentSettings::undoBytes` (512 MiB; the newest always stays).
- `document_sliding.cpp`: the sliding commands (decision 0014): starts and goals, stamps, layouts.
- `document_stacking.cpp`: the stacking commands (decision 0015): rod sets, discs, stacks, saved searches.
- `document_tilted.cpp`: a document with a tilted tray (decision 0045).
- `explorers.{h,cpp}`: `AssemblyExplorer` (its matrix the solve's, `solveSetup`; made in two steps: `copy()` under the problem's lock, `build()` from the copy alone) with `AssemblyExplorerBuild`, its build as a job the engine keeps, and `MovementExplorer`, each on a copy of the puzzle.
- `layer_geometry.{h,cpp}`: `layerGeometry`, the grid editor's drawing of a layer.
- `placement_fit.{h,cpp}`: `PlacementFit`, a problem's assemblies counted on each board of the tray's placements.
- `playback.{h,cpp}`: `PlaybackSampler` and `orientedMesh`.
- `queries.{h,cpp}`: `problemFit`, `shapeTable`, `shapeInfo` (one row of it), `shapeStatusLine`, `isNotchable`, `isMillable`, `problemKind`, `rodSetLayout`, `stlPieces`.
- `shape_tools.{h,cpp}`: the tool tabs' tasks per grid (`isNudge` the one test of a nudge) and the grid editor's cell tools.
- `solve_setup.{h,cpp}`: `solveRefusal` and `prepareShapesForSolving`, the Document's rules before a solve, which the Python module and the CLI's `--save` call too.
- `tray_board.h`: a tilted tray's board and the pieces on it (`fillTrayBoard`, `onTray`, `keepOnTray`, `fitToTray`; internal, defined in `document_tilted.cpp`).

Rules (decisions 0011, 0014, 0015, 0027, 0033, 0039, 0044, 0045):
- The owner calls the document from one thread. A command checks first and
  changes after, so a refused one changes nothing; each is one undo step
  (a drag's group still gives every change a revision), the start of a run
  or a take-apart too (0011's amendment; `solveAbort` is not undoable), and
  adds its bits to `lastChange_` beside its helpers', never over them.
  `poll()` puts in what background work did as a revision of its own.
- While a run, an analysis or a tray's placement fit is active
  (`solving()`), shapes, lists and the solving problems are frozen; runs and
  analyses only read shapes (hotspots reset and caches warmed before the
  first starts) and write their own problem, so they run side by side.
  `solveAbort` is the hard stop of a solve under way and refused otherwise:
  a finished run's solutions go by `deleteSolutions` or the undo of its run.
- A solution is found by its id under the solve's lock (a run moves
  solutions about); ids and `Problem::solutionsVersion()` never repeat (a
  problem an undo swaps in counts on, `countOnFrom`). Names and notes refuse
  what a file cannot hold (control characters; line breaks in names).
- Sliding: a command works on one problem's starts and goals; an edit of a
  board keeps, in every problem on it, the stamps whose cells survive and
  removes the others with a notice (`ChangeSet::notice`). What the
  original grayed out is refused (groups, the count of a piece a stamp
  needs), but for its problems: made, copied and deleted as on other grids.
- Stacking: since decision 0044 each problem's uid names its search folder,
  so copies have their own. Two problems share one only when both were
  paused before uids with the same rules and stacks; then one run at a time
  works on it (`sharedSearchActive`: Solve, Continue and Abort of the other
  refused, its saved search not told meanwhile).
- Tilted trays: the board is shape 0, never deleted, copied, moved, edited or
  a piece. A cell edit keeps to whole cells and a shape task that would not
  is refused; a tray change moves each piece to its nearest whole-cell
  place, and one with none stays where it was (`fitsTray()` tells).
  `PlacementFit` runs an ordinary `SolveSession` per board, so a count is
  what the Solver would find there.
- Checked against the original: the analysis (`tests/data/analysed_by_original/`,
  byte for byte), the layer drawing (its corner formulas), the playback's
  tracks (`app_dump.txt`).

## engine

Files in `engine/`:
- `app_log.{h,cpp}`: the engine's lines for the app's log (the `log` setting: `off`, `info`, `debug`).
- `convert.{h,cpp}`: the schema's enum names, the engine's own bounds of its inputs and the JSON of the items (snapshot, summaries, fit, meshes as frames, solver settings, options, progress, failures, solution rows, placements).
- `engine.{h,cpp}`: `Engine` (the dispatcher: `handleLine`, `poll`, `run`), `State`, `Settings`, the method table.
- `events.cpp`: `document.changed` and the poll's events (solves, analyses, fits, assembly explorers being prepared).
- `main.cpp`: the process's entry.
- `methods_{color,document,engine,explore,export,playback,problem,shape,sliding,solution,solve,stacking,tray}.cpp`: one file per schema file in `api/` (`document.*` with `edit.*`, `engine.*` with `settings.*`, `shape.*` with `grid.*`).
- `output.{h,cpp}`: `Output` (lines and frames), `FrameBuilder` (typed arrays at multiples of four, described for the reply).
- `rpc.{h,cpp}`: the error codes (`Errc` → `-32001`…, `data.errc` on every error), `Params`, the ids, `BTE_TRY` / `BTE_CHECK`.
- `wasm_main.cpp`: the engine compiled with Emscripten for the site's demo.

Rules (decisions 0012, 0028, 0043):
- The methods are `api/*.json`, documented in the generated `docs/API.md`;
  a new method follows `api/README.md` (schema, a contract call that
  succeeds, `gen_api_md.py`, `just app-gen`).
- Requests run one at a time in order on the main thread, which alone
  touches the document and stdout. A reply comes before the events it
  caused, a frame before the reply that refers to it (so a notification gets
  no frame); a run's last `solve.state` and an analysis's end come before
  the `document.changed` of their results. The revision clients see never goes back (a new document
  counts on from the old one). Handles hold copies and survive edits; a new
  document drops them. Long work (a solve, an analysis, a fit, an assembly explorer's matrix: `explore.ready`) runs on a thread of its own, its stop asked for on the main thread, never waited for there; `solution.page` keeps its order in `State` (`PageOrder`).
- A malformed or oversized line is answered, never fatal; `Params` refuses
  missing, mistyped and unknown fields as the schema does; `core/` never
  sees JSON. An exception outside a handler is logged; the engine ends by
  letting its document go (at end of input, shutdown or a refused write).
- `Settings` carry what the core may not read from the environment (the
  search folders, disk budgets, `searchMemoryGB`: one `MemoryShare` for all
  running searches, 0028) into every solve's options; a `solve.continue`
  without options of a solve paused in memory goes on with its last run's
  (0043) and these settings as they are now.
- Tested in process (`tests/unit/engine/`) and as a subprocess
  (`tests/contract/`: every message against the schema, every errc one the
  method declares, every method called with success; against the dev, ASan
  and TSan engines).

## cli

Files in `cli/`:
- `dot_level.h`: the level text as burrTxt `--json` read it, and the best level of a solve.
- `main.cpp`: the modes: the solve, `-x`, `-a`, `--save`, `--progress`, sliding and stacking puzzles.
- `options.{h,cpp}`: `Options`, `parseArgs`, `usage(kind)`: burrTxt's options and rules, burrTxt2's as long options for `--save`, the A/B switches.
- `text_print.{h,cpp}`: burrTxt's text drawings.

Rules:
- Checked by the harness: `just oracle` reads its `--json`, `just
  oracle-text` compares its text with burrTxt's byte for byte, `just
  oracle-files` solves with `--save`. `--original-drawings` draws as burrTxt
  did (wrong shapes below the first separation), for the comparison.
- The CLI reads the environment the core does not (`SHAPELOGIC_SLIDE_DIR`,
  `SHAPELOGIC_SLIDE_DISK_GB`, `SHAPELOGIC_PANEX_DIR`,
  `SHAPELOGIC_PANEX_DISK_GB`), and prints what the original's library said on
  stderr from what the core returns.

## python

Files in `python/src/`:
- `module.cpp`: the module's classes, names, docstrings and reprs as the original's; new arguments are keywords.
- `py_{puzzle,shape,problem}.{h,cpp}`: `PyPuzzle` (a `shared_ptr<Puzzle>`), `PyShape` and `PyProblem` (the puzzle and an index).
- `py_solution.h`: the plain values handed out for an assembly or a solution.
- `solution_iterator.{h,cpp}`: `SolutionIterator`, the solve behind `Problem.solve`.

Rules (decision 0018):
- Built when `BT_PYTHON` is `ON`, or `AUTO` and CMake finds Python's headers
  outside sanitizer and cross-architecture builds (`cmake/Python.cmake`, which
  then builds everything position independent). The names are the original
  module's, so its scripts run unchanged: change them only with a reason.
- Python sees only copies or the shared puzzle through an index, read
  afresh at each call; nothing the worker thread reads is reachable from
  Python; the module reads no environment variable (`work_dir`).
- Checked by `python/tests/` (ctest `python`), `oracle.py python` and by
  hand under the sanitizers (docs/handoff/deferred-7.md).

## web

Node 22; the packages pinned in `web/package.json` (three.js 0.186.1,
Playwright 1.64.0, gltf-validator for the tests).

Files in `web/src/engine/`:
- `api.generated.ts`: the types of `api/*.json` (`tools/gen-api.ts`, `just app-gen`), committed; a test fails when it is stale.
- `app-bridge.ts`: what crosses between the main process and the renderer besides the engine (`window.btApp`'s interface and the checks of what crosses).
- `base64.ts`: `toBase64`, `fromBase64`, without Node (the one copy).
- `bridge.ts`: the engine's messages between the host and the renderer's proxy (decision 0013).
- `client-types.ts`: `EngineApi`, `Reply<M>`: what the client and the proxy both offer.
- `client.ts`: `EngineClient`: `engine.hello` first, typed calls pipelined in order and bounded, errors as `EngineError`, coalescing by key, typed events.
- `dispatch.ts`: `OrderedInbox` (a reply settles before the events behind it are dispatched) and `makeDefer`.
- `errors.ts`: the errors a call ends with.
- `explore-open.ts`: `openAssemblyExplorer`: `explore.openAssembly`, the wait for `explore.ready`, the progress meanwhile, and the cancel that ends the engine's build; for the renderer's explorers and the tests.
- `frames.ts`: `viewFrame` (typed-array views without copies), `attachFrame`, `meshArrays`, `poseArrays`.
- `framing.ts`: `FrameParser` (stdout as lines and frames), `StderrRing`.
- `handlers.ts`: `EventHandlers`, `Listeners`: the one registry of event, status and problem handlers (the client, the host, the proxy), each handler guarded.
- `node-transport.ts`: `NodeTransport`, the engine as a child process (the only Node file here).
- `recorder.ts`: `RecordingTransport`, a session recorded byte for byte.
- `serve.ts`: `serveEngine`: the host's side of the bridge, `admit` for paths (the main process's and the demo's).
- `spawn-failure.ts`: `SpawnFailure`, `EngineStartError`: a failed start with every field the system gave.
- `start-diagnosis.ts`: a failed start's likely cause by code, the Windows exit codes.
- `transport.ts`: `Transport`, the byte pipe the client speaks over; `setOnce`, its handlers set once.
- `write-hint.ts`: the Controlled Folder Access hint of a write Windows refused.

Files in `web/src/main/`:
- `app-ipc.ts`: `window.btApp`'s main side: `installAppIpc` over the groups in `ipc/`, `AppIpcHost` their interfaces joined.
- `app-log.ts`: the app's log file (`logs/shapelogic.log`; one handle, a tick's lines written together), its rotation and the Log File Manager's pages; the engines' stderr and protocol problems in it.
- `cache-files.ts`: View ▸ Manage Cache and Autosave Files: the items, `CacheLinks`.
- `cache-folder.ts`: the cache folder (`CacheFolderManager`): the engines' `slideFolder` and `panexFolder`, moves and deletes.
- `cli-tool.ts`: the command line tool the app ships, and its install.
- `diagnostics.ts`: Help ▸ Copy Diagnostics: the report, the system checks on request.
- `engine-hash.ts`: the engine's SHA-256 against `engine.sha256`.
- `engine-host.ts`: `EngineHost`: one engine per window, restarted when it dies, its status told.
- `engine-stops.ts`: `EngineStops`: the engines of closed windows until they have ended (their solves pause first); a quit waits for them, up to `QUIT_WAIT_MS`.
- `engine-path.ts`: where the programs are (`programFile`, `builtProgram`, `findProgram`, `findEngine`): the `.exe` and `build/<preset>/{engine,cli}/` rule of the app, the tools and the tests.
- `env.d.ts`: what electron-vite puts into the main process at build.
- `export-file.ts`: `writeFileSafely`, an export written as the core writes a puzzle; `ExportGrants`.
- `goal-window.ts`: `GoalWindows`, the Play tab's goal picture in a window of its own.
- `gpu-check.ts`: WebGL2 at start-up on Linux.
- `health.ts`: the "App Health" block logged at every start.
- `index.ts`: Electron's main process: windows, hosts, IPC, menus, files the system opens, the app's scheme.
- `ipc/cache.ts`: the cache folder's choice; the cache manager's items, deletes and links.
- `ipc/cli.ts`: the command line tool's info, install and uninstall.
- `ipc/engine.ts`: the window's engine: `bt:call`, the first document, Try Again, held commands, Copy and Save Diagnostics.
- `ipc/files.ts`: the open, save and folder dialogs (`KINDS`), the export grants and writes, a dropped file's grant.
- `ipc/log.ts`: the Log File Manager's pages, Clear, close and Export.
- `ipc/register.ts`: `registrar` (`handle`, `on`, `sync`: the sender checked once), `post`, `startDir`, `logRefusedWrite`.
- `ipc/rescue.ts`: the rescue copy's record: written by the window's engine, read, cleared.
- `ipc/store.ts`: the stored settings and layouts, recent files, the app's info, the update check.
- `ipc/window.ts`: the menus' state, the close, New Window, a file's owner, the goal window, the help links (`openHelp`).
- `menu.ts`: the menus' template from the menu state (Electron's types only).
- `path-grants.ts`: `PathGrants`, the files a window's engine may be given; `settingsRefusal`.
- `path-owners.ts`: `PathOwners`, which window owns a file (decision 0036).
- `renderer-watch.ts`: `RendererWatch`: a window's page that crashed reloaded (three times a minute, then asked), one that hangs asked about; `CLOSE_TAKEN_MS`, how long a page may take to take a close.
- `replace-file.ts`: `replaceFile`, `replaceFileSync`: the one way the main process replaces a file (a temporary file of its own, synced, renamed); `tempName`, `isTempOf`: those files' names (the log's compression streams into one).
- `rescue.ts`: `RescueStore`, `RescueSlots`: the rescue copies (the window's engine writes and opens each one: `document.saveRescue`, `document.open` with `as`).
- `search-folders.ts`: what both cache modules know of the search folders: the engine's names, `isLockFile`, the walk, `errorCode`.
- `security.ts`: the Electron security checklist as data and pure functions.
- `startup-line.ts`: each window's Startup line.
- `state-store.ts`: `StateStore`, `state.json`.
- `updates.ts`: whether a newer ShapeLogic is out (GitHub's latest release); `VERSION_PATTERN`, the app's and the tools' one version pattern.

Files in `web/src/preload/`:
- `index.ts`: exposes `window.bt` and `window.btApp`; keeps the commands that come before the page listens.

Files in `web/src/demo/`:
- `demo-app.ts`: the demo's `window.btApp`: no menus, dialogs or files.
- `engine-bridge.ts`: the demo's `window.bt` over the browser engine; refuses a search.
- `engine/shapelogic_engine.d.ts`: the Emscripten module's types.
- `main.ts`: the demo page's entry (`site/public/demo/`, `just site-demo`): the examples and their played runs.
- `wasm-transport.ts`: the browser engine as a `Transport`.

Files in `web/src/renderer/`:
- `assets.d.ts`, `env.d.ts`: the renderer's ambient types (pictures by URL, Vite's environment).
- `engine-proxy.ts`: `EngineProxy`, the client's interface over `window.bt`.
- `index.html`, `styles.css`: the page and its shared sheet (the mockup's body and style, compact, left alone by Prettier: the tokens, the shared controls, the layout, the densities, the static markup's rules). A rule naming what one module writes is in that module's sheet (`<module>.css`, imported by it, after `styles.css`); a section a module draws is an empty mount point here.
- `main.ts`: the renderer's entry: the stored settings and layouts, then the shell.

Files in `web/src/renderer/state/`:
- `edits.ts`: `Edits`: a mutating call awaited until the page shows its revision; `refuse`, `refuseList`; `#stage[data-busy]`.
- `placements.ts`: the words of a tray's Placements dialog.
- `revision-waits.ts`: `RevisionWaits` and `whenShown`: calls waiting for a view to show a revision, released by a newer document too (DOM-free).
- `solves.ts`: `Solves`, the solve records, one per problem (DOM-free).
- `stacking.ts`: what the stacking screens work out (DOM-free).
- `store.ts`: `Store`, the mirror of the engine: status, info, settings, `Snapshot`, `generation`, `whenRevision`, the documents' replacements, `save`, `wantedSettings`.
- `tilted.ts`: a Tilted Square puzzle's words and `trayPicture`.

Files in `web/src/renderer/shell/`:
- `app.ts`: the shell put together; the status line.
- `cache-events.ts`: the word that cache files were deleted in this window.
- `cache-files.{ts,css}`: the Manage Cache and Autosave Files dialog.
- `cache-notice.{ts,css}`: the notice at the start of what the cache folder holds.
- `cache-text.ts`: the cache files' words (DOM-free).
- `cli-tool.ts`: the Command Line Tool dialog.
- `color-picker.{ts,css}`: the color picker in the popover.
- `command-gate.ts`: document commands held for the first document or while a dialog is open, or dropped once the engine failed.
- `confirm.ts`: a question before an action that cannot be taken back.
- `cursors.ts`: the editing tools' pictures and data-URL cursors.
- `dialog.ts`: modal dialogs (`askChoice`, `tell`, `VIEW_LOGS`, `onDialogsClosed`, `trapTab`); the confirmations and the bulk range dialog are of them.
- `dom.ts`: the DOM helpers (`setHtml`, `markSelected`).
- `engine-failure.ts`: the dialog when the engine cannot start.
- `escape.ts`: Esc's step-back order.
- `files-order.ts`: the order File ▸ New lists the grids in.
- `files.{ts,css}`: `Files`: New, Open, Save, Save As, the window's close, recent files, drops, paused solves, read-only files.
- `help.{ts,css}`: the F1 dialog over the one table, About, the Notes dialog.
- `history.ts`: undo and redo, named by `edit.history`.
- `html.ts`: `esc`, for modules the unit tests run without a DOM.
- `icons.ts`: the line icons.
- `imports.{ts,css}`: `Imports`: File ▸ Import and Export ▸ Puzzlecad, Edit ▸ Convert Brick Grid Type.
- `keyboard.ts`: the keydown handler: an action from the table, handlers per action.
- `keys.ts`: **the one keyboard table** (webui-spec.md section 6).
- `layout.{ts,css}`: the rail, cards, 3D focus, layouts per workspace; `setWorkspaceShown`.
- `log-export.ts`, `log-files.{ts,css}`, `log-rows.ts`: the Log File Manager: its Export choices, the dialog, its rows.
- `menus.ts`: `Menus`: the menu state sent to the main process; commands to their handlers.
- `new-grid-images/`: File ▸ New's example pictures.
- `new-grids.ts`: what File ▸ New says about each grid (`NewKind`).
- `paths.ts`: `baseName`, `puzzleStem`: file names as the dialogs show and propose them (DOM-free).
- `panel.{ts,css}`: a non-modal window over the workspace (status window, debug statistics).
- `persist.ts`: the settings and layouts kept across sessions.
- `picture-keep.ts`: what the picture cache keeps: the hashes and stack keys a snapshot names, the pictures a dialog holds; the rest let go, textures first (DOM-free).
- `picture-math.ts`: a sliding piece's picture to its cells (DOM-free).
- `piece-colors.ts`: the original's `pieceColor` table and jitter; board colors.
- `piece-images.ts`: pictures fetched once per hash and decoded once (decision 0038); `stackPicture`; only what the document names is kept (`keepPicturesOf`).
- `popover.ts`: the popover menu, drawn over the dialogs (a dialog's menu takes the arrow keys, Tab and Esc first).
- `prefs.ts`: the settings as the client holds them: one record per setting (`PREFS`: default, kind, choices or range, the engine's name), from which the type, the defaults, `validPrefs` and the one engine map (`engineSettingsOf`) come.
- `release-notes.ts`: a release's notes as HTML for the update dialog.
- `rescue.ts`: `Rescue`: the rescue copy written and offered.
- `settings.{ts,css}`: the settings dialog.
- `status.ts`: the status bar, flashes, `reportError`.
- `theme.ts`: theme, density, font size, card widths.
- `tooltip.{ts,css}`: tooltips the page draws itself, the settings read at each hover ("Show tooltips", Minimal's captions).
- `topbar.{ts,css}`: the file's name, path and dirty mark; the window's title.
- `tray-border.ts`: a tray's border as the views share it (decision 0046).
- `updates.{ts,css}`: the update check and its dialog.
- `widgets.ts`: the one stepper (`stepperHtml`, `wireSteppers`), switch (`switchHtml`; Space and Enter by `pressSwitch`), rename in place, list keys and keyboard map (`keysHtml`).

Files in `web/src/renderer/view3d/`:
- `board.ts`: a stacking board in 3D (`boardBoxes`, `BoardScene`, `BoardView`).
- `camera.ts`: one orientation quaternion; orbit, pan, zoom, Home and Fit; the arc-ball.
- `depth-order.ts`: `paintingOrder`, the vector image's back-to-front order (DOM-free).
- `extent.ts`: a space's size in mesh units (`gridExtent`), `cellToMesh`, `placedAt`, `isCubeGrid`; held to `engine.info`'s rules by `tests/engine/extent.test.ts`.
- `labels.ts`: the Solver view's piece labels.
- `layer.ts`: `ViewLayer`, one puzzle kind's part of the view (reload, resolution, look, the Puzzle view's drawing), `LayerHost`.
- `material.ts`: the faces' shader: the original's colors and light.
- `mesh.ts`: a `MeshFrame` as three.js geometry through `meshArrays()`; `disposeBuilt`.
- `meshcache.ts`: meshes by shape, style and transformation with their revision.
- `overlays.ts`: axes, bounds, the layer's slab, drawn again only when their options change.
- `picking.ts`: a raycast's triangle to its cell and side.
- `pieces.{ts,css}`: `PiecesScene`, the Puzzle workspace's pieces scene.
- `pointer.ts`: `ViewPointer`: navigation, picking (one ray a pointer event), the edit host's hover and click, Alt-drags of the Solver's pieces; `ViewMods`, `EditHost`.
- `preview.ts`: the 3D edit's preview, made again only when its ghosts change.
- `renderer.ts`: `ViewRenderer`: three.js, the cameras, render on demand, `capture`, `band`, `tilt`, `turn`.
- `scene.ts`: `ShapeScene`: one shape, its passes, marks, edge lines, overlays.
- `shape-meshes.ts`: `ShapeMeshes`, one built copy of each shape mesh, shared and counted; `MeshSlot`, one asker's loads (the shape view, the pieces scene, the trays, the board).
- `sliding-layer.ts`: `SlidingLayer`: a sliding board's trays in the Puzzle view, the walls and borders.
- `solution.ts`: `SolutionScene`: a solution's pieces posed, framed on the result.
- `stacking-layer.ts`: `StackingLayer`: a stacking puzzle's board (the rod set, the Puzzle's stacking, the player's pegs).
- `svg.ts`: the vector image (File ▸ Export ▸ Vector Image).
- `tilted-layer.ts`: `TiltedLayer`: a Tilted Square puzzle's turn, tray, home view and board cells.
- `toolbar.{ts,css}`: the view's toolbar and Display menu.
- `tray-border.ts`: a tray's border in 3D (decision 0046).
- `tray3d.ts`: `TrayScene`, a tilted tray's outline, walls and cells (decision 0045).
- `trays.ts`: `TrayView`, a sliding board's START and GOAL trays in the Puzzle view.
- `view.ts`: `View3D`: the view put together over its layers and pointer (the look worked out once a task); its state as `#vp`'s `data-*`.
- `viewcube.ts`: the view cube, drawn again only when what it shows changed.
- `walls.ts`: a sliding tray's walls in the Solver's view.

Files in `web/src/renderer/entities/`:
- `border-card.{ts,css}`: `BorderCard`, a sliding board's Tray border section (decision 0046).
- `edit3d.ts`: `Edit3D`, 3D editing.
- `editor-card.{ts,css}`: the voxel editor's card: tools, planes, layers, selection, size, palette.
- `editor-state.ts`: `EditorState`; `planeMap`; `expand` (mirror and span as the core makes them).
- `glyphs.ts`: the editor's pictures.
- `grid2d.{ts,css}`: `Grid2D`, one SVG renderer for every grid over a layer drawing; `buildTilted`.
- `image-dialog.{ts,css}`: Apply image (decisions 0038 and 0047).
- `inspector.{ts,css}`: Transform, Fit & scale, Repair & surface: the buttons of `shape.taskSet`.
- `layer-geometry.ts`: `grid.geometry` by grid, size and layer; the cube's XZ and YZ planes.
- `placements-dialog.{ts,css}`: a tilted tray's Placements dialog (decision 0045).
- `rods-card.{ts,css}`: `RodsCard`, a stacking puzzle's rod sets.
- `shape-cells.ts`: `ShapeCells`, the shape's cells as the engine has them.
- `shapes-list.{ts,css}`: the shapes list (the disc list on a stacking puzzle, the board first on a tilted one).
- `status-window.{ts,css}`: `StatusWindow`, the original's status window over `shape.table`.
- `stroke.ts`: `Stroke`, a drag as one undo step.
- `tilt-card.{ts,css}`: a tilted tray's Tilt & tolerance section.
- `tools.ts`: which tool a press uses; the cube's faces.
- `tray-view.{ts,css}`: a tilted board's Tray and Cells views.
- `tray.ts`: `TrayState`, a sliding board as the Start and goal card shows it (DOM-free).
- `workspace.{ts,css}`: `EntitiesWorkspace`, put together; the frozen state.

Files in `web/src/renderer/puzzle/`:
- `counts.ts`: a part's count as the chips write it, `nextCount`, the groups.
- `layout.ts`: `puzLayout`, `arrange`: the pieces scene's items and labels.
- `piece-card.{ts,css}`: `PieceCard`: the selected piece, its group, the color rules.
- `pieces-card.{ts,css}`: `PiecesCard`: the result, the parts, the library, the fit summary.
- `problems.ts`: `ProblemsList`.
- `slide-card.{ts,css}`: `SlideCard`, a sliding problem's Start and goal card.
- `stacks-card.{ts,css}`: `StacksCard`, a stacking problem's board and stacks.
- `workspace.ts`: `PuzzleWorkspace`, put together; the frozen state.

Files in `web/src/renderer/solver/`:
- `autosave.ts`: `Autosave`, the crash recovery journal; `saveAtSavePoint` (decisions 0024, 0034, 0043).
- `boxes.ts`: the Search Options' check boxes in one ordered table (`BOXES`): where each shows, what stores it, its default and setting (DOM-free).
- `cache-links.ts`: tells the main process which puzzle and problem each search folder belongs to.
- `cadence.ts`: when the journal saves (base/save_cadence.h's rule).
- `live-preview.ts`: `LivePreview`, the newest `solve.preview` on the view's preview layer.
- `option-text.ts`: the Search Options' names and help.
- `pager.ts`: `SolutionPager`, `solution.page` in pages of 100 by `solutionsVersion` (DOM-free).
- `performance.ts`: the Performance Analysis of a sliding solve.
- `player.ts`: `Player`, the disassembly player.
- `poses.ts`: `PoseTrack`, a playback's poses in chunks, interpolated (DOM-free).
- `problem-note.ts`: the note beside each problem in the Solver's list.
- `run-card.{ts,css}`: the left card: problems, options, the run.
- `run-questions.ts`: `RunQuestions`: a run's note at its end, Full Search, optimized paths, a search from the start alone, a cache folder that cannot be written.
- `run-text.ts`: a run's words over `RunView`: the tag, the empty scene, the status bar, the counters, Activity and Details (DOM-free).
- `settings.ts`: the options over a problem's stored `SolverSettings`; `defaultsOf`; the solvers' names.
- `solutions-card.{ts,css}`: the right card: the list, the player's controls, analysis, Manage.
- `span.ts`: the time counters as words.
- `stats-panel.ts`: `StatsPanel`, the debug statistics.
- `to-shapes-form.ts`, `to-shapes.ts`: Edit ▸ Convert Assemblies to Pieces.
- `workspace.ts`: `SolverWorkspace`, put together.

Files in `web/src/renderer/explore/`:
- `debug-stats.ts`: the debug panel's report (DOM-free).
- `explorer.{ts,css}`: `Explorer`: the drawer with Placements, Step and Movements.
- `handle-slot.ts`: `HandleSlot`: an assembler handle opened once, closed when its reply comes after the drawer closed (DOM-free).
- `tree.ts`: `MovementTree`, `dragDirection` (DOM-free).

Files in `web/src/renderer/export/`:
- `exports.ts`: `Exports`: Image, Vector Image, STL, Solution Animation; `#stage[data-exported]`.
- `form.ts`: the dialogs' numbers.
- `glb.ts`: `animationGlb`, the solution animation as binary glTF (DOM-free).
- `shape-picker.{ts,css}`: the lists the exports pick shapes in.
- `stl-dialogs.{ts,css}`, `stl-form.ts`, `stl-preview.ts`: the STL dialogs, their words and fields (DOM-free), the preview.

Files in `web/src/renderer/play/`:
- `board.{ts,css}`: a sliding board seen from above as SVG (decision 0039).
- `board-shell.ts`: `BoardShell`, what both boards share (the frame's resize, the pointer, a drag's end, a move's frame loop `animateFor`), `translate`, and `toggleButton` for the buttons beside them.
- `follow.ts`: how a dragged piece follows the pointer (DOM-free).
- `game.ts`: `GameOf`, a game's moves and place in them (DOM-free).
- `goal-picture.ts`, `goal-places.ts`: the goal's picture for its own window, and what it is told.
- `kind.ts`: `PlayKind`, what the workspace asks of a kind of puzzle.
- `moves-file.ts`: the `.slmoves` file of a sliding game.
- `outline.ts`: a set of cells' outline as one SVG path.
- `room.ts`: where a board stands in its frame.
- `sliding.ts`, `stacking.{ts,css}`: the two kinds (decisions 0039 and 0042).
- `stack-board.{ts,css}`, `stack-game.ts`, `stack-look.ts`, `stack-moves-file.ts`: a stacking game's board, moves, drawing (DOM-free) and `.slmoves` file.
- `workspace.{ts,css}`: the Play workspace (shown only for a sliding or stacking puzzle).

Rules (AGENTS.md's; decisions 0012, 0013, 0034, 0036, 0043):
- The engine is the truth; the client computes only what the engine cannot
  be asked for in time (the cube planes' squares, the cube's faces, the
  preview's mirror and span cells, a space's size and its cells' spacing:
  `view3d/extent.ts`), each checked against the engine by a test. Every
  mutating call waits for its revision (a new document releases the waits);
  an id kept across an await checks `Store.generation` again (ids repeat).
- `src/engine/` is platform-free (Node only in `node-transport.ts`). The
  renderer is sandboxed and reaches only `window.bt` and `window.btApp`.
- Decision 0011's frozen parts are refused before any call ("Pause the
  solve first"). While anything solves (`Snapshot.solving`): every shape
  edit, a problem's delete or move, undo and redo; a new or duplicated
  problem still goes in at the end. A problem's own edits only while it is
  itself solved or analysed.
- Undo depth, threads, search memory and the searches' disk space are the
  engine's settings (`Store.wantedSettings` on every new engine); the rest
  are the client's. The main process gives each engine its folders before
  the page may call it, and refuses a page's `settings.set` naming one.
- A path reaches an engine only from a dialog, the rescue record or copy, a drop,
  Open Recent or the system (`PathGrants`); an export is written by the main process only
  to a path its save dialog gave that window for that kind, once; it replaces any file through `replace-file.ts`.
- One engine, rescue slot and goal window per window; the first window to
  hold a file owns it, the others hold it read-only (0036).
- The version is the git tag's (`tools/project-version.ts` and
  `cmake/ProjectVersion.cmake`, one rule; `__BT_VERSION__`): the app's,
  packaging's, and the one `NodeTransport.start` requires of the engine;
  `dist.test.ts` holds it to the tag and to the release engine's
  `--version` (`package.json`'s stays 0.0.0). A packaged app runs only
  the engine in its resources (on Windows linked statically, on Linux with
  libstdc++ and libgcc linked in: `engine/CMakeLists.txt`).
- Tests (Vitest): `tests/unit/` (no engine), `tests/engine/`, `tests/e2e/`
  (Playwright on out/ or BT_OUT_DIR), on a run's copy of the engine
  (`tests/helpers/engine-path.ts`); after an engine change, `npm run record`.

## Testing against the original

`tests/oracle/` runs the original programs (decision 0007;
`tests/oracle/README.md` has the commands and tiers):
- `shapelogic_cli` (the harness's `bt`) against `burrTxt`: the summary
  (`just oracle`; `--fields`, `--threads`, `--solver crowell`), the text
  byte for byte (`just oracle-text`), the solved files solution by solution
  (`compare-files`, read by `bt_solutions`), saved positions carried on in
  the other program (`just oracle-resume`), and the speed
  (`tests/bench/bench.py`: within 5% or faster). Beside it: every file
  loaded and saved by both (`just oracle-roundtrip`), the imports, Puzzlecad
  export and conversion (`bt_imports`, `oracle.py imports`), and the Python
  module's transcript against the original module's, line for line
  (`oracle.py python`).
- The `sliding` and `stacking` tiers use puzzles built by the original's
  code and its solve thread (`slide_solve_old`, `stack_solve_old`); a
  puzzle whose count depends on the order of its starts is compared on one
  thread (flag `O`); each stacking run has a Panex folder of its own, never
  the user's cache. `panex-resume` and `blockpack-check` compare saved
  searches and packing. Flag `D` marks the grids only the new core takes
  apart (assemblies compared, solutions asked for).
- The recorders (`grid-dump`, `model-dump`, `app-dump`, `stl-dump`,
  `record-saves`, `record-positions`, `record-analysed`,
  `record-slide-routes`, `record-imports`, `stl-solution`) compile a dump tool
  against the original's library and write into `tests/data/`;
  `v2_movement_checks.py` records BurrTools V2's movement checks (0025). The
  unit tests compare with those recordings and never read the original repo.

## Rules that hold everywhere

AGENTS.md has the full list; the load-bearing ones:
1. Cancellation only through `StopToken`; nothing resets a stop request.
2. A `SolveStateCell` has one writer at a time (base's rules).
3. Recoverable errors are `Result<T>`; broken invariants are `BT_ASSERT`.
4. No owning raw pointers; value types and `unique_ptr`.
5. No global state or environment switches in `core/`.
6. Ported code matches the original on the harness before it is reshaped.
7. File compatibility is one way: everything the original wrote opens.

## Build and test

The recipes are AGENTS.md's. ctest runs the unit shards, the `[slow]` cases
(one test each), `api-schema` (the schema checks itself), `api-docs`
(`docs/API.md` is current), `architecture-map` (this file's map), `contract`,
`cli` and, when the module is built, `python`. Presets: `CMakePresets.json`;
strict warnings are errors in `dev` and `release` (and `release-x64`,
`release-arm64`: one Mac architecture each, for the packaged app); macOS
builds target 13.0 (`base`: Electron 44's lowest), so a newer library call
fails to compile. Dependencies (`cmake/Dependencies.cmake`): zlib from the
system or a pinned release; zstd 1.5.7 (pinned, built from source as a static
library, one thread a call); nlohmann/json 3.12.0 (the engine only); pybind11
3.0.0 (the Python module only); Manifold 3.5.2 (built optimized, without its
cross sections or parallel backend, as the original builds it; uninstrumented under ASan, decision 0016); Catch2.

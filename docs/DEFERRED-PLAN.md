# Plan: the deferred features

## Goal
Finish what docs/PLAN.md put under "Later" and docs/FRONTEND-PLAN.md under
"Later": **sliding puzzles, stacking/Panex puzzles, the real STL meshes and
exports, the imports (PuzzleSolver3D, Puzzlecad), Puzzlecad export, grid
conversion, and Python bindings**. Each feature lands **complete**: core
port, engine methods, front end, tests, all with the same discipline as
the core and web stages. After the last part, BurrTools Alpha does
everything the original BurrTools does, apart from what is deliberately
dropped (end of this document).

## Where things stand (2026-10-10)
- Core stages 0 to 8 are done (docs/PLAN.md): every brick grid, all three
  assemblers, disassembly with rotations, the solve session, the app layer,
  the engine (`bt-engine`, JSON-RPC, `api/*.json`, docs/API.md) and the
  `bt` CLI. They match the original on the whole corpus and are as fast
  or faster.
- Front-end stages 1a to 8 are done (docs/FRONTEND-PLAN.md): Electron +
  TypeScript + three.js in `web/`, packaging, and the security audit.
- **What is still missing, and how it shows today:**
  - Sliding (grid 5) and stacking (grid 6) files **open, edit as plain
    shapes, and save without loss** (decision 0006). Rod sets are kept as
    preserved XML; `Problem` has the sliding goal shape. `problem.kind`
    reports `supported: false`, `solve.*` answers `unsupported`, and `bt`
    refuses them with burrTxt's message. The front end shows them with the
    solver disabled.
  - **STL**: File ▸ Export ▸ STL writes the shape's flat mesh, labelled
    "interim" in the dialog. Solutions ▸ "Export to STL" is disabled. The
    "STL-like" render style is missing.
  - **Imports, Puzzlecad export, Convert grid type**: greyed out in the
    menus.
  - **Python bindings**: none.
- The GLB solution animation is **done** (the client builds it from
  `playback.track`, web-7.md). The original's `gltfexport.cpp` is not
  ported.

## Decision: seven parts, seven sessions, run one after another

**Not all at once.** The deferred work is about 12,500 lines of the
original's core plus a front end for each feature. That is more than any
earlier stage, with very different risks:
- the disk-backed, resumable, multi-threaded Panex search needs extra-high
  effort and an audit;
- the imports are small parsers.

One session cannot hold it all in context. A failure in one feature would
also block the others, and the audit points would be lost.

**Split by feature, and within the two big features by layer**, so each
session has one kind of difficulty and ends with a usable, committed,
gated increment:

| Part | Work | Original code | Model, effort | Audit after | Gate (summary) |
|---|---|---|---|---|---|
| 1 | Sliding: core, engine, CLI | `sliding.*` 3.1k lines, plus their parts in `solvethread`, `disasmtomoves`, `puzzle`, `problem` | Opus 5.5, extra high | **Yes** (Fable 5.1, extra high) | oracle sliding tier equal for the three solvers and nested slides; ported `test_sliding.cpp`; routes equal the original's; TSan |
| 2 | Sliding: front end | GUI: tray edit, walls, start/goal, locks, solver options, player with routes | Opus 5.5, high | No | e2e: build the sliding fixture in the UI, solve, play; equals `bt --json` |
| 3 | Stacking and Panex: core, engine, CLI | `stacking.*`, `panex.*`, `blockpack.*` 3.5k lines, their parts in `solvethread`, `paused_solve`, `puzzle`, `problem` | Opus 5.5, extra high | **Yes** (Fable 5.1, extra high) | oracle stacking tier equal for both solvers; Panex pause/continue and saved searches, compatible with the original; ported `test_stacking.cpp`; TSan |
| 4 | Stacking and Panex: front end | GUI: rod sets, discs, start/goal rods, Panex columns, pocket, player of transfers | Opus 5.5, high | No | e2e: build Hanoi and a Panex swap in the UI, solve, play |
| 5 | STL meshes and exports | `stl*`, `cubemesh`, `cubepoly`, `minkmesh` (Manifold), `millable` 2.3k lines; GUI `stlexport*` | Opus 5.5, high (extra high for `minkmesh`) | No | meshes equal dumps from the original; closed and printable; both STL dialogs; STL-like style |
| 6 | Imports, Puzzlecad export, grid conversion | `ps3dloader`, `scadloader`, `converter` 1k lines; GUI `convertwindow` | Opus 5.5, high | No | imported files equal the original's import; conversions equal the original's |
| 7 | Python bindings | `src/python` 0.9k lines, `test/python` | Opus 5.5, medium (Sonnet 5.5 at high is acceptable) | No | the original's Python tests ported and passing; wheel builds |

**Order.**
- Sliding comes before stacking. Both add a tray-like editor and a
  move-sequence player, and sliding sets the pattern stacking follows.
- Each feature's core part comes before its front end, because the
  front end needs the engine methods.
- Parts 5 to 7 are independent of 1 to 4 and of each other, and could go
  first or run in parallel in separate git worktrees. **Run them in
  sequence anyway:** every part edits `api/*.json`, the generated
  TypeScript types, `engine.info`, the menus and the CMake lists, and
  parallel branches would conflict there. Python comes last, so it can
  bind the sliding and stacking API too.
- Each part is one fresh session. If a part runs long, it may stop at a
  clean point with a handoff note (e.g. 3a: stacking + stack search,
  3b: Panex + blockpack + saved searches). Part 3 was split by layer
  instead (3a the core and `bt`, 3b the document's commands and the
  engine): the original's Stacking Solver runs on the Panex search, so
  the two searches go together (docs/handoff/deferred-3a.md).

## Rules every part follows
Read first, in this order:
1. AGENTS.md (C++ rules and finishing steps, the front-end section).
2. docs/ARCHITECTURE.md.
3. This plan.
4. The previous part's handoff (`docs/handoff/deferred-N.md`; for part 1,
   web-8.md and stage-8.md).
5. docs/API.md and decision 0012 (the protocol).
6. api/README.md ("Adding a method").
7. Decisions 0006 (file compatibility) and 0007 (port against the
   original).
8. For front-end parts: docs/FRONTEND-PLAN.md ("Finishing a front-end
   stage", "Rules for the front end") and decision 0013.

- **Port faithfully, then reshape** (decision 0007). Each ported file names
  its source at the top. The original's tests for the area are ported
  first, then new ones added.
- **The original is the oracle.** Extend `tests/oracle/` for the feature:
  - a corpus tier;
  - baselines recorded from the original `burrTxt --json`;
  - where the summary is not enough, a dump tool built against the
    original (`tests/oracle/*_old.cpp` through `build_against_old`, as
    `app_dump_old.cpp` does) whose output is committed in `tests/data/`.

  The bundled sliding and stacking files are few. **Generate more**: a
  dump tool that builds the puzzles of the original's `test_sliding.cpp`
  and `test_stacking.cpp` with the original's code and saves them under
  `tests/data/puzzles/` gives the corpus breadth.
- **Never use the puzzle "One Way or Another"** (copyrighted). For sliding
  work, use `test/test_sliding_solver.xmpuzzle` (81 moves), its copy
  `examples/SliderByGiiker.xmpuzzle`, `test/test_sliding_nested.xmpuzzle`,
  and generated puzzles.
- **The architecture's rules hold:**
  - cancellation only through `StopToken` (decision 0004);
  - one solve session per problem (decision 0010); sliding and stacking
    runs go through `SolveSession`, not a side path;
  - edits are undoable and refused while solving (decision 0011);
  - no global state or environment switches in `core/` (the original's
    `BURRTOOLS_PANEX_DIR` and `BURRTOOLS_PANEX_DISK_GB` become options set
    by the engine, the CLI and the settings);
  - `core/` never prints.
- **Engine additions**:
  - each in its own commit, `Engine: <method>`, following api/README.md
    (schema, handler, contract call, `python3 api/gen_api_md.py`, then
    `just web-gen` and the staleness test);
  - additive where possible; a breaking change bumps nothing silently and
    is named in the handoff;
  - core changes outside the feature in their own commits (`Core: …`).
- **Remove the "not yet" notes** a part makes untrue, in the same part:
  `engine.info.grids[].supported`, `problem.kind.supported`, the
  descriptions in `api/*.json` ("sliding and stacking: not yet"), `bt`'s
  refusal, the front end's disabled solver and notices, greyed menu items,
  the STL dialog's "interim" words, README and ARCHITECTURE status lines,
  `grid_type.h`'s "(not ported yet)".
- **New decisions** get records (`docs/decisions/0014-…` onward), e.g. the
  sliding model in the core, the Panex search store, the STL mesh
  parameters in the API.
- **Front-end design**: the mockup has no sliding or stacking screens. The
  original GUI decides (FRONTEND-PLAN.md's rule), fitted into the mockup's
  workspaces and visual language. Each front-end part first writes its
  screens into docs/webui-spec.md (a new section) and only then builds
  them.

### Finishing a part
In this order:
1. **C++ gates**, as AGENTS.md lists them: `just test-all`,
   `just test-asan`, `just test-tsan` (solver parts), the oracle tiers the
   part touches plus `just oracle --tier fast` (nothing else moved),
   `just oracle-roundtrip`, `just check` if LLVM is installed.
2. **Front-end gates**, three times each: `just web-check`, `just web-test`
   with the release engine built, and `just web-e2e`. Run
   `just web-dist --dir` and `just web-smoke` when packaging could be
   affected (new resources, a new dependency).
3. ARCHITECTURE.md and docs/API.md updated, and webui-spec.md for front-end
   parts.
4. `docs/handoff/deferred-N.md`: what was done, gate results, differences
   from the original and why, known issues, and the brief for the next
   part. For audited parts, an "Audit" section added by the audit session.
5. This plan's status ticked.
6. One commit for the part (`Deferred N: <summary>`), after its `Engine:`
   and `Core:` commits.
7. Tell the user to open a new session, which model and effort to use, and
   the exact prompt (the template at the end, filled in).

## Status
- [x] **Part 1**: sliding: core, engine, CLI (2026-10-10; docs/handoff/deferred-1.md)
- [x] Audit of part 1 (2026-10-10; the Audit section of docs/handoff/deferred-1.md)
- [x] **Part 2**: sliding: front end (2026-10-10; docs/handoff/deferred-2.md)
- [x] **Part 3**: stacking and Panex: core, engine, CLI
  - [x] 3a: the core and `bt` (the model, the stacking and Panex searches,
    blockpack, stacking solves, the paused-solve file's saved searches,
    the playback in transfers; the oracle's stacking tier; 2026-10-10;
    docs/handoff/deferred-3a.md, decision 0015)
  - [x] 3b: the document's stacking commands and the engine (rod sets,
    discs and stacks, undoable; `stacking.*`; stacking solves in the
    engine with their saved searches in its Panex folder; 2026-10-11;
    docs/handoff/deferred-3b.md, decision 0015 amended)
- [x] Audit of part 3 (3a and 3b together) (2026-10-11; the Audit section of
  docs/handoff/deferred-3b.md: three findings fixed)
- [x] **Part 4**: stacking and Panex: front end (2026-10-11; docs/handoff/deferred-4.md)
- [x] **Part 5**: STL meshes and exports (2026-10-12; docs/handoff/deferred-5.md, decision 0016)
- [ ] **Part 6**: imports, Puzzlecad export, grid conversion
- [ ] **Part 7**: Python bindings

## Part details

### Part 1: sliding, core, engine and CLI
**Port** (original `src/lib/`):
- `sliding.{h,cpp}`:
  - the tray and start/goal shapes (`isStartGoalShape`,
    `addStartGoalShape`, `ensureSetup`, user names);
  - stamps (`stampPiece`, `stampFits`, `clearStamp`, `stampAt`,
    `dropChangedStamps`);
  - start and goal placement and start locks (`placeStart`, `placeGoal`,
    `toggleGoal`, `refreshStartLocks`, `clearStartAt`);
  - walls (`toggleCellMark`), `syncSlidingProblems`, `syncMaxHoles`,
    `startGoalError`, `stampOverflowMessage`, `flattenPieces`,
    `floorCells`, `pieceColor`;
  - the search: `slideSearch_c`, `findSlidePath`, outcomes, memory budget
    `memoryStates`, the fast / deep / full limits, nested and partially
    nested slides;
  - routes: `slideRoute`, `applySlideRoutes`, `finalPlacementKey`.
- The sliding parts of `solvethread.cpp` (its sliding run, start layouts,
  `slidingSummary`).
- `problem.cpp` and `puzzle.cpp`: what is not yet modelled (sliding
  options, start/goal data) instead of preserved XML. Keep byte-identical
  saves (`just oracle-roundtrip` stays 100%).
- `disasmtomoves.cpp`: routes in the playback sampler (`app/playback`).
- The original's `burrTxt` sliding options: `--solver` fast / deep / full,
  `--nested-slides`, `--high-memory`.

**Engine:**
- `problem.kind` sliding `supported: true`;
- `solve.*` accepts sliding problems with sliding options in
  `SolveOptions` (solver choice, nested, partially nested, high memory);
- methods for the editing actions above (a `sliding.*` namespace, or
  `problem.*` methods: decide and record it in a decision);
- `playback.*` follow routes;
- `engine.info` lists the sliding solvers.

**Harness:**
- tier `sliding` in `corpus.txt`: the fixtures above plus generated
  puzzles, with flags for solver and nested;
- `bt --json` equal to `burrTxt --json` for each solver;
- file comparison (`oracle-files`) for the solved files;
- a route dump from the original for the playback.

**Tests:** port `test/test_sliding.cpp` (1,371 lines) in full;
`[parallel]` cases for the sliding search on several threads (decision
0004 stops, multiset comparison).

**Gate:** all of the above equal the original; the contract suite drives a
sliding solve, pause and continue, and playback; TSan clean.

### Part 2: sliding, front end
- **Inventory the original GUI first:**
  - the `isSliding` branches of `mainwindow.cpp`, `mainwindow_layout.cpp`,
    `mainwindow_solve.cpp`, `grideditor.cpp`, `tooltabs.cpp`,
    `BlockList.cpp`;
  - `slidingcolors.h`, `voxelframe.cpp`'s `buildSlidingWalls`;
  - the labels: "Tray edit", "Walls", "Start/Goal", "New Start/Goal
    Positions", "Delete Start/Goal Positions", "Locked", "Allow Nested
    Slides", "Allow Partially Nested", "Enable High Memory", the three
    sliding solvers, "Sliding search, N moves deep.", start/goal
    validity messages.

  Write the screens into webui-spec.md: where tray, walls and start/goal
  live in the Entities and Puzzle workspaces, and the Solver's sliding
  options.
- **Build:**
  - the tray editor (walls and floor on the 2D grid and in 3D);
  - stamping pieces onto start and goal, locks, validity messages;
  - the sliding solver options; "Enable High Memory" (FRONTEND-PLAN.md
    left it waiting for this);
  - the player following routes;
  - the walls drawn in the 3D view;
  - sliding files no longer show the "cannot solve" notice.

**Gate:**
- e2e: build `test_sliding_solver.xmpuzzle`'s puzzle from a new sliding
  document through the UI, and `problem.get` equals the fixture's;
- solve with each solver: counts and moves equal `bt --json`;
- play: poses equal `playback.poses` at integer steps, routes followed;
- nested fixture with and without nested slides;
- reference screenshots of the tray and the walls.

### Part 3: stacking and Panex, core, engine and CLI
**Port:**
- `stacking.{h,cpp}`:
  - rod sets (`rodSet_c`, pockets, outside channel, Panex columns, rod
    names, capacity, height);
  - discs (`generateDisk`, `addDisk`, `ensureHotspot`);
  - stacks and maps (`placeDisk`, `liftTop`, `moveDisk`, `dropInstance`,
    `trimStacks`, `syncMaps`, `setupError`, `layoutBoard`);
  - rod sets in the file (`saveRodSets`, `loadRodSets`, problem
    load/save: the preserved XML becomes a model, with byte-identical
    saves);
  - the stack search (`findStackPath`, `stackSearch_c`, `searchInput`,
    `pathSeparation`, `logicalMoves`, `startAssembly`, `boardSpan`).
- `panex.{h,cpp}`: the Panex solver, its disk-backed resumable search,
  the saved-search folder (`savedSearch`, `savedMs`, `searchFolder`,
  `discardSaved`), threads, the disk budget.
- `blockpack.{h,cpp}`: pack and unpack, byte-compatible.
- The stacking parts of `solvethread.cpp` (`runStacking`) and of the
  paused-solve file (BTSOLVE2's `searchz/<name>` sections, decision 0006):
  a stacking search paused in one program continues in the other.
- The original `burrTxt` options: `--solver` stacking / panex,
  `--panex-dir`, `--panex-disk`, `--restart`. These are options here, not
  environment variables.

**Engine:**
- `solve.*` for stacking, with Panex's save folder in the engine's
  settings (default: the app's data directory);
- methods for rod sets and disc placement (a `stacking.*` namespace);
- `playback.*` for transfers (lift, cross, drop; the slider counts
  transfers, as the original's `logicalMoves`);
- `solve.exportPaused` and `importPaused` with stacking searches.

**Harness:**
- tier `stacking`: `test_stacking_hanoi.xmpuzzle`, `PanexLevel3to10`,
  the generated Panex swaps (3, 4, 5 discs: 42, 128, 343 transfers) and
  Panex Jr (143), both solvers;
- a resume check (pause the Panex search in one program, continue in the
  other, as `oracle-resume` does for assemblers);
- blockpack round trips against the original's packer.

**Tests:** port `test/test_stacking.cpp` (956 lines) with its per-process
scratch folders. Never a fixed temp name, never the user's cache: the
cause of the CI failures of 2026-10-05.

**Gate:** all equal the original; random pause/continue points in the
Panex search (as the assembler sweep); TSan clean; the contract suite runs
a Panex solve with a pause, a saved search and a continue.

### Part 4: stacking and Panex, front end
- **Inventory the original GUI:**
  - `mainwindow_rods.cpp`, `rodbars.h`, `voxelframe.cpp`'s
    `showStackingAssembly`;
  - the `isStacking` branches;
  - the labels: "Rods", "Disc", "How many rods", "Defined height", "Grow
    to the height of all pieces", "Disc size matters - no large on
    small", "Disc can only move over 1 rod", "Panex Style Columns", "Add
    pocket column", "Set Start/Goal Rods", rod set names, "Stacking
    Solver", "Panex Solver", "The 3D view shows this disc".

  Write the screens into webui-spec.md.
- **Build:**
  - the rod-set editor;
  - disc shapes;
  - placing discs on start and goal rods (with the original's rules and
    messages);
  - the stacking view in 3D (plates, rods, the START/GOAL trays);
  - the Panex options and the saved-search notice ("You have to start
    from the beginning", "Discard It", "Start Over");
  - the player in transfers.

**Gate:**
- e2e: Hanoi and a 3-disc Panex swap built from a new stacking document,
  and `problem.get` equals the fixtures';
- solve with both solvers: equal `bt --json`;
- a Panex solve paused, the app restarted, continued from the saved
  search;
- the player steps in transfers;
- reference screenshots.

### Part 5: STL meshes and exports
**Port:**
- `stl.{h,cpp}`, `stl_0` (cubes: unit size, bevel, offset, and the rest
  of its six parameters with their tooltips) and `stl_2` (spheres);
- `cubemesh`, `cubepoly`, `minkmesh` (Minkowski meshes for prisms,
  rhombic and tetra-octa, with **Manifold**: add it to
  `cmake/Dependencies.cmake` at the original's version 3.5.2, pinned),
  `millable`, `getSTLMesh` and the half-edge pieces they need;
- `mergeCoplanarFaces` if not yet ported;
- binary and text STL writing.

Port `test_cubemesh.cpp`, `test_minkmesh.cpp` (including the random-shapes
stress case as `[slow]`) and `test_manifold_smoke.cpp`.

**Engine:** an `stl` style for `shape.mesh` (the original's "Draw pieces
like STL export"), `export.stlParameters {grid}` (names, types, defaults,
tooltips), and `export.stl {shape, parameters, binary}` returning the
bytes (or writing a granted path, as the front end's export grants
require).

**Front end:**
- the STL dialog with the grid's parameters, the "interim" words gone;
- Solutions ▸ "Export to STL" enabled (the original's Export Solution
  Pieces to STL: one file per distinct piece placed in the solution, into
  a chosen folder, names made safe and distinct; see the original's
  `stlexportsolution.cpp`);
- the STL-like render style in the View menu and the Display options.

**Gate:**
- the mesh of every fixture shape with default parameters equals a dump
  from the original (vertex positions within 1e-5, same triangle count);
- every exported STL is a closed, consistently oriented surface (a test);
- the solution export's files and names equal the original's for
  CubeInCage;
- e2e of both dialogs; reference screenshots of the STL-like style.

### Part 6: imports, Puzzlecad export, grid conversion
**Port:**
- `ps3dloader` (PuzzleSolver3D files);
- `scadloader` (Puzzlecad `.scad`: load and `saveOpenScadPuzzle`; cube
  grid only, as the original's message says);
- `converter` (Convert brick grid type to other: the target grids it
  offers, as `convertwindow.cpp` presents them).

**Engine:** `document.import {format, path|bytes}` (a new document, dirty
and untitled), `document.exportPuzzlecad {problem}` (text), and
`document.convertGrid {target}` (undoable, or a new document as the
original does: follow the original).

**Front end:** File ▸ Import ▸ PuzzleSolver3D… and Puzzlecad…, File ▸
Export ▸ Puzzlecad…, Edit ▸ Convert grid type… (with the original's
dialog choices); the greyed items enabled.

**Gate:**
- each import of the original's test inputs (find them in its tests and
  examples; make small ones with the original if none exist) gives a
  document whose save equals the original's import followed by its save;
- Puzzlecad export text equals the original's for every cube-grid
  fixture;
- every conversion the original offers gives the same shapes as the
  original's;
- e2e of the menu items.

### Part 7: Python bindings
- A `python/` module (pybind11 through FetchContent, built when
  `BT_PYTHON=ON` or when Python and pybind11 are found, as the original's
  `python` option did) binding the **new core's** public API with the
  original module's names, so existing scripts keep working:
  - classes `Puzzle`, `Shape`, `Problem`, `Solution`, `Placement`,
    `SolutionIterator`;
  - `load`, `save`, `add_shape`, `add_problem`, the shape cell accessors
    (`get`, `set`, `get_state`, `set_state`, `get_color`, `set_color`,
    `is_empty` / `is_filled` / `is_variable`, `count_filled`,
    `count_empty`, `fill`, `size_x` / `y` / `z`, `dimensions`), the
    problem accessors (`set_result`, `result_shape_index`,
    `set_piece_count`, `set_piece_range`, `get_piece_min` / `max`,
    `num_pieces`), `solve` with a solution iterator (`is_finished`,
    `iterations`, `stop`);
  - sliding and stacking where the core exposes them.
- Port `test/python/test_burrtools.py` (8 tests) and add tests for what is
  new. A `just python-test` recipe; a wheel build documented in the
  module's README.

**Gate:** the ported tests pass; the module imports from a fresh venv after
`pip install` of the built wheel; `just test-all` runs the Python tests
when the module is built.

## Deliberately not done
- **Lua scripting**: dropped (PLAN.md).
- **`burrgrower.cpp`**: not referenced anywhere in the original (dead
  code there too).
- **The core port of `gltfexport.cpp`**: the front end builds the GLB
  (web-7.md).
- **OpenGL display lists**, and the original's FLTK-only settings.
- Release work (signing, notarisation, the Windows and Linux builds by
  hand, version tags): web-8.md's "Brief for the first release" stands and
  is not part of this plan. After part 7, a release note for the
  next version names the features added here.

## Prompt template for a part's session
> Do part N of docs/DEFERRED-PLAN.md in
> /Users/colin/git/burr-tools-rotations/burrtools_alpha (the original
> BurrTools is ../burrtools_rotations). First read AGENTS.md,
> docs/ARCHITECTURE.md, docs/DEFERRED-PLAN.md, the previous handoff
> (docs/handoff/deferred-<N-1>.md, or web-8.md and stage-8.md for part 1),
> docs/API.md, api/README.md and the decisions the plan names. Follow
> "Rules every part follows" and "Finishing a part" exactly: port
> faithfully, extend the oracle harness, add engine methods in their own
> commits, remove the "not yet" notes you make untrue, write
> docs/handoff/deferred-N.md, tick the plan, commit. If the session runs
> long, stop at a clean point with a handoff note. When you finish, tell
> me to open a new session, which model and effort to use, and the exact
> prompt for the next step (the audit, if the plan asks for one, or the
> next part).

For an audit session:
> Audit part N of docs/DEFERRED-PLAN.md in
> /Users/colin/git/burr-tools-rotations/burrtools_alpha with Fable 5.1 at extra high effort: the
> diff of the part's commits, its gate results, docs/handoff/deferred-N.md
> and the plan. Probe every suspicion before calling it a finding; report
> findings by severity with concrete failure scenarios; fix them in a
> commit "Deferred N audit: …" with a test for each (each test must fail
> without its fix); add an "Audit" section to docs/handoff/deferred-N.md;
> tick the plan; then tell me the next session's model, effort and prompt.

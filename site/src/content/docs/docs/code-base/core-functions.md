---
title: Where the Code Lives
description: "A map from each feature of ShapeLogic to the files, classes and functions that implement it, in the core, the engine and the app."
---

This page answers "where is the code for X?". Each table names the files
and the main types or functions of one area. Paths in the core are under
`core/shapelogic/`; paths in the app are under `web/src/`. The full
account, with the rules each part keeps, is `docs/ARCHITECTURE.md` in the
repository.

## Cancellation, state and threads (`base/`)

| Concern | Where | Names |
|---|---|---|
| Stopping long work | `base/stop.h` | `StopSource`, `StopToken`, `StopKind` (Pause or Abort); one source per run, never reset; `child()` for a stage-level stop |
| The state of a solve | `base/solve_state.h` | `SolveState` (Idle, Preparing, Assembling, Draining, Paused, Finished, Error), `SolveStateCell`, `isActive()` |
| Recoverable errors | `base/error.h` | `Errc`, `Error`, `Result<T>` (a `std::expected`), `fail()` |
| Broken invariants | `base/assert.h` | `BT_ASSERT`, on in every build |
| Threads of a solve | `base/thread_pool.h` | `ThreadPool` (`run`, `addLoad`), `kMaxThreads`, `hardwareThreads()`, `splitThreads()` |

## Grids, voxels and symmetry (`grid/`)

| Concern | Where | Names |
|---|---|---|
| The kinds of grid | `grid/grid_type.h` | `GridKind`, `GridType` (capabilities, `getSymmetries()`, `makeVoxel()`) |
| A shape's cells | `grid/voxel.h` | `Voxel`: cells with state and colour, bounding box, hotspot, `translate`, `resize`, `minimizePiece`, `fillHoles`, `scale`, connectivity; `warmCaches()` before threads read it |
| One class per grid | `grid/voxel_bricks.h`, `voxel_prisms.h`, `voxel_spheres.h`, `voxel_rhombic.h`, `voxel_tetra_octa.h` | the original's `voxel_0` to `voxel_4` |
| Rotations and symmetry groups | `grid/symmetries.h`, `symmetries_impl.h`, `symmetries_2.h` | `Symmetries` (`transAdd`, `minimizeTransformation`, `calculateSymmetry`); the tables in `tabs_0/`, `tabs_1/`, `tabs_2/` (`grid/TABLES.md` says where they come from) |
| Finding a shape in any orientation | `grid/voxel_table.h` | `VoxelTable` |

## The puzzle model (`model/`)

| Concern | Where | Names |
|---|---|---|
| A puzzle | `model/puzzle.h` | `Puzzle`: grid, shapes, colours, problems, comment, rod sets |
| A problem | `model/problem.h` | `Problem`: parts (`Part`: shape, min, max, group), result, colour rules, holes, `ProblemState`, counts, the saved search position, pending assemblies, saved solutions, `solutionSortOrder()` |
| An assembly | `model/assembly.h` | `Assembly`, `Placement`, `transform`, `smallerRotationExists`, `createSpace` |
| A take-apart's result | `model/disassembly.h` | `Disassembly` (levels and moves), `Separation` (the tree), `SeparationInfo`, `movesText()` |
| A solution | `model/solution.h` | `Solution`: an assembly with an optional separation, its numbers and runtime id |
| Sliding and stacking | `model/sliding.h`, `model/stacking.h`, `model/stacking_types.h` | start/goal shapes and stamps; rod sets, discs and stacks |
| Grid conversion | `model/grid_convert.h` | `convertGrid`, `convertTargets` |

## Files (`io/`)

| Concern | Where | Names |
|---|---|---|
| `.xmpuzzle` | `io/puzzle_file.h` | `loadPuzzle`, `parsePuzzle`, `savePuzzle`, `writePuzzle`; saves are byte-identical to the original's |
| XML | `io/xml_pull_parser.h`, `xml_document.h`, `xml_writer.h` | the original's parser, the tree, `XmlWriter` |
| Compression | `io/gzip_file.h` | whole-file and in-memory gzip through zlib |
| Paused solves (`.slsolve`) | `io/paused_solve.h` | the puzzle, the paused problem and a stacking search's saved levels, streamed |
| Imports | `io/imports.h`, `ps3d_file.cpp`, `scad_file.cpp` | `readPuzzleSolver3D`, `readPuzzlecad`, `writePuzzlecad` |

## The assembly search (`solve/`)

| Concern | Where | Names |
|---|---|---|
| The interface | `solve/assembler.h` | `Assembler`: `createMatrix(MatrixOptions)`, `reduce(stop)`, `assemble(callback, SearchOptions, stop) -> RunEnd`, `savePosition()`, `restorePosition()`; the problem hooks `beginSolving()`, `storePosition()`, `canStartSolving()` |
| Choosing the assembler | `solve/solver_type.h` | `SolverType` (Classic, Crowell), `chooseAssembler()`, `makeAssembler()`: assembler 0 when every part is exactly one piece, else assembler 1 |
| Dancing links | `solve/assembler_0.h`, `solve/dlx_matrix.h` | `Assembler0`, `DlxMatrix`, `DlxNodes`; plain, split and vector searches |
| Cover with counts | `solve/assembler_1.h` | `Assembler1` (Wei-Hwa Huang's cover: piece ranges, holes), `SearchStacks` |
| Piece placements | `solve/placement_finder.h` | `PlacementFinder`, the placements of a rotated piece in the result |
| SIMD covers | `solve/simd_bitset.h`, `simd_exact_cover.h`, `simd_huang_cover.h` | AVX2, AVX-512 and NEON kernels behind runtime checks, `SimdKernels` |
| One run | `solve/search_run.h`, `solve/resume_state.h` | `SearchRun` (own stop source, serialised hand-over), `TaskProgress`, `runTasks()`, `SplitSearchState` (the saved position of a split search) |
| Duplicate assemblies | `solve/rotation_filter.h` | `RotationFilter`: rotated and mirrored copies dropped |

## The take-apart (`solve/`)

| Concern | Where | Names |
|---|---|---|
| Taking one assembly apart | `solve/disassembler.h` | `Disassembler::disassemble(assembly, stop) -> TakeApartResult`, `DisassemblerOptions`, `canDisassemble()` (cubes and prisms); breadth first, level by level, spread over a pool |
| Positions | `solve/disassembler_node.h`, `solve/node_hash.h` | `DisassemblerNode`, `NodeRef`, `NodeHash` |
| The moves from a position | `solve/movement_analysator.h`, `solve/movement_cache.h` | `MovementAnalysator` (Bill Cutler's algorithm), `MovementCacheBricks`, `MovementCachePrisms` |
| Rotation moves | `solve/rotation_rules.h`, `solve/rotation_moves.h`, `rotation_moves_classic.h`, `rotation_moves_crowell.h` | whether a 90 degree turn is possible; the Classic (complete) and Crowell (heuristic, `CROWELL.md`) turn generators |
| Pieces that stay together | `solve/grouping.h` | `Grouping` |
| Positions at each step | `solve/disasm_to_moves.h` | `DisasmToMoves`, `PiecePositions`: what the player samples |

## The solve session (`solve/solve_session.h`)

`SolveSession` is the solve of one problem across all its runs: `start()`,
`prepare()`, `resume()`, `pause()`, `pauseAtSavePoint()`, `abort()`,
`wait()`, and from any thread `state()`, `progress()`, `stats()`,
`failure()`, `readProblem(f)`. `SolveOptions` carries the Solver tab's
switches. Inside a run (`solve_session.cpp`): `launch()` checks and starts
the thread, `run()` and `prepareSearch()` build the matrix,
`assembleAndTakeApart()` runs the assembler with `onAssembly()` as its
callback, `enqueue()` and `workerLoop()` feed the take-apart workers,
`processTask()` and `addSolution()` store what comes apart, `endRun()`
closes the run. `solve_progress.h` has `SolveProgress` and `SolveStats`.
The [Example Solve Flow](/docs/code-base/example-solve-flow/) walks
through it.

Sliding puzzles search from each start layout to the goal in
`solve/sliding_search.h`; stacking puzzles search from the start stacking in
`solve/stack_search.h` and `solve/panex_search.h`. Both run inside the same
session.

## Meshes (`mesh/`)

| Concern | Where | Names |
|---|---|---|
| Drawing meshes | `mesh/voxel_mesh.h`, `mesh/draw_mesh.h`, `mesh/grid_geometry.h` and `grid_geometry_<n>.cpp` | the meshes the 3D view draws, per grid, with per-face flags, colours and cells |
| Polyhedra and modifiers | `mesh/polyhedron.h`, `mesh/modifiers.h`, `mesh/triangulate.h` | half-edge meshes, bevels, triangulation |
| STL | `mesh/cube_mesh.h`, `mesh/mink_mesh.h`, `mesh/sphere_mesh.cpp`, `mesh/stl.h` | the cube grid's chamfer table, the Minkowski mesher on Manifold, the STL file as the original wrote it |

## The document (`app/`)

| Concern | Where | Names |
|---|---|---|
| The open puzzle | `app/document.h`, `document.cpp`, `document_commands.cpp`, `document_sliding.cpp`, `document_stacking.cpp` | `Document`: ids, revision, dirty flag, the undo history, every editing command as a `Result`, the solves (`solveStart`, `solvePrepare`, `solveResume`, `solvePause`, `solveAbort`, `solveState`, `solveProgress`, `readProblem`), `poll()`, the paused solve files, imports, grid conversion |
| Tool tabs and cell tools | `app/shape_tools.h` | `ShapeTask`, `applyTask`, `CellTool`, `editCells` |
| Reads | `app/queries.h` | `problemFit` (`FitStats`), `shapeTable`, `shapeStatusLine`, `stlPieces` |
| The grid editor's drawing | `app/layer_geometry.h` | `layerGeometry(space, layer)` |
| Taking saved assemblies apart later | `app/analysis.h` | `Analysis` |
| The player's data | `app/playback.h` | `PlaybackSampler` (`sample(step)`, `track()`), `orientedMesh` |
| Step-by-step explorers | `app/explorers.h` | `AssemblyExplorer`, `MovementExplorer` |

## The engine (`engine/`)

| Concern | Where | Names |
|---|---|---|
| Reading requests, running them | `engine/engine.cpp` | `Engine::run` (the loop and its reader thread), `Engine::handleLine`, `Engine::poll` |
| A method | `engine/methods_<area>.cpp` | one function per method, registered in `add<Area>Methods`; for example `solveStart` in `methods_solve.cpp` |
| Parameters and ids | `engine/rpc.h` | `Params::id`, `string`, `optBoolean`, `finish()` (refuses unknown fields); `formatId` |
| Items as JSON | `engine/convert.cpp` | `snapshot`, `solveOptionsFrom`, `progressJson`, `failureJson`, the summaries |
| Events | `engine/events.cpp` | `pollEvents`, `pollSolve`, `pollAnalysis`, `announceChange` |

## The command line tool and the Python module

`cli/main.cpp` is `burrTxt` and `burrTxt2` in one: `cli/options.h` parses
the options, `cli/text_print.h` prints assemblies and moves as the original
did, and `--save` solves through a `SolveSession` as the app does.
`python/src/module.cpp` and `py_*.cpp` bind `Puzzle`, `Problem`, `Shape`
and `Solution` with the original module's names.

## The app: the engine client (`web/src/engine/`)

| Concern | Where | Names |
|---|---|---|
| Talking to the engine | `client.ts` | `EngineClient`: `hello()` first, `call(method, params)` pipelined in order, `on(event)` |
| Lines and frames | `framing.ts`, `frames.ts` | `FrameParser`, `viewFrame`, `attachFrame`, `meshArrays`, `poseArrays` |
| Replies before events | `dispatch.ts` | `OrderedInbox` |
| The child process | `node-transport.ts` | `NodeTransport.start`, `engineVersion` |
| What crosses IPC | `bridge.ts`, `app-bridge.ts` | `BridgeMessage` (call, reply, error, event, status), `BtApp` |
| The types | `api.generated.ts` | `Methods`, `Events`, `METHOD_NAMES`, `ERROR_CODES`, `FRAME_PATHS`; generated by `tools/gen-api.ts` |

## The app: the main process (`web/src/main/`)

| Concern | Where | Names |
|---|---|---|
| Windows and IPC | `index.ts` | `createWindow`, the `bt:call` and `bt:message` channels, the app's own URL scheme, menus rebuilt from the focused window's state |
| One engine per window | `engine-host.ts` | `EngineHost`: start, restart after a death, status |
| The host's side of the bridge | `engine-ipc.ts` | `serveEngine`: checks the method name, admits or refuses paths, forwards the call |
| Security | `security.ts`, `path-grants.ts` | `windowOptions`, `allowedRequest`, `HELP_URLS`; `PathGrants` |
| Menus, dialogs, files | `menu.ts`, `app-ipc.ts`, `export-file.ts`, `state-store.ts`, `rescue.ts` | the menu template, the file dialogs, safe writes, `state.json`, the rescue copy |

## The app: the renderer (`web/src/renderer/`)

| Concern | Where | Names |
|---|---|---|
| Starting | `main.ts`, `shell/app.ts` | `startApp` makes the `EngineProxy`, `Store`, `Solves`, `View3D`, `Edits` and the three workspaces |
| The engine, as the page sees it | `engine-proxy.ts` | `EngineProxy` over `window.bt`: same interface as the client |
| The document's mirror | `state/store.ts` | `Store`: the `Snapshot`, reloaded on `document.changed`; `whenRevision`; `solving` |
| Edits in flight | `state/edits.ts` | `Edits.mutate`, `track`, `refuse` ("Pause the solve first") |
| Solves | `state/solves.ts` | `Solves`, `SolveRecord`: state, phase, progress, the run's end by revision, `whenEnded` |
| Keyboard | `shell/keys.ts`, `shell/keyboard.ts` | the one table of keys, `onKey`, `runAction` |
| Files and menus | `shell/files.ts`, `shell/menus.ts`, `shell/rescue.ts`, `shell/imports.ts` | New, Open, Save, the unsaved-changes question, Open Recent, the rescue copy |
| The 3D view | `view3d/view.ts`, `renderer.ts`, `camera.ts`, `scene.ts`, `pieces.ts`, `solution.ts`, `mesh.ts`, `material.ts`, `picking.ts`, `meshcache.ts` | `View3D`, `ViewRenderer`, `ShapeScene`, `PiecesScene`, `SolutionScene`, `buildMesh` |
| The Shapes workspace | `entities/workspace.ts`, `shapes-list.ts`, `editor-card.ts`, `grid2d.ts`, `edit3d.ts`, `stroke.ts`, `inspector.ts`, `status-window.ts` | `EntitiesWorkspace`, `Grid2D`, `Edit3D`, `Stroke` (a drag as one undo group) |
| The Puzzle workspace | `puzzle/workspace.ts`, `problems.ts`, `pieces-card.ts`, `piece-card.ts`, `layout.ts` | `PuzzleWorkspace`, `ProblemsList`, `PiecesCard`, `PieceCard` |
| The Solver workspace | `solver/workspace.ts`, `run-card.ts`, `solutions-card.ts`, `player.ts`, `poses.ts`, `pager.ts`, `settings.ts`, `autosave.ts`, `stats-panel.ts` | `SolverWorkspace` (`run`, `pause`, `abort`), `RunCard`, `SolutionsCard`, `Player`, `PoseTrack`, `SolutionPager`, `solveOptions`, `Autosave` |
| Explorers and exports | `explore/explorer.ts`, `explore/tree.ts`, `export/exports.ts`, `export/glb.ts`, `export/stl-dialogs.ts` | `Explorer`, `Exports` |

## Tests by area

| Area | Tests |
|---|---|
| core modules | `tests/unit/<module>/test_*.cpp`; the dumps compared with the original's recordings (`test_grid_dump.cpp`, `test_model_dump.cpp`, `test_app_dump.cpp`) |
| the solve session | `tests/unit/solve/test_solve_session.cpp`, `tests/unit/app/test_document_solving.cpp` |
| the engine | `tests/unit/engine/test_engine.cpp` in process; `tests/contract/test_engine.py` as a subprocess |
| the client and the renderer's pure parts | `web/tests/unit/` |
| the app against the real engine | `web/tests/engine/` (`solving.test.ts`, `solver.test.ts`, `session.test.ts` among them) |
| the app end to end | `web/tests/e2e/*.e2e.ts` (`solver.e2e.ts` drives the Solver tab) |
| against the original | `tests/oracle/` |

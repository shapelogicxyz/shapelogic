---
title: Repository Layout
description: "What is in each directory of the ShapeLogic repository, from the C++ core to the website."
---

The repository is one tree with the core, the engine, the command line
tool, the Python module, the app and the website side by side. Build
output goes to `build/<preset>` and is never committed.

## The top level

| Path | Contents |
|---|---|
| `core/shapelogic/<module>/` | the C++ library, headers and sources together; included as `"shapelogic/<module>/x.h"` |
| `engine/` | `shapelogic_engine`, JSON-RPC 2.0 over stdio around one `Document`; also built as the library `btengine` for the unit tests |
| `cli/` | `shapelogic_cli`, the original's `burrTxt` and `burrTxt2` for brick puzzles, with `--json` and `--save` |
| `python/` | the `shapelogic` Python module: `src/` the pybind11 bindings, `tests/` its tests, `pyproject.toml` its wheel |
| `api/` | the JSON Schema of the engine's methods and events, the validator `apischema.py` and the generator of `docs/API.md` |
| `web/` | the desktop app: Electron, TypeScript and three.js |
| `site/` | this website: the landing page, the user guide and the browser demo |
| `tests/` | the C++ tests, the engine's contract tests, the CLI tests, the comparison harness and the benchmarks |
| `docs/` | the architecture document, the plans, the generated API reference, the decision records, the hand-off notes and the vendored design mockup |
| `cmake/` | warnings, sanitizers, dependencies, and whether the Python module is built |
| `assets/` | the project's icon |
| `.github/workflows/` | the Release, Staple and Site workflows |
| `AGENTS.md`, `CLAUDE.md` | the working rules for anyone, human or tool, changing the code |
| `justfile`, `CMakeLists.txt`, `CMakePresets.json` | the build |

## The core: `core/shapelogic/`

One directory per module. Dependencies point downwards only: `app` may use
everything below it, `base` uses nothing.

| Module | What it holds |
|---|---|
| `base/` | `StopSource` and `StopToken` (cancellation), `SolveState`, `Result<T>` and `Error`, `BT_ASSERT`, `ThreadPool`, `ClonePtr`, the version |
| `grid/` | `GridType`, the abstract `Voxel` space and one class per grid (`voxel_bricks`, `voxel_prisms`, `voxel_spheres`, `voxel_rhombic`, `voxel_tetra_octa`), the symmetry tables in `tabs_0/`, `tabs_1/`, `tabs_2/`, `VoxelTable` |
| `model/` | `Puzzle`, `Problem`, `Assembly`, `Disassembly` and `Separation`, `Solution`, the sliding and stacking models, grid conversion |
| `io/` | `.xmpuzzle` reading and writing (XML pull parser, XML writer, gzip), the paused solve file, the PuzzleSolver3D and Puzzlecad imports |
| `solve/` | the assemblers (`assembler_0`, `assembler_1`, the SIMD covers), the run layer `search_run`, the take-apart (`disassembler`, `movement_*`, `rotation_*`), the slide, stack and Panex searches, and `solve_session` |
| `mesh/` | the drawing meshes of every grid, the STL meshes (Minkowski mesher on Manifold), triangulation, `vec3` |
| `app/` | `Document` (the puzzle with ids, undo history, editing commands and the solves), `shape_tools`, `queries`, `layer_geometry`, `analysis`, `playback`, `explorers` |

Each ported file names the original BurrTools file it comes from in a
comment at the top.

## The engine: `engine/`

| File | What it holds |
|---|---|
| `main.cpp` | the process: `--version`, binary stdio on Windows, runs `Engine` |
| `engine.h`, `engine.cpp` | `Engine` (the dispatcher and the run loop with its reader thread), `State` (the document, settings, handles, the watches of running solves) |
| `rpc.h`, `rpc.cpp` | error codes, `Params` (reads a method's fields, refusing unknown ones), the id formats (`s7`, `p1`, `sol42`, `pb1`), `BTE_TRY` and `BTE_CHECK` |
| `output.h`, `output.cpp` | writing lines and binary frames, `FrameBuilder` |
| `convert.h`, `convert.cpp` | the JSON of every item: snapshots, summaries, options, progress, failures, rows |
| `methods_<area>.cpp` | one file per schema file: `document`, `shape`, `colour`, `problem`, `solve`, `solution`, `playback`, `explore`, `sliding`, `stacking`, `export`, `engine` |
| `events.cpp` | `document.changed` and the poll of background work: `solve.state`, `solve.progress`, `solve.solutionsAdded`, `solve.error`, the analysis events |
| `wasm_main.cpp` | the entry of the WebAssembly build for the website's demo |

## The app: `web/`

| Path | What it holds |
|---|---|
| `src/engine/` | the engine client, platform-free: `framing.ts` (stdout into lines and frames), `frames.ts` (typed-array views), `client.ts` (`EngineClient`), `dispatch.ts` (replies before events), `bridge.ts` (what crosses between main process and renderer), `app-bridge.ts` (menus, dialogs, files), `api.generated.ts` (the types), and the one Node file `node-transport.ts` (the engine as a child process) |
| `src/main/` | Electron's main process: `index.ts` (windows, IPC, the scheme the page is served on), `engine-host.ts` (one engine per window, restarted when it dies), `engine-ipc.ts` (the host's side of the bridge), `engine-path.ts`, `menu.ts`, `app-ipc.ts`, `security.ts` (web preferences, allowed requests, help URLs), `path-grants.ts` (which files a window's engine may be given), `state-store.ts`, `rescue.ts`, `export-file.ts` |
| `src/preload/` | the sandboxed preload exposing `window.bt` (the engine bridge) and `window.btApp` (the app bridge), and nothing else of Electron or Node |
| `src/renderer/` | the web page: `index.html`, `styles.css`, `main.ts`, `engine-proxy.ts` and the directories below |
| `src/renderer/shell/` | the frame of the app: `app.ts` puts everything together; `keys.ts` is the one keyboard table; `layout.ts`, `menus.ts`, `files.ts`, `status.ts`, `settings.ts`, `help.ts`, dialogs and popovers |
| `src/renderer/state/` | `store.ts` (the mirror of the engine's document), `edits.ts` (mutating calls awaited to their revision), `solves.ts` (one record per problem being solved), `stacking.ts` |
| `src/renderer/view3d/` | the 3D view in three.js: `view.ts`, `renderer.ts`, `camera.ts`, `mesh.ts`, `material.ts`, `scene.ts`, `pieces.ts`, `solution.ts`, `picking.ts`, `meshcache.ts`, `svg.ts` |
| `src/renderer/entities/` | the Shapes workspace: the shapes list, the voxel editor (`grid2d.ts`, `edit3d.ts`, `stroke.ts`), the inspector, the status window |
| `src/renderer/puzzle/` | the Puzzle workspace: problems, the pieces card, the piece card, the pieces scene layout |
| `src/renderer/solver/` | the Solver workspace: `workspace.ts`, `run-card.ts`, `solutions-card.ts`, `player.ts`, `poses.ts`, `pager.ts`, `settings.ts`, `autosave.ts`, `stats-panel.ts` |
| `src/renderer/explore/` | the step-by-step assembly and movement explorers and the debug statistics |
| `src/renderer/export/` | File ▸ Export: image, vector image, STL, glTF |
| `src/demo/` | the website's browser demo: the engine's WebAssembly transport and a stand-in for Electron's menus and dialogs |
| `tools/` | `gen-api.ts` (the types from the schema), `dist.ts` (packaging), `record-sessions.ts` (the fake engine's recordings), `build-demo.ts`, `project-version.ts` |
| `fake-engine/` | a recorded engine session replayed as a transport, for the unit tests |
| `tests/unit/` | Vitest, no engine |
| `tests/engine/` | Vitest against the real release engine as a child process |
| `tests/e2e/` | Playwright: the built app driven through its window, with reference screenshots in `__screenshots__/` |
| `tests/docs/` | the Playwright scripts that take this guide's screenshots |
| `packaging/` | the icons and the macOS entitlements |

## The tests: `tests/`

| Path | What it holds |
|---|---|
| `unit/<module>/` | Catch2 tests, one directory per core module plus `engine/`; one binary `bt_tests` |
| `contract/` | `test_engine.py` drives `shapelogic_engine` as a subprocess and checks every reply and event against the schema |
| `cli/` | `test_cli.py`: the command line tool's options and modes on the fixtures |
| `oracle/` | the comparison harness against the original BurrTools: `oracle.py`, the corpus list, small programs built against the original's sources, the recorded baselines |
| `bench/` | `bench.py`: solve speed against the original, back to back |
| `data/` | fixtures: `puzzles/`, and the original's recorded answers (`grid_dump.txt`, `model_dump.txt`, `saved_by_original/`, `analysed_by_original/`) |
| `tools/` | small programs the harness drives (`roundtrip`, `resume`, `solutions`, `imports`) |

## The website: `site/`

| Path | What it holds |
|---|---|
| `astro.config.mjs` | Starlight's configuration, including the sidebar you are looking at |
| `src/content/docs/docs/` | the guide's pages, Markdown; the path is the URL (`code-base/stack.md` is `/docs/code-base/stack/`) |
| `src/assets/screens/` | the screenshots, taken by the app itself (`just docs-screenshots`) |
| `src/pages/index.astro`, `src/styles/landing.css` | the landing page |
| `public/demo/` | the built browser demo, committed; `public/examples/` the puzzles the guide links to |
| `tests/` | the Help URL check and the demo's staleness check |

## The documents: `docs/`

| Path | What it holds |
|---|---|
| `ARCHITECTURE.md` | what exists now, module by module, and the rules that hold everywhere; updated at the end of every stage |
| `PLAN.md`, `FRONTEND-PLAN.md`, `DEFERRED-PLAN.md` | the stages of the core, the app and the deferred features, ticked as they were done |
| `API.md` | the engine reference, generated from `api/` |
| `webui-spec.md`, `core-api.md` | the specification of the app and the draft core API |
| `decisions/` | numbered decision records, each with its context, the decision and its consequences |
| `handoff/` | one note per finished stage: what was done, the gate results, known issues and the brief for the next stage |
| `mockup/` | Girish Sharma's design mockup, vendored; the app's CSS and card renderers come from it |

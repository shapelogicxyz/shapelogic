# Plan: the web front end

## Goal
A desktop application, **Electron + TypeScript + three.js**, that drives
`bt-engine` over JSON-RPC (decisions 0003 and 0012) and gives the original
BurrTools GUI's functions for everything the core solves, laid out as
Girish Sharma's mockup (docs/webui-spec.md). The mockup is the **basis**:
its HTML, CSS and interaction code are taken over and reworked, section by
section, not designed again. The engine is the reference for what an
action does; the mockup is the reference for how it looks and feels; the
original GUI is the reference where the mockup is silent (webui-spec.md
section 7).

**In scope (first complete version):** the shell and the 3D view; the
Entities, Puzzle and Solver workspaces of webui-spec.md; the disassembly
player; the explorers and the debug panel; menus, files, settings,
recovery; the exports that need no new core work (image, vector image,
GLB animation, paused solves); packaging for macOS, Windows and Linux.

**Later (with the core features of PLAN.md's "Later"):** sliding and
stacking/Panex editing and solving, STL export from the STL meshes, the
imports (PuzzleSolver3D, Puzzlecad), grid conversion; a browser-only build
(a WebSocket bridge or WebAssembly, decision 0003); localisation.

## The mockup as the basis
- **Source:** `index.html` on the `gh-pages` branch of `grssam/burr-tools`,
  commit `d7afb3ed` (2026-10-04), GPL-3.0; BurrTools Alpha is
  GPL-3.0-or-later, so the code can be taken over. The Pages URL
  (https://grssam.github.io/burr-tools/) answered 404 on 2026-10-08; the
  file is read from the branch. Stage 1 vendors it as
  `docs/mockup/index.html` with its commit and licence named, adds the
  author to AUTHORS, and every file derived from it cites the source at
  its top, as AGENTS.md asks of ported code (`pages/index.html` on the
  same branch is an earlier copy: ignored).
- **What is kept:** the CSS (its tokens for light and dark, density,
  cards, rails, chips, strips: lines 7 to 711), the body (the shell:
  top bar, workspace rail, the six cards, the viewport with its toolbar,
  status bar, settings, popups), and the script's shell logic (layout,
  focus, theme, density, settings, flash messages, confirmations, the
  keyboard map, the shapes list, the 2D editor's strips and tool rows, the
  Puzzle and Solver cards' render functions) as **views over the engine's
  data**. Its wording, keys and messages stay.
- **What is replaced:** its data model and every client-side voxel
  operation (`mkVox`, `cloneShape`, `doFill`, `doMin`, `surfaceSets`…:
  the engine does these), the hand-projected canvas renderer
  (`render3d`, `cubeGeo`, `cubeHit`: three.js over `shape.mesh` frames),
  the mock geometry of the non-cube grids (`grid.geometry` and the
  engine's meshes), the fake solver (`svRun`, `svBuild`, `svStep`: the
  `solve.*` calls and events), the fake player (`playback.*`).
- **How:** its 1,400-line script becomes TypeScript modules; each stage
  below names the mockup functions it ports. webui-spec.md's "its code is
  not [adopted]" is superseded; stage 1 updates that paragraph and records
  the choice in decision 0013.
- The mockup's own spec (items "OQ-19", "C1" to "C14") is unpublished;
  where it is cited, the original GUI decides.

## The shape of the client (settled in decision 0013, stage 1)
- **Layout:** `web/` beside `engine/` and `cli/`:
  `web/src/engine/` (the client, platform-free: framing, the typed calls,
  the generated types), `web/src/main/` (Electron's main process: the
  engine process, windows, menus, file dialogs, recent files, settings
  storage), `web/src/preload/` (the bridge), `web/src/renderer/`
  (the app: `shell/`, `view3d/`, `entities/`, `puzzle/`, `solver/`,
  `explore/`, `state/`), `web/tools/gen-api.ts`, `web/tests/` (`unit/`,
  `engine/` against the real `bt-engine`, `e2e/` Playwright),
  `web/fake-engine/` (recorded sessions replayed for fast tests).
- **Process model:** one `bt-engine` per window, spawned by the main
  process; the renderer is sandboxed (`contextIsolation` on,
  `nodeIntegration` off, `sandbox` on) and reaches the engine through a
  proxy over IPC that has the client's interface, frames crossing as
  `ArrayBuffer`s; events are forwarded. The client itself runs over a
  `Transport` interface, so the same code drives the engine in Node tests
  and, later, over a WebSocket. Everything is asynchronous; the renderer
  never blocks on the engine.
- **State:** a store that mirrors `Snapshot` plus a per-problem solve
  record; `document.changed` reloads what its ids name (empty lists: the
  lists themselves); the UI never edits its mirror, it calls and waits
  for the revision (decision 0012's order: reply, `document.changed`,
  then the poll's events). `invalid_state` shows "pause first" (decision
  0011). Meshes are cached by shape, revision, style and transformation.
- **Toolchain** (pinned in `web/package.json`, lockfile committed):
  Node 22 LTS, npm; TypeScript (strict), electron-vite (Vite for the
  renderer, bundles main and preload), Electron (the current stable at
  the start of stage 1), three.js, electron-builder; Vitest, Playwright's
  Electron driver, ESLint with typescript-eslint, Prettier. **No UI
  framework:** the mockup is plain DOM with template strings and
  re-renders a card from state; porting it to React would be a rewrite.
  Revisit after stage 2 if the pattern hurts.
- **Recipes** (`justfile`): `web-install`, `web-gen` (the types),
  `web-dev` (the app against `build/dev` or `BT_ENGINE`), `web-check`
  (`tsc --noEmit`, eslint), `web-test` (unit; the engine tests when
  `build/release/engine/bt-engine` exists), `web-e2e`, `web-dist`.
- **Prerequisites on this machine:** Node 22 (installed: 22.23.3,
  Homebrew); Playwright's Electron driver needs no browser download; the
  engine tests need `just build release`.

## Stages and sessions

Each session starts fresh, reads AGENTS.md, ARCHITECTURE.md, this plan,
docs/webui-spec.md, docs/API.md, decision 0012 and the newest
`docs/handoff/web-*.md`, and ends with "Finishing a front-end stage"
below. Effort is Claude Code's setting (extra high is `xhigh`); the
model column is the recommendation of "Models, effort and audits".

| Session | Stage | Work | Model | Effort | Gate |
|---|---|---|---|---|---|
| 1 | 1a | Toolchain; TypeScript types generated from `api/`; the engine client: framing, frames, typed calls, events, `engine.hello`, restart | Opus 5.5 | extra high | framing replays byte-identical at every chunk size; the generated types are current and name every `engine.info` method; the client passes the scripted session against the real engine; events in decision 0012's order |
| 2 | 1b | The shell from the mockup; the 3D view in three.js drawing `shape.mesh` frames; picking; the view cube | Opus 5.5 | high | every shape of every fixture drawn in all styles on all five grids (reference screenshots); a click names the cell and side the frame says; the keyboard map's shell rows work; `web-e2e` runs |
| 3 | 2 | Entities workspace: shapes list, 2D editor on `grid.geometry`, tools, drags as one undo step, planes and layers, size, palette, transforms, 3D edit, undo/redo | Opus 5.5 | high | a fixture's shapes rebuilt through the editor equal its cells; a drag is one undo step; every `shape.taskSet` button works on every grid; nothing refused by the engine during the scenario |
| 4 | 3 | Puzzle workspace: problems, result, pieces, counts and ranges, groups, colour rules, fit, the pieces scene | Opus 5.5 | medium | a fixture's problem rebuilt through the UI equals `problem.get`; the fit line equals `problem.fit` |
| 5 | 4 | Solver workspace: options, Solve / Prepare / Continue / Pause / Abort, progress, solutions list and slider, remove, analyse, the player on `playback.*`, autosave; the dt-less orientation fix in the core | Opus 5.5 | extra high | counts equal `bt --json`'s on the fixtures; the list in every sort equals `solution.page`; the player's poses equal `playback.poses`; pause, continue, abort, save point, error run; fake-engine replays of every event-order rule |
| 6 | 5 | Explorers (placements, movements, step), the debug statistics panel, the status window | Opus 5.5 | medium | placements and positions drawn where the engine says (hotspot); the movement tree's analyse / add / find / prune round-trip the contract session |
| 7 | 6 | Menus and files: new (grid choice), open, save, save as, recent, close with changes, notes, paused solves, F1 map, about; settings and layout persistence; engine crash recovery | Opus 5.5 | high | open and save of every fixture leaves the document clean and the bytes the engine's; every keyboard map entry fires; an engine killed mid-edit is restarted and the rescue copy reloaded; Windows run by hand |
| 8 | 7 | Exports needing no core work: image, vector image (SVG), GLB animation, interim STL from the flat mesh | Opus 5.5 | medium | the GLB validates and matches the player at sampled times; STL of a cube is closed |
| 9 | 8 | Packaging: electron-builder, the engine bundled per platform, versions tied, security checklist, smoke test of the built app | Opus 5.5 | medium | the packaged app on macOS opens and solves a fixture; `bt-engine --version` equals the app's; Windows and Linux builds run by hand |

Engine additions the stages need (the "What the API lacks" table) are
made in the stage that needs them, each as its own commit on the C++
side following api/README.md's "Adding a method" (schema, handler,
contract call, `gen_api_md.py`, then `just web-gen`), never silently.

**Recommended audits** (by the highest available model, max effort, given
the diff, the gate results and this plan, reporting findings with
concrete failure scenarios): after stage 1a (the client everything else
sits on; its failures are silent), after stage 4 (the event order, paging
by `solutionsVersion`, the abort and autosave paths, the core change),
the save / recover / close paths of stage 6, and the Electron security
surface before the first packaged release (stage 8). See "Models,
effort and audits".

## Status

- [x] **Stage 1a**: toolchain, generated types, the engine client (docs/handoff/web-1a.md)
- [x] Audit of stage 1a (docs/handoff/web-1a.md, "Audit")
- [x] **Stage 1b**: the shell and the 3D view (docs/handoff/web-1b.md)
- [x] **Stage 2**: the Entities workspace (docs/handoff/web-2.md)
- [x] **Stage 3**: the Puzzle workspace (docs/handoff/web-3.md)
- [x] **Stage 4**: the Solver workspace and the player (docs/handoff/web-4.md)
- [x] Audit of stage 4 (docs/handoff/web-4.md, "Audit")
- [ ] **Stage 5**: the explorers, the debug panel, the status window
- [ ] **Stage 6**: menus, files, settings, recovery
- [ ] Audit of stage 6's file paths
- [ ] **Stage 7**: exports
- [ ] **Stage 8**: packaging
- [ ] Security audit before the first release

## Stage details

### Stage 1a: toolchain, generated types, the engine client
**Toolchain.** `web/` as above; `just web-*`; `web/README.md` (how to
run, where the engine comes from) and a front-end section in AGENTS.md
(the rules below, the finishing steps); decision 0013; the mockup vendored
and the author in AUTHORS; webui-spec.md's source paragraph updated.

**Types** (`web/tools/gen-api.ts`, output `web/src/engine/api.generated.ts`,
committed). Reads `api/*.json` (the subset api/README.md lists, nothing
more): every `$defs` entry a named type; each method an entry of
`interface Methods { 'shape.mesh': { params; result } }`, each event of
`Events`; `enum` and `const` as literal unions, `oneOf [T, null]` and
`type: [..., "null"]` as `T | null`, `additionalProperties: {type: string}`
as an index signature, `$ref` across files, descriptions as doc comments;
`METHOD_NAMES`, `EVENT_NAMES`, the errc union with its codes, and a
marker for results that carry a `Frame` (`MeshFrame`, `Poses`). A unit
test regenerates into memory and fails when the committed file is stale
(as ctest `api-docs` does for API.md); an engine test compares
`METHOD_NAMES` with `engine.info.methods`.

**The client** (`web/src/engine/`):
- `framing.ts`: a parser over byte chunks with two states, a line and
  the L bytes of a frame; emits messages and frames; copies frame bytes
  once; keeps stderr in a ring buffer for error reports. Invariants: a
  `frame` notification is followed by the reply that names it; a reply
  naming a frame that did not come, or a frame no reply names, is a
  protocol error.
- `client.ts`: `EngineClient` over `Transport` (`write`, `onData`,
  `onExit`, `kill`): `call('shape.mesh', {shape})` typed by `Methods`,
  the reply carrying its frame as typed-array views over the bytes
  (`arrays`, no copy); `EngineError {code, errc, message}`; `on(event,
  handler)` typed by `Events`; `hello(1)` first, any other protocol
  refused; requests only (never notifications: the reply order is what
  the UI relies on), pipelined in order; a reply is settled before the
  events behind it are dispatched; a request kind can be **coalesced**
  (the newest mesh request per key wins, stale replies dropped) so a
  drag never queues a hundred meshes; a bounded number in flight.
- `node-transport.ts` (child process, pipes, exit status, `--version`
  check), `recorder.ts` (a session's bytes and messages recorded for the
  framing tests and `web/fake-engine/`), `fake-engine/` (replays a
  recording: the same lines and frames, timing optional).
- The host and the proxy: `main/engine-host.ts` owns the process and
  restarts it when it dies (the window is told; stage 6 adds the rescue
  reload); `preload.ts` exposes `window.bt`; `renderer/engine-proxy.ts`
  has `EngineClient`'s interface.

**Gate.** `just web-check` clean. Framing: recordings of the real engine
replayed in chunks of 1, 7, 4096 bytes and whole give the same messages
and frames; a 0-byte frame; a line that is not JSON; the engine closing
mid-frame; a 64 MiB line refused and the engine going on. Engine tests
(skipped with a message when the release engine is not built): hello and
a refused protocol; info; open every fixture in `tests/data/puzzles/`;
`shape.mesh` of every shape in all three styles with the frame's sizes
matching `triangles` and `vertices`; the scripted session of stage 7's
contract tests in TypeScript (open Bermuda, solve with take-apart, page
sorted every way, play back with poses and a track, explore, take apart
again, turn into shapes, remove, save); pause, save point, continue and
abort on the 2x4x5 box of dominoes; a new document during a run gets the
last `idle`; the event order of decision 0012 asserted (a run's last
`solve.state` before the `document.changed` with the new revision; an
abort's two `document.changed`); the engine killed: the client reports
and the host restarts it. Run three times.

### Stage 1b: the shell and the 3D view
**The shell** (`renderer/shell/`, from the mockup's `applyLayout`,
`setTab`, `applyTheme`, `applyDensity`, `openSettings`, `renderSettings`,
`flash`, `updateStatus`, `askConfirm`, `popOpen`, `renderRail`, the
keydown handlers): the top bar (file name, path on hover, dirty mark),
the workspace rail, the six cards with collapse and focus and the layout
remembered per workspace, the status bar (flash, cursor), the settings
dialog (every setting of webui-spec.md section 1; undo depth and threads
go to `settings.set`, the rest are the client's; stored by stage 6), theme
and density, Esc's step-back order, Tab order, tooltips. The keyboard map
is **one table** (`shell/keys.ts`) with the rows of webui-spec.md section
6; the F1 dialog (stage 6) prints it. The default piece colours are the
original's `pieceColor` table (src/gui/piececolor.cpp), ported and cited.

**The 3D view** (`renderer/view3d/`): `renderer.ts` (three.js, DPR,
resize, lights on or off, perspective and orthographic), `camera.ts`
(the mockup's orbit, pan, zoom with the Drag and Arc-ball methods, the
reverse-zoom setting, Home and F with animated moves; three.js's
controls if they can be made to follow the mockup's map, else the
mockup's logic ported), `viewcube.ts` (the mockup's canvas cube driven by
the camera's quaternion; click and double-click to views), `mesh.ts`
(`MeshFrame` to `BufferGeometry`: positions, normals, index; the
per-triangle `colour`, `flags`, `voxel`, `voxelFace` expanded per vertex;
drawing, wireframe (flag 8 faces as lines, "show through solid pieces")
and flat (edge lines as `LineSegments`) styles; the colour view: the
shape's colour or the constraint colours; variable marks (flags 2, 4),
bevel and gap shades (16, 32), inside faces (128) hidden unless x-ray;
the light checker colour (1)), `picking.ts` (a `Raycaster` hit's
`faceIndex` gives `voxel` and `voxelFace`: the cell and side), `overlays.ts`
(axes, bounds, the current layer's slab and dimming of the others, from
the voxel index and the shape's size), `scene.ts` (one shape now; the
pieces grid and the solution poses in stages 3 and 4), `meshcache.ts`.
The viewport toolbar (Orbit, Pan, Edit, Display, Focus) and the empty
message. Nothing of the mockup's cube projection survives; the
non-cube grids draw from the engine's meshes only.

**Gate.** The app starts, spawns the engine, shows every fixture's shapes
(a temporary shape picker until stage 2) in drawing, wireframe and flat
styles, lit and unlit, both projections, both colour views, on all five
grids; reference screenshots recorded in `web/tests/e2e/__screenshots__`
after one look beside the original GUI, compared with a tolerance from
then on. Picking: clicking a known face of a known shape yields the cell
and side the frame's arrays hold (an e2e over `tests/data/puzzles`).
The shell rows of the keyboard map work (Ctrl+1/2/3, Ctrl+,, Ctrl+[ ],
Ctrl+Space, Esc, O/P, Home, F). Orbiting the largest fixture mesh stays
at the display's rate; the mesh build of a 1,000-cell shape under 10 ms
(measured, in the handoff). `just web-e2e` runs under Playwright.

### Stage 2: the Entities workspace
From the mockup's `renderShapes`, `renderInsp`, `renderTransform`,
`renderTools`, `renderGrid`, `spaceGridSvg`, `renderLayers`,
`renderPlane`, `renderColors`, `renderSelbar`, `applyTool`, `applyCell`,
`hover2d`, the `#garea` pointer handlers, `shapeAction`, the list's
keydown handler.
- **Shapes list** (`shape.list`, `create`, `duplicate`, `delete` (not the
  last), `move` by drag or Alt+arrows, `rename` (F2, Enter, double-click;
  line breaks refused by the engine: show its message), `setWeight`,
  `setColour`), the right-click menu, selection.
- **The 2D editor:** one SVG renderer for every grid from `grid.geometry`
  (polygons, circles on the sphere grid, neighbours for the cursor's
  edges); cells from `shape.get {layer}`; the cube grid's XZ and YZ planes
  are the client's (squares, cells filtered by y or x, as `shape.get`
  says); the layer strip with marks for layers holding cells, PgUp/PgDn;
  the tools fixed, variable, erase, paint (1 to 4), the modifiers
  (Shift, Alt or Cmd, Ctrl) and right-click erase; mirror and span as
  `shape.setCells` axes (verify what the core does with them on each
  grid; hide them where it ignores them); a **drag** collects the cells
  entered, sends them in batches of one animation frame inside
  `edit.begin` / `edit.end`, so a drag is one undo step and never floods
  the engine; box selection with fill and clear (`shape.setBox`), Esc;
  the grid size steppers in `sizeUnit` blocks with Apply to all
  (`shape.resize toAll` grows the others, never shrinks: say so in the
  tooltip); the palette (`colour.add`, `edit`, `delete`; Default is the
  shape's own colour); the status bar's cursor cell and the counts.
- **Transform, fit and repair:** the buttons come from `shape.taskSet`
  (group, label, toAll) so each grid shows its own set in the original's
  order, run through `shape.task`; the blocked nudge and the counts
  ("Filled 3 interior voxels" from `cellsChanged`) as flash messages.
- **3D edit mode** (E): a click on a face applies the tool to the cell
  (erase, paint) or to the cell across the face (fixed, variable): the
  client's table for cubes, `grid.neighbour` for the other grids (the
  engine addition below); the live preview with mirror and span ghosts.
- **Undo and redo** (Ctrl+Z, Shift+Ctrl+Z; the labels from
  `edit.history`), and the frozen state while a solve or analysis runs
  (`Snapshot.solving`): the cards disable and the status bar says "pause
  the solve first", so the engine's `invalid_state` is never provoked.

**Engine addition:** `grid.neighbour {size, cell, face} -> {cell | null}`
over `Voxel::getNeighbor` (face kind), the side numbering of the mesh's
`voxelFace`.

**Gate.** e2e on each of the five grids: new document, draw with every
tool, modifiers and right-click, mirror and span, box fill and clear,
layers, resize with and without Apply to all, every task of
`shape.taskSet` (the blocked nudge's message among them), palette and
paint, 3D edit on cubes (and on the other grids through `grid.neighbour`);
a drag over N cells leaves one entry in `edit.history`; undo and redo
give the cells back (`shape.get` before and after); every shape of
Bermuda and of Prisgon rebuilt from their cell lists through the
editor's actions equals the fixture's `shape.get`; the engine's log of
the scenario holds no `invalid_state` or `invalid_argument` reply.

### Stage 3: the Puzzle workspace
From the mockup's `renderPuzzle`, `renderPzSel`, `renderPzGroups`,
`renderPzRules`, `pzAdd`, `pzBump`, `pzBulkRange`, `pzLibChip`,
`pzUsedTile`, `pzDragStart` / `pzDragEnd`, `pzMenu`, `pzStats`,
`renderPuzzleScene`, `puzLayout`, `puzPick`.
- **Problems** (`problem.list`, `create`, `duplicate`, `delete` (not the
  last), `rename`, `move`), the **result** (`problem.setResult`, the cell
  count and "can hold n–m" from `problem.fit`), **pieces** as chips or a
  table (`ProblemDetail.parts`: `addPiece`, `removePiece`,
  `setPieceCount` for ranges, `movePart`), the **library** of all shapes
  (drag in, double-click, +; drag out), the More menu (`addOneOfEach`,
  `setRangeAll`, `removeAllPieces`), the **fit summary** and the status
  bar (`FitStats.statusLine`), the **selected piece** card (copies or a
  range, the exclusive group: `setGroup`, `setGroupCount`,
  `deleteGroup`), the **colour rules** matrix (`setColourRule`,
  `setColourRules` presets), the right-click menus (one more, one fewer,
  remove, set as result), the keys (+, −, Del).
- **The pieces scene:** the result, the selected piece and every copy of
  every piece in a grid (`shape.mesh` per shape, instanced per copy),
  optional copies faded, click to select, each piece turning about its
  own centre.
- Sliding and stacking puzzles open and show (`engine.info.grids[].supported`,
  `problem.kind`), with a notice that this version does not solve them.

**Gate.** e2e: the problem of CubeInCage (and of a piece-range fixture,
DemoPieceGenerator) built from an empty problem through the UI's actions;
`problem.get` equals the fixture's (result, parts, min, max, groups,
colour rules, maxHoles); the fit summary and the status bar equal
`problem.fit`; chips and table agree after every action; the scene shows
one mesh per copy with the optional ones faded.

### Stage 4: the Solver workspace and the player
From the mockup's `renderSolver`, `renderSolverDrawer`, `renderSvSel`,
`renderSvSortAct`, `renderSolverPlayer`, `svSelect`, `svSeek`,
`svTogglePlay`, `svPlayLoop`, `svManage`, `svDaCell`, `renderPcs`,
`renderSolverScene`, `svKey`, the Solver keydown handlers.
- **Options:** the search options over `SolveOptions`, read and written
  with the problem's stored `SolverSettings` (both spellings are
  permanent: `types.json` has the mapping; `problem.setSolverSettings`
  on change), the solver type, Check Rotations, Just Levels, Strict
  Colours, Thorough rotation check (`completeRotations`), Keep at most,
  Sort by; the mockup's "Drop below level" becomes "Keep every n-th"
  (`keepEach`), as the original has it; "Enable High Memory" waits for
  the sliding solvers. Threads from the settings.
- **Run:** Solve (`solve.start`), Prepare, Continue, Pause, **Abort**
  (refused once a run has ended: the button greys out as the original's;
  "remove all" is `solution.delete {which: all}`, undoable); the progress
  bar and counters from `solve.progress`, the activity text, time used;
  the problem's state from `solve.state`; a run's end waited for by its
  **revision** (`document.changed`), not its state; `solve.error` shown
  with the failure; a per-problem solve record, since several problems
  may run.
- **Solutions:** the list over `solution.page` with the engine's sorts
  (click a header, again to reverse), paged on scroll, re-read when
  `solve.solutionsAdded` moves `solutionsVersion`; the slider view; Del,
  the Remove menu (`solution.delete`: one, before, after, without
  disassembly, all; each asks), delete disassemblies; Analyse
  (`solution.analyse` with progress and stop, the results on
  `analyseEnded` and its `document.changed`); the status lines
  (`SolutionRow.disassembly`: tree, summary, none).
- **The player:** `playback.open`, `playback.mesh` per piece in its
  orientation and the chosen style, `playback.poses` for the steps
  shown (sampled; the client interpolates between samples, slerp for the
  turns), play/pause, one move back and forward, jump to assembled and
  apart, the seek bar, Space / Enter / arrows / Home / End / PgUp /
  PgDn; the piece chips solid, wireframe, hidden; fade of removed pieces
  (the setting); a solution without a disassembly is the single pose.
  The assembled view of a solution outside the player (`solution.get`
  placements over `shape.mesh {transformation}`) needs the hotspot: the
  engine addition below.
- **Autosave** (the setting, every 20 minutes while a solve runs):
  `solve.savePoint`, wait for `paused`, `document.save`, `solve.continue`
  (decision 0010); refused saves while solving offer this path.
- **The core change:** the dt-less playback orientation (stage 7's
  handoff, "Found in the original"): `DisasmToMoves::getTrans` answers
  "not given" for a state without orientations, so a classic solution
  reloaded from a file plays as the assembly placed it. Its own commit:
  the unit test in `test_app_dump.cpp` that matched the original's
  sampler is adjusted and the difference recorded in decision 0013's
  list and the handoff; `just oracle --tier fast` and `just oracle-files
  --tier fast` stay green (the files hold no poses).

**Engine addition:** `hotspot` (a `Cell`) in `MeshFrame` for `shape.mesh`
(and `playback.mesh`), so a turned mesh is placed at its cell minus it.

**Gate.** e2e: Bermuda solved with take-apart on every thread: the
counters, then the final counts equal those of `bt -q -n -d` (`--json`)
on the same file; the list sorted every way equals `solution.page`'s
order; the first solution played to the end, stepped back and forward,
jumped; the player's poses at integer steps equal `playback.poses`'s
samples; pause, continue and abort on the dominoes box, a save at a save
point then continue, Prepare then Continue, the error run of the contract
suite shown with its failure; Analyse on a fixture solved without
take-apart, with progress and a stop that keeps what was done; every
Remove variant with its confirmation. Fake-engine replays assert the
rules: `solve.state` before the run's `document.changed`; a run that
starts and ends between polls; an abort's two `document.changed`; a new
document's last `idle` and `analyseEnded {stopped}`; `solutionsAdded`
without a page change is ignored. The core change's tests and oracle
tiers pass.

### Stage 5: the explorers, the debug panel, the status window
From the mockup's `svPrepare`, `svStep` and the Advanced section.
- **Placements** (`explore.openAssembly`, `placements`, drawn with
  `shape.mesh {transformation}` at the placement minus the hotspot),
  **Step** (`explore.step`, the pieces placed now, `finished`),
  **Movements** (`explore.openMovements`, the tree of `MovementNode`s:
  `analyse`, `addMove` with the grid's `directions`, `findMatching` by
  dragging a piece in the view, `prune`), handles closed when the panel
  closes or the document changes.
- **Debug statistics** (`solve.status.stats`, refreshed with the
  progress events) and the **status window** (`shape.table`), both
  dialogs of the original (webui-spec.md section 7).

**Gate.** e2e on Bermuda: every piece's placements listed and drawn;
stepping reaches a placement set and `finished` grows; the movement
tree's root, analyse, add a move, find a matching move and prune match
the contract session's replies; the status window's rows equal
`shape.table`; the panel's numbers equal `solve.status`.

### Stage 6: menus, files, settings, recovery
- **Menus** (Electron's, with the mockup's placeholders replaced): File
  (New with the grid chosen once, Open, Open Recent, Save, Save As, Import
  paused solver state, Export submenu, Close), Edit (Undo and Redo with
  their labels, Convert assemblies to pieces (`solution.toShapes` with
  the original's filters), Settings), View (the display modes and render
  styles, Toggle 3D), Help (Tutorial and User Guide links, the keyboard
  map on F1, About with the engine's version from `engine.info`).
  Import PuzzleSolver3D and Puzzlecad, Convert grid type and the STL
  meshes stay greyed until the core has them.
- **Files:** `document.new`, `open`, `save`, the title and dirty mark
  from `Snapshot`, close and quit asking about unsaved changes, recent
  files, drag and drop of a file onto the window, `.btsolve` import and
  export (`solve.importPaused`, `exportPaused`); a save refused while
  solving offers the save-point path.
- **Notes** (`document.setNotes`, allowed while solving), the F1 dialog
  from `shell/keys.ts`.
- **Persistence** of the settings and the layouts (the main process's
  store); **recovery:** a rescue copy (`document.saveBytes` after each
  change, throttled, in the app's data directory, as the original's
  `__rescue.xmpuzzle`) and, when the engine dies, a restart with the
  rescue copy offered.
- Windows: the dev app run once on Windows (the engine's binary stdio has
  never run there) and what was found recorded.

**Gate.** e2e: new on every grid; every fixture opened and saved as a
copy with no `document.changed` on open and the copy's bytes equal to
the engine's own save of it (the UI adds nothing); a document edited,
the engine killed, the app restarted the engine and offered the rescue
copy with the edits; close with unsaved changes asks and cancel keeps the
window; every row of the keyboard map fires its action (one e2e over the
table); settings and layouts survive a restart. Windows by hand.

### Stage 7: exports
- **Image** (the view at 1x and 2x, PNG), **vector image** (three.js's
  `SVGRenderer` over the flat mesh), **GLB animation** (`playback.track`
  keyframes and `playback.mesh` per piece through three.js's
  `GLTFExporter`, the original's `gltfexport.cpp` as the reference for
  the holds and reassembly), **STL** from the flat mesh as an interim
  (the bevelled STL meshes are a later core feature; say so in the
  dialog), the **paused solver state** (stage 6). Puzzlecad export waits
  for the core (its text needs the grid's cell geometry rules).

**Gate.** The GLB passes the glTF validator and, at sampled times, places
each piece where the player does; the PNG and SVG of a fixture shape
match the view (reference images); the STL of a one-cell cube is a closed
surface and a fixture shape's STL loads in a viewer.

### Stage 8: packaging
electron-builder for macOS (arm64, x64), Windows and Linux; `bt-engine`
from the release preset in the bundle's resources (the host finds it
there, or `BT_ENGINE`); one version for the app and the engine
(`CMakeLists.txt`'s, read at build); `just web-dist`; the Electron
security checklist (sandbox, context isolation, no remote content, a
CSP, no `shell.openExternal` on untrusted strings, file paths only from
dialogs and drops) as a unit test over the window options; signing and
notarisation documented, not automated; a smoke e2e against the packaged
app.

**Gate.** The packaged app on this Mac opens and solves a fixture;
`bt-engine --version` in the bundle equals the app's version; the
security test passes; Windows and Linux builds made and run once by hand.

## What the API lacks for webui-spec.md

Checked method by method against webui-spec.md and the mockup on
2026-10-08 (stage 8's handoff asked for this audit). "Client" means the
front end does it without the engine; "later" means a core feature of
PLAN.md's "Later" list.

| Need (webui-spec.md) | API today | What to do |
|---|---|---|
| Draw an assembled solution, an explorer's placement or a movement position (§5, §7) | `shape.mesh {transformation}` gives the turned mesh; **no reply gives the turned shape's hotspot**, so the client cannot place it | **Added** in stage 4: `hotspot` (a `Cell`) in `MeshFrame`; the client places the mesh at the cell minus it, scaled by the grid's cell spacing (`extent.ts`'s `cellScale`, checked against the player's poses) |
| 3D edit mode on non-cube grids (§3) | the mesh names the cell and side of each triangle; nothing gives the cell across a side | **Added** in stage 2: `grid.neighbour {size, cell, face}` over `Voxel::getNeighbor`; cubes use a client table (checked against it) |
| "Cannot be disassembled" against "Disassembly not checked" (§5) | `SolutionRow.disassembly` is `none` for both | Decided in stage 4: the core records no failed take-apart (a solve with take-apart keeps only what comes apart), so the client remembers what an analysis of this session took and could not take apart (web-4.md) |
| Mirror and span on non-cube grids (§3, "to be decided") | `shape.setCells` takes the axes on any grid | Verified in stage 2: the core applies them on every grid (cells the grid lacks left out); shown everywhere (web-2.md) |
| "Drop below level" (§5) | does not exist in the original; `keepEach` keeps every n-th | The option becomes "Keep every n-th" |
| Apply grid size to all shapes (§3) | `shape.resize toAll` grows the others, never shrinks (the original's rule) | Keep; the tooltip says so |
| Menus: New, Open, Save, Save As, recent, notes, status window, about (§7.1) | `document.*`, `shape.table`, `engine.info` | Client (stage 6) |
| Import PuzzleSolver3D, Puzzlecad; Convert grid type (§7.1) | none | Later (core) |
| Export: image, vector image, GLB (§7.1) | `playback.track`, `playback.mesh` | Client (stage 7) |
| Export: STL (§5, §7.1) | the drawing and flat meshes; the STL meshes (`cubemesh`, `minkmesh`) are not ported | Interim from the flat mesh (stage 7); the real one later |
| Export: Puzzlecad (§7.1) | none | Later |
| Paused solver state import and export (§7.2) | `solve.importPaused`, `exportPaused` | Client (stage 6) |
| This fork's solver options (§7.2) | `SolveOptions` has them all but High Memory | High Memory waits for the sliding solvers |
| Abort separate from Pause (§7.2) | `solve.abort`, refused once a run has ended | Grey the button out as the original did |
| Autosave every 20 minutes (§7.2) | `solve.savePoint` then `document.save` then `solve.continue` | Client policy (stage 4) |
| Debug statistics panel (§7.2) | `solve.status.stats` | Client (stage 5) |
| Sliding and stacking puzzles (§7.3) | open, edit and save; solving refused (`unsupported`); no walls, goals, rods | Show with the solver disabled; the rest later |
| Anaglyph display (§7.4) | none | Client, three.js's anaglyph effect (cheap; decide in stage 6) |
| Render styles (§7.4) | drawing, wireframe, flat | STL-like waits for the STL meshes |
| Colour-constraint colour view (§7.4) | per-triangle `colour` in every mesh | Client (stage 1b) |
| Transform sets of rhombic and tetra-octa grids (§7.5) | `shape.taskSet` gives each grid's buttons with labels | Client renders what the engine lists |
| XZ and YZ planes on non-cube grids (§7.5) | `grid.geometry` draws XY layers | Cube grid only, as the mockup; the original had no more |
| Many pieces in the Puzzle scene (§4) | one `shape.mesh` call per shape | Fine; a batched `shape.meshes` only if profiling asks |
| A save's cleanliness | `document.save` returns no `dirty` (stage 7's note) | Trust the spec, or `document.get` after; no change |
| Keyboard map, density, theme, layouts, recent files, settings beyond undo depth, threads and solver | engine settings cover three | Client |

Everything else in webui-spec.md sections 1 to 6 maps to a method or an
event of docs/API.md; the stage details name them.

## Finishing a front-end stage
Every stage ends with, in this order:
1. `just web-check`, `just web-test` (with the release engine built, so
   the engine tests run), `just web-e2e`, each three times (a flaky e2e
   is a bug of the stage); after a core or engine change, the C++
   finishing steps of AGENTS.md for what changed (`just test-all`, the
   contract tests, `python3 api/gen_api_md.py`, the oracle tiers it
   could touch), then `just web-gen` and the staleness test.
2. The reference screenshots reviewed when they changed.
3. `docs/ARCHITECTURE.md` updated (the `web` section: what exists).
4. `docs/handoff/web-N.md`: what was done, gate results, what the mockup
   did differently and why, known issues, the brief for the next stage.
5. This plan: tick the stage.
6. One commit for the stage (message: `Web N: <summary>`); engine
   additions in their own commits before it (`Engine: <method>`).

**Rules for the front end** (to go into AGENTS.md in stage 1a): the engine
is the truth: the UI never computes what the engine can answer (cells,
fits, counts, meshes, sorts); every mutating call waits for its revision;
`invalid_state` is "pause first", never retried; the renderer never sees
Node; frames are never parsed in the renderer by hand (the client's views
only); no state the engine holds is duplicated except as a mirror
refreshed by `document.changed`; keys come from the one table; the
mockup's wording is kept unless the original's is more exact; derived
files cite their source.

## Models, effort and audits

The parts, what makes each one hard, and the model and effort to run it
at. "Audit" means a separate review by the highest available model (Fable
5.1) at max effort, given the diff, the gate results and this plan,
reporting findings with concrete failure scenarios, fixed in a commit of
their own, as the core's audits were.

| Part | Stage | Why this level | Model, effort | Audit |
|---|---|---|---|---|
| The engine client and the types | 1a | A byte-level state machine (frames split across chunks, zero-copy views), request pipelining, the event order every later stage relies on; bugs here show up later as corrupt meshes or hung waits, not as failures of this stage | Opus 5.5, extra high | **Yes**: everything sits on it |
| The shell and the 3D view | 1b | A large but mechanical port of the mockup plus the mesh translation (flags, styles, picking) and camera feel; judgement, not subtlety | Opus 5.5, high | No; a review at high of `mesh.ts` and `picking.ts` is enough |
| The Entities workspace | 2 | Dense interaction (drags as one undo step, modifiers, planes, five grids' geometry) against a complete API | Opus 5.5, high | No; the drag batching and the frozen-state handling get a focused review |
| The Puzzle workspace | 3 | Forms, lists, drag and drop and a scene layout over a complete API; little that can go silently wrong | Opus 5.5, medium (Sonnet 5.5 at high is acceptable) | No |
| The Solver workspace and the player | 4 | The event order, paging by `solutionsVersion`, the abort and save-point semantics, several problems running at once, interpolation of poses, and a change to the core with a test to adjust | Opus 5.5, extra high | **Yes**: the state machine and the core change |
| The explorers and dialogs | 5 | Tree and table views over handles; the hotspot placement is the one subtle point | Opus 5.5, medium (Sonnet 5.5 at high is acceptable) | No |
| Menus, files, recovery | 6 | Where data is lost: close and quit paths, save refused while solving, the rescue copy, a dead engine | Opus 5.5, high | **Yes, short**: the save, recover and close paths only |
| Exports | 7 | Library work (glTF, SVG, STL) with clear checks | Opus 5.5, medium | No |
| Packaging | 8 | Configuration; the security surface matters more than the code | Opus 5.5, medium | **Yes, short**: the Electron security surface before the first release |
| Engine additions (`hotspot`, `grid.neighbour`, maybe `inseparable`) | 2, 4 | Small, additive, covered by the contract tests | Opus 5.5, high | No |

Fable 5.1 is reserved for the audits: three full ones (stages 1a, 4 and
the release's security surface) and one short one (stage 6's file
paths). None of the parts needs it to implement; the two extra-high parts
need a model that keeps a protocol's rules in mind across a long session,
which Opus 5.5 does. Sonnet 5.5 can take stages 3 and 5 if a cheaper run
is wanted; it should not take 1a, 4 or 6.

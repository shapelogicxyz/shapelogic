# Handoff: web stage 1b (the shell and the 3D view)

**Date:** 2026-10-08 · **Next:** stage 2, the Entities workspace.

## Done

### The shell (`web/src/renderer/shell/`, `index.html`, `styles.css`)
Ported from docs/mockup/index.html, each file naming the functions it
came from: the page (the mockup's body: top bar, workspace rail, the six
cards, the viewport with its toolbar, status bar, settings dialog,
popover) and its stylesheet, kept in the mockup's compact form so it can
be compared line by line (Prettier leaves `styles.css` alone). `layout.ts`
(`applyLayout`, `setTab`, `LAYS`, `renderRail`: cards collapse to rails,
the 3D view takes the focus, each workspace keeps its layout),
`settings.ts` and `prefs.ts` (`SET`, `rowHtml`, `renderSettings`,
`openSettings`: searchable, three pages and the shortcuts, Reset section,
Restore all defaults; undo depth and threads go to `settings.set` and are
mirrored from the engine, the threads' range is the machine's; Tab stays
in the dialog), `theme.ts` (`applyTheme`, `applyDensity`,
`svDensityTitles`, and "Show tooltips", which the mockup only listed:
titles are held back while it is off), `status.ts` (`flash`,
`updateStatus`, the cursor; engine news ends a flash; `invalid_state`
shows as "Pause the solve first"), `escape.ts` (Esc's order: dialog,
menu, rename, playback, selection, focus; views register their steps),
`popover.ts` (`openPop`, `openPopAt`, `closePop`, `popOpen`),
`confirm.ts` (`askConfirm`), `topbar.ts` (file name, its path on hover, a
dirty mark; a temporary **Open…** and drop onto the window, by
`document.openBytes`, until stage 6's File menu), `icons.ts` (`I`, `ic`),
`cursors.ts` (`badgeSvg`, the toolbar icon of `ptrSvg`), `app.ts`
(puts it together; the Entities status line of `updateStatus`).

**The keyboard map is one table**, `shell/keys.ts`: every row of
webui-spec.md section 6 in the mockup's help wording (`HELP`), with the
chords of the rows that act now and their actions; the settings dialog's
shortcuts page (F1) prints it; `keyboard.ts` dispatches from it (Ctrl or
Cmd, a physical key as well, Shift free unless named; keys go to text
fields; a modal dialog keeps them). A unit test reads section 6 of
webui-spec.md and fails when a key of it is missing from the table.

**Piece colours:** `shell/piece-colours.ts` is the original's
`src/gui/piececolor.cpp` (the 18-colour table, the sine formula after it,
the jitter of a piece's copies, dark, light, contrast), checked against
values printed by a probe compiled against that file.

### The 3D view (`web/src/renderer/view3d/`)
- `mesh.ts`: a `MeshFrame` through `meshArrays()` into a non-indexed
  `BufferGeometry`; the per-triangle flags, colour number and cell (from
  `voxel` and the shape's size) expanded per vertex, so the colour view,
  lighting, x-ray and the dimmed layers are uniforms (no rebuild); the
  wire-frame style keeps the flag-8 faces, with a map from drawn triangle
  to frame triangle; a variable cell's faces (flag 2) get the black inner
  polygon of the original's `drawShrinkTriangle` /
  `drawShrinkQuadrilateral` (SHRINK 0.13, MY 0.005), the polygons rebuilt
  from the engine's fans; the flat style's `edgeLines`.
- `material.ts`: the faces' shader, colours and light as
  `voxelFrame_c::drawShape` and `draw()`: the colour view's constraint
  colour, the variable marker black, the checker's light faces
  `lightPieceColor`, the rest `darkPieceColor`; lit as GL_COLOR_MATERIAL
  with global ambient 0.2, a light of ambient 0.2 and diffuse 0.6 at
  (700, 200, 700) in eye space: `c · (0.4 + 0.6 · max(0, n·l))`; written
  without colour management, as OpenGL did. X-ray is the original's
  `insideVisible` (inside faces opaque, the others at 0.1 through).
- `camera.ts`: one orientation quaternion; the mockup's yaw, pitch and
  roll are its Euler angles (Z, X, Y). The mockup's Drag orbit (half a
  degree a pixel, pitch held in ±89°, the roll respected), pan, smooth
  wheel zoom (0.3 to 4, reversed by the setting), Home and F animated
  (cubic ease, 360 ms, slerp); the **Arc-ball** method is the original's
  `arcBall_c` (the mockup had only the setting).
- `renderer.ts`: three.js at the display's pixel ratio, resized with the
  viewport, rendering only when asked; the perspective camera 64 units
  away (the mockup's `64/(64 − z)`) and the orthographic one framed by the
  mockup's `sc`; the pan as a lens shift (`setViewOffset`), as the mockup
  shifted its picture. Each render is a `performance.measure`.
- `viewcube.ts`: the mockup's canvas cube (`cubeGeo`, `cubeHit`,
  `solveAB`, `cubeAligned`, `renderCube`, `rollIcon`, its handlers) over
  the camera's quaternion.
- `picking.ts`: a `Raycaster` hit's `faceIndex` → the frame's triangle →
  `voxel` and `voxelFace`: the status bar says `Cell x, y, z · side n`
  (`· edge` for bevels and gaps) under the mouse and on a click.
- `overlays.ts`: axes, the space's bounds, the editor layer's slab (one
  unit on cubes; on the other grids the layer's cells as the mesh has
  them). `extent.ts`: a space's size in mesh units (the original's
  `calculateSize`), what the view centres and frames.
- `scene.ts` (one shape), `meshcache.ts` (by shape, style and
  transformation with the revision asked at; coalesced by
  `mesh:<shape>:<style>:<transformation>`; dropped by what
  `document.changed` names, everything on `colours`), `toolbar.ts` (Orbit,
  Pan, Edit (disabled until the editor), Display, Focus; the Display menu
  with the original's three styles and x-ray added; Minimal's hiding
  toolbar), `view.ts` (glue; the empty message; the view's state as
  `data-shown`, `data-camera`, `data-moving`, `data-triangles`,
  `data-projection` on `#vp`).

### State, main process, toolchain
- `state/store.ts`: `Store` waits for `running` before its first call,
  loads `engine.info`, `settings.get` and the snapshot, loads again when
  `restarts` grows (and says so: "The engine was started again: a new,
  empty document"), reloads the snapshot on `document.changed` (the newest
  load wins), counts documents (`generation`) so the old one's ids are not
  taken for the new one's meshes. `entities/shape-picker.ts`: the
  temporary shape picker.
- `main/index.ts`: a main-frame navigation disposes the window's bridge
  server and serves a new one (the audit's known issue); in development
  the renderer's warnings and errors are echoed to the terminal.
- three.js 0.186.1 (a dependency), `@types/three` 0.186.0 and
  `@playwright/test` 1.64.0 pinned, the lockfile updated;
  `tsconfig.e2e.json` (the e2e tests run code in the page: DOM types);
  `playwright.config.ts`; `just web-dev`, `just web-e2e` (both clear
  `ELECTRON_RUN_AS_NODE`: an editor's terminal sets it, and Electron then
  runs as Node: "does not provide an export named 'BrowserWindow'"). The
  CSP is unchanged and allows the bundle (and the dev server: `just
  web-dev` ran with no renderer warning).

### Tests
- Unit (`tests/unit/renderer/`, 29): the keyboard table against
  webui-spec.md section 6, its chords and groups; the piece colours
  against the original's; `mesh.ts` (expansion, the wire-frame filter and
  the pick mapping, the marked polygons from the fans, the shrink against
  hand values); extents and slabs; the camera (angles, Drag, the pitch
  clamp, the roll, `dirToView`, the arc-ball's points and turn, Home, F,
  the zoom's range, the framing); the store (no call before `running`,
  restart, newest load, open by bytes, errors) and the mesh cache.
- e2e (`tests/e2e/`, 21, Playwright's Electron driver on the built app with
  the release engine): `ipc` (a mesh across the bridge as a frame; the
  engine killed with SIGKILL: "The engine stopped (SIGKILL); starting it
  again…", a new engine process, an empty document, and it works; a
  reload with 300 slow calls in flight whose ids the new page reuses, the
  lowest answered last: the new page loads its own replies), `view` (all
  170 shapes of the 19 fixtures in the three styles; every fixture unlit,
  orthographic and in its colours; 36 reference screenshots: one shape of
  each grid and the bricks with colours, each drawn, wire frame, flat,
  unlit, orthographic, in its colours), `picking` (every fixture: the view
  turned by the cube's +Z face, orthographic; a face chosen from the
  frame's arrays of the test's own engine as the front-most triangle at a
  point; the click names its cell and side), `keys` (Ctrl+1/2/3, Ctrl+,,
  F1, Ctrl+[ ], the layout per workspace, Ctrl+Space, Esc's order, O/P,
  the Drag orbit, the wheel, Shift-drag pan, F, Home, the Arc-ball;
  theme, density, tooltips, undo depth and threads at the engine), `perf`.
- Checked that they catch what they claim: the reload test fails without
  the re-serve in `main/index.ts` (shape list never loads); the first
  version of it passed without, and was made to send the old calls with
  the lowest ids last.

## Gate results
| Check | Result |
|---|---|
| `just web-check` | clean, three times: three TypeScript projects (node, web, e2e), ESLint (no warnings), Prettier |
| `just web-test` (release engine) | **150/150**, three times, ~14 s each: 16 files (stage 1a's 121, and `unit/renderer/` 29) |
| `just web-e2e` | **21/21**, three times, ~101 s each (build included): ipc 3, view 8, picking 1, keys 8, perf 1 |
| perf (each run) | mesh build 5.8, 5.6, 5.4 ms; frame 13.4 ms idle, orbit median 13.3/13.4/13.4 ms, p95 14.8/14.6/14.7 ms; 149–150 renders in 150–152 frames; render median 0.2 ms, p95 0.3–0.4 ms |
| reload test without the fix | fails (the new page's shape list never loads) |
| `just web-dev` | starts the dev server and the app; no renderer warning or error |
| C++ | unchanged (no engine addition was needed) |

## The reference screenshots
`web/tests/e2e/__screenshots__/` (36 PNGs, CSS pixels of the viewport,
1280 × 800 window; compared with `maxDiffPixelRatio` 0.01 and
`threshold` 0.15). Looked at as contact sheets: bevels and the checker on
every grid, the black variable marks of CubeInCage's result, the flat
style's black edge lines, the wire frame's bars, the constraint colours
of Dracula's and the Augmented Second Stellation's shapes in the colour
view, unlit flat colours, the orthographic framing. **The look beside the
original GUI could not be taken here:** the original (`build/burrtools`)
started, but screen capture from this session gives a black picture (no
Screen Recording permission for the terminal). The colours and the light
are ported from its code (`voxelFrame_c::drawShape`, `draw()`); someone
should open CubeInCage and DraculasDentalDesaster in both once and compare
(the orientation differs by design: the mockup's home view). If the
screenshots change, `just web-e2e --update-snapshots` after a look.

## Where this stage departs from the plan, and why
1. **The wire-frame style draws the flag-8 faces filled**, not "as
   lines": they are the bars of the frame (the original's `gridline` mode
   draws them as triangles); lines would outline every bar.
2. **The colour view, lighting, x-ray and dimming are uniforms** of one
   shader, and each style is its own mesh from the engine (the cache has
   all three): switching costs no rebuild.
3. **The space's extent is a client table** (`extent.ts`, five formulas
   of the original's `calculateSize`): no reply gives a space's size in
   mesh units, and the view must frame the space, not the cells. A
   `MeshFrame` field could carry it when stage 4 adds `hotspot`.
4. **The layer slab of an empty layer on the non-cube grids** is the
   space's height divided evenly (no cell gives its place).
5. **The temporary Open… button and drop** read the file in the renderer
   and send its bytes: the renderer has no paths, and a dialog over IPC
   is stage 6's.
6. **The view's state is readable as `data-*` attributes** of `#vp` and
   `#stage[data-engine]`, which the e2e tests wait on, instead of a test
   hook.
7. **Engine status in the status bar** ("Starting the engine…", "The
   engine stopped (SIGKILL); starting it again…", "The engine could not be
   kept running: …") and the empty message say what the mockup could not.

## What the mockup did differently, and why it changed
- A 1600 × 1000 board scaled to the window: the stage fills the window
  (popovers are placed in window coordinates).
- Its hand-projected canvas (`render3d`, `FACES`, `cubeGeo` for shapes,
  the mock geometry of the non-cube grids, `inPoly` picking): three.js
  over the engine's meshes and a raycast.
- Mock-only controls (voxel type, outcome, density selects, "Show
  zones"), the menu placeholders: gone (menus are Electron's, stage 6).
- The Arc-ball setting did nothing: it is the original's arc-ball.
- "Show tooltips" did nothing: it holds the titles back.
- "Use OpenGL display lists": dropped (webui-spec.md section 1).
- "Dim other layers" made faces transparent (0.28): they are lightened as
  the original dimmed a piece, opaque (no sorting artefacts).
- The Display menu offered "Show through solid pieces" in the Solver only:
  here in every workspace, with the original's three styles.
- Animations interpolated yaw, pitch and roll: a quaternion is slerped.
- Its short `KEYS` list (an older copy of the help) is gone; `HELP` is the
  table. Keys of a row are split at a "+" followed by a key, so "+" itself
  shows.
- The threads slider ran 1 to 32 with a default of 12: 1 to the machine's
  hardware threads, the engine's default (0, every thread).

## Performance (measured by `perf.e2e.ts`, release engine, this Mac)
DraculasDentalDesaster's S10 (1,656 cells, 25,008 triangles, the largest
fixture mesh): the mesh build (`buildMesh`, frame to geometry) 5.4 to
5.8 ms (under 10 ms). Orbiting it for two seconds: the display's frame
13.4 ms idle, 13.3 to 13.4 ms median during the orbit (p95 at most
14.8 ms), about 150 renders in 150 frames (one per frame), a render
0.2 ms median (CPU side; 0.4 ms p95 at most). Numbers of the gate's three
runs in the table above.

## Known issues and notes
- The one look beside the original GUI is still to be taken (above).
- Settings and layouts live in memory until stage 6 stores them.
- A document opened by bytes shows its file's name; a reload of the page
  (development) keeps the engine's document but shows it as "Untitled".
- The sphere grid's wire frame shows the balls' strips with gaps between
  them, as the engine's wire-frame mesh has it (not compared with the
  original).
- The light is the original's positional light taken at its eye-space
  point per fragment; the original's camera distance is not modelled.
- The e2e tests open windows on the screen, one at a time (about 2
  minutes for all); the screenshots are in CSS pixels but were taken on
  this Mac's fonts and GPU: another machine may need `--update-snapshots`.
- The Edit button is disabled until the editor (stage 2).

## Brief for the next stage

**Stage 2** (Opus 5.5, high), FRONTEND-PLAN.md's "Stage 2". Where things
are:
- Replace `entities/shape-picker.ts` with the shapes list (the mockup's
  `renderShapes`, `shapeAction`, the list's keydown handler); keep
  `store.select()`, which the view follows. The `.lrail` of the left card
  is filled by the picker today.
- The 2D editor goes in the right card (`#right .rfull`, now a
  placeholder) and its rail (`#rrail`, filled by `layout.ts`). The view
  has `plane` and `layer` fields (`view3d/view.ts`) for the slab and the
  dimming: add a setter that calls `lookChanged()`.
- Keys: add the Entities rows' chords to `shell/keys.ts` (1–4, E,
  PgUp/PgDn, F2, Del, Ctrl+D; the list's ↑/↓ and Alt+↑/↓) with new
  `KeyAction`s, and `onKey` handlers; Esc steps `ESC_ORDER.rename` and
  `.selection` exist.
- 3D edit: `picking.ts` gives the cell and side; enable the Edit button
  in `toolbar.ts` (`renderEdit`); the tool cursors are the mockup's
  `cursorFor` (data-URL SVG cursors: the CSP then needs
  `img-src 'self' data:`).
- Mutations wait for their revision; the store reloads the snapshot on
  `document.changed` and the mesh cache drops the shapes it names. For a
  drag, reloading the whole snapshot per batch may be too much: consider
  `shape.get` of the shape alone.
- The engine addition `grid.neighbour` is its own commit
  (`Engine: grid.neighbour`, api/README.md's "Adding a method"), then
  `just web-gen`.
- e2e helpers: `tests/e2e/app.ts` (`launch`, `openFixture`, `shapes`,
  `selectShape`, `display`, `style`, `setting3d`, `settle`, `camera`).

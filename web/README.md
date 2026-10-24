# ShapeLogic: the web front end

An Electron + TypeScript application that drives `shapelogic_engine` over JSON-RPC
(decisions 0003, 0012 and 0013), built in stages on Girish Sharma's mockup
(`../docs/mockup/`). The plan is `../docs/FRONTEND-PLAN.md`; what exists is
the `web` section of `../docs/ARCHITECTURE.md`.

**Status:** stage 8: the toolchain, the types generated from `../api/`,
the engine client with its host, bridge and proxy, the app's shell
(top bar, workspace rail, cards, status bar, settings, keyboard map), the
3D view in three.js, the Entities, Puzzle and Solver workspaces with the
player and the explorers, and Electron's menus: File (New, Open, Open
Recent, Save, Save As, Import PuzzleSolver3D and Puzzlecad, the paused
solver state, Close), Edit (Undo and Redo, Notes, Convert Brick Grid Type
to Other, Convert Assemblies to Pieces, Settings), View (the status
window, the display modes and styles), Help (the keyboard shortcuts on
F1, the user guide, About). The settings and layouts are kept in the
app's data directory, with the rescue copy of a puzzle with unsaved
changes (`__rescue.xmpuzzle`), offered after the engine or the app died.
The exports (stage 7): image, vector image, STL (the engine's STL meshes
since deferred part 5, with Solutions ▸ Export to STL), solution animation,
Puzzlecad (deferred part 6).
Packaging (stage 8): electron-builder for macOS, Windows and Linux, the
engine inside the app, the Electron security checklist.

## Running

Needs Node 22 (`brew install node@22`) and, for the engine tests, the
release engine (`just build release`). From the repository's root:

```bash
just app-install   # npm ci: the pinned packages
just app-gen       # src/engine/api.generated.ts from api/*.json (after a schema change)
just app-check     # type checks (node and renderer projects), ESLint, Prettier
just app-test      # unit tests; the engine tests too when the release engine exists
just app-dev       # the app (electron-vite dev, reloads on change)
just app-e2e       # build, then the end-to-end tests (Playwright's Electron driver)
```

`just app-e2e -g picking` passes arguments to Playwright;
`--update-snapshots` records the reference screenshots again (look at them
first: `tests/e2e/__screenshots__`). The e2e tests open windows on the
screen, one at a time. Both recipes clear `ELECTRON_RUN_AS_NODE`, which
some editors' terminals set and which turns Electron into plain Node.

Inside `web/`: `npm run format` reformats, `npm run record` records the
fake engine's sessions again (after a change to what the engine sends),
`npm run build` bundles with electron-vite into `out/`.

## Packaging

```bash
just app-dist          # the installers for this machine: macOS .dmg and .zip, Windows NSIS, Linux AppImage and .deb
just app-dist --dir    # the unpacked app only (dist/mac-arm64/, dist/win-unpacked/, dist/linux-unpacked/)
just build release-x64 && just app-dist --arch x64   # an Intel Mac's app, on an Apple silicon Mac
just app-smoke         # the smoke test of the unpacked app (after `just app-dist --dir`)
```

`tools/dist.ts` builds for the platform it runs on only (the engine is
built natively); it puts `build/release/engine/shapelogic_engine` (on macOS
`build/release-<arch>/` for the other architecture) into the app's
resources, after checking its architecture and that `--version` says
CMakeLists.txt's version, which the app takes too (read at build). The
output is in `dist/`. `app-smoke` starts the unpacked app as a user would,
with a fixture on its command line, and drives its page over Chromium's
DevTools protocol (the app's fuses turn `--inspect` off, so Playwright's
Electron driver cannot reach it): the fuses, the archive, the engine's
version, the development switches ignored, the page on the app's own
scheme (`bt-app://app/`, served from the renderer's directory only) with
its CSP, a file: script refused, no file read outside the renderer, a
path no dialog gave refused, the fixture solved.
`BT_PACKAGED_APP=<executable>` tests another copy (an installed one).

**Signing.** On macOS the app is signed ad hoc (it runs on the machine
that built it; elsewhere Gatekeeper refuses a downloaded copy until the
user allows it in System Settings ▸ Privacy & Security). With a Developer
ID certificate in the keychain and `CSC_NAME="Developer ID Application:
…"` (or `CSC_LINK`, `CSC_KEY_PASSWORD` for a .p12), electron-builder signs
with the hardened runtime, and notarises when `APPLE_API_KEY` (the .p8
file), `APPLE_API_KEY_ID` and `APPLE_API_ISSUER` are set. On Windows the
installer is unsigned (SmartScreen warns) unless `CSC_LINK` (or
`WIN_CSC_LINK`) and `CSC_KEY_PASSWORD` name a code-signing certificate.
docs/handoff/web-8.md has the steps by hand. In CI, Release
(`.github/workflows/release.yml`) signs the macOS builds and sends the
`.dmg` for notarisation without waiting for Apple; Staple (`staple.yml`,
given that run's id) staples the tickets once Apple has accepted them.

## The website's demo

`just site-demo` builds the app for the browser into `../site/public/demo/`
(served at shapelogic.xyz/demo/): the engine compiled to WebAssembly by
the `wasm` CMake preset (`../engine/wasm_main.cpp`; needs Emscripten,
`brew install emscripten`), then `npm run demo` (`tools/build-demo.ts`)
copies it into `src/demo/engine/` and builds the renderer's page with
`src/demo/main.ts` as its entry (`demo.vite.config.ts`). In the page,
`src/demo/wasm-transport.ts` is the engine's stdio, `engine-bridge.ts`
serves the client to the renderer as the main process does (it refuses a
search: an analysis, an explorer or another problem's solve would hold
the page's one thread), and `demo-app.ts` stands in for Electron's menus,
dialogs and files. The demo opens `../site/public/examples/PelikanBurr.xmpuzzle`
and takes every edit; nothing is kept. Its problem's Solve plays a run
without a search (the bridge answers `solve.start` and `main.ts` sends a
run's events for about three seconds), then opens the puzzle again with
the file's pieces and problem (its saved solution) among the visitor's
other shapes, problems and colours, spliced into the saved document. `manifest.json` records a hash of the
demo's inputs: `site/tests/demo.test.mjs` (run by `just site-build`)
fails when the app changed since, so the demo is rebuilt and committed
with the change. `just site-demo --check` drives the built demo in Google
Chrome (`--headed` shows it).

## Where the engine comes from

- The tests run `build/release/engine/shapelogic_engine`, or `BT_ENGINE` when it is
  set, from the repository's root (fixtures are opened by relative path).
  Without one the engine tests are skipped and the run says so; so are the
  e2e tests, which also need `npm run build` (`just app-e2e` builds).
- The app in development (`src/main/engine-path.ts`) runs `BT_ENGINE`,
  else `build/dev/engine/shapelogic_engine`, else the release one. A packaged app
  runs the copy in its resources and nothing else: it ignores `BT_ENGINE`,
  `BT_CALL_LOG`, `BT_AUTOSAVE_SECONDS` and electron-vite's
  `ELECTRON_RENDERER_URL` (`src/main/security.ts`); only `BT_USER_DATA`
  is taken. Either way the engine must say the app's version.

## Layout

- `src/engine/`: the client, platform-free: `framing.ts` (stdout to
  messages and frames), `frames.ts` (typed-array views), `client.ts`
  (`EngineClient`), `dispatch.ts` (replies before their events),
  `client-types.ts`, `errors.ts`, `bridge.ts` (what crosses IPC),
  `recorder.ts`, `transport.ts`, `api.generated.ts`; and
  `node-transport.ts` (the child process, Node only).
- `src/main/`: Electron's main process: `engine-host.ts` (one engine,
  restarted when it dies), `engine-ipc.ts` (the host's side of the bridge),
  `engine-path.ts`, `security.ts` (the window's options, the switches a
  packaged app takes, the requests and permissions allowed, the help
  links), `path-grants.ts` (the files a window's engine may be given),
  `app-ipc.ts`, `menu.ts`, `export-file.ts`, `rescue.ts`,
  `state-store.ts`, `index.ts` (with `BT_CALL_LOG=<file>` every call of the
  renderer is logged there with how it ended; the e2e tests read it;
  `BT_AUTOSAVE_SECONDS=<n>` shortens the Solver's autosave from 20
  minutes, for the e2e tests).
- `src/preload/`: `window.bt` and `window.btApp`.
- `src/renderer/`: `index.html` and `styles.css` (the mockup's body and
  stylesheet), `main.ts`, `engine-proxy.ts` (the client's interface over
  `window.bt`); `shell/` (the app's frame: `app.ts`, `keys.ts` the one
  keyboard table, `keyboard.ts`, `layout.ts`, `settings.ts`, `prefs.ts`,
  `theme.ts`, `status.ts`, `escape.ts`, `history.ts` (undo and redo),
  `popover.ts`, `confirm.ts`, `topbar.ts`, `icons.ts`, `cursors.ts`,
  `piece-colours.ts`, `dom.ts`),
  `state/` (`store.ts` the mirror of the engine, `edits.ts` the edits
  under way, `solves.ts` the solve records), `view3d/` (the 3D view:
  `view.ts`, `renderer.ts`, `camera.ts`, `viewcube.ts`, `mesh.ts`,
  `material.ts`, `scene.ts`, `pieces.ts`, `solution.ts`, `overlays.ts`,
  `picking.ts`, `meshcache.ts`, `extent.ts`, `toolbar.ts`, `preview.ts`),
  `entities/` (the Entities workspace: `workspace.ts`, `shapes-list.ts`,
  `inspector.ts`, `editor-card.ts`, `grid2d.ts`, `layer-geometry.ts`,
  `shape-cells.ts`, `stroke.ts`, `edit3d.ts`, `tools.ts`,
  `editor-state.ts`, `glyphs.ts`), `puzzle/` (the Puzzle workspace),
  `solver/` (the Solver workspace: `workspace.ts`, `run-card.ts`,
  `solutions-card.ts`, `player.ts`, `poses.ts`, `pager.ts`, `settings.ts`,
  `autosave.ts`).
- `tools/`: `gen-api.ts` (the types), `sessions.ts` and
  `record-sessions.ts` (the recorded sessions), `dist.ts` (the packaged
  app), `project-version.ts` (CMakeLists.txt's version).
- `packaging/`: the icons (the original's).
- `fake-engine/`: `fake-engine.ts` (a recording replayed as a transport),
  `recordings/`.
- `tests/`: `unit/` (`unit/renderer/` for the app's pure parts), `engine/`
  (against the real engine), `e2e/` (Playwright: `app.ts` launches the
  built app and calls its engine through the page's bridge, `editor.ts`
  drives the voxel editor, `puzzle.ts` the Puzzle's cards, `solver.ts` the
  Solver's; `packaged.smoke.ts` the packaged app's smoke test;
  `__screenshots__/` the reference screenshots),
  `helpers/`.

## Rules

See AGENTS.md, "The web front end": the engine is the truth; every
mutating call waits for its revision; `invalid_state` is "pause first";
the renderer never sees Node; frames are never parsed by hand; files taken
from the mockup cite it.

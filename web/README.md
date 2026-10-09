# BurrTools Alpha: the web front end

An Electron + TypeScript application that drives `bt-engine` over JSON-RPC
(decisions 0003, 0012 and 0013), built in stages on Girish Sharma's mockup
(`../docs/mockup/`). The plan is `../docs/FRONTEND-PLAN.md`; what exists is
the `web` section of `../docs/ARCHITECTURE.md`.

**Status:** stage 6: the toolchain, the types generated from `../api/`,
the engine client with its host, bridge and proxy, the app's shell
(top bar, workspace rail, cards, status bar, settings, keyboard map), the
3D view in three.js, the Entities, Puzzle and Solver workspaces with the
player and the explorers, and Electron's menus: File (New, Open, Open
Recent, Save, Save As, the paused solver state, Close), Edit (Undo and
Redo, Notes, Convert Assemblies to Pieces, Settings), View (the status
window, the display modes and styles), Help (the keyboard shortcuts on
F1, the user guide, About). The settings and layouts are kept in the
app's data directory, with the rescue copy of a puzzle with unsaved
changes (`__rescue.xmpuzzle`), offered after the engine or the app died.
The exports come in stage 7, packaging in stage 8.

## Running

Needs Node 22 (`brew install node@22`) and, for the engine tests, the
release engine (`just build release`). From the repository's root:

```bash
just web-install   # npm ci: the pinned packages
just web-gen       # src/engine/api.generated.ts from api/*.json (after a schema change)
just web-check     # type checks (node and renderer projects), ESLint, Prettier
just web-test      # unit tests; the engine tests too when the release engine exists
just web-dev       # the app (electron-vite dev, reloads on change)
just web-e2e       # build, then the end-to-end tests (Playwright's Electron driver)
```

`just web-e2e -g picking` passes arguments to Playwright;
`--update-snapshots` records the reference screenshots again (look at them
first: `tests/e2e/__screenshots__`). The e2e tests open windows on the
screen, one at a time. Both recipes clear `ELECTRON_RUN_AS_NODE`, which
some editors' terminals set and which turns Electron into plain Node.

Inside `web/`: `npm run format` reformats, `npm run record` records the
fake engine's sessions again (after a change to what the engine sends),
`npm run build` bundles with electron-vite into `out/`.

## Where the engine comes from

- The tests run `build/release/engine/bt-engine`, or `BT_ENGINE` when it is
  set, from the repository's root (fixtures are opened by relative path).
  Without one the engine tests are skipped and the run says so; so are the
  e2e tests, which also need `npm run build` (`just web-e2e` builds).
- The app (`src/main/engine-path.ts`) runs `BT_ENGINE`, else
  `build/dev/engine/bt-engine`, else the release one; stage 8 adds the copy
  inside the packaged app.

## Layout

- `src/engine/`: the client, platform-free: `framing.ts` (stdout to
  messages and frames), `frames.ts` (typed-array views), `client.ts`
  (`EngineClient`), `dispatch.ts` (replies before their events),
  `client-types.ts`, `errors.ts`, `bridge.ts` (what crosses IPC),
  `recorder.ts`, `transport.ts`, `api.generated.ts`; and
  `node-transport.ts` (the child process, Node only).
- `src/main/`: Electron's main process: `engine-host.ts` (one engine,
  restarted when it dies), `engine-ipc.ts` (the host's side of the bridge),
  `engine-path.ts`, `index.ts` (with `BT_CALL_LOG=<file>` every call of the
  renderer is logged there with how it ended; the e2e tests read it;
  `BT_AUTOSAVE_SECONDS=<n>` shortens the Solver's autosave from 20
  minutes, for the e2e tests).
- `src/preload/`: `window.bt`.
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
  `record-sessions.ts` (the recorded sessions).
- `fake-engine/`: `fake-engine.ts` (a recording replayed as a transport),
  `recordings/`.
- `tests/`: `unit/` (`unit/renderer/` for the app's pure parts), `engine/`
  (against the real engine), `e2e/` (Playwright: `app.ts` launches the
  built app and calls its engine through the page's bridge, `editor.ts`
  drives the voxel editor, `puzzle.ts` the Puzzle's cards, `solver.ts` the
  Solver's; `__screenshots__/` the reference screenshots),
  `helpers/`.

## Rules

See AGENTS.md, "The web front end": the engine is the truth; every
mutating call waits for its revision; `invalid_state` is "pause first";
the renderer never sees Node; frames are never parsed by hand; files taken
from the mockup cite it.

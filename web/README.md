# BurrTools Alpha: the web front end

An Electron + TypeScript application that drives `bt-engine` over JSON-RPC
(decisions 0003, 0012 and 0013), built in stages on Girish Sharma's mockup
(`../docs/mockup/`). The plan is `../docs/FRONTEND-PLAN.md`; what exists is
the `web` section of `../docs/ARCHITECTURE.md`.

**Status:** stage 1a: the toolchain, the types generated from `../api/`,
and the engine client with its host, bridge and proxy. The shell and the 3D
view come in stage 1b.

## Running

Needs Node 22 (`brew install node@22`) and, for the engine tests, the
release engine (`just build release`). From the repository's root:

```bash
just web-install   # npm ci: the pinned packages
just web-gen       # src/engine/api.generated.ts from api/*.json (after a schema change)
just web-check     # type checks (node and renderer projects), ESLint, Prettier
just web-test      # unit tests; the engine tests too when the release engine exists
```

Inside `web/`: `npm run format` reformats, `npm run record` records the
fake engine's sessions again (after a change to what the engine sends),
`npm run build` bundles with electron-vite into `out/`.

## Where the engine comes from

- The tests run `build/release/engine/bt-engine`, or `BT_ENGINE` when it is
  set, from the repository's root (fixtures are opened by relative path).
  Without one the engine tests are skipped and the run says so.
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
  `engine-path.ts`, `index.ts`.
- `src/preload/`: `window.bt`.
- `src/renderer/`: `engine-proxy.ts` (the client's interface over
  `window.bt`); the app from stage 1b.
- `tools/`: `gen-api.ts` (the types), `sessions.ts` and
  `record-sessions.ts` (the recorded sessions).
- `fake-engine/`: `fake-engine.ts` (a recording replayed as a transport),
  `recordings/`.
- `tests/`: `unit/`, `engine/` (against the real engine), `helpers/`.

## Rules

See AGENTS.md, "The web front end": the engine is the truth; every
mutating call waits for its revision; `invalid_state` is "pause first";
the renderer never sees Node; frames are never parsed by hand; files taken
from the mockup cite it.

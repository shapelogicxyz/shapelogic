# 0013: The web front end: Electron, TypeScript, the mockup's code

**Status:** accepted (web stage 1a); `web/`, `docs/mockup/`

## Context
Decision 0003 put the core in `bt-engine` behind JSON-RPC so a web front
end could drive it; decision 0012 settled the protocol. The front end's
plan (docs/FRONTEND-PLAN.md) builds it in stages on Girish Sharma's mockup
(docs/webui-spec.md), whose layout, wording and keyboard map the spec had
adopted but not its code. This record settles the shape of the client that
every later stage sits on, and keeps the list of the front end's deliberate
differences from the original GUI.

## Decision
**The mockup's code is the basis.** `docs/mockup/index.html` (the
`gh-pages` branch of `grssam/burr-tools`, commit `d7afb3ed`, GPL-3.0) is
vendored unmodified; its CSS, its shell and its card render functions are
taken over as TypeScript modules over the engine's data, each citing it;
its data model, voxel operations, canvas renderer, fake solver and player
are replaced by the engine and three.js. This supersedes webui-spec.md's
"its code is not" adopted. **No UI framework**: the mockup is plain DOM
with template strings that re-render a card from state; revisit after
stage 2 if that hurts.

**Layout.** `web/` beside `engine/` and `cli/`: `src/engine/` the client
(platform-free except `node-transport.ts`), `src/main/` Electron's main
process (the engine host, windows; menus and files from stage 6),
`src/preload/` the bridge, `src/renderer/` the app, `tools/` (the type
generator, the session recorder), `tests/` (`unit/`, `engine/` against the
real engine, `e2e/` from stage 1b), `fake-engine/` (recorded sessions
replayed).

**Process model.** One `bt-engine` per window, owned by an `EngineHost` in
the main process. The renderer is sandboxed (`sandbox`, `contextIsolation`
on, `nodeIntegration` off) and reaches the engine only through
`window.bt`, which the preload exposes: calls go one way (`bt:call`),
replies, events and the engine's status come back on one ordered channel
(`bt:message`), so the renderer sees decision 0012's order. A reply's frame
crosses as its description plus an `ArrayBuffer`; the renderer's
`EngineProxy` makes the same typed-array views the client makes and has
the client's interface (`EngineApi`). The host checks what the renderer
sends (a well-formed call of a schema method) before calling. When the
engine dies the host fails its calls (`disconnected`), tells the window
(`exited`, after the errors of the calls it failed, then `running` with a
restart count), and starts another after a back-off (none, then 250 ms
doubling to 5 s; after five quick deaths in a row it gives up: `failed`).
Calls made while no engine is running, or before a new one has answered
`engine.hello`, fail at once: a call queued for the next engine would
carry the old document's ids into a new one. The window reloads its
mirror on the restart (stage 6 adds the rescue copy). A proxy asks the
host for the status when it connects: the engine starts while the page
loads, so a `running` posted before the renderer listened would be lost.
Stopping the host (the window closed) ends an engine being started too,
and a status handler that throws does not stop a restart (the audit of
stage 1a).

**The client** (`src/engine/`), over a `Transport` (`write`, `onData`,
`onStderr`, `onExit`, `kill`, `close`): Node's child process now, a
recording in tests, a WebSocket later.
- *Framing*: a parser with two states, a line or the L bytes of a frame;
  chunks may split anything. A frame is copied once into a buffer of its
  own and handed out with the reply after it; a frame followed by anything
  but a reply, a reply naming a frame that did not come or a frame no reply
  names are protocol errors. A line that is not JSON, a line over the limit
  (just under V8's longest string, so a base64 document of 256 MiB of XML
  fits) and a frame over 1 GiB are dropped and reported, and the stream
  goes on; a frame announced without a size loses the stream (fatal: the
  calls fail as `protocol` and the engine is killed, so a host restarts
  it). stderr's last 64 KiB are kept for error reports.
- *Calls*: requests only, never notifications. Ids are given when a call
  is made; calls are written in order, at most 32 at the engine at once,
  the rest waiting in the client. The engine answers in order, so a reply
  for a later call fails the earlier ones still waiting (`protocol`, their
  reply was lost), and an error without an id (a line the engine could not
  read) belongs to the oldest call waiting. A call whose line is over the
  engine's 64 MiB fails without being sent, as the engine would fail it
  (`-32600 invalid_request`).
- *Order*: settling a reply only queues the code awaiting it, so the
  events behind a reply wait a macrotask (`dispatch.ts`: `setImmediate`
  where there is one, else a `MessageChannel`, since a browser holds a
  nested `setTimeout(0)` to 4 ms): the caller sees its reply before the
  `document.changed` it caused and the poll's events after it. The proxy
  keeps the same rule.
- *Errors*: `EngineError {code, errc, detail, method}`; the engine's errcs
  and codes as decision 0012 gives them (`ERROR_CODES`), and three of the
  client's own with code 0: `disconnected` (the engine ended; the call's
  outcome is unknown), `protocol`, `superseded`.
- *Hello*: `engine.hello {protocol: 1}` first; calls made before it is
  answered wait; an engine that refuses it, or answers with another
  protocol or name, fails them (`unsupported`) and is killed.
- *Coalescing*: a call can carry a key; at most one call of a key is at the
  engine, the newest later one waits, and an even newer one takes the
  waiting one's place (which fails as `superseded`, never sent; a call
  still queued is replaced in its place). So a drag never queues more
  than two meshes of a shape. Replies to calls that were sent are
  delivered: no stale reply is dropped, since dropping the replies of sent
  calls while newer ones keep coming would leave a long drag without one
  update. A coalesced call may be sent after calls made later: keys are
  for reads whose newest answer counts.
- *Types*: calls typed by the generated `Methods`, events by `Events`;
  `Reply<M>` replaces a frame description with its `FrameView`
  (`arrays`: views of the bytes, `layout`: their description, `buffer`);
  `meshArrays` and `poseArrays` check a mesh's or pose frame's arrays
  against its counts.
- *Recordings*: `RecordingTransport` keeps a session byte for byte (the
  lines written, stdout chunks as they came, stderr, the end);
  `FakeEngine` replays one, each run of output after the request before it,
  timing optional; a request that is not the recorded one is answered with
  an error and noted. The sessions are code (`tools/sessions.ts`) whose
  calls depend only on what they receive, recorded with `npm run record`
  together with what the client saw.

**Types generated from `api/`** (`tools/gen-api.ts`, output
`src/engine/api.generated.ts`, committed): the subset api/README.md lists
and nothing more (another keyword, an unresolved `$ref`, an unknown errc or
a frame the client cannot find stops it). Every `$defs` entry is a named
type: types.json's under their names, an area file's too unless another
file uses the name, then the area's name goes in front (`ShapeMade`,
`ProblemMade`, `SolveProblemParam`); `enum` and `const` are literal unions
(`null` included), `oneOf` and type lists unions, three-item arrays tuples,
`additionalProperties` a schema an index signature (which takes the named
properties' types too), descriptions and constraints doc comments.
`METHOD_NAMES`, `EVENT_NAMES`, `ERROR_CODES` (the table is the
generator's: the schema names errcs, not codes) and `FRAME_PATHS` (the
methods whose result holds a frame, and where). A unit test fails when the
file is stale; an engine test compares `METHOD_NAMES` with `engine.info`.

**Toolchain** (pinned in `web/package.json`, lockfile committed): Node 22
(22.12 or later; it runs the `.ts` tools by type stripping, so imports name
their `.ts` files and only erasable syntax is used), npm; TypeScript 6.0.3,
strict with `noUncheckedIndexedAccess`; electron-vite 5.0.0 on Vite 7.3.7;
Electron 44.7.0 with its `@types/node` (24.19.1, one copy; the tests run on
Node 22); Vitest 5.0.3; ESLint 10.12.0 with typescript-eslint 8.71.1
(strict and stylistic, type-checked); Prettier 3.9.9. TypeScript 7 and Vite
8 were current, but typescript-eslint supports TypeScript below 6.1 and
electron-vite Vite 7 at most. Two TypeScript projects: `tsconfig.node.json`
(main, preload, the client, tools, tests) and `tsconfig.web.json` (the
renderer, and `src/engine/` without `node-transport.ts`, compiled without
Node's types: the check that the client is platform-free). ESLint keeps
Node and Electron out of `src/engine/` and out of the renderer, and the
renderer away from `main/` and `preload/`. Stage 1b added three.js 0.186.1
(with `@types/three` 0.186.0) and Playwright 1.64.0 (its Electron driver;
no browser is downloaded), and a third TypeScript project,
`tsconfig.e2e.json`, for the e2e tests (Node's types and the DOM's, for
code run in the page). electron-builder comes with stage 8.

**Recipes:** `just web-install`, `web-gen`, `web-check` (both type checks,
ESLint, Prettier), `web-test` (unit; the engine tests against
`build/release/engine/bt-engine` or `BT_ENGINE`, skipped with a message
without one); `web-dev` and `web-e2e` (stage 1b; both clear
`ELECTRON_RUN_AS_NODE`, which an editor's terminal may set); `web-dist`
comes with stage 8.

**State** (from stage 1b): a store mirrors `Snapshot` and a record per
problem being solved; `document.changed` reloads what its ids name; the UI
never edits its mirror, it calls and waits for the revision;
`invalid_state` is shown as "pause first"; meshes are cached by shape,
revision, style and transformation.

**Deliberate differences from the original GUI** (later stages add to the
list; each also in its handoff):
1. (stage 1b) The 3D view is navigated as the mockup's, not the
   original's: its home view (yaw −32°, pitch 26°), its framing and gentle
   perspective, its Drag orbit as the default; the original's arc-ball is
   the "Arc-ball" rotation method. The faces are coloured and lit as the
   original's (its checker, variable marks, light), with its positional
   light taken as a fixed eye-space point.
2. (stage 1b) "Dim other layers" lightens the other layers' faces as the
   original dimmed a piece (1 − (1 − c) · 0.2), opaque, where the mockup
   made them transparent.
3. (stage 4) The dt-less playback orientation.

## Consequences
- Building the front end needs Node 22; the core does not.
- The renderer reaches nothing but `window.bt`; everything the engine
  answers is asked of it, and frames are never parsed by hand.
- Another way to run the engine (a browser with a WebSocket bridge, or
  WebAssembly) needs another `Transport`, not another client.
- The recordings are snapshots of the engine's output: after a change to
  what it sends, `npm run record` renews them (a replay of an old one
  still passes).
- Calls fail while the engine restarts; the front end waits for `running`
  and reloads.

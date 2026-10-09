# Handoff: web stage 1a (toolchain, generated types, the engine client)

**Date:** 2026-10-08 · **Next:** the audit of stage 1a (FRONTEND-PLAN.md,
"Recommended audits"), then stage 1b, the shell and the 3D view.

## Done

### The toolchain (`web/`)
Node 22.23.3 (Homebrew) and npm 10.9.9 on this Mac. `web/package.json`
pins TypeScript 6.0.3, electron-vite 5.0.0, Vite 7.3.7, Electron 44.7.0,
`@types/node` 24.19.1, Vitest 5.0.3, ESLint 10.12.0 with `@eslint/js`
10.0.1 and typescript-eslint 8.71.1, Prettier 3.9.9; the lockfile is
committed and `just web-install` is `npm ci`. Why not the newest:
TypeScript 7.0.2 and Vite 8.3.4 were current, but typescript-eslint
supports TypeScript below 6.1 and electron-vite 5 supports Vite 7 at most;
`@types/node` is Electron's own version so there is one copy (the tests
run on Node 22). Two TypeScript projects: `tsconfig.node.json` (main,
preload, the client, tools, tests) and `tsconfig.web.json` (the renderer
and `src/engine/` without `node-transport.ts`, without Node's types: the
check that the client is platform-free); ESLint (strict and stylistic,
type-checked) keeps Node and Electron out of `src/engine/` and the
renderer, and the renderer away from `main/` and `preload/` (checked with
a probe file: each import and global is refused). `electron-vite build`
bundles main (ESM), the preload (CommonJS, as a sandboxed preload must
be) and the renderer stub. Recipes: `just web-install`, `web-gen`,
`web-check` (both type checks, ESLint, Prettier), `web-test`.

### The types (`tools/gen-api.ts` → `src/engine/api.generated.ts`)
2,381 lines from the ten schema files (94 methods, 8 events): every `$defs` entry a named type
(area files' names prefixed with the area only when another file uses the
name: `ShapeMade`, `ProblemMade`, `ProblemProblemParam`,
`SolveProblemParam`), `Methods` (params, result, the errcs declared) and
`Events` by name, `METHOD_NAMES` and `EVENT_NAMES` (sorted),
`ERROR_CODES` (the generator's table; the schema names errcs but not their
codes), `FRAME_PATHS` (the four methods whose result holds a frame, all at
`frame`). `enum`/`const` (with `null`) literal unions, `oneOf` and type
lists unions, three-item arrays tuples (`Cell`, `Size`), an object with
`additionalProperties` a schema an index signature that also takes its
named properties' types (`SolverSettings`), descriptions and constraints
as doc comments. Anything outside the subset stops it with a message.
`node tools/gen-api.ts --check` exists too; the unit test is the gate.

### The client (`src/engine/`)
`framing.ts` (`FrameParser`, `StderrRing`), `frames.ts` (`viewFrame`,
`attachFrame`, `meshArrays`, `poseArrays`), `client.ts` (`EngineClient`),
`dispatch.ts` (`OrderedInbox`), `client-types.ts` (`EngineApi`,
`Reply<M>`, `CallOptions`), `errors.ts` (`EngineError`), `transport.ts`,
`bridge.ts`, `recorder.ts` (`RecordingTransport`), `node-transport.ts`
(`NodeTransport`, `engineVersion`). Decision 0013 has the rules; in short:
frames copied once and handed out with the reply after them, typed-array
views over them; requests only, pipelined in order, 32 at the engine at
most; replies matched in order (a later reply fails the earlier calls
still waiting, an id-less error belongs to the oldest); a reply settles,
and the code awaiting it runs, before the events behind it; `engine.hello`
first, the calls made before its answer held; coalescing by key; client
errcs `disconnected`, `protocol`, `superseded` (code 0).

### The host, the bridge, the proxy (`src/main/`, `src/preload/`, `src/renderer/`)
`EngineHost` (one engine, restarted with a back-off when it dies, its
status told, calls failed while no engine is running or before a new one
has said hello, giving up after five quick deaths), `serveEngine` (the
host's side of the bridge, checking the renderer's calls), the preload's
`window.bt`, `EngineProxy` (the client's interface over it, frames viewed
again from `ArrayBuffer`s, the same reply-before-events rule).
`main/index.ts` makes a window with its host; `renderer/index.html` and
`main.ts` are a stub showing the engine's status, there so the bundle
builds: stage 1b replaces them.

### Recordings and the fake engine (`fake-engine/`, `tools/sessions.ts`)
Two sessions, recorded against the release engine by `npm run record`:
`basic` (Bermuda: meshes in every style, six pipelined, solved, paged,
played back, a turned mesh, a refused call, an edit; 383 KB) and
`dominoes` (the 2x4x5 box paused, carried on, aborted, then a new
document; 9 KB), each with the transcript of what the client saw.
`FakeEngine` replays a recording as a transport; the same session code
over it sees the same transcript, with and without the recorded timing.

### Documents
Decision 0013; `docs/mockup/index.html` (blob `ac44c8eb`, 240,443 bytes,
the same as GitHub's at `d7afb3ed`) with `docs/mockup/README.md`; AUTHORS;
webui-spec.md's source paragraph; AGENTS.md's front-end section (rules,
code, tests, finishing a front-end stage) and its command list;
ARCHITECTURE.md's `web` section; api/README.md (`just web-gen` is step 5
of adding a method); `web/README.md`. No C++ changed.

## Gate results
| Check | Result |
|---|---|
| `just web-check` | clean, three times: both TypeScript projects, ESLint (no warnings), Prettier |
| `just web-test` (release engine) | **100/100**, three times, ~13 s each: 11 files (unit: framing 26, client 26, gen-api 8, frames 8, host 7, fake-engine 5, bridge 3; engine: engine 6, session 6, solving 3, host 2) |
| framing | both recordings in chunks of 1, 7, 4096 bytes, whole, and 50 random cuttings each: the same messages and frames; a frame of newline bytes cut at every place and at every pair of places; a 0-byte frame; two frames before one reply; a line that is not JSON, not UTF-8, not an object; a frame followed by an event; a frame twice; a frame over the limit skipped; a line over the limit dropped at chunk sizes 1 to 4096 (a line of exactly the limit kept); a frame announced without a size (fatal); the stream ending mid-frame, mid-line and after an unanswered frame |
| generated types | current (`just web-gen`: "is current"); every method and event named; the subset's mappings and refusals on synthetic schemas |
| engine: hello | protocol 1 agreed; protocol 2 refused `unsupported` (-32005), 0 `invalid_params`; a fake engine of protocol 2 refused by the client, its waiting calls failed, it killed |
| engine: info | `engine.info.methods` sorted equals `METHOD_NAMES` (94) |
| engine: fixtures | all 19 of `tests/data/puzzles/` opened clean, no `document.changed`, all seven grids |
| engine: meshes | 170 shapes x 3 styles = 510 meshes, pipelined per fixture, every frame's arrays fitting `triangles` and `vertices`, indices below `vertices`, flat edge lines |
| engine: 64 MiB line | refused by the client without sending (-32600); a client allowed to send it gets the engine's id-less -32600 on that call, the calls around it answered, the engine going on, exiting 0 |
| engine: error codes | not_found, invalid_params, parse_error (-32003), io_error, invalid_state, method_not_found, invalid_argument, unsupported: each with decision 0012's code |
| engine: scripted session | stage 7's `solving_session` in TypeScript on DemoPieceGenerator (solve with take-apart, paging sorted every way, solution.get, turned meshes, playback with poses and a track, both explorers, take-apart afterwards and its stop, toShapes and undo, every removal and undo, save and reopen), then Bermuda solved, played (every piece's mesh, poses, track) and saved |
| engine: solving | the 2x4x5 dominoes (one thread): the guards while solving, pause, save while paused, continue, save point, continue, abort; a new document during an analysis and a run: the last `analyseEnded {stopped}` and `idle` after the reply, no `document.changed` |
| engine: event order | a run's last `solve.state` before its end's `document.changed` (only progress and solutionsAdded between), the start's `document.changed` after its reply; the abort's two `document.changed` (its own, after its reply, then the run's end after `idle`) |
| engine: killed | SIGKILL with meshes at the engine: those calls fail `disconnected` naming the signal; the host says `exited` then `running` (restart 1), the new engine works on a new document; `stop()` ends it with status 0; `--version` checked (a wrong expected version and a missing engine refused) |
| skipped without an engine | with the release engine moved aside: 17 engine tests skipped, the run says why |
| `electron-vite build` | builds main, preload (CommonJS), renderer |
| C++ | unchanged; `ctest -R api-` passes (the docs edits touched no schema) |

## Where this stage departs from the plan, and why
1. **Coalescing drops no stale replies.** At most one call of a key is at
   the engine, the newest later one waits, an even newer one supersedes
   it unsent; the reply of a call that was sent is delivered. Dropping
   replies of sent calls while newer ones keep coming would leave a long
   drag (a mesh slower than a frame) without a single update; with one at
   the engine at a time, nothing stale is ever produced to drop. A
   coalesced call may be sent after calls made later (keys are for reads).
2. **The scripted session runs on DemoPieceGenerator**, as stage 7's does:
   the plan names Bermuda, which has one solution, so sorting and the
   removals would test nothing. Bermuda is solved, played and saved after
   it (and is the `basic` recording).
3. **Calls wait for hello's answer** rather than being pipelined behind
   it, so no call reaches an engine of another protocol; one round trip
   at the start.
4. **The host fails calls during a restart** instead of holding them for
   the new engine: they would carry the old document's ids into the new
   (empty) one. The window waits for `running` and reloads.
5. Toolchain versions below the newest (TypeScript 6, Vite 7), as above.
6. The Electron app is **not launched** in this stage (its binary is
   fetched on first run; launching is stage 1b's gate). The bridge is
   tested in memory with structured clones, one task a message, as IPC
   delivers them; `main/index.ts` and the preload are type-checked and
   bundled.

## What the mockup did differently
Nothing of the mockup is ported yet; it is vendored for stage 1b.

## Found while testing
- **Replay order.** The engine writes a reply and its `document.changed`
  as two flushes, and a client may send its next request between them;
  the recording keeps that order and the fake engine reproduces it (the
  event comes after the next request). A test that waits for an event a
  recording put behind a request it never sends waits forever: replay the
  session's own requests.
- **One reply per chunk is not enough to keep the order.** The parser
  hands a chunk's messages over one at a time, so after a reply the inbox
  is empty for an instant; the first version then dispatched the event
  behind it before the awaiting code ran. The inbox now defers whatever
  follows a settled reply, in the same task or the next
  (`client.test.ts`, "are typed, and come after the reply in front of
  them is settled").

## Known issues and notes
- The recordings are snapshots of the engine's output: after a change to
  what it sends, `npm run record` (the old ones still replay, but no
  longer show what the engine does).
- A line from the engine is kept up to just under 512 MiB (V8's longest
  string): `document.saveBytes` of more than about 380 MiB of XML could
  not be read (the engine's own gzip limit is 256 MiB).
- The engine tests run one file at a time (`fileParallelism: false`):
  the pause tests time real solves. The 2x4x5 box takes about 9 s on one
  thread in the release build; the dev engine is far slower and is not
  what `web-test` uses.
- `NodeTransport` has not run on Windows (neither has the engine's binary
  stdio).
- The repository's README.md still says "stages 0 to 7 of 8"; not touched.
- `just check` (clang-tidy) still needs LLVM; no C++ changed here.

## Brief for the next stages

**First, the audit of stage 1a** (Fable 5.1, max effort): the stage's
diff, decision 0012 and 0013, this note; concrete failure scenarios in the
framing state machine, request ordering and coalescing, the event order,
the type generator's subset and the host's restart; probes and mutation
tests rather than reading alone; fixes in a commit of their own
(`Web 1a audit: ...`), recorded in this note.

**Then stage 1b** (Opus 5.5, high), FRONTEND-PLAN.md's "Stage 1b":
- The renderer talks through `EngineProxy` (`new EngineProxy(window.bt)`);
  wait for its status `running` before the first call, reload the mirror
  when `restarts` grows.
- Meshes: `shape.mesh` replies carry `frame.arrays`; take them through
  `meshArrays()` (checked against the counts) into `BufferGeometry`;
  coalesce requests by a key such as `mesh:<shape>:<style>:<transformation>`.
- three.js and Playwright's Electron driver go into `web/package.json`
  pinned; the Electron binary comes with `npx install-electron` (or the
  first `electron` run). The first e2e should exercise the IPC path end to
  end: a mesh across the bridge, an engine killed and the status seen.
- `just web-dev` (against `build/dev` or `BT_ENGINE`) and `just web-e2e`
  are 1b's recipes; the CSP in `renderer/index.html` must keep allowing
  the bundle (`script-src 'self'`).
- Port the mockup's shell functions the plan lists, each file citing
  `docs/mockup/index.html` and the functions it came from.

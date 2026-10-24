---
title: Engine Protocol
description: "How the ShapeLogic app and shapelogic_engine talk: JSON-RPC over stdio, binary frames, events, and the rules every call follows."
---

The app never links the C++ core. It starts `shapelogic_engine` as a child
process and talks to it over the process's standard input and output. The
command line tool and the contract tests drive the same engine, and any
language that can write a line of JSON can too.

The protocol is settled in two decision records,
`docs/decisions/0003-engine-process-json-rpc.md` and
`docs/decisions/0012-engine-protocol.md`. The method reference is
`docs/API.md`, generated from the schema in `api/`.

## Messages

Each line on the engine's stdin is one JSON-RPC 2.0 request. Each line on
its stdout is one reply or one notification. Logs go to stderr.

```json
{"jsonrpc":"2.0","id":7,"method":"solve.start","params":{"problem":"p1","options":{"solver":"classic","disassemble":true}}}
```

```json
{"jsonrpc":"2.0","id":7,"result":{"revision":42}}
{"jsonrpc":"2.0","method":"document.changed","params":{"revision":42,"what":["problems","solutions"],"shapes":[],"problems":["p1"]}}
```

- Parameters are always by name, in an object. A missing, mistyped or
  unknown field is refused, as the schema says (`additionalProperties:
  false` everywhere).
- Items are named by ids that survive reordering: shapes `s7`, colours
  `c2`, problems `p1`, solutions `sol42`, rod sets `r1`, playback handles
  `pb1`, explorers `ax1` and `mx1`.
- A request without an `id` is a notification: it runs and gets no reply.
- Errors carry JSON-RPC's code, a message and `data.errc`, one of the
  core's error kinds: `not_found`, `invalid_argument`, `invalid_state`,
  `unsupported`, `io_error`, `parse_error`, `internal` and so on. Every
  method's schema entry lists the errors it can return.
- Limits: a line of at most 64 MiB, nested at most 64 deep; batches
  (arrays) are refused. A malformed line is answered, never fatal.

The first call is always `engine.hello` with the protocol the client speaks
(this version: 1). The engine answers with its name, version and the
protocol it will speak, or refuses one it does not know.

## Binary frames

Meshes and pose tracks are not JSON. The engine announces a frame with a
notification, then writes exactly that many raw bytes, then the reply that
refers to the frame:

```
{"jsonrpc":"2.0","method":"frame","params":{"frame":3,"bytes":245760}}
<245760 raw bytes>
{"jsonrpc":"2.0","id":9,"result":{"frame":3, ...}}
```

A frame holds typed arrays at offsets that are multiples of four; the reply
describes them. On the app's side `framing.ts` splits stdout into lines and
frames, and `frames.ts` gives typed-array views over the bytes without
copying. The renderer never parses a frame by hand.

## Events

Events are notifications the engine sends on its own.

| Event | When |
|---|---|
| `document.changed` | after a call that changed the document, and when background work ended; carries the new revision, what changed (`shapes`, `colours`, `problems`, `solutions`, `notes`, `rodSets`) and which ids |
| `solve.state` | a problem's solve moved to another state |
| `solve.progress` | at most four times a second while a solve runs, and once when a run ends |
| `solve.solutionsAdded` | the problem's saved list changed; carries `solutionsVersion` so a client knows whether to page again |
| `solve.error` | a run ended in error, with the failure |
| `solution.analyseProgress`, `solution.analyseEnded` | an analysis of saved assemblies |

The engine's main thread finds out about background work by polling: the
run loop in `engine/engine.cpp` waits for a line for 50 ms while a solve
runs (500 ms otherwise), then calls `pollEvents` in `engine/events.cpp`,
which asks each watched problem's session for its state and progress.

## The rules

1. **Requests run one at a time, in order.** The main thread of the engine
   runs every request and alone touches the document. Solves run on
   threads of their own and are read through `readProblem()`.
2. **A reply comes before the events it caused.** After `solve.start`
   returns `{revision}`, the `document.changed` for that revision follows,
   then the poll's `solve.state` and `solve.progress`. A frame comes
   before the reply that refers to it.
3. **The revision never goes back.** Every change of the document is a new
   revision. A new document counts on from the old one. The app waits for
   a mutating call's revision before it acts on what the call made
   (`Store.whenRevision`, `Edits.mutate`).
4. **A run's end is its revision.** The last `solve.state` of a run
   (`finished`, `paused`, `error` or `idle`) comes before the
   `document.changed` that takes the run's results in. The app treats the
   run as over only at that `document.changed`.
5. **`invalid_state` means "pause first".** While a solve is active, the
   shapes it reads and the lists of shapes, colours and problems are frozen.
   The app shows the refusal as "Pause the solve first" and never retries.
6. **The engine is the truth.** The app never computes what the engine can
   answer: cells, fits, counts, meshes and sort orders are all read from
   it. `solution.page` sorts and pages the solutions; the app holds only a
   mirror, refreshed by `document.changed`.

## From schema to code

The schema in `api/*.json` is the single source. `api/README.md` describes
the files and the subset of JSON Schema they use. Adding a method takes
five steps:

1. Its entry in the area's schema file.
2. The handler in `engine/methods_<area>.cpp`, registered in that file's
   `add…Methods` function.
3. A call in `tests/contract/test_engine.py`; the coverage check fails for
   a method never called with success.
4. `python3 api/gen_api_md.py` to regenerate `docs/API.md`.
5. `just app-gen` to regenerate the app's types in
   `web/src/engine/api.generated.ts`.

## Trying it by hand

The engine reads stdin, so a shell can drive it:

```bash
just build release
printf '%s\n' \
  '{"jsonrpc":"2.0","id":1,"method":"engine.hello","params":{"protocol":1}}' \
  '{"jsonrpc":"2.0","id":2,"method":"document.open","params":{"path":"tests/data/puzzles/PelikanBurr.xmpuzzle"}}' \
  '{"jsonrpc":"2.0","id":3,"method":"problem.fit","params":{"problem":"p1"}}' \
  | build/release/engine/shapelogic_engine
```

`tests/contract/engine_client.py` is a small Python client that does the
same with replies matched to requests; the contract tests are built on it.

## How the app crosses the process boundary

In the app there is one more hop: the window's page is sandboxed and
cannot start processes. The main process owns the engine (`EngineHost`),
and the page talks to it over Electron's IPC:

```
renderer page                preload                 main process              engine
EngineProxy.call  ──bt:call──▶ window.bt.send ──ipc──▶ serveEngine.receive ──▶ EngineClient.call ──stdin──▶
                  ◀bt:message─ onMessage      ◀─ipc── post(reply/event)   ◀── OrderedInbox      ◀─stdout──
```

`serveEngine` in `web/src/main/engine-ipc.ts` checks that the method is one
of the schema's, refuses a call naming a file the window was never handed
(`path-grants.ts`), and forwards the rest. Replies, events and the engine's
status come back on one ordered channel, so the page sees the same order
the engine produced.

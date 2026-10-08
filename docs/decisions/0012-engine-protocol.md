# 0012: The engine protocol

**Status:** accepted (stage 7); `engine/`, `api/`, `docs/API.md`

## Context
Decision 0003 put the core in its own process, `bt-engine`, speaking
JSON-RPC 2.0 over stdio with binary frames for meshes, and the method list
in JSON Schema under `api/`. Stage 6 built the app layer the engine wraps:
one `Document` called from one thread (decision 0011), solves that run on
threads of their own and are read through `readProblem()` (decision 0010),
and samplers and explorers that hold copies of what they need. This record
settles what 0003 left open.

## Decision
**JSON library.** nlohmann/json 3.12.0, pinned by URL and SHA-256 in
`cmake/Dependencies.cmake` and linked into the engine only; `core/` stays
free of JSON. A reader of our own was weighed and dropped: the parser is
the part that meets untrusted bytes (UTF-8, escapes, number ranges), and
a well-tested one is worth the dependency.

**Messages.** Each line on stdin is one JSON-RPC 2.0 request (UTF-8, at
most 64 MiB); each line on stdout is one response or notification. Batches
(JSON arrays) are refused (`-32600`). A request without `id` is a
notification: it runs and gets no reply (its errors go to stderr). stderr
carries logs only. Requests run one at a time in the order they came; a
call that waits (`solve.abort`) holds up the ones behind it.

**Binary frames.** Meshes and pose tracks are not JSON. A frame is the
line `{"jsonrpc":"2.0","method":"frame","params":{"frame":N,"bytes":L}}`
followed at once by exactly L raw bytes (no newline after them); the reply
that refers to frame N comes after it. Frame numbers count from 1 per
process. A frame holds typed arrays, little-endian, each starting at a
multiple of 4 bytes; the reply describes them:
`{"frame":N,"bytes":L,"arrays":{"positions":{"type":"f32","offset":0,"length":…},…}}`
(types `f32`, `u32`, `i32`, `u8`), so a JavaScript client makes typed
arrays over the buffer without copying. Bytes inside JSON (`document.openBytes`,
`document.saveBytes`) are base64.

**Errors.** JSON-RPC's own codes for the protocol: `-32700` the line is not
JSON, `-32600` not a request, `-32601` no such method, `-32602` the params
do not fit the method (missing, wrong type, unknown name: params are
checked strictly, as the schema's `additionalProperties: false`), `-32603`
internal. Errors of the core (`Errc`) take one code each: `-32001`
invalid_argument, `-32002` not_found, `-32003` parse_error (a file or bytes
that are no puzzle), `-32004` io_error, `-32005` unsupported, `-32006`
invalid_state; `Errc::Internal` and an `InternalError` caught at the
dispatcher are `-32603`. Every error's `data` is `{"errc": "<name>"}` (the
`Errc` names of core-api.md, `invalid_params` for -32602, `method_not_found`,
`invalid_request`, `parse_error` for -32700), so a client switches on one
field. The front end shows `invalid_state` as "pause first" (decision 0011).

**Threads.** A reader thread reads stdin and queues lines; the main thread
owns the `Document`, the samplers and explorers, and stdout: it runs the
queued requests and, every 50 ms while a solve or an analysis is active,
polls them for events. Nothing else touches the document. On end of input
or `engine.shutdown` the document is destroyed (its solves pause and are
waited for) and the process exits 0.

**Ids.** Shapes `s<n>`, colours `c<n>`, problems `p<n>`, solutions
`sol<n>`: the document's `Id` (and `Solution::getId()`) with a prefix, so
an id of the wrong kind is caught (`invalid_params`). A solution id is
unique within its problem; every call naming a solution names its problem.
Labels (`S1`, `C1`, `P1`) follow the list order and come with every item.
Samplers and explorers live in the engine behind handles (`pb<n>` playback,
`ax<n>` assembly explorer, `mx<n>` movement explorer), never reused,
closed by the client; a new document drops them all. Movement explorer
positions are numbers within their handle.

**Revisions and changes.** Every mutating method returns `{revision}`
(and what it made: `{shape, revision}`). After a call that changed the
document, and when background work changed it (a run ended, an analysis
was applied: `Document::poll()`, which gives them a revision), the engine
sends `document.changed {revision, what, shapes, problems}` from
`Document::lastChange()`; empty id lists mean "the lists themselves".

**Long work is keyed by its problem.** A problem has at most one solve
(decision 0010) and one analysis, so the problem id is the job id;
`solve.*` and `solution.analyse*` return at once. Events, per problem:
`solve.state {problem, state}` for every state the poll sees change (the
last one of a run is always sent), `solve.progress` at most four times a
second (and once when a run ends), `solve.solutionsAdded {problem, saved,
assemblies, solutions}` when the saved list changes (its length or the
newest solution id; at most four times a second), `solve.error {problem,
failure}` on entering Error; `solution.analyseProgress` (throttled the same
way) and `solution.analyseEnded`. Events are what the poll sees: a run's
`document.changed` may come just before or just after its last
`solve.state`. A pause or a save point is refused (`invalid_state`) when
no run is under way. Options come from the request over the engine's
settings (threads, solver). Autosave stays the client's policy (decision
0010): `solve.savePoint`, wait for `paused`, `document.save`,
`solve.continue`. An analysis is solving work (decision 0011): it records
no undo step, the document is frozen while it lasts, and its separations
go in when it ends (`Document::poll()`), those finished before a stop
too.

**Solutions are paged as a view.** `solution.page` sorts the saved list's
indices under `readProblem()` with the problem's own comparators (the
original's sort buttons: found order, level, moves, pieces; descending by
the swapped comparator, so ties keep their order) and returns a slice of
rows. It does not reorder the document; the stored order is `list`.

**Settings** are the engine's, kept across documents: undo depth (passed
into `DocumentSettings`), threads and the default solver (the defaults of
every `SolveOptions`).

**Schema.** `api/types.json` holds the shared definitions; one file per
area (`engine`, `document`, `shape`, `colour`, `problem`, `solve`,
`solution`, `playback`, `explore`) lists its methods (`params`, `result`,
`summary`) and events (`params`) as JSON Schema 2020-12, in a subset a
small validator of our own checks (`api/apischema.py`: `type`,
`properties`, `required`, `additionalProperties`, `items`, `enum`,
`const`, `pattern`, `minimum`, `maximum`, `minItems`, `maxItems`,
`minLength`, `oneOf`, `$ref`), so the tests need no packages. `docs/API.md` is
generated from it (`api/gen_api_md.py`; a test fails when it is stale).
The contract tests start `bt-engine`, check every reply and event against
the schema, and check that the engine's methods are the schema's.

**Not yet.** Sliding and stacking puzzles open, edit and save; solving,
analysing, playback and explorers refuse them with `unsupported`.

## Consequences
- A client in any language needs a JSON parser and a line reader that
  can switch to reading L bytes; a JavaScript client wraps frames in typed
  arrays directly.
- Requests are serial: a client wanting a responsive UI keeps long work in
  jobs (solves, analyses), which is how the API is shaped.
- The schema is the contract: a method added to the engine and not to
  `api/` (or the reverse) fails the contract tests.
- Windows: stdout must be put in binary mode for frames (stage 8 builds
  there).

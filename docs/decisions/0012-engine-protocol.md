# 0012: The engine protocol

**Status:** accepted (stage 7; amended by the stage 7 audit: the limits
while reading, the nesting depth, notifications and frames, the abort
rule, the event order, a document's end, the solutions version,
`engine.hello`, the names); `engine/`, `api/`, `docs/API.md`

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
most 64 MiB, nested at most 64 deep); each line on stdout is one response
or notification. Batches (JSON arrays) are refused (`-32600`), as is a
deeper line ("nesting too deep", `-32600`: the parser copes with any
depth, what comes after it recurses, and a line is never trimmed in
silence). The reader thread keeps at most 64 MiB of a line: the rest of a
longer one is read and dropped, and the line is answered `-32600` ("the
line is too long") once its newline arrives, so a client cannot grow the
engine's memory with a line. The queue of lines read and not yet run is
bounded (1,000 lines or 64 MiB): a client that floods requests is held
back on its pipe rather than filling memory. A request without `id` is a
notification: it runs and gets no reply (its errors go to stderr); a
notification is for the mutating calls whose outcome `document.changed`
reports, and one that would produce a frame is refused (logged to stderr,
no frame: nothing would refer to it). stderr carries logs only. Requests
run one at a time in the order they came; a call that waits
(`solve.abort`) holds up the ones behind it.

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

**Versioning.** `engine.hello {protocol}` is a client's first call: the
engine answers with the protocol it will speak (`1` now; `engine.info`
names the same) or refuses one it does not know (`unsupported`). Clients
are told apart by it when a change breaks them; a client that skips it
gets protocol 1 for as long as it exists.

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
solutionsVersion, assemblies, solutions}` when the saved list changes (the
problem's `solutionsVersion()` moved; at most four times a second, and
once when the run ends), `solve.error {problem, failure}` on entering
Error; `solution.analyseProgress` (throttled the same way) and
`solution.analyseEnded`, both with `failure` (what an internal error on
the analysis's thread said, else null). What a client may rely on in the
order: a request's reply, then its `document.changed`, then the poll's
events; for a run that ends by itself, `solve.state` (finished, paused or
error), `solve.progress`, `solve.solutionsAdded`, then the run's own
`document.changed` with a new revision, so a client waits for the
revision, not the state, before reloading the lists; `solve.state` skips
the states a run passes through within one poll; `solve.abort` yields two
`document.changed` (its own, then the run's end from the poll). A pause
or a save point is refused (`invalid_state`) when no run is under way;
so is an abort unless a run is active or paused in memory: the hard stop
of a solve, never the removal of a finished one's solutions (that is
`solution.delete {which: all}`, undoable; the original's Abort button
was greyed out once the solve thread had ended, and a click in the
moment after a run finished must not destroy its results). A paused solve
from a file is carried on (`solve.continue`) or started over
(`solve.start`). Options come from the request over the engine's
settings (threads, solver). Autosave stays the client's policy (decision
0010): `solve.savePoint`, wait for `paused`, `document.save`,
`solve.continue`. An analysis is solving work (decision 0011): it records
no undo step, the document is frozen while it lasts, and its separations
go in when it ends (`Document::poll()`), those finished before a stop
too.

**A new document ends the old one's work.** `document.new`, `open`,
`openBytes` and `solve.importPaused` destroy the open document (its runs
pause and are waited for, its analyses stop, its handles go) and, after
the reply, send a last `solve.state idle` and `solution.analyseEnded
{stopped: true}` for each problem whose work the events were reporting;
no `document.changed` (the revision counts on). A client's progress panel
for the old problem closes on those; the problem's id answers
`not_found` from then on.

**Solutions are paged as a view.** `solution.page` sorts the saved list's
indices under `readProblem()` with the problem's own comparators (the
original's sort buttons: found order, level, moves, pieces; descending by
the swapped comparator, so ties keep their order) and returns a slice of
rows. It does not reorder the document; the stored order is `list`. One
page is consistent (taken under the solve's lock); two are related by
`solutionsVersion`, a runtime counter the `Problem` moves on every change
of its saved list (added, removed, re-sorted, pieces edited), returned
with every page and with `solve.solutionsAdded`: pages read at one value
belong together, and a client pages again when it moves.

**Meshes in an orientation.** A `Placement` carries a transformation
number of the grid's symmetry table; `shape.mesh {transformation}` gives
the shape's mesh turned by it (the core's `Voxel::transform`, as the
playback's piece meshes are made), so an assembled solution, a placement
of the assembly explorer or a position of the movement explorer is drawn
as the mesh of the piece's transformation at its cell minus the turned
shape's hotspot; the engine never deals in matrices or pixels. A solution
without a disassembly (not taken apart, found not to come apart, or only
its level summary kept) opens in `playback.open` as a single pose (steps
0, every pose the assembled one), so one path shows every solution.
`playback.mesh` takes the same `style` as `shape.mesh`.

**Names that stay.** The API's `SolveOptions` names its fields
(`completeRotations`, `strictColours`, `justLevels`) and the stored
`SolverSettings` keep the file's (`deepSymmetry`, `strictColors`,
`justLevels`): the stored settings are the original's text, saved as it
is and shown as the front end decides, so translating them would be
guessing at their meaning for other versions. Both spellings are
permanent; `types.json` records the mapping. The rest of what clients
bake in (the frame layout and array names, the id prefixes, the
`document.changed` conventions, the errc names and codes, the
`SolutionRow` fields, cells and sizes as arrays, the `frame`
notification's name) is as stage 7 made it; changes are additive.

**Limits of untrusted input.** Besides the line (64 MiB, depth 64) and
the queue: gzip data (`document.open`, `openBytes`, the paused file's
puzzle) inflates to at most 256 MiB (`parse_error` beyond; a puzzle file
is a few MiB); names and notes refuse control characters other than tab,
newline and carriage return, and names line breaks too (`invalid_argument`
from the document, so every front end gets it: the file is XML 1.0, and
an attribute gives a line break back as a space).

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

**Not yet.** Stacking puzzles open, edit and save; solving, analysing,
playback and explorers refuse them with `unsupported`. Sliding puzzles
are solved and played since the deferred plan's part 1 (decision 0014:
`SolveOptions.sliding`, the `sliding.*` methods); analysing refuses them,
as the original did (they have no take-apart). The original's "Enable
High Memory" box (`highMemory` in `SolverSettings`) maps to
`SolveOptions.sliding.highMemory`; for stacking it waits for that
solver.

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

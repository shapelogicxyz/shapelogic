# Handoff: stage 7 (the engine)

**Date:** 2026-10-08 · **Next:** stage 8 (the `bt` CLI and performance) in a
new session at high effort. The brief is at the end.

The stage fitted one session, so it is one commit, not the 7a / 7b split
the plan allowed. It was audited the same day (`tmp/audit/stage7report.txt`:
nine findings, the API design concerns A to I, the test gaps); the fixes
are the commit after it, "Stage 7 audit: fixes for the nine findings",
listed under "Fixed by the audit" below.

## Done

### The design (decision 0012)
JSON-RPC 2.0 over stdio, one message per line; binary frames (a `frame`
notification with its byte count, then the bytes; the reply after) made
of little-endian typed arrays that the reply describes; nlohmann/json
3.12.0 pinned in `cmake/Dependencies.cmake`, in the engine only; the error
codes (`Errc` to -32001…, `data.errc` on every error, params checked
strictly); one thread on the document, a reader thread that only queues
lines, a 50 ms poll while anything runs; ids with a prefix per kind
(`s7`, `c2`, `p1`, `sol42`, handles `pb1`, `ax1`, `mx1`); revisions that
never go back across documents; long work keyed by its problem; events
throttled to four a second; the solutions paged as a sorted view.

### The schema and its tools (`api/`)
93 methods and 8 events in ten files (`types.json` and one per area),
JSON Schema 2020-12 in a subset that `apischema.py` checks (no packages
needed; it also checks the schema itself), and `gen_api_md.py`, which
writes `docs/API.md` (ctest `api-docs` fails when it is stale). core-api.md
is marked superseded, with the renames listed.

### The engine (`engine/`)
`btengine` (a library, so the unit tests drive it in process) and
`bt-engine`. `engine.h` (the dispatcher, the run loop, `State`), `rpc.h`
(errors, `Params`, ids, `BTE_TRY` / `BTE_CHECK`), `output.h` (lines,
frames, base64), `convert.h` (enum names, the items' JSON),
`methods_<area>.cpp` (one per schema file), `events.cpp`.

### Ported into the app layer first (left from stage 6)
- `layer_geometry.h`: the grid editors' drawing of a layer
  (grideditor_0 to _4): polygons, circles on the sphere grid, the cell
  across each edge.
- `analysis.h`: "Add disassembly" for one solution, all, or those without
  their moves (cb_AddDisasm / cb_AddAllDisasm) as a job on a thread of its
  own, with progress and a stop that keeps what was finished; the
  document runs one per problem and puts the results in at `poll()`.
- `io/paused_solve.h` and `Document::exportPaused` / `importPaused`: the
  BTSOLVE2 file of cb_ExportPaused / cb_ImportPaused (gzip in memory added
  to `io/gzip_file.h` for it; `document.openBytes` takes gzip bytes too).
- `Document::setSolverOptions`: the Solver tab's settings stored with a
  problem (cb_SolverOptions), one undo step.
- `Document::poll()`: the end of a run and an analysis's results get a
  revision and a `lastChange()` of their own (for `document.changed`).
- `solutionSortOrder(by)` in `model/problem.h`: the comparison
  `sortSolutions` uses, so the engine can sort a view.

### Fixed in the app layer on the way
- `lastChange()` after undo, redo, a solve start or an abort kept the ids
  of the command before; it is reset now.
- `playback()` and `exploreMovements()` looked a solution's place up
  outside the solve's lock and used it inside: a running solve could have
  moved the list in between. The place is found under the lock now.
- `MovementExplorer::findMatching` with a piece not in the position hit
  an assertion of the analysator (the original's movement browser never
  offered one); it is refused (InvalidArgument) now.

### Harness
`oracle.py record-analysed` (`tests/oracle/analyse_old.cpp`, built
against the original's library) records `tests/data/analysed_by_original/`:
the saved assemblies of nine fixture cases taken apart again as the
original's "Add disassembly" does (rotations and Crowell for the rotation
fixture); recorded twice, identical.

### Tests (444 cases in `bt_tests` after the audit; ctest also runs `api-schema`, `api-docs`, `contract`)
- `test_layer_geometry.cpp`: every cell of every layer of every grid
  against the original's corner formulas (getTriangle and getPolygon
  copied into the test, drawTileFrame's sums), the neighbours across the
  edges (mutual, and neighbours in the grid).
- `test_analysis_files.cpp`: the analysis byte for byte against the
  original's (`analysed_by_original/`, the 314 level summaries of
  SolidSixPieceBurrs among them, `[slow]`); one solution with turns;
  what is refused while it runs and for grids without a take-apart; a
  stop (`[parallel]`); `poll()` after a run; gzip in memory against
  `writeGzipFile`'s bytes; BTSOLVE2 both ways (the layout byte for byte,
  a stacking section skipped, cut and wrong files refused, an imported
  solve carried on to the straight totals).
- `test_engine.cpp`: ids, `Params`, base64, frames, the dispatcher's
  replies, errors and `document.changed`, the run loop's ends, a solve
  polled while the owner keeps calling (`[parallel]`, for TSan), and a
  run that starts and ends between two polls in the state the last one
  ended in still telling its end (a bug of this stage's first version,
  caught by review: a client waiting for `finished` would have hung).
- `test_document.cpp`: the stored solver settings, undo's `lastChange()`.
- `tests/contract/test_engine.py` (`engine_client.py`): `bt-engine` as a
  subprocess, every reply and event checked against the schema, every
  method called with success at least once (the coverage check), 12
  sessions: engine info and settings, the protocol's errors, files, undo
  and groups, shapes and colours, tool tasks and views on all five grids
  (meshes and layer drawings as frames), problems; the scripted session
  of the brief (open a fixture, solve with take-apart, page the solutions
  sorted every way, play one back with pose and track frames, explore
  placements and movements, take apart again, turn into shapes, remove,
  save); pause, save point, continue and abort on a run long enough to
  pause (a 2x4x5 box of dominoes) with every guard of decision 0011 while
  it runs; prepare, export, import and carry on; the unsupported grids and
  a run that ends in error. 3-4 s.

## Fixed by the audit
The nine findings, in the audit's order:
1. **`solve.abort` destroyed saved solutions** (on an idle solved problem,
   and in the moment after a run finished, with no undo step). It is the
   hard stop of a solve under way now: `Document::solveAbort` refuses
   (`invalid_state`) unless the problem's session is active or paused; a
   finished solve's solutions go through `solution.delete {which: all}`
   (undoable; the problem stays solved with its counts, as the original's
   "remove all" left it), a paused solve from a file is carried on or
   started over. Contract case: abort after finished is refused and the
   solutions stay.
2. **Params nested 50,000 deep crashed the engine** (nlohmann's copy
   constructor recursed). The line is parsed with a callback that flags a
   depth over 64 and is answered `-32600` "nesting too deep" (never
   trimmed in silence); `params` is a pointer into the parsed message.
   Contract case in `protocol_errors`.
3. **No way to draw an assembled solution or an explorer's placement.**
   `shape.mesh` takes an optional `transformation` (a `Placement`'s
   number; `app::orientedMesh`, which the playback's piece meshes use
   too), `playback.mesh` a `style`, and `playback.open` opens a solution
   without a disassembly as a single pose (`steps` 0, `movesText` null).
   Unit case: the oriented mesh equals the mesh of the transformed voxel
   space; the single pose equals step 0 of a disassembly made in-process.
4. **A timing-dependent contract test.** `solving_session` compares the
   *last* `solve.solutionsAdded` of the run (and that the counts never go
   back, and the end's order: `solve.state`, then `document.changed`);
   `pause_continue_and_the_guards` runs a 2x4x6 box on one thread and
   fails when the pause does not land; the always-true check went; the
   client refuses an errc a method's schema entry does not declare (this
   found `shape.rename`, `problem.rename` and `document.setNotes` missing
   `invalid_argument`); `document_files` opens a copy of its fixture;
   `just test-tsan` runs the contract suite against the TSan engine.
5. **Uncapped gzip inflation.** `gunzipIfCompressed` and
   `readMaybeGzipFile` stop at 256 MiB (`parse_error` "the file is too
   large"); the limit is a parameter, so the unit test uses a 4 MiB
   stream of zeros against a 1 MiB cap.
6. **The line limit after the read, an unbounded queue.** The reader
   keeps at most 64 MiB of a line, drops the rest and queues a marker
   that is answered `-32600` once the newline arrives; the queue blocks
   the reader at 1,000 lines or 64 MiB. Unit case through `Engine::run`;
   contract case: a 64 MiB line is refused and the engine goes on.
7. **Orphan frames.** A method that would send a frame inside a
   notification is refused (`State::sendFrame` throws; logged to stderr,
   no frame). Unit and contract cases.
8. **An analysis's internal error hidden.** `failure: string | null` in
   `AnalyseEvent` and `AnalyseProgress`, from `AnalyseProgress::failure`.
9. **Control characters in names and notes.** The document refuses
   control characters other than tab, newline and carriage return in
   names and notes, and line breaks in names (`invalid_argument`). Unit
   and contract cases.

From the design concerns: **A** (`playback.mesh` style, with 3); **B**
(`solutionsVersion`, a counter `Problem` moves on every change of its
saved list, in `solution.page` and `solve.solutionsAdded`; the poll
detects changes by it, so the scan over every saved solution went;
`Problem::lastSolutionId()` is there for the newest id); **C** (the event
order written into `solve.json`'s summaries and decision 0012; the
contract checks the end's order and the abort's two `document.changed`);
**D** (a new document queues a last `solve.state idle` and
`solution.analyseEnded {stopped}` for the old one's watched problems,
sent after the reply; contract case `document_replaced_while_working`);
**F** (the planes, `highMemory` and the live counters noted in the
schema); **G** (`engine.hello {protocol}`, `unsupported` for a protocol
the engine does not know); **H** (recorded in decision 0012: the API's
`SolveOptions` names and the file's `SolverSettings` names are both
permanent, with the mapping in `types.json`). The two `[parallel]` unit
cases of the test gaps were added (a solve next to an analysis of another
problem; the explorers, fit, paging and the table while a solve runs,
ended by a new document), the run-loop test's streams outlive the test,
and `Problem`'s version and newest id have a model test.

Not done from the concerns, deliberately: the engine still reads the
stored check boxes as `"1"` (the original's text; I), `document.save`'s
reply has no `dirty` (I), and `solve.continue` on a solved problem keeps
the core's message naming `start()` (I; `core/bt/solve` was not touched).

## Gate results
| Check | Result |
|---|---|
| `just test-all` (dev, warnings as errors) | pass: 433 cases (415 quick, 18 `[slow]`), `api-schema`, `api-docs`, `contract` |
| quick suite repeated (with the contract tests) | 5 more times in dev, 8 in release: all pass |
| `just test-asan` (address + undefined) | pass: every case, `[slow]` included, and the contract tests against the ASan-built engine |
| `just test-tsan` | pass: 65 `[parallel]` cases, no warnings (the engine's run loop, a solve polled while the owner calls, an analysis read and stopped while it runs, the document read during a run) |
| `just oracle` every field, all tiers, Classic | **180/180** all threads, **180/180** `--threads 1` |
| `just oracle --solver bt2`, every field, all tiers | **180/180** all threads, **180/180** `--threads 1` |
| `just oracle --solver crowell`, every field, all tiers | **178/178** all threads, **178/178** `--threads 1` (InsomniAC1 and CoverUp3: the original's hour-long timeout) |
| `just oracle-files --tier all` | **180/180** Classic, **180/180** BurrTools 2 |
| `just oracle-resume` | **395/395** (76 single-thread pauses the same text, 84 BurrTools 2 positions refused) |
| `just oracle-roundtrip` | **365/365** byte-identical |
| `oracle.py record-analysed` | recorded twice, identical; matched byte for byte |
| `just check` | **not run: LLVM still not installed** |

After the audit's fixes (nothing in `core/bt/solve`, `grid` or `model`
changes behaviour; `model/problem` gained a runtime counter and a getter):

| Check | Result |
|---|---|
| `just test-all` (dev) | pass: 444 cases (18 `[slow]`, 69 `[parallel]`), `api-schema`, `api-docs`, `contract` (13 sessions, 94 methods covered); 405 s |
| `just test-asan` | pass: every case, `[slow]` included, and the contract suite against the ASan engine; 24 min |
| `just test-tsan` (the `[parallel]` cases, then the contract suite against the TSan engine) | pass: 69 cases, no report; the contract suite 13 of 13 against the TSan engine (finding 4's test among them) |
| contract suite, release engine, three times | pass, pass, pass (about 9 s each) |
| `just oracle --tier fast` | **19/19** (all fields, default threads, Classic) |

## Faithfulness beyond the gates
- The layer drawing's corners are the original's at its pixel scale for
  every cell of every layer of the five grids.
- The analysis writes exactly what the original's "Add disassembly" for
  all solutions writes, on nine fixture cases (Classic, with turns, and
  Crowell's take-apart).
- The paused solve file has the original's layout; the stored solver
  settings keep the original's names and values.

## Deliberate differences from the original
1. **The sphere grid's circles** are centred on their cells; the
   original's sat 0.09 of a cell up and to the left (a rounding of the
   widget's pixels: 3/10 of a cell taken off where 0.207 centres it).
2. **Layer polygons are counter-clockwise** (the original's corner order
   was its drawing calls' order); they come in cell units, not pixels.
3. **The analysis runs on a thread of its own** with progress and a stop
   (the original took apart on the GUI thread behind a modal window); like
   solving it records no undo step and freezes the document while it runs.
4. **The solutions are sorted as a view** (`solution.page`); the original's
   sort buttons reordered the problem's list, which a later save kept.
5. **A pause is refused when no run is under way** (the original's button
   was greyed out).
6. **Strings with bytes that are not UTF-8** (Latin-1 comments in old
   files: Prisgon's "Götz") reach a client with U+FFFD in their place;
   JSON carries only UTF-8. The document keeps the bytes; a client that
   writes such a string back replaces them.
7. API names differ from core-api.md's draft where a single method covers
   several (listed in core-api.md's status); the mockup's "drop below
   level" does not exist in the original (its "Drop" keeps every n-th
   solution: `keepEach`).

## Found in the original
- Solving and "Add disassembly" store the same take-apart with the
  separation's pieces in different orders (CubeInCage: `0 3 4 5 1 2` from
  the solve, `0 1 2 3 4 5` taken apart again): the same moves, the file
  differs. Both programs agree on each path; noted for anyone comparing
  files.
- The sphere grid editor's circles are off centre (difference 1).
- **A disassembly read from a file without `<dt>` plays in orientation
  0** (found while testing the single pose): a state without
  orientations answers `getOrient()` 0, `disasmToMoves_c::getTrans`
  passes it on, and both the original's view (`voxelFrame_c::updatePositions`
  re-transforms a piece whose `shapeOrients` differs) and its glTF
  sampler turn every piece to orientation 0 with the bounding-box
  compensation, instead of leaving it as the assembly placed it. Classic
  `<solutions>` are written without `<dt>`, so this is every classic
  solution reloaded from a file (PelikanBurr's fixture: piece 2, assembly
  transformation 13, is drawn turned by a 120 degree quaternion). The port
  is faithful (`test_app_dump` matches the original's sampler) and the
  single pose of `playback.open` is right (the pieces as placed). The fix
  is one line in `DisasmToMoves::getTrans` (answer `-1`, "not given", for
  a state without orientations, so the sampler keeps the assembly's
  orientation), to be taken as a deliberate difference and recorded when
  the front end's player is built; not done here, since the audit's scope
  was the engine.

## Known issues and notes
- **Windows:** stdout must be put in binary mode for frames (`_setmode`);
  the engine has not been built on MinGW yet.
- **Requests are serial:** `solve.abort` waits for the run to end and holds
  up the requests behind it; `explore.step` is capped at a million steps
  for that reason.
- **No TypeScript types yet:** the schema is ready to generate them from
  when the front end starts.
- **Undo depth** is an engine setting applied to the open document and
  every new one.
- **Flooding is held back, not refused:** a client that sends requests
  faster than they run is blocked on its pipe once 1,000 lines or 64 MiB
  are queued (decision 0012); nothing tells it so.
- **`solutionsVersion` starts at 0 for a loaded file** and is not saved:
  it relates reads within one open document only.
- **Declared-but-untested error pairs:** the contract client now refuses
  an errc a method does not declare, but most declared pairs are still
  never observed (the audit counted 114 of 159); a wrong extra entry in
  an `errors` list goes unnoticed.
- **The dt-less playback orientation** (see "Found in the original") is
  left as the original has it.
- Unchanged: `bt` aborts on the stacking fixture (stage 8); AVX2/AVX-512
  kernels untested on x86-64; LLVM missing, so nothing has been through
  clang-tidy; Burr-Glar on one thread behind the original (stage 8).

## Brief for stage 8 (the `bt` CLI and performance)
Work in `/Users/colin/git/burr-tools-rotations/burrtools_alpha`; the original
is `../burrtools_rotations`. Read AGENTS.md, docs/ARCHITECTURE.md (the
solve and engine sections above all), docs/PLAN.md (stage 8), decision
0007 (the speed rule), tests/bench/README.md, tests/oracle/README.md and
this note. Stage 7 is done and audited: the commit "Stage 7: engine,
JSON-RPC API schema and contract tests" (148d743) and the audit's fixes
in the commit after it, "Stage 7 audit: fixes for the nine findings"
(the list above; the engine's rules are in decision 0012 as amended).
Start stage 8 directly. Effort: high.

**Scope** (PLAN.md, stage 8):
- **Complete `bt`** (`cli/main.cpp`) as the original's `burrTxt` for brick
  puzzles (`src/burrTxt.cpp`): `-h [TYPE]`, `--progress` (stderr once a
  second), `-d`, `-p` (print the moves of each solution), `-s` (print the
  assembly), `-q`, `-n`, `-o n` and `-o all`, `-x` (only take the given
  solutions apart again: the analysis of stage 7), `-a s0 / s1 / c`, `-r`,
  `-R`, `-C`, `-t n`, `--solver`, combined short options (`-dR`); the
  same text output as burrTxt's, checked by a harness command that runs
  both programs on the corpus (record the original's output once, as the
  other recorders do); sliding and stacking puzzles refused with
  burrTxt's message instead of the abort (stage 6 known issue). Keep
  `--json` byte-compatible (the oracle depends on it) and the A/B
  switches. Decide whether `bt` also takes burrTxt2's solve-and-save mode
  (`tests/tools/bt_solve_file` does it for the harness now).
- **Benchmarks and the performance pass:** `tests/bench/bench.py` (the
  default set at 1 and all threads, `--plain`, `--set glar`), back to back
  on a quiet machine, best of 5; the gate is within 5% of the original or
  faster on each (decision 0007). Profile what is behind (macOS `sample`;
  Burr-Glar on one thread is the known gap) and close it without changing
  an answer (the oracle stays green). Record the results in
  tests/bench/README.md. Never benchmark while a sanitizer suite runs.
- **From stage 7, if time allows:** stdout in binary mode on Windows for
  the engine; the TypeScript types generated from `api/` (or leave them
  to the front end's first stage and say so). The dt-less playback
  orientation ("Found in the original") belongs to the front end's player
  stage, not here.

**Mind:** `core/` never prints: the CLI formats everything. The oracle,
compare-files, resume and roundtrip gates must stay green; `--json` is
what they read. Never use the sliding puzzle "One Way or Another"
(copyrighted); `test_sliding_solver.xmpuzzle` stands in.

Finish with the "Finishing a stage" steps of AGENTS.md (all of its gates,
the benchmarks among them), write `docs/handoff/stage-8.md` (the project's
state at the end of the plan, and what comes next: the front end, the
later features of PLAN.md), tick stage 8 in PLAN.md, one commit
`Stage 8: ...`.

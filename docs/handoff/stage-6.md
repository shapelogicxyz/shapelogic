# Handoff: stage 6 (the app layer and the meshes)

**Date:** 2026-10-07 · **Next:** stage 7, the engine, in a new session:
the API design at extra high effort first (PLAN.md), then the
implementation. A separate review of the API design is recommended before
the implementation (PLAN.md). The brief is at the end.

## Done

### The audit of stage 5 (its own commit, db3cb40)
The six findings of `tmp/audit/stage5report.txt` (local):
1. A run that does not take apart and ends with counted pending
   assemblies ends Paused (the search through), not Finished with the
   waiting solutions missing; the next run that takes apart finishes it.
2. The mirror and rotation filter flags are applied to a kept assembler
   on every run (`applySolutionFilterFlags`, as the solve thread did).
3. The stage 5 handoff and decision 0010 corrected: the original crashes
   (libc++ hardening) on any position after the last piece, its own
   assembler 0 one too (`setPosition` reads `rows[piecenumber]`), so such
   files continue in the new core only; added to "Found in the original".
4. `lastFinished_` is reset per run: a second solve's progress no longer
   starts at 99.9%.
5. Hotspots are reset when they are not where `initHotspot()` puts them
   (`Voxel::hasInitialHotspot`, the sphere nearest the origin on the
   sphere grid), not when they are away from the origin.
6. Tests for what the audit found untested, each failing with the code
   it covers broken (checked by mutation): the save point's meaning
   (nothing pending, a position) and a save point while draining ends
   Finished; late assemblies (the quick twins also on one thread); the
   running-twice guard (unconditional now); the tie order (Demo Piece
   Generator's ties with 8 and 16 workers); the position cleared while
   assembling; the search statistics (and the tried-apart count in every
   pause test's totals); the live sort; the thinning of a continued solve.
Also: the lock nesting stated in ARCHITECTURE.md; the phase cleared when
a run fails (7e). Not changed: the unsorted limit's thinning and the
solution numbers depend on timing (7b, 7c: the original's design); a
position that trips an assertion on restore errors on every resume (7d,
as the original).

### The meshes (`core/bt/mesh/`)
Ported from `src/halfedge/`, `voxel_c::getMeshInternal`,
`getDrawingMesh`, `getWireframeMesh`, `getFlatMesh`, the face geometry of
`voxel_0` to `voxel_4` (`getConnectionFace`, `calculateSize`,
`recalcSpaceCoordinates`; the `meshverts.inc` tables copied unchanged into
`tabs_0`, `tabs_1`, `tabs_3`, `tabs_4`) and `voxel_2_mesh.cpp`. The
half-edge structure refers by number, not pointer; faces and half-edges
keep the original's list order. `DrawMesh` is the plain output for a
binary frame. See ARCHITECTURE.md ("mesh").

### The app layer (`core/bt/app/`, decision 0011)
- `Document`: the puzzle, ids, revision, dirty flag, undo history
  (generalised `shapeHistory_c`: each step keeps the parts it changed and
  swaps them back and forth; groups; depth), every command of core-api.md
  sections 1 to 4 and the solution commands, the solves (a `SolveSession`
  per problem), queries, playback and explorers.
- `shape_tools`: the tool tabs' tasks of every grid (`tooltabs.cpp`'s
  `applyTask` / `cb_transform` / button lists), the cell tools
  (`grideditor.cpp`), the editor's size units.
- `queries`: the fit (`StatProblemInfo`'s line and a verdict), the status
  window's table, notchable / millable, the problem kind.
- `playback`: `PlaybackSampler`, `gltfexport.cpp`'s sampler (poses at any
  step, the keyframe track with holds and reassembly).
- `explorers`: `AssemblyExplorer` (placements, the search step by step)
  and `MovementExplorer` (the movement browser's tree).
- Two model additions (runtime only, nothing saved changes): a solution
  id (`Solution::getId`, given by its problem as it arrives, kept through
  sorting) and `Puzzle::replaceShape`.

### Harness
`oracle.py app-dump` records `tests/data/app_dump.txt` with
`tests/oracle/app_dump_old.cpp`, built against the original's library:
the drawing, wire-frame and flat meshes of every fixture shape, the
status window's table, every problem's fit line, and the playback tracks
of the first two solutions with a disassembly (gltfexport.cpp's sampler,
copied into the tool). `tests/unit/app/test_app_dump.cpp` makes the same
dump with the new core and compares it line by line (five fast fixtures
in the quick suite, the others `[slow]`).

### Tests (42 new cases: `tests/unit/app/`, `tests/unit/mesh/`)
- `test_meshes.cpp`: the original's `test_halfedge.cpp` case (two cubes
  meeting at an edge: every half-edge twinned), a cell of every grid closed
  with Euler characteristic 2 and its faces tagged, face counts, a ring,
  the triangle lists (tags, normals, edge lines).
- `test_app_dump.cpp`: the comparison with the original (above).
- `test_document.cpp`: every command with its undo / redo round trip
  (byte for byte through `writePuzzle`, the ids too) and what it does;
  every tool-tab task of every grid; refusals that change nothing; ids
  through reordering; solution ids; groups; the depth; dirty and the
  revision; save and open.
- `test_document_solving.cpp`: the guards of decision 0011 while a run is
  active; an edit of a paused problem's shape throws its solve away; a
  paused solve survives a change of the lists (a new session carries on
  from the saved position to the straight totals); a paused BurrTools 2
  search blocks such changes; the autosave flow (save point, save,
  carry on, and the file carried on in another document); reads during a
  run (`[parallel]`, for ThreadSanitizer).
- `test_tools_queries.cpp`: task sets, nudges, turns, centre, origin,
  prune, scale, minimize, fill holes, the surface, the cell tools; fit
  verdicts, the status line, the table, notchable / millable, the kind.
- `test_playback_explorers.cpp`: ported from `test_gltfexport.cpp` (a burr:
  seven pieces, none turning; the rotation moves turn pieces; a solution
  without disassembly refused), the track's times, mid-turn poses; the
  placements and the search stepped to a full assembly; the movement tree
  (analyse, matching move, a move by hand, prune).

## Gate results
| Check | Result |
|---|---|
| `just test-all` (dev, warnings as errors) | pass, 411 cases (394 quick, 17 `[slow]`); the quick suite 16 more times in dev and 15 in release |
| `just test-asan` (address + undefined) | pass, every case, `[slow]` included; the cases changed afterwards (timing only) again under ASan |
| `just test-tsan` | pass, 61 `[parallel]` cases, the document read while a solve runs among them; the solving cases again after the last change |
| `just oracle` every field, fast + full + slow, Classic | **180/180** with all threads, **180/180** with `--threads 1` |
| `just oracle --solver bt2`, every field, all tiers | **180/180** with all threads, **180/180** with `--threads 1` |
| `just oracle --solver crowell`, every field, all tiers | **178/178** with all threads, **178/178** with `--threads 1`; InsomniAC1 and CoverUp3 not compared (the original's hour-long timeout) |
| `oracle.py compare-files`, all tiers | **180/180** Classic, **180/180** BurrTools 2 |
| `just oracle-resume` | **395/395** (unchanged) |
| `just oracle-roundtrip` | **365/365** the same XML, **365/365** byte-identical |
| `oracle.py app-dump` against the original | recorded three times, identical each time; `test_app_dump.cpp` matches it line for line (2,793 lines) |
| `just check` | **not run: LLVM still not installed** |

The audit commit (db3cb40) passed the quick suite (three times in dev,
five in release), the session tests under ASan and TSan, and the fast
tiers of `oracle` (every field, the three solvers) and `compare-files`;
every mutation the audit named is now caught (checked one by one). After
it, three of its tests were made independent of timing (a slow machine
could let a run slip by unseen): they are in the stage 6 commit.

## Faithfulness beyond the gates
- The meshes are the original's face for face: every drawing and flat
  mesh of the 19 fixtures (all five grids) has the same faces in the same
  order with the same corners (as floats), flags, cell tags and colours.
- The status window's table and the fit line of every fixture, and the
  playback keyframes of 8 solutions (4 decimals), are the original's.
- Every tool-tab task and cell tool is the original's code path; the
  model operations underneath were checked in stages 1 and 2.

## Deliberate differences from the original
1. **The wire frame's faces** come in the order the faces and vertices
   were made; the original's hole filling went through sets ordered by
   object address, so its wire frames differ from run to run (in 112 to
   151 of the 170 fixture shapes between two runs of the same program;
   only their Euler characteristic is stable, and it is the port's).
2. **Undo covers every edit**, solutions included (the original's history
   kept shapes and problem definitions and lost solutions); a command
   undone gives the file back byte for byte. Solving is not undoable.
3. **While a run is active** shapes, the lists and the solving problems
   are frozen (decision 0011); the original greyed out only the shapes the
   solving problem used. Runs read shapes only: the document resets
   hotspots and warms caches first.
4. **Explorers work on a copy:** stepping the assembler or browsing
   placements never moves a solve (the original used the problem's
   prepared assembler, and a step could mark the problem solved).
5. **A nudge that would push cells out of the grid is refused** (the
   web UI's spec); the original dropped them.
6. Buttons the original showed but that did nothing on a grid (grid scale
   on spheres and tetra-octa) or only said "not implemented" (minimize on
   prisms, rhombic, tetra-octa) are not in the task sets.
7. `setGroup` (all pieces of a shape in one group, or none) and
   `deleteGroup` are new conveniences over the groups editor's counts
   (`setGroupCount`); the fit's verdict and hole range are new (the web
   UI's fit summary); the status line is the original's.
8. Not ported: the STL meshes (`getSTLMesh`, `cubemesh.cpp`,
   `minkmesh.cpp`) and the coplanar merge of the flat glTF meshes
   (exports, later); the 2D cell drawing of the grid editors
   (`grideditor_0` to `_4`, core-api's `grid.geometry`: stage 7); sliding
   start/goal editing; stacking.

## Found in the original
- **The wire frame is not deterministic** (difference 1): a view showing
  the same shape twice may draw different faces. Worth fixing there by
  ordering the hole filling's sets by index.

## Known issues and notes
- **Memory of undo:** a step that changes a list copies the puzzle with
  its solutions; the undo depth bounds it. Shape edits and new shapes
  copy only the shape.
- **The quick suite** grew by about six seconds (the app dump's quick
  twin about a second at -O0); the full dump is `[slow]` (the hole filling
  of Draculas Dental Desaster's wire frame is quadratic, as the
  original's: 3 s in release).
- `bt` (the CLI) aborts with an uncaught `InternalError` on the stacking
  fixture (`test_stacking_hanoi.xmpuzzle`): the CLI does not refuse
  stacking puzzles as the session does (stage 8).
- Unchanged: AVX2/AVX-512 kernels untested on x86-64; LLVM missing, so
  nothing has been through clang-tidy; Burr-Glar on one thread behind the
  original (stage 8); Crowell's InsomniAC1 and CoverUp3 not compared.

## Brief for stage 7 (the engine)
Work in `/Users/colin/git/burr-tools-rotations/burrtools_alpha`; the original
is `../burrtools_rotations`. Read AGENTS.md, docs/ARCHITECTURE.md (the
app, mesh and solve sections above all), docs/PLAN.md (stage 7),
docs/core-api.md (all of it: stage 7 turns it into the API),
docs/webui-spec.md, api/README.md, decisions 0003, 0004, 0010 and 0011,
and this note. Effort: extra high for the API design, high for the
implementation (PLAN.md); split the stage in two sessions if it runs long
(7a the design, the schema and the dispatcher with the document methods;
7b solving, events, frames, paging and the contract tests).

**Scope** (PLAN.md, stage 7): `engine/` (`bt-engine`, JSON-RPC 2.0 over
stdio, decision 0003) over `core/bt/app`, and `api/` (the JSON Schema).
- **Design first, in a decision record (0012):** the JSON library (a
  pinned one through `cmake/Dependencies.cmake`, or a small reader and
  writer of our own: decide), the message framing (one JSON message per
  line; binary frames announced by a JSON message with an id and a
  length, decision 0003), the error mapping (`Errc` to JSON-RPC codes,
  core-api.md "Errors"), the threading (the `Document` is called from one
  thread: a reader thread queues requests, the main loop runs them and
  polls the solves for events; nothing else touches the document), the
  id formats ("s7", "c2", "p1", "sol42": core-api.md's `Id` plus a
  prefix; a solution id is per problem), handles for playback samplers
  and explorers, and event throttling (progress a few times a second).
- **Methods** for core-api.md sections 1 to 7 on the stage 6 API: the
  document (`Document::open/openBytes/save/saveBytes`, `revision`,
  `dirty`, `history`, groups, settings), shapes and cells (commands of
  `Document`; `shape.transformSet` is `taskSet`; `shape.info` is a row of
  `shapeTable`; `shape.mesh` a `DrawMesh` frame, by `MeshStyle`),
  colours, problems (`problem.fit` is `FitStats`, `problem.kind`),
  solving (`solve.*` on the document's solve calls; `SolveOptions` from
  the request; `SolveProgress` and `SolveStats` into events), solutions
  (`solution.page` sorted and paged by the engine: `Problem::sortSolutions`
  and its comparators, rows built under `readProblem`; `solution.get`,
  `solution.delete`, `solution.toShapes`), playback (`PlaybackSampler`:
  `playback.open/poses/close`, pose tracks as frames) and the explorers
  (`AssemblyExplorer`, `MovementExplorer` behind handles).
- **Left from stage 6, to port in `core/bt/app` first (with tests and,
  where it computes, values recorded from the original):**
  `grid.geometry` (the 2D cell outlines of a layer for every grid: the
  drawing of `src/gui/grideditor_0.cpp` to `grideditor_4.cpp`, as plain
  polygons and circles); `solution.analyse` (take apart the saved
  assemblies without a disassembly as a job with progress and stop:
  `cb_AddDisasm` / `cb_AddAllDisasm` of `mainwindow_solve.cpp`, on the
  `Disassembler` of stage 4, refused while the problem is solved);
  `solve.exportPaused` / `importPaused` (`cb_ExportPaused` /
  `cb_ImportPaused` of `mainwindow_file.cpp`, the BTSOLVE2 format); the
  problem's stored solver options (`cb_SolverOptions`, kept in
  `Problem::getSolverOptions`).
- **Events:** `document.changed {revision, what, ids}` from
  `Document::lastChange()` after each command (and after a run ends:
  solutions arrive outside commands), `solve.state`, `solve.progress`
  (throttled), `solve.solutionsAdded`, `solve.error` (`SolveFailure`).
- **Schema and docs:** `api/` holds a JSON Schema per method and event;
  `docs/API.md` is generated from it by a script under `tools/` or
  `api/`; the contract tests check requests and replies against it.
- `engine.info` (version, grids, solvers, `hardwareThreads()`),
  `settings.get/set` (undo depth; threads and solver defaults are the
  engine's own values, passed into `SolveOptions`).

**Tests:** contract tests that start `bt-engine` as a subprocess and drive
it (a Python script under `tests/contract/` run by ctest, or a C++ test
with a pipe), checking every reply against the schema; a scripted
session: new and open (a fixture), edit cells in a group, undo and redo,
make a problem, solve with take-apart, pause, continue, page the
solutions sorted, play one back (pose frames), explore placements and
movements, save, and the errors (`invalid_state` while solving,
`not_found`, `parse_error`). Unit tests for the new app functions; the
harness stays green (nothing below `app` should change).

**Mind:** `core/` never prints; the engine owns stdout (JSON and frames)
and stderr (logs). A `Document` is movable, not assignable; solves end
when it is destroyed. Never use the sliding puzzle "One Way or Another"
(copyrighted); `test_sliding_solver.xmpuzzle` stands in. Sliding and
stacking stay unsupported (their files load and save; solving and
playback refuse them with `unsupported`).

Finish with the "Finishing a stage" steps of AGENTS.md (all of its gates,
the oracle ones included), write `docs/handoff/stage-7.md` with the brief
for stage 8, tick stage 7 in PLAN.md, one commit `Stage 7: ...` (or one
per half). A separate review of the API design is recommended before the
implementation (PLAN.md).

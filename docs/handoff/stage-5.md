# Handoff: stage 5 (the solve session)

**Date:** 2026-10-07 · **Next:** a separate audit of stage 5 (recommended by
PLAN.md after each solver stage), then stage 6 (the app layer) in a new
session at high effort. The brief is at the end.

## Done

### The audit of stage 4 (its own commit, 60dc717)
The three findings of `tmp/audit/stage4report.txt` (local) are fixed, each
with a test that fails without the fix. All three are bugs of the original
(1 and 2 of upstream BurrTools too) and change no answer on the corpus:
1. Groups are registered under the part index, as the take-apart asks for
   them (the original registered the shape id: pieces in several groups
   were never grouped, or grouped by another part's counts).
2. `Grouping::reSet()` gives the counts back: every take-apart of a
   disassembler starts with the counts registered (the original kept them
   used up; only a disassembler's first take-apart could group).
3. The turn generators give no turns from a position of more than
   `kMaxTurnPieces` (31) pieces: their piece masks are 32 bits, and the
   original shifted past them (undefined behaviour; 2^32 groups at 32).
   The parts such a position comes apart into get turns again.

Also from the report: `bt_solve_file` ended with "error in solver" when a
saved position was refused, as burrTxt2 (4a; the tool now runs the
session, which does the same); the Disassembler says its shapes and
hotspots must not change while it exists (4c; the session makes new ones
on every run). Before that commit, the pending renaming of the documents
to "BurrTools Alpha" went in on its own (836f0b1).

### The session (`core/bt/solve/`, decision 0010)
- `solve_session.{h,cpp}`: `SolveSession` (port of `solveThread_c` and of
  the GUI's Solve, Prepare, Continue, Abort, autosave pause and
  recordSearchStats), `SolveOptions` (the PAR_ flags, `SolutionOrder`, the
  limit and drop of kept solutions, threads, take-apart workers, the A/B
  switches of the assembly and the take-apart), `SolveFailure`.
- `solve_progress.{h,cpp}`: `SolveProgress` (port of `solveProgress_c` with
  `activity()`, word for word) and `SolveStats` (`solveStats_c`).
- What a run does is in ARCHITECTURE.md ("The solve session"): one thread
  of the session's own per run; moved hotspots reset; Preparing (matrix,
  reduce, `beginSolving`); Assembling (pending first, the assembler on a
  pool of its own, each assembly into a bounded queue that one
  `Disassembler` per worker empties, levels spread over a take-apart pool
  that counts the load of the assembly and the busy workers, the
  assembly's cores lent while it waits at a full queue); Draining; the end
  as Finished, Paused (position written into the problem), Idle (aborted)
  or Error.
- `AssemblerBt2`: a search that is through is saved as the dancing links
  position after the last piece (`"<pieces + 1> <iterations> "`) and
  restored as through (see the differences).

### Tools and harness
- `bt_solve_file` runs a `SolveSession` (it stood in for one since stage
  4): burrTxt2's options and defaults (reduce, the ten best by moves),
  continue unless `--restart`, "error in solver" and exit 1 on Error.
- `oracle.py compare-files` passes `--limit 0` only with `-d`: the saved
  assemblies of the `A` puzzles (not compared) keep the solve thread's
  limit. tests/oracle/README.md says so.

### Tests (`tests/unit/solve/test_solve_session.cpp`, 20 cases)
Ported from the original's `test_solver.cpp` ("Solver: ...": the thread
count, pausing and continuing with delays 2 to 300 ms straight on and
through a save, a pause as assembling starts, the autosave pause, no
solution limit, a damaged saved position errors once) and
`test_rotation_solvers.cpp` ("solve progress: ... only goes forward" for
all three solvers, with EnigmaTIC's tree checked as its solve-thread case
did). The SolidSixPieceBurrs cases are `[slow]` at -O0; each has a quick
twin on Demo Piece Generator (928 assemblies, 927 solutions). New: random
pauses (now and then at a save point) in memory and through saves, for
assembler 0, 1 and BurrTools 2 (in memory); a pause-delay sweep spread
over the whole solve (`[slow]`); a pause as each run starts; abort
(running, paused, through); refusals (no take-apart, sliding, stacking, a
solved problem, a solve saved without its position, running twice); the
preparation's error; a broken invariant on a worker thread; what is kept
(saved assemblies thinned, counts only, summaries only); the kept
solutions independent of which worker finished first; BurrTools 2 paused
while it drains and reloaded; a moved hotspot (before the solve and while
paused); the `[parallel]` case with workers, level threads, a symmetry
breaker (Bermuda) and progress read all the while. `test_assembler_bt2`:
the saved position of a search that is through.

## Gate results
| Check | Result |
|---|---|
| `just test-all` (dev, warnings as errors) | pass, 358 cases (342 quick, 16 `[slow]`); the quick suite five more times in a row (about 29 s each), the session's quick cases ten more times |
| `just test-asan` (address + undefined) | pass, every case, `[slow]` included |
| `just test-tsan` | pass, 59 `[parallel]` cases (3762 assertions): the session with workers, level threads, Bermuda (symmetry breaker, assembler 0 and BurrTools 2), EnigmaTIC with turns, progress, stats and `readProblem` read all the while; random pauses through saves; the kept solutions with four workers |
| `just oracle` every field, fast + full + slow, Classic | **180/180** with all threads, **180/180** with `--threads 1` |
| `just oracle --solver bt2`, every field, all tiers | **180/180** with all threads, **180/180** with `--threads 1` |
| `just oracle --solver crowell`, every field, all tiers | **178/178** with all threads, **178/178** with `--threads 1`; InsomniAC1 and CoverUp3 not compared (the original did not finish within an hour, as in stage 4) |
| `oracle.py compare-files`, all tiers (`bt_solve_file` on the session) | **180/180** Classic, **180/180** BurrTools 2 |
| `just oracle-resume` | **395/395** (unchanged: 76 single-thread pauses save the same text, 5 after the last piece continued by the new program only, 84 BurrTools 2 positions refused) |
| `just oracle-roundtrip` | **365/365** the same XML, **365/365** byte-identical (unchanged) |
| `just check` | **not run: LLVM still not installed** |

The audit commit (60dc717) passed the quick suite, the new tests under
ASan (the 33-cube probe no longer stops it), and the fast tier of `oracle`
(Classic, Crowell) and `compare-files`; the gates above cover it too.

## Faithfulness beyond the gates
- `compare-files` now runs the session itself: every saved solution of the
  original's solved files (burrTxt2) is found in the new core's with the
  same tree, level text and moves, Classic and BurrTools 2.
- The pause tests compare the saved solutions as a multiset (assembly and
  level text of each), not only counts, between interrupted and straight
  solves.

## Deliberate differences from the original
1. **One object across runs.** A `SolveSession` lives from Solve to the
   end; each run is a thread of its own. The search lives in the session
   between runs (in the original the problem held the assembler). The
   owner's calls are one at a time; `pause()`, the progress and the
   problem (through `readProblem`) from any thread.
2. **Stopping** (decision 0004): one `StopSource` per run with a child for
   the assembly (`pauseAtSavePoint`, the original's `stopSoft`); the
   caller only asks, the run's own thread winds down (the original's GUI
   thread stopped the disassemblers and joined the workers itself in
   `stop()`). A stop is asked for under the queue's mutex, so no wait on
   the queue misses it. `abort()` waits for the run and clears the problem
   (the GUI's Abort did both after deleting the solve thread).
3. **Hotspots** are reset before the matrix and on every run (the solve
   thread reset them after the matrix; burrTxt and burrTxt2 before it).
   Stage 4 found what that cost; a hotspot moved while a solve is paused
   no longer parts the take-apart from the kept matrix (a test). Only a
   hotspot away from the origin is reset: a reset drops the shape's
   caches, and warming them again on every run made each run slow enough
   to start that short pauses never let a take-apart finish.
4. **Equal solutions in assembly order.** Sorted by moves, level or turns,
   equals go by assembly number (the original: the order its workers
   finished). With the limit, the kept solutions no longer depend on which
   worker was faster (a test with one worker against four).
5. **A stopped take-apart is not counted as done** (the original's timing
   guard counted it, and the next run took it apart and counted it again).
6. **An exception on a worker thread ends the solve in Error** with the
   results dropped (the original's escaped its `std::thread`: terminate).
7. **BurrTools 2's search that is through** saves as the dancing links
   position after the last piece and restores as through, so a pause while
   its last assemblies are taken apart survives a reload. The original
   saved the start for it, so the reloaded solve searched again and
   reported every assembly twice (stage 3b's finding), and the new core's
   rule for started searches would have dropped everything. The original
   reads the new text (assembler 0 writes the same when through) and, as
   for every BurrTools 2 position, searches again.
8. **The saved position belongs to the paused state:** a run clears the
   problem's position text when it starts assembling and writes it when it
   pauses (the original wrote the live assembler's position on every save,
   while the search ran). Save a running solve at a save point.
9. **`setSolutionsWithRotations`** at the start of every run that turns
   pieces (the original set it in the constructor, before its own
   strict-colours rule could drop it again with the solutions).
10. **recordSearchStats** (searched steps, assemblies tried apart) is the
    session's, at every Paused and Finished; it was the GUI's.
11. **Counted pending assemblies** wait when a run does not take apart (the
    original queued them without workers and lost them).
12. **Two pools:** the assembly's (the original's assembler threads) and
    the take-apart's (`helperPool`, with the load accounting). One pool for
    both would hold the assembly's lent threads while it waits at a full
    queue, so the levels could not borrow them (decision 0010, the one
    exception to decision 0009's one pool per solve).
13. **Thread counts:** `SolveOptions::threads` is the original's thread
    limit (0: none); `takeApartWorkers` is `BURRTOOLS_DISASM_WORKERS`. The
    choices are the original's (`splitThreads`, `chooseDisasmWorkerCount`,
    `bt2Threads`).
14. **A changed solver between runs** applies to the take-apart; the
    assembly carries on with the assembler it has.
15. **The checks before a solve** (an assembler for the grid, a take-apart
    when asked for, a result shape, pieces; sliding and stacking refused)
    are the session's, returned as `Result` errors (the GUI's before).
16. **`progress()` from any thread:** the overall figure never goes back
    for anyone (an atomic maximum; the original's was for one thread).
    `getProgress(assemblyFraction)` (the GUI's variant) is not ported.
17. **One failure type:** `SolveFailure` holds the assembler's error and
    parameter, or the internal error's message, file and line (the
    original's errState/errParam and its assert exception).
18. Not ported: sliding, stacking and Panex (`runStacking`,
    `slidingSummary`, the slide searches, `solverNote`, the search depth and
    slide progress, `PAR_NESTED_SLIDES` and the other sliding flags).

## Found in the original
- **A take-apart worker's exception ends the program** (it escapes a
  `std::thread`), where an assert in the solve thread itself becomes
  ACT_ASSERT.
- **A stopped take-apart counts twice** in the assemblies tried apart.
- **Counted pending assemblies are lost** when a solve continues without
  taking apart.
All three worth fixing there; the hotspot finding of stage 4 is fixed here.

## Known issues and notes
- **BurrTools 2 cannot carry on a saved search** (decision 0009): an
  autosave of a BurrTools 2 solve helps in memory only; a reloaded one
  ends in Error (CannotRestoreInterrupted) with its results dropped, and
  the next run starts from scratch. Except when the search was through.
- **After an abort, nothing carries on:** the session drops its assembler
  on abort and on an exception, so the BurrTools 2 frame that a throwing
  callback leaves behind (stage 3 audit, 6c) is never re-entered.
- **Saving a running solve** writes a Solving problem without a position,
  which cannot carry on after loading; save at a save point
  (`pauseAtSavePoint`, then save, then `resume`).
- **The owner must not edit the puzzle while a run is active**, and edits
  between runs (a hotspot, a shape) are the app layer's to police: an
  edited problem is made Unknown by the model (`editProblem`), which drops
  the kept assembler at the next run; a shape edit does not touch the
  problems. Stage 6's Document should refuse shape edits while a problem
  using the shape is being solved, or abort it.
- Unchanged: AVX2/AVX-512 kernels untested on x86-64; LLVM missing, so
  nothing has been through clang-tidy; Burr-Glar on one thread behind the
  original (stage 8); Crowell's InsomniAC1 and CoverUp3 not compared.

## Brief for stage 6 (the app layer)
Work in `/Users/colin/git/burr-tools-rotations/burrtools_alpha`; the original
is `../burrtools_rotations`. Read AGENTS.md, docs/ARCHITECTURE.md (all of
it: stage 6 builds on every module), docs/PLAN.md (stage 6),
docs/webui-spec.md and docs/core-api.md (sections 1 to 4 and 7: what the
app layer must offer the engine), decisions 0003, 0004, 0005 and 0006, and
this note. Effort: high. If a separate audit of stage 5 has been done,
apply its findings first, as their own commit.

**Scope** (PLAN.md, stage 6): a new module `core/bt/app/` (it may use
everything below it; nothing below uses it) and `core/bt/mesh/`.
- `Document`: a `Puzzle` with its undo history (the original's
  `shapeHistory_c`, `src/gui/shapehistory.cpp`, generalised to every
  edit), a revision number bumped by every change, a dirty flag,
  begin/end grouping into one undo step (`edit.begin`/`edit.end`), stable
  runtime ids for shapes, colours, problems and solutions (core-api.md,
  "Conventions": labels follow the order, ids survive reordering; the file
  stores positions). Load and save go through `bt/io`.
- **Commands** for every editing action of core-api.md sections 1 to 4
  (shapes and cells, transforms per grid, resize, prune, centre, minimise,
  scale, fill holes, surface actions, colours, problems: result, piece
  counts and ranges, bulk ranges, groups, colour rules), each one undo
  step, each returning a `Result`. Port the logic from the original GUI
  where it lives there (`mainwindow.cpp` callbacks, `voxeleditgroup.cpp`,
  `grideditor*.cpp` for the cell tools, `bulkrangewindow.cpp`,
  `groupseditor.cpp`, `constraintsgroup.cpp`); the voxel operations exist
  (`Voxel`).
- **Solving from the Document:** a `SolveSession` per problem being solved
  (the session is stage 5's; the Document owns it). While a problem is
  being solved its shapes and the problem must not change (the session
  writes the problem and resets hotspots): refuse such edits with
  `InvalidState`, or abort the solve first (decide, and say which in a
  decision record). Reads of a solving problem go through
  `SolveSession::readProblem`. Solution deletion (`solution.delete`) and
  "convert assemblies to pieces" (`solution.toShapes`) belong here.
- **Queries:** fit statistics (`problem.fit`: result cells min/max, piece
  count range, piece cell range, holes, the verdict line; the original's
  `StatProblemInfo`/`StatPieceInfo` in `mainwindow.cpp` around line 1796,
  and the status window `statuswindow.cpp`: cells, symmetry,
  connectedness, identical shapes), `problem.kind`.
- **Meshes** (`core/bt/mesh/`): the drawing meshes of every grid as the
  original's 3D view draws them: `src/halfedge/` (polyhedron, faces,
  modifiers: port as value types, no owning raw pointers),
  `voxel_c::getMeshInternal` / `getDrawingMesh` / `getWireframeMesh` /
  `getFlatMesh` (`src/lib/voxel.cpp` from line 1001), the spheres'
  `voxel_2_mesh.cpp`. Output a plain mesh (positions, normals, indices,
  per-face voxel tags) the engine can send as a binary frame. The STL
  meshes (`cubemesh.cpp`, `minkmesh.cpp` with the manifold library, the
  view's "STL style") belong to the exports, later: leave them out, and
  say so.
- **Playback sampler:** generalise `DisasmToMoves` (stage 4) into what
  `src/lib/gltfexport.cpp` samples: per piece, at any step and fraction,
  translation, rotation (quaternion, the turns about their pivots) and
  visibility; `samplesPerStep`; reassembly. No glTF writing (exports are
  later).
- **Explorers:** placements of a piece in the result (the assemblers'
  placement browser API, `placementbrowser.cpp`), the movement browser
  (`movementbrowser.cpp` over `MovementAnalysator::completeFind` and
  `findMatching`, which stage 4 kept for it), the assembler's single step
  (`debugStep`, `currentAssembly`).

**Tests:** a unit test per command, undo/redo round trips (every command
undone gives the puzzle back byte for byte through `writePuzzle`, redone
gives the edited one), ids stable across reordering, the solving guards
(an edit refused or the solve aborted, as decided), mesh checks (ported
from `test/test_halfedge.cpp`; watertight meshes, face counts per grid,
the voxel tags), the sampler against `test/test_gltfexport.cpp`'s cases
(a burr, the rotation moves of `test_rotation_solvers.xmpuzzle`; a
solution without disassembly is refused), fit statistics against values
taken from the original (record them with a dump tool compiled against
the original's library, as `oracle.py model-dump` does). The harness stays
green (nothing below `app` should change).

**Mind:** core/ never prints and has no global state; the GUI settings of
`configuration.cpp` that are not UI (threads, undo depth, solver defaults)
become plain values the engine passes in. Never use the sliding puzzle "One
Way or Another" (copyrighted); `test_sliding_solver.xmpuzzle` stands in.
Sliding and stacking stay unsupported (their files load and save).

Finish with the "Finishing a stage" steps of AGENTS.md (all of its gates,
the oracle ones included), write
`docs/handoff/stage-6.md` with the brief for stage 7, tick stage 6 in
PLAN.md, one commit `Stage 6: ...`.

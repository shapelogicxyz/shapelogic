# Handoff: deferred part 1 (sliding puzzles: core, engine, CLI)

Part 1 of docs/DEFERRED-PLAN.md: the original's sliding puzzles ported
into the core, solved through the solve session, edited through the
document and the engine's new `sliding.*` methods, solved by `bt` as
burrTxt solved them, and compared with the original by a new `sliding`
tier of the harness. Decision 0014 records the shape of it. Next: the
audit of this part (Fable 5.1, extra high effort), then part 2, the
sliding front end.

## Done

Four commits: `ab15050` "Core: sliding puzzles: the tray model, the slide
search, sliding solves, routes", `d9b26cd` "Engine: sliding solving:
problem.kind, engine.info, solve options, progress, note, settings",
`91c3a4e` "Engine: sliding.*: start/goal shapes and stamps", then the
part's "Deferred 1: …" (the CLI, the harness and its recordings, the
front end's notices, the documents).

### The core (`ab15050`)
- `bt/model/sliding.{h,cpp}` (from `src/lib/sliding.{h,cpp}`, the tray
  part): start/goal shapes (`sg:<n>:` names, user names, the display name,
  hidden `__sggoal:` shapes), stamps (`stampPiece`, `stampCells`,
  `stampFits`, `stampIsAt`, `clearStamp`, `stampAt`,
  `dropChangedStamps`), walls (`toggleCellMark`), `syncSlidingProblems`,
  `shapeIsRequired`, `startGoalError`, `stampOverflowMessage`, the start
  locks (`refreshStartLocks`, `pieceColor`), the marks renumbered when
  shapes go or swap (`noteShapeRemoved`, `noteShapeSwap`), the older
  goal-map model (`ensureSetup`, `placeStart`, `placeGoal`, `toggleGoal`,
  `clearStart`, `clearGoal`, `clearStartAt`), `syncMaxHoles`,
  `flattenPieces`, `floorCells`, `prepareForSolving` and
  `needsPreparing`.
- `bt/solve/slide_levels.h` (from `slidelevels.h`): `KeySet`, the full
  search's sorted level files (varint deltas, an index, in memory up to a
  budget, then in the work folder).
- `bt/solve/sliding_search.{h,cpp}`: the three searches of the original
  (the text-key search, `legacy`; the position-table queue search, Fast
  and Deep; the level search, Full), nested and partially nested slides,
  one-way moves, copies of like pieces made one, the memory budget
  (`memoryStates`), the outcomes, `slideRoute`, `applySlideRoutes`,
  `finalPlacementKey`; `SlidingOptions` and `slideSearchFor()`.
- `bt/base/system`: `physicalMemoryBytes()` (the memory budget) and
  `userCacheDirectory()` (for the CLI and the engine; the core does not
  call it).
- `SolveSession`: the solve thread's sliding run (see decision 0014): the
  options it forced, the slide pool per run, the search in the assembly
  callback, the pending start on a stop, the shorter path per ending, the
  summary (`note()`, `slidingSummary`'s words), the progress stage
  `Other` with `searched` and `searchDepth`, the searched states added to
  the statistics.
- The document (`document_sliding.cpp` and hooks in the commands): the
  start/goal commands, the shape commands keeping stamps and problems in
  step, the refusals of what the GUI greyed out, Solve's checks
  (`startGoalError`, `stampOverflowMessage`) and its setup at launch,
  flattening on open with `openNotice()`, `ChangeSet::notice`,
  `solveNote()`. Playback follows routes; the assembly explorer works on
  sliding problems.
- Tests: `test/test_sliding.cpp` ported in full (32 cases,
  `tests/unit/solve/test_sliding.cpp`; the legacy-against-tables case is
  `[slow]`, the full search on 1 and 4 threads `[parallel]`);
  `test_sliding_session.cpp` (9 cases: 1 against 4 threads, random
  pauses, the save point, the notes, abort, Find Solutions off, the
  limit, the start locks, the playback's corners);
  `test_document_sliding.cpp` (6 cases: the 81-move fixture rebuilt
  through the commands and solved, stamps, the shape commands, the greyed
  buttons, Solve's checks, flattening); `test_slide_routes.cpp` (below).

### The engine (`d9b26cd`, `91c3a4e`)
- `problem.kind` and `engine.info`'s grids: sliding `supported: true`;
  `engine.info.slidingSolvers` with the original's menu labels.
- `SolveOptions.sliding` (`solver` fast/deep/full, `nestedSlides`,
  `partialNested`, `highMemory`); `SolveStage` `search`;
  `SolveProgress.searched`, `searchDepth`; `solve.status.note`.
- Settings `slideFolder` (default the user cache folder's `sliding`) and
  `slideDiskGB` (0: automatic), both in `settings.get`/`set`.
- `SolverSettings.partialNested` (the original's newest box).
- `sliding.addStartGoal`, `sliding.place`, `sliding.clearAt`,
  `sliding.tray`, `sliding.preview` (`api/sliding.json`).
- `ShapeSummary.startGoal` (the number; `name` is the user's part for a
  start/goal shape), `Part.required`, `document.changed.notice`,
  `Snapshot.notice` (only in the result of `document.open`,
  `document.openBytes` and `solve.importPaused`). `problem.setResult`,
  `problem.removePiece` and `problem.deleteGroup` declare
  `invalid_argument` (a sliding puzzle's refusals).
- `Params::point` ([x, y]) beside `cell`.
- Contract: `sliding_solve_pause_continue_and_play` (the 81-move fixture
  on the full search and played, the nested fixture with and without
  nested slides, a deep search paused mid-search and continued, its
  note) and `sliding_start_goal_shapes` (every `sliding.*` method, the
  notices, the refusals, other grids). 100 methods, all called.

### `bt` (this part's commit)
- `--solver` "Sliding Fast/Deep/Full Solver" (also `fast`, `deep`,
  `full`; the default the full search), `--nested-slides`,
  `--partial-nested` (implies nested), `--high-memory`, and the A/B
  switches `--slide-legacy`, `--slide-apart`, `--slide-queue`,
  `--slide-trace`; a help section for sliding puzzles.
- The solve: the matrix as the solve thread built it (strict colours,
  mirrors and rotations kept, the start locks and maximum of holes set
  up), a slide search per start layout in the callback, the shorter path
  per ending, the original's notes after the summary; `-x` (the saved
  solutions' routes again), `--save` (a `SolveSession`; `-d` allowed:
  see the differences), `--json`.
- `BURRTOOLS_SLIDE_DIR` and `BURRTOOLS_SLIDE_DISK_GB` read by the CLI
  into the options.
- Stacking stays refused; `--panex-dir`/`--panex-disk` refused with a
  message saying so.
- `tests/cli/test_cli.py`: the help, the refusals, each solver's
  `--json`, nested and partially nested, `--save` then `-a` and `-x`.

### The harness (this part's commit)
- `sliding_puzzles_old.cpp` (`oracle.py sliding-puzzles`): 33 puzzles
  made with the original's code from its `test_sliding.cpp` and 12 fuzzed
  ones among them, saved in `tests/data/puzzles/sliding/`.
- The `sliding` tier in `corpus.txt` (25 lines, 36 puzzles once the fuzz
  glob is expanded: the three fixtures and the generated puzzles; flags
  `N`, `P`, `L`, `O`; `alpha:` paths) and
  its baselines for the three solvers (`baseline/sliding*.json`,
  `baseline/text-sliding*.json`), all recorded on one thread.
- `slide_solve_old.cpp`: the original's solve thread solving and saving
  (burrTxt2 refuses `-d` on the grid), for `baseline-files`;
  `compare-files` compares the files state by state.
- `slide_routes_old.cpp` (`oracle.py record-slide-routes`): twelve
  puzzles solved by the original
  (`tests/data/slide_solved_by_original/`), every move's route and the
  player's positions at quarter steps (`tests/data/slide_routes.txt`),
  checked by `tests/unit/solve/test_slide_routes.cpp` (with the playback
  sampler's translations).
- `oracle.py`: `--solver fast|deep|full` (`full` the default's
  baselines), sliding entries run on one thread on the old side, `O`
  entries compared with `--threads 1` only, `L` entries skipped by the
  full search, the round trip includes the tier.

### The front end (this part's commit)
The front end reads `supported` from the engine, so a sliding file now
opens with the Solver enabled, solves (with the default sliding options:
the full search) and plays along its routes. The "not yet" words now name
stacking only (the Puzzle's notice, the Solver's notice, the empty
scene's text, the player's message). New does not offer the sliding grid
yet: without the start/goal editor (part 2) a new sliding puzzle could be
given no start or goal. e2e: the solver test solves
`test_sliding_solver.xmpuzzle` with `bt --json`'s counts and plays its 81
moves (poses at 0 and 81 the engine's), the puzzle test checks that
sliding has no notice and stacking has; parts carry `required`.

### Documents
Decision 0014 (new); decision 0012's "Not yet"; ARCHITECTURE.md (the
state, grid, model, the slide search, the session, app, engine, cli, web,
testing); README's status; `tests/oracle/README.md` (the sliding tier);
`grid_type.h`; docs/API.md and the generated types.

## Gate results

All run on the final tree (2026-10-10, M-series Mac):
- `just test-all`: 6/6 (501 unit cases, 19 of them `[slow]`; the api
  schema and docs; the contract suite, 100 methods called; the CLI), 404 s.
- `just test-asan`: 6/6, no sanitizer report, 1,288 s.
- `just test-tsan`: the `[parallel]` cases (74 cases, 12,481 assertions,
  the sliding ones among them) and the contract suite on the TSan engine,
  clean.
- `just oracle --tier fast`: 19/19 (all fields).
- The sliding tier, summaries: all threads 26/26 (the 9 `O` puzzles left
  to one thread); one thread 35/35 full search (the `L` puzzle left
  out), 36/36 fast, 36/36 deep.
- Solved files (`oracle-files`): sliding 35/35 full, 36/36 fast, 36/36
  deep, state by state; the brick fast tier 19/19.
- Text (`oracle-text`): sliding 245/245 full, 252/252 fast, 252/252
  deep, byte for byte; the brick fast tier 115/115.
- `just oracle-roundtrip`: 432/432 byte-identical to the original's save.
- `just oracle-resume`: 395/395.
- `test_slide_routes.cpp`: the routes and quarter-step positions of 12
  puzzles' solutions equal the original's (in `just test`).
- `just check`: not run (LLVM is not installed on this machine).
- Front end, three times each: `just web-check` clean, `just web-test`
  292/292 (release engine), `just web-e2e` 61/61. `web-dist` and
  `web-smoke` not run: nothing about packaging changed (no new resource
  or dependency).

## Deliberate differences from the original, and why

1. **Nested slides with more than 64 pieces use the text-key search.**
   The original's position tables overran their fixed arrays there; the
   tables refuse and the legacy search (the original's own first search)
   runs. The answers are the original's where it did not overrun.
2. **The full search's threads are a `ThreadPool` job** (decision 0004:
   no raw threads); the job of index 0 saves the finished level and then
   expands like the others (`run(threads + 1)`); the original gave the
   saver a thread of its own. Same path on any number of threads.
3. **No environment and no printing in the core**: the work folder and
   the disk budget are options (`bt` reads the original's variables, the
   engine has settings); the level trace is a callback (`bt
   --slide-trace` prints it).
4. **A start lock that is already right is not written again** (the
   write dropped the shape's caches while a run's threads read them).
5. **`bt --save -d` solves sliding puzzles**: burrTxt2 refused `-d` on
   the sliding grid, which has no take-apart; the GUI's Solve ran the
   solve thread's sliding run, which is what `--save` runs.
6. **Solve's setup is part of the launch**, after every check that can
   refuse, and is not an undo step (decision 0011: what solving does is
   not undoable); the original's solve thread set it up as it started.
7. **The engine's API**: a `sliding.*` namespace; `y` rises upwards in
   `at` as in the grid editor; a start/goal shape's name is the user's
   part with the number apart (`startGoal`); the start/goal editor's
   reads (`sliding.tray`, `sliding.preview`) are methods, where the GUI
   read the puzzle.
8. **The front end's New leaves the sliding grid out** until part 2
   brings the start/goal editor.

## Found in the original

- **Several threads give different counts on puzzles with several
  starts.** The solve thread keeps, for each final picture, the first
  path found or a shorter one found later (which counts again, as
  burrTxt's counter did); which start comes first depends on the
  assembler's threads, so `slide_shorter_path` gave 2 or 4 solutions from
  run to run. The new core is faithful (its assembler's order varies
  too); the harness records one thread's answers and compares the `O`
  puzzles on one thread only.
- **The position tables overran their arrays** with nested slides and
  more than 64 pieces (difference 1).

Not fixed in `../burrtools_rotations` (never modified; it had someone
else's uncommitted edits to `solvethread.cpp`, `symmetries.h`,
`symmetries_2.*` and `tabs_2` throughout this part, left alone).

## Known issues and notes

- `bt` and the engine count solutions as the original did (a shorter
  path to a picture already counted counts again): on several threads a
  puzzle with several starts can give different counts from run to run,
  as the original's did. Kept for faithfulness; part 2 may want the
  Solver to say so.
- `slide_swapped_fifteen` (flag `L`) is too long for the full search in
  the harness (the original takes 8 to 14 s, the fuzz_11 full search 33
  to 39 s); the fast and deep solvers run it.
- The `[sliding]` unit cases take minutes in the dev build; the slow
  comparison of the legacy search is `[slow]` (33 s at `-O0`).
- The front end has no sliding options yet (it sends none: the full
  search, no nesting); part 2 adds them, with "Enable High Memory".

## Brief for the audit of part 1

Audit part 1 with Fable 5.1 at extra high effort: the diff of `ab15050`,
`d9b26cd`, `91c3a4e` and the "Deferred 1" commit, the gate results above,
this note and the plan. Where to look hardest:
- the slide search's three searches against the original's
  `sliding.cpp` (keys, nested and partially nested outlines, one-way
  moves, like pieces, the memory budget, the `Disk` and `Memory`
  outcomes, the level files and their removal);
- the solve session's sliding run: stops at every point (the search, the
  assembler, a save point), pending starts, counts after pause and
  continue, the note, threads (TSan), the slide pool's budget next to the
  assembler's;
- the document's hooks: every shape command on a sliding puzzle with
  stamps, undo and redo byte for byte, the notices, solve launch's
  prepare after its checks, the frozen state while solving;
- the engine's `sliding.*` methods: ids, bounds of `at` and `size`,
  refusals and their errc, the notices' presence and absence;
- `bt`'s sliding modes against burrTxt (the harness covers `--json`,
  the text and the files; `-x`, `--save` on a paused file, `-t`).

## Brief for part 2 (after the audit)

Part 2 of docs/DEFERRED-PLAN.md, the sliding front end: inventory the
original GUI's sliding branches first and write the screens into
webui-spec.md, then build the tray editor (walls and floor), stamping on
start and goal (`sliding.place`, `sliding.preview`, `sliding.clearAt`,
`sliding.tray` for the validity bar), the sliding solver options with
"Enable High Memory" (`SolveOptions.sliding`), the notices
(`document.changed.notice`, `Snapshot.notice`), the walls in 3D, and New
offering the sliding grid (`web/src/renderer/shell/files.ts` and
`tests/e2e/files.e2e.ts` filter it out until then). Read the "Notes for
part 2" of the Audit section below first.

## Audit

**Date:** 2026-10-10 · Fable 5.1 at extra high effort, on the diff
`4a3d6ba..ed37914` (`ab15050` Core, `d9b26cd` and `91c3a4e` Engine,
`ed37914` Deferred 1), the gate results above, this note, the plan and
decision 0014. The fixes are one commit after the part's: `Deferred 1
audit: refusals the GUI's greyed buttons gave, marks past the 63rd place`.

### How it was done
- The three searches, the level files, the routes and the tray model read
  side by side with the original's `sliding.cpp`, `slidelevels.h` and the
  sliding branches of `solvethread.cpp`, `burrTxt.cpp` and
  `mainwindow.cpp`; the session's sliding run against the solve thread's
  callback, stops, note and statistics; the document's hooks against the
  GUI's callbacks and the buttons it greyed out or hid.
- Suspicions probed with small programs against
  `build/release/core/libbtcore.a` in the session's scratchpad (not
  committed), and `bt -x` against `burrTxt -x` on three solved files
  (`-x`, `-x -p`, `-x -s`, `-x -q`: byte-identical).
- Each test added was run against the part's sources (the fixed files
  copied aside, the part's versions written in their place, copied back)
  and the colour refusal against a mutant with that block alone removed:
  each fails without its fix.

### Findings, by severity
1. **Medium: deleting a colour renumbered a sliding puzzle's start marks
   and locks.** The start marks are cell colours (shape index + 1) and the
   pieces' locks the same numbers; `Puzzle::removeColor` moves every
   higher cell colour down by one and leaves the goal marks (`goalPiece_`)
   where they are. The original hid the colour group on sliding puzzles;
   the document, and so `colour.delete` (the Entities workspace offers
   the colour chips on every grid), allowed it. Probe: the 81-move
   fixture opened, colour 1 deleted: every start mark and lock one lower
   (S3's start now says S2, and so on), `startGoalError` "The start of S2
   is not the whole piece. Place it again.", the file saved wrong, the
   problem unsolvable. Fix: `Document::deleteColour` refuses
   (`invalid_argument`) on a sliding puzzle; `colour.delete` declares the
   error. `colour.add` stays (harmless: the marks keep their numbers).
   Tests: "what the original greyed out is refused" (`deleteColour`
   refused, `addColour` leaves the start/goal check as it was), the
   contract case `sliding_start_goal_shapes`.
2. **Low: a stamped piece moved past the 63rd place threw an
   assertion.** A mark is six bits; `noteShapeSwap` swapped mark 63 to 64
   and `Voxel::setColor` asserts `color < 64`: an `InternalError` out of
   `shape.move`, the step uncommitted. The original has the same
   `bt_assert` (found in the original, not fixed there). Probe: a sliding
   document with 64 pieces, the 63rd stamped, moved one place up:
   "assertion failed: color < 64". Fix: `noteShapeSwap` drops a mark that
   would go to a shape past the 63rd place and returns the start/goal
   shapes that lost one; `Document::moveShape` then syncs the problems
   and says so in `document.changed.notice` ("Only the first 63 shapes
   can have a start or goal: S64 moved past the 63rd place and its start
   and goal were removed from S1."). Test: "a piece moved past the 63rd
   place loses its marks, not the puzzle" (moved up, and crossed by a
   shape moved down; undo and redo byte for byte; no notice when nothing
   crossed).
3. **Low: the problem buttons the original greyed out (New, Copy and
   Delete problem) were not refused.** A sliding puzzle's problems follow
   its start/goal shapes (`syncSlidingProblems`): a problem added was
   dropped by the next stamp (its id gone), a deleted START/GOAL problem
   came back at the next stamp with a new id and only the stamped pieces
   (the user's counts of free pieces lost), a copy doubled a tray's
   problem for good. The Puzzle workspace offers all three. Fix:
   `createProblem`, `duplicateProblem` and `deleteProblem` refuse
   (`invalid_argument`) on a sliding puzzle; `problem.create`,
   `problem.duplicate` and `problem.delete` declare the error. Tests: the
   greyed-out case, the contract case.

### Probed and cleared
- Concurrent slide searches: `onSlidingAssembly` runs the search outside
  `resultsMutex_`, but the assembler's run layer (`SearchRun::deliver*`)
  holds its hand-over mutex across every callback ("never called by two
  threads at once"), as the original's `assemblyCallbackMutex` did: one
  search at a time, so the memory and disk budgets hold.
- The three searches, `KeySet`, `mergeRuns`, the level search's `Memory`
  and `Disk` outcomes and trace-back, `findSlidePath`, the routes and the
  keys: line for line the original's; the differences are decision 0014's
  four.
- The session: a pause stops the search under way (pending, not counted);
  a save point lets it finish while the assembler's other threads drop
  what they hold (`deliverOnce` after a stop), as the original's
  `stopSoft`; the note, the statistics (`searched` adds the last search's
  count per run, as `recordSearchStats` did) and the stage `Other` match
  the original; the slide pool's threads add to the assembler's as the
  original's did.
- The document: every shape command on stamped pieces (edit, resize, the
  tool tasks, delete, move, duplicate, rename) keeps the marks or drops
  them as the GUI did; `solveLaunch`'s prepare runs after every refusal
  the session has; `moveShape` does not resync (the GUI's exchange did
  not either), the next launch repairs the locks; a second tray's launch
  while another runs is refused when it would repaint the pieces.
- The engine's `sliding.*`: ids, bounds, refusals and notices as the
  schema says; `bt -x` equals `burrTxt -x` on solved sliding files.

### Gate after the fixes
- Release: `bt_tests "[app]"` 63 cases, `"[sliding]"` 46 cases (benches
  and fuzz left out), the contract suite (100 methods called), the api
  checks; `just web-gen` and the generated types' test.
- `just test-all` (dev): 6/6 (unit, the slow cases, the api schema and
  docs, the contract suite, the CLI), 395 s; `just web-check` clean.

### Notes for part 2
- Hide the colour chips and the New/Copy/Delete problem buttons on
  sliding puzzles (the engine refuses them now) and show
  `document.changed.notice`.
- The editor should offer no colours on a sliding puzzle: a cell painted
  with one is a stray start mark cell (the GUI forced the Walls tool
  there); `shape.setCells` does not refuse it.
- `shape.rename` with a name beginning "sg:" turns a piece into a
  start/goal shape until the next sync (the GUI's cb_NameShape had the
  same hole): refuse such names in the front end.

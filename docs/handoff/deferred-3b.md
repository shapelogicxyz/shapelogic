# Handoff: deferred part 3b (stacking and Panex: the document and the engine)

Part 3 of docs/DEFERRED-PLAN.md, its second half (part 3a,
docs/handoff/deferred-3a.md, ported the core and `bt`): the document's
stacking commands, ported from the original GUI's callbacks and each one
undo step, and the engine: stacking solves with their saved searches in
the engine's Panex folder, and a `stacking.*` namespace for the rod sets,
the discs, the stacks and the reads the editor and the player need.
Decision 0015 is amended with what 3b decided. Next: the audit of part 3
(3a and 3b together; Fable 5.1, extra high effort), then part 4, the
stacking front end.

## Done

Five commits: `e6ab98f` "Core: stacking puzzles in the document: rod
sets, discs and stacks", `f736508` "Engine: stacking solves: problem.kind,
stackingSolvers, SolveOptions.stacking, the Panex folder, saved
searches", `1453ded` "Engine: stacking.*: rod sets, discs, stacks and the
board", `a6a17c6` "Core: a disc for a rod past the board's count is
refused, not asserted" (found while writing this note: `syncMaps` also
drops the empty rods past the count, which the document's check of a
rod did not count; a regression test), then the part's "Deferred 3b: …"
(the front end kept on its notices, the recorder's neutral home, the
documents).

### The document (`e6ab98f`)
`core/shapelogic/app/document_stacking.cpp`, from mainwindow_rods.cpp (cb_NewRod,
cb_DeleteRod, cb_CopyRod, cb_NameRod, cb_RodExchange, cb_RodField with
syncPanexFields), BlockList.cpp's disc list (DiskSelector::applySize,
onDelta, onSize) with cb_DiskList, cb_NewShape's stacking branch, and the
stacking branches of the Problems tab in mainwindow.cpp (cb_ShapeToResult,
cb_AddShapeToProblem with placeOneDisk, cb_AddAllShapesToProblem,
cb_RemoveShapeFromProblem, cb_RemoveAllShapesFromProblem,
cb_ProbShapeExchange):
- **Rod sets** have ids beside the puzzle's (`rodSetIds()`,
  `rodSetIndex`, `rodSetLabel`: R1, R2, as the GUI's list labelled them;
  kept in the whole-puzzle undo parts, assigned on open): `addRodSet`,
  `duplicateRodSet`, `deleteRodSet` ("Keep at least one rod set"; the
  problems on it stand on none, their stacks kept), `renameRodSet`,
  `moveRodSet` (one place at a time, the problems keep their board),
  `setRodSetRules` (cb_RodField: rod count 1 to 16, heights 1 to 64, the
  pocket only with Panex columns, the outside channel with Panex columns
  and three rods or more, "can move over" written off; the problems on
  the set get their stacks fitted (`syncMaps`) and lose their solutions).
  Each saves the whole puzzle for undo; refused while anything is solved.
- **Discs**: `createShape()` on a stacking puzzle is cb_NewShape's
  `addDisk(1)` (a size refused); `setDiscSize` 1 to 100 (the disc list's
  bounds and its message), nothing at an equal size, solutions kept
  (cb_DiskList kept them), a paused session of a problem with the disc
  ended.
- **Stacks**: `setProblemRodSet` (Set Start/Goal Rods: `setRodSetId` +
  `syncMaps`), `placeDisc` (placeOneDisk: one more copy, and the minimum
  with it, when every copy is on that stacking; the rules not enforced),
  `placeAllDiscs` (largest first; `DiscsPlaced`: how many and why the next
  did not go), `removeDisc` (the copy off both stackings, the later ones
  renumbered, one copy fewer), `clearStacks` (the edited stacking emptied,
  changeProblem), `moveDisc` (±1 on its rod; returns the new place). The
  rod and the place are the stack map's (the pocket last; places from the
  bottom). Refused while their problem is solved; "Choose a rod set with
  Set Start/Goal Rods" without a board.
- **What the GUI hid is refused** (`InvalidArgument`): `editCells`,
  `editBox`, `resizeShape`, `shapeTask` (the cell editor and the tool tabs
  were replaced by the disc list), `setResult`, `setPieceCount` (and
  `addPiece`, `removePiece`), `setRangeAll`, `addOneOfEach`,
  `removeAllPieces`, `movePart`, the groups (the Problems tab's buttons did
  the stacking commands there).
- `savedSearch(problem, folder)` → `SavedSearchInfo` (description, ms,
  level bytes; none while the problem's run is active) and
  `solveAbort(problem, folder)`: a search saved on disk that no session
  holds is a solve under way, thrown away with the problem's results
  (cb_BtnAbort, abortable()).
- `app::rodSetLayout` (queries): a rod set drawn alone (showRodSet).
- `ChangeFlag::kChangeRodSets`; `checkText` shared through
  `document_internal.h`.

### The engine (`f736508`, `1453ded`)
- `problem.kind` and `engine.info.grids[].supported` true for every grid;
  `engine.info.stackingSolvers` (Stacking Solver, Panex Solver).
- `SolveOptions.stacking` {solver, highMemory, autosave}; no solver named:
  the one the original's menu offered for the problem's board (Panex
  columns: the Panex Solver). `SolveProgress.searchTraced`, `panexSearch`.
- Settings `panexFolder` (default the user's cache folder's `panex`, as
  `bt` and the original) and `panexDiskGB`, given to every stacking solve,
  `solve.status.savedSearch`, `solve.abort`, `solve.exportPaused`
  (`withLevels`, default true) and `solve.importPaused`.
- `solve.savePoint` on a stacking solve: refused (`unsupported`): the
  original never autosaved one, its search saves itself.
- A stacking solution's row counts transfers (moves, steps, text), as
  the playback and the original did.
- `invalid_argument` declared for `solve.start/prepare/continue` (it was
  already answered, e.g. "a result shape must be defined"), and on the
  brick commands the stacking puzzle refuses.
- `stacking.*` (`engine/methods_stacking.cpp`, `api/stacking.json`):
  `addRodSet`, `duplicateRodSet`, `deleteRodSet`, `renameRodSet`,
  `moveRodSet`, `setRules` (the fields given over the rod set's as shown),
  `setDiscSize`, `setProblemRodSet`, `placeDisc`, `placeAll`, `removeDisc`,
  `clear`, `moveDisc`, `board` (rods with names, pocket, position and
  capacity; the stacks; the layout of rods and discs; `setupError`; the
  centre the playback's poses are placed around) and `rodSetLayout`.
- Ids `r<n>` (`IdKind::RodSet`); `Snapshot.rodSets` (`RodSet`: the fields
  as the Rods panel showed them, and `rods`, the stack map's count);
  `ProblemSummary.rodSet` / `ProblemDetail.rodSet`; `ProblemDetail.stacks`
  {start, goal}; `ShapeSummary.disc`; `document.changed.what` "rodSets".
- 115 methods (15 new), all called by the contract suite.

### The front end (this part's commit)
The engine now says stacking is supported; the front end keeps its
stacking notices until part 4 through one rule,
`web/src/renderer/state/kinds.ts` (`frontEndSolves`): the Solver's
`supported`, the Puzzle's notice and New's list of grids use it, so a
stacking puzzle opens, shows and saves as before, the Solver disabled.
The fake engine's sessions were recorded again (the replies have new
fields); the recorder now gives the engine a neutral home
(`/home/user`), so the default folders in `settings.get` name no one's
home (a recording made as usual wrote `/Users/<name>/Library/...` into
the committed files).

### Tests
- `tests/unit/app/test_document_stacking.cpp` (11 cases, 3 of them from 3a): every command
  undone and redone byte for byte with its ids; Hanoi built from a new
  document equals the fixture byte for byte and solves in 7; what drops
  solutions per callback (new rules and a cleared stacking do; a disc's
  size, a moved disc, a board chosen do not); the brick commands and the
  cell editor refused; the rules' clamps and Panex dependencies; the rod
  set alone laid out; the saved search described, refused while running,
  aborted without a session after an import; the original's undo case
  with the GUI's Add instead of a count.
- `tests/unit/engine/test_engine.cpp`: a stacking solve polled while it
  runs, paused at its first progress and carried on (`[parallel]`).
- `tests/contract/test_engine.py`: `stacking_rod_sets_discs_and_stacks`
  (Hanoi through `stacking.*`, saved byte for byte as the fixture; every
  command; undo; refusals), `stacking_solve_pause_saved_search_continue`
  (a six-disc Panex swap, generated as the corpus's generator makes them
  (the five-disc one is checked equal to the fixture), solved on one
  thread so the pause lands, `solve.savePoint` refused, paused, its saved
  search and note, exported with and without levels, the engine ended,
  **a second engine** finds the search and carries it on to 881 transfers,
  playback in transfers, imported twice, an abort no session holds;
  without a Panex folder an import is refused), and the stacking
  refusals and Hanoi solved in `unsupported_and_failures`.

## Gate results

All run on the final tree (2026-10-11, M-series Mac):
- `just test-all`: 6/6 (unit, the slow cases, the api schema and docs,
  the contract suite with 115 methods called, the CLI), 436 s.
- `just test-asan`: 6/6, no sanitizer report, 1,479 s.
- `just test-tsan`: the `[parallel]` cases (81 cases, 17,948 assertions;
  the engine's stacking solve polled, paused and carried on among them)
  and the contract suite on the TSan engine (17 tests, 115 methods),
  clean.
- `just oracle --tier fast`: 19/19 (all fields; all threads and one).
- The stacking tier: 40/40 with the Stacking Solver and 11/11 with the
  Panex Solver, all threads and one (the setup that is not ready is the
  original's failure too). The searches did not change in this part.
- `just oracle-roundtrip`: 511/511 byte-identical to the original's save.
- Contract suite (dev engine): 17 tests, 115 methods, ~26 s; the new
  in-process engine case three times in a row.
- `just check`: not run (LLVM is not installed).
- Front end, three times each: `just web-check` clean, `just web-test`
  304/304 (release engine), `just web-e2e` 65/65 (5.4 min). `web-dist` and `web-smoke`
  not run: no packaging change.

## Differences from the original, and why

1. The rod history is the document's undo (one history for everything,
   decision 0011), not a separate forty-deep rod undo.
2. What the GUI hid on a stacking puzzle is refused rather than silently
   possible through the API (decision 0014's rule for sliding puzzles):
   cell edits, the tool tabs, the result, counts, ranges, part order,
   groups.
3. A problem's stacks are not edited while it is solved, nor rod sets
   while anything is (decision 0011; the GUI greyed the rod fields out
   while any solve ran, and acted on the Problems tab's buttons without
   checking).
4. `solve.abort` of a saved search with no session: the original's Abort
   did the same from the GUI; the engine's rule that an abort needs a
   solve under way (decision 0012) counts a saved search as one.
5. `solve.savePoint` refused on a stacking solve (the GUI never asked one
   for a save point).
6. `placeDisc` refuses a rod off the board (the GUI's rod slider could not
   name one); `removeDisc` and `moveDisc` without a board refuse with the
   "Choose a rod set" message where the GUI's buttons did nothing.
7. The engine's stacking reads (`stacking.board`, `stacking.rodSetLayout`)
   are the GUI's drawing code's numbers (layoutBoard, showRodSet), so the
   front end computes none.

## Found in the original

- The stacking branch of `cb_ColSel` (painting a disc's cells in the
  chosen constraint colour) cannot be reached: the colour list
  (`colorsGroup`) is hidden on stacking puzzles (`syncStackingChrome`).
  Not ported; a disc's colour is the shape's own (the Color button,
  `shape.setColour`).
- `cb_ProbShapeExchange`'s `moveDisk` runs `syncMaps` before it checks the
  place, so a refused move could still add empty rods to the stacks
  (which the file then writes). The port checks first; nothing changes on
  a refusal.
- `mainwindow_layout.cpp` builds a `diskSizeInput` (1 to 64) that nothing
  reads; the disc list's own size field (1 to 100) is the one in use.

Not fixed in `../burrtools_rotations` (never modified).

## Known issues and notes

- Saved searches live in folders named by the puzzle's fingerprint (the
  rules and the stackings), as in the original: two identical problems
  share one, and an edit that changes the rules or the stacks leaves the
  old search on disk, unreachable, until the folder is cleaned (the
  original did the same). The engine's default folder is the user's cache
  folder, shared with `bt`. Since the audit only one run at a time works
  on a shared search (see "Audit").
- `setDiscSize` keeps solutions found with the old size (as the GUI did),
  though the size rule may no longer hold for them.
- The GUI's Solve asked "Continue it, or start over and discard it?" when
  a saved search existed; the engine's `solve.start` throws it away
  (decision 0015) and `solve.status.savedSearch` lets the front end ask
  first (part 4).
- The solution rows count transfers; `solution.page`'s sort by moves
  sorts by the path's frames, which orders the same.
- `just check`: not run (LLVM is not installed).

## Brief for the audit of part 3 (3a and 3b)

Audit part 3 with Fable 5.1 at extra high effort: the diff of `a87241d`
and the "Deferred 3a" commit (`ea6a91d`), then `e6ab98f`, `f736508`,
`1453ded` and the "Deferred 3b" commit; the gate results of both
handoffs, both notes and the plan. Where to look hardest:
- the Panex search against the original's `panex.cpp`: the keys, the
  level merges, `meet`, the store (BTPANEX4/3), `traceStored`, the
  thread pool's slices and the level writer folded into the next level's
  first job, stops at every point and what is saved then, the disk and
  memory budgets, the twin folding of a swap;
- the stack search, `setupError`, `layoutBoard`, `pathSeparation`
  against `stacking.cpp`; blockpack and the paused file's `searchz/`
  sections (streamed, both ways, a hostile file);
- the solve session's stacking run: Paused versus Finished, the problem
  as the original wrote it, `start()` discarding and `resume()` carrying
  on, `abort()` (with and without a session, the folder given), the
  progress, threads (TSan);
- the document's commands: each against its callback (solutions dropped
  or kept, counts in placeOneDisk, `dropInstance` with the counts
  lowered, the rules' clamps), undo and redo byte for byte, the frozen
  state, the refusals of what the GUI hid;
- the engine: `stacking.*` bounds, ids and errcs, `setRules`' merge over
  the shown fields, `solve.status.savedSearch` while another problem's
  run writes the same folder (identical problems), the default solver,
  the Panex folder in every path that needs it.

## Brief for part 4 (after the audit)

Part 4 of docs/DEFERRED-PLAN.md, the stacking front end: inventory the
original GUI first (mainwindow_rods.cpp, rodbars.h, voxelframe.cpp's
`showStackingAssembly`, `showRodSet`, `showStacking`, the `isStacking`
branches, the labels the plan lists) and write the screens into
webui-spec.md; then remove `state/kinds.ts`'s rule (and New's filter in
`tests/e2e/files.e2e.ts`) and build the rod-set editor (`stacking.*`
rod set methods, `Snapshot.rodSets`), the disc list
(`ShapeSummary.disc`, `stacking.setDiscSize`, `shape.create`), the
stacks editor (`stacking.board`, `placeDisc`, `placeAll`, `removeDisc`,
`moveDisc`, `clear`, the validity bar from `board.error`), the 3D view of
rods and discs (`board.layout`, `rodSetLayout`, `board.centre` for the
player), the Solver's stacking options (`SolveOptions.stacking`,
`engine.info.stackingSolvers`, the menu of one solver by the board), the
saved-search question at Solve ("This problem has …", Continue / Start
Over / Cancel, from `solve.status.savedSearch`), Abort for a saved search,
the export's levels question beyond 1 GB (`levelBytes`), and the player
in transfers. Hide what the engine refuses on stacking puzzles (the cell
editor, the tool tabs, the Problems tab's brick controls). Decide whether
the app sets `panexFolder` to its own data directory (the plan's
suggestion) or keeps the cache folder the engine defaults to.

## Audit

**Date:** 2026-10-11 · begun with Fable 5.1 at extra high effort (the
reading and the probes), finished with Opus 5.5 at extra high effort (the
fixes, the tests and the gates) after the model was switched mid-session.
On the diff of part 3, `4cb6e2b..63d53ba`: `a87241d` and `ea6a91d` (3a),
`e6ab98f`, `f736508`, `1453ded`, `a6a17c6` and `63d53ba` (3b); the gate
results of both notes, both notes, the plan and decision 0015. The fixes
are one commit after the part's: `Deferred 3 audit: one run per shared
saved search, saved states trusted no further than their files, no import
while a stacking puzzle solves`.

### How it was done
- Read side by side with the original: the Panex search against
  `panex.cpp` (the keys and rules, `subtract`, `radixSort`, the merges,
  `meet`, the store with BTPANEX4 and BTPANEX3, `findIn`, `thin`, the
  searcher with `findSigma` and the twins, `nextLevel`'s slices and the
  level written by the next expansion's first job, `traceStored`,
  `meetSearch`'s stops and saves, `setup`, `classicTower`, the fingerprint
  and the folder, `solve`); the model and the stack search against
  `stacking.cpp`; `blockpack.cpp`; the export and import loops of
  `mainwindow_file.cpp`; `runStacking`; the GUI's Solve, Continue and
  Abort (`mainwindow_solve.cpp`); the rod set callbacks
  (`mainwindow_rods.cpp`), the disc list (`BlockList.cpp`) and the
  Problems tab's stacking branches (`mainwindow.cpp`); the engine's
  `stacking.*` and `solve.*`.
- Suspicions probed with small programs against
  `build/release/core/libbtcore.a` and `build/asan/core/libbtcore.a` in
  the session's scratchpad (not committed), on a six-disc Panex swap made
  as the contract suite makes it.
- Each test added was run with its fix disabled (a one-line mutant: the
  check returns early or never refuses), then the source restored: each
  fails without its fix.

### Findings, by severity
1. **Medium: identical stacking problems solved at once shared one saved
   search, and one run's end deleted the other's paused search.** A copy
   of a stacking problem keeps its rules and stacks (in the original too),
   so its saved search lives in the same folder (named by the
   fingerprint). The original never ran two solves at once; the document
   does, and nothing kept two runs off one folder. Probe: the swap opened,
   its problem copied, both solved on one thread each, the first paused:
   "a saved search 256 moves deep, 404310 stackings"; once the copy's run
   finished (its end removes the folder): no saved search, while the first
   problem's note still said "The search is saved: press Continue to carry
   on". Continue then starts from scratch. Solve or Abort of one also threw
   the folder away under the other's run, and `solve.status` of the copy
   described files the running search was writing. Fix:
   `Document::sharedSearchActive` (another problem's active run on the
   same folder, worked out with that run's own work folder);
   `solveStart` and `solveResume` refuse (`invalid_state`) meanwhile,
   `savedSearch` tells none, and `solveAbort` refuses for a problem whose
   own run is not the active one. Paused, the search stays shared, as in
   the original (the copy's Continue carries it on). Test: "stacking
   document: identical problems share their saved search, one run at a
   time".
2. **Medium: a saved state that claimed more keys than its file held was
   allocated and filled.** `Store::loadState` resized each layer to the
   count the file gave before reading it (the original did the same). A
   damaged or crafted `state.bin` (one imported from a `.btsolve`) then
   asks for whatever it claims. Probe: a paused search's 20,560-byte
   `state.bin` with its first count set to 2^40: on Continue the release
   build was killed by the system; under ASan, "allocation-size-too-big"
   in `loadState` from `runStacking`. Through the engine: an imported
   file, then `solve.continue`, and the engine dies. Fix: each count is
   checked against the bytes left in the file; a state that claims more is
   no saved search, and the search starts afresh, as with a truncated one.
   `savedSearch` still describes it from its header. Test: "stacking
   session: a saved search that claims more than its file holds starts
   afresh" (a claim of 2^60 keys, which without the fix throws
   `length_error` out of the search).
3. **Low: an import while the open stacking document solved the same
   puzzle lost the imported search.** `solve.importPaused` unpacks the
   search into its folder (emptied first) and then replaces the document;
   the old document's run, stopping with it, saves over the imported
   state. Probe: a search exported 602 moves deep (4.9M stackings),
   imported while the same puzzle solved: right after the import the
   folder held it; once the old document had gone, "a saved search 418
   moves deep, 1.8M stackings", the old run's. Fix: `solve.importPaused`
   is refused (`invalid_state`) while the open document is a stacking
   puzzle being solved (the original refused any import while it solved:
   `threadStopped`); declared in `api/solve.json`, `docs/API.md` and the
   generated types updated. Test: the contract case
   `stacking_import_refused_while_solving`.

### Probed and cleared
- The Panex search is the original's line for line. Its differences are
  the documented ones: `subtract`'s steps, the pool's slices taken from a
  counter, and the level written by the next expansion's first job. Every
  path that needs the file or the budget finishes that write first: the
  next level's end, a save or an autosave, the meeting and the loop's end.
  A stop inside the expansion still writes it, since job 0 runs on the
  caller before it looks at the stop. The level's memory is untouched
  until then.
- Stops: a stop at the top of a level or inside its expansion saves the
  frontier (the kept levels to disk first); a stop while tracing keeps
  the meeting written before it; Memory saved as the original did. A
  level that is missing makes the trace solve that end by halves: with
  two runs on one folder (before the fix) both still found the path.
- The budgets, `thin`, the keep cap, the halving path, `findSigma` and
  the twins; the stack search, `setupError`, `checkStack`, `moveAllowed`,
  `layoutBoard`, `pathSeparation` and `stateFor`: line for line.
- `blockpack` line for line; a hostile packed section is bounded (the
  chunk sizes against the 64 MB chunk and the section's bytes, the key
  gaps' decoding), and section names are bare file names. What a packed
  section may inflate to is not capped: a level file can rightly be
  hundreds of GB, as in the original.
- The streamed export fills in each section's size by seeking back;
  `FdOutBuf` writes what it holds before every seek.
- The session: Paused against Finished, the problem as the solve thread
  left it, `abort()` with and without a session (the saved search thrown
  away after the run is joined), the progress and its words.
- Undo and redo of the rod set commands (whole-puzzle copies) end every
  session before the puzzle is swapped back.
- The document's commands against their callbacks: `placeOneDisk`'s
  counts, `dropInstance` before the counts are lowered (a solution of the
  problem places every disc, so it goes, as in the original), the rules'
  clamps and the Panex dependencies (the fields shown as
  `syncPanexFields` left them, what an edit writes off), the refused
  move that changes nothing.
- The engine: `stacking.*`'s bounds, ids and errors, `setRules`' merge
  over the shown fields, the default solver, and the Panex folder in
  every path that needs it (the solve options, status, abort, export and
  import).

### Gate after the fixes
All on the final tree (2026-10-11, M-series Mac), one after another:
- `just test-all`: 6/6 (unit, the slow cases, the api schema and docs,
  the contract suite with 115 methods called and the new import case,
  the CLI), 473 s.
- `just test-asan`: 6/6, no sanitizer report, 1,601 s.
- `just test-tsan`: the `[parallel]` cases (82, 18,393 assertions, the
  shared-search case among them) and the contract suite on the TSan
  engine (18 tests, 115 methods), no report, 826 s.
- `just oracle --tier fast`: 19/19, all threads and one.
- The stacking tier: 40/40 with the Stacking Solver and 11/11 with the
  Panex Solver, all threads and one (the setup that is not ready is the
  original's failure too); solved files 40/40; text 284/284.
- `oracle.py panex-resume`: 624/624 (the saved states read with the new
  check, carried on in both programs); `oracle.py blockpack-check`:
  648/648, 162/162 packed byte for byte.
- `just oracle-roundtrip`: 511/511 byte-identical.
- `just web-gen` (one summary changed); `just web-check` clean, `just
  web-test` 304/304 (release engine). The front end's code did not
  change: `web-e2e`, `web-dist` and `web-smoke` not run.
- `just check`: not run (LLVM is not installed).

### Notes for part 4
- Solve, Continue and Abort on a problem whose copy is solving answer
  `invalid_state` ("an identical problem is being solved, and the two
  share one saved search: pause it first"): show it, or grey the buttons
  while the copy runs.
- `solve.importPaused` answers `invalid_state` while a stacking puzzle is
  being solved: ask the user to pause it first.
- The saved search of a problem is its copies' too: the question at Solve
  ("This problem has …") applies to a copy's search as well.

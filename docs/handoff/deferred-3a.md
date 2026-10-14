# Handoff: deferred part 3a (stacking and Panex: the core and `bt`)

Part 3 of docs/DEFERRED-PLAN.md, its first half: the original's stacking
puzzles ported into the core (rod sets and stacks modelled, the Stacking
Solver's search, the Panex search with its saved searches, blockpack),
solved through the solve session and played in transfers, carried in the
paused-solve file, solved by `bt` as burrTxt solved them, and compared
with the original by a new `stacking` tier of the harness, a Panex resume
sweep across the two programs and a blockpack cross-check. Decision 0015
records the shape of it. Next: part 3b, the document's stacking commands
and the engine (Opus 5.5 at extra high effort), then the audit of the
whole part.

## Why the part was split here

The plan suggested "3a: stacking and the stack search, 3b: Panex,
blockpack and the saved searches". That split does not hold: the
original's Stacking Solver itself runs on the Panex search whenever the
puzzle fits it (`runStacking` calls `panex::solve` with `anyRules`), so a
faithful stacking solve needs the Panex search, and the harness's answers
(the paths, the counts, the files) come from it. The split is by layer
instead: 3a everything below the engine, 3b the document's editing
commands and the engine. The engine still refuses stacking solves
(`problem.kind` says `supported: false`, `solve.*` answers
`unsupported`), so nothing is half advertised.

## Done

Two commits: "Core: stacking puzzles: rod sets and stacks in the model,
the stacking and Panex searches, stacking solves, playback in
transfers", then the part's "Deferred 3a: …" (`bt`, the harness and its
recordings, the documents).

### The core
- `model/stacking_types.h` (`RodSet`, `DiskRef`, `StackMap`,
  `kNoRodSet`) and `model/stacking.{h,cpp}` (from `stacking.{h,cpp}` but
  its search): the board's rules (`totalRods`, `isPocket`,
  `rodPosition`, the outside channel, `rodName`, `rodHeight`,
  `rodCapacity`), the discs (`generateDisk`, `ensureHotspot`, `addDisk`,
  `placeSolvingHotspot`, `discsOf`), the stacks (`syncMaps`, `placeDisk`,
  `liftTop`, `dropInstance`, `moveDisk`), `setupError` word for word,
  `layoutBoard`, `moveAllowed`, `searchInput`, `pathSeparation` (three
  frames a transfer), `logicalMoves`, `startAssembly`, `boardSpan`.
- `Puzzle` holds the rod sets (the default one made with a stacking
  puzzle; `addRodSet`, `removeRodSet`, `exchangeRodSets` renumbering the
  problems'); `Problem` the rod set id and the start and goal stacks,
  copied with `copyDefinition`, kept in step on `removeShape`,
  `exchangeShapes` and every `setShapeMaximum` (`trimStacks`), as the
  original's problem.cpp hooked them.
- `io/puzzle_file.cpp`: `<rodSets>` and `<stacking>` read and written as
  the original did (`countAttribute`'s ranges, `decodeRod`'s stream
  reading, the default rod set added on reading a stacking file without
  one, the writer's order: rod sets after the shapes, `<stacking>` after
  `<goal>`); no longer preserved XML.
- `solve/stack_search.{h,cpp}`: `findStackPath` and `stackBfs` (packed
  64-bit keys or text keys, the memory budget), `StackingSolver`,
  `StackingOptions`.
- `solve/panex_search.{h,cpp}`: the Panex search line for line
  (`rules_c`, the 128-bit keys, `subtract`, `radixSort`, the merges,
  `meet`, `store_c` with BTPANEX4 and BTPANEX3, `searcher_c` with
  `findSigma`, `nextLevel`, `traceStored`, `meetSearch`, `classicTower`,
  `fingerprintOf`, `folderFor`, `unsupported`, `savedSearch`, `savedMs`,
  `searchFolder`, `discardSaved`, `solve`); its steps on a `ThreadPool`.
- `io/blockpack.{h,cpp}`: the packer (signature, chunk layout, key gaps,
  `pack`, `unpack`) on a pool.
- `SolveSession`: the stacking run (`launchStacking`, `runStacking`,
  `endStackingRun`), `SolveOptions.stacking`, `SolveProgress.searchTraced`
  and `panexSearch`; `abort()` discards the saved search; a save point does
  nothing on a stacking run.
- `io/paused_solve`: the `searchz/<name>` sections (written through the new
  `io::writeFileStreamed`, read section by section, unpacked into the
  folder an import is told; `SavedSearchFiles`, `SavedSearchFolder`,
  `savedLevelBytes`).
- The document: discs' hotspots in `prepareShapesForSolving`;
  `exportPaused(problem, path, PausedExport)` and
  `importPaused(path, settings, stackingWorkDir)`; the analysis's and the
  explorers' "not yet" refusals of stacking dropped (their capability
  checks refuse in the grid's words).
- `app/playback`: stacking solutions play in transfers (`stepFrames_`,
  three frames a step), from places, centred on the board
  (`stackingCentre`).

### `bt` (this part's commit)
burrTxt's stacking branch: `--solver` Stacking Solver or Panex Solver,
`--high-memory`, `--restart` (also without `--save` on a stacking
puzzle), `--panex-dir` and `--panex-disk` (else `SHAPELOGIC_PANEX_DIR`,
`SHAPELOGIC_PANEX_DISK_GB`, else the cache folder), "continuing a saved
search …", Ctrl-C saving the search (a second quits), the summary and the
"panex search:" / "stack search:" lines and their notes word for word,
`-p` the transfer plan, `--json`, `-x`; `--save` through a session (a
saved search carried on unless `--restart`); the A/B switches
`--no-tower-rule` and `--panex-log`; the stacking help section. `-a s1`
draws no stacking assembly (the original died there).

### The harness (this part's commit)
- `stacking_puzzles_old.cpp` (`oracle.py stacking-puzzles`): 39 puzzles
  made with the original's code from its `test_stacking.cpp` and a few
  more, in `tests/data/puzzles/stacking/`.
- The `stacking` tier (17 corpus lines, 41 puzzles; flags `Q`, `L`, `I`)
  and its baselines for both solvers (`baseline/stacking*.json`,
  `baseline/text-stacking*.json`); a Panex folder of its own per run.
- `stack_solve_old.cpp` (the original's solve thread as the GUI's Solve
  ran it) for `baseline-files`; `compare-files` compares state by state.
- `panex_resume_old.cpp` and `tests/tools/panex_resume.cpp`
  (`bt_panex_resume`): `oracle.py panex-resume`.
- `blockpack_old.cpp` and `tests/tools/blockpack.cpp` (`bt_blockpack`):
  `oracle.py blockpack-check`.

### Tests
- `tests/unit/solve/test_stacking.cpp`: `test/test_stacking.cpp` ported in
  full, every case in its order (28 cases; its undo case is the
  document's; the scratch folders this process's own; four `[slow]`).
- `tests/unit/solve/test_stacking_session.cpp` (8 cases): one thread and
  four give the same path; pauses at a sweep of depths carried on (the
  same path wherever paused); a stopped search continued by a new session;
  abort discards; Solve starts afresh; the refusals and notes; the
  Stacking Solver on nine rods; Just Count; the progress.
- `tests/unit/app/test_document_stacking.cpp` (3 cases): solved and played
  through the document; a paused Panex solve exported (with and without
  its levels) and imported into another folder, carried on; the
  original's undo case.
- `test_playback_explorers.cpp`: a stacking solution plays in transfers;
  `test_puzzle_file.cpp`: rod sets and stacks read and written;
  `test_solve_session.cpp`: prepare refused; `tests/cli/test_cli.py`: the
  stacking modes.

## Gate results

All run on the final tree (2026-10-10, M-series Mac):
- `just test-all`: 6/6 (unit, the slow cases with the seven new `[slow]`
  stacking ones, the api schema and docs, the contract suite with 100
  methods called, the CLI), 424 s.
- `just test-asan`: 6/6, no sanitizer report, 1,463 s. (Its first run
  stopped on undefined behaviour in the Panex search's `subtract`,
  inherited from the original: see "Found in the original"; fixed, and
  the whole chain run again. After the last change, the writer's core in
  the search's pool, the ASan build ran the 41 stacking cases and the CLI
  tests again: clean.)
- `just test-tsan`: the `[parallel]` cases (80 cases, 17,188 assertions,
  the stacking session's and the document's export among them) and the
  contract suite on the TSan engine, clean, 784 s.
- The stacking tier, both solvers: summaries 40/40 (Stacking Solver, all
  threads and one; the setup that is not ready is the original's failure
  too) and 11/11 (Panex Solver, all threads and one); solved files 40/40
  and 11/11, state by state; text 284/284 and 76/76, byte for byte.
- `oracle.py panex-resume`: 624/624 (five searches, three ways, straight
  and stopped at a sweep of depths in both programs: the saved folders
  byte for byte alike, the search carried on in both to the same path).
- `oracle.py blockpack-check`: 648/648; 162/162 packed byte for byte as
  the original packs.
- `just oracle --tier fast`: 19/19 (all fields; all threads and one); the
  sliding tier on one thread 35/35.
- `just oracle-roundtrip`: 511/511 byte-identical to the original's save
  (the stacking fixtures and solved files included).
- `just oracle-resume`: 395/395.
- `tests/cli/test_cli.py`: all passed (in `test-all`).
- `just check`: not run (LLVM is not installed).
- Front end, three times each (web/ unchanged, the engine under it not):
  `just web-check` clean, `just web-test` 304/304 (release engine), `just
  web-e2e` 65/65 (5.4 min). `web-dist` and `web-smoke` not run: no
  packaging change.

## Differences from the original, and why

Decision 0015's seven, in short: a stopped Panex search ends the session
Paused (the problem written as the original wrote it); the search's steps
and its level writer run on a pool; `prepare()` refused and save points
doing nothing on stacking runs; an empty work folder saves nothing; `bt
--save -d` solves stacking puzzles; `bt -a s1` draws no stacking assembly;
an export's levels in name order.

## Found in the original

- **`burrTxt -a s1` dies on a solved stacking file**: `print()` draws the
  assembly into the result shape, which a stacking problem does not have
  (an assertion; the harness records what it printed before).
- **burrTxt2 has no stacking solver** (no assembler capability on the
  grid): solved files come from the solve thread.
- **The Panex search's `subtract` formed pointers it may not**: from an
  empty level's null data (`lo + step` on a null pointer, which UBSan
  stopped in `just test-asan`) and, stepping in doubling strides, past
  the end of the level. The port steps the same way over indices and
  returns at once for an empty level; the answers are the same.

Not fixed in `../burrtools_rotations` (never modified).

## Known issues and notes

- The engine refuses stacking solves until part 3b; its `playback.open`
  already plays a stacking solution (the core no longer refuses it), and
  `explore.openAssembly` refuses with the grid's message.
- `bt --save` on a stacking problem that is solved and has no saved
  search says "is already solved" (burrTxt2's rule); the GUI's Continue
  ran the search again.
- The Panex search's `peakMemory` of a resumed run counts that run only
  (as the original's).
- Speed (the hidden `[.bench][panex]` case, the seven-disc swap,
  `BT_PANEX_BENCH_N=7`, back to back with the original's
  `test_burrtools`, best of three, the machine otherwise idle): all
  threads 11.23 s against 10.99 s (+2%), one level thread 27.7 s against
  34.4 s. The first measurement was 8% slower on one thread: the
  original's writer thread wrote a saved level beside the search, which a
  pool of one core could not; the search's pool now has a core for it
  (decision 0015), and its job then helps with the level, hence faster.

## Brief for part 3b

Part 3b of docs/DEFERRED-PLAN.md (Opus 5.5, extra high effort): the
document's stacking commands and the engine.
- **The document**, from the GUI's callbacks (`mainwindow_rods.cpp`,
  `mainwindow.cpp`'s stacking branches): rod sets (new, delete keeping at
  least one, copy, rename, move, the fields of cb_RodField with its
  clamps and the Panex dependencies, which sync the maps and drop the
  solutions of the problems on that rod set), discs (a new disc,
  cb_NewShape's `addDisk`; a disc's size, `generateDisk`; a disc's
  colour, cb_ColSel), the problem's rod set (Set Start/Goal Rods:
  `setRodSetId` + `syncMaps`), placing a disc (placeOneDisk: a copy added
  when every copy is placed, unenforced), adding all discs (largest
  first), removing a disc (`dropInstance` and the count one lower),
  clearing the edited map (cb_RemoveAllShapesFromProblem), moving a disc
  on its rod (`moveDisk`); all undoable (the GUI's rod history becomes the
  document's undo), refused while solving, with tests that undo and redo
  byte for byte. Which edits drop solutions: follow each callback.
- **The engine** (each in its own `Engine:` commit): `problem.kind`
  stacking `supported: true`, `engine.info.stackingSolvers`,
  `SolveOptions.stacking` (solver, highMemory, autosave),
  `SolveProgress.searchTraced`/`panexSearch`, settings `panexFolder`
  (default the cache folder's `panex`) and `panexDiskGB`, `solve.status`
  with the saved search's description and `savedMs`, a way to discard a
  saved search without a session (the GUI's Abort and Start Over),
  `solve.savePoint` on a stacking solve (refuse, or answer at once: decide
  and record), a `stacking.*` namespace for the commands above and the
  reads the editor needs (`layoutBoard`, `setupError`, the board's
  geometry and `stackingCentre` for the player), `solve.exportPaused`
  with the levels choice and their size, `solve.importPaused` with the
  folder; the contract suite runs a Panex solve with a pause, a saved
  search and a continue; `python3 api/gen_api_md.py`, `just web-gen`.
- The front end keeps its stacking notices until part 4, but must not
  break when the engine says `supported: true` (decide in 3b whether the
  Solver opens for stacking puzzles with the engine's defaults, as part 1
  did for sliding).
- Then the audit of part 3 (3a and 3b together), Fable 5.1 at extra high
  effort.

# Handoff: web stage 4 (the Solver workspace and the player)

**Date:** 2026-10-09 · **Next:** the audit of stage 4 (Fable 5.1, max
effort; FRONTEND-PLAN.md, "Recommended audits"), then stage 5, the
explorers, the debug panel and the status window.

## Done

### Three C++ commits first
- **`a70e3c1` Engine: hotspot.** `MeshFrame` has `hotspot` (a `Cell`): the
  hotspot of the shape in the mesh's orientation, in grid coordinates (the
  core's `DrawMesh` carries the hotspot of the space it was made from,
  `shapeDrawMesh`). An assembly puts a piece's hotspot on its placement's
  cell, so `shape.mesh {transformation}` is drawn at the cell minus it, in
  mesh units; `playback.mesh` has it too. Tests: an app case places every
  piece of the first solutions of every fixture on all five grids at its
  cell minus its turned mesh's hotspot and finds it where the playback
  sampler's single pose puts it (119 of its 1,223 assertions fail with a
  hotspot axis zeroed); the contract session checks it on
  DemoPieceGenerator; `docs/API.md` and the web types regenerated. Its
  finishing steps: `just test-all` 6/6, the contract suite, `python3
  api/gen_api_md.py`, `just web-gen`, the staleness test.
- **`3707bcb` Core: a disassembly without orientations plays as the
  assembly placed it** (stage 7's handoff, "Found in the original").
  `DisasmToMoves::getTrans` answers "not given" for a state without
  orientations (a file without `<dt>`: every classic `<solutions>`
  element), so the playback sampler keeps each piece in its assembly
  orientation; the original answered 0 and turned such pieces to
  orientation 0 (PelikanBurr's piece 2, transformation 13, drawn turned by
  120 degrees). Recorded as decision 0013's item 7. `test_app_dump.cpp`
  leaves the poses of those turned pieces out of its comparison with the
  original's sampler (everything else of each track still compared) and
  checks every such track against the same tree with the assembly's
  orientations given (`withOrientations` in `app_test_helpers.h`); a quick
  case on PelikanBurr. Both fail under the old answer (21,452 and 24,248
  assertions). Its finishing steps: `just test-all` 6/6, `just test-asan`
  6/6, `just oracle --tier fast` 19/19, `just oracle-files --tier fast`
  19/19.
- **`9259304` Engine: a run's and an analysis's end told before their
  document.changed** (found by this stage's gate: the dominoes e2e failed
  once in three runs, its error run's record waiting for an end that had
  already been announced). The engine's poll told the states first and
  took in what had ended after (`Document::poll`): a run ending between
  the two had its `document.changed` before its last `solve.state`; an
  analysis ending between the two had its results announced first and its
  `analyseEnded` came at the next poll with zeros. Now the poll takes in
  first and tells after (what was taken in has ended, so its last state
  and counts come first), and the document keeps how each problem's last
  analysis ended (`analyseProgress`, `solution.analyseStatus`) until the
  next one starts. `pollEvents` has a seam between the taking in and the
  telling; two engine cases end a run and an analysis there and check the
  order and the counts (both fail with the old order). Its finishing
  steps: `just test-all` 6/6, `just test-asan`, `just test-tsan` (see the
  gate), the contract suite; the fake engine's sessions re-recorded.

### The solve records (`web/src/renderer/state/solves.ts`)
`Solves`: one record per problem (several may run), DOM-free, shared by
the workspaces. The state as the events tell it (`solve.state`, and the
snapshot's until one did: `told`), a run's phase (none, active, ending),
the progress, the failure (`solve.error`), the last `solutionsAdded`, an
analysis, the solver this client started it with. Decision 0012's order:
a run is over at the `document.changed` that names its problem after its
last `solve.state` (`whenEnded`, `endRevision`); a run this client started
(`launch`, at the call's reply, before its events) is waited for even if
it starts and ends between two polls; a state that moved between two ends
is such a run too; a solve paused in memory and aborted ends no run (one
`document.changed`, before the idle); a state told again (a replaced
document's last idle) is no run; an abort's own `document.changed` comes
while the run is still active here and ends nothing. `clear()` when
another document comes (at the replacing call's reply, so its last idle
and `analyseEnded {stopped}` meet no run and no analysis). `pausedBt2()`:
a BurrTools 2 search paused in memory.

### The Solver workspace (`web/src/renderer/solver/`)
Each file names the mockup's functions it came from.
- `workspace.ts` (`SolverWorkspace`): the problem shown, read after every
  change (`problem.get`; `problem.kind` once); `#leftS[data-problem]`,
  `[data-revision]`, `[data-state]`, `[data-phase]`, `[data-end]`,
  `[data-assemblies]`, `[data-solutions]`, `[data-autosaved]`. Its solve is
  the record's. The options (stored with the problem by
  `problem.setSolverSettings`, refused while the problem is solved or
  analysed); Solve, Prepare, Continue (the record told of the run at the
  reply; Solve and Prepare drop the selection and a pending playback
  first: the run drops the solutions), Pause, Abort (asked, as the
  original; enabled only while a run is under way or paused in memory,
  so it greys out once a run has ended). The list's selection: kept by id
  across re-reads, shown (read, played) only while the Solver is shown and
  only from rows of the list's newest version, not chosen automatically
  while a run changes the list (chosen once it is over). Remove
  (`solution.delete`: one, before, after, without a disassembly, all; each
  asks; undoable), delete the disassembly of one or all
  (`solution.deleteDisassembly`), Analyse (`solution.analyse`: this one,
  all, the missing ones, with the problem's solver type and Check
  Rotations; its progress and Stop; the solutions it takes read first, so
  the ones it took and could not take apart are marked "Cannot be
  disassembled" once its results are in). The keys, the status line (the
  mockup's svStatus), the tag (svTag), the view's empty message.
- `run-card.ts` (the left card): the problem picker (each problem with its
  solve: "solving", "paused"), what to check (Assemblies / Assemblies and
  disassembly, with Don't keep the disassemblies and Check rotations),
  the search options (Just count, Keep mirror solutions, Keep rotated
  solutions, Thorough rotation check, Strict colours, Solver type, Sort
  found solutions by, Keep every n-th, Limit, Autosave every 20 min; "n
  changed" when folded; the original's rules grey out what does not
  apply), the run's buttons, the progress bar, the counters (Activity,
  Assemblies, Time used, Solutions, Time left), a failure (`#slvFail`), the
  notice of a puzzle this version does not solve (`#slvKind`).
- `solutions-card.ts` (the right card): the list (virtual: the rows in
  sight drawn and asked of the pager; # (the assembly number), Level,
  Moves (moves, and turns as "+nR"), Pcs, DA; a header's click sorts, again
  the other way, a third time the saved order) or the slider (Solution i
  of n, the meta grid); the player's controls; an analysis's progress and
  Stop (`#slvAna`); Sort by (slider view); Manage solutions (the mockup's
  menu; while an item is pointed at, the rows it would remove are struck
  through) and Export to STL (disabled: the exports); the piece chips.
- `player.ts` (`Player`): the solution chosen is shown assembled at once
  (`solution.get`'s placements; each piece the mesh of its placement's
  transformation from the view's mesh cache at the cell minus that mesh's
  hotspot, in mesh units: `extent.ts`'s `cellScale`, centred on the result
  as the poses are); 120 ms after the choice rests its playback is opened
  (`playback.open`, if the row is still current in the list then: a
  removal or an abort holds an open not yet asked for, and the list's next
  reading opens it again if the solution is still there; every piece's
  `playback.mesh` in the view's style, or
  the wire-frame style for a wire-frame chip; the poses of the 32-step
  chunk around the time shown, the next one ahead). Play and pause (1.2
  moves a second at 1×, the speeds 0.5× to 8×), one move back and forward,
  the jumps, the seek bar; a double-click on a row plays once the playback
  is in. A solution without its moves shows why and offers Analyse; a
  puzzle this version does not solve is shown assembled, not played.
  `#splayer[data-solution]`, `[data-ready]`, `[data-steps]`, `[data-t]`,
  `[data-playing]`, `[data-handle]`.
- `poses.ts` (`PoseTrack`): the samples (8 a step) in chunks of 32 steps,
  a chunk's last step the next one's first; a pose at any time from the
  two samples around it: translations straight, rotations by slerp (the
  short way); at a whole step exactly the engine's sample; a piece shown
  now and gone at the next whole step fades over that step with the
  setting (the original's alpha with fadeOut).
- `pager.ts` (`SolutionPager`): `solution.page` in pages of 100 by sort,
  each read at its `solutionsVersion`; a page of a newer version makes the
  others old (their rows stay shown until their own new page comes);
  `added(version)` reads again only at another version than the pages';
  `refresh()` after a `document.changed` naming the problem's solutions.
- `settings.ts`: the options over the stored `SolverSettings` (the file's
  names: `deepSymmetry`, `strictColors`, `justLevels`) and to
  `SolveOptions` (the API's: `completeRotations`, `strictColours`), the
  original's rules (`updateSolverOptionCheckboxes`, `cb_SolverOptions`),
  the original's defaults (Autosave on, Sort by moves; Display Limit 100,
  Keep Each 1, not stored); settings this version does not show (High
  Memory, the sliding ones, other versions') kept as they were.
- `autosave.ts` (`Autosave`): while a solve with the setting runs, every
  20 minutes: `solve.savePoint` on every run under way (the save needs
  them all paused), their ends waited for by revision, `document.save`,
  `solve.continue` with the options each ran with. A puzzle without a file
  is not saved (said once a solve). `saveAtSavePoint()` is the path a save
  refused while solving is to offer (stage 6's Save).

### The 3D scene (`view3d/solution.ts`, `view.ts`, `extent.ts`)
`SolutionScene`: each piece its own mesh and colour (the original's
`pieceColor` of the shape, jittered per copy: `pieceSubColour`), a group
per piece posed by a translation and a quaternion, in the turned root (the
poses are centred on the result); solid, wire frame or hidden; with x-ray
the wire frames are drawn over everything (the mockup's second pass); a
fading piece blended by its opacity. The view frames it on the result's
extent (`View3D.setSolution`, `poseSolution`, a `SolverHost`); `#vp` says
`data-solution`, `data-solution-pieces`, `data-poses` (the poses shown, 4
decimals), `data-solution-view`. `extent.ts` has `cellScale`.

### Shell and the other workspaces
- `keys.ts`: one press, one action (38 chords): the Solver's rows have
  their chords (Space `activate`, ← → `move.back`/`move.forward`, Shift+←
  → `move.start`/`move.end`, End `end`); the keys the Entities rows and the
  view already held got names for their meaning wherever they act (↑ ↓
  `prev`/`next`, Home `home`, PgUp PgDn `page.up`/`page.down`, Enter
  `activate`; F2 stays `shape.rename`). `keyboard.ts`: `onKey(action,
  handler, first)` (the Solver's list before the view's Home).
- `confirm.ts`: the focus goes back where it was when the question closes
  (Del on the list after a Cancel found no list to act on).
- `state/edits.ts`: `refuseList()`: a change of the lists of shapes,
  colours and problems, undo and redo are refused while anything solves
  ("Pause the solve first") and while a BurrTools 2 search is paused in
  memory ("Abort or finish the paused BurrTools 2 solve of P1 first": the
  engine would refuse it as `invalid_state`, decision 0011); used by the
  problems list, the shapes list's delete and move, the colours' add and
  delete, and undo/redo (`history.ts`).
- `shell/app.ts`: the solve records, cleared on another document; the
  Solver workspace; its status line.
- `main/index.ts`: `BT_AUTOSAVE_SECONDS` (the e2e tests) passed in the
  page's address.
- `index.html`: the Solver cards are the mockup's markup (lines 780–795
  and 844–854 of `docs/mockup/index.html`) with `#slvKind` and `#slvAna`;
  `styles.css`: the stage's rules at the end.

### Tests
- Unit (`tests/unit/renderer/solver.test.ts`, 17): the options read and
  written in both spellings, other settings kept, `SolveOptions`, the
  original's rules, `cellScale`; the poses at whole steps, between samples
  (straight, slerp, a quarter turn 22.5 degrees), the fade, the chunks and
  their boundary, the single pose; the pager (pages once, at their
  version; `solutionsAdded` at its version ignored, at another read
  again; a newer page makes the others old, their rows kept meanwhile);
  the records on made-up events. `keys.test.ts`: the Solver's chords.
- Replays (`tests/unit/solver-replay.test.ts`, 10): the sessions
  `solver-end` and `solver` (tools/sessions.ts, recorded with `npm run
  record`) drive the records and the pager over the real engine's output:
  a run's end at its `document.changed` after its last state; a run that
  starts and ends between two polls (the recording with the run's active
  states taken out: the engine polls right after each request, so a real
  one is a race) ends at the same revision; an abort's own change ends
  nothing, the second does; a paused solve aborted ends no run; after a
  new document the last idle and `analyseEnded {stopped}` leave no run and
  no analysis; `solutionsAdded` at the version the pager has asks nothing
  (made certain: paused, a solution removed (the version moves, told by
  `document.changed` only), carried on after the progress's quarter
  second: its first `solutionsAdded` tells the version paged).
- Engine (`tests/engine/solver.test.ts`, 3): the assembled placement
  (`cellScale`, `gridExtent`, the hotspot) equals the player's first pose
  for every piece of the first solutions of every fixture on all five
  grids; the pager in every sort and direction gives one
  `solution.page`'s rows (SolidSixPieceBurrs, 314 rows, 4 pages); the
  chunked poses are the engine's samples at every whole step.
- e2e (`tests/e2e/solver.e2e.ts`, 5; helpers `tests/e2e/solver.ts`):
  Bermuda solved with take-apart on every thread, the counters equal
  `bt -q -n -d --json`'s (1 assembly, 1 solution), its solution played to
  the end, stepped back and forward with ← and →, jumped with Shift, Home
  and End on the seek bar, Space and Esc, the buttons, the poses shown at
  every whole step equal to `playback.poses`'s samples read through the
  bridge, the chips; SolidSixPieceBurrs's list sorted every way (each
  header three times) against `solution.page`, End/PgUp/↑/Home, the
  slider, Del with Cancel and Delete, Ctrl+Z, This only, Before, After,
  All (each asked); PelikanBurr solved without take-apart (12 assemblies),
  analysed: one comes apart, eleven marked "Cannot be disassembled", All
  without a disassembly removes them, the disassembly of the last deleted
  (level kept) and analysed again, Enter plays; a 2x4x5 box of dominoes
  kept whole (83,442 assemblies) analysed and stopped: what was done kept,
  in the stored order; the same box with take-apart on one thread:
  pause, continue, the autosave (2 seconds here) pausing at a save point,
  saving the file and carrying on, Abort asked (Cancel, then Abort), greyed
  after; Prepare then Continue; the contract suite's error run shown with
  its failure; two boxes solved at once (one with BurrTools 2), each with
  its own state, both paused; the paused BurrTools 2 search refusing
  + New, Ctrl+Z and a shape's Del before any call; after the abort the
  lists change again; a sliding puzzle with the Solver disabled. Each
  test's call log holds no failed call but the client's own superseded
  reads.
- Recordings: `basic` and `dominoes` re-recorded (the mesh replies have
  `hotspot`); `solver-end` and `solver` new.
- Mutation checks: the run's end on the abort's own change, `launch`
  ignored, a paused abort taken for a run, a state told again taken for a
  run, `solutionsAdded` always paging, no slerp, no fade: each fails a
  unit or replay test; dropping the BurrTools 2 refusal fails the e2e.

## Gate results
| Check | Result |
|---|---|
| `just web-check` | clean, three times (three TypeScript projects, ESLint without warnings, Prettier) |
| `just web-test` (release engine) | **211/211**, three times, ~18 s each: 23 files (stage 3's 180; `unit/renderer/solver.test.ts` 17, `unit/solver-replay.test.ts` 10, `engine/solver.test.ts` 3, `keys.test.ts` +1) |
| `just web-e2e` | **38/38**, three times, 4.3 min each (build included): stage 3's 33 and `solver` 5 (Bermuda ~11 s, the list ~2 s, Analyse ~4 s, the dominoes ~4 s, two problems ~2 s) |
| perf (each e2e run) | mesh build 6.3, 5.4, 5.9 ms; orbit median 13.4/13.4/13.3 ms at a 13.4/13.3/13.4 ms display frame, p95 14.6/14.9/14.8 ms; 151–153 renders in 151–152 frames |
| C++ (after each C++ commit) | Engine: hotspot: `just test-all` 6/6 (unit 50 s, unit-slow 285 s), contract, `api-docs`, `just web-gen` and the staleness test. Core: `just test-all` 6/6, `just test-asan` 6/6 (unit-slow 975 s), `just oracle --tier fast` 19/19, `just oracle-files --tier fast` 19/19. Engine order fix: `just test-all` 6/6, `just test-asan` 6/6 (unit-slow 941 s), `just test-tsan` (71 parallel cases, then the contract suite: 0 failed) |
| before the gate | the Solver e2e file run some dozen times while it was written, four times more after each fix (20/20 at the end); a first gate round failed two Solver tests (the engine's poll order, and a test left behind a message change), so the gate was run again whole after the fixes |

## Found while testing
- **The engine's poll order** (fixed in `9259304`, above): a run or an
  analysis ending between the poll's telling and its taking in broke
  decision 0012's order; the Solver's error run waited for an end already
  announced (one e2e run in three).
- **Reads of solutions the UI itself was removing:** a playback opened
  120 ms after a choice reached the engine after a removal of that very
  solution (`not_found`, one e2e repeat in twenty); Solve and Prepare drop
  the selection first, a removal and an abort hold an open not yet asked
  for, and the open waits for the row to be current. A hidden Solver read
  rows of an old version after a bridge abort (`solution.get` →
  `not_found` in the Puzzle's e2e): the Solver shows solutions only while
  it is shown and only from current rows.
- A confirmation that kept the focus on its removed Cancel button left
  the list's keys dead after a Cancel: the focus goes back.

## The reference screenshots
Unchanged: the 36 of stages 1b and 2 pass as they are (the Solver draws
only in its own workspace). The stage adds none: the Solver is checked
against the engine (counts, rows, poses), not pictures; this session's
screenshots of the Solver (PelikanBurr's stored solution playing,
Bermuda's take-apart at its end and with chips changed, the options, a
run under way and paused, the Manage menu's preview, the slider, the
sliding notice) were looked at.

## Where this stage departs from the plan, and why
1. **The keys' actions were renamed** so that one press has one action
   wherever it acts (the brief's rule): ↑ ↓ are `prev`/`next` (a shape, a
   solution), Home `home`, PgUp PgDn `page.up`/`page.down`, Enter
   `activate` (rename in the shapes list, play in the Solver, cycle a
   chip); each workspace's handler says what it does there.
2. **A solution is shown assembled first** (solution.get's placements over
   shape.mesh {transformation} at the cell minus the hotspot: the plan's
   assembled view outside the player), its playback opened once the
   choice rests 120 ms (running through the list with the arrow keys
   opens no playback per row).
3. **Autosave saves the puzzle's own file** (the plan's path:
   `document.save`); the original's autosave saved a recovery copy (its
   tooltip). A puzzle opened by its bytes (the temporary Open) has no file
   and is not autosaved until stage 6's Save As gives it one; there is no
   Save in this stage, so "refused saves while solving offer this path" is
   `Autosave.saveAtSavePoint()`, for stage 6's Save to call.
4. **"Cannot be disassembled"** is the client's memory of this session's
   analyses (decided per the plan's table): the core keeps no mark of a
   failed take-apart (a solve with take-apart keeps only what comes
   apart). The analysis's targets are read before it starts (the stored
   order; the list is frozen while it runs), the ones done that did not
   come apart are marked once its results are in.
5. **Keep at most and Keep every n-th are not stored** with the problem
   (the original's Display Limit and Keep Each were not either); the
   defaults are the original's 100 and 1. Threads are the engine's
   setting (the settings dialog), not passed.
6. **The sort header's third click** returns to the saved order (the
   engine's `list`, the order the solve kept them in); the mockup had only
   the two directions of its own client-side sorts.
7. **The Remove questions say "Undo (Ctrl+Z) brings them back"**: the
   engine's `solution.delete` is one undo step (the mockup said deleted
   solutions cannot be restored). The deletes of disassemblies do not ask
   (the mockup's).
8. **Abort asks first** and is the hard stop (the original asked, "This
   cannot be undone"); it greys out once a run has ended (decision 0012);
   the original's later "Reset" of a finished problem is Remove → All
   solutions.
9. **Not in this stage:** the step-by-step explorer section of the left
   card and the mockup's drawer (`renderSolverDrawer`, `#sdrawer`: stage
   5's explorers), Export to STL (shown disabled; stage 7), Enable High
   Memory (waits for the sliding solvers).
10. **The list does not choose a solution while a run changes it** (a
    solution thinned by Keep at most would be gone before it is read); it
    chooses the first once the run is over, and a solution picked by hand
    is shown.
11. **Test hooks** as before: `#leftS[data-…]`, `#splayer[data-…]`,
    `#vp[data-solution…]`, `#slvSel[data-total]`, `[data-sort]`,
    `[data-version]`, `#slvSpace[data-current]`, `#slvAna[data-active]`.

## What the mockup did differently, and why it changed
- Its solver was canned (`svBuild`, `svRun`, `svSegs`): the engine's runs,
  events and counts.
- Its run row was Prepare / Start / Continue / Stop: the original's Solve,
  Pause and Abort (a separate hard stop) with Prepare and Continue.
- Its "Drop below level" became "Keep every n-th" (the plan); its "Limit"
  is the engine's `keepAtMost` (0 keeps all).
- Its options lacked this fork's: the solver type, Check rotations,
  Strict colours, Autosave; its sort-by choices were the client's (search
  order, pieces): the original's four (Unsorted, Moves for complete
  disassembly, Level, Rotations), the ones needing the take-apart disabled
  without it, as the mockup did with its own.
- Its list was sorted in the client and drawn whole: the engine's sorts,
  paged as the list scrolls (rows drawn only in sight).
- Its player moved each piece along an axis by canned segments: the
  engine's poses, interpolated; its "Move t / total" counts the engine's
  steps (moves and turns).
- Its Del took Backspace too: the one table maps Delete only.
- Its piece chips were the shapes of one canned assembly: the pieces the
  solution places, labelled S5.1, S5.2 for copies, in the original's
  colours.

## Known issues and notes
- **Autosave needs a file**: until stage 6, only a document given a path
  (here only through the bridge, as the e2e test does) is autosaved.
- **The analysis reads the ids of the whole list first** (its targets, in
  pages of 10,000 rows): on a list of 100,000 solutions that is about
  10 MB of JSON before it starts.
- **A solution picked by hand while a run thins the list** (Keep at most
  reached, sorted) may be gone before `solution.get` or `playback.open`
  reaches the engine: the call fails `not_found`, silently; the list then
  reads its new version.
- At the end of a disassembly every piece is hidden: the engine's poses
  (the original's animation hid the last group too).
- One playback is open at a time; its meshes are read per piece and
  style (no cache across solutions; the assembled view's meshes are the
  view's mesh cache's).
- The tag shortens the problem's name beside "· Solution i of n" in a
  narrow view.
- From stage 3, still open: the maximum holes cannot be set; the right
  card of the Puzzle is re-rendered whole on a change.
- From stage 1b, still open: the one look beside the original GUI.

## Brief for the audit of stage 4

FRONTEND-PLAN.md's "Recommended audits": the event order, paging by
`solutionsVersion`, the abort and autosave paths, several problems
running at once, the pose interpolation, the core change and its test.
Where to look:
- **The event order:** `web/src/renderer/state/solves.ts` (the phase
  machine, `launch`, `whenEnded`, `clear`), its use in
  `solver/workspace.ts` (`run`, `pause`, `doAbort`, `counts`, the
  `solves.subscribe` handler) and `state/edits.ts` (`refuseList`); the
  replay tests `tests/unit/solver-replay.test.ts` over
  `fake-engine/recordings/solver*.json`.
- **Paging:** `solver/pager.ts`; the workspace's `newPager`,
  `pageChanged`, `showSelected` (current rows only), the store's
  `changed` handler (`refresh`), `solves`' `added`.
- **Abort and autosave:** `workspace.ts` (`abortable`, `abort`,
  `doAbort`), `autosave.ts` (`tick`, `saveAtSavePoint`: every run paused,
  the ends by revision, the save, the continues in `finally`).
- **Several problems at once:** the records per problem, the picker,
  autosave pausing every run, `launchOptions`.
- **The poses:** `solver/poses.ts` (chunk boundaries, `missing`, the fade,
  slerp) and `player.ts` (`ensureChunks`, the play loop holding the time
  while a chunk is missing, `show`/`reopen`/`close` and the tokens, the
  handle closed per generation).
- **The core change and its test:** `3707bcb` (`disasm_to_moves.cpp`'s
  `orientOf`, `test_app_dump.cpp`'s masking and `checkGivenOrientations`,
  the PelikanBurr case); the hotspot: `a70e3c1`; the engine's poll order
  and the kept analysis end: `9259304` (`events.cpp`'s `pollEvents`,
  `Document::poll` and `analyseProgress`, the two seam tests in
  `test_engine.cpp`).

## Brief for the next stage (after the audit)

**Stage 5** (Opus 5.5, medium), FRONTEND-PLAN.md's "Stage 5": the
explorers (Placements, Movements, Step) as the left card's step-by-step
section and the mockup's `#sdrawer` (its markup in the mockup's viewport,
`renderSolverDrawer`), the debug statistics panel (`solve.status.stats`)
and the status window (`shape.table`). Where things are:
- The placements are drawn as the Solver's assembled view is: the mesh of
  the transformation at the cell minus its hotspot (`MeshFrame.hotspot`,
  `extent.ts`'s `cellScale`), in a `SolutionScene` model (`View3D.
  setSolution`, `poseSolution`; `player.ts`'s `loadAssembled` is the
  pattern).
- The Solver's state: `SolverWorkspace` (`record`, `stateOf`, `current`,
  `player`), the records (`state/solves.ts`); Placements and Step need a
  prepared or paused solve (the mockup's rule), Movements a solution with
  its moves.
- Keys: the one table (`shell/keys.ts`); add rows for the explorers if
  they have keys; `onKey(…, first)` for a focused control's meaning of a
  shared key.
- e2e helpers: `tests/e2e/solver.ts` (`toSolver`, `solverSettled`,
  `runButton`, `runEnded`, `dominoBox`, `enginePoses`).

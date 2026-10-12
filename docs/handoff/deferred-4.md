# Handoff: deferred part 4 (stacking and Panex puzzles: the front end)

Part 4 of docs/DEFERRED-PLAN.md: the original GUI's stacking branches
inventoried and written into docs/webui-spec.md (section 9), then built
into the three workspaces: the disc list and the Rods card (Entities), the
board and its Stacks card (Puzzle), the stacking solver options, the saved
search's question, Continue and Abort of a saved search, the player in
transfers with the board's pegs (Solver), every stacking view tilted as
the original's, File ▸ New offering the stacking grid, and the app's own
folder for saved searches. No engine or core change was needed: part 3b's
`stacking.*`, `SolveOptions.stacking` and `solve.status.savedSearch` carry
everything. Next: part 5, the STL meshes and exports (no audit after this
part, as the plan says).

## Done

One commit, the part's "Deferred 4: …" (no `Engine:` or `Core:` commit:
the engine was not changed).

### The inventory (webui-spec.md section 9)
The original's stacking branches, read file by file:
`mainwindow_rods.cpp` (the rod sets' callbacks, `cb_RodField` with
`syncPanexFields` and `loadRodFields`, `showSelectedRods`, `cb_ProblemRod`,
`cb_StackMode`, `cb_StackRod`, `refreshStackSlider`, `refreshStackList`,
`refreshStackValid`, `placeOneDisk`, `syncSolverTypeMenu`,
`syncStackingChrome`), `mainwindow_layout.cpp` (the Rods panel's labels,
tooltips and bounds, the disc list, the Problems tab's rod list, rod
slider, Start/Goal, Stack Order, the validity strip), `rodbars.h`
(`StackValidBar_c`, `RodIndexBar_c`), `BlockList.cpp` (`DiskSelector`,
`RodSelector`, `RodStackList`), `resultviewer.cpp`, `mainwindow.cpp`
(`cb_NewShape`'s disc, the Problems tab's stacking branches, the Activity
lines of the Panex and stacking searches, Solve's and Continue's tooltips,
"Paused. The search is saved."), `mainwindow_solve.cpp` (Solve's question,
`cb_BtnCont`'s checks, `abortable`, the Abort question, the stacking rules
of `updateSolverOptionCheckboxes`, `animStep`), `mainwindow_file.cpp` (the
export's levels question, `problemPaused`), `mainwindow_help.cpp` (the two
solvers' help) and `voxelframe.cpp` (`showRodSet`, `showStacking`,
`showStackingAssembly`, `addPeg`, `addRodBase`, `addPanexV`,
`tiltPlates`). Found: the "Disc" and "Size" tabs (`discTabs`, with "The 3D
view shows this disc. It is one voxel thick.") are made hidden and never
shown, and the layout's `diskSizeInput` (1 to 64) is read by nothing; the
disc list's size field (1 to 100) is the one in use. The plan's "START/GOAL
trays" are the sliding view's: the original's stacking view shows the start
or the goal, as Start/Goal says. And the plan's "You have to start from the
beginning" and "Discard It" are not stacking labels: they are the
assembler's restore errors and the brick autosave's recovery question
(`mainwindow.cpp`); the stacking question is Solve's (Cancel, Continue,
Start Over), which is built.

### Entities (section 9.1)
- The shapes list's rows are the disc list on a stacking puzzle: "size"
  with its field, + and − (1 to 100; another value is refused with the
  original's message and the size goes to 1), no weight; New makes a disc
  of size 1; rename, duplicate, delete, reorder and the colour stay.
- The Rods card in the voxel editor's place (`entities/rods-card.ts`): the
  rod sets with New, Delete (never the last), Copy, Label (a dialog, as
  `fl_input`), ← and →; the selected set's rules with the original's
  labels, tooltips and bounds, greyed as `syncPanexFields` greyed them;
  each change `stacking.setRules`; frozen while anything solves. The rod
  history is the document's undo.
- The inspector and the voxel editor are hidden (`#stage.stk`); the view
  shows the disc chosen (lying flat), or the rod set chosen alone
  (`stacking.rodSetLayout`) once a rod set is chosen or edited.

### Puzzle (section 9.2)
- The board in the result's place (`puzzle/stacks-card.ts`): "Result: R1 -
  name" or "No Start/Goal Rods", the rod sets, Set Start/Goal Rods; another
  rod set chosen shows alone in the view (`cb_ProblemRod`).
- The Stacks card in the selected piece's place: Start or Goal, the rod
  slider ("Rod: P" first with a pocket; the rod taken when let go), Stack
  Order top first in the pieces' colours, ↑ ↓ +1 −1 All +1 Clear All as the
  `stacking.*` commands (a refusal's reason in the status bar, as the
  original's status line; +1 and All +1 work without a board to say why),
  the validity bar ("Valid" or "Invalid - reason").
- "All pieces" lists the discs with their size: + or a double-click puts
  one on the rod chosen; + − Del act on the stack. "In this puzzle", the
  fit summary, groups and colour rules are hidden; `problem.fit` is not
  asked on a stacking puzzle (`stacking.board` instead, read for the
  stacking edited and shown only once read for it).
- The view draws the board with the stacking's discs (`view3d/board.ts`)
  instead of the pieces scene.

### Solver (section 9.3)
- Find Solutions checked at first, with Just Levels (as the original kept
  it; the search ignores it); Just count, Enable High Memory, the board's
  one solver ("Stacking Solver" or "Panex Solver", `stackingSolverOf`,
  sent as `SolveOptions.stacking.solver`), Keep every n-th, Limit,
  Autosave (the search's own 20 minutes). No Prepare; Solve and Continue
  greyed without a board or with invalid stacks, with the original's
  tooltip.
- Solve with a saved search asks "This problem has … Continue it, or start
  over and discard it?" (Cancel, Continue, Start Over). Continue is on for
  a saved search ("Carry on …"), the Activity line then "Paused. The search
  is saved."; Abort throws a saved search away (asked). The Activity line
  while searching is the original's (Panex depth and stackings, the trace
  back, or the stacking search's count); the note comes in a message box
  when a run ends (through, or paused: where it was saved).
- No 20-minute save point on a stacking puzzle (the search saves itself);
  File ▸ Save while its search runs pauses it, saves and carries it on.
  The paused export asks about the levels beyond 1 GB.
- The player steps in transfers; the discs are placed by their centres
  around the board's middle, the pegs drawn around them; Analyse and the
  explorers are not offered (no take-apart, no assembler).

### The saved searches' folder (decisions 0013 item 13, 0015 "Part 4")
The app keeps saved searches in `panex` in its data directory: the main
process sets the engine's `panexFolder` before the page may call it
(`EngineHostOptions.setup`), and refuses a page's `settings.set` naming
`panexFolder` or `slideFolder` (`settingsRefusal`; a search's folder is
emptied and written by the engine). The e2e tests' searches stay in each
launch's own data directory.

### Removed "not yet" notes
`state/kinds.ts` (`frontEndSolves`) and its uses; the Puzzle's and the
Solver's notices (`#pzKind`, `#slvKind`) and their CSS; New's filter;
README, FRONTEND-PLAN.md, ARCHITECTURE.md and decision 0015's lines.

### Tests
- `tests/unit/renderer/stacking.test.ts` (13 cases): the stacking options,
  their rules and `SolveOptions.stacking`, the Activity lines, the board's
  boxes (pegs, base, Panex Vs, the pocket), its middle and the discs'
  places, the disc sizes, the rod sets' text and greyed fields, the rod
  slider with a pocket, the validity's words, the export's size, the
  folder settings refused; `host.test.ts`: the setup before `running`, a
  failed setup failing the start.
- `tests/engine/solver.test.ts`: a stacking solution's discs where the
  assembled view, the start's board and the goal's board put them, equal
  to the engine's first and last poses (five fixtures).
- `tests/e2e/stacking.e2e.ts` (5 tests): Hanoi and the three-disc Panex
  swap built from File ▸ New and saved byte for byte as the fixtures; both
  solved as `bt --json` and played in transfers on the engine's poses; a
  seven-disc Panex search paused, the app restarted on the same data, the
  search carried on (Solve's question, Continue) to its 2,189 transfers;
  the rod sets' commands, the rules' dependencies, frozen while solving,
  Abort of a saved search; reference screenshots `stacking-puzzle-board`,
  `stacking-rods`, `stacking-player` (reviewed). `files.e2e.ts` (New lists
  stacking third), `puzzle.e2e.ts` (Hanoi's board and stacks),
  `solver.e2e.ts` (Hanoi solved as bt) updated.

## Gate results

All run on the final tree (2026-10-11/12, M-series Mac):
- Front end, three times each, in sequence: `just web-check` clean; `just
  web-test` 318/318 (release engine; 13 new unit cases, 1 new host case, 1
  new engine case); `just web-e2e` 70/70 (6.0 to 6.1 min; 5 new stacking
  tests, 3 new reference screenshots, reviewed by eye: the board with both
  towers, the rod set with its Panex Vs, the player mid-swap). `just
  web-dist` and `web-smoke` not run: no new resource or dependency.
- C++: no core or engine change in this part; `just test` (unit, api
  schema and docs, the contract suite, the CLI) passed as a check. `just
  test-all`, ASan, TSan and the oracle tiers not run (nothing they cover
  changed).
- `just check`: not run (LLVM is not installed).

## Differences from the original, and why

1. **The editors take the places of the cards the original hid** (decision
   0013 item 11): the disc list is the shapes list's rows, the Rods panel
   the voxel editor's card, the board the Puzzle's result, the stacks the
   selected piece's card. The original's own rod Undo and Redo are the
   document's (Ctrl+Z).
2. **The app's own folder for saved searches**, not the cache folder the
   original and `bt` share: a search paused in the app does not carry on in
   the original by itself (the `.btsolve` export carries it).
3. **Save while a stacking search runs pauses it** (it saves itself), then
   saves and carries it on; the original saved the file as it stood.
4. **Prepare is not offered, Placements and Step are off** on stacking
   puzzles (the engine refuses them; the original greyed some and answered
   others with a message).
5. **The rod slider takes the rod when it is let go**, not while dragged
   (each rod is a re-read of the stack list here).
6. **The view's tag names the rod set** when one is shown alone ("R1 · Rod
   set"); the original's view had no tag.

## Known issues and notes

- The end-of-run Activity text is "Finished." without the stackings
  scanned and the depth searched (as for sliding, part 2: no reply carries
  the problem's search statistics).
- Just Levels shows on stacking puzzles, as in the original, and does
  nothing there.
- The view frames the whole board (its extent), as it frames a shape; the
  original eased its zoom back for a board (`easeRodZoom`), which framing
  by the extent makes unneeded.
- The view's tag is cut short by the toolbar on narrow views (older than
  this part, every grid).
- `just check`: not run (LLVM is not installed).

## Brief for part 5

Part 5 of docs/DEFERRED-PLAN.md, the STL meshes and exports (Opus 5.5,
high effort; extra high for `minkmesh`): port `stl*`, `cubemesh`,
`cubepoly`, `minkmesh` (Manifold 3.5.2, pinned in
`cmake/Dependencies.cmake`), `millable`, `getSTLMesh` and the half-edge
pieces they need, binary and text STL writing; port `test_cubemesh.cpp`,
`test_minkmesh.cpp` (the random-shapes stress case as `[slow]`) and
`test_manifold_smoke.cpp`; the engine's `stl` mesh style,
`export.stlParameters {grid}` and `export.stl {shape, parameters,
binary}`; the front end's STL dialog with the grid's parameters (the
"interim" words gone), Solutions ▸ Export to STL enabled (the original's
`stlexportsolution.cpp`: one file per distinct piece, names made safe and
distinct), the STL-like render style in the View menu and the Display
options. Gate: every fixture shape's mesh with default parameters equal to
a dump from the original (vertices within 1e-5, same triangle count);
every exported STL closed and consistently oriented; the solution export's
files and names equal the original's for CubeInCage; e2e of both dialogs;
reference screenshots of the STL-like style.

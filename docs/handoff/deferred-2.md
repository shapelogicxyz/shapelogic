# Handoff: deferred part 2 (sliding puzzles: the front end)

Part 2 of docs/DEFERRED-PLAN.md: the original GUI's sliding screens
inventoried and written into docs/webui-spec.md (section 8), then built
into the three workspaces: the start/goal editor (walls, starts, goals,
the validity bar), the two trays and the walls in 3D, the sliding solver
options with "Enable High Memory", the notices, the greyed buttons, and
File ▸ New offering the sliding grid. Next: part 3, stacking and Panex in
the core, the engine and `bt` (Opus 5.5 at extra high effort, then an
audit).

## Done

Two commits: `651a7e7` "Core: sliding shapes stay one layer deep, pieces
keep plain names", then the part's "Deferred 2: …".

### The inventory (webui-spec.md section 8)
The original's sliding branches, read file by file: `mainwindow.cpp`
(`updateInterface`'s Puzzle tab: New, Copy and Delete problem, Set
Result and Groups greyed, the colour groups hidden, a required piece's
count locked; `updateEntitiesTab`: the start/goal row, Delete Shape and
the shape colour greyed on a tray; the `RS_SLIDING_CLICK` callback; the
load's flattening message; the Activity line's "Sliding search, N moves
deep."; the solver note's message box; Solve's tooltip),
`mainwindow_rods.cpp` (`applySlidingGridMode`, `cb_NewStartGoal`,
`cb_DelStartGoal`, `refreshSlideValid`), `mainwindow_layout.cpp` (the
row's buttons, the three sliding boxes and their tooltips),
`mainwindow_solve.cpp` (`cb_BtnCont`'s checks and flags,
`updateSolverOptionCheckboxes`: Find Solutions, which boxes go),
`mainwindow_help.cpp` (the three solvers' help), `tooltabs.cpp`
(`ToolTab_0`'s Start/Goal tab, its piece list and flat preview, `lockZ`),
`grideditor.cpp` (the tray's tiles, the marks in the pieces' colours,
`drawStampPreview`'s orange/green/red), `BlockList.cpp`,
`slidingcolors.h`, `voxelframe.cpp` (`showSingleShape`'s START and GOAL
trays, `buildSlidingWalls`, `drawSlidingWalls`), `gridtypegui.cpp`
(Sliding second in New's list). Found: `ToolTab_Sliding` ("Tray edit",
"Walls", Start, Goal) is never made by the original; its labels name the
new Tray edit switch.

### The core (`651a7e7`)
What the original only greyed out for sliding shapes is refused, as
decision 0014 does for the rest: `taskSet(Sliding)` leaves out the tasks
that add layers (nudges along Z, turns about X and Y, double, triple,
the grid scale; `shape.task` answers them `unsupported`),
`Document::resizeShape` refuses another Z on a sliding puzzle, and a
piece's rename to a name beginning "sg:" or "__sggoal:" is refused
(`sliding::isReservedName`; the audit's note for part 2). Tests: the unit
case "shapes stay one layer deep, pieces keep plain names", the contract
case `sliding_start_goal_shapes`; `docs/API.md` and the generated types.

### The front end (this part's commit)
- **Entities:** the shapes list's "(START/GOAL n)" rows with black chips;
  New/Delete Start/Goal Positions above the list (`#sgRow`; a new tray
  takes the selected shape's X and Y, else 6 by 6); a tray's Delete and
  Colour greyed; no Paint tool and no palette on sliding puzzles; the Z
  stepper and the XZ/YZ planes off; on a tray the **Tray edit** switch:
  Walls (the tools as floor, corridor, wall), Start and Goal (the piece
  list with the chosen piece drawn flat, the marks in the pieces'
  colours, the piece outlined under the mouse from `sliding.preview`, a
  click `sliding.place`, a right click `sliding.clearAt`, the refusal's
  reason in the status bar), its Start and Goal green or red; the
  validity bar under the view; the view's two trays with the pieces
  standing on them and their titles (`view3d/trays.ts`).
- **Puzzle:** no New, Copy or Delete problem, no Change…, no groups or
  colour rules; a required piece's −/+, count fields, range and remove
  off with the reason; the tray drawn dark grey.
- **Solver:** Assemblies or **Find Solutions** (checked at first); Just
  count, Allow Nested Slides, Allow Partially Nested (needs nested),
  Enable High Memory, the sliding Solver type (`engine.info`'s
  `slidingSolvers`), Keep every n-th, Limit, Autosave; stored with the
  problem under the file's names; `SolveOptions.sliding`; Solve and
  Prepare greyed while the tray is invalid (the reason as Solve's
  tooltip); the Activity line's "Sliding search, N moves deep. Scanned N
  arrangements."; the solver's note in a message box at the end of a run
  (after an autosave's continue too); the walls around the tray in the
  player (`view3d/walls.ts`); Movements and Analyse not offered.
- **Notices:** `document.changed.notice` and an opened file's
  `Snapshot.notice` in message boxes, one at a time (`Store.onNotice`,
  `dialog.ts`'s `tell`).
- **New:** every grid this version solves, in the original's order
  (Brick, Sliding, then the others).
- Tests: `tests/unit/renderer/sliding.test.ts` (12 cases);
  `tests/e2e/sliding.e2e.ts` (4 tests: the 81-move puzzle built from New
  and checked against the fixture, solved by the three solvers as `bt
  --json`, played step by step on the engine's poses with the walls; the
  nested fixtures; the notices and the refusals; reference screenshots:
  `sliding-tray-walls`, `sliding-tray-start`, `sliding-tray-goal`,
  `sliding-trays`, `sliding-walls`); `files.e2e.ts` (New lists sliding
  second).

## Gate results

All run on the final tree (2026-10-10, M-series Mac), in sequence:
- Front end, three times each: `just web-check` clean; `just web-test`
  304/304 (release engine; 12 new unit cases); `just web-e2e` 65/65
  (5.6 to 5.7 min; 4 new sliding tests, 5 new reference screenshots,
  reviewed by eye: the tray in Walls, Start and Goal, the two trays with
  their pieces and titles, the walls around the 81-move puzzle). `just
  web-dist` and `web-smoke` not run: no new resource or dependency.
- `just test-all`: 6/6 (unit with the new sliding document case, the
  slow cases, the api schema and docs, the contract suite with every
  method called, the CLI), 378 s.
- `just test-asan`: 6/6, no sanitizer report, 1,285 s.
- `just test-tsan`: not run (no solver change; the core change is three
  refusals in the document and the task set).
- `just oracle --tier fast`: 19/19 (all fields); the sliding tier on one
  thread: 35/35 (all fields); `just oracle-roundtrip`: 432/432
  byte-identical to the original's save.
- `just check`: not run (LLVM is not installed).

## Differences from the original, and why

1. **The start/goal editor is a switch, not a tab** (decision 0013 item
   8): the mockup's voxel card has no tool tabs; the original's unused
   `ToolTab_Sliding` had the labels.
2. **Refusals where the original greyed out** (decision 0013 item 9): the
   engine is the truth, so the tasks that add layers, another Z and
   "sg:" names are refused there, not only hidden.
3. **Movements and Analyse are not offered on sliding puzzles** (item
   10): the engine has no take-apart there.
4. **"Finished." without the search's counts**: the original's Activity
   line added "Scanned N arrangements. Searched N moves deep." from the
   problem's search statistics, which no reply of the engine carries
   (known issue below).

## Known issues and notes

- The end-of-run Activity text lacks the scanned arrangements and depth
  (an engine addition: the problem's `searched` and `searchDepth` in
  `ProblemSummary` or `solve.status`).
- On several threads a sliding puzzle with several starts can give
  different counts from run to run, as the original's did (deferred-1.md);
  the Solver does not say so.
- The hovered cell of the 2D grid draws its own outline partly under its
  neighbours on every grid (the cell's `:hover` stroke); with a stamp's
  outline over it, a short blue corner shows. Cosmetic, older than this
  part.
- `shape.setCells` with a colour still paints a stray mark on a sliding
  cell: the front end offers no colour there, the engine does not refuse.

## Brief for part 3

Part 3 of docs/DEFERRED-PLAN.md, stacking and Panex in the core, the
engine and `bt` (Opus 5.5, extra high effort; it may split into 3a:
stacking and the stack search, 3b: Panex, blockpack and the saved
searches): port `stacking.*`, `panex.*`, `blockpack.*` and their parts of
`solvethread`, `paused_solve`, `puzzle` and `problem`; the rod sets as a
model with byte-identical saves; `solve.*` for stacking with Panex's
folder in the engine's settings; a `stacking.*` namespace; transfers in
`playback.*`; the oracle's stacking tier and a Panex resume check; port
`test_stacking.cpp` with per-process scratch folders; TSan. The front end
keeps showing stacking puzzles with the solver disabled until part 4.

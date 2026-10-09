# Handoff: web stage 3 (the Puzzle workspace)

**Date:** 2026-10-09 · **Next:** stage 4, the Solver workspace and the player.

## Done

No engine addition and no core fix: the `problem.*` methods cover the
stage (see "Known issues" for the one gap, the maximum holes).

### Shared bookkeeping (`web/src/renderer/state/edits.ts`)
`Edits`, moved out of the Entities workspace as stage 2's brief asked:
`mutate` (a call awaited until the page shows its revision; the wait is
the workspace's: the snapshot for Entities, the problem's cards for
Puzzle), `track`, `refuse` (a frozen part says "Pause the solve first"
before any call) and the one count of edits under way,
`#stage[data-busy]`. The Entities workspace delegates to it.

### The Puzzle workspace (`web/src/renderer/puzzle/`)
Each file names the mockup's functions it came from.
- `workspace.ts` (`PuzzleWorkspace`): the problem shown, read after every
  change by `problem.get` and `problem.fit` (coalesced, the newest load
  winning) and once by `problem.kind`; `#leftP[data-problem]` and
  `[data-revision]` say what is shown, and every action is a call awaited
  until the cards show its revision (`whenShown`). The actions: one more
  and one fewer (`addPiece`, `removePiece`), remove (`setPieceCount 0 0`,
  "Removed from the puzzle" for Del), a count or range from a field or a
  stepper (`setPieceCount` with `counts.ts`'s `nextCount`), the range
  switch, set result, move a part (`movePart`; a drag of several places
  as one undo step: the moves inside `edit.begin`/`edit.end`), groups
  (`setGroup`, `setGroupCount`, `deleteGroup`), colour rules
  (`setColourRule`, `setColourRules`), add one of each, the bulk range,
  remove all. The frozen state (decision 0011): the problems list while
  anything is solved (`Snapshot.solving`), a problem while it is solved or
  analysed (`ProblemSummary.solve` active, `analysing`; `#stage.pzfrozen`,
  `data-pzfrozen`), refused before any call; selecting and looking stay
  allowed. The keys + and − (one more, one fewer) and Del (out of the
  puzzle), all on the selected piece; the status line
  (`FitStats.statusLine`, the original's; "is being solved — pause the
  solve first", "has no result", "No problems"); the view's tag (pzTag)
  and empty message; the scene's model.
- `problems.ts` (`ProblemsList`): P1, P2… with names; new (shown at
  once), duplicate (added last, shown), delete (never the last; the
  problem now in its place is shown), rename in place (the pencil or a
  double-click; Enter commits, Esc cancels; the engine's refusal shows as
  "Rename: …"), reorder by drag (`problem.move`; "Order changed —
  problems renumbered P1…Pn").
- `pieces-card.ts` (`PiecesCard`): the result with its cells and "can
  hold n–m" (`FitStats.resultMin`–`resultMax`) and Change… (a popover of
  every shape); the parts in the problem's order as chips (count or
  range, group badge, − and +) or a table (count fields, range box,
  group select with + New); "All pieces" (every shape but the result,
  ×n for those in the puzzle; drag a chip in, double-click it or press
  its +; drag a tile out onto the library or its header; a tile dropped
  on another moves it there); the More menu (add one of each, set range
  for all pieces… as the mockup's dialog, remove all); the right-click
  menus of tiles, rows and chips (one more, one fewer, remove, set as
  result; move earlier and later for a part); the fit summary from
  `FitStats` (`#pzSummary[data-fit]`, `[data-verdict]`); the notice for
  the puzzles this version opens but does not solve (`#pzKind`,
  `[data-kind]`).
- `piece-card.ts` (`PieceCard`): the selected piece (its cells; "Copies
  in the puzzle" or "Count range" with Min and Max steppers, the switch
  between them; the exclusive group: None, G1…, + New, and for a piece of
  several copies the number in the group, `setGroupCount`); the groups
  with their members (each with its copies in the group) and delete;
  the colour rules matrix over the puzzle's constraint colours with the
  presets "Each colour on itself", "Allow all", "Clear".
- `counts.ts` (the count as the chips write it, `nextCount`, the groups
  read off the parts, `solveActive` = the core's `isActive`),
  `layout.ts` (`puzLayout`, the scene's items and positions; `label`).

### The pieces scene (`web/src/renderer/view3d/pieces.ts`, `view.ts`, `renderer.ts`)
In the Puzzle workspace the view draws `PiecesScene` instead of the one
shape: the result, the selected piece and every copy of every piece in
the mockup's layout. One `shape.mesh` per shape (asked at the revision
the shape last changed at, so a problem edit asks for no mesh), its
geometry shared by every copy; the optional copies (above a part's
minimum) faded, their nearest surface only (a depth pass first: blended
on both sides through every bevel, 0.42 looked solid); each item turned
about its own centre by the camera's orientation while the layout stays
on the screen (`ViewRenderer.frameScene`: the mockup's scale, the eye
2.5 layouts away); pan and zoom move the layout. The labels ("RESULT ·
S1", "SELECTED · S3", "S3 · 1/2", "(optional)" on a line of its own) and
the selection and hover boxes are HTML in `#pzScene`; a click picks the
smallest box under it, never the result, and selects the piece. At most
100 copies are drawn. `#vp` says `data-scene` (role, shape, copy/total,
faded, from the three.js objects), `data-scene-meshes`,
`data-scene-key`, `data-scene-style`.

### Shell
- `keys.ts`: the Puzzle row "+ / −" has chords (`piece.more` on `+` or
  the Equal key, `piece.fewer` on `-` or the Minus key, so = and _ too,
  as the mockup took them); Del stays the one action `delete`, which the
  Puzzle workspace handles in its tab (32 chords).
- `app.ts`: the status line is the workspace's (the Puzzle's fit line).
- `dom.ts`: `setHtml` (a list's HTML replaced only when it changed:
  the mockup re-rendered the lists on every click, and a double-click
  whose first click re-rendered never reached its element: the library's
  double-click added nothing until this), `forgetHtml`, `markSelected`.
- `index.html`: the Puzzle cards are the mockup's markup (lines 768–778
  and 835–843 of `docs/mockup/index.html`) with `#pzKind` and
  `#pzScene`; `styles.css`: the stage's rules at the end (the scene's
  boxes and labels, the notice, the frozen controls, the tag's name
  shrinking before the toolbar).

### Tests
- Unit (`tests/unit/renderer/puzzle.test.ts`, 11; `keys.test.ts` +1):
  the counts as written, `nextCount` (fixed and range, a piece not in the
  puzzle, removal at 0, what a field holds, and 2,000 random cases never
  asking for a minimum above the maximum), the groups, `solveActive`, the
  layout (positions as the mockup's, rows of five, the selected piece
  left out when it is the result, `MAX_COPIES`), the labels; the new
  chords.
- Engine (`tests/engine/puzzle.test.ts`, 4): every count `nextCount` asks
  for is taken and given back by the engine; the groups as the UI reads
  them (a copy added later joins no group, so a group is set after the
  count; `setGroup` takes every copy; `setGroupCount`; `deleteGroup`); a
  new problem allows each colour on itself; the kinds and `supported` of
  a sliding, a stacking and a brick fixture; every fixture's problems
  (more than 15) with a fit that agrees with their parts and a layout of
  their copies.
- e2e (`tests/e2e/puzzle.e2e.ts`, 3; helpers `tests/e2e/puzzle.ts`):
  CubeInCage's problem built in a new problem through Change…, a
  double-click, a drag in, the tiles' and the library's + and −, the
  menus, the + − Del keys, Move earlier, two tile drags (the first one
  undo step "Move piece"), the groups by + New and G1, a copy out of the
  group and back (`setGroupCount`), the group deleted and made again, the
  table's count fields, range box and group select, a click on a piece in
  the scene, an orbit (the boxes stay put while the camera turns) and the
  flat style; DemoPieceGenerator's range built through a chip's "Set as
  result", More's add one of each and remove all, a drag in, the bulk
  range dialog, the range switch and the copies field, the Min steppers.
  After every action `checkShown`: the chips and the table (the view
  switched to read the other) both equal `problem.get`'s parts, the fit
  summary equals `problem.fit`, the status bar its `statusLine`, the
  scene one mesh per copy with exactly the optional ones faded. Each ends
  with `problem.get` equal to the fixture's (result, parts with min, max
  and groups, colour rules, maxHoles) and a call log without a refusal
  (only the client's own `superseded` reads). The third test: the
  problems list (empty message, new, rename, Esc cancelling, duplicate,
  new, a drag, delete; labels and names equal to `problem.list`), the
  colour rules (a cell and the three presets, matrix and `problem.get`
  agreeing), a problem frozen by a solve started through the bridge
  (keys, buttons, menus, Change…, presets and + New refused with no edit
  sent, the controls disabled; another problem's pieces editable, the
  list not), editing again after the abort, and the sliding and stacking
  fixtures opened with the notice (a stacking problem without a result
  shows its pieces and "noResult").
- A mutation check: dropping the frozen refusal of "one more" and drawing
  the optional copies unfaded each fail the tests (all three, by the
  call log and by `data-scene`).

## Gate results
| Check | Result |
|---|---|
| `just web-check` | clean, three times (three TypeScript projects, ESLint without warnings, Prettier) |
| `just web-test` (release engine) | **180/180**, three times, ~14 s each: 20 files (stage 2's 164, `unit/renderer/puzzle.test.ts` 11, `keys.test.ts` +1, `engine/puzzle.test.ts` 4) |
| `just web-e2e` | **33/33**, three times, 3.9 min each (build included): stage 2's 30 and `puzzle` 3 (CubeInCage ~10 s, DemoPieceGenerator ~5 s, the problems/rules/frozen/kinds test ~14 s) |
| perf (each e2e run) | mesh build 6.3, 6.0, 5.5 ms; orbit median 13.3/13.4/13.3 ms at a 13.3 ms display frame, p95 14.7/14.9/15.0 ms; 150–152 renders in 152–153 frames |
| repeats while building | the Puzzle e2e file run several times while it was written (a lost double-click and a stale list cache found and fixed on the way), the whole e2e suite once more before the gate; the mutation check above |

No engine or core change, so no C++ finishing steps; `just build release`
was current (ninja: no work to do).

## The reference screenshots
Unchanged: the 36 of stages 1b and 2 pass as they are (the Entities
viewport did not change; the scene's label layer is hidden outside the
Puzzle workspace). The stage adds none: the Puzzle is checked against
the engine, not pictures; this session's screenshots of CubeInCage,
DemoPieceGenerator (the faded copies), Bermuda, the sliding fixture
(the notice and a matrix of twelve colours) and the table were looked
at.

## Where this stage departs from the plan, and why
1. **One more and one fewer are the original's** (`addPiece`,
   `removePiece`: the minimum and the maximum both, `cb_AddShapeToProblem`),
   where the mockup changed a range's maximum only; the steppers and
   fields of the selected piece and the table set one end
   (`setPieceCount`).
2. **The range switch is the view's** (the engine has a minimum and a
   maximum, no "fixed" flag): a piece shows as a range when its ends
   differ or the switch was turned on for it (per problem, until another
   document); turned off, both ends take the maximum, as the mockup's.
3. **Groups are the engine's:** a group exists while a part has copies in
   it, so "+ New" puts the piece in a new group at once and the mockup's
   empty groups ("No members — pick this group on a piece") cannot be;
   a piece with copies in several groups lights each, with the copies in
   it. `setGroupCount` is the "Copies in G1" stepper of a piece of several
   copies.
4. **`movePart`** has the tiles' menu items and a tile dropped on another
   (the mockup could not reorder pieces).
5. **The scene's copies follow the problem's parts** (its piece
   numbering), where the mockup went through the shapes in order; one
   mesh per shape shared by its copies rather than a three.js
   `InstancedMesh` (the faces' shader has no instance matrix, and at most
   100 copies are drawn); the boxes come from each piece's bounding
   sphere (the same while it turns).
6. **The colour rules have the constraint colours only** (the original's
   constraints window; `problem.setColourRule` takes colours): the
   mockup's "Default" row and column are gone, a cell without a colour
   fits any.
7. **The fit summary's holes** are the engine's `holesMin`–`holesMax`
   (with the file's maximum when it sets one), where the mockup computed
   them or said "Holes depend on the counts".
8. **"The solver disabled"** for sliding and stacking puzzles is the
   notice; there is no Solver workspace yet (stage 4 disables it by
   `problem.kind`).
9. **Test hooks** as before: `#leftP[data-problem]`, `[data-revision]`,
   `#stage[data-pzfrozen]`, `#pzSummary[data-fit]`, `#pzKind[data-kind]`,
   `#pzSel[data-shape]`, `#vp[data-scene…]`.

## What the mockup did differently, and why it changed
- Its problems, counts, `lock` flags, groups and rules were a client
  model, and `pzStats` counted cells: the engine's problem, read after
  every change, and `problem.fit`.
- A new problem took the first shape as its result: the engine's has
  none, so the card says "No result" and Change… chooses.
- Its scene projected cubes by hand with alpha per face in painter's
  order: three.js over the engine's meshes, the faded copies by a depth
  pass.
- Its labels "S3 · 1/10 (optional)" overlapped in a row of copies at
  this window's size: "(optional)" goes on a second line.
- Its lists were re-rendered on every click, which loses a double-click:
  the lists keep their elements unless their HTML changed.
- Its tag ran under the toolbar with a long name: the name shrinks.
- The problems' delete asked nothing, as the mockup's (Ctrl+Z undoes it).

## Known issues and notes
- **The maximum holes cannot be set:** no method sets `maxHoles` (the
  original's groups editor had the field; decision 0013, item 6). The fit
  summary shows a file's. A `problem.setMaxHoles` engine method and a
  field in the groups section would close it (a later stage; neither gate
  fixture sets it).
- **A paused BurrTools 2 search** makes the engine refuse changes of the
  lists as `invalid_state` (decision 0011: its position lives in memory
  only); the snapshot does not say which solver a paused problem used, so
  the Puzzle cannot refuse those first, and "Pause the solve first" would
  be the wrong advice (abort or finish it). Stage 4 starts the solves and
  knows: see the brief.
- The right card is re-rendered whole on every change: a number typed
  into one of its fields and not yet committed is lost when a change
  arrives meanwhile (the left card's lists keep theirs).
- The scene's scale follows the largest item (the mockup's rule): a large
  result makes small pieces small; zoom helps.
- The view still loads the Entities shape's mesh while the Puzzle is
  shown (as before; the e2e helpers wait on it after opening a file).
- The colour matrix of a puzzle with many colours scrolls sideways (the
  sliding fixture has twelve).
- The hover box (dashed) is not covered by a test.
- From stage 1b, still open: the one look beside the original GUI.

## Brief for the next stage

**Stage 4** (Opus 5.5, extra high), FRONTEND-PLAN.md's "Stage 4", with
its two C++ commits first (`Engine: hotspot` in `MeshFrame`; the dt-less
playback orientation in the core). Where things are:
- The Solver cards (`#leftS`, `#rightS` in `index.html`) are
  placeholders; the mockup's markup is at lines 780–795 and 844–854 of
  `docs/mockup/index.html`, its script from line 2022 (`svBuild`,
  `svRun`, `svPrepare`, `svStep`, `svManage`, `renderSolver` at 2074,
  `svDaCell`, `renderSvSel`, `renderSvSortAct`, `renderPcs`, `svSelect`,
  `renderSolverPlayer`, `renderSolverDrawer`, `renderSolverScene`,
  `svTogglePlay`, `svSeek`, `svKey`, `svPlayLoop`); its `#sdrawer` (the
  solutions drawer) is in the mockup's viewport markup, not yet in
  `index.html`.
- Follow `puzzle/` as the pattern: a workspace class over the store's
  snapshot and the engine's replies, `Edits` (`state/edits.ts`:
  `mutate` with the workspace's own wait, `refuse`, `data-busy`), a
  `data-revision` of what the cards show, `setHtml` for lists that must
  keep their elements.
- The frozen state: `Snapshot.solving`, `ProblemSummary.solve`
  (`counts.ts`'s `solveActive` is the core's `isActive`). Decision 0011:
  while a **BurrTools 2** search is paused, changes of the lists are
  refused (`invalid_state`); the Solver knows which solver it started, so
  its per-problem solve record should say so and the Puzzle's list
  actions (`PuzzleWorkspace.refuseList`) refuse those with the right
  advice ("abort or finish the paused BurrTools 2 solve first").
- Sliding and stacking puzzles: `PuzzleWorkspace.kind` (`problem.kind`,
  cached per document) and `engine.info.grids[].supported`; the Solver
  is disabled for them (the Puzzle's notice already says so).
- The view: `View3D` switches by workspace (`tab`): the Puzzle draws
  `PiecesScene` (`setPieces`, a `PuzzleHost`); the Solver needs its own
  scene (the assembled solution from `solution.get` placements and
  `shape.mesh {transformation}` at the cell minus the new `hotspot`, and
  the player's poses), its own host and `renderer.frameScene` or
  `frame` as fits; `meshcache.ts` keys meshes by transformation already.
- Keys: the Solver rows have no chords yet (Space/Enter, ←/→ and
  Shift, ↑/↓, Home/End, PgUp/PgDn); Del is again the action `delete`:
  register a Solver handler with `onKey` (the Entities and Puzzle
  handlers return false outside their tabs); `keys.test.ts` counts the
  chords (32 now).
- e2e: `launch({callLog})`, `engineCall`, `openFixture`, `readCallLog`,
  `settle`, `camera`, `style` (`tests/e2e/app.ts`), `settled`
  (`editor.ts`), `pzSettled`, `checkShown` (`puzzle.ts`) as models; the
  plan's gate also asks for fake-engine replays of the event-order
  rules (`web/fake-engine/`, `npm run record`).

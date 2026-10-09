# Handoff: web stage 5 (the explorers, the debug panel, the status window)

**Date:** 2026-10-09 · **Next:** stage 6, menus, files, settings and
recovery (then the short audit of its file paths).

## Done

### One C++ commit first
- **`269d0ef` Engine: explore.openMovements vectors.** The movement
  explorer's reply names each direction of the grid's movement cache by
  its step in grid coordinates (`vectors`, a `Cell` per direction,
  reversed is the negation) beside its labels. A label does not say how
  far a step goes: Bermuda is a prism puzzle, whose directions are
  (0, 0, 1), (2, 0, 0), (1, 1, 0), (−1, 1, 0), labelled "z", "x", "xy",
  "-xy", and `explore.findMatching` takes the step itself, so a dragged
  piece cannot be matched from the labels (the contract session's unit
  deltas found only the z moves on prisms). `MovementExplorer::
  directionVectors` in the core; the app test checks both grids' tables
  and that a prism piece moves by a vector and not by a unit x step; the
  contract and web session tests drag by the vectors. Its finishing
  steps: `just test-all` 6/6 (unit 48 s, unit-slow 273 s, contract,
  api-docs, cli), the contract suite (95 methods called), `python3
  api/gen_api_md.py`, `just web-gen`, the staleness test and the web
  session test against the new release engine. No oracle tier touches the
  explorers.

### The explorers (`web/src/renderer/explore/`)
- `explorer.ts` (`Explorer`): the mockup's drawer at the foot of the view
  (`#sdrawer`, `renderSolverDrawer`) with three views, each over an
  engine handle on a copy of the puzzle, and the Advanced section's
  buttons (`#slvAdv`: Placements, Movements, Step; the mockup's rules:
  Placements and Step need a prepared or paused solve, Movements a
  solution with its moves).
  - **Placements** (`explore.openAssembly` with the search options shown,
    `explore.placements`): a row per piece with its count (every piece's
    list read at once, the chosen one first), the placement chosen
    (Back/Next, a slider, ↑ ↓ Home End; ← → the piece, as the original's
    browser), drawn as the result's wire frame and the piece solid.
  - **Step** (an assembler of its own, `explore.step` by 1, 10, 100 or
    1,000; Reset opens a new one): the pieces placed now drawn in the
    result's wire frame, the list of pieces placed, `finished`, the steps
    taken; the left card's Step button steps once (the mockup's svStep)
    and opens the view; Space or Enter in the drawer steps.
  - **Movements** (`explore.openMovements` on the selected solution and
    `solution.get` for its orientations): the tree (`tree.ts`) with
    Analyse, Add move… (the grid's directions by label, forward and
    reversed, and the pieces to move; valid or not), Step back, Prune
    tree, ↑ ↓ ← → Home End Enter; a matching move by **Alt-dragging a
    piece** in the view: the drag is compared with each direction's step
    (`vectors` × `cellScale`) as it looks on the screen at that piece
    (`View3D.pieceOnScreen`), the closest within 45 degrees, forward or
    reversed, goes to `explore.findMatching`; "No move of that piece that
    way" when the engine finds none. Another solution chosen opens its own
    tree.
  - Pieces are drawn as the assembled view draws them
    (`extent.ts`'s new `placedAt`: the mesh of the transformation at the
    cell minus its hotspot, scaled, centred on the result; the player
    uses it too), from the view's mesh cache, in the view's **explorer
    layer** over the player's solution. The handles are closed when the
    drawer closes (its ×, Esc, the Advanced button again), the problem
    shown changes or an explorer is no longer allowed (a run started);
    another document's are dropped without a call (the engine dropped
    them). The tag says what is shown ("· Placements: S7, 3 of 10",
    "· Assembler state — 11 of 12 pieces placed", "· Movements: position
    2").
- `tree.ts` (DOM-free): `MovementTree` (the nodes the engine returned,
  each under its parent in the engine's order; Prune Tree as the engine
  does it: the path to a node, without its children; the rows depth
  first), `dragDirection`.
- `debug-stats.ts` (DOM-free): the debug panel's report, ported from
  `debugstatspanel.cpp` (`formatReport`, `appendIdleRows`,
  `buildAnalysis`, `formatDuration`, `statusTitle`; its unsigned integer
  divisions truncated as the original's).

### The two dialogs
- `solver/stats-panel.ts` (`StatsPanel`): the debug statistics of the
  problem the Solver shows (`solve.status`'s `stats`), read again with
  every event of its solve (state, progress at most four times a second,
  end) and when another problem is shown; the original's rows and its
  analysis notes. Opened by "Debug statistics…" under the explorer's
  buttons. It holds the figures of a run that stopped until a run is under
  way again or Refresh is pressed, as the original's panel did
  (`freezeLive`): see "Found in the original".
- `entities/status-window.ts` (`StatusWindow`): the original's status
  window over `shape.table`: the shape (chip), Units (normal, variable,
  total), Identical (mirror, shape, complete: the earlier shape's chip),
  Connectivity (face, edge, corner: X, or the shape's colour where the
  original coloured the box), Holes (2D, 3D), Tools (notch, mill; cube
  grid only), Sym ("---" when the grid's tables do not know it); the
  shape's status line as the row's tooltip. Refresh, Close, Remove
  selected (one undo step, `edit.begin`/`edit.end`; refused while solving
  before any call; never every shape), Select holes, Select Identical
  Shapes / Complete / Mirror (added to what is selected, as the
  original's). Read again when a change names the shapes or colours, and
  for another document. Opened by a temporary **Status…** button in the
  top bar until stage 6's menus.
- `shell/panel.ts`: a window of its own over the workspace (title, ×,
  body, buttons), **not modal** (the solve it watches can be paused and
  carried on beside it); Esc closes the newest after a dialog and a menu.

### The view and the shell
- `view3d/view.ts`: two layers of solution models (`setSolution(model,
  layer)`, `poseSolution(poses, layer)`: the player's, and an explorer's
  shown over it while it has one; `#vp[data-solution-layer]`);
  `pieceOnScreen`; in the Solver, an Alt (or ⌘) drag that starts on a
  piece goes to the host (`movesPieces`, `pieceDragged`) instead of
  orbiting, while the movement explorer is open; `#vp[data-hover-piece]`
  and `[data-piece-screen]` while it is. `view3d/solution.ts`: `pick` (the
  piece a ray meets, never the result an explorer draws around them),
  `centreOf`.
- `shell/keys.ts`: an **Explorers** group of rows (no new chords: 38 as
  before): while the drawer has the focus, ↑ ↓ ← → Home End Space Enter
  mean the explorer's things (`onKey(…, first)`), the Alt-drag, Esc
  closes the explorer (`ESC_ORDER.playback + 5`).
- `index.html`: the mockup's Step-by-step explorer section (lines 790–792
  of the mockup) with "Debug statistics…", the drawer `#sdrawer` in the
  viewport (line 800), the Status… button; `styles.css`: the stage's
  rules at the end (the drawer's CSS was the mockup's already).

### Tests
- Unit (`tests/unit/renderer/explore.test.ts`, 11): the tree (order, a
  node replaced by the engine's, Prune Tree, rows and paths), the drag's
  direction (forward, reversed, 45 degrees, a click, a direction seen end
  on), `placedAt` on cubes and prisms, the report (durations, idle rows,
  a finished run's rows and notes, take-apart off, a row per take-apart
  under way). `keys.test.ts`: the help's groups with Explorers.
- Engine (`tests/engine/explore.test.ts`, 3): every placed piece of the
  first solutions of every fixture (five grids, over 200 pieces) is among
  its piece's `explore.placements`, so placements are in the solutions'
  frame and drawn by the same formula; the stepped assembler's first
  complete set (Bermuda) has each piece at one of its placements; the
  client's `MovementTree`, grown by the replies alone (Analyse twice, Add
  move, a matching move) and pruned, is `explore.node`'s for every row,
  the pruned ones `not_found`. `solver.test.ts` now checks `placedAt`
  itself (it had the formula's twin: the stage 4 audit's note).
- e2e (`tests/e2e/explore.e2e.ts`, 3, on Bermuda): prepared, the debug
  panel's numbers `solve.status`'s (all but the wall time, held, which
  must not exceed the engine's) and its rows `statsReport`'s; Placements:
  every piece's count `explore.placements`' on the UI's own handle, its
  first and last placement (Home, End) drawn where the engine says (the
  result's wire frame and the piece; the pose `placedAt` of the turned
  mesh's hotspot from `shape.mesh`), ↑ ↓ ← →; Step: the engine's own
  explorer stepped one by one to its first complete set (step 1,022), the
  UI's Run, 10 and Step buttons to the same count: the same placements
  and `finished`, every piece drawn where the engine says, then Run until
  `finished` grows past it; Esc closes the drawer and both handles
  (`explore.close` answers `not_found`). Solved with take-apart:
  Movements' root, Analyse (every child's label and `explore.node`), Add
  move (the first piece, direction 0), an Alt-drag of a visible piece
  along a direction the engine's `findMatching` accepts, Prune tree at
  Analyse's first child, each equal to the replies of the same calls on an
  explorer of the test's own (the contract session's calls) and drawn
  where the engine says; ← →; the drawer's × closes the handle. The status
  window on every fixture: each row's cells and tooltip `shape.table`'s;
  Select Identical Mirror on SolidSixPieceBurrs ticks the engine's rows,
  Remove selected removes them as one undo step ("Remove shapes"), Esc
  closes the window, Ctrl+Z brings them back. Each test's call log holds
  no refusal.
- Mutation checks: drawing the movement positions in transformation 0
  fails the movement e2e (the drawing assertions are load-bearing); the
  unit tests cover the tree's prune and the drag's sign.

## Gate results
| Check | Result |
|---|---|
| `just web-check` | clean, three times (three TypeScript projects, ESLint without warnings, Prettier) |
| `just web-test` (release engine) | **226/226**, three times, ~18 s each: 25 files (stage 4's 212 after its audit; `unit/renderer/explore.test.ts` 11, `engine/explore.test.ts` 3) |
| `just web-e2e` | **41/41**, three times, 4.4, 4.5, 4.4 min (build included): stage 4's 38 and `explore` 3 (~3 s, ~2.5 s, ~1.6 s) |
| perf (each e2e run) | mesh build 5.9, 4.9, 5.7 ms; orbit median 13.3/13.4/13.4 ms at a 13.4/13.3/13.3 ms display frame, p95 15.1/14.1/14.6 ms; 150–152 renders in 152–153 frames |
| C++ (the engine commit) | `just test-all` 6/6, the contract suite 0 failed (95 methods), `api-docs`, `just web-gen`, the staleness test |
| before the gate | the explore e2e file run some ten times while written; two first gate rounds were stopped and the gate run again whole: a keys unit test that lists the help's groups (the new Explorers group), then an ESLint error in the new unit test |

## The reference screenshots
Unchanged: the 36 of stages 1b and 2 pass as they are (the explorers draw
only in the Solver, the dialogs only when opened). The stage adds none:
the explorers are checked against the engine (counts, placements, poses,
nodes), not pictures; this session's screenshots of the debug panel
(Bermuda prepared), Placements (S7, placement 3 of 10 in the result's
wire frame), Step (11 of 12 pieces placed after 1,011 steps), Movements
(the root analysed: 253 moves; a position chosen), the Add move form and
the status window (Bermuda's 23 shapes) were looked at.

## Where this stage departs from the plan, and why
1. **An engine addition the plan did not list** (`vectors`, above): the
   plan's "findMatching by dragging a piece" needs the steps, which the
   labels do not give on the prism grid.
2. **The drag is an Alt-drag** (or ⌘): a plain drag orbits the view, as
   everywhere; the original found a matching move by a Shift-click
   (push) or Ctrl-click (pull) on a face, and Alt added the move even
   when invalid. Here the direction is the drag's; a move valid or not is
   Add move….
3. **Step is a third view of the drawer**: the mockup's drawer had two
   (Placements, Movements) and its Step only moved a fake counter shown
   in the view; the original stepped the problem's own prepared
   assembler. The stepper here is an assembler of its own (the API's:
   stepping never moves a solve), so its state needs somewhere to be
   shown.
4. **The mockup's rules are kept** though the explorers no longer need
   them: Placements and Step need a prepared or paused solve (each opens
   an assembler of its own, prepared with the options shown), Movements a
   solution with its moves (the original's browser opened on any saved
   assembly). The original warned when the prepared assembler was
   neither at its start nor its end; a fresh assembler always is.
5. **The two dialogs are windows beside the workspace**, not modal: the
   original's debug statistics replaced the 3D view on a Debug tab shown
   when "Enable Debug Statistics" was set; here "Debug statistics…" is
   always there (no setting until stage 6's persistence). The original's
   status window opened from the shapes toolbar's "Details" button; here
   from a temporary top-bar button (stage 6's menus).
6. **The movement explorer opens without rotations** (the original's
   browser too): `MovementNode` carries no orientations, so a position
   reached by a turn could not be drawn.
7. "Analyze more" of the original is left out: it called the same
   analysis (its level was unused).

## What the mockup did differently, and why it changed
- Its drawer was "layout only": made-up rows (placement counts per
  piece, positions per level) and a Back / Step / Run / Reset row; here
  the engine's placements, steps and tree.
- Its Step advanced a counter that revealed the canned pieces one by
  one; here the assembler's own steps (pieces are placed and taken away
  as the search backtracks).
- Its explorer buttons lived in the left card only; the drawer's header
  switches views too (the mockup's segment control).

## Found in the original
- **The solve's wall time counts on after a run** (`solveStats_c::
  elapsedMs` is the time since the run's `statsOrigin`, read live, so a
  paused or finished solve's figure keeps growing until the next run).
  The original's panel hid it by freezing its text once a run stopped
  (`freezeLive`), but a Debug tab opened after a run showed the time
  since the run began, not its length. The core's `SolveSession::runMs`
  ports it as it is (`SolveStats.elapsedMs`); the panel freezes as the
  original's did. Worth fixing in both: stop the clock at a run's end.

## Known issues and notes
- **The player still runs under an explorer**: its controls act on the
  playback the explorer's layer hides; closing the drawer shows it again.
- **Placements reads every piece's list when it opens** (for the counts):
  a large puzzle with thousands of placements per piece and many pieces
  sends megabytes of JSON; `explore.openAssembly` (the matrix and the
  reduction) runs on the engine's one request thread and holds the calls
  behind it for as long as it takes.
- **The drawer covers the lower part of the view** (250 px, the mockup's);
  the scene is not framed above it.
- A direction seen end on cannot be dragged: turn the view.
- The stepped assembler's first complete set on Bermuda is another
  orientation of its one assembly than the solution stored in the file
  (the engine test checks the frame, not the orientation).
- From stage 4, still open: autosave needs a file (stage 6's Save As),
  the analysis reads every id first, a solution picked while a run thins
  the list may be gone, every piece hidden at a disassembly's end.
- From stage 3: the maximum holes cannot be set. From stage 1b: the one
  look beside the original GUI.

## Brief for the next stage

**Stage 6** (Opus 5.5, high), FRONTEND-PLAN.md's "Stage 6": menus,
files, settings, recovery; then the short audit of its save, recover and
close paths. Where things are:
- **The temporary top-bar buttons** (`shell/topbar.ts`, `index.html`):
  New… (grid chosen, `document.new`), Open… and drop (`document.openBytes`:
  no path, so no autosave, no Save), Status… (`entities/status-window.ts`'s
  `StatusWindow.open()`) all become menu items (Electron's menus, the
  main process; the renderer through the bridge). The status window is
  webui-spec.md section 7.1's "Status window"; the debug statistics stay
  in the Solver (`StatsPanel`), a setting for them is the stage's choice.
- **Save refused while solving** offers `Autosave.saveAtSavePoint()`
  (`solver/autosave.ts`): pause every run at a save point, save, carry
  on; the autosave itself needs a document with a path.
- **Settings** are in memory (`shell/prefs.ts`; undo depth and threads go
  to `settings.set`); the layouts per workspace in `shell/layout.ts`.
- **The F1 dialog** prints `KEY_MAP` by `keyGroups()` (`shell/keys.ts`),
  now with the Explorers group; the settings dialog's "Keyboard shortcuts"
  page prints it already (`openSettings('keys')` on F1 today).
- **Windows of their own** (`shell/panel.ts`, not modal) and the
  confirmation (`shell/confirm.ts`, focus returned) are there for the
  stage's dialogs (notes, about, close with changes).
- e2e helpers: `tests/e2e/app.ts` (`launch` with `callLog`, `engineCall`
  through the bridge, `openFixture`, `newDocument`), `tests/e2e/solver.ts`.

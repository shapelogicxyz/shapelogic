# Handoff: web stage 2 (the Entities workspace)

**Date:** 2026-10-08 · **Next:** stage 3, the Puzzle workspace.

## Done

### Before the stage's commit: one engine addition, one core fix
- **`Engine: grid.neighbour`** (2f1c2ac): `grid.neighbour {size, cell,
  face} -> {cell | null}`, the cell across a side, the sides numbered as a
  `MeshFrame`'s `voxelFace` (`Voxel::getNeighbor`, faces), null past the
  space's edge; a cell outside the space or not of the grid, or a side the
  cell lacks: `invalid_argument`. Schema, handler (a one-cell space of the
  grid answers: neither `getNeighbor` nor `validCoordinate` depends on the
  size), a contract call per grid, `gen_api_md.py`, `just web-gen`. An
  in-process test fills, for every side a lone cell's mesh draws on each of
  the five grids, the cell `grid.neighbour` names, and checks that exactly
  that side goes (a swapped numbering fails it: 10 failures).
- **`Core: every change inside an edit group gets its own revision`**
  (e57f106), found by this stage's drag batching: a command decided
  whether it changed anything by whether it saved a new undo part, and
  inside `edit.begin` / `edit.end` a shape's part is saved once, so the
  second and later batches of a drag changed the shape with no new
  revision, no `lastChange` and no `document.changed` (the grid showed the
  first batch only). The document now counts its parts' touches (commands
  touch only what they change), saved or already covered. Tests: the
  document's group case (a revision per batch, none for a batch that
  changes nothing; the old rule fails it) and an engine case (each
  batch's `document.changed`). A fault of stage 6's app layer, not of the
  original (which had no revisions).

### The Entities workspace (`web/src/renderer/entities/`)
Each file names the mockup's functions it came from.
- `workspace.ts` (`EntitiesWorkspace`): puts the workspace together; the
  workspace's keys (1–4, E, PgUp/PgDn; the list's; F2, Del, Ctrl+D; Esc
  clears the box); the status line ("Shape S1 has n voxels …", or "Shapes
  are frozen while a solve runs — pause the solve first to edit them");
  the frozen state: `Snapshot.solving` puts `#stage.frozen` (the cards'
  controls disabled) and every edit path asks `refuse()` first, which says
  "Pause the solve first" and sends nothing; `mutate` (a call awaited to
  its revision, its failure reported); `data-busy` (edits under way, for
  the e2e tests).
- `shapes-list.ts`: the mockup's rows, actions and menu; new (the grid's
  default size), duplicate (added last, selected), delete (never the last:
  the engine would allow it; the shape after takes the selection), move by
  Alt+arrows, the menu or a drag of a row (labels renumber; "Order changed
  — shapes renumbered S1…Sn"), rename in place (F2, Enter in the list, a
  double-click; Esc cancels; the engine's refusal shows as the flash
  "Rename: <its message>"), weight − and + (W badge), the shape's own
  colour (the menu's Colour… and Default colour: added, the mockup had
  none), the collapsed card's chips.
- `inspector.ts`: Transform, Fit & scale, Repair & surface from
  `shape.taskSet`, placed as the mockup draws each grid's set (the matrix
  of flip, ±90° and ±1 for the cube, rhombic and tetra-octa grids, whose
  tasks are the cube's; the sphere and prism grids' rotations and nudge
  pads); a task the layout does not place gets a plain button of its group
  (none today). Tooltips are the engine's labels (the original's).
  "Apply to all shapes" (one state with the right card's switch, as the
  original's single check box) sends `toAll` for the tasks that allow it.
  Flash messages: "Blocked: the piece would leave the grid" (a refused
  nudge), "Filled n interior voxels" / "No interior holes found", "n inner
  (outer) voxels updated", "Already at minimum resolution", "<label>: every
  shape".
- `editor-card.ts`: the tool strip, mirror and span, the planes (XZ and YZ
  on the cube grid only), the box button (the mockup's `selbox` icon) by
  the layer label, the layer strip with marks on the layers that hold
  cells, the selection bar (n cells selected, Fill, Clear, ×), the grid
  size steppers in `sizeUnit` blocks shown rounded up as the original's
  (a tetra-octa shape of 4 cells shows 2 blocks of 3), "Apply to all
  shapes" ("the others grow by as much (they never shrink)"), the palette
  (Default is the shape's own colour; + adds one of the mockup's colours
  and selects it; the menu edits it through the system's picker or removes
  it), the rail.
- `grid2d.ts` (`Grid2D`): one SVG renderer for every grid over a layer
  drawing (`spaceGridSvg`'s fit; the cube planes' rulers); the cells
  coloured as the engine has them (variable half filled and dashed); the
  cursor outlined by its cells' edges towards cells not under it (the
  original's grid editors), the cursor being the hovered cell with its
  mirror and span cells of this layer; the box selection; the stroke's
  cells tinted until the engine answers; a drag collects every cell the
  pointer passes over (its path sampled between two moves); the right
  button erases; a drag ends on release or when the window loses focus.
- `stroke.ts` (`Stroke`): a drag as ONE undo step: `edit.begin` (labelled
  "Draw fixed voxels", "Draw variable voxels", "Erase voxels", "Paint
  voxels"), the cells entered sent once a frame, one `shape.setCells` at
  the engine at a time (the next batch waits for the one before), the
  rest and `edit.end` after the last; a group that opened is closed
  whatever failed inside it.
- `layer-geometry.ts`: `grid.geometry` kept by grid, size and layer; the
  cube's XZ and YZ planes as the client's squares with the same structure.
- `shape-cells.ts` (`ShapeCells`): the shown shape's cells, the XY layer
  from `shape.get {layer}` and the whole shape from `shape.get` (the layer
  marks, the XZ and YZ planes, the 3D preview), coalesced, read again when
  a change named the shape and the snapshot after it still has it.
- `edit3d.ts` (`Edit3D`): 3D editing (E, or a modifier held): erase and
  paint on the face's cell, fixed and variable on the cell across it
  (`tools.ts`'s table on cubes, `grid.neighbour` on the other grids,
  cached), with mirror and span, one `shape.setCells` (one step); the
  preview: boxes on cubes (an empty target in the drawing colour, a cell
  that would change faint, an erased one in the danger colour), a cell's
  own faces on the other grids, and the face a cell would grow from (the
  drawing colour's contrast) since an empty cell's shape is not the
  client's to know; a refused target shows its face in the danger colour;
  the tool cursors; the Edit button's tool (a quick tool dashed).
- `tools.ts`: which tool a press uses (`eff2d`, `actFor`, Cmd as Alt), the
  cube's faces. `editor-state.ts`: `EditorState`, `planeMap`, `expand`
  (mirror and span's cells as the core's `setRecursive` makes them, for
  the cursor and the preview: an engine test checks it against
  `shape.setCells` on all five grids). `glyphs.ts`: the mockup's `G`.

### The 3D view, the shell, the state, the main process
- `view3d/view.ts`: an edit host (`View3D.edit`: the face under the mouse
  and a click that was no drag, with the modifiers; the hover is told
  again when a modifier key changes or the mesh is rebuilt), the editor's
  plane and layer (`setEditLayer`: the slab and the dimming follow), the
  preview, the cursor; `#vp[data-revision]`, `#vp[data-preview]`.
  `view3d/preview.ts` (the ghosts, drawn on top), `toolbar.ts` (the Edit
  button live), `scene.ts` (the preview group).
- `shell/keys.ts`: the Entities rows have their chords (1–4, E, PgUp/PgDn,
  ↑/↓ and Alt+↑/↓ in the list, F2 and Enter, Del as the action `delete`,
  Ctrl+D) and a new row, Ctrl+Z / Shift+Ctrl+Z (undo, redo). 30 chords.
  `keyboard.ts`: several handlers per action, tried in turn (a false return
  passes the press on), each given the event. `history.ts`: undo and
  redo, the status bar naming the step ("Undone: Draw fixed voxels"),
  refused while solving without a call, a drag just released waited for.
  `cursors.ts`: the tools' mouse cursors (data URLs: the CSP now has
  `img-src 'self' data:`). `topbar.ts`: a temporary **New…** (the grids
  this version solves; `document.new`) beside the temporary Open…, until
  stage 6's File menu. `app.ts`: `#stage[data-generation]`,
  `[data-revision]`.
- `state/store.ts`: snapshot reloads coalesced (key `document.get`);
  `whenRevision`; `newDocument`; `solving`; `superseded` is no error.
- `styles.css`: the Puzzle library chips' rules scoped to `.pz-lib .lc`:
  the stage's own "left collapsed" class is `lc` too, and on the
  full-window stage (1b) the chips' `height:28px` and border took the
  whole stage when the left card collapsed (a blank window; stage 1b's
  tests checked the classes only).
- `main/index.ts`, `engine-ipc.ts`: with `BT_CALL_LOG=<file>` each call of
  the renderer is logged with its id and errc.

### Tests
- Unit (`tests/unit/renderer/entities.test.ts`, 10; `keys.test.ts` +1): a
  stroke's protocol (begin first, a batch a frame, one at the engine, the
  end after the last, the group closed after a failure, nothing after a
  refused begin), the planes' maps, the cube planes' squares, the quick
  tools, mirror and span, the cube's faces, the store's revisions and new
  document; the new chords.
- Engine (`tests/engine/editor.test.ts`, 3): the preview's mirror and span
  cells equal what `shape.setCells` changes on all five grids (two sizes
  each, six combinations, a seventh of the cells); the cube's face table
  equals `grid.neighbour`; a stroke over several frames is one undo step
  with one `document.changed` per batch.
- e2e (9 new, 30 in all): `entities.e2e.ts`, the editor on each of the five
  grids: a new puzzle and shape (the default size), every tool by key and
  by button, Shift / Alt / Ctrl clicks and the right button, the status
  bar's cell, a drag over N cells as one `edit.history` entry named "Draw
  fixed voxels", Ctrl+Z and Shift+Ctrl+Z giving `shape.get`'s cells back
  ("Undone: …", "Redone: …"), a right-button drag erasing, mirror and span
  (the UI's click equals the same `shape.setCells` through the bridge on a
  copy), the box selection's Fill and Clear (equal to `shape.setBox` on a
  copy) and Esc, PgUp/PgDn and the layer marks, the size steppers with and
  without "Apply to all shapes" (grow by as much, never shrink), every
  task of `shape.taskSet` (each equal to the engine's on a copy; the
  blocked nudge's message), the palette (add, paint, a new cell's colour,
  Default, edit, remove), 3D editing (Shift-click on cubes, edit mode on
  the others, the cell across the face the status bar names equal to
  `grid.neighbour`'s, the preview shown first; Ctrl-click erasing), the
  status bar's counts, and the call log. `shapes.e2e.ts`: the list (the
  last shape not deleted, new, Ctrl+D, ↑/↓, Alt+↑/↓, the menu's move, a
  dragged row, F2 / Enter / double-click rename, Esc cancelling, a refused
  name's message, weight, colour, Del and the menu's delete, the rail).
  `frozen.e2e.ts`: a solve of a 2x4x6 box of dominoes started through the
  bridge: frozen, the status, keys and clicks refused with no edit sent,
  looking still allowed, editing again after the abort. `rebuild.e2e.ts`:
  every shape of Bermuda (23) and Prisgon (12, with names, weights and
  variable cells) rebuilt through New, the steppers, F2, the weight
  buttons, the layer chips and drags along runs of edge-adjacent cells,
  equal to the fixture's `shape.get`.
- The e2e helpers: `app.ts` (`launch({callLog})`, `readCallLog`, which
  leaves the test's own calls out, `engineCall` through the page's bridge
  with ids from 1e9 that the page's proxy ignores, `newDocument`),
  `editor.ts` (a point inside a cell's polygon or circle, clicks, drags,
  `settled`: `data-busy` 0 and the page at the engine's revision).

## Gate results
| Check | Result |
|---|---|
| `just web-check` | clean, three times (three TypeScript projects, ESLint without warnings, Prettier) |
| `just web-test` (release engine) | **164/164**, three times, ~14 s each: 18 files (stage 1b's 150, `unit/renderer/entities.test.ts` 10, `keys.test.ts` +1, `engine/editor.test.ts` 3) |
| `just web-e2e` | **30/30**, three times, 3.4 min each (build included): stage 1b's 21 (the 36 reference screenshots re-recorded first, below), `entities` 5 (~8.5 s per grid), `shapes` 1, `frozen` 1, `rebuild` 2 (Bermuda 18 s, Prisgon 38 s) |
| perf (each e2e run) | mesh build 5.9, 6.2, 5.5 ms; orbit median 13.3/13.3/13.4 ms at a 13.4 ms display frame, p95 14.5/14.6/15.2 ms; 148–152 renders in 152 frames |
| repeats while building | the shapes test 8 times after its flake was fixed; `entities`, `frozen`, `rebuild` 3 times more (24/24) |
| `Engine: grid.neighbour` | the in-process test on five grids, its mutation (10 failures), the contract call per grid; `just test-all` (6/6, 327 s), the contract suite on the ASan engine |
| `Core: …revision` | the document's group test and its mutation (4 failures), the engine's case; `just test-all` again (6/6, 329 s), `just test-asan` (6/6: unit 166 s, slow 961 s, contract, cli), the contract suite on the ASan engine. TSan not run: the change is the document's, which one thread owns. No oracle tier: `bt` does not use the app layer |
| `just web-gen`, staleness | current (the gen-api test passes; `METHOD_NAMES` equals `engine.info`'s 95 methods) |

## The reference screenshots
The 36 of stage 1b still passed within their tolerance, but they show the
viewport with its toolbar and hint, which changed: the Edit button is
live, and the hint has the mockup's modifier line again ("Shift click 1
fixed · Alt/⌘ click 2 variable · Ctrl click 3 erase"). Re-recorded
(`--update-snapshots=all`) after comparing old and new: those two
differences only, the shapes drawn alike. Stage 2 adds none: the editor
is checked against the engine, not against pictures; this session's own
screenshots of every grid, the box selection, the 3D preview and the
menus were looked at.

## Mirror and span: what the core does per grid
The core applies them on every grid as the original's grid editor did
(`setRecursive`): a mirror maps a coordinate to size − 1 − it, a span
fills its row, and cells the grid does not have are left out. Probed per
grid (and now an engine test): bricks and prisms mirror and span always;
on the sphere grid a mirror across an even size lands on cells of the
other parity, which the grid lacks, so it changes the clicked cell only
(odd sizes mirror), and a span fills every other cell of its row; on the
tetra-octa grid a mirror across 3 blocks maps each cell to one the grid
lacks (4 blocks mirror); rhombic mirrors at block sizes. So no grid
ignores them, and they are shown on every grid (the mockup disabled them
on the non-cube grids as unconfirmed, OQ-19); the cursor and the preview
show exactly the cells a click changes.

## Where this stage departs from the plan, and why
1. **A core fix** (above) in a commit of its own before the stage's: the
   drag batching the plan asks for cannot be seen by a client without it.
2. **Cells from `shape.get {layer}` and from the whole shape**: the drawn
   XY layer is `{layer}`'s, as the plan says; the whole shape is read too,
   for the layer marks, the XZ and YZ planes and the 3D preview.
3. **The call log's refusals**: the plan's gate says the scenario's log
   holds no `invalid_state` or `invalid_argument`. The engine refuses a
   nudge out of the grid as `invalid_argument` by design, and the gate
   also asks for that nudge's message, so the editor test requires that
   every refusal is one of the blocked nudges the test clicked (counted:
   one each) and nothing else; the shapes test's one refusal is the
   deliberate name with a control character. The UI never sends
   `invalid_state`: a frozen workspace refuses before calling.
4. **3D additions outside the space are refused** (the mockup's
   behaviour), where the original grew the space (decision 0013's list).
5. **The box selection is a mode** (a button), since the mockup's bar had
   no gesture to select with (decision 0013's list).
6. **A temporary New…** in the top bar (the gate's "new document" on each
   grid; stage 6's File menu replaces it, as the temporary Open…).
7. **Test hooks**: `BT_CALL_LOG` in the main process, `data-busy`,
   `data-revision`, `data-generation`, `data-frozen`, `data-preview`, and
   test calls through the page's bridge, in the manner of stage 1b's
   `data-*` attributes.

## What the mockup did differently, and why it changed
- Its cube editor was a CSS grid of divs and its other grids mock
  geometry with "Mock: not interactive" operations: one SVG renderer over
  the engine's layer drawings, everything live on every grid.
- Its rhombic and tetra-octa transform sets were placeholders (OQ-19): the
  engine's sets (the cube's tasks); every set and label is the engine's.
- Mirror and span disabled on the non-cube grids: enabled (above).
- "Minimize ÷n" and Double/Triple disabled past 64 were client
  computations: the engine decides (static text, its message on refusal).
  The size steppers clamped to 1–32 in the client: the engine's limits.
- The selection bar had no way to select: the box button.
- 3D ghosts of the non-cube grids drew the new cell from mock geometry:
  the face it grows from.
- A colour was edited live on every input event: on the picker's change
  (one undo step).
- The list focused itself a moment after a press (`setTimeout`): dropped,
  since a late focus took the focus from a rename begun meanwhile (a flaky
  e2e, 3 runs in 8, until it went); the list is focusable, a press focuses
  it.
- "New shape (Ctrl+N)": the title keeps no key the map does not have.
- Not in the mockup: undo and redo, the shape's own colour, the frozen
  state, the temporary New….
- The plain-DOM pattern (no UI framework) held for this card-heavy stage;
  what needed care was re-rendering under an element with the focus (the
  rename field is skipped while open) and inputs replaced by a re-render
  (the colour pickers' change handlers are delegated).

## Known issues and notes
- The colour menu's "Edit colour…" opens the system's picker, which a test
  cannot drive: the e2e sets the input's value instead.
- On the sphere grid the core's mesh gives a ball's connection and filling
  faces the number of the cap drawn before them (faithful to the
  original's `sphere_mesh`): they are no side for 3D additions (refused),
  and the face preview leaves them out.
- The engine lets the last shape be deleted; the UI refuses it.
- A drag's cells repaint every cell's attributes of the layer per batch;
  fine to 64 by 64, not measured beyond.
- The prism grid's mirror is the core's coordinate mirror, as the
  original's; whether it is a geometric mirror for every size was not
  examined.
- Undo while a solve runs says "Pause the solve first" (the engine would
  refuse it); undo and redo have no buttons until stage 6's Edit menu.
- From stage 1b, still open: the one look beside the original GUI.

## Brief for the next stage

**Stage 3** (Opus 5.5, medium), FRONTEND-PLAN.md's "Stage 3". Where
things are:
- The Puzzle cards (`#leftP`, `#rightP` in `index.html`) are placeholders;
  the mockup's markup is at lines 767–778 and 834–843 of
  `docs/mockup/index.html`, its script from `renderPuzzle` (line 1853).
  The library chips must sit inside `.pz-lib` (their rules are scoped
  there, see above).
- Follow `entities/` as the pattern: a workspace class that renders cards
  from the store's snapshot and the engine's replies, `mutate` (a call
  awaited to its revision), `refuse()` while frozen, `data-busy` for the
  tests (move the busy counter and `mutate` to a shared place rather than
  copying them). Decision 0011: during a solve the solving problems and
  the lists are frozen, others may not be: read `ProblemSummary.solve`.
- Keys: the Puzzle rows (+, −, Del) need chords; Del is the action
  `delete` (the Entities handler returns false outside Entities): register
  a Puzzle handler with `onKey('delete', …)`; one press, one action
  (`keys.test.ts` counts the chords).
- The view: `View3D` draws `store.shape` in one `ShapeScene`; the pieces
  scene is new (`shape.mesh` per shape, instanced per copy, the optional
  copies faded, a click selects); `View3D.edit` is the Entities' 3D edit
  host and checks the workspace itself.
- e2e: `launch({callLog})`, `engineCall`, `newDocument`, `openFixture`,
  `readCallLog` (`tests/e2e/app.ts`), `settled` (`tests/e2e/editor.ts`).

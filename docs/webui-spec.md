# Web UI specification

What the future web front end does, screen by screen, mapped to the original
BurrTools features, with the gaps the design still has to fill. The engine
API ([core-api.md](core-api.md)) is derived from this document: every action
here must map to an API call.

**Source:** Girish Sharma's mockup (`index.html` on the `gh-pages` branch
of `grssam/burr-tools`, commit `d7afb3ed`, 2026-10-04, GPL-3.0), vendored
unmodified as [mockup/index.html](mockup/index.html); its Pages site,
https://grssam.github.io/burr-tools/, answered 404 on 2026-10-08. The mockup
is a single HTML file with mock data and hand-projected canvas drawing. Its
layout, workflows, wording and keyboard map are adopted, and so is its code
(decision 0013, superseding "its code is not" of this paragraph's first
version): its CSS, its shell and its card render functions become
TypeScript modules over the engine's data, section by section
([FRONTEND-PLAN.md](FRONTEND-PLAN.md), "The mockup as the basis"); its data
model, voxel operations, canvas renderer, fake solver and player are
replaced by the engine and three.js. The mockup cites a separate spec (item
ids such as "OQ-19", "C14") that is not published; where it is cited, the
original GUI decides (section 7).

## 1. Shell

- **Top bar:** brand, file name (full path on hover), Settings button
  (`Ctrl+,`). The mockup leaves the **menu bar** out of scope; its
  placeholder items are File, Toggle 3D, Export, Status, Edit comment, Help,
  About. (Section 7 lists what the menus must offer.)
- **Workspace rail:** Entities (`Ctrl+1`), Puzzle (`Ctrl+2`), Solver (`Ctrl+3`).
- **Body:** each workspace has a left card, the **3D view** in the centre and
  a right card. Cards collapse to narrow rails (`Ctrl+[`, `Ctrl+]`); the 3D
  view can take focus (`Ctrl+Space`). Layout is remembered per workspace.
- **Status bar:** messages on the left (a transient "flash" after actions,
  e.g. "Filled 3 interior voxels"), cursor position on the right.
- **Density:** Standard, or Minimal (icons without captions, helper text in
  tooltips, auto-hiding viewport toolbar, narrower cards). Every function is
  available in both.
- **Theme:** Light, Dark, System.

### Settings dialog
Searchable, three pages, "Restore all defaults", changes apply immediately.
| Page | Setting | Values (default) |
|---|---|---|
| General | Interface density | Standard / Minimal (Standard) |
| | Theme | Light / Dark / System (System) |
| | Undo history depth | 25, 50, 100, 200, 500 (25) |
| | Show tooltips | on |
| 3D view | Show view cube | on |
| | Reverse scroll zoom direction | off |
| | Rotation method | Drag / Arc-ball (Drag) |
| | Lighting | on |
| | Fade out removed pieces | on |
| Performance | Worker threads | 1-32 (machine dependent) |

The mockup's "Use OpenGL display lists" setting is dropped (no OpenGL).

## 2. 3D view (all workspaces)

- Toolbar: **Orbit** / **Pan** modes (`O` / `P`), **Edit** (Entities only,
  `E`), **Display** options, **Focus**.
- Navigation: left-drag orbit (or pan in Pan mode), Shift+left-drag the other,
  middle-drag pan, wheel zoom, `Home` home view, `F` fit keeping orientation.
- **View cube** in a corner: click a face, edge or corner to turn to it;
  double-click a face for its upright view.
- Display options: axes, bounds, current layer, dim other layers, x-ray
  (show through solid pieces), perspective.
- Perspective or orthographic projection.

## 3. Entities workspace: shapes and voxels

### Left card: object scope ("which shape, and what happens to all of it")
- **Shapes list**: id chip (S1, S2…, renumbered by order), optional label,
  **weight** (W−/W+; in the solver heavier shapes stay in place, lighter ones
  move), rename (F2, Enter, double-click), duplicate (`Ctrl+D`, added last),
  delete (`Del`, not the last shape), reorder by drag or `Alt+↑/↓` (ids
  follow the order), right-click menu with the same actions.
- **Transform** (Flip · Rotate · Nudge), per axis X/Y/Z: flip (mirror across
  the plane), rotate −90°/+90°, nudge −1/+1. Blocked with a message when the
  piece would leave the grid. Non-cube grids have their own transform sets:
  spheres rotate ±90° per axis with layered nudge pads; triangular prisms
  flip, rotate 180° about X/Y, ±60° about Z, nudge in plane and ±Z. (The
  mockup marks rhombic and tetra-octa sets as unconfirmed, OQ-19: take them
  from the original's grid editors.)
- **Fit & scale**: Prune grid to fit piece; Center on the grid; To origin
  (corner at 0,0,0); Minimize (largest resolution divisor that keeps the
  shape); Double ×2 / Triple ×3 (grid limit applies). Spheres: no scaling.
- **Repair & surface**: Fill interior holes (empty cells not reachable from
  outside become fixed; message with the count); Surface voxels, inner or
  outer: make fixed, make variable, clear colour.

### Right card: voxel scope ("tool, plane, layer, grid size and colour together")
- **Tools:** Fixed (`1`), Variable (`2`), Erase (`3`), Paint colour (`4`).
  Quick tools with modifiers in both 2D and 3D: Shift+click fixed,
  Alt/⌘+click variable, Ctrl+click erase. Right-click/right-drag erases in 2D.
- **Mirror** X/Y/Z (also place the mirrored cell) and **Span** X/Y/Z (fill
  the whole grid along that axis on each click). Cube grid only in the
  mockup; non-cube grids to be decided.
- **Plane** XY / XZ / YZ (non-cube grids: XY only in the mockup), **layer
  strip** with layers that contain cells marked, `PgUp`/`PgDn`.
- **2D grid**: click or drag applies the tool to every cell entered, **one
  undo step per drag**; box selection with Fill / Clear / deselect (`Esc`).
  Non-cube grids draw their real cell shapes (triangles, circles, tetrahedra
  bases).
- **Grid size** steppers X/Y/Z with **Apply to all shapes**; shrinking removes
  cells outside.
- **Colour palette**: Default (the piece's own colour) plus constraint
  colours; add, edit (colour picker), remove (cells using it revert to
  Default). The current drawing colour shows as a large swatch.
- **3D edit mode** (`E`): plain clicks in the 3D view apply the active tool;
  the cursor shows the tool.

## 4. Puzzle workspace: problems

### Left card
- **Problems list**: P1, P2… with optional names; new, duplicate, delete
  (not the last), rename, reorder by drag.
- **Result**: the result shape, its cell count, "can hold n–m", **Change…**.
- **In this puzzle**: the pieces used, as **chips** or a **table** (shape,
  count, range on/off, group). Chips show the count; −/+ buttons.
- **All pieces** library: drag a chip in, double-click, or `+` to add one;
  drag a tile out to remove. "More" menu: add one of each, **set range for
  all pieces…** (one min/max for every piece), remove all.
- **Fit summary**: "Pieces can fill the result" / "too large" / "cannot
  reach", with result cells, piece count range, piece cell range, holes.
  The status bar repeats the counts.

### 3D view
The result, the selected piece, and every copy of every piece in a grid;
optional copies (above the minimum count) drawn faded; click to select.
Rotation turns each piece about its own centre.

### Right card
- **Selected piece**: cells (fixed/variable), **copies in the puzzle** (one
  count) or **count range** (min–max) with a switch between the two,
  **exclusive group** (None, G1, G2…, + New).
- **Exclusive groups** list, with members; delete a group.
- **Colour rules** matrix: rows = piece colour, columns = result colour; a
  ticked cell lets a piece cell of that colour sit on a result cell of that
  colour. Buttons: each colour on itself, allow all, clear.

## 5. Solver workspace

### Left card
- **Problem** picker.
- **What to check**: assemblies only, or assemblies and disassembly.
- **Search options**: Just count; Keep mirror solutions; Keep rotated
  solutions; Thorough rotation check; Drop below level (solutions with a
  lower level are dropped); Keep at most this many solutions; Don't keep the
  disassemblies; Sort found solutions by.
- **Run**: Solve problem (from the start), **Prepare** (build the search
  structures without solving), **Continue** (resume a stopped search),
  **Pause**; progress bar with percentage; activity, assemblies, solutions,
  time used.
- **Step-by-step explorer** (advanced): **Placements** browses the assembly
  search tree, **Movements** browses the disassembly search, **Step**
  advances the assembler one step.

### Right card
- **Solutions** as a **List** (sortable columns: original search order,
  level / moves, pieces used…) or a **Slider** (one at a time).
- **Disassembly player**: play/pause, one move back/forward, jump to
  assembled / fully apart, seekbar; status lines ("Disassembly stored",
  "Level only — moves not stored", "Cannot be disassembled", "Disassembly not
  checked").
- **Actions**: Analyse disassembly (for solutions stored without one),
  Export to STL, **Remove assemblies**: this solution only, all before this
  one, all after this one, all without a disassembly, all (each asks first).
- **Piece display**: chips per piece cycling solid → wireframe → hidden,
  Show all; "show through solid pieces" for wireframes.

## 6. Keyboard and mouse map (adopted from the mockup verbatim)

| Where | Keys | Action |
|---|---|---|
| Everywhere | Ctrl+1 / 2 / 3 | Entities / Puzzle / Solver |
| | F1 | keyboard shortcuts |
| | Ctrl+N / Ctrl+O, Ctrl+S / Shift+Ctrl+S, Ctrl+W | new, open; save, save as; close the window (the File menu, stage 6) |
| | Ctrl+, | Settings |
| | Ctrl+[ / Ctrl+] | collapse or expand left / right card |
| | Ctrl+Space | focus the 3D view (toggle) |
| | Esc | step back: close dialog → close menu → cancel rename → pause playback → exit focus |
| | Tab / Shift+Tab, Enter / Space | keyboard focus and activation |
| 3D view | left-drag, Shift+left-drag, middle-drag, wheel | orbit/pan, the other, pan, zoom |
| | O / P, Home, F | orbit / pan mode, home view, fit |
| | click / double-click view cube | turn to view / upright face view |
| Entities | 1 / 2 / 3 / 4 | tool fixed / variable / erase / paint |
| | E | 3D edit mode |
| | Shift / Alt(⌘) / Ctrl + click | quick fixed / variable / erase (2D and 3D) |
| | right-click or right-drag (2D) | erase |
| | drag across cells | apply to every cell entered, one undo step |
| | PgUp / PgDn | next / previous layer |
| Shapes list | ↑ / ↓, Alt+↑ / Alt+↓ | select, move earlier / later |
| | F2 / Enter / double-click, Del, Ctrl+D | rename, delete, duplicate |
| Puzzle | + / −, Del | one more / fewer copy, remove piece |
| | double-click or drag chip | add one copy / add or remove |
| | right-click tile or chip | one more, one fewer, remove, set as result |
| Solver | Space / Enter | play / pause the disassembly |
| | ← / →, Shift+← / Shift+→ | one move, jump to assembled / apart |
| | ↑ / ↓, Home / End, PgUp / PgDn | previous / next, first / last, 8 back / forward |
| | Del | remove the selected solution (asks) |
| | double-click row, click header | select and play, sort (again to reverse) |
| Dialogs | Esc, Enter, Space | close, commit, toggle |

## 7. Gaps: needed but not in the mockup

1. **Menus and app flows:** New (choose the grid type once, it cannot change
   later), Open, Save, Save As, recent files; Import PuzzleSolver3D and
   Puzzlecad; Export (STL, image, vector image, GLB solution animation,
   Puzzlecad, paused solver state); Convert grid type; Convert assemblies to
   pieces; puzzle notes/comment; Status window (per-shape table: cells,
   identical shapes, symmetry, connectedness); tutorial, user guide, about.
2. **This fork's solver options:** solver type (Classic, Crowell, BT2),
   Check Rotations (rotation take-apart), Just Levels, Strict Color
   Restrictions, Deep Symmetry Check, Enable High Memory, Autosave every
   20 minutes, **Abort** (hard stop that clears the results, separate from
   Pause), paused-solve import/export, debug statistics panel.
3. **Sliding and Stacking/Panex puzzles**: sliding in section 8 (deferred
   part 2); stacking in section 9 (deferred part 4): rods, disc sizes, start/goal
   trays, Panex columns, pocket column.
4. **Display modes:** colour-constraint colours exist (`colorView`), anaglyph
   and the original's three render styles (bevelled voxels, flat with edges,
   STL-like) do not. Decide which to keep.
5. **Non-cube grids:** transform sets for rhombic and tetra-octa, mirror and
   span tools, XZ/YZ planes (OQ-19 in the mockup's spec).

## 8. Sliding puzzles (deferred part 2)

The mockup has no sliding screens; the original GUI decides, fitted into
the three workspaces. Its sliding branches (inventoried 2026-10-10):
`mainwindow.cpp` (`updateInterface`, `updateEntitiesTab`, the
`RS_SLIDING_CLICK` callback, `cb_CopyShape`, `cb_NameShape`,
`cb_ShapeColor`, the load's flattening message, the Activity line, the
solver note's message box, Solve's tooltip), `mainwindow_rods.cpp`
(`applySlidingGridMode`, `cb_NewStartGoal`, `cb_DelStartGoal`,
`refreshSlideValid`), `mainwindow_layout.cpp` (the start/goal row, the
sliding check boxes), `mainwindow_solve.cpp` (`cb_BtnCont`'s checks and
flags, `updateSolverOptionCheckboxes`), `mainwindow_help.cpp` (the three
solvers' help), `tooltabs.cpp` (`ToolTab_0`: the Start/Goal tab, its
piece list and preview, `lockZ`), `grideditor.cpp` (the tray's tiles,
the marks in the pieces' colours, `drawStampPreview`), `BlockList.cpp`
(the list's text and swatch), `slidingcolors.h`, `voxelframe.cpp`
(`showSingleShape`'s two trays, `buildSlidingWalls`,
`drawSlidingWalls`) and `gridtypegui.cpp` (Sliding second in New's
list). `ToolTab_Sliding` ("Tray edit", "Walls", Start, Goal) is never
made by the original (dead code); its labels name the right card's mode
switch below.

**What a sliding puzzle is** (decision 0014): pieces are shapes of one
layer; a **start/goal shape** (a tray) is a shape of one layer whose
fixed cells are floor, variable cells corridors (pieces cross them but do
not stop on them) and empty cells walls; each piece may have a start and
a goal stamped on a tray, the whole piece at a time; each tray has its
own problem, "START/GOAL n", whose pieces follow the stamps.

### 8.1 Entities workspace
- **Shapes list:** a tray reads "S1 (START/GOAL 1)" and, after a dash, the
  user's name (`BlockList.cpp`); its chip is black (`slidingColors::LIST`).
  Above the list, on sliding puzzles only, a row of two buttons:
  **New Start/Goal Positions** ("Add a shape that holds start and goal
  positions": `sliding.addStartGoal`, the size of the selected shape's
  X and Y, else 6 by 6, as `cb_NewStartGoal`) and **Delete Start/Goal
  Positions** ("Delete the selected start/goal shape"; only on a tray).
  The list's Delete (and `Del`) is greyed on a tray, as the original's
  Delete Shape: the tray goes with its own button. A name beginning
  "sg:" is refused (the engine).
- **Inspector:** the shape's colour button is off on a tray (it shows the
  pieces' colours). The tasks that would give a shape more layers are
  not offered (`lockZ`: nudge along Z, rotate about X and Y, double,
  triple, grid scale; the engine's task set leaves them out).
- **Voxel editor (right card):** the Z size stepper is off (one layer);
  the plane is XY only. The **Paint colour** tool and the **colour
  palette** are hidden (a sliding puzzle has no constraint colours: a
  cell's colour is a start mark, and the colour group was hidden in the
  original). On a tray, a segmented **Tray edit** switch above the tool
  strip: **Walls** ("Toggle floor and wall cells on the tray": the
  ordinary tools, Fixed = floor, Variable = corridor, Erase = wall; the
  tiles drawn dark, `EDIT_LIGHT`/`EDIT_DARK`), **Start** ("Place or
  remove a piece's starting position on the tray"), **Goal** ("Place or
  clear a piece's goal position on the tray"). Start and Goal are the
  original's Start/Goal tab:
  - the tool strip, mirror, span and the box selection are hidden; a
    **piece list** (every shape but the trays, "S2 - name") with the
    chosen piece drawn flat beside it (`slidePiecePreview_c`);
  - the grid shows each marked cell in its piece's colour (the
    chequer's light and dark variants), the start marks in Start, the
    goal marks in Goal;
  - the chosen piece follows the mouse with its top-left cell under it,
    outlined green where it fits, red where it does not, orange where it
    already is (`drawStampPreview`; `sliding.preview`);
  - a left click places it (or takes it off when it is exactly there),
    a right click takes off the piece under the mouse
    (`sliding.place`, `sliding.clearAt`); a refusal's reason goes to the
    status bar ("The piece does not fit there: …").
  On a piece (not a tray) of a sliding puzzle the switch is not shown.
- **Validity bar** (under the 3D view, a tray selected,
  `refreshSlideValid`): green "Valid Start/Goal positions for pieces in
  Start and Goal of this shape" or red "Invalid Start/Goal positions -
  <reason>" (`sliding.tray.error`); the Start and Goal buttons of the
  switch take the same green or red (the tab's colour).
- **3D view of a tray** (`showSingleShape`): two trays side by side,
  **START** and **GOAL** (dark grey, `VIEW_GREY` 0.12, a gap of 4 units),
  each piece placed on a tray standing one layer above it in its own
  colour. The START tray is the editable shape (3D edit works on it).

### 8.2 Puzzle workspace
- **Problems:** New, Copy and Delete problem are hidden (the engine
  refuses them: a tray's problem follows its stamps); rename and order
  stay.
- **Result:** the tray, fixed: **Change…** is hidden.
- **Pieces:** a piece a stamp requires (`Part.required`) keeps its count:
  its −/+ and remove are off, with the reason in their tooltip; "Remove
  all" and "Set range for all pieces" leave it (the engine does).
- **Exclusive groups** and **Colour rules** are hidden (the original hid
  the colour groups and greyed Groups).
- The 3D scene draws the tray in its dark grey.

### 8.3 Solver workspace
- **What to check:** "Assemblies" (the start layouts) or **"Find
  Solutions"** ("Keep a placement only when each marked piece can slide
  from its start cell to its goal cell"), checked by default on a sliding
  puzzle (`slidingDisasmDefaulted`); Just Levels and Check Rotations are
  not shown.
- **Search options:** **Solver type** lists the engine's
  `slidingSolvers` ("Sliding Fast Solver (250k depth)", "… Deep … (1mil
  depth)", "… Full … (full depth)", Full the default) with the help of
  `mainwindow_help.cpp`; **Allow Nested Slides**, **Allow Partially
  Nested** (only with nested slides) and **Enable High Memory**, with the
  original's tooltips; Just count, Keep every n-th, the limit and
  Autosave stay. Keep mirror, Keep rotated, the thorough rotation check,
  Strict colours and the sort are not shown (a sliding solve keeps every
  start and matches colours strictly; a path found goes last). They are
  stored with the problem under the file's names (`nestedSlides`,
  `partialNested`, `highMemory`, `solverType` fast/deep/full).
- **Run:** Solve and Prepare are off while the tray's starts and goals
  are invalid, with "Fix the start and goal on the Entities tab first:
  <reason>" as their tooltip (and the engine refuses with the original's
  message, shown as the failure). The Activity line during the search is
  the original's "Sliding search, N moves deep. Scanned N arrangements."
  (or without the depth); at the end "Finished." (the original added
  "Scanned N arrangements. Searched N moves deep." from the problem's
  search statistics, which no reply carries yet). The solver's **note** (`solve.status.note`:
  "no solution, proven", or the limit that stopped it) comes in a
  message box when the run ends, as `bt_message`.
- **Player:** the moves follow their routes (a move round a corner turns
  it); the **walls** are drawn around the tray's floor
  (`buildSlidingWalls`: black lines, 0.08 outside the floor's edge, at
  the floor's bottom and top, posts where the outline turns), in the
  player (the original's `showAssembly`; the explorers draw none).

### 8.4 Notices and New
- A change's `document.changed.notice` and an opened file's
  `Snapshot.notice` come in a message box, as the original's `bt_message`
  ("Sliding shapes are one layer deep. …", "S3 changed shape: its start
  and goal were removed from S1.", "Only the first 63 shapes …").
- **File ▸ New** offers **Sliding** second, after Brick (the original's
  order); a new sliding puzzle starts empty (add pieces, then New
  Start/Goal Positions).

## 9. Stacking and Panex puzzles (deferred part 4)

The mockup has no stacking screens; the original GUI decides, fitted into
the three workspaces. Its stacking branches (inventoried 2026-10-11):
`mainwindow_rods.cpp` (the rod sets: `cb_NewRod`, `cb_DeleteRod`,
`cb_CopyRod`, `cb_NameRod`, `cb_RodExchange`, `cb_RodField` with
`syncPanexFields` and `loadRodFields`, `showSelectedRods`,
`cb_ProblemRod`, `cb_StackMode`, `cb_StackRod`, `refreshStackSlider`,
`refreshStackList`, `refreshStackValid`, `placeOneDisk`,
`syncSolverTypeMenu`, `syncStackingChrome`), `mainwindow_layout.cpp`
(the Rods panel with its fields and tooltips, the disc list, the
Problems tab's rod list, the rod slider, Start/Goal, Stack Order, the
validity strip), `rodbars.h` (`StackValidBar_c`: "Valid" or "Invalid -
reason" on green or red; `RodIndexBar_c`: "Rod: N", the pocket "Rod: P"
first), `BlockList.cpp` (`DiskSelector`: a row per disc, "S1 - name",
"size:", the field, + and −, 1 to 100 and its message; `RodSelector`:
"R1 - name" on 70,110,150; `RodStackList`: top of the rod first),
`resultviewer.cpp` ("Result: R1 - name", "No Start/Goal Rods"),
`mainwindow.cpp` (`cb_NewShape`'s disc, the Problems tab's stacking
branches of Set Result, +1, −1, All +1, Clear All and the arrows,
`activateProblem`'s `showStacking`, the solution slider in transfers,
the Activity lines of the Panex and stacking searches, Solve's and
Continue's tooltips, "Paused. The search is saved."),
`mainwindow_solve.cpp` (Solve's question about a saved search,
`cb_BtnCont`'s checks, `abortable`, the Abort question,
`updateSolverOptionCheckboxes`' stacking rules, `animStep`),
`mainwindow_file.cpp` (the export's question about the levels beyond
1 GB, `problemPaused`), `mainwindow_help.cpp` (the two solvers' help) and
`voxelframe.cpp` (`showRodSet`, `showStacking`, `showStackingAssembly`,
`addPeg`, `addRodBase`, `addPanexV`, `tiltPlates`, `diskTint`). The
`discTabs` ("Disc": "The 3D view shows this disc. It is one voxel
thick.", "Size") are made hidden and never shown (dead code), and the
layout's `diskSizeInput` (1 to 64) is read by nothing; the disc list's
size field is the one in use. The plan's "START/GOAL trays" are the
sliding view's: the original's stacking view shows one stacking at a
time, the start or the goal, as the Start/Goal switch says.

**What a stacking puzzle is** (decision 0015): its shapes are **discs**
(a square of 2·size+1 cells, one thick, its centre empty); a **rod set**
is a board (how many rods, their height, the rules of a move); a
problem stands on one rod set and holds the discs of each rod at its
**start** and its **goal**; its solution is the fewest rod transfers.
Every stacking view is drawn tilted as the original's (`tiltPlates`: a
disc lies flat, the rods stand up).

### 9.1 Entities workspace
- **Left card: the disc list** in the shapes list's place (`DiskSelector`):
  a row per disc, its chip, "S1 - name", and "size" with its field and
  − and + (1 to 100; another value is refused with "An invalid size was
  entered, please pick a value between 1-100." and the size goes to 1, as
  the original). New makes a disc of size 1 (`shape.create`); rename,
  duplicate, delete (never the last), reorder and the shape's colour
  stay; the weight is not shown (no take-apart). Clicking a disc shows
  it in the view. The inspector (Transform, Fit & scale, Repair &
  surface) is hidden: its tasks are the cell editor's.
- **Right card: Rods** in the voxel editor's place (the original's Rods
  panel; the voxel editor, the tool strip, the planes and the palette
  are hidden). The rod sets ("R1 - name" chips) with **New** ("Add
  another rod set"), **Delete** ("Delete the selected rod set"; the last
  one stays: "Keep at least one rod set"), **Copy**, **Label** ("Name
  the selected rod set"), ← and → ("Exchange with the previous/next rod
  set"). Their Undo and Redo are the document's (Ctrl+Z), not a rod
  history of their own. The fields of the selected rod set, with the
  original's tooltips:
  - **How many rods** (1 to 16; "Pegs on this board");
  - **Grow to the height of all pieces** or **Defined height** n (1 to
    64);
  - **Disc size matters - no large on small** (off with Panex columns);
  - **Disc can only move over 1 rod**;
  - **Panex Style Columns**;
  - **Add pocket column** n (with Panex columns; its height 1 to 64);
  - **Extra outside channel** (with Panex columns and three rods or
    more).
  A change is `stacking.setRules` (the problems on the set lose their
  solutions, as the original). Everything here is frozen while anything
  solves (the original greyed the fields).
- **3D view:** the disc selected (lying flat), or the selected rod set
  drawn alone (`stacking.rodSetLayout`: grey pegs, a base, Panex
  columns' Vs) once a rod set is chosen or edited, as the original's
  `showSelectedRods`; choosing a disc shows the disc again.

### 9.2 Puzzle workspace
- **Problems:** as on the other grids (New, Copy, Delete, rename, order).
- **Board** in the Result's place: "Result: R1 - name" on the rod sets'
  blue, or "No Start/Goal Rods" in red; the rod sets to choose from and
  **Set Start/Goal Rods** ("Set the selected rod set as this problem's
  board", `stacking.setProblemRodSet`). Choosing another rod set than
  the problem's shows it alone in the view (`cb_ProblemRod`).
- **All pieces:** the discs; + or a double-click puts the disc on top of
  the selected rod of the stacking edited (`stacking.placeDisc`: the
  next copy not on it, or one more copy); a refusal's reason goes to the
  status bar ("Choose a rod set with Set Start/Goal Rods"). "In this
  puzzle", the counts, the ranges, the More menu and the fit summary are
  hidden: a stacking problem's pieces are the discs on its rods.
- **Right card: Stacks** in the selected piece's place: **Start** or
  **Goal** ("Order the discs that start on the selected rod, bottom to
  top" / "Order the discs the solver must reach on the selected rod"),
  the **rod slider** ("Rod: 1", "Rod: P" for the pocket column, which
  comes first; "Select which rod in the chosen rod set to stack discs
  on"), **Stack Order** (the discs on that rod, the top first, in the
  pieces' colours; one selected), and the original's buttons: ↑ and ↓
  ("Move the selected disc up/down the rod", `stacking.moveDisc`), **+1**
  (the selected piece onto the rod), **−1** (the selected disc off,
  `stacking.removeDisc`: "That rod is empty"), **All +1** (every disc,
  largest first, `stacking.placeAll`; why the next did not go in the
  status bar), **Clear All** (the edited stacking emptied,
  `stacking.clear`). Under them the **validity bar** ("Whether the start
  and goal stackings obey the rod set's rules"): "Valid" in green or
  "Invalid - <reason>" in red (`stacking.board`'s error). Exclusive
  groups and colour rules are hidden.
- **3D view:** the board with the discs of the stacking edited, start or
  goal (`stacking.board`'s layout), each disc in its colour.
- **Status line:** the problem's board and the validity's reason.

### 9.3 Solver workspace
- **What to check:** "Assemblies" or **"Find Solutions"** ("Search for a
  shortest sequence of rod transfers from the start stacking to the
  goal."), checked at first (`stackingDisasmDefaulted`); Just Levels
  stays (it forces Find Solutions, and the search ignores it, as the
  original's), Check Rotations is not shown.
- **Search options:** Just count, **Enable High Memory** ("Let the
  search hold up to half of this computer's memory…"), the **Solver
  type** with one entry by the problem's board, as the original's menu:
  "Panex Solver" for Panex columns, else "Stacking Solver"
  (`engine.info.stackingSolvers`, `SolveOptions.stacking.solver`), with
  the help of `mainwindow_help.cpp`; Keep every n-th, Limit, and
  **Autosave every 20 min** (here: the search saves itself every 20
  minutes; the original's PAR_PANEX_NO_AUTOSAVE without it). Keep
  mirror, Keep rotated, the thorough check, Strict colours and the sort
  are not shown.
- **Run:** Solve, Continue, Pause, Abort; no Prepare (a stacking search
  has nothing to build). Solve and Continue are off while the problem
  has no board ("Choose a rod set with Set Start/Goal Rods") or its
  stacks are invalid ("Fix the puzzle on the Puzzle tab first: <reason>"
  as their tooltip). **Solve with a saved search** asks first: "This
  problem has <a saved search …>. Continue it, or start over and discard
  it?" with Cancel, Continue and Start Over (`solve.status.savedSearch`).
  **Continue** is on whenever a saved search exists (its tooltip "Carry
  on <a saved search …>"), and the Activity line then says "Paused. The
  search is saved." **Abort** is on for a saved search too and asks as
  for any solve. While the search runs the Activity line is the
  original's: "Panex search, N moves deep. Found N stackings.",
  "Panex search, tracing the path. Traced N of M moves.", or "Stacking
  search. Scanned N stackings."; the solver's note comes in a message
  box when a run ends (through, or paused: where it was saved). The
  20-minute save point of the other grids is not used (the search saves
  itself); File ▸ Save while a stacking solve runs offers to pause it (the
  search saves itself as it pauses), saves, and carries it on.
- **Solutions:** one at most, its moves the transfers; Analyse,
  Movements, Placements and Step are not offered (no take-apart, no
  assembler).
- **Player:** the board's pegs around the discs, its steps the
  transfers (each a lift, a move across and a drop), the poses centred
  on the board (`stacking.board`'s centre).
- **File ▸ Export ▸ Paused Solver State** of a stacking problem asks,
  when the saved levels take more than 1 GB, "The saved search keeps
  <size> of levels for finding the path once the search is done. Include
  them? Left out, the file is much smaller, but finding the path at the
  end takes longer." (Cancel, Include, Leave Out; `withLevels`).

### 9.4 New, the saved searches' folder
- **File ▸ New** offers **Stacking** third, after Brick and Sliding (the
  original's order); a new stacking puzzle has one rod set (R1, three
  rods that grow, size matters), no discs and no problem.
- The app keeps saved searches in its own data folder (`panex` in the
  app's data directory): the main process sets the engine's
  `panexFolder` before the page may call it, and refuses a page's
  `settings.set` that names a folder. A search paused in the app carries
  on in the app (also after a restart); the `.btsolve` export carries it
  to `bt` or the original.

## 10. Mapping to the original

| UI | Original code (burrtools_rotations) |
|---|---|
| Shapes list, rename, duplicate, delete, reorder, weight | `mainwindow.cpp` shape callbacks, `puzzle_c` shape list, `voxel_c::setWeight` |
| Transform, nudge | `voxel_c::transform`, `translate`; grid editors in `src/gui/grideditor_*.cpp` |
| Prune, center, origin, minimize, ×2/×3 | `voxel_c::minimizePiece`, `resize`/`resizeInclude`, `scale`, `scaleDown`; GUI buttons in `mainwindow.cpp` |
| Fill holes, surface inner/outer | `voxel_c::fillHoles`, `actionOnSpace` |
| Tools, mirror, span, layers, planes | `grideditor*.cpp` (logic to move into the core's commands) |
| Colours | `puzzle_c` colour list, `voxel_c` cell colours |
| Problems, result, counts, ranges, groups | `problem_c`, `groupseditor.cpp`, `bulkrangewindow.cpp` |
| Colour rules | `problem_c` colour constraints, `constraintsgroup.cpp` |
| Fit summary | `mainwindow.cpp` status line (problem info) |
| Solve, Prepare, Continue, Pause, Abort, progress | `solvethread.cpp`, `mainwindow_solve.cpp` |
| Search options | `solveThread_c::PAR_*` flags, `mainwindow_layout.cpp` checkboxes |
| Placements / Movements / Step | `placementbrowser.cpp`, `movementbrowser.cpp`, assembler single-step |
| Solutions list, sort, remove | `problem_c` solutions, `mainwindow.cpp` sort/delete callbacks |
| Disassembly player | `disasmtomoves.cpp`, `voxelframe.cpp` animation |
| Analyse disassembly | `mainwindow_solve.cpp` re-disassemble |
| Export to STL | `stlexportsolution.cpp` (later in this project) |
| Rod sets, discs, stacks, the validity bar | `mainwindow_rods.cpp`, `BlockList.cpp` (`DiskSelector`, `RodSelector`, `RodStackList`), `rodbars.h` |
| The stacking view, the player in transfers | `voxelframe.cpp` (`showRodSet`, `showStacking`, `showStackingAssembly`), `mainwindow_solve.cpp` (`animStep`) |

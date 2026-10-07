# Web UI specification

What the future web front end does, screen by screen, mapped to the original
BurrTools features, with the gaps the design still has to fill. The engine
API ([core-api.md](core-api.md)) is derived from this document: every action
here must map to an API call.

**Source:** Girish Sharma's mockup, https://grssam.github.io/burr-tools/
(`index.html` on the `gh-pages` branch of `grssam/burr-tools`, commit
`d7afb3ed`, 2026-10-04, GPL-3.0). The mockup is a single HTML file with mock
data and hand-projected canvas drawing; its layout, workflows, wording and
keyboard map are adopted, its code is not. The mockup cites a separate spec
(item ids such as "OQ-19", "C14") that is not published; ask the author.

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
3. **Sliding and Stacking/Panex puzzles** (later in this project): walls,
   start locks, goal, nested slides, deep/full search; rods, disc sizes,
   start/goal trays, Panex columns, pocket column.
4. **Display modes:** colour-constraint colours exist (`colorView`), anaglyph
   and the original's three render styles (bevelled voxels, flat with edges,
   STL-like) do not. Decide which to keep.
5. **Non-cube grids:** transform sets for rhombic and tetra-octa, mirror and
   span tools, XZ/YZ planes (OQ-19 in the mockup's spec).

## 8. Mapping to the original

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

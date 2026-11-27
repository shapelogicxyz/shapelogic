# Web UI specification

What the future web front end does, screen by screen, mapped to the original
ShapeLogic features, with the gaps the design still has to fill. The engine
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
- **Workspace rail:** Entities (`Ctrl+1`), Puzzle (`Ctrl+2`), Solver (`Ctrl+3`);
  centerd in the top bar (the default) or a rail down the left side (Main
  tab bar location). While a sliding or a stacking puzzle is open a
  fourth, **Play** (`Ctrl+4`), follows Solver (sections 8.5 and 9.5,
  decisions 0039 and 0042).
- **Body:** each workspace has a left card, the **3D view** in the center and
  a right card. Cards collapse to narrow rails (`Ctrl+[`, `Ctrl+]`); the 3D
  view can take focus (its Focus button, View ▸ Toggle 3D). Layout is remembered per workspace.
- **Status bar:** messages on the left (a transient "flash" after actions,
  e.g. "Filled 3 interior voxels"), cursor position on the right.
- **Density:** Standard; Compact (Standard with the scrolling lists' rows,
  shapes, problems, solutions and the pieces library, about half as tall,
  and on the Shapes tab each control on one line, its icon beside its
  caption, the action buttons without their descriptions, which are in
  their tooltips, and tighter padding);
  or Minimal (icons without captions, helper text in tooltips, auto-hiding
  viewport toolbar, narrower cards). Every function is available in all
  three, and the text is the same size in all three. The shapes list and
  the Puzzle and Solver problems lists grow to about eight Standard rows
  before they scroll.
- **Font size:** Small, Medium (the default), Large: every text size of the
  interface, Medium and Large each about 2 points larger than the size
  before; rows and controls a line of text sets the height of grow with it.
- **Theme:** Light, Dark, System; also View ▸ Color Theme (Use Dark Colors
  Theme, Use Light Colors Theme, Use System Color Theme).

### Settings dialog
Searchable, four pages, "Restore all defaults", changes apply immediately.
| Page | Setting | Values (default) |
|---|---|---|
| General | Interface density | Standard / Compact / Minimal (Standard) |
| | Font size | Small / Medium / Large (Medium) |
| | Sidebar width | Static / Variable (Variable); Static: left and right widths, 200 to 600 px (340, 374); Variable: past a 1600 px window each card and the 3D view take a third of the extra width, cards up to 600 px, the right card 10% wider than the left (374 to 660 px) |
| | Main tab bar location | Top / Left (Top): the workspace tabs centerd in the top bar, or a rail down the left side |
| | Theme | Light / Dark / System (System) |
| | Top bar theme | Colorful / Boring (Colorful): the axis colors behind the name, fading into the bar, and the tabs colored Shapes green, Puzzle blue, Solver red; or the plain bar, a small square of the three colors beside the name |
| | Undo history depth | 25, 50, 100, 200, 500 (25) |
| | Show tooltips | on |
| | Check for updates at startup | on |
| | Debug logging | off: the log file's DEBUG lines (the engines' `log` setting `debug`, the main process's app-log.ts): each sliding level done, each save of a search and the files it names, a saved search read back; the log compressed past 100 MB instead of 20 MB; turned off, the compressed log goes, the newest 100 lines stay and the 20 MB before them are compressed |
| | Cache folder | the main process's (main/cache-folder.ts), not a setting of the page: the folders in use (sliding searches, stacking searches), Choose Folder… (a folder dialog; a `ShapeLogic` folder made in the folder chosen, with `sliding` and `panex` in it, and a file written in each at once: a folder that cannot be written is refused and the folders in use stay) and Use Default (`sliding` and `panex` in the system's application data folder for ShapeLogic: `~/Library/Application Support/ShapeLogic`, `%LOCALAPPDATA%\ShapeLogic`, `$XDG_DATA_HOME/shapelogic` or `~/.local/share/shapelogic`; what the earlier defaults, the system cache folder's `sliding` and Electron's data directory's `panex`, hold moved there at the start); data found in the folders in use is described, then moved there or deleted (Move Data, Delete and Start Clean, Cancel); refused while a solve runs in any window; Reset section and Restore all defaults leave it |
| Grid editor | Grid plane | XY / XZ / YZ (XY) |
| | New shape size | Normal Burrs and Sliding, each X, Y, Z: 1-100 each, in the size steppers' units (4, 4, 4; Sliding 6, 6, 1); on a sliding puzzle + New Piece takes the last piece's size and + New Board the last board's, the setting only for the first |
| | Sliding board color, Light theme / Dark theme | a color each (#383838, #c8c8c8): a board with no color of its own (a new board has none) shows the color of the theme in use, in the shapes list, the 2D editor and the 3D view, and changes with the theme; a board given a color keeps it |
| | Voxel hint layer: Hinted layer | Below / Above (Below): the cells of that layer drawn faintly under the edited one's, in the shape's color (the original's layer-below hint) |
| 3D view | Show view cube | on |
| | Reverse scroll zoom direction | off |
| | Rotation method | Drag / Arc-ball (Drag) |
| | Lighting | on |
| | Fade out removed pieces | on |
| Solver | Worker threads | 1-32 (machine dependent) |
| Search Options | (a note: the defaults of the Solver's Search Options, for a problem that saved none; an option on the Solver tab changes that problem only) | |
| | Every option of the Solver's Search Options, in its order, "(Only: …)" under the name of one only some puzzle types have: Always show Assemblies, Just Count, Just Levels, Keep Mirror Solutions, Keep Rotated Solutions, Deep Symmetry Check, Strict Color Restrictions, the five sliding boxes, Solver type (Complete / Fast), Keep every n-th, Limit | as shipped: the original's |
| | Memory: Search memory | 2 GB to 4 GB less than the machine's memory in steps of 2 GB (Automatic: half of it, the bold tick; a warning under the slider above three quarters), shared equally by the sliding and stacking searches running at once (decision 0028); replaces Enable High Memory |
| | Disk space: Sliding disk space | Automatic (half the free space, at most 200 GB), 10 GB to 5 TB |
| | Disk space: Stacking disk space | Automatic (half the free space, at most 200 GB), 10 GB to 5 TB |

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
  rename (F2, Enter, double-click), duplicate (`Ctrl+D`, added last),
  delete (`Del`, the last shape too), reorder by drag or `Alt+↑/↓` (ids
  follow the order), right-click menu with the same actions and **weight**
  −1/+1 (in the solver heavier shapes stay in place, lighter ones move; the
  row has no weight buttons or badge: those are in the Puzzle workspace's
  Groups table, and a tile there shows "Weight: n" when it is not 1).
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
  outer: make fixed, make variable, clear color.

### Right card: voxel scope ("tool, plane, layer, grid size and color together")
- **Header:** "Voxel editor" and a label "Type: <name>", the puzzle type
  as File ▸ New names it (Brick, Sliding, Stacking, Triangular Prism,
  Spheres, Rhombic, Tetrahedra-Octahedra; no lock), fixed when the file is
  made.
- **Tools:** Fixed (`1`), Variable (`2`), Erase (`3`), and a last button
  that switches to the box selection ("Select", a dotted square with a
  pointer; while selecting it reads "Paint" with the brush and goes back
  to drawing). Paint color (`4`) is a brush beside the palette's trash
  can, shown only while a constraint color is the drawing color
  (Default chosen: the tool goes back to Fixed, and `4` does nothing).
  Quick tools with modifiers in both 2D and 3D: Shift+click fixed,
  Alt/⌘+click variable, Ctrl+click erase. Right-click/right-drag erases in 2D.
- **Mirror** X/Y/Z (also place the mirrored cell) and **Span** X/Y/Z (fill
  the whole grid along that axis on each click). Cube grid only in the
  mockup; non-cube grids to be decided.
- **Plane** XY / XZ / YZ (non-cube grids: XY only in the mockup), **layer
  strip** with layers that contain cells marked, `PgUp`/`PgDn`; it is as
  wide as its heading ("Z layer") on one line.
- **2D grid**: click or drag applies the tool to every cell entered, **one
  undo step per drag**; box selection with Fill / Clear / deselect (`Esc`).
  Non-cube grids draw their real cell shapes (triangles, circles, tetrahedra
  bases). The axis lines run along the cells' outer edge (the sphere
  grid's circles reach past the grid's box).
- **Grid size** steppers X/Y/Z with **Apply to all shapes**; shrinking removes
  cells outside.
- **Color palette**: the current drawing color as a large swatch,
  labeled **Shape color** (the shape's own: **Default**, or **Custom**
  once set) or **Restriction color** (C1, C2, ...), then + (add: the
  candidate color least like those there and the shape's own), the
  pencil (edit: an in-app picker in the popover, a saturation and
  brightness square, a hue slider and a hex field; for the shape's own
  color **Restore Default** below it) and the trash can (remove: cells
  using it revert to the shape's color; off on the shape's color; the
  drawing color moves to the one before). Once there is a constraint
  color, a row below holds the shape's color and the constraint
  colors, one row shown at a time: a full row starts another, the view
  follows a color chosen or added, and small up and down arrows on the
  right step through the rows once there are two. In the grid a cell
  with a constraint color is drawn in the shape's color with the
  constraint color as a box in its top-left quarter.
- **3D edit mode** (`E`): plain clicks in the 3D view apply the active tool;
  the cursor shows the tool, and the view's hint gains lines on what a
  click does. With fixed or variable, a face's left half adds the cell
  across it, its top-right quarter (as the screen shows the face) gives
  its own cell the tool's state (its color kept; nothing if it has it),
  its bottom-right quarter erases its cell (previewed in the danger color
  at 0.25); a click on no face adds the editor's layer's cell under the
  mouse on the cube grids. A refused target is shown in the muted color.

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
Rotation turns each piece about its own center. The result (with the
selected piece beside it) is centerd at the top, the copies in rows under
it, a short last row centerd; the number of copies a row is the one that
shows them largest in the view's shape (rows of five under a sliding
board's trays). The pieces keep their sizes to one another, the largest
filling its place with its mesh's outline at the home view (not the
sphere any turn stays in, nor the grid's box, which a grid's cells need
not fill), each centerd on its mesh, and are drawn 15% larger in their places than the
mockup's; the result fills a place 60% larger whatever its size, so small
pieces of a large result are not shrunk to it. Under a result the rows
of copies sit 35 pixels further down, the layout fitted to the height
less that. The zoom grows or shrinks
each piece where it stands; the layout keeps its places (the pan moves
it all).

### Right card
- **Selected piece**: cells (fixed/variable), **copies in the puzzle** (one
  count) or **count range** (min–max) with a switch between the two,
  **exclusive group** (None, G1, G2…, + New).
- **Exclusive groups** list, with members; delete a group.
- **Color rules** matrix: rows = piece color, columns = result color; a
  ticked cell lets a piece cell of that color sit on a result cell of that
  color. Buttons: each color on itself, allow all, clear.

## 5. Solver workspace

### Left card
- **Problem** list, as the Puzzle workspace's (without its editing): each
  problem's solve marked solving or paused; the one shown's result and piece
  shapes below it.
- **What to check**: assemblies only, or assemblies and disassembly.
- **Search options**: Just count; Keep mirror solutions; Keep rotated
  solutions; Thorough rotation check; Drop below level (solutions with a
  lower level are dropped); Keep at most this many solutions; Don't keep the
  disassemblies; Sort found solutions by.
- **Run**: Solve problem (from the start), **Continue** (resume a
  stopped search: in Solve's place while a solve runs, is paused or has a
  saved search; starting over is Abort, then Solve), **Pause**; no
  **Prepare** (the original's built the search and paused, which its
  placement browser and Step needed; here they build assemblers of their
  own); progress bar with percentage; activity, assemblies, solutions,
  time used.
- **Step-by-step explorer** (advanced): **Placements** browses the assembly
  search tree, **Movements** browses the disassembly search, **Step**
  advances the assembler one step. Placements and Step prepare an
  assembler of their own; on a problem where that takes a while the
  drawer says how far it is (the placements found, the optimize pass)
  with a **Cancel** button, and the rest of the app goes on working
  meanwhile.

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
| Everywhere | Ctrl+1 / 2 / 3 / 4 | Entities / Puzzle / Solver / Play (a sliding puzzle) |
| | F1 | keyboard shortcuts |
| | Ctrl+N / Ctrl+O, Ctrl+S / Shift+Ctrl+S, Ctrl+W | new, open; save, save as; close the window (the File menu, stage 6) |
| | Shift+Ctrl+N | File ▸ New Window (decision 0036) |
| | Ctrl+, | Settings |
| | Ctrl+[ / Ctrl+] | collapse or expand left / right card |
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
| Play | drag a piece, Shift+drag | slide it, anywhere it can reach one move; carry what is nested, or move what it is nested in, first |
| | Ctrl+Z / Shift+Ctrl+Z | undo / redo a move of the game |
| | ← / →, Shift+← / Shift+→ | one move back / forward, to the Start / the last move |
| | Space, Esc | play / pause the moves, pause |
| Dialogs | Esc, Enter, Space | close, commit, toggle |

## 7. Gaps: needed but not in the mockup

1. **Menus and app flows:** New (choose the grid type once, it cannot change
   later), Open, Save, Save As, recent files; Import PuzzleSolver3D and
   Puzzlecad; Export (STL, image, vector image, GLB solution animation,
   Puzzlecad, paused solver state); Convert grid type; Convert assemblies to
   pieces; puzzle notes/comment; Status window (per-shape table: cells,
   identical shapes, symmetry, connectedness); tutorial, user guide, about.
2. **This fork's solver options:** solver type (Complete (ShapeLogic), Fast (Crowell)),
   Check Rotations (rotation take-apart), Just Levels, Strict Color
   Restrictions, Deep Symmetry Check, Autosave every
   20 minutes (not an option here: a running solve is always kept in the
   rescue copy, as often as that stays cheap, decision 0024), **Abort** (hard stop that clears the results, separate from
   Pause; **Reset** in its place once the solve is over, an undoable
   solve.reset, the fork's setAbortLabel), paused-solve import/export, debug statistics panel.
3. **Sliding and Stacking/Panex puzzles**: sliding in section 8 (deferred
   part 2); stacking in section 9 (deferred part 4): rods, disc sizes, start/goal
   trays, Panex columns, pocket column.
4. **Display modes:** color-constraint colors exist (`colorView`), and the
   original's render styles (bevelled voxels, flat with edges, the wire
   frame, and since deferred part 5 the STL-like one, section 10.3);
   anaglyph does not.
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
the marks in the pieces' colors, `drawStampPreview`), `BlockList.cpp`
(the list's text and swatch), `slidingcolors.h`, `voxelframe.cpp`
(`showSingleShape`'s two trays, `buildSlidingWalls`,
`drawSlidingWalls`) and `gridtypegui.cpp` (Sliding second in New's
list). `ToolTab_Sliding` ("Tray edit", "Walls", Start, Goal) is never
made by the original (dead code); its labels name the right card's mode
switch below.

**What a sliding puzzle is** (decision 0014): pieces are shapes, one
layer or more; a **start/goal shape** (a tray) is a shape, one layer or
more, whose fixed cells are floor, variable cells corridors (pieces cross them but do
not stop on them) and empty cells walls; each copy of a piece may have a
start and a goal stamped on a tray, the whole piece at a time (a piece
the problem has three copies of, up to three of each, any copy ending on
any of its goals; a copy without a start starts anywhere, one without a
goal ends anywhere). The original gave each tray a problem of its own,
"START/GOAL n"; here the problems are the user's, as on the other grids:
a new problem has no result, and the user chooses a board as its result
(several problems may share a board and its stamps), whose stamped pieces
then follow in its parts. The original edited the stamps on the Entities
tab (its Start/Goal tab); here they are the Puzzle workspace's (8.2),
where the problem's counts are.

### 8.1 Entities workspace
- **Shapes list:** a tray reads "S1 Board 1" and, after a dash, the
  user's name (`BlockList.cpp`); its chip is black (`slidingColors::LIST`).
  On sliding puzzles the boards have a **Boards** list of their own
  above Shapes (four and a half rows before it scrolls; the pieces stay
  in Shapes), the Shapes header has **+ New Piece** (the other grids'
  **+ New**) and the Boards header **+ New Board** (the original's New
  Start/Goal Positions: "a shape that holds start and goal positions",
  `sliding.addStartGoal`, the size of the selected shape, else 6 by 6
  by 1, as `cb_NewStartGoal`); no problem comes with it. A tray is
  deleted by the list's Delete (the trash, the right-click menu and
  `Del`), which stands in for the original's Delete Start/Goal
  Positions; the problems on it stay, without a result. A name
  beginning "sg:" is refused (the engine).
- **Inspector:** the shape's color button is off on a tray (it shows the
  pieces' colors). The tasks are the bricks' (the original's `lockZ`
  left out those that add layers; sliding shapes may have them now).
- **Voxel editor (right card):** the Z size stepper is on (a new shape
  is one layer); the plane is XY only, the layers one at a time. The
  **color palette** and **Paint color** are as on other grids (decision
  0021: the start marks are apart from the colors; the original kept the
  marks in the colors and hid the color group); the grid draws the cell
  colors as on other grids, and a board's marks over them. A tray is
  drawn with the ordinary tools, as a piece is: Fixed = floor, Variable =
  corridor, Erase = wall (their tooltips say so), on the original's dark
  tiles (`EDIT_LIGHT`/`EDIT_DARK`). The original's Tray edit switch and
  Start/Goal tab are not here: the starts and goals are placed on the
  Puzzle workspace (8.2).
- **Images on pieces and boards (decision 0038):** beside the pencil in
  the Shape color area, a piece or a board has an **image** button while
  its own color (Default) is chosen; it is lit when the shape has an
  image. It
  opens **Apply image**: Choose image… (PNG, JPEG, WebP or GIF; shrunk to 2048
  pixels), **Crop** (the middle part at the piece's proportions),
  **Stretch** or **Manual** (the whole picture over the piece, faint off
  its cells, with dots at its corners and sides to resize it, Shift for
  free proportions at a corner, and one in its middle to move it, arrow
  keys too; it may be smaller than the box and lie partly off it, down
  to 12 preview pixels a side and 2 of them over the box; several
  pictures lie one on another, decision 0047), each picture chosen a tile
  beside the button with an × that takes it off and that can be dragged
  to reorder them (left the bottom, right the top; a click on a tile or on
  a picture in the preview picks the one the dots are on), the button
  Additional image… in Manual once there is a picture (on top, half the
  box's size in its middle) and Choose image… otherwise (Crop and Stretch
  show and send the picked picture only, and Choose image… replaces it),
  a preview of the piece's
  cells with the picture (as large as the window leaves room for: a 13 ×
  9 tray in a 1400 × 900 window is drawn over 600 × 400 pixels), and
  Remove image when it has one. The picture covers the piece's bounding
  box, shows only on its filled cells (in the 2D grid and on the faces
  looking up in every 3D view) and turns, flips and moves with the
  piece. A board's lies on its floor: in the 2D grids (the Puzzle
  workspace's Start and Goal grids show it on the cells no mark covers),
  the Puzzle workspace's 3D trays and the Play tab's floor, under the
  goals' tint and the pieces; the Solver's wall lines show none. The
  color menu (right click) has Apply image… / Change image… and Remove
  image too.
- **Tray border** (decision 0046): with a board selected, a section of
  that name above Transform, open, and Transform folded (open again for a
  piece). **Show tray border** (a switch, off for a new board) draws the
  tray's body round the floor, as the Play tab draws it, in every view of
  the board: a rim round the board's rectangle and a block on every cell
  that is no floor, a layer higher than the floor on the Shapes tab and in
  the Puzzle tab's trays, as high as the result round the Solver's pieces
  (in place of the walls' lines), and the Play tab's tray. While it is on:
  **Corners** (Rounded, Sharp: the tray's outside corners), **Color** (a
  swatch that opens the system's color picker, the image button, which
  opens Apply image for the border: the picture over the whole tray, its
  rim included, shown on all but the floor; Default takes the color off),
  **Material** (Smooth, Textured, Wood grain; wood is a light brown until
  the border has a color or an image, and the grain lies on those too),
  **Thickness** (Thin, the rim Play has always drawn; Thick, a rim one
  voxel wide, with **Voxels** below it choosing 1 to 10; the Puzzle tab's
  trays stand further apart round a thick one).
  Each change is one undo step; the look is kept while the border is off,
  and Play then draws its tray as it always has. The 2D voxel editor
  shows no border.
- **3D view of a tray:** the tray as any shape, with its border when it
  shows one.

### 8.2 Puzzle workspace
- **Problems:** as on the other grids (New, Copy, Delete, rename,
  order); the original grayed New, Copy and Delete out, its problems
  following the trays.
- **Result:** the board. **Change…** offers only the Boards list (and
  Clear Result; with no board yet it says to add one with + New Board),
  and a piece's right-click menu has no Set as result (the engine
  refuses a piece). With no board chosen the result row, the fit summary
  and the status say to choose one, and the problem's pieces show as on
  a Brick puzzle (no Start and goal, no trays). The starts and goals are
  the board's: a problem that shares it shares them, each problem's
  counts giving the copies it may place (`sliding.place`,
  `sliding.preview` and `sliding.tray` take the problem shown).
- **Pieces:** a piece a stamp requires (`Part.required`) stays in the
  puzzle (its remove is off, "Remove all" and "Set range for all pieces"
  leave it, as the engine does) and keeps a copy for each of its starts
  and its goals (`Part.placed`: its − is off there, with the reason in the
  tooltip); + adds copies.
- **Right card: Start and goal** in the selected piece's place (the
  original's Start/Goal tab, moved here): the tray twice, **Start** in the
  top half and **Goal** in the bottom half. Each has the problem's pieces
  with the copies placed there of each ("S2 1/3"; a click selects the
  piece), a hint (no pieces: add them to "In this puzzle"; none
  selected: select one; one copy: a click places it or moves it there;
  several: each click places one more copy, a click on a copy or a right
  click takes it off), the layers of a tray of several, and the grid: the
  tray's tiles, each marked cell in its piece's color (the chequer's
  light and dark variants), the selected piece following the mouse, its
  slice on the layer shown outlined with the slice's top-left cell under
  the mouse, at the lowest height that fits, green where it fits, red
  where it does not, orange on a copy already there (`drawStampPreview`;
  `sliding.preview`). The piece placed is the one selected in "In this
  puzzle" (or above the grid); a left click places a copy (or takes off
  the copy exactly there), a right click takes off the copy under the
  mouse (`sliding.place`, `sliding.clearAt`); a refusal's reason goes to
  the status bar ("The piece does not fit there: …", "All 3 copies of the
  piece have a start already: …"). The card's ☰ More options menu (left
  of Focus 2D editor) has **Clone start position to goal position** and
  **Clone goal position to start position** (`sliding.clone`, one undo
  step): when the side cloned to has pieces, a question first ("Pieces
  were found on the goal board…", Cancel / Clear and clone); a start on
  a variable cell is refused with the reason in the status bar. Below
  them, **Clear Start position board** and **Clear Goal position board**
  (`sliding.clear`, one undo step) take every piece off that side, asked
  first (Cancel / Clear); each is off while its side has no pieces.
- **Validity bar** (under the 3D view, `refreshSlideValid`): green "Valid
  Start/Goal positions for pieces in Start and Goal of this board" or red
  "Invalid Start/Goal positions - <reason>" (`sliding.tray.error`: starts
  or goals that are not whole pieces, more than the problem has copies,
  or no piece with every copy at a start and at a goal).
- **Exclusive groups** are hidden (the original grayed Groups); the
  **Color rules** matrix is as on other grids (decision 0021; the
  original hid the color groups, its cell colors being the start marks).
- **3D view** (`showSingleShape`): two trays side by side, **START** and
  **GOAL** (dark gray, `VIEW_GRAY` 0.12, a gap of 4 units), each copy
  placed standing on a tray in its piece's color (on a tray of layers,
  as many layers higher as its stamp is), at the top of the view under
  its tag; below them the pieces scene's copies of the pieces (no result
  or selected item: the board is the result, and the selected piece's
  copies are boxed), above the validity bar, drawn 15% larger in their
  places, as on a Brick puzzle (in rows of five, and no 35 px drop). The pieces take a band as tall as they
  need, at most 55% of the height between the tag and the bar, and always
  fit it (the zoom and the pan are the trays'); the trays are fitted at
  the home view into the space above and drawn over the whole view, so a
  zoom or a turn is not cut off at a border. With no pieces the trays
  fill the view.

### 8.3 Solver workspace
- **What to check:** "Assemblies" (the start layouts) or **"Find
  Solutions"** ("Keep a placement only when each marked piece can slide
  from its start cell to its goal cell"), checked by default on a sliding
  puzzle (`slidingDisasmDefaulted`); Just Levels and Check Rotations are
  not shown.
- **Search options:** **Solver type** shows the engine's one
  `slidingSolvers` entry, the **Sliding Puzzle Solver** (the original's
  Sliding Full Solver; its Fast and Deep solvers, which stopped at 250,000
  and 1,000,000 arrangements, are not offered, and the choice is not
  stored), with the help of `mainwindow_help.cpp`; **Allow Nested Slides**, **Allow Partially
  Nested** (only with nested slides), with the original's tooltips (the
  original's **Enable High Memory** is the Search memory setting,
  section 1); after Just count, **Allow any orientation for unplaced
  pieces** and under it **Include mirrored orientations** (only with it),
  both off, which the original did not have (every piece is placed as
  drawn; with them, a piece with no start or goal may be turned, and
  mirrored); above the Solver type, **Automatically use optimized paths**
  (decision 0029), off, which the original did not have: with it the
  stops are held to the grid a board of blocks fits; with it off, Solve
  asks first when the board fits one (`sliding.gridAdvice`: the grid and
  the stops it leaves; **Use Optimized Paths** for that solve, **Full
  Search**, **Cancel** or Escape solves nothing), a result found so is "(optimized)" in
  the problems list, and when it finds no solution the note offers **Full
  Search**; Continue goes on with optimized paths when the paused problem
  keeps a grid (`slideGrid`), whatever the switch says now, and asks first
  when the saved search would go on from the start alone where a new one
  would go from both ends (decision 0033, `sliding.resumeAdvice`: a search
  with nested slides on a grid paused in v1.8.1 or earlier): **Cancel**
  (the default) or Escape leaves the solve paused, and the question says
  that Abort, then Solve, searches again from both ends; **Continue**
  carries the saved search on to its end; Just count, Keep every n-th and the limit stay.
- **Performance Analysis** (decision 0030): while a sliding solve is under
  way or paused and no solution is listed, a second dashed box under the
  empty solutions card: the growth of the moves' times ("Each move takes
  about 1.05× as long as the one before."), then a table (columns Move,
  Time and "Remaining", or "Once continued" when paused): the move last
  searched with its time and "Complete"; the move under way, its Time
  counting up each second and its Remaining counting down from the last
  move's time times the growth, below zero (a minus, in the danger color)
  once it runs past the estimate; then every fifth move past it, eleven
  from the first multiple of five (the move under way not listed twice),
  with that move's time and the time until it. From `solve.progress`'s
  `slideForecast` (a paused problem's from `problem.get`); it scrolls when
  taller than the card, and goes when the solve is through. While a run
  takes its saved search up after a Continue (`slidePickingUp`: mostly a
  few seconds, in the later moves of a deep search now and then many
  minutes, before the move's clock starts and before any picture of the
  search comes), the row of the move under way, the empty card and the
  view say "Picking up the saved search… 47 s": how long it has taken so
  far (`slidePickingUpMs`), counted on each second so that the program is
  seen to be at work; the view and the card add "This usually takes only
  a few seconds. In the later moves of a big solve it can occasionally
  take 2 to 10 minutes or more." Between moves, while the search visits
  no arrangement (`slidePhase`: the move's arrangements all expanded, it
  waits for the earlier moves to go into the arrangements already seen,
  merges what it reached into the next level, writes levels to disk to
  make room, autosaves, or, a move of the level reaching the goal, goes
  through the level again for the first arrangement that does ("Move 81
  reaches the goal; finding the move to the goal, 43% (12 s)."); deep in
  a big solve for many minutes, with the
  counts and the live preview standing still and Remaining below zero), a
  line under the move's row says which, how far it has got and for how
  long ("Move 202 searched; merging what it reached into the next level,
  43% (4 min 12 s)."; its tooltip says why nothing else moves), the
  Details line adds the same sentence, and the view's tag reads "Live
  preview (on hold: merging the level, 43%)". A Continue the
  engine refuses because its search options name another search than the
  paused one's (decision 0043) is told in a message box, with what
  differs. A paused
  sliding solve's empty card says "Search paused" and to press Continue. Keep mirror, Keep rotated, the thorough rotation check,
  Strict colors and the sort are not shown (a sliding solve keeps every
  start and matches colors strictly; a path found goes last). They are
  stored with the problem under the file's names (`nestedSlides`,
  `partialNested`, `anyOrientation`, `mirroredOrientations`, `optimizedPaths`, `highMemory`; a stored `solverType`, the original's
  fast, deep or full, is kept as it was and not written).
- **Run:** Solve is off while the tray's starts and goals
  are invalid, with "Fix the start and goal on the Puzzle tab first:
  <reason>" as their tooltip (and the engine refuses with the original's
  message, shown as the failure). During the search the Activity line
  says "Search active with the <solver>", and the Details line has the
  original's Activity text without its "Sliding search, ": "N moves deep.
  Scanned N arrangements." (or without the depth); at the end Activity
  says "Search finished" and Details the problem's search statistics
  (`ProblemSummary.searched`, `searchDepth`, kept in the file), as the
  original's finished Activity line: "Scanned N arrangements. Searched N
  moves deep.". The count is the solve's, every run of it: paused (or
  opened paused from a file), Details says what it said as the search ran,
  in its words, and Continue counts on from there; Solve starts it at 0.
  The solver's **note** (`solve.status.note`:
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
  ("S3 changed shape: its start
  and goal were removed from S1.", "Only the first 255 shapes …").
- **File ▸ New** offers **Sliding** second, after Brick (the original's
  order); a new sliding puzzle starts empty (add pieces with + New Piece,
  then a board with + New Board, then on the Puzzle workspace a problem
  with + New and the board with Change…).

### 8.5 Play workspace (decision 0039)
This program's own: the original has no way to play a puzzle.
- **The tab** is in the rail, after Solver, only while a sliding puzzle
  (or a stacking one, section 9.5) is open; with another puzzle `Ctrl+4`
  does nothing, and a Play tab shown when another puzzle is opened gives
  way to Shapes.
- **Left card "Play":** the problems (the Solver's list), with the pieces'
  count and "play to the Goal" or "no Goal: free play"; **Game** (the
  moves made, Restart, asked first); **Position** (Save position as Start,
  Save position as Goal: `sliding.setLayout`, every piece, asked first
  when the problem has solutions or a paused solve; refused while a solve
  runs); **Moves file** (Export moves…, Import moves…, `.slmoves`).
- **Center:** the board over the 3D view, drawn from `sliding.playStart`
  (the floor in the board's chequer, corridors dashed, each goal tinted
  under the pieces and outlined over them, each piece with its name and
  picture); **Undo** and **Redo** right beside the board; the banner
  "Solved! — n moves" when the problem has goals and the position shown
  is the goal ("The Start is the Goal already" at move 0); a Z layer strip
  on a deeper board; the playback bar below (to the Start, back, play or
  pause, forward, to the last move, a seekbar, "Move i of n").
- **A drag:** the press asks `sliding.playReach` once; the piece follows
  the pointer over the stops, a step at a time, and a release somewhere
  new is a move. The same pieces dragged again go on with their move;
  back where the move began, the move is gone. With nested slides stored
  for the problem the piece moves alone when it can step that way, else
  with what is nested in it; Shift carries first. Pieces stay in their
  layer.
- **Undo, Redo, playback:** one place in the game's moves; a step is
  drawn along its route (`sliding.playReplay`). `Ctrl+Z` and
  `Shift+Ctrl+Z` are the game's here, and Edit names them Undo Move and
  Redo Move.
- **A problem that cannot be played** (no board, a piece without a start
  for every copy): the engine's reason and Go to Puzzle.
- **The moves file** is taken for the same board and start only, and when
  every move can be made; otherwise a message box says what is wrong
  ("Move 12 of 37 cannot be made here: …").
- The game is kept for each problem while the document is open, not in
  the puzzle's file; after a change of the document a game of the same
  start is checked again and goes on.

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
(a square of 2·size+1 cells, one thick, its center empty); a **rod set**
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
  duplicate, delete (never the last), reorder and the shape's color
  stay; the weight is not shown (no take-apart). Clicking a disc shows
  it in the view. The inspector (Transform, Fit & scale, Repair &
  surface) is hidden: its tasks are the cell editor's.
- **Right card: Rods** in the voxel editor's place (the original's Rods
  panel; the voxel editor, the tool strip and the planes are hidden;
  the palette keeps only the drawing color, the disc's own, and the
  pencil). The rod sets ("R1 - name" chips) with **New** ("Add
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
  solves (the original grayed the fields).
- **3D view:** the disc selected (lying flat), or the selected rod set
  drawn alone (`stacking.rodSetLayout`: gray pegs, a base, Panex
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
  pieces' colors; one selected), and the original's buttons: ↑ and ↓
  ("Move the selected disc up/down the rod", `stacking.moveDisc`), **+1**
  (the selected piece onto the rod), **−1** (the selected disc off,
  `stacking.removeDisc`: "That rod is empty"), **All +1** (every disc,
  largest first, `stacking.placeAll`; why the next did not go in the
  status bar), **Clear All** (the edited stacking emptied,
  `stacking.clear`). Under them the **validity bar** ("Whether the start
  and goal stackings obey the rod set's rules"): "Valid" in green or
  "Invalid - <reason>" in red (`stacking.board`'s error). Exclusive
  groups and color rules are hidden.
- **3D view:** the board with the discs of the stacking edited, start or
  goal (`stacking.board`'s layout), each disc in its color.
- **Status line:** the problem's board and the validity's reason.

### 9.3 Solver workspace
- **What to check:** "Assemblies" or **"Find Solutions"** ("Search for a
  shortest sequence of rod transfers from the start stacking to the
  goal."), checked at first (`stackingDisasmDefaulted`); Just Levels
  stays (it forces Find Solutions, and the search ignores it, as the
  original's), Check Rotations is not shown.
- **Search options:** Just count (the original's **Enable High Memory**
  is the Search memory setting, section 1), the **Solver
  type** with one entry by the problem's board, as the original's menu:
  "Panex Solver" for Panex columns, else "Stacking Solver"
  (`engine.info.stackingSolvers`, `SolveOptions.stacking.solver`), with
  the help of `mainwindow_help.cpp`; Keep every n-th, Limit, and
  no **Autosave every 20 min** (the search always saves itself as it
  runs, a minute in and then as often as saving stays cheap; the
  original's PAR_PANEX_NO_AUTOSAVE is never set). Keep
  mirror, Keep rotated, the thorough check, Strict colors and the sort
  are not shown.
- **Run:** Solve, Continue, Pause, Abort. Solve and Continue are off while the problem
  has no board ("Choose a rod set with Set Start/Goal Rods") or its
  stacks are invalid ("Fix the puzzle on the Puzzle tab first: <reason>"
  as their tooltip). **Continue** stands in Solve's place whenever a
  saved search exists (`solve.status.savedSearch`; its tooltip "Carry on
  <a saved search …>"), so Solve never meets one and asks nothing (the
  original's cb_BtnStart asked Continue or Start Over): starting over is
  Abort, then Solve, and the Activity line then says "Search
  paused and saved". **Abort** is on for a saved search too and asks as
  for any solve. While the search runs the Activity line names the
  solver ("Search active with the Panex Solver") and the Details line
  is the original's Activity text without the search's name: "N moves
  deep. Found N stackings.", "Tracing the path. Traced N of M moves.",
  or "Scanned N stackings."; the solver's note comes in a message
  box when a run ends (through, or paused: where it was saved). The
  other grids' save point is not used (the search saves itself as it
  runs); File ▸ Save while a stacking solve runs offers to pause it (the
  search saves itself as it pauses), saves, and carries it on.
- **Solutions:** one at most, its moves the transfers; Analyse,
  Movements, Placements and Step are not offered (no take-apart, no
  assembler).
- **Player:** the board's pegs around the discs, its steps the
  transfers (each a lift, a move across and a drop), the poses centerd
  on the board (`stacking.board`'s center).
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
  on in the app (also after a restart); the `.slsolve` export carries it
  to `bt` or the original.

### 9.5 Play workspace (decision 0042)
This program's own, as section 8.5 is; what is not said here is as there
(the left card, the banner, Reset, Undo and Redo, the playback bar, the
keys, a game kept for each problem while the document is open).
- **Left card "Play":** the discs' count and "play to the Goal" or "no
  Goal: free play"; the note under the moves says why a goal that holds
  discs is not played to, or on a Panex board how its columns work;
  **Position** saves with `stacking.setStacks`, every disc where it is.
- **Center:** the rods from the front, drawn from `stacking.playStart`:
  the pegs on a base, each disc a bar as wide as its size, with its name
  or, without one, its size (too wide for the disc: each word's first
  letter, "R 1" for "Right 1"; still too wide: none); a Panex column
  with the steps a disc of each size rests on behind it, and the bridge
  across the top; with the
  outside channel (the problem's rod set's `outsideChannel` in the
  snapshot), a loop over the bridge from the first column to the last,
  above where a moving disc crosses, not on the Goal's picture. Beside the
  board, under Redo: the **Goal** as a small picture of the same board,
  a button with a pop-out icon (two windows) at its top right that opens
  the goal's window (below), **Hide Goal** and **Hide Disc Labels**.
- **The goal's window** (`SlApp.goalWindow`, `main/goal-window.ts`): a
  sliding puzzle's **Show Goal Image** (under Hide Goal Lines, an image
  icon) or a stacking puzzle's Goal picture opens the goal in a window of
  its own, one per app window, above it and closed with it, moved and
  sized freely; a click again brings it forward. Sliding: the board as
  drawn, each piece with a goal moved onto it (`goalPlaces`: the first
  copy of its shape whose cells the goal is), the others, the goals' tint
  and outlines left out. Stacking: the goal drawn at board size, cropped
  to the drawing (each part measured where it is drawn, the discs'
  translate included). **GOAL** in bold above the drawing, white in the
  dark theme and black in the light (the picture's `dark`); the window
  opens 30 pixels taller for it. The drawing is made standalone (`play/goal-picture.ts`:
  the stylesheet's values written into each element, so the theme and the
  hidden labels carry over) and shown as an image under a CSP that allows
  nothing else; it is drawn again as the problem, the game's start, a
  button's choice or the theme changes while the window is open. The demo opens a
  popup instead.
- **A move:** a press anywhere in a rod's column takes its top disc and
  asks `stacking.playMoves` once for the position. Dragged, the disc
  follows the pointer and goes onto the rod it is let go over; clicked, it
  lifts and goes onto the rod clicked next (its own rod: put down). The
  rods it may go to are marked. A transfer the rules refuse puts the disc
  back and tells the rule in the status bar ("A larger disc cannot sit on
  a smaller one."). Every transfer is one move; a disc put straight back
  on the rod it came from takes its move off.
- **Undo, Redo, playback:** a step is drawn as a solution's transfer is:
  the disc up over the rods, across and down.
- **A problem that cannot be played** (no rod set, no discs, a disc not on
  the start, a start that breaks the board's rules): the engine's reason
  and Go to Puzzle. A problem whose goal is empty is played without one.
- **The moves file** (`.slmoves`, `kind: "stacking"`) is taken for the
  same rods and the same start only, and when every transfer can be made;
  a sliding puzzle's file is told what it is for, as a stacking one is on
  a sliding puzzle.

## 10. The STL export and the STL-like style (deferred part 5)

The original's two STL dialogs (`src/gui/stlexport.cpp`,
`src/gui/stlexportsolution.cpp`) and its fourth render style
(`voxelframe.cpp`'s `styleSTL`, `getSTLMesh`). Everything the dialogs
show comes from the engine: the grid's parameters
(`export.stlParameters`), the preview's mesh and volume
(`export.stlMesh`), the file's bytes (`export.stl`), a solution's pieces
and their file names (`export.solutionPieces`); the page writes the bytes
through the main process, to paths its own dialogs gave.

### 10.1 File ▸ Export ▸ STL… (`stlExport_c`)

A dialog "Export STL" (`#exportStl`), wide, in two columns:
- **Left:** the grid's parameters, one row each, in the original's order
  with its labels and tooltips (cube, prism, rhombic and tetra-octa grids:
  Unit Size, Unit Size Y, Unit Size Z, Bevel, Offset, Interior chamfers;
  spheres: Sphere radius, Connection radius, Curvature radius, Offset,
  Recursions, Inner radius, Hole diameter, Square Hole): a number field
  for numbers (shown as the original's `%2.2f`), a whole-number field for
  Recursions (`%i`), a check box for a switch. **Binary STL** (on).
  **Shapes** (not the original's single PieceSelector): two multi-select
  lists side by side, **All shapes** and **To export** (label, name), with
  arrow buttons between them (→ the selected across, ⇒ all, ← and ⇐
  back; a double click moves one). To export starts with the shapes last
  exported from this document, else the shape selected. The preview shows
  the shape last picked in either list.
- **Right:** the preview, a small 3D view of the STL mesh of the chosen
  shape at the values as they are (drag turns it, the wheel zooms), in the
  piece's color, lit, with the original's two modes as a pair of buttons:
  **Normal** and **Insides** ("Display the insides of the STL object": the
  inner faces drawn, the outer ones faint, as x-ray). Under it the status
  line: "Volume: 123.4 cubic-units" (`%1.1f`), or the engine's refusal in
  the original's words ("Unit size too small", "Shapes with variable
  voxels cannot be exported", "The generated mesh is faulty in some way,
  try to tweak the parameter") with the preview emptied, as the original
  showed nothing.
- A changed field or shape updates the preview (on change, as the
  original's field callbacks; a field that is not a number says so in the
  status line and keeps the last preview).
- Buttons: **Close** (the original's Abort) and **Export STL…** (grayed
  with nothing to export). With several shapes to export: the system's
  folder dialog, then a file per shape named by the engine
  (`export.stlFileNames`, as a solution's pieces), the question before
  files are replaced and the status line "Done — exported n shape(s).",
  as Export to STL (10.2). With one: the system's save dialog (the original's File name and Path fields and its
  "..." chooser), proposing "<shape name, else label>.stl" in the
  puzzle's folder; the system dialog asks before replacing a file (the
  original asked "File exists overwrite?"). The file's solid is named by
  the file's name, as the original's. The dialog stays open, as the
  original's did; the status bar says "Exported name.stl".
- A large shape on the prism, rhombic or tetra-octa grid takes seconds
  (the Minkowski mesher): the status line says "Building the mesh…" while
  the engine works.

### 10.2 Solutions ▸ Export to STL (`stlExportSolution_c`)

The Solver's solutions card's **Export to STL** (enabled with a solution
chosen; grayed while nothing is chosen) opens "Export Solution Pieces to
STL" (`#exportStlSolution`):
- The grid's parameters and **Binary STL**, as in 10.1 (no preview, as
  the original's).
- **Output folder**: the folder chosen, and **Choose…** (the system's
  folder dialog; the original's "..." picked a file inside it). Until one
  is chosen, Export All is grayed (the original started at ".", the
  folder the program was started from, which a packaged app has no use
  for).
- The box "Pieces placed in solution N:", one line per distinct shape the
  assembly places: "S2  Frame1  ×1" (without a name "S4  ×1"), then
  "Total instances: 6"; the file each is written to is its own
  (`fileName`: the name made safe, told apart).
- **Export All** writes one file per piece into the folder; when some of
  them are there already it asks first: "N output file(s) already exist.
  Overwrite?" (Cancel, Overwrite). The status line then says "Done —
  exported 5 piece type(s)." or "Done — exported 4 piece type(s), 1
  error(s)." with each error told in a message box ("Error exporting
  name:" and the reason), as the original. **Close** closes it.

### 10.3 The STL-like style

View ▸ **Draw Pieces like STL Export** (enabled; the fourth of the style
radio items) and the Display menu's Style **Like the STL export**: the
pieces drawn with the STL mesh at the view's own parameters (the engine's
`shape.mesh` style `stl`: gap 0.02 and bevel 0.05 of a cell with interior
chamfers, the sphere grid's balls at two recursions), each in one color
(no checker, no edge lines), as the original drew `styleSTL`; everything
else as in the bevelled style (3D editing picks the cell under the
pointer, the wire mode of a piece stays the wire frame). On the prism,
rhombic and tetra-octa grids a large shape takes seconds the first time.

## 11. Imports, the Puzzlecad export and grid conversion (deferred part 6)

From the original's `mainwindow_file.cpp` (`cb_Load_Ps3d`,
`cb_Load_Scad`, `cb_Export_Scad`, `cb_Convert`), `convertwindow.cpp` and
`mainmenu.cpp`. The engine reads and writes the files and converts
(`document.import`, `document.exportPuzzlecad`, `document.convertGrid`);
the grids a puzzle converts to are `engine.info`'s `convertsTo`.

### 11.1 File ▸ Import ▸ PuzzleSolver3D… and Puzzlecad (OpenSCAD)…

- Enabled always, as Open. The unsaved-changes question first ("Save
  before you import another puzzle?": Save, Don't Save, Cancel), as the
  original's `confirmDiscard("import another puzzle")`.
- The system's open dialog: "Import PuzzleSolver3D File" with
  "PuzzleSolver3D Files (*.puz)", or "Import Puzzlecad File" with
  "OpenSCAD Files (*.scad)"; All Files beside.
- The puzzle replaces the open one: a new document without a file, with
  unsaved changes, shown by the source's name without its extension
  ("pieces●" for pieces.puz), so Save asks where to write it and proposes
  "pieces.xmpuzzle" (the original's `setFileName("")`: never over the
  source). The Entities workspace comes forward with the first shape, as
  the original's `selectEntitiesTab` and `activateShape`. The status bar
  says "Imported pieces.puz". The file does not go into Open Recent (it is
  not a ShapeLogic file).
- A file it cannot read: a message box with the original's "Could not
  load puzzle, sorry!" and the engine's reason under it; the open puzzle
  stays.

### 11.2 File ▸ Export ▸ Puzzlecad (OpenSCAD)…

- Enabled when the puzzle has a shape with cells, as Export ▸ STL (the
  original enabled it with any shape and then said "Could not export
  puzzlecad file." when none had a cell).
- Cube-grid and sliding puzzles (the original: the cube grid only); grayed
  on the other grids (`scad` of the menu state). A sliding puzzle's file
  has its own template (the trays in packing_box calls, the pieces'
  burr_plate after them, api/document.json).
- The engine is asked first: a puzzle it refuses gets the original's
  message box before anything else is asked ("Puzzlecad export is only
  available for cube-grid puzzles.", "Nothing to export.").
- Then a dialog "Export Puzzlecad (OpenSCAD)" (`#exportScad`; not the
  original's, which wrote the problem's distinct pieces): radio buttons
  **Pieces** (the shapes picked in the two lists of 10.1, every shape but
  a sliding puzzle's boards, all on the left to start with, the ones
  exported last kept per document; `shapes`)
  and **Problems** (the lists grayed; the problem chosen
  in the list under it, the Puzzle workspace's first, with every copy of
  each part: `copies`); on a sliding puzzle only (absent on the brick
  grid, where the engine refuses `tray`), check boxes **Export Tray**
  (`tray`; on to start with) and **Add Tray Lip** with its **Height
  (voxels)**, 1 to 16 (`lip`). The choices are kept for the next export.
- Then the system's save dialog "Export Puzzlecad File" with "OpenSCAD
  Files (*.scad)", proposing "<puzzle name>.scad" beside the puzzle (the
  original proposed the puzzle's own path); it asks before replacing a
  file (the original's "File exists; overwrite?"), and ".scad" is added
  when the name has no extension. The header comment names the puzzle's
  file, or the export's when the puzzle has none, as the original. The
  status bar says "Exported name.scad"; a file that cannot be written: a
  message box "Could not export puzzlecad file." and the reason.

### 11.3 Edit ▸ Convert Brick Grid Type to Other…

- Enabled with a document; grayed while a solve or an analysis is active
  (the engine refuses then: the original replaced the puzzle under its
  solver thread).
- A dialog "Convert Grid Type" (`#convertGrid`):
  - when the puzzle's grid converts to others: "Please Select Target
    grid", one radio button per grid (the labels New shows), the first
    chosen; a line saying what follows ("The puzzle is replaced: its undo
    history starts afresh and its solutions are dropped; Save writes it
    over its file."); buttons **Abort** and **Continue**;
  - otherwise: "There are no grids that this puzzle can be converted to,
    sorry" and **Ok**.
- Continue converts: the document is replaced (unsaved changes, nothing
  to undo, the same file: Save writes the converted puzzle there, as the
  original kept its file name), the Entities workspace comes forward with
  the first shape, and the status bar says "Converted the puzzle to the
  rhombic grid".

## 12. Mapping to the original

| UI | Original code (burrtools_rotations) |
|---|---|
| Shapes list, rename, duplicate, delete, reorder, weight | `mainwindow.cpp` shape callbacks, `puzzle_c` shape list, `voxel_c::setWeight` |
| Transform, nudge | `voxel_c::transform`, `translate`; grid editors in `src/gui/grideditor_*.cpp` |
| Prune, center, origin, minimize, ×2/×3 | `voxel_c::minimizePiece`, `resize`/`resizeInclude`, `scale`, `scaleDown`; GUI buttons in `mainwindow.cpp` |
| Fill holes, surface inner/outer | `voxel_c::fillHoles`, `actionOnSpace` |
| Tools, mirror, span, layers, planes | `grideditor*.cpp` (logic to move into the core's commands) |
| Colors | `puzzle_c` color list, `voxel_c` cell colors |
| Problems, result, counts, ranges, groups | `problem_c`, `groupseditor.cpp`, `bulkrangewindow.cpp` |
| Color rules | `problem_c` color constraints, `constraintsgroup.cpp` |
| Fit summary | `mainwindow.cpp` status line (problem info) |
| Solve, Continue, Pause, Abort, progress | `solvethread.cpp`, `mainwindow_solve.cpp` |
| Search options | `solveThread_c::PAR_*` flags, `mainwindow_layout.cpp` checkboxes |
| Placements / Movements / Step | `placementbrowser.cpp`, `movementbrowser.cpp`, assembler single-step |
| Solutions list, sort, remove | `problem_c` solutions, `mainwindow.cpp` sort/delete callbacks |
| Disassembly player | `disasmtomoves.cpp`, `voxelframe.cpp` animation |
| Analyse disassembly | `mainwindow_solve.cpp` re-disassemble |
| Export to STL | `stlexportsolution.cpp` (section 10.2) |
| File ▸ Export ▸ STL, the STL-like style | `stlexport.cpp`, `voxelframe.cpp`'s `styleSTL` (section 10) |
| File ▸ Import, File ▸ Export ▸ Puzzlecad, Edit ▸ Convert | `mainwindow_file.cpp` (`cb_Load_Ps3d`, `cb_Load_Scad`, `cb_Export_Scad`, `cb_Convert`), `convertwindow.cpp` (section 11) |
| Rod sets, discs, stacks, the validity bar | `mainwindow_rods.cpp`, `BlockList.cpp` (`DiskSelector`, `RodSelector`, `RodStackList`), `rodbars.h` |
| The stacking view, the player in transfers | `voxelframe.cpp` (`showRodSet`, `showStacking`, `showStackingAssembly`), `mainwindow_solve.cpp` (`animStep`) |

## 13. Tilted Square puzzles (decision 0045)

Not in the mockup or the original: a mode of ShapeLogic's own, built in the
stages of docs/TILTED-SQUARE-PLAN.md. A Tilted Square puzzle is a Brick
puzzle with a tray (`Snapshot.tray`, `state/tilted.ts`'s `isTilted`): a
`width` by `height` tray of straight unit squares, each open or a wall,
whose grid is turned by arctan(down/over). The turned cells wholly inside
the tray and clear of the walls (less the allowance) are the **board**,
shape 0, its cells variable: the pieces must fit it, not fill it. Every
shape has the board's size, one layer.

### 13.1 New
File ▸ New lists **Tilted Square** after Stacking, as plainly as the grids:
a description and, as the example, the tray a new puzzle starts with (8 by
8, 1 down 2 over, strict: 45 whole cells), drawn from `tray.preview`. OK
makes it with one problem whose result is the board. The type chip reads
"Type: Tilted Square".

### 13.2 Entities workspace
- **Left card.** The board leads in a **Board** section of one row (label
  `B`; renamed and colored, never copied, deleted or moved; Del says
  why). **+ New Piece** makes a shape of the board's size. A piece with
  no place where all its cells are whole (`ShapeSummary.fits` false) has a
  red **Doesn't fit** tag in its row, and the tray change or wall that
  left it so names it in the status bar ("S1 no longer fits the board: no
  place has every cell on a whole cell").
- **Tilt & tolerance** (`#secTilt`), with the board selected, in place of
  Transform, Fit & scale and Repair & surface: **Width** and **Height**
  steppers with **Keep square** (on at first; off for a rectangle; turned
  on, the height takes the width), **Down** and **Over** steppers, a line
  "6 by 6 · 1 down, 3 over · turned 18.43°", **Allowance** (a switch;
  on, **Amount** in cell widths, 0.05 at first, at most 0.25), **Placement**
  (**Most whole cells** or **Custom**, with the shift along x and y), **Every
  placement** (**Placements…**, section 13.5). Each
  change is one `tray.set`, one undo step; the engine makes the board again
  and moves each piece by whole cells to the nearest place where it is
  whole. Below it, the **Tilted Square Tutorial** header opens the research
  page (`openHelp('tilted')`).
- **Right card, the board.** The voxel editor's controls are hidden
  (`#stage.tltb`). **Focus Tray** and **Focus Cells** switch the board's
  picture: both draw the turned cells under the tray's straight squares;
  Focus Tray brings the squares out, Focus Cells the cells (whole in a blue
  and green checker, cut in gray). Above it: "Tray: 6 by 6", "Whole
  cells: 24", "Tilt angle: 18.43°", "Walls: 2" and how to draw walls. A
  click or a drag over squares makes walls (a drag from a wall opens
  squares): one `tray.setWalls`, one undo step.
- **Right card, a piece.** The grid editor turned so the tray stands
  straight, clipped to it, with its outline and walls; the cells the tray
  cuts gray and inert. No size steppers and no layers (`#stage.tlt`).
- **3D view.** The model turned by the tray's angle about Z, the tray's
  outline and walls as blocks one layer high, the board as its whole
  cells in the Cells view's checker; home is straight down.

### 13.3 Puzzle workspace
The result is the board, with no Change…; the pieces are turned as in the
tray.

### 13.4 Solver workspace
**Assemble Only**; **Assemble and Disassemble** is grayed ("A tilted
tray's pieces lie flat and lift straight out"). **Pieces may be turned
over** in Search Options, on at first, stored with the problem
(`flipPieces`); off, each piece keeps to the four turns in the tray's
plane and the search has no symmetry breaker. The tray's walls stand
around the solution's pieces in the 3D view. The disassembly player says
"A tilted tray's pieces lie flat and lift straight out: there is nothing
to take apart." and offers no **Analyse disassembly** (nor on Spheres,
whose grid has no disassembler). A solve that ends with no assembly says
"No assemblies found. The pieces do not fit the board where the grid lies
now. Another placement of the grid in the tray may take them." with **Try
every placement**, which opens the Placements dialog (13.5) for the
problem and counts at once with the options shown.

### 13.5 Placements
A dialog (`#placementsDlg`, `entities/placements-dialog.ts`) of every board
the grid can give in the tray (`tray.placements`), opened by
**Placements…** in Tilt & tolerance's **Every placement** row and by the
Solver's **Try every placement**.
- A line says how many boards there are ("The grid gives 6 boards in this
  tray, the most whole cells first. A board that is part of another is left
  out: it holds nothing the larger one cannot."), and when the engine's
  list is cut short, that it is not complete.
- **Pieces of** chooses the problem (hidden with one problem). **Count
  assemblies** starts `tray.fitPlacements` and reads **Stop** while it
  runs; beside it the state: "Trying placement 3 of 6…", then "1 of 6
  placements takes the pieces", "No placement takes the pieces", or
  "Stopped after 2 of 6 placements: none takes the pieces so far". A
  refusal is said there too (a problem without pieces; in the demo, that
  searches run in the app).
- A row a board, 40 at a time with **Show more**: its picture (the Cells
  view, from `tray.preview` at its shift), "24 whole cells", under it
  "Most whole cells" for the first and "In use" for the board the tray
  lies at, then the count ("Waiting", "Trying…", "12 so far…", "No
  assembly", "1 assembly", "7 or more" for the board a count was stopped
  on, "Not tried"), and **Use** ("In use", disabled, on the board in use).
  When a count ends, the first board that takes the pieces is scrolled
  into sight.
- **Use** stops a count under way, then sets the placement with one
  `tray.set` (the first board: the best placement; another: custom, its
  shift), one undo step, closes the dialog and says "The grid lies at the
  placement chosen: 24 whole cells" (or names the pieces that no longer
  fit). **Close**, Esc or a click beside the dialog closes it and stops a
  count under way.

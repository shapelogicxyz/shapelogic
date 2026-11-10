# Release notes

The changes in each ShapeLogic release, newest first. The installers for every
release are on the [GitHub releases page](https://github.com/shapelogicxyz/shapelogic/releases),
and [Install](https://shapelogic.xyz/docs/install/) says which file to download.

## [ShapeLogic v1.7](https://github.com/shapelogicxyz/shapelogic/releases/tag/v1.7) (10 November 2026)

### What's new

- **Two new grids: Hexagonal Prism and Cairo Prism.** **File ▸ New** makes puzzles from hexagonal prisms or from prisms on the Cairo pentagonal tiling, a space of any size. The solver assembles them and takes them apart, and the guide has a page for each.
- **Disassembly on more grids.** Rhombic, tetra-octa, hexagonal prism and Cairo prism puzzles are now taken apart as well as assembled. Only spheres are still assembled only.
- **Uniform bevels.** A new style in **Display** draws each piece as one solid with every edge bevelled to the same width, whatever its angle, on every grid but spheres. The STL export has **Uniform bevels** and **Bumpy** switches, off by default, and a large rhombic piece exports in a fraction of a second instead of about a minute.
- **Faster solving with many identical pieces.** The check that drops an assembly already found in another orientation now works on the solver's own rows, which makes puzzles with many pieces of one shape 4 to 6 times faster.
- **Every core busy to the end.** A thread that runs out of work takes over untried branches from a busy one, so the last part of a long solve no longer runs on a single core. A paused solve keeps its place and loses no work, on one thread too.
- **Crash recovery, always on.** While a solve runs, ShapeLogic keeps a rescue copy of the puzzle with its results and its place in the search, which the next start offers to reopen. It replaces **Autosave every 20 min**: your puzzle's file is never written unless you save it.
- **Search Options defaults.** **Settings ▸ Search Options** sets what the Solver's Search Options start at for a problem that has none of its own, including the solver type. Every setting now shows its default under its description.
- **Release notes in the update dialog.** When a new version is out, **Details** shows what is new in it.
- **Sliding puzzles.** Each piece's name is written on the Start and Goal grids, the piece previews under the trays grow to fill the space and keep their labels apart, and the rulers and the cursor's coordinates count from 1.
- **More in the guide.** Pages for the two new grids, crash recovery and setting the defaults, and the new ShapeLogic logo on the website.

Many small bug fixes and improvements.

## [ShapeLogic v1.6](https://github.com/shapelogicxyz/shapelogic/releases/tag/v1.6) (1 November 2026)

### What's new

- **Problems on sliding puzzles work as on other puzzles.** You make, copy and delete a sliding puzzle's problems yourself and choose a board as each one's result. **Clone start position to goal position** (and back) copies a board's pieces from one side to the other.
- **Color restrictions on sliding puzzles.** A sliding puzzle's start and goal marks no longer use the cell colors, so restriction colors, the **Paint** tool and the color rules work as on every other grid. They limit where a free piece may start. Up to 255 pieces can have a start or goal (63 before). Files from earlier versions and from BurrTools convert as they open.
- **Pieces that can turn.** The new **Allow any orientation for unplaced pieces** option lets a sliding piece with no start or goal be placed in any quarter turn, and **Include mirrored orientations** lets it be placed mirrored too. A piece never turns as it slides. The command line tool has `--any-orientation` and `--mirrored-orientations`.
- **Bigger sliding searches.** The Sliding Puzzle Solver keeps its memory use level and writes the rest to disk, so the free disk space is what limits it. When the goal is a single arrangement it also searches back from the goal. As the disk fills, it keeps fewer of the older moves and works out the missing ones again at the end.
- **Pause and carry on a sliding search.** **Pause** saves the search under way and **Continue** carries it on. A search that fills its disk space is saved too and the solve pauses: give it more **Sliding disk space**, or free some disk, and press **Continue**. It always stops while at least 100 MB and the room to save itself are still free.
- **Clearer messages.** The Puzzle tab's fit summary no longer warns that pieces can't reach a sliding board, and stacking messages show large numbers with thousands separators.
- **More in the guide.** New pages on the testing suites and on setting environment variables for the code base, the command line tool's sliding options, and wider pages on large windows. The website has a research section with the golden rhombus puzzles and the Panex solver white paper.

Many small bug fixes and improvements.

## [ShapeLogic v1.5](https://github.com/shapelogicxyz/shapelogic/releases/tag/v1.5) (29 October 2026)

### What's new

- **Keep the assemblies that don't come apart.** The new **Always show Assemblies** search option keeps every assembly the solver finds, even when it can't be taken apart, and lists them after the disassemblies. The solutions list now says what it holds: **Disassemblies**, **Assemblies** or **Solutions**, with a heading for each group.
- **An Export menu on the Solver tab.** The Solutions card's **Export ▾** menu exports the shown solution's pieces as STL, and has everything in **File ▸ Export** too: Puzzlecad, images, STL and the solution animation.
- **Clear a problem's result.** The **Change…** menu on the Puzzle tab ends with **Clear Result**, which leaves the problem without a result. It can be undone.
- **A Boards list for sliding puzzles.** On the Shapes tab, a sliding puzzle's boards have their own **Boards** list above the shapes, with the **+ New Board** button.
- **Start and goal side by side.** **Focus 2D editor** on a sliding puzzle's Puzzle tab shows the Start and Goal boards side by side. The focus stays on as you move between the Shapes and Puzzle tabs. On the Shapes tab, the editor's tools sit in one strip above the grid, so the grid gets all the room. A tip suggests the focus on large grids.
- **Smoother turning.** **Free Spin** turns the 3D view smoothly with the mouse and no longer jumps back when you drag past the edge. Outside the circle it rolls and tilts the view. The STL preview turns the same way.
- **A new top bar.** The top bar grows and shrinks with the font size and density, and the puzzle's name has more room. The new **Top bar theme** setting chooses **Colorful**, with the axis colors behind the ShapeLogic name and on the tabs, or **Boring**, a plain bar.
- **Easier to read.** The text is a size larger by default (**Medium**), the side cards grow with a wide window by default, and the right card is a little wider. In **Compact** density, the pieces in **In this puzzle** take one line each.

Many small bug fixes and improvements.

## [ShapeLogic v1.4](https://github.com/shapelogicxyz/shapelogic/releases/tag/v1.4) (28 October 2026)

### What's new

- **One sliding puzzle solver.** Sliding puzzles now have a single solver, the **Sliding Puzzle Solver**. It has no limit on the positions it looks at, so it always finds the fewest moves or proves there is no solution, and it uses every core. The Fast and Deep solvers, which stopped at 250,000 and 1,000,000 positions, are gone. Puzzles saved with them open as before.
- **Several copies of a sliding piece.** When a puzzle uses more than one copy of a piece, each copy can have its own start and goal. A copy without a start can start anywhere, and one without a goal can end anywhere.
- **Boards and pieces side by side.** On a sliding puzzle's **Puzzle** tab, the **START** and **GOAL** boards sit at the top of the 3D view and the pieces are shown under them. Starts and goals are placed on the new **Start and goal** card.
- **Clearer piece previews.** On the **Puzzle** tab the pieces are larger and closer together, the result sits larger at the top, and the columns fit the shape of the window. Zooming makes every piece larger or smaller where it stands.
- **Board colors that follow the theme.** Sliding boards are dark gray in the Light theme and light gray in the Dark theme, and change when you switch. **Settings ▸ Grid editor** sets the color for each theme. A board you color yourself keeps its color.
- **New look settings.** **Settings ▸ General** adds **Font size** (Small, Medium or Large), **Main tab bar location** (the Shapes, Puzzle and Solver tabs in the top bar, or down the left side), **Compact** density, and sidebar widths that are fixed or grow with the window. **View ▸ Color Theme** switches between Light, Dark and System.
- **Memory and disk for big searches.** **Settings ▸ Solver** sets how much memory and disk space a sliding or stacking search may use, so a large search can go further.
- **Easier editing.** The voxel editor can show the layer below (or above) faintly under the one you edit. **+ New Piece** and **+ New Board** take the size of the last piece or board.
- **About ShapeLogic** shows what ShapeLogic is built with, who makes it, and how to get in touch.

Many small bug fixes and improvements.

## [ShapeLogic v1.3.1](https://github.com/shapelogicxyz/shapelogic/releases/tag/v1.3.1) (26 October 2026)

### What's new in v1.3

- **Tidier sliding puzzles.** Boards (start/goal positions) now always stay at the top of the shapes list and are labelled **B1**, **B2** and so on. The pieces below them are **S1**, **S2** and so on. When you delete or move a board, the others are numbered again, so there are no gaps.
- **Name your boards.** A board shows **(START/GOAL 1)** until you rename it, and the new name also shows in the Puzzle tab's problems list. Boards no longer appear in **All pieces** and can't be added to a puzzle as pieces.
- **New Settings pages.** A **Grid editor** page holds **Grid plane** and a new **New shape size** setting (4 × 4 × 4 unless you change it). **Performance** is now called **Solver** and adds a **Default solver type**, Complete or Fast.
- **Quicker new shapes.** **+ New** makes the new shape the same size as the one you have selected.
- **Piece names in the Solver.** The **Piece display** chips show each piece's name, as in **S2 - my piece**. **Compact** switches back to labels only. You can also draw each piece's label on it in the 3D view.
- **Easier voxel editing.** You can type a size straight into the X, Y and Z boxes. The **XZ** grid now has X 0, Z 0 at the top left. Hovering over a voxel always highlights it in the 3D view, even when a click would not change it.
- **Simpler view settings.** **Z axis direction** now offers **Z Towards** and **Z Up**. **Rotation method** offers **Free Spin** and **Top Up**.
- **Enable High Memory is on by default** for sliding and stacking puzzles.
- **File dialogs remember where you were.** Open and Save start in the folder you used last, even after a restart.

Many small bug fixes and improvements.

### v1.3.1 (26 October 2026)

- **Update notifications.** ShapeLogic now checks for a newer version when it starts and tells you when one is out, with a link to download it. **Check for Updates…** (in the **Help** menu, or the **ShapeLogic** menu on macOS) checks at any time. You can turn off the check at startup in **Settings ▸ General**.
- **Time used counts the whole solve.** When you continue a solve, also after quitting or loading a paused solver state, **Time used** carries on from the time already spent instead of starting again at zero. Times are now shown in words, such as **5.5 minutes** or **2 hours**. **Time left** says **unknown** until the solver can estimate it.
- **A new shape size for sliding puzzles.** **Settings ▸ Grid editor ▸ New shape size** now has separate sizes for **Normal Burrs** (4 × 4 × 4) and **Sliding** (6 × 6 × 1).
- **The right version number.** **About ShapeLogic** now shows the version you installed.

## [ShapeLogic v1.2](https://github.com/shapelogicxyz/shapelogic/releases/tag/v1.2) (24 October 2026)

### What's new

- **Vertical moves are now an option for sliding puzzles.** On a tray with more than one layer, pieces now stay in their own layer unless you turn on **Allow Vertical Moves** in the Solver tab. Most multi-layer puzzles now solve much faster. Turn the option on for puzzles where a piece must be lifted over the others.
- **Easier 3D editing.** In 3D edit mode, clicking different parts of a voxel's face adds, changes or deletes it, and you can start drawing an empty shape right in the 3D view.
- **Piece weights are in the Puzzle tab.** The W− and W+ buttons are now in the Groups table, next to each piece.
- **New guide pages.** A new Piece Weights page, and a Code Base section for developers.

Many small bug fixes and improvements.

## [ShapeLogic v1.1](https://github.com/shapelogicxyz/shapelogic/releases/tag/v1.1) (23 October 2026)

These are the changes since the v1.0 release (19 October 2026).

### Highlights

- **Sliding puzzles can have more than one layer.** Pieces and trays are no longer flattened to a single layer, and pieces can move up and down as well as sideways.
- **A reworked color area in the voxel editor.** It has clear labels, direct buttons, a built-in color picker and a palette that holds many colors.

### New features

#### Sliding puzzles with several layers

- Sliding shapes keep their layers when a file is opened. Before, a sliding shape with more than one layer was made flat on open, which left the document with unsaved changes and showed a notice.
- The **Z** size stepper and the tools that add layers now work on sliding puzzles, for both pieces and Start/Goal shapes (trays).
- **New Start/Goal Positions** takes the full X, Y and Z size of the selected shape, or 6 by 6 by 1 if no shape is selected.
- A piece's start or goal covers every cell of the piece in every layer. To place a piece, show a layer of the tray. The outline under the mouse is the piece's slice on that layer, and the piece sits as low as it can while still fitting.
- On a tray with more than one layer, pieces can move up and down as well as in the four flat directions. This lets a piece be lifted out of a box, carried over the other pieces and put down somewhere else. Solution routes and the player follow these vertical moves.
- Puzzles with one-layer trays are searched and solved exactly as before, so their answers do not change. Pieces still only slide, never turn, so a goal that needs a piece turned cannot be reached.

#### Voxel editor color area

- The large swatch is labelled **SHAPE COLOR** or **RESTRICTION COLOR**. A shape color also shows **Default** or **Custom**.
- The "…" menu is replaced by three buttons: **+** (add a color), a pencil (edit the color) and a trash can (remove the color).
- An in-app color picker edits the current color. For the shape's own color it has a **Restore Default** button.
- A new restriction color is picked to look as different as possible from the colors already in the puzzle. When you remove a color, the one before it becomes the drawing color.
- The palette sits on rows below the swatch, with the labels **C1**, **C2** and so on. It shows one row at a time, and up and down arrows step between rows once there are two or more.
- In the grid, a voxel with a restriction color is drawn in the shape's color, with a small box in the restriction color in its top-left corner.
- On sliding and stacking puzzles, which have no restriction colors, the area shows only the shape's color and the pencil.

### Improvements

- **The last shape can be deleted.** The Delete button and menu item are no longer disabled when only one shape is left.
- **Clearer Solver settings for spheres, rhombic and tetra-octa puzzles.** ShapeLogic cannot take apart puzzles on these grids. The Solver workspace now sets them to **Assemble Only** and greys out **Assemble and Disassemble**, **Just Levels** and **Check Rotations**. Before, you had to untick these options yourself.
- Larger icons in the shape rows and the color area.
- The settings icon is now drawn as a gear.

### Documentation and website

- The user guide is updated for these changes:
  - **Shapes tab**: a new *Shape and restriction colors* section replaces *Drawing color*.
  - **Sliding puzzles**: a new *Trays with more than one layer* section.
  - **Spheres**, **Rhombic**, **Tetra-octa** and **Puzzle types**: these pages now describe the Assemble Only setting.
  - **Color restrictions** and **Puzzle tab**: links and wording updated.
- The website's landing page has a News section with the v1.0 release announcement and a contact address.
- The online demo is rebuilt with the new color area and multi-layer sliding puzzles.

### For developers

- **Engine API (sliding).** `sliding.addStartGoal` takes a `size` of `[x, y]` or `[x, y, z]`. `sliding.place`, `sliding.preview` and the other `sliding.*` methods take a cell `at` of `[x, y]` or `[x, y, z]`, where `[x, y]` means layer 0. `sliding.preview` returns cells as `[x, y, z]`. The old `Cell2` and `Size2` types are replaced by `TrayCell` and `TraySize`. Callers that send two-element arrays keep working.
- **Engine API (shape).** `shape.resize` no longer refuses a Z size other than 1 on sliding puzzles.
- **Document notice.** Opening a file no longer produces the "flattened" notice for sliding shapes.
- **Design record.** Decision 0014 (sliding puzzles) has a new section, *Shapes of more than one layer*.
- **Tests.**
  - New unit and contract tests cover multi-layer trays and vertical moves, using the new fixture `slide_layers_blocks.xmpuzzle`.
  - End-to-end tests no longer depend on the host machine. The app under test runs in sRGB with scrollbars hidden, so screenshots no longer change with the display, a connected mouse or the scrollbar setting. The reference screenshots were recorded again.
  - Shape counts come from the engine instead of a fixed list.
  - Picking targets are chosen to be robust on curved surfaces.

## [ShapeLogic v1.0](https://github.com/shapelogicxyz/shapelogic/releases/tag/v1.0) (19 October 2026)

This is the first public release of **ShapeLogic**, a designer and solver for interlocking, burr, sliding and stacking puzzles.

ShapeLogic is a complete rewrite: a new solver core, a new interface and a new documentation site. We've worked on it for a long time and are very excited to finally share it.

### 🌐 Visit [shapelogic.xyz](https://shapelogic.xyz)

The ShapeLogic website has:

- **A live demo:** [try ShapeLogic in your browser](https://shapelogic.xyz/demo/) with nothing to install. It opens a solved puzzle so you can explore the app before you download it.
- **Detailed documentation:** a [full user guide](https://shapelogic.xyz/docs/) covering designing shapes, setting up puzzles, solving and playing back disassemblies.
- **Download links** to the latest release for every platform.

### A word about BurrTools

ShapeLogic grew out of [BurrTools](https://burrtools.sourceforge.net/) by Andreas Röver and contributors. Its algorithms are ported from BurrTools, and it reads and writes the same `.xmpuzzle` files. **ShapeLogic is not meant to replace BurrTools or draw its community away.** BurrTools is still in development and remains an excellent tool. ShapeLogic is another option: it handles many of the same puzzles and adds puzzle types and modes that BurrTools doesn't have. Use whichever suits the puzzle in front of you, or both.

### What's different

- **Built for the web from the start.** The interface is written in web technologies and runs in Electron. The old FLTK interface and its supporting code are gone. The result looks and behaves the same on macOS, Windows and Linux, and displays quickly and reliably on all kinds of screens.
- **A rebuilt solver core.** The core is modern C++23 and runs as a separate engine process, so the interface stays responsive while a solve runs. It has two solvers: **Complete** for exhaustive searches and **Fast** (Andrew Crowell's take-apart). Both have been tested and benchmarked heavily for solving speed.
- **More kinds of puzzles.** Besides interlocking and burr puzzles, ShapeLogic designs, solves and plays back **sliding puzzles** and **stacking and Panex puzzles**. Long Panex searches can be paused and resumed later.
- **Imports and exports.** Import PuzzleSolver3D and Puzzlecad files, export to Puzzlecad, export STL meshes for 3D printing on every grid, and convert cube-grid puzzles to the rhombic and tetra-octa grids.
- **A large new testing framework.** Hundreds of unit and contract tests, end-to-end tests of the app's interface, and a comparison harness that checks every solver's answers against the original on a corpus of 180 puzzles. This catches regressions early and lets us add features with confidence.
- **A new website and documentation site** at [shapelogic.xyz](https://shapelogic.xyz), with a full user guide and a live demo you can try in your browser.

### Feedback

This is a first release, and we'd love to hear from you. Bug reports, feature ideas and puzzles that trip up the solver are all welcome.

- Open an issue: https://github.com/shapelogicxyz/shapelogic/issues
- Email questions, issues or feedback to **shapelogic.xyz@gmail.com**
- Learn more, read the docs and try the demo at **https://shapelogic.xyz**

Thank you for trying ShapeLogic, and happy puzzling! 🧩

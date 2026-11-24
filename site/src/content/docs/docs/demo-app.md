---
title: Demo App
description: Try ShapeLogic in your browser, with a solved puzzle of each type to explore.
---

The [demo](/demo/) is ShapeLogic running in your browser, with nothing to
install. It opens a solved puzzle, the Pelikan burr, so you can look around
the app and see how it works before you download it.

The list at the top right opens a solved example of each puzzle type:

| Puzzle type | Example |
| --- | --- |
| Brick | Pelikan burr |
| Sliding (standard) | Kuroko Dairu |
| Sliding (nested) | 1 to 6 Heart |
| Stacking | Panex Swap, with 3 discs per tower |
| Tilted Square | Triple Play, by Goh Pit Khiam and Jerry Loo |
| Triangular Prism | Prisgon |
| Spheres | Ball Room |
| Rhombic | Diagonal Cube |
| Tetrahedra-Octahedra | Four Piece Tetrahedron |
| Hexagonal Prism | a honeycomb slab |
| Cairo Prism | a pentagon slab |

Each example has one problem and only the shapes it uses. The demo's
address keeps the type and the tab you chose (for example
`/demo/?type=sliding-nested&tab=play`), so you can link straight to one.
The types are `brick`, `sliding-standard`, `sliding-nested`, `stacking`,
`tilted`, `prism`, `spheres`, `rhombic`, `tetraocta`, `hexagonal` and
`cairo`; the tabs are `shapes`, `puzzle`, `solver` and `play` (Play only for
the sliding and stacking examples). The list beside **Reset** switches between
Light Mode and Dark Mode.

## What it is

The demo is a functional model of the app. It is the same interface as the
desktop version, and it carries enough of the app's backend logic to make
building puzzles work for real:

- **Shapes tab:** draw and erase voxels, flip, rotate and nudge pieces, make
  new shapes, rename them and change their colors. The 3D view rotates, pans
  and zooms, and every display style is there.
- **Puzzle tab:** make new problems, choose their result and add pieces.
- **Solver tab:** change the options and play the disassembly of the saved
  solution, move by move. Spheres and Tilted Square puzzles only assemble:
  the Ball Room and Triple Play show their assemblies.
- **Play tab** (Sliding and Stacking): move the pieces or discs yourself
  from the start to the goal.

Undo and redo work as in the app.

## What it is not

The solver is not in the demo. Pressing **Solve** does not search for
solutions: it plays a simulated run, with the progress bar and counters
moving for a few seconds, and then shows the puzzle's saved solution again.
The solution, its disassembly and the numbers in the run are static data
that came with the puzzle, not results worked out in your browser.

So the demo always shows the example's saved solution, whatever you change.
Editing a piece of the puzzle clears its solutions, as it does in the app;
**Solve** brings them back. The other searches (Prepare, Continue, Analyse
and the step-by-step explorers) are turned off.

Nothing you do in the demo is saved. Opening and saving files, imports and
exports are not available. Choosing another puzzle type drops your
changes, and so does **Reset** at the top right, which starts the example
again. **Exit** returns to the front page.

To design puzzles of your own and solve them,
[download ShapeLogic](https://github.com/shapelogicxyz/shapelogic/releases/latest)
(see [Install](/docs/install/)).

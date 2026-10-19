---
title: Demo App
description: Try ShapeLogic in your browser, with a solved puzzle to explore.
---

The [demo](/demo/) is ShapeLogic running in your browser, with nothing to
install. It opens a solved puzzle, the Pelikan burr, so you can look around
the app and see how it works before you download it.

## What it is

The demo is a functional model of the app. It is the same interface as the
desktop version, and it carries enough of the app's backend logic to make
building puzzles work for real:

- **Shapes tab:** draw and erase voxels, flip, rotate and nudge pieces, make
  new shapes, rename them and change their colors. The 3D view rotates, pans
  and zooms, and every display style is there.
- **Puzzle tab:** make new problems, choose their result and add pieces.
- **Solver tab:** change the options and play the disassembly of the saved
  solution, move by move.

Undo and redo work as in the app.

## What it is not

The solver is not in the demo. Pressing **Solve** does not search for
solutions: it plays a simulated run, with the progress bar and counters
moving for a few seconds, and then shows the puzzle's saved solution again.
The solution, its disassembly and the numbers in the run are static data
that came with the puzzle, not results worked out in your browser.

So the demo always shows the Pelikan burr's solution, whatever you change.
Editing a piece of the puzzle clears its solutions, as it does in the app;
**Solve** brings them back. The other searches (Prepare, Continue, Analyse
and the step-by-step explorers) are turned off.

Nothing you do in the demo is saved. Opening and saving files, imports and
exports are not available. **Reset** at the top right starts the demo
again, and **Exit** returns to the front page.

To design puzzles of your own and solve them,
[download ShapeLogic](https://github.com/shapelogicxyz/shapelogic/releases/latest)
(see [Install](/docs/install/)).

---
title: Sliding puzzles
description: Flat pieces in a tray, moved from a start position to a goal position.
---

Pieces are flat shapes in a tray. Mark where each piece starts and where it
must end, and the solver finds the fewest moves that slide them from start to
goal, one piece at a time. Pieces can carry others nested inside them when that
is allowed.

![Sliding puzzle by GiiKER Super Slide, a Sliding puzzle.](../../../../assets/grids/sliding.png)

*Sliding puzzle by GiiKER Super Slide.*

You can try one now: download [slide_klotski.xmpuzzle](/examples/slide_klotski.xmpuzzle),
a Klotski-style puzzle of ten pieces that the solver finishes in 81 moves.

## How a sliding puzzle is made

A sliding puzzle has two kinds of shape:

- **Pieces** are shapes one layer thick. They are the things that slide.
- A **Start/Goal shape**, or **tray**, is a one-layer shape that describes the
  board. Its **fixed** cells are floor, its **variable** cells are corridors
  (pieces can cross them but not stop on them) and its **empty** cells are
  walls.

Each piece gets a **start** and a **goal** position, stamped onto a tray: the
whole piece at a time. A tray is listed as `S1 (START/GOAL 1)` and has a
problem of its own.

## Shapes: build the tray and place the pieces

![The Shapes workspace for a sliding puzzle: a tray with its start and goal views, and the Tray edit switch.](../../../../assets/screens/sliding-entities.png)

1. Draw each piece on its own shape, one layer thick.
2. Press **New Start/Goal Positions** above the shapes list to add a tray. It
   takes the size of the selected shape's X and Y, or 6 by 6 if there isn't
   one. **Delete Start/Goal Positions** removes a tray.
3. With the tray selected, the right card has a **Tray edit** switch:
   - **Walls** draws the tray with the usual tools: **Fixed** is floor,
     **Variable** is corridor and **Erase** is wall.
   - **Start** and **Goal** place a piece. Choose the piece from the list,
     then click where it goes. It follows the mouse: green where it fits, red
     where it does not, orange where it already is. Click again on it to take
     it off, or right-click a piece to remove it.
4. The 3D view shows two trays side by side, **START** and **GOAL**. A bar
   under it reads green (*Valid Start/Goal positions…*) or red (*Invalid
   Start/Goal positions - …*, with the reason).

Sliding puzzles use the XY plane only, and have no colour constraints.

## Puzzle: nothing to choose

On the **Puzzle** workspace the tray is the result, and a piece that has a
start or goal keeps its count. A tray's problem follows its stamps, so you do
not add or copy problems by hand.

## Solver: find the shortest slide

![The Solver after finding the 81-move solution of a Klotski puzzle.](../../../../assets/screens/sliding-solver.png)

For a sliding puzzle, **Find Solutions** is selected by default. It keeps a
start layout only when each marked piece can slide from its start cell to its
goal cell. Press **Solve**: the solver searches breadth first and reports the
fewest moves. The **Solutions** list shows the number of moves, and the
**player** replays them, each piece following its route around corners.

The search options include nested slides and partially nested slides, which
let a piece carry another inside it. **Solve** and **Prepare** stay off while
the start and goal are invalid, with the reason in the button's tooltip.

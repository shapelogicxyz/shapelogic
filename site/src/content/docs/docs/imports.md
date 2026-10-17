---
title: Imports
description: "Everything in the File ▸ Import menu: PuzzleSolver3D files, Puzzlecad (OpenSCAD) files and paused solver states, and opening BurrTools .xmpuzzle files."
---

**File ▸ Import** reads puzzles made by other programs, and solves that were
paused on another computer. Each import replaces the puzzle that is open. If
it has unsaved changes, ShapeLogic asks first.

An imported puzzle has **no file yet**. Its name in the title bar comes from
the imported file, and **Save** asks where to write it as a ShapeLogic file.
ShapeLogic never writes over the file you imported from.

The File ▸ Import menu has three items:

| Item | Reads | Makes |
|---|---|---|
| **PuzzleSolver3D…** | a PuzzleSolver3D file (`.puz`) | a brick puzzle with one problem |
| **Puzzlecad (OpenSCAD)…** | a Puzzlecad OpenSCAD file (`.scad`) | a brick puzzle with one problem |
| **Paused Solver State…** | a ShapeLogic paused solve (`.slsolve`) | the puzzle, ready to carry on its solve |

## BurrTools puzzles: use File ▸ Open

BurrTools puzzle files (`.xmpuzzle`) need no import: open them with **File ▸
Open** (`Ctrl+O`, `⌘O` on a Mac). ShapeLogic uses the same file format, and
BurrTools can open the files ShapeLogic saves.

## PuzzleSolver3D…

Reads a puzzle in the text format of the **PuzzleSolver3D** program (`.puz`).
The file lists each piece and the result as blocks of characters: a size
line (`PIECE` or `RESULT`), then the shape's rows, with its layers side by
side.

The import makes a new **brick** (cube grid) puzzle:

- each piece of the file becomes a shape, and the result becomes a shape;
- one problem is made, with that result and **one copy of each piece**.

Check the counts on the [Puzzle tab](/docs/puzzle-tab/) afterwards if the
original puzzle used several copies of a piece.

## Puzzlecad (OpenSCAD)…

Reads an **OpenSCAD** file written for **Puzzlecad**, the OpenSCAD library
for designing and 3D printing puzzles (`.scad`). ShapeLogic reads the pieces
in the file's `burr_plate` and `burr_piece` calls. The result is the file's
first `packing_box`. A file without one gets a cube of **variable** voxels
as its result, which you can then edit into the result you want.

Like the PuzzleSolver3D import, it makes a brick puzzle with one problem and
one copy of each piece.

A file that can't be read gives the message **Could not load puzzle,
sorry!**

## Paused Solver State…

Opens a **`.slsolve`** file made with **File ▸ Export ▸ Paused Solver State…**
(see [Exports](/docs/exports/#paused-solver-state)). The file holds the
puzzle and a solve that was paused part way. Use it to move a long solve to
another computer, for example a faster one, or to share a solve in progress.

After importing, go to the **Solver** tab, pick the problem, and click
**Continue**. The solve carries on from where it was paused. The imported
puzzle is untitled; save it if you want to keep the results.

For a **stacking** puzzle the file carries the Panex search itself, packed.
It is unpacked into ShapeLogic's working folder, replacing a saved search of
the same problem that was there.

## Size limits

To protect your computer from damaged or hostile files, an import whose
shapes would hold more than about 134 million voxels together is refused.
Real puzzles come nowhere near this.

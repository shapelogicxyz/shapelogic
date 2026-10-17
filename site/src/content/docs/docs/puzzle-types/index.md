---
title: Puzzle Types
description: The seven kinds of puzzle ShapeLogic handles, and how to choose one.
---

When you choose **File ▸ New**, ShapeLogic asks which kind of puzzle to make.
The choice sets the **space grid**: the shape of the cells that every piece
in the puzzle is built from. All shapes in one puzzle share a grid, and the
grid has to be chosen before you start, so pick it first.

| Type | Pieces are made of | Typical puzzles |
|---|---|---|
| [Brick](/docs/puzzle-types/brick/) | Cubes | Burrs, packing puzzles |
| [Triangular Prism](/docs/puzzle-types/triangular-prism/) | Prisms with a triangular base | Puzzles built on triangular layers |
| [Spheres](/docs/puzzle-types/spheres/) | Balls in the tightest packing | Pyramid and ball packing |
| [Rhombic](/docs/puzzle-types/rhombic/) | Small tetrahedra cut from cubes | Rhombic dodecahedra, stellations |
| [Tetrahedra-Octahedra](/docs/puzzle-types/tetra-octa/) | Tetrahedra and octahedra | Tetrahedron and octahedron puzzles |
| [Sliding](/docs/puzzle-types/sliding/) | Flat pieces in a tray | Klotski, 15-puzzle style puzzles |
| [Stacking](/docs/puzzle-types/stacking/) | Discs on rods | Tower of Hanoi, Panex |

The workflow is the same for every type: draw the pieces on the **Shapes**
workspace, set up the problem on **Puzzle**, and solve it on **Solver**. See
[Getting started](/docs/getting-started/) for that tour. Each page below says
what is different about its type.

## What the solver can check

For **Brick** and **Triangular Prism** puzzles the solver finds every way the
pieces fill the result, then checks which assemblies can really be taken apart
and shows how.

For **Spheres**, **Rhombic** and **Tetrahedra-Octahedra** puzzles there is no
disassembler. The solver finds every assembly but cannot check whether it can
be taken apart. Untick **Assemble and Disassemble** on the Solver workspace
before solving; the Solutions count then stays empty and the assemblies are
what you get.

**Sliding** and **Stacking** puzzles are solved a different way: the solver
finds the fewest moves from a start position to a goal position.

:::note
The grid cannot be changed after a puzzle is made. A puzzle can sometimes be
converted to another grid without loss, but plan on choosing the right grid
at the start.
:::

# 0025: Movement checks derived from the cells; the take-apart on every grid of polyhedra

**Status:** accepted (idea 4 of docs/V2-IDEAS-PLAN.md);
`core/shapelogic/mesh/cell_sweep.h`,
`core/shapelogic/solve/movement_checks.h`,
`core/shapelogic/solve/movement_cache.h` (`MovementCacheDerived`),
`core/shapelogic/grid/grid_type.cpp` (the capabilities)

## Context
The take-apart asks one question of a grid: how many steps can this piece
move in this direction before it runs into that one. The original answers
it for cubes and triangular prisms with tables made by hand
(`movementcache_0.cpp`, `movementcache_1.cpp`): the directions, and for
each direction and kind of cell the cells that must be empty for one step.
It has no such tables for the rhombic and tetra-octa grids, so those were
assembled and never taken apart, and the hexagonal and cairo grids
(decision 0023) came without them too.

BurrTools V2 declares the movement vectors of a grid in its script and
computes the cells: it shrinks a cell a little, sweeps it along the vector
(a Minkowski sum) and asks a convex-intersection routine about every cell
nearby (`src/lib/grid.cpp`, `calculateMovementChecks`). Its rhombic and
octahedral grids are taken apart that way.

The cells of every grid as convex polyhedra exist since idea 2
(`mesh/cell_geometry.h`). What was missing was the sweep, the tables, and
a movement cache that reads them. And there is no original to compare the
answers with.

## Decision
**The sweep test** (`mesh/cell_sweep.h`: `sweepEnters`, `sweptCells`). A
cell moved in a straight line covers the convex hull of the cell and the
moved cell. It passes through another cell exactly when no plane separates
the two, and the planes to try are few (the separating axis theorem): the
faces of either cell, the planes along an edge of the cell and the move,
and the planes along an edge of each. Two bodies that overlap by less than
1e-4 along some direction only touch (the cells are about 1 across, their
corners lie on a lattice). So nothing is shrunk, and a cell that slides
along a face of another is told from one that cuts its corner by the
geometry alone. `sweptCells` refuses a step that does not move the grid
onto itself (the cell at the end is not the moved cell).

**The directions** (`solve/movement_checks.h`: `movementDirections`).
Pieces that lie face to face can only slide along the faces they share, so
the directions worth trying are those of the cells' edges, where two face
planes meet. One step is the shortest move along a direction that puts the
grid onto itself. With each list goes the box after which the grid
repeats: a cell's position in that box is its kind.

| Grid | Period | Directions (each both ways) |
|---|---|---|
| Brick | 1 x 1 x 1 | 3: the axes (the original's) |
| Triangular Prism | 2 x 2 x 1 | 4: up, and the triangle's three sides (the original's) |
| Rhombic | 5 x 5 x 5 | 13: the cube's edges, the diagonals of its faces, its space diagonals (V2's) |
| Tetrahedra-Octahedra | 6 x 6 x 6 | 9: the diagonals of the cubes' faces, one cube; the axes, two cubes (V2's) |
| Hexagonal Prism | 1 x 1 x 1 | 7: up; across the three sides to the next cell; along the three sides to the next cell but one |
| Cairo Prism | 4 x 4 x 1 | 5: up; (2, 2), (2, -2), (4, 0), (0, 4) in the plane |

The hexagonal and cairo grids have no movement vectors in V2; theirs are
new. A hexagon's move across a side is no edge direction, but it is the
shortest move there is and a prism with nothing beside its way can make
it (the checks ask for the two cells beside the way as well as the one
moved to). A pentagon's sides run in no direction the cairo grid repeats
in, so in the plane the grid gets its shortest moves and the checks say
where a piece has room for them; most take-aparts there lift pieces out.

**The tables** (`MovementChecks`, `deriveMovementChecks`,
`movementChecks`). For each direction and each position in the period box
that is a cell: the offsets of every cell the cell passes through in one
step, the one it ends in among them. A grid's table is derived the first
time it is asked for (about 20 ms for the rhombic or the tetra-octa
grid) and kept: function-local
statics, immutable once made, shared by every thread. `extent` is how far
a check reaches, so that the cache knows when a piece has left another
for good.

**The cache** (`MovementCacheDerived`). The walk of the prisms' cache
(`movementcache_1.cpp`) over the table's directions and cells; V2's
`MovementCache` does the same. `makeMovementCache` gives it to every grid
with a table but cubes and triangular prisms, which keep the original's.
The movement analysis, the disassembler, the hashes and the solution's
moves needed no change: they already took the number of directions and
each direction's step from the cache.

**The capabilities.** The rhombic, tetra-octa, hexagonal and cairo grids
have `kCapDisassemble`. Everything that offers or refuses a take-apart
asks the grid (`Disassembler::canDisassemble`): the CLI's `-d`, the solve
session, the explorers, Analyse, `engine.info`, the Python module. So File
▸ New's warning, the Solver's greyed "Assemble and Disassemble", Movements
and Analyse follow without code of their own.

**What stays as it was.**
- Spheres: balls have no faces to slide along, and a ball can leave
  between others in more ways than a list of directions holds. Still
  assembled only.
- Turns: the rotation take-apart is the original's, for cubes.
- The disassembler's search, and with it what counts as a solution: a
  puzzle is solved when it comes apart by moves along the grid's
  directions in whole steps.

**Whole steps on the rhombic grid.** V2 cuts each of our rhombic cells in
two, and its grid repeats after half a space diagonal of the cube; ours
repeats after the whole diagonal. A piece that must stop halfway along a
space diagonal and go on another way is found by V2 and not by this
take-apart. For the other twelve directions and for the tetra-octa grid
the steps are V2's.

**The A/B switch.** `MovementAnalysator::Options::derivedChecks`
(`--derived-checks`) gives cubes and triangular prisms the derived cache
in place of the original's tables. The answers are the same; it exists so
that the derived checks, the only ones the other grids have, can be run
against the original.

## How it is tested without an original
- The sweep: by hand on cubes, and on every grid against points sampled
  in the moved cell along its way (`tests/unit/mesh/test_cell_sweep.cpp`).
- The tables of cubes and triangular prisms are the original's, cell for
  cell (`test_movement_checks.cpp`); the hexagon's are checked by hand;
  a search twice as wide finds no further cell on any grid.
- **The tetra-octa and rhombic tables are BurrTools V2's.**
  `tests/oracle/v2_movement_checks.py` builds a small tool against V2's
  library and records what it computes
  (`tests/data/movement_checks_by_v2.txt`); the test maps V2's cells onto
  ours by their centroids and compares every cell of the period box in
  every direction (360 and 312 comparisons; on the rhombic grid V2's two
  halves of a cell together, and two of its half steps for one of ours
  along the space diagonals).
- The cache: its values are the ported caches' on cubes and prisms, and
  on every grid what moving the cells themselves step by step allows
  (`freeSteps` in `movement_test_helpers.h`, which uses the sweep test
  and neither the tables nor the cache).
- The take-apart (`test_derived_disassembly.cpp`): every take-apart found
  is played back on the cells and no piece may pass through another; a
  breadth-first search written for the tests (`ReferenceTakeApart`) must
  need as many moves at every node, and must find no way apart where the
  take-apart finds none: on the original's rhombic and tetra-octa
  examples, and on random puzzles of two and of several pieces on all
  four grids, where it asks the cells and not the cache. The same search
  agrees with the ported take-apart on cube and prism fixtures, which is
  the check of the checker.
- Cubes and prisms come apart with the derived checks as with the
  original's tables, in the unit tests and through the harness: all 180
  corpus puzzles match the original with `--derived-checks`, every field,
  on one and on all threads, and with Crowell's take-apart the 178 the
  original answers.
- The harness's corpus marks the eight rhombic and tetra-octa examples
  `AD`: the original only assembles them, so the assemblies are compared
  and solutions are asked for; the solved-file, text and Python
  comparisons run them without the take-apart, as before.
- The engine's contract suite analyses, solves, plays back and explores
  a rhombic fixture (`derived_take_apart`).

## Consequences
- Rhombic, Tetrahedra-Octahedra, Hexagonal Prism and Cairo Prism puzzles
  are solved with levels and moves, their solutions play back, and
  Movements and Analyse work on them. The original's library reads a
  rhombic or tetra-octa file solved this way and writes it back
  unchanged, separations and all; it could not have made them.
- The original's examples on these grids are stored solved, with
  assemblies only. They stay so when opened: Analyse (the missing
  disassemblies) or a new solve takes them apart.
- Of the original's eight examples on these grids all have solutions:
  HexSticks 2 of 33 assemblies, PiecesOfEight 5 of 6, the others every
  assembly or one of two.
- A new grid of polyhedra gets a take-apart from a list of directions and
  its period.
- Only Spheres is left with the "no disassembler" warning.

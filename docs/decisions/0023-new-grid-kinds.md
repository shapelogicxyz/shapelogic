# 0023: New grid kinds: hexagonal and cairo prisms

**Status:** accepted; `core/shapelogic/grid/grid_type.h` (`GridKind::Hexagonal`
= 7, `GridKind::Cairo` = 8), `core/shapelogic/grid/voxel_hex.h`,
`core/shapelogic/grid/hex_lattice.h`, `core/shapelogic/grid/voxel_cairo.h`,
`core/shapelogic/grid/cairo_lattice.h`,
`core/shapelogic/grid/symmetries_derived.h`,
`core/shapelogic/mesh/grid_geometry_7.cpp`, `grid_geometry_8.cpp`,
`core/shapelogic/app/layer_geometry.cpp`, `core/shapelogic/app/shape_tools.cpp`,
`api/types.json` (`Grid`), `web/src/renderer/shell/new-grids.ts`,
`web/src/renderer/view3d/extent.ts`

## Context
BurrTools V2 added hexagonal and cairo prism grids as Lua scripts over
its generic grid machinery (docs/V2-IDEAS-PLAN.md, idea 2). This core
keys everything on `GridKind`: a voxel class per grid with its neighbour,
transformation and alignment code, symmetry tables generated offline, mesh
tables per grid, the editor's layer geometry, the shape tools, the file
format's `type` number, the engine's grid names and the front end's lists.
The original has no such grids, so there is nothing to compare them with.

## Decision
**Two grid kinds, 7 and 8, assembled but not taken apart** (as the rhombic,
tetra-octa and sphere grids): their capabilities are assemble and STL.

**Coordinates that make every placement a plain addition.** The hexagonal
grid uses axial coordinates: cell (x, y) is the cell x along row y, each
row half a cell to the right of the one below, so every integer move is a
symmetry of the honeycomb and `onGrid` is always true; a shape's space is
a parallelogram of cells. (Row-offset coordinates, where every other row
is shifted, were tried first and dropped: a move by one row is a symmetry
of the grid but changes the coordinates' pattern, so the assembler could
not have placed a piece on an odd row.) The cairo grid uses V2's
coordinates: the integer points with x + y odd, four kinds of pentagon,
the lattice spanned by (2, 2) and (2, -2); `onGrid`, `identicalInBB`,
`minimizePiece`, `resizeInclude` and the shape tools move by lattice
vectors only, as the cut-cube grids move by blocks.

**Symmetry tables derived at first use, not generated offline.**
`DerivedSymmetries` takes a grid's matrices (the turns about z, each also
turned over, then all mirrored: 24 for the hexagonal grid, 16 for the
cairo), builds the composition table from their products, enumerates every
subgroup as the symmetry groups (the identity group first, so
`unSymmetric` holds), and works out each group's unified, unique and
minimizer tables by the original generator's rules (as `symmetries_2.cpp`
does for the sphere grid's new lists). A shape's group is found by trying
every transformation. The hexagonal grid comes out with 54 groups, the
triangular prism grid's count, which checks the generated tables as well.
`Symmetries::getNumSymmetryGroups()` is new, for the tests.

**Transformations through the lattice.** `transformPoint` turns a cell's
centre by the matrix about the centre of cell (0, 0, 0) (hexagonal) or the
4-fold centre at the lattice point (0, 0) (cairo) and finds the cell there.
On the cairo grid the mirrors and the turns over are glides of the tiling,
so an offset per transformation, found once from the pentagons, puts them
on the right cells. The grid oracle test checks all of this as it checks
the ported grids.

**Meshes by the uniform mesher.** The new grids have no bevel tables
(`connectionFace` gives their faces only); every style of the 3D view and
the STL export use the uniform mesher (decision 0022) on them. The cairo
grid's corners are whole numbers plus multiples of one irrational offset,
so the editor's layer geometry keys shared corners on that pair of
numbers.

**Not in the original.** Files with `type` 7 or 8 cannot be opened by
BurrTools. Nothing of the oracle harness covers these grids; their tests
are invariants (the oracle test, the grid invariants) and a brute-force
count of domino tilings that the assembler must reproduce.

## Consequences
- File ▸ New lists Hexagonal Prism and Cairo Prism after the others; the
  user guide has a page for each and the demo carries them.
- The front end's cell-to-mesh map (`cellToMesh`) is a linear map rather
  than a scale per axis, for the hexagonal shear.
- Scale tools (Double, Triple, grid scale) are not offered on the new
  grids; nor is grid conversion.
- A derived movement analysis (idea 4) would give these grids a
  disassembler; until then they are assemble-only.

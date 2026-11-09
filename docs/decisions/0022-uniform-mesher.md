# 0022: The uniform mesher

**Status:** accepted; `core/shapelogic/mesh/uniform_mesh.h`,
`core/shapelogic/mesh/voxel_mesh.h` (`MeshStyle::Uniform`),
`core/shapelogic/mesh/stl.cpp` (the "Uniform bevels" and "Bumpy"
switches), `api/types.json` (`MeshStyle`), `web/src/renderer/view3d/toolbar.ts`,
`web/src/main/menu.ts`

## Context
The meshes of the 3D view and of the STL export are ported per grid: the
view's bevelled cells come from a corner table per grid
(`tabs_*/meshverts.inc`), in which bevel and offset are fixed directions,
so a bevel is wider or narrower depending on the edge's angle; the STL
export uses a generated chamfer table on the cube grids and Minkowski
sums through Manifold on the prism, rhombic and tetra-octa grids
(decision 0016), which take seconds on large shapes and have a known
needle case. BurrTools V2, the original author's rewrite, has one mesher
for every grid (`src/lib/polygen.cpp`): it reads a grid's convex
polyhedra and the ring of cells around each edge from the grid's
registration and makes bevels, gaps, corners and a "bumpy" look from
them (docs/V2-IDEAS-PLAN.md, idea 1).

## Decision
**One more mesher, beside the ported ones, never instead.** The ported
meshers stay as they are and keep giving the original's bytes; the
uniform mesher is a new style of the view and two new switches of the
STL export, both off by default.

**Everything from the cells' face polygons.** This core has no lattice
description of a grid, so the mesher takes what every grid already
gives: `connectionFace` at bevel and offset zero (the exact face
polygons, wound counter clockwise seen from outside, coinciding exactly
between neighbours) and `getNeighbor`. From those, per cell, it finds
the two faces across the edges at each corner and intersects the three
planes, moved in by one unit, for the bevel and offset directions,
scaled by the sine of half the angle between the faces so that the
chamfer is the same width at every edge: the width of the ported
meshers' bevel at a right-angle edge (the cut into each face there is
the bevel value, so a cube comes out as before), kept at every other
angle. The cells around an edge are walked
from face to face through the filled neighbours (the other face of the
cell at the edge, then the neighbour across it, until an empty cell):
the sum of the cells' angles tells convex from concave, and the face at
the other end of the run is the one the bevel or gap strip joins to.
Each strip is drawn once, by the face that sees the edge run upwards in
lexicographic order. The strips' loose sides are chained into the
polygons that close the corners, by vertex id rather than by position
(V2's `HoleHandler` matched positions within a tolerance); a corner that
is not flat is snipped into the triangles that are, then fanned around
its centre. Vertices are shared through a position lookup with a
tolerance of 1e-5, as the corners are computed, not read from tables.

**The STL export stretches first.** With unit sizes in y or z the cells
are stretched before the bevels are worked out, as V2 does, so the
bevels keep their width; the ported meshers scale the finished mesh.

**Limits.** Convex cells with no gaps and exactly coinciding shared
faces (every grid but the spheres; the mesher refuses others, and the
view then shows the drawing mesh). The checker is by cell parity, as
the drawing mesh's.

## Consequences
- The view has a fifth style, "Uniform bevels": each piece one solid
  with bevels of one width on every grid, concave edges bevelled too;
  the gap between neighbouring cells lies flat in their faces (as in
  V2's view), so only the checker shows the cells. The STL dialog has
  "Uniform bevels" and "Bumpy". The defaults
  of both give exactly what they gave before.
- The STL export of a large rhombic or tetra-octa shape with "Uniform
  bevels" on takes a fraction of the Minkowski mesher's time and needs
  no Manifold.
- The mesher's output has no original to compare with: its tests check
  that the meshes are watertight (every half-edge twinned, no hole
  faces, Euler characteristic 2 for a cell), outward, as wide as asked,
  and that the volume moves the right way.
- The cell geometry it derives (faces, normals, the cells around an
  edge) is what ideas 2 to 4 of docs/V2-IDEAS-PLAN.md would build on.

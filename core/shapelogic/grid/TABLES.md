# Generated grid tables

`tabs_0` (cubes, also used by the rhombic and tetra-octa grids), `tabs_1`
(triangular prisms) and `tabs_2` (spheres) are copied unchanged from the
original `src/lib/tabs_N/`, where the generator programs (`generator_N.cpp`)
that made them still live:

| File | Contents |
|---|---|
| `tablesizes.inc` | transformation and symmetry-group counts |
| `rotmatrix.inc` | the rotation/mirror matrix of every transformation |
| `transmult.inc` | `transAdd`: the composition of two transformations |
| `symmetries.inc` | every symmetry group as a set of transformations |
| `unifiedsym.inc` | the groups used for symmetry intersection |
| `transformmini.inc` | `minimizeTransformation` per group |
| `uniquesym.inc` | `isTransformationUnique` per group |
| `symcalc.inc` | the decision tree that finds a shape's group (not for spheres) |

`tabs_2` has no `symcalc.inc` (removed, as in the original): the sphere
transformations do not form a group, so the generated groups are not every
list a shape can have, and the tree answered wrongly for one outside them.
`symmetries_2.cpp` works out a shape's whole list and looks it up in a
registry seeded from these tables, appending a list it has not met with
its tables derived by the generator's rules (`deriveSphereTables`).

`symcalc.inc` is the only file changed, and only in syntax. The original
trees create and delete voxel spaces by hand; here every test node is
`if (same(t)) {`, where `same` is defined by the including function with the
original's exact check (transform a copy by `t`, compare it with
`identicalInBB`). The conversion was done with sed and checked
to keep every transformation number, every result and the brace structure in
the same order. The mesh tables (`meshverts.inc`, `tabs_3`, `tabs_4`) come
over with the meshes in stage 6.

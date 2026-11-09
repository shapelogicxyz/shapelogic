# Plan: ideas from BurrTools V2

## Context
BurrTools V2 ("BurrTools NG", versions 0.1 to 0.3, 2026; a local checkout
sits in `/Users/colin/git/burr-tools/burr-tools-v2_andreas`) is the
original author's rewrite of BurrTools. Its author says it offers "a lot
of interesting new possibilities concerning the grids with much nicer mesh
generation". This plan records what that means against ShapeLogic, and
five ideas worth taking, each with what we have, what V2 has, what a user
would see, and the difficulty. It was written on 2026-11-08 from a reading
of both codebases; nothing here is scheduled.

**What V2 changed.** In the original and in ShapeLogic a grid is code: one
`Voxel` subclass per grid with hand-written neighbour tables, symmetry
tables generated offline (`core/shapelogic/grid/TABLES.md`), and a mesh
corner table per grid (`core/shapelogic/mesh/tabs_*/meshverts.inc`). In V2
a grid is data: a Lua script (`data/scripts/grid_*.lua`) giving the lattice
vectors in voxel and physical coordinates, the valid voxel positions in
one lattice cell, one convex polyhedron per such position, the movement
directions, and the camera snap orientations for its editor. From that,
`src/lib/grid.cpp` derives the face, edge and corner neighbourhoods by
geometric matching, the bevel and offset vectors per face corner, the
grid's rotational and mirror symmetries (searched by testing candidate
rotations, not declared), and the cells a one-step move must find empty
(a Minkowski sweep of a shrunk voxel tested with a convex-intersection
routine). One mesher, `src/lib/polygen.cpp`, draws every grid for the
screen and for STL. Hexagonal and cairo prisms were added as scripts with
no C++ changes.

**What it cost.** Spheres were dropped; rhombic and tetra-octa were
re-split into tetrahedra with new storage ids; hexagonal and cairo have no
movement analysis; voxels must be convex with no gaps and exactly
coincident faces; the rotation set is fixed at 60; the mesher has no
tests; rendering is flat shaded; old .xmpuzzle files are import only
(shapes and piece counts, no solutions) and nothing writes XML. Rotation
moves in disassembly and disk spill of the search are comments, not code.

## The ideas

### 1. A generic bevel mesher as a new drawing and STL style
**Done 2026-11-08** (decision 0022): `core/shapelogic/mesh/uniform_mesh.h`,
the "Uniform bevels" style in the view, the "Uniform bevels" and "Bumpy"
switches in the STL dialog.
- **We have:** two systems. The 3D view's bevelled cells come from the
  per-grid corner tables through `core/shapelogic/mesh/voxel_mesh.cpp`,
  where bevel and offset are fixed in-table displacement directions. STL
  uses a generated chamfer table for cubes (`cube_mesh.cpp`) and a
  Minkowski offset through the Manifold library for the prism, rhombic
  and tetra-octa grids (`mink_mesh.cpp`): seconds on large shapes, a known
  needle case, no cancel (decision 0016, `docs/handoff/deferred-5.md`).
  The STL dialog offers unit sizes, bevel, offset and interior chamfers.
- **V2 has:** one mesher for every grid and both purposes. For each
  exposed face it walks the edge loop (the ring of voxels around that
  edge), decides convex or concave, emits bevel quads scaled by the
  dihedral angle so the bevel width is uniform, emits flat connector
  strips between inset neighbouring faces, and closes corners with a
  generic hole filler that chains loose edges into polygons. A "bumpy"
  mode renders each voxel as a rounded pillow joined to its neighbours by
  necks. STL export accepts pre-stretch matrices and computes the bevels
  after stretching.
- **The user would see:** a new entry in the 3D view's style menu beside
  "Bevelled voxels", with bevels of equal width on the triangle and
  tetrahedron grids where today they vary with the edge angle. In the STL
  dialog, a mesher choice and a bumpy switch, and a fast preview on large
  rhombic shapes. The existing styles stay, so the oracle output does not
  change.
- **Difficulty: medium.** A new C++ module of about the size of V2's
  polygen plus its edge-loop and bevel-vector derivation, fed by our
  `connectionFace` corners and `getNeighbor`. Spheres excluded. Then one
  `MeshStyle` value and one STL parameter in `api/`, the schema and docs
  regeneration, small front-end changes. No solver or file-format risk.
  The edge-loop geometry this builds is reused by ideas 2 to 4.

### 2. A declarative grid description as a test oracle, later as a runtime
**First stage done 2026-11-08:** `tests/unit/grid/test_grid_oracle.cpp`
rebuilds the tables from the cells' face polygons (`connectionFace` at
zero bevel and offset) and checks them: every cell a closed convex
polyhedron; the face, edge and corner neighbour tables of every grid
against the corners the cells share; every transformation a rigid
motion of the cells with the matrix of `getTransformMatrix`; the
transformation tables a group with that matrix product; every symmetry
group a subgroup whose unique and minimal transformations are its coset
representatives; the bevel faces closing each cell. All pass. Two
findings, both the original's as well:
- The rhombic and tetra-octa grids have no corner-neighbour table
  (`getNeighbor` kind 2 answers false), though their cells do touch at
  corners; a corner-connectivity check on them sees faces and edges
  only. The geometry the test derives is what such a table would hold.
- On those two grids `transformPoint` turns coordinates about their
  origin as cubes', half a cell off the grids' symmetry centres, so some
  turns land cells between cells or on cells of another kind. One
  whole-cell offset per transformation puts them right, which is what
  `Assembly::transform` supplies through the hotspot's real move; the
  test finds and checks that offset. Brick and prism need none.
The second stage, new grids, is not started.
- **We have:** per-grid `Voxel` subclasses with hand-written tables
  (`voxel_rhombic.cpp` carries a literal 24 by 15 edge-neighbour array),
  symmetry tables copied from generators that exist only in the original
  repo, per-grid mesh corner tables. Nothing checks these against
  geometry.
- **V2 has:** lattice vectors plus one convex polyhedron per base
  position; neighbourhoods derived by matching, symmetries found by
  search.
- **The user would see:** nothing at first. The first stage is a unit test
  in `tests/unit/grid` that rebuilds our tables from geometry and fails on
  any mismatch. The second stage, hexagonal and cairo prisms in File ▸ New
  as assemble-only grids, is a separate project: `GridKind`, 38 grid-kind
  switches in `app/shape_tools.cpp`, layer geometry, mesh, STL, the web
  `Grid` type and the .xmpuzzle `type` attribute all key on the enum, and
  the original has no such grids to compare against.
- **Difficulty: low to medium** for the oracle test (self-contained, no
  product code). **High** for new grids.

### 3. An arbitrary cutting-plane editor with snaps
- **We have:** the 2D editor draws one z-layer per grid from the engine's
  layer geometry (`core/shapelogic/app/layer_geometry.cpp`); XZ and YZ
  planes exist for the cube grids only. On the rhombic and tetra-octa
  grids the user edits thin tetrahedron slices in z. Click-on-a-face 3D
  editing already exists (`web/src/renderer/entities/edit3d.ts`), so V2's
  3D-edit mode is not a gap.
- **V2 has:** a 3D view with a clipping plane. The plane's normal snaps to
  the orientations the grid script declares (cubes: face, edge and corner
  directions), its offset snaps to declared stops, and the plane is
  painted with the true cross-section polygons of the voxels. Layer up and
  down move to the next stop.
- **The user would see:** a plane control with per-grid snap orientations,
  the 3D view sliced at the plane with cell outlines painted on it, and
  painting directly on the cut. For rhombic and tetra-octa puzzles the
  natural planes of the grid become visible and editable.
- **Difficulty: high.** Engine: a plane-to-cell cross-section per grid
  (polyhedron per cell intersected with a plane) and a method returning
  the cell polygons in 3D with neighbour positions. Front end: a clipped
  render pass, picking on the plane, snap animation, key bindings, all in
  `view3d`. A cheaper first step is XZ and YZ for the prism grid.

### 4. Derived movement checks for disassembly
- **We have:** disassembly on bricks and triangular prisms only, with
  movement rules ported per grid. Rhombic, tetra-octa and spheres show the
  "no disassembler" warning in File ▸ New and Assemble Only in the Solver.
- **V2 has:** movement vectors declared per grid; the cells that must be
  empty for a one-step move are computed by sweeping a slightly shrunk
  voxel along the vector and testing intersection against neighbours.
  Rhombic and octahedral grids get disassembly this way.
- **The user would see:** the warning gone for rhombic and tetra-octa,
  Solutions with levels and moves, the solution animation for those
  puzzles.
- **Difficulty: high.** The derivation is contained, but the movement
  cache and disassembler in `core/shapelogic/solve` must generalise to
  non-cube directions, and there is no original to oracle against:
  correctness rests on new tests and on cross-checking V2's answers for
  the same puzzles. Do this only after idea 2's geometry layer exists.

### 5. Solver and storage ideas from outside the grid code
- **We have:** the ported dancing-links assembler (classic and crowell),
  a thread pool, pause and resume with the saved position in the file,
  the front end's autosave that pauses the solve at a save point and
  writes the whole .xmpuzzle, undo on the document model (decision 0011),
  .xmpuzzle read and written both ways (decision 0006), the Python module
  and the oracle harness reading it.
- **V2 has:** Knuth's dancing-cells algorithm with native per-piece
  minimum and maximum counts; a search tree split into sub-problems for
  threads and persisted whole for resume; a SQLite `.dbpuzzle` file as
  the only document, with every edit and every solution committed as it
  happens, a trigger-based undo log, and "save as" implemented as
  `VACUUM INTO`; Lua scripts for shape filters and modifiers. SQLite is
  compiled into the executable (the bundled amalgamation unless a system
  library of at least 3.38.1 is found); no separate binary at run time.
- **The user would see:** faster solves with many identical pieces from a
  dancing-cells variant. Undo already exists here, so a storage change
  would be invisible or a compatibility regression.

**Decision on storage (discussed 2026-11-08).** Keep .xmpuzzle as the
document. Reasons:
- Compatibility with the original and the oracle harness (decision 0006),
  and the Python module, all read it.
- Scripts: Python reads SQLite without an extra install, so the database
  itself is not the barrier; the barrier is that V2 stores voxel spaces,
  assemblies and disassemblies as private binary blobs that need the C++
  decoders. The .xmpuzzle is self-describing and diffable.
- Thread management does not need a database: the solve session already
  owns the run threads and hands results over under one mutex (decision
  0010). V2's write queue does the same and merely drains to SQLite.

A "SQLite during the solve, then fold into .xmpuzzle and delete" hybrid is
sound but over-built. What a solve-time store would add is only:
- crash safety finer than the autosave interval (every solution durable
  as found);
- solution lists larger than RAM (we keep a vector and sort in memory).

If either is wanted, prefer an **append-only journal** beside the
document: each solution as the same `<solution>` element the .xmpuzzle
uses, one per line, plus a periodic position checkpoint; on the next open
the engine finds the journal, offers recovery, folds it into the document
and deletes it after a clean save. No dependency, no schema, readable by
the same scripts. SQLite earns its place only for queries over a solution
set that does not fit in memory during the solve (sorting, de-duplicating
millions of solutions), which is beyond what ShapeLogic targets. Note for
the site demo: the engine runs as WebAssembly there; SQLite builds under
Emscripten but adds about a megabyte and needs a persistence shim, so any
store must be optional in that build.

Caveat: a document with millions of solutions is slow to parse as text
however it is stored; V2 avoids that by never loading them all. If
analyses of that size are ever needed, the decision is about the document
format (a binary solutions section or a sidecar for the large case), not
the solve-time store.

- **Difficulty: medium** for a dancing-cells solver behind the existing
  solver option (the assembler interface is pluggable, the oracle checks
  counts; decision 0019). **Not planned** for storage.

## Summary

| Idea | Effort | User-visible payoff |
|---|---|---|
| 1 Generic mesher | Medium | Better bevels, fast STL, bumpy mode |
| 2 Grid oracle test | Low | None now; confidence and a path to new grids |
| 3 Cutting-plane editor | High | Large, especially rhombic and tetra-octa |
| 4 Derived movement | High | Disassembly on two more grids |
| 5 Dancing cells | Medium | Speed on identical-piece puzzles |
| 5 SQLite storage | n/a | Not planned; journal if crash recovery is asked for |

Suggested order: 1 first (self-contained, no compatibility risk, builds
the edge-loop geometry the others reuse), then 2's test, then 3 or 4 as
user demand shows.

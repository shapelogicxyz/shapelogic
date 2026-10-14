# 0016: The STL meshes and their export

**Status:** accepted (deferred part 5); `core/shapelogic/mesh/cube_mesh.h`,
`core/shapelogic/mesh/mink_mesh.h`, `core/shapelogic/mesh/triangulate.h`,
`core/shapelogic/mesh/stl.h`, `core/shapelogic/mesh/modifiers.h`,
`core/shapelogic/app/queries.h` (`stlPieces`), `engine/methods_export.cpp`,
`api/export.json`, `web/src/renderer/export/stl-*.ts`,
`web/src/main/export-file.ts` (`FolderGrants`).

## Context
The original exported a shape as an STL file for printing (File ▸ Export
STL, `stlexport.cpp`; Export Solution Pieces to STL,
`stlexportsolution.cpp`) and drew pieces "like STL export" in its view.
Its meshers: on the cube grids a table of the chamfered surface around
each cell corner (`cubemesh.cpp`, generated offline), on the prism,
rhombic and tetra-octa grids Minkowski sums and differences through the
Manifold library (`minkmesh.cpp`), on the sphere grid its ball meshes
(`voxel_2_mesh.cpp`, ported in stage 6); then the writer merged coplanar
faces and wrote binary or text STL to a file name, its title in the
header. The new core had no STL meshes: the front end wrote an interim
STL of the flat mesh (web stage 7).

## Decision
**Port the meshers faithfully, Manifold included.** Manifold 3.5.2, the
original's release, is pinned in `cmake/Dependencies.cmake` and built as
the original builds it (a static library without cross sections,
bindings, tests or the parallel backend). It is optimised in every
preset (`MANIFOLD_OPTIMIZED`, as the original's subproject was built
Release; Manifold sets `-ffp-contract=off` on itself, so its answers do
not depend on the optimisation). It is the core's first third-party
library besides zlib (Apache-2.0, decision 0002). The cube table is
copied unchanged (`tabs_cube/`). The result is the original's to the
byte: `oracle.py stl-dump` records the original's STL file of every
fixture shape with the default parameters, and the new core writes the
same bytes for all 170 shapes in the dev and the release build; the unit
test compares within 1e-5 so that another compiler's rounding does not
fail it.

**The file is made in memory.** `mesh::stlFile(mesh, title, binary)`
returns the bytes; where they go is the caller's business (the core does
not write export files). The binary header holds the title given: the
engine's client gives the file's name, as the original did. (The
original's macOS `basename` found the first slash, not the last, so its
headers held most of the path; not copied.)

**The parameters are the engine's, as values in the grid's order.**
`export.stlParameters {grid}` lists each parameter's name, kind, default
and tooltip as the original's exporter classes declared them; the other
calls take `parameters: number[]` in that order (absent: the defaults),
so the client shows whatever a grid has without knowing it. The core
checks the count and the ranges with the original's messages.

**Bytes back, the client writes.** `export.stl` answers the file as a
frame of one u8 array; the front end's main process writes it, to the
path its save dialog gave (web stage 7's grants) or, for a solution's
pieces, into the folder its folder dialog gave last (`FolderGrants`),
under a name of the form the engine gives pieces (`isPieceFileName`:
ASCII letters, digits, '-', '_' and ".stl"), so a page cannot write
elsewhere. `export.solutionPieces` names the pieces as the original's
dialog did (`stlPieces`: the shapes the assembly places, their names
made safe and told apart).

**Two limits the original did not have.** More than 6 recursions of the
sphere grid's balls are refused before anything is built: each level
makes four times the triangles, 6 already fails an assertion in both
programs, and 10 needs tens of gigabytes first. A mesh that fails one of
the meshers' assertions is refused (InvalidArgument) with the words the
original's dialog used when it caught one ("The generated mesh is faulty
in some way, try to tweak the parameter"), in `Document::stlMesh`, where
the dialog caught it.

**The view's STL-like style is a mesh style.** `MeshStyle::Stl`
(`shape.mesh`, `playback.mesh` style `stl`) is getSTLMesh: the meshers
at the view's own parameters (gap 0.02, bevel 0.05 with the interior
chamfers; the sphere grid's balls at two recursions), the flat mesh where
the mesher fails. The front end draws it in one colour, as the original.

**Two guards around Manifold, results unchanged.** Manifold 3.5.2 reads
out of bounds in two places (both seen under the sanitizers, both
present in the original's build): checking a triangle list in which an
edge is used more often one way than the other, it reads a half-edge's
pair that was never set, far outside its arrays; and in its boolean's
bookkeeping it copies a few half-edge tangents past the end of their
array. The first could fault: the mesher no longer hands Manifold such a
list (`balanced` in mink_mesh.cpp; Manifold refused every one of them, so
the meshes are the same bytes). The second reads tangents nothing here
uses: the ASan preset leaves Manifold uninstrumented
(`cmake/Dependencies.cmake`) and our code checked.

## Consequences
- The core links Manifold; every preset builds it (the sanitizer presets
  instrument it too). A first build takes about a minute longer.
- The Minkowski mesher is slow for large shapes (10 s for the largest
  fixture shape in a release build, 50 s in the dev build): the engine
  answers `export.*` and `shape.mesh` style `stl` on its main thread, as
  the original's dialog and view did, so a large shape holds up the calls
  behind it. The dump test's large fixtures are `[slow]`.
- The original's merge of coplanar faces can leave an edge with four
  triangles (Bermuda's shape 22: a needle the Minkowski mesher leaves is
  doubled by the merge); the file is still closed and oriented, and the
  tests allow exactly that one (docs/handoff/deferred-5.md, "Found in the
  original").

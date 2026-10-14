# Handoff: deferred part 5 (the STL meshes and exports)

Part 5 of docs/DEFERRED-PLAN.md: the original's STL meshes ported to the
core (the cube grids' chamfer table, the Minkowski mesher on Manifold
3.5.2, the sphere grid's balls at the export's parameters, the merge of
coplanar faces, the binary and text file), the engine's `export.*`
methods and `stl` mesh style, and the front end's two STL dialogs and
STL-like render style. The new core writes the original's STL files byte
for byte for all 170 fixture shapes. No audit after this part, as the
plan says. Next: part 6, the imports, the Puzzlecad export and grid
conversion.

## Done

Six commits, in order:
- `d7e8094` **Core: the STL meshes** (`core/shapelogic/mesh/`): `triangulate.h`
  (cubeMesh::triangulateLoops), `cube_mesh.h` (cubemesh.cpp with the
  tables `tabs_cube/cubetable*.inc` copied unchanged, cubepoly.cpp),
  `mink_mesh.h` (minkmesh.cpp), `stl.h` (stl.cpp, stl_0.cpp, stl_2.cpp,
  getStlExporter: the parameters, `stlMesh`, `stlFile` in memory),
  `modifiers.h` (+ `scalePolyhedron`, `mergeCoplanarFaces`, `volume` of
  volume.cpp), `MeshStyle::Stl` (getSTLMesh); `app::stlPieces` (queries:
  stlexportsolution.cpp's placedShapeCounts, safeName and naming),
  `Document::stlMesh` and `stlPieces`; Manifold 3.5.2 in
  `cmake/Dependencies.cmake`; the oracle's `stl-dump` and
  `stl-solution` recorders with their recordings; the ported tests.
- `180c5ae` **Engine: shape.mesh style stl** (MeshStyle `stl`, also for
  `playback.mesh`; the front end's style types widened).
- `2a96d45` **Core: a faulty STL mesh is refused as the original's dialog
  said; at most 6 recursions.**
- `15bdfa8` **Engine: export.stlParameters, export.stlMesh, export.stl,
  export.solutionPieces** (`api/export.json`, `engine/methods_export.cpp`).
- **Core: a triangle list Manifold would refuse is not handed to it**
  (found by the ASan gate; see "Found in Manifold" below), with the ASan
  preset leaving Manifold uninstrumented.
- "Deferred 5: …" (this note): the front end, the docs.

### The core (decision 0016)
- The cube grids (bricks, sliding, stacking): `cubeSurface` places the
  table's polygons for each cell corner's body (48 symmetries, the
  face-connected pieces of a corner, the regime of the r/g ratio), welds
  them, merges coplanar polygons into faces, drops the vertices that are
  nobody's corner and triangulates (ear clipping with holes bridged);
  `cubePolyhedron` tags each triangle with its cell and side.
- The prism, rhombic and tetra-octa grids: `minkPolyhedron`, line for
  line the original's minkmesh.cpp on the same Manifold calls (the exact
  body, the bits, the steps with their rebuild onto planes, the fold
  check and retries, the face tagging).
- The sphere grid: the stage 6 port of voxel_2_mesh.cpp at the export's
  parameters (recursion 2 by default, not the view's fast mesh).
- `stlParameters(grid)` (the six of stl_0 for every grid but the
  spheres' eight of stl_2, with the original's names, kinds, defaults and
  tooltips), `stlMesh(voxel, values)` (the original's checks and
  messages, then the mesher, scaled by the unit sizes), `stlFile(mesh,
  title, binary)` (mergeCoplanarFaces, each face's fan, the original's
  binary layout and text format).
- `Document::stlMesh` catches the meshers' assertions as the original's
  dialog caught every exception ("The generated mesh is faulty in some
  way, try to tweak the parameter"); more than 6 recursions are refused
  in `stlMesh` before anything is built.

### The harness
- `oracle.py stl-dump` (`stl_dump_old.cpp` against the original's
  library) → `tests/data/stl_dump.txt` (790 KB): for every shape of the
  top-level fixtures with its grid's defaults, the triangles of the
  original's binary STL file (distinct vertices and index triples; for
  more than 5,000 triangles, the sphere grid's, the box and the sums), its
  view mesh (getSTLMesh: the faces' attributes as a hash, the box), the
  text STL of the first small shape. `tests/unit/mesh/test_stl_dump.cpp`
  compares line by line (numbers within 1e-5; the 4 large fixtures as
  `[slow]`) and checks every file is closed and consistently oriented.
- `oracle.py stl-solution` (`stl_solution_old.cpp`: the dialog's GUI code
  copied, the original's exporter) → `tests/data/stl_solution_by_original/
  CubeInCage/` (its five files and `pieces.txt`); `.gitignore` lets those
  `.stl` files in.
- A probe compared the binary files' bytes (after the header) of all 170
  fixture shapes in both programs: equal in the dev and the release
  build; 18.4 s for all in release against the original's 19.3 s.

### The engine
- `export.stlParameters {grid?}`, `export.stlMesh {shape, parameters?}`
  (a MeshFrame and the volume), `export.stl {shape, parameters?, binary?,
  name?}` (the file as a frame of one u8 array `file`), and
  `export.solutionPieces {problem, solution}`; `shape.mesh` and
  `playback.mesh` take style `stl`. The client's frames are always at
  `frame` (the generator's rule), so `export.stlMesh` answers the
  MeshFrame's fields at the top level beside `volume`.
- The contract suite (`stl_exports`): the parameters of two grids,
  CubeInCage's pieces and each file equal to the original's recording,
  the preview's volume (plain cells of unit size 1 = the cells), a text
  file, the refusals (variable cells, the count, a range, the types, a
  control character or 256 bytes in the name, unknown ids, 6 recursions);
  one cell of every grid drawn in the `stl` style.

### The front end (docs/webui-spec.md section 10)
- File ▸ Export ▸ STL (`export/stl-dialogs.ts`, `#exportStl`): the grid's
  parameters with the original's labels, `%2.2f`/`%i` values and
  tooltips, Binary STL, the shape; a preview (`export/stl-preview.ts`: a
  small three.js view of its own on the face material, the arc-ball drag
  and the wheel, Normal and Insides) with "Volume: x cubic-units" or the
  engine's refusal in the original's words; Export STL… asks the save
  dialog and keeps the dialog open; the values typed and Binary STL are
  remembered per grid for the next dialog.
- Solutions ▸ Export to STL (the Solver's solutions card, with a solution
  chosen; `#exportStlSolution`): the parameters, Binary STL, the output
  folder (Choose…: the main process's folder dialog), "Pieces placed in
  solution N:" as the original's box, Export All with the original's
  "N output file(s) already exist. Overwrite?" and its closing line, an
  error per file in a message box.
- The main process: `bt:choose-folder` grants the folder to the window
  (`FolderGrants`, the last one chosen), `bt:existing-exports` counts what
  is there, `bt:write-folder-export` writes only into that folder and only
  names of the engine's form (`isPieceFileName`), through
  `writeFileSafely` (temporary file, rename, links followed, read-only
  refused).
- The STL-like style: View ▸ Draw Pieces like STL Export (no longer
  greyed) and the Display menu's "Like the STL export"; one colour
  (`hasChecker`: only the bevelled style and the wire frame have the
  checker), no edge lines.

### Removed "not yet" notes
The interim STL (`export/stl.ts`, its dialog's "later version" words, the
three.js STLExporter path), the greyed View menu item, the disabled
Export to STL button and its tooltip, the menu module's list of what is
greyed, `playback.h`'s "belongs to the exports, later", ARCHITECTURE's
mesh note, README's and web/README's status lines, FRONTEND-PLAN.md's
rows for the STL export and the render styles, webui-spec.md's gap 4.

### Tests
- C++: `tests/unit/mesh/test_stl.cpp` (the original's test_cubemesh,
  test_minkmesh with the random-shapes stress as `[slow]`
  (`BT_MINK_STRESS` scales it), test_manifold_smoke; new: the parameters,
  every refusal's words, the unit sizes and the volume, the file layouts,
  the sphere grid hollow and with holes), `test_stl_dump.cpp`,
  `tests/unit/app/test_stl_export.cpp` (CubeInCage's pieces and files
  against the original's, names made safe and told apart, the faulty and
  the too large mesh), the halfedge case's STL mesh restored in
  `test_meshes.cpp`.
- Front end: `tests/unit/renderer/exports.test.ts` (the fields' formats
  and reading, the words), `tests/unit/app.test.ts` (the View menu's STL
  style, the bridge's checks, `FolderGrants`, `isPieceFileName`),
  `tests/engine/exports.test.ts` (the engine's STL of a cube: closed,
  facing out, its volume; every fixture shape's file, but the four slow
  fixtures', loaded by three.js's STL loader with its size, closed and
  oriented), `tests/e2e/exports.e2e.ts` (both dialogs end to end, the
  files equal to the original's), `tests/e2e/view.e2e.ts` (a reference
  screenshot of the STL-like style for each grid: 6 new images).

## Gate results

All on the final core and engine (2026-10-12, M-series Mac):
- `just test-all` 6/6 (617 s; unit 96 s, unit-slow 486 s with the STL
  dump's large fixtures and the Minkowski stress case).
- `just test-asan` 6/6 (2,058 s), after the fix for what its first run
  found (commit `8f068c1`, "Found in Manifold 3.5.2" below).
- `just test-tsan`: 82 `[parallel]` cases, then the contract suite
  against the TSan engine, clean.
- `just oracle --tier fast`: 19/19 on all threads and with `--threads 1`
  (nothing else moved); `just oracle-roundtrip`: 511/511 byte-identical.
  The part's own oracle is the recordings: `oracle.py stl-dump` and
  `stl-solution` (see "The harness"); a probe comparing the binary files
  of all 170 fixture shapes byte for byte with the original's passed
  before and after the fix.
- The contract suite: 119 methods, all called; `stl_exports` also passes
  against the `release-x64` engine under Rosetta.
- Front end, three times each in sequence: `just web-check` clean; `just
  web-test` 323/323 (release engine); `just web-e2e` 70/70 (5.9 min; 6 new
  reference screenshots of the STL-like style, one per grid, reviewed: one
  colour, chamfered, the gaps between cells showing). `just web-dist
  --dir` and `just web-smoke` passed (a new library in the engine).
- `just check`: not run (LLVM is not installed).

## Differences from the original, and why

1. **More than 6 recursions are refused** on the sphere grid (the
   original had no limit): each level makes four times the triangles, 6
   already fails an assertion in both programs, 10 needs tens of
   gigabytes before it gets there (decision 0016).
2. **The binary header names the file**: the original's title was
   `basename(file)`, but its macOS `basename` found the first slash, so
   its headers held most of the path; the client passes the file's name.
3. **The system's dialogs** take the original's File name and Path fields
   and its "..." choosers: the save dialog asks before replacing a file
   (the original's "File exists overwrite?"); the solution export's folder
   comes from a folder dialog and must be chosen before Export All (the
   original started at ".", the program's working folder, meaningless for
   a packaged app).
4. **Fields are read strictly**: a field that is not a number is said to
   be one (the original's atof and atoi read it as 0 and refused the 0 if
   the range did); the ranges and their words are the engine's.
5. **The preview** is a small view of its own in the dialog, as the
   original's; its Insides mode is the main view's x-ray.
6. **The dialogs remember** the values typed per grid and Binary STL for
   the next dialog; the original started from the defaults each time.

## Found in the original (worth fixing there)

- **An edge with four triangles in one STL file**: on Bermuda's shape 22
  (prism grid) the Minkowski mesher leaves a needle triangle where a side
  face meets a bevel; `mergeCoplanarFaces` triangulates the bevel's
  outline with an ear that is the same needle the other way round, so
  the edge between them carries four triangles, two of them without area.
  The file is closed and consistently oriented but not a manifold; the
  port writes the same bytes, and the tests allow exactly this one shape.
- **macOS `basename`** in stl.cpp uses `strchr` (the first slash): the
  binary header holds the path after its first component.
- **6 recursions and more on the sphere grid fail an assertion**
  (`finalize`'s even half-edge count) after building the whole mesh; the
  dialog's catch-all hid it as "faulty".

## Found in Manifold 3.5.2 (the original's build has both)

- `Manifold(MeshGL64)` on a triangle list where some edge is used more
  often one way than the other (the rebuild's triangulation can give one;
  the original's minkmesh.cpp hands it over and falls back on the
  refusal): `CreateHalfedges` leaves a pair unset and `IsManifold` reads
  `paired_[pair]` at an index around -10^9 (UBSan: vec_view.h:60). In a
  release build that read lands wherever it lands; on these fixtures it
  did not fault. The port checks the list first (`balanced`, skipping the
  triangles with a corner twice as Manifold does) and gives an empty solid
  for one Manifold would refuse; the 170 fixture shapes' files stay the
  same bytes.
- `CsgLeafNode::Compose` → `SortGeometry` → `ReindexFace` reads up to a
  few `vec4` half-edge tangents past the end of the combined tangent
  array (ASan, the large fixtures' view meshes). Only tangents are copied,
  and nothing uses them without smoothing. Not root-caused further; the
  ASan preset leaves Manifold uninstrumented because of it. Worth a report
  upstream with the 12PieceSeparation shape as the case.

## Known issues and notes

- The engine builds STL meshes on its main thread, as the original's
  dialog and view did: a large rhombic shape takes seconds (10 s for
  12PieceSeparation's largest in a release build), and the calls behind
  it wait. The preview says "Building the mesh…" meanwhile. No cancel.
- The Intel Macs' engine (`release-x64`, run under Rosetta) builds with
  Manifold and writes CubeInCage's files with every corner the same as the
  arm64 recording and the facet normals a few last bits apart (no fused
  multiply-add on x86-64); the contract and e2e tests compare the normals
  within 1e-5, the dump test every number within 1e-5.
- The quick unit suite takes about 17 s more in the dev build (the dump of
  the fifteen quick fixtures); the large ones are `[slow]`.
- One SIGBUS of `stl_dump_old` was seen once in a run of the recorder and
  never again (four runs since, two of them the whole list in one
  process, all equal); the recording is from a clean run.
- `just check`: not run (LLVM is not installed).

## Brief for part 6

Part 6 of docs/DEFERRED-PLAN.md, the imports, the Puzzlecad export and
grid conversion (Opus 5.5, high effort): port `ps3dloader` (PuzzleSolver3D
files, 129 lines), `scadloader` (Puzzlecad `.scad`: load and
`saveOpenScadPuzzle`, cube grid only, as the original's message says;
711 lines) and `converter` (Convert brick grid type to other: the target
grids `convertwindow.cpp` offers; 127 lines), with the tests the original
has for them (`test/test_puzzle_model.cpp` touches them; make small inputs
with the original where none exist and commit them under `tests/data/`).
Engine: `document.import {format, path|bytes}` (a new document, dirty and
untitled), `document.exportPuzzlecad {problem}` (text), and
`document.convertGrid {target}` (undoable, or a new document as the
original does: follow the original); each in its own `Engine:` commit.
Front end: File ▸ Import ▸ PuzzleSolver3D… and Puzzlecad…, File ▸ Export
▸ Puzzlecad…, Edit ▸ Convert grid type… with the original's dialog
choices; the greyed items enabled (main/menu.ts, its header comment).
Gate: each import of the original's test inputs gives a document whose
save equals the original's import followed by its save; Puzzlecad export
text equal to the original's for every cube-grid fixture; every
conversion the original offers gives the same shapes as the original's;
e2e of the menu items. Write the screens into docs/webui-spec.md (a new
section) before building them.

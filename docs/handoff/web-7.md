# Handoff: web stage 7 (exports)

**Date:** 2026-10-09 · **Next:** stage 8, packaging (Opus 5.5, medium;
"Brief for stage 8" below), then the security audit before the first
release (Fable 5.1, max).

## Done

No engine or core commit: everything the exports need was there.
`playback.track` already samples the animation as the original's
`gltfexport.cpp` did (the core's `PlaybackSampler::track`, ported in core
stage 6 from `solutionAnimation`: the hold on the assembled puzzle, the
moves, a hold on the puzzle apart, the moves backwards to reassemble and a
last hold; the poses of hidden pieces next to where they are shown; the
quaternions in one hemisphere), and `playback.mesh` / `shape.mesh` give
every style. The Windows run of web-6.md's "Windows: what to do by hand"
was not given to this session: it is still to be done (the section stands
as written).

### The main process (`web/src/main/`)
- `export-file.ts` (new): `writeFileSafely`, an export written the way the
  core now writes a puzzle (`saveTarget` and `replaceWith` in
  `core/shapelogic/io/gzip_file.cpp`, since the stage 6 audit): `<file>.tmp` beside
  the file, renamed over it once whole; a symbolic link followed (at most
  40) so the link stays and the file it names is written; a read-only file
  refused ("… is read-only"), where a rename would replace it; the replaced
  file's permissions given to the new one. `ExportGrants`: the paths each
  window may write, Electron-free.
- `app-ipc.ts`: four new kinds of file (`image` .png, `vector` .svg, `stl`
  .stl, `glb` .glb) in `KINDS`, each with its dialog title ("Export Image"
  …); `bt:choose-save` grants an export kind's path to the window that
  asked; `bt:write-export` writes bytes only to a path so granted, once,
  with that kind's extension (so no export can overwrite a puzzle or a
  file the user never chose), and answers `{ok}` or `{error}` with the
  reason in words. The open dialog takes only puzzles and paused solves.
- `menu.ts`: File ▸ Export ▸ Image…, Vector Image… (a document open), STL…
  (a shape with cells: `MenuState.shapes`), Solution Animation (GLB)… (the
  Solver shows a solution with its moves stored: `MenuState.animation`);
  Puzzlecad stays greyed.
- `engine/app-bridge.ts`: the commands `exportImage`, `exportVector`,
  `exportStl`, `exportGlb`; `FileKind` with `ExportKind`, `isFileKind`,
  `isExportKind`; `BtApp.writeExport(path, bytes)` (the preload turns an
  `{error}` into a rejection with that message).

### The renderer
- `export/exports.ts` (`Exports`), the four commands:
  - **Image**: a dialog with the two sizes, the view's and twice it (in
    pixels), then the save dialog; `View3D.image(scale)` →
    `ViewRenderer.capture`: a render at that pixel ratio, read with
    `toDataURL` in the same task (before the page shows it), the
    display's ratio restored. What the canvas draws is in it (the shape,
    the overlays, the pieces, the solution); the HTML over the canvas is
    not (the view cube, the toolbar, the tag, the Puzzle scene's labels).
  - **Vector Image**: straight to the save dialog; `View3D.vectorImage()`
    (`view3d/svg.ts`, below).
  - **STL**: a dialog with the shape (the selected one first, the shapes
    with cells), Unit Size (10, the original's default), Binary STL (on),
    and the words "an interim … The STL meshes of the original, with
    bevelled edges and an offset for printing, come in a later version";
    `export/stl.ts` writes the shape's flat mesh (`shape.mesh` style
    `flat`), each triangle turned to go counter-clockwise about its normal,
    scaled, through three.js's `STLExporter`, binary or text.
  - **Solution Animation**: the solution the Solver's player shows (a row
    with `disassembly: 'tree'`); a playback of its own
    (`playback.open`, closed after), then the original's dialog
    (gltfexportwindow.cpp: Seconds per move 0.50, Cell size (mm) 10.0,
    Reassemble after the disassembly, Bevelled edges as in the 3D view,
    "N steps. Pieces keep their solution colours.", its tooltips), the save
    dialog (`<name> solution N.glb`), `playback.mesh` per piece (drawing or
    flat), `playback.track` (8 samples a step), `export/glb.ts`.
  - A field that is not a number above 0 keeps the dialog open with a
    flash; the choices are kept for the next export of the session; each
    write counts in `#stage[data-exported]` (the tests wait for it) and
    flashes "Exported <file>"; a failure is reported ("Export the image: …
    is read-only").
- `export/glb.ts` (`animationGlb`, DOM-free): `gltfexport.cpp`'s scene: a
  scene named "Solution N", a root node "Puzzle" scaled by the cell size
  (metres), a node per piece named as `playback.open` names it (the shape's
  name or S1, " #2" for a second copy) with its mesh (the playback's, in
  its assembly orientation; the inside faces left out as the original's
  FF_INSIDE_FACE; indexed by `mergeVertices`, flat shaded), a
  `MeshStandardMaterial` of the piece's colour as the player draws it (the
  view's sRGB numbers, written linear by three.js's colour management, as
  the original's `srgbToLinear`), metallic 0, roughness 0.6, both sides;
  an animation "Disassembly" whose channels are `playback.track`'s
  keyframes thinned as the original thinned them (`keptKeys`: a key stays
  only when the interpolation from the last kept key to the next does not
  reproduce every key between within 1e-4; slerp for rotations, STEP for
  scale), translation always, rotation when the piece turns, scale 0 or 1
  (STEP) when it hides; written by three.js's `GLTFExporter` (binary).
- `view3d/svg.ts` (`vectorImage`): the meshes the view draws (each faces
  mesh now says where it came from: `MeshSource` in `buildMesh`, kept in
  `faces.userData` — a shape at a revision and transformation, or a
  playback's piece; set by the view, the pieces scene, the player and the
  explorer) asked again in the flat style, placed as the view has them,
  from the view's camera at its size: the faces turned towards the eye, in
  the colour the view's shader gives them (the material's uniforms: piece
  colour, colour view and palette, the variable marker, the checker, the
  dimmed layers, the original's light) computed per triangle; the flat
  style's edge lines; the variable marks; a wire-frame piece as its lines;
  the faded copies and fading pieces at their opacity; painted in the
  order of `view3d/depth-order.ts` and written by three.js's `SVGRenderer`
  (its own sort off), with the view's background as a rectangle.
- `view3d/depth-order.ts` (`paintingOrder`, DOM-free): a BSP tree over the
  faces (a face's plane splits what crosses it, Sutherland–Hodgman) and
  the lines (cut where they cross a plane; a line lying in a face's plane
  painted just after that face), walked back to front for an eye (or a
  direction: the orthographic camera), as gl2ps sorted the original's
  vector export. Iterative walk; the build recurses once per plane.
- `view3d/renderer.ts`: `drawNow`, `capture`. `view3d/scene.ts`: the
  shape's faces mesh is marked `userData.faces` as the other scenes' are.
- `solver/solutions-card.ts`: the disabled "Export to STL" button says
  what it waits for (the original's Export Solution to STL writes each
  piece's STL mesh) and where a shape's flat STL is.
- `shell/app.ts`, `shell/menus.ts`: the commands wired; the menu state's
  `shapes` and `animation`.
- `package.json`: `gltf-validator` 2.0.0-dev.3.10 (Khronos's glTF
  validator, Apache-2.0), a dev dependency for the tests.

### Tests
- Unit (`tests/unit/app.test.ts`, 5 new, 19 in all): an export's file
  written whole and replaced with no temporary file left; through a link
  (the link kept, the file it names written); a read-only file refused and
  left; the permissions kept (0600); a temporary file that cannot be made
  (a folder in its place) fails and leaves the old file; the grants (only
  the window's own path, once, with its kind's extension; a `.xmpuzzle`
  granted as glb refused); the menus' export items enabled by the state;
  the bridge's new checks. `tests/unit/renderer/exports.test.ts` (7): the
  keys a channel keeps (straight runs, corners, STEP, slerp), the dialogs'
  numbers, the painting order (behind first and turned round with the
  eye, orthographic, a floor split by a wall painted in two places, a line
  after its face and a line behind before it, a line cut by a plane, a
  face without area left out).
- Engine (`tests/engine/exports.test.ts`, 3): the .glb of every fixture
  solution with its moves stored (AlPackino, CubeInCage, Dracula,
  PelikanBurr, Prisgon ×2, test_rotation_solvers ×2: turns included)
  passes Khronos's validator with no error and no warning, and, read back
  by a reader of the tests' own (`tests/helpers/gltf.ts`: the chunks, the
  accessors, the samplers interpolated as glTF says), has the root's
  scale, the playback's pieces in order, each mesh the playback's outer
  triangles with its bounds, the times of the holds and the moves, and
  every piece where the player's `PoseTrack` puts it at 1/16 steps of the
  moves forward and backward and in the holds (translation within 1e-3,
  rotation within 1e-5 of a unit dot; visibility forward; beside a piece's
  vanishing the .glb keeps its last shown pose, as the original meant, and
  is not compared there); the STL of a one-cell cube a closed surface
  (each edge once each way, 12 triangles) with signed volume +1000 at unit
  10, binary 684 bytes, text 12 facets; every fixture shape's STL (binary
  and text) loaded by three.js's `STLLoader` with the flat mesh's
  triangles and its bounds. Checked by mutation: the thinning's tolerance
  loosened, the rotation channel dropped, the scale channel dropped: each
  fails.
- e2e (`tests/e2e/exports.e2e.ts`, 2): Image of CubeInCage S1 at 1x and 2x,
  each of the canvas's size times the scale and the view's picture (drawn
  by the page beside a screenshot of the canvas: mean difference under 2
  of 255, under 0.5% of pixels off by more than 48), and a reference image
  of the 1x; Puzzlecad greyed; Vector Image in the flat style without the
  overlays: the SVG's viewBox the view's, drawn by the page like the view
  (mean difference under 4, under 2% off; measured 0.54 and 0.37%) and a
  reference image of it. STL of CubeInCage S2 from its dialog (the
  interim's words, the selected shape first): binary of the flat mesh's
  triangles; a unit of 0 keeps the dialog; text at 2.5; a read-only file
  refused ("read-only" in the status bar), left with its bytes and mode; a
  path no dialog gave refused by the main process and not written.
  Solution Animation greyed in Entities, enabled once the Solver plays the
  first solution; the dialog's steps the player's; a cell size of 20 mm;
  the .glb valid (Khronos), the root's scale 0.02, the playback's pieces
  by name, a material per piece. `files.e2e.ts`'s greyed list now Puzzlecad,
  the imports, grid conversion and the anaglyph modes.
- Looked at by eye in this session (a throwaway spec, not kept): the
  vector image and the PNG of the Puzzle workspace's scene and of the
  Solver's solution next to the view, in the flat style: the same picture.

## Gate results
| Check | Result |
|---|---|
| `just web-check` | clean, three times (three TypeScript projects, ESLint without warnings, Prettier), 11–12 s |
| `just web-test` (release engine) | **257/257**, three times, 20, 21, 20 s: 28 files (stage 6's 242; `unit/app.test.ts` 5 more, `unit/renderer/exports.test.ts` 7, `engine/exports.test.ts` 3) |
| `just web-e2e` | **58/58**, three times, 5.4 min each (build included): the 56 of stage 6 and its audit, and `exports` 2 |
| perf (each e2e run) | mesh build 6.0, 5.8, 6.1 ms; orbit median 13.3 ms at a 13.3–13.4 ms display frame, p95 14.7, 13.8, 14.9 ms; 151, 151, 152 renders in 151, 150, 152 frames |
| C++ | no change (no engine or core commit) |
| before the gate | one full run of the three (clean: 257 and 58), then the gate run whole three times, clean |

## The reference screenshots
Two new: `export-image-bricks-1x.png` (the 1x PNG export of CubeInCage S1,
the drawing style with the axes, the boundary and the layer's slab) and
`export-vector-bricks.png` (the SVG of the same shape in the flat style,
without overlays, as the page draws it); both looked at, and the SVG's
against the view's flat style. The 36 earlier ones are unchanged.

## Where this stage departs from the plan, and why
1. **The vector image's painting order is a BSP tree's, not
   SVGRenderer's.** SVGRenderer sorts whole triangles by their centres:
   the first SVG of CubeInCage had every edge line under the faces (none
   showed) and black inside faces painted over the cage (2.6% of its
   pixels off from the view). The tree (as gl2ps, which the original's
   vector export used) gives the view's picture (0.37%). SVGRenderer still
   projects and writes the paths, in the tree's order.
2. **Only the faces turned towards the eye are in the SVG**, and no
   overlays (axes, boundary, slab), labels or x-ray inside faces: the
   flat faces enclose each shape, and the overlays are lines three.js's
   `LineSegments2` draws, which SVGRenderer cannot.
3. **The PNG export has the plan's two sizes only.** The original's image
   export (imageexport.cpp) had paper sizes, DPI, supersampling, a
   transparent background, and several images of a shape, a problem or a
   solution's steps; the view's renderer has no alpha channel, so a
   transparent background would mean another renderer.
4. **SVG is the one vector format**; the original offered PS, EPS, TeX,
   PDF, SVG and PGF through gl2ps.
5. **The .glb is written by three.js's GLTFExporter**, so its generator is
   "THREE.GLTFExporter r186" and its layout is three.js's (the original
   wrote the JSON by hand); the scene, nodes, names, materials, channels
   and thinned keys are the original's.
6. **The animation has its own playback** (opened for the export, closed
   after), so the player can move on while the file is made.
7. **The files are written by the main process to a path its dialog gave
   that window for that kind** (the brief), not by the engine as the paused
   solve is: the bytes are the renderer's.

## What the mockup did differently, and why it changed
- Its top bar's "Export" placeholder is File ▸ Export (stage 6's menus).
- Its Solver card's "Export to STL" flashed that a dialog "would open"; it
  stays disabled until the STL meshes (the original's Export Solution to
  STL writes them), with a tooltip saying so and pointing at File ▸
  Export ▸ STL.

## Known issues and notes
- **The interim STL is not watertight on every shape**: of the fixtures'
  170 shapes with cells, 163 come out closed (every edge once each way)
  and 7 do not (probed in this session). The flat mesh merges a side's
  coplanar cells into polygons, so where polygons meet, corners need not
  match (T-joins), and shapes whose cells touch only along an edge share
  it four ways; slicers mend these, strict checkers report open edges. A
  one-cell cube is closed (the gate); the STL meshes (later) are the real
  answer.
- The SVG has hidden faces under the visible ones (the painter paints
  everything turned towards the eye); its size grows with the scene
  (CubeInCage's shape about 60 KB, the Puzzle scene about 160 KB).
- The BSP build recurses once per plane: a scene of many differently
  turned pieces (the Puzzle scene of a large puzzle) makes a deep tree;
  not measured beyond the fixtures.
- In the Puzzle scene a faded optional copy is painted with all its faces
  turned towards the eye at its opacity, so its far inner faces show
  through it; the view draws its nearest surface only.
- The PNG is the canvas: the Puzzle scene's labels and boxes, the view cube
  and the toolbar (HTML) are not in it.
- A solution without its moves cannot be animated (the original refused
  it too: "This solution has no disassembly to animate"); the menu item is
  greyed then.
- On Windows the write's rename over an existing file and `access(W_OK)`
  on a read-only file are unverified (as the core's: web-6.md's Windows
  section).
- From stage 6 and earlier, still open: as web-6.md's "Known issues" and
  its audit's "Known issues left" list them.

## Brief for stage 8
**Stage 8** (Opus 5.5, medium), FRONTEND-PLAN.md's "Stage 8": packaging.
electron-builder for macOS (arm64, x64), Windows and Linux; `bt-engine`
from the release preset in the bundle's resources (the host finds it
there, else `BT_ENGINE`: `main/engine-path.ts` looks at `BT_ENGINE`, then
the repository's dev and release builds); one version for the app and the
engine (`CMakeLists.txt`'s `project(... VERSION 0.1.0)`, read at build;
`web/package.json` says 0.1.0 by hand today); `just web-dist`;
the Electron security checklist as a unit test over the window options
(sandbox, context isolation, no node integration, no remote content, the
CSP `index.html` already has (check it is the one served), no `shell.openExternal` on untrusted strings — the
help links are the main process's own table, `app-ipc.ts`'s `HELP_URLS`,
file paths only from dialogs, drops, the recent files and the rescue
record — and from this stage: exports written only to a path the dialog
gave, `ExportGrants`); signing and notarisation documented, not
automated; a smoke e2e against the packaged app. Where things are:
- `main/index.ts` (the window's options, navigation and new windows
  refused, `BT_USER_DATA`, `BT_CALL_LOG`, `BT_AUTOSAVE_SECONDS`: decide
  which test switches a packaged app keeps), `main/engine-path.ts`,
  `main/engine-host.ts`, `preload/index.ts` (what `window.bt` and
  `window.btApp` expose), `electron.vite.config.ts`, `package.json`.
- The engine on Windows needs the MinGW runtime DLLs beside it (web-6.md's
  Windows section runs it with `C:\msys64\ucrt64\bin` on `PATH`); a
  packaged app must carry them or link statically.
- The gate: the packaged app on this Mac opens and solves a fixture;
  `bt-engine --version` in the bundle equals the app's version; the
  security test passes; Windows and Linux builds made and run once by
  hand (written down for the user, as web-6.md did).
- After stage 8: the security audit before the first release (Fable 5.1,
  max): the Electron surface, the IPC handlers' checks, the file paths.

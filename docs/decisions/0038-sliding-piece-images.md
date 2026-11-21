# 0038: Images on sliding puzzle pieces

**Status:** accepted; `core/shapelogic/grid/shape_image.h` (`ShapeImage`,
`ImageFit`), `core/shapelogic/grid/voxel.h` (`getImage`, `setImage`; moved
by `translate`, `VoxelBricks::transform`, `scale`, `scaleDown`),
`core/shapelogic/model/puzzle_image.h` (`PuzzleImage`, `sniffImage`,
`imageHash`), `core/shapelogic/model/puzzle.h` (`addImage`, `findImage`,
`images`, `imageUsed`), `core/shapelogic/io/puzzle_file.cpp`
(`readShapeImage`, `readImages`, `writeVoxel`, `writePuzzle`),
`core/shapelogic/app/document_commands.cpp` (`setShapeImage`,
`clearShapeImage`), `core/shapelogic/mesh/draw_mesh.cpp` (`imageUv`),
`core/shapelogic/base/base64.h`, `api/shape.json` (`shape.setImage`,
`shape.clearImage`, `shape.imageData`), `api/types.json` (`ShapeImage`,
`MeshFrame`'s `imageUv`), `web/src/renderer/entities/image-dialog.ts`,
`web/src/renderer/shell/piece-images.ts` and `picture-math.ts`, the 2D
grid (`grid2d.ts`: `paintPicture`) and the face material
(`view3d/material.ts`: `setPicture`)

## Context
A sliding puzzle's pieces are often tiles with a picture on them. The user
asked for a picture from the computer to be put on a piece, in the Shapes
tab's Shape color area beside the pencil, laid over the piece's rectangle
and shown only on the cells that hold a voxel. The original program has
nothing like it, and the .xmpuzzle format has no place for binary data.
Decided with the user: a picture per piece now, with room to spread one
picture over the whole goal later; fitted by Crop or Stretch, chosen in a
dialog with a preview; and the picture turns, flips and moves with the
piece.

## Decision
**The file.** The pictures are a table at puzzle level, and each piece
says which one it shows and how:

```xml
<images>
  <image id="0" type="image/png" width="64" height="48">iVBORw0KGgo…</image>
</images>
<shapes>
  <voxel x="3" y="2" z="1" … image="0" imageFit="crop"
         imageSrc="8 0 48 48" imageBox="0 0 2 2" imageTurn="1" type="0">…</voxel>
```

- `<image>` holds the file's bytes in base64, unchanged: a PNG or a JPEG
  (`sniffImage` reads the type and size from the bytes and refuses
  anything else, or over 8192 pixels a side or 32 MiB). The table is
  written before `<shapes>` and keeps only the pictures some shape shows.
  Two shapes with the same picture share one entry.
- `image` is the entry's id. `imageSrc` is the part of the picture shown,
  `x y width height` in whole pixels (no floating point in the file).
  `imageBox` is the cell rectangle it covers, in the shape's own grid.
  `imageTurn` (0 to 7) is the turn and mirror of the picture on it.
  `imageFit` (`crop`, `stretch`) is how it was fitted, so the dialog
  opens with the same choice.

It cannot be a child of `<voxel>`: the reader keeps an element's text only
when it has no children (`xml_document.cpp`), so a child would wipe the
cells. Puzzle-level children and voxel attributes this version does not
know are already kept on load and save (decision 0008), so ShapeLogic
versions before this one keep the pictures when they open and save a file.
The original program passes over them; it does not need to open our files
(decision 0006, as 0021 did for the sliding marks). The format version
stays 2. A bad picture (unknown id, bytes that are not the type stated, a
bad box or turn) is dropped and the file loads: the cells matter more.

**The map.** Seen from above (+Z), for (u, v) in [0, 1]² across `src`
(u to the right, v down the picture):

    q = M(turn) (u − ½, ½ − v),   point = box corner + (q + ½) · box size

`M(turn)` is (turn mod 4) quarter turns counter-clockwise after a mirror
in x when turn ≥ 4; turn 0 has the picture's top at the high-y side. When
the cells move, so does the picture: `translate` moves the box,
`VoxelBricks::transform` maps the box's corners with the cell matrix and
composes its 2×2 part into the turn (a turn that tips the shape, which a
sliding grid has none of, takes the picture off), `scale` and `scaleDown`
scale the box. The unit test turns, flips, nudges and prunes an L and
checks that each filled cell shows the same point of the picture.

**Applying.** `shape.setImage` lays a picture (new bytes, or an id the
puzzle has) over the shape's bounding box, upright. Only a sliding
puzzle's shapes take one, not an empty shape. A board (a start/goal
shape) takes one too (added 2026-11-21): its box is its floor's (the
filled and variable cells; the walls are empty cells), and the picture
lies on the floor under the pieces. The app
shrinks a picture to 2048 pixels and saves it as a PNG or JPEG before it
sends it, since base64 makes it a third bigger and gzip barely shrinks
picture files. The dialog works out `src`: Stretch takes the whole picture,
Crop the middle part with the box's proportions.

**Undo.** A `Voxel` holds only the small `ShapeImage`; the bytes are in the
puzzle's table behind a `shared_ptr`, so the copy of a shape an undo step
keeps stays small. Pictures are never taken out of the table while the
document is open (a later redo may need them); the file leaves out those
no shape shows.

**Drawing.** The snapshot names each shape's picture by id and hash
(`ShapeSummary.image`), never with its bytes, because the whole snapshot is
sent on every change. The app fetches the bytes once per hash
(`shape.imageData`) and decodes them once. The 2D grid fills the filled
cells with an SVG pattern of the picture (a data URL: the page's CSP
allows `data:` images, not `blob:`). For 3D the engine puts in each mesh,
per vertex on a face looking up, its place in the picture (`imageUv`).
Turned meshes are made from a turned copy of the shape whose picture
turned with it, so the solution player, the trays and the pieces scene
need no transform arithmetic of their own. The shader samples the picture
there in place of the piece colour (over the checker, under the colour
view's restriction colours), lit as the faces are.

## Consequences
- Old files and files without pictures read and write as before (the
  round-trip and oracle files have none).
- One picture over the whole goal layout needs no new format: each piece
  shows the same `image` with the `imageSrc` its goal place covers. That
  action, and pictures in the SVG and GLB exports, are left for later.
- A picture is drawn with the 3D view's light, so it looks darker on faces
  turned away from it, as the piece colours do.
- A board's picture shows in the Shapes and Puzzle tabs' 2D grids (under
  the start and goal marks, which keep their chequer), the Puzzle tab's
  3D trays and the Play tab's floor (under the goals' 30% tint and the
  pieces). The Solver's 3D view draws a sliding board as wall lines only,
  with no floor, so it shows none there.

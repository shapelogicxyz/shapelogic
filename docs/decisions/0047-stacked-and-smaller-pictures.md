# 0047: Several pictures on a sliding shape, and pictures smaller than it

**Status:** accepted; `core/shapelogic/grid/shape_image.h` (`ShapeImages`,
`kMaxShapeImages`, `overlapsPicture`), `core/shapelogic/grid/voxel.h`
(`getImages`, `setImages`), `core/shapelogic/grid/tray_border.h`
(`TrayBorder::images`), `core/shapelogic/io/puzzle_file.cpp`
(`readShapeImages`, `imagePrefix`, `isImageAttribute`),
`core/shapelogic/app/document_commands.cpp` (`setShapeImages`),
`api/types.json` (`ShapeSummary.images`, `TrayBorder.images`,
`ImageLayer`, `PixelRect`), `api/shape.json` (`shape.setImages`),
`web/src/renderer/shell/piece-images.ts` (`stackPicture`),
`web/src/renderer/shell/picture-math.ts` (`dragRect`, `sourceFromRect`),
`web/src/renderer/entities/image-dialog.ts`, `view3d/material.ts` and
`view3d/tray-border.ts` (the shaders)

## Context
Decision 0038 put one picture on a sliding piece or board, and its Manual
fit kept the picture over the whole box: `src` had to lie inside the
picture. The user asked for more freedom in Manual: a picture sized down
below the piece or tray, moved until it is about to leave the box
altogether, and several pictures on one shape. In the dialog each picture
is a tile with an X beside the button; dragging a tile reorders them; the
button becomes Additional image… once there is a picture, in Manual only,
and the new picture goes on top. Crop and Stretch keep one picture, with
Choose image… replacing it.

## Decision
**The model.** A shape holds a stack, `ShapeImages`, the bottom picture
first (at most 16); so does a tray's border (decision 0046). Every
picture of a stack lies over the same box with the same turn: the stack
is applied in one go over the shape's bounding box, and the tool tabs'
moves, turns and scalings change each picture alike. `getImage()` stays
as the bottom picture, `setImage()` as a stack of one.

**A picture smaller than its box.** `src` may reach past the picture's
edges (x or y below 0, or past its width or height), as long as it
overlaps the picture by a pixel (`overlapsPicture`). The formula of
`ShapeImage` is unchanged: the part `src` spans the box, and where it
holds no picture the shape's color shows.

**The file.** The bottom picture keeps the attributes it had (`image`,
`imageFit`, `imageSrc`, `imageBox`, `imageTurn`), so a file with one
picture is as before and an older version reads the bottom one; the next
are `image2…`, `image3…` with the same suffixes, and `borderImage2…` for a
border. A picture whose id the file lacks, or whose part shown misses it,
is dropped. Older versions keep the numbered attributes as attributes
they do not know (decision 0008).

**The API.** `ShapeSummary.image` and `TrayBorder.image` became `images`
(empty for none). `shape.setImages` replaces a stack in one undo step
(none when it is the same); `shape.setImage` sets a stack of one;
`shape.clearImage` takes them all off.

**Drawing.** The engine's mesh carries one set of picture coordinates
(the bottom picture's box and turn, which the stack shares). The page
draws a stack, or one picture whose `src` reaches past it, once into a
picture over the box (`stackPicture`: as many pixels a cell as the
sharpest picture, at most 2048 a side, transparent where no picture
reaches), cached by the pictures and parts shown, and hands the views
that one picture with `src` its whole: the 2D grids, Play and the 3D
views draw it as they drew one picture. The 2D views already lay a
picture over the shape's color; the 3D shaders now mix the picture over
the color by its alpha and keep the color outside the picture, so a
transparent PNG shows the piece's color through it too.

**The dialog.** Manual frames the box and the active picture together,
so a picture far smaller than the box leaves the box in view. A drag may
shrink a picture to 12 preview pixels and move it until 2 preview pixels
of it are left over the box. A picture placed in Manual keeps its place
when the fit goes to Crop or Stretch and back; one not yet placed starts
from what the fit before showed. An additional picture starts at half
the box's size in its middle, so the ones under it still show. Crop and
Stretch send only the active picture; the others stay in the dialog until
it closes. Taking every tile off and applying removes the pictures.

## Consequences
- A board's snapshot row has `images` in place of `image`; the fake
  engine's sessions were recorded again.
- A piece with a stack costs one drawing in the page per change of the
  stack, not per frame.

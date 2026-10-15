# Packaging resources (web/packaging)

electron-builder's `buildResources` (tools/dist.ts). The app icon is
ShapeLogic's (`assets/puzzle_icon.jpg`, 860 by 860); the document icons are the
original BurrTools's (`../burrtools_rotations/mac/`, GPL-3.0):

- `icon.icns`: the app's icon on macOS, made from `assets/puzzle_icon.jpg`
  on a 824-pixel rounded tile (radius 185, transparent margin, soft
  shadow, as Apple's template; the JPEG's near-white noise snapped to
  white) in a 1024 canvas, then `sips` for the iconset's sizes and
  `iconutil`. macOS does not mask icons, so the tile is part of the image.
- `icon.png`: the same, 512 by 512, for Windows (electron-builder makes
  the .ico) and Linux.
- `document.icns`: `mac/BurrToolsDoc.icns`, the icon of `.xmpuzzle` and
  `.btsolve` files on macOS.
- `document.ico`: `mac/icon-source-doc.png` at 256 by 256
  (`sips -s format ico -Z 256`), the same on Windows.

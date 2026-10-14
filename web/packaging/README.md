# Packaging resources (web/packaging)

electron-builder's `buildResources` (tools/dist.ts). The app icon is
ShapeLogic's (`assets/sl_icon.png`, 64 by 64); the document icons are the
original BurrTools's (`../burrtools_rotations/mac/`, GPL-3.0):

- `icon.icns`: the app's icon on macOS, made from `assets/sl_icon.png`
  with `sips` and `iconutil` (upscaled; replace with a larger source).
- `icon.png`: the same, 512 by 512, for Windows (electron-builder makes
  the .ico) and Linux.
- `document.icns`: `mac/BurrToolsDoc.icns`, the icon of `.xmpuzzle` and
  `.btsolve` files on macOS.
- `document.ico`: `mac/icon-source-doc.png` at 256 by 256
  (`sips -s format ico -Z 256`), the same on Windows.

# Packaging resources (web/packaging)

electron-builder's `buildResources` (tools/dist.ts). The icons are the
original BurrTools's (`../burrtools_rotations/mac/`, GPL-3.0):

- `icon.icns`: `mac/BurrTools.icns`, the app's icon on macOS.
- `icon.png`: the same, 512 by 512 (`sips -s format png -Z 512 icon.icns`),
  for Windows (electron-builder makes the .ico) and Linux.
- `document.icns`: `mac/BurrToolsDoc.icns`, the icon of `.xmpuzzle` and
  `.btsolve` files on macOS.
- `document.ico`: `mac/icon-source-doc.png` at 256 by 256
  (`sips -s format ico -Z 256`), the same on Windows.

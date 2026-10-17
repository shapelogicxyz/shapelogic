---
title: Linux and graphics
description: Install ShapeLogic on Linux, and what to do when it says it cannot draw its 3D views.
---

ShapeLogic draws its puzzles with WebGL2. This page covers the Linux downloads
and what to try when the app tells you WebGL2 is not available.

## Which download to use

Each release has a Linux file for Intel and AMD (x64) and one for ARM (arm64),
as an `.AppImage` and as a `.deb`.

- **`.deb`** (Ubuntu, Debian and their relatives). Install it with
  `sudo apt install ./shapelogic-web_<version>_<arch>.deb`. This is the easier
  choice: it brings the libraries it needs, and adds ShapeLogic to the
  application menu with its icon.
- **`.AppImage`** (any distribution). Make it executable with `chmod +x` and run
  it. It needs two libraries that a fresh Ubuntu does not always have:
  - `zlib1g-dev`, which provides the unversioned `libz.so` the AppImage's
    launcher asks for;
  - FUSE 2: `libfuse2t64` on Ubuntu 24.04, `libfuse2` on older releases.

  Without FUSE you can still run it with `--appimage-extract-and-run`. A bare
  AppImage has no menu entry, so the desktop may show a generic icon for it.

## "ShapeLogic cannot draw its 3D views on this computer"

The 3D views need WebGL2. Chromium, the engine inside the app, keeps a
blocklist of graphics drivers it does not trust, and it blocks WebGL2 on some
real drivers and in most virtual machines. When ShapeLogic starts it checks
this. If WebGL2 is blocked it starts again once with the blocklist overridden,
using software rendering when there is no usable GPU. You only see the message
when that did not work either.

### Things to try

1. **Virtual machine.** Turn on 3D acceleration for the virtual machine (in
   VMware: Settings, Display, *Accelerate 3D graphics*) and restart the guest.
   Without it, expect slow software rendering at best.
2. **Update the graphics driver and Mesa**, then start ShapeLogic again.
3. **Start it by hand with the overrides**, to see the app's own errors:

   ```sh
   ./ShapeLogic-<version>-<arch>.AppImage --ignore-gpu-blocklist --enable-unsafe-swiftshader
   ```

   Software WebGL is slower than a GPU but draws correctly. If you installed the
   `.deb`, run `shapelogic` with the same two options.
4. **Check what Chromium reports.** Run the command above with
   `--enable-logging=stderr`, and look for `WebGL2 blocklisted` or the GPU
   errors that come before it.

The messages `vaInitialize failed` and `'--ozone-platform=wayland' is not
compatible with Vulkan` are noise from the graphics stack and do not stop the
app.

If none of this helps, open an issue on
[GitHub](https://github.com/shapelogicxyz/shapelogic/issues) with your
distribution, your graphics hardware (or virtual machine), and the output of
the command in step 3.

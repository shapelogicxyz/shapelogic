---
title: Linux
description: Install ShapeLogic on Linux, and what to do when it says it cannot draw its 3D views.
---

ShapeLogic has a Linux based GUI that has been tested on Ubuntu Desktop. This
page covers the Linux downloads, how the app checks your graphics when it
starts, and what to try when it tells you WebGL2 is not available.

## Which download to use

Each release has a Linux file for Intel and AMD (x64) and one for ARM (arm64),
as an `.AppImage` and as a `.deb`.

- **`.deb`** (Ubuntu, Debian and their relatives). Install it with
  `sudo apt install ./` followed by the file's name, for example
  `sudo apt install ./ShapeLogic-v1.0-linux-x64.deb`. This is the easier
  choice: it brings the libraries it needs, and adds ShapeLogic to the
  application menu and the dock with its icon. The package is called
  `shapelogic`, so `sudo apt remove shapelogic` removes it. To install the same
  version again, add `--reinstall`.
- **`.AppImage`** (any distribution). Make it executable with `chmod +x` and run
  it. It needs two libraries that a fresh Ubuntu does not always have:
  - `zlib1g-dev`, which provides the unversioned `libz.so` the AppImage's
    launcher asks for;
  - FUSE 2: `libfuse2t64` on Ubuntu 24.04, `libfuse2` on older releases.

  Without FUSE you can still run it with `--appimage-extract-and-run`. A bare
  AppImage has no menu entry, so the desktop may show a generic icon for it.

## How ShapeLogic checks your graphics

The 3D views need WebGL2. Chromium, the engine inside the app, keeps a
blocklist of graphics drivers it does not trust, and it blocks WebGL2 on some
real drivers and in most virtual machines. So when ShapeLogic starts on Linux:

1. It asks a hidden page for a WebGL2 context. If it gets one, the app opens.
2. If not, it starts again once with the blocklist overridden, allowing
   software rendering when there is no usable GPU, and asks again.
3. Only when that fails too does it show "ShapeLogic cannot draw its 3D views
   on this computer".

The second start is normal in a virtual machine: the window may take a few
seconds longer to appear the first time.

### Which renderer is in use

Start ShapeLogic from a terminal and it prints one line about the check:

```text
ShapeLogic: WebGL2 status undefined; context in a page: yes (renderer: ANGLE (VMware Inc., SVGA3D; build: RELEASE;  LLVM;, OpenGL ES 3.1))
```

The renderer names the GPU that draws the 3D views. `SwiftShader` there means
software rendering: it draws correctly but is slower. When the first try is
refused, the line says `no` and the second start's line does not reach the
terminal. To see it, start the second try yourself:

```sh
shapelogic --ignore-gpu-blocklist --enable-unsafe-swiftshader --shapelogic-gpu-retry
```

## "ShapeLogic cannot draw its 3D views on this computer"

### Things to try

1. **Virtual machine.** Turn on 3D acceleration for the virtual machine and
   restart the guest. In VMware Fusion and Workstation it is Settings, Display,
   *Accelerate 3D graphics*. Without it, expect slow software rendering at
   best. ShapeLogic has been tested in VMware Fusion on Apple silicon with
   Ubuntu Desktop (arm64), where it draws on VMware's virtual GPU.
2. **Update the graphics driver and Mesa**, then start ShapeLogic again.
3. **Try X11 instead of Wayland.** Some drivers cannot give Chromium a window
   buffer under Wayland, and the window then never appears. Start it with:

   ```sh
   shapelogic --ozone-platform=x11 --ignore-gpu-blocklist
   ```

   For the AppImage, run the file with the same two options.
4. **Check what Chromium reports.** Add `--enable-logging=stderr` to the
   command above, and look for `WebGL2 blocklisted` or the GPU errors that
   come before it.

These messages are noise from the graphics stack and do not stop the app:
`vaInitialize failed`, `'--ozone-platform=wayland' is not compatible with
Vulkan`, `GetVSyncParametersIfAvailable() failed` and `GPU stall due to
ReadPixels`.

### Nothing happens when I start it

ShapeLogic runs once per user: a second start hands its files to the first and
quits. If an earlier start went wrong and left a ShapeLogic process behind
without a window, every new start is handed to that process. Check for one
and end it:

```sh
pgrep -af ShapeLogic
pkill -f /opt/ShapeLogic/shapelogic
```

If none of this helps, open an issue on
[GitHub](https://github.com/shapelogicxyz/shapelogic/issues) with your
distribution, your graphics hardware (or virtual machine), and the output of
`shapelogic --enable-logging=stderr` started from a terminal.

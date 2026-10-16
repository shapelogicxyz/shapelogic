---
title: Install
description: Download and install ShapeLogic on macOS, Windows or Linux.
---

Download the installer for your system from the
[latest release](https://github.com/shapelogicxyz/shapelogic/releases/latest)
on GitHub.

| System | File | Notes |
|---|---|---|
| macOS (Apple silicon or Intel) | `.dmg` | Open it and drag ShapeLogic to Applications. Release builds are signed and notarised by Apple. |
| Windows | `.exe` installer | Not code-signed yet, so Windows SmartScreen may warn. Choose **More info**, then **Run anyway**. |
| Linux (Intel or ARM) | `.AppImage` or `.deb` | Make the AppImage executable (`chmod +x`) and run it, or install the `.deb` with your package manager. |

ShapeLogic opens `.xmpuzzle` files, the format BurrTools uses, and saves them
too, so puzzles move between the two programs.

### The macOS `.zip`

Each macOS release has a `.zip` as well as a `.dmg`, for Apple silicon and for
Intel. The `.zip` holds the same signed `ShapeLogic.app` directory that the
`.dmg` installs. Unzipping it gives you the app directly, with no disk image to
open.

You might prefer it when you:

- want to put the app somewhere other than Applications, such as an external
  drive or a folder in your home directory, without mounting and ejecting a disk
  image;
- install from a script or the command line, where `unzip` or `ditto -xk` is
  easier to automate than a `.dmg`;
- deploy it to several Macs, or want to keep more than one version side by
  side, since each unzipped `ShapeLogic.app` stands alone.

Otherwise the `.dmg` is the usual choice. Either way, pick the file for your
Mac: the Apple silicon (arm64) file for M-series Macs, the Intel (x64) file for
older ones.

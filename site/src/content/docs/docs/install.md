---
title: Install
description: Download and install ShapeLogic on macOS, Windows or Linux.
---

Download the installer for your system from the
[latest release](https://github.com/shapelogicxyz/shapelogic/releases/latest)
on GitHub.

| System | File | Notes |
|---|---|---|
| macOS (Apple silicon or Intel) | `.dmg` | Open it and drag ShapeLogic to Applications. Release builds are signed and notarised by Apple. See [macOS](/docs/macos/) for the `.zip` download and which file fits your Mac. |
| Windows | `.exe` installer | Not code-signed yet, so Windows SmartScreen may warn. Choose **More info**, then **Run anyway**. See [Windows](/docs/windows/). |
| Linux (Intel or ARM) | `.AppImage` or `.deb` | Make the AppImage executable (`chmod +x`) and run it, or install the `.deb` with your package manager. See [Linux](/docs/linux/) for the libraries the AppImage needs and what to do if WebGL2 is unavailable. |

ShapeLogic opens `.xmpuzzle` files, the format BurrTools uses, and saves them
too, so puzzles move between the two programs.

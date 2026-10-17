---
title: macOS
description: Install ShapeLogic on macOS, from the .dmg or the .zip.
---

ShapeLogic runs on Macs with Apple silicon and with Intel processors. This page
covers which download to pick, the two ways to install it, and the command
line tool.

## Which download to use

Each release has macOS files for Apple silicon (arm64), for M-series Macs, and
for Intel (x64), for older ones. To see which your Mac has, open the Apple menu
and choose **About This Mac**: it lists a **Chip** (Apple silicon) or a
**Processor** (Intel).

Release builds are signed and notarised by Apple, so macOS opens them without
a warning.

## The `.dmg`

The usual choice. Open it and drag ShapeLogic to Applications, then eject the
disk image.

## The `.zip`

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

Otherwise the `.dmg` is the usual choice.

## Puzzle files

ShapeLogic registers itself for `.xmpuzzle` puzzle files and `.slsolve` paused
solves, so double-clicking one in Finder opens it in ShapeLogic.

## The command line tool

Choose **ShapeLogic ▸ Install Command Line Tool…** to put `shapelogic_cli` in
your command path. See [Command Line Tool](/docs/command-line/#getting-the-tool).

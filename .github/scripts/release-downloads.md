## Which file to download

Each system has more than one file. Most people need only the file marked **Most people** below. For more detail, see [Install](https://shapelogic.xyz/docs/install/).

### macOS

First choose the file for your Mac's processor. Open the Apple menu and choose **About This Mac**. If it lists a **Chip** (M1, M2, M3 and so on), use **arm64**. If it lists a **Processor** (Intel), use **x64**.

| File | What it is |
|---|---|
| `ShapeLogic-{version}-macos-arm64.dmg` | **Most people with Apple silicon.** A disk image: open it and drag ShapeLogic to Applications. |
| `ShapeLogic-{version}-macos-x64.dmg` | **Most people with Intel.** The same, for Intel Macs. |
| `ShapeLogic-{version}-macos-arm64.zip` | The same app in a zip, with no disk image to open. Useful for installing from a script, putting the app somewhere other than Applications, or keeping several versions side by side. |
| `ShapeLogic-{version}-macos-x64.zip` | The same, for Intel Macs. |

All macOS files are signed and notarised by Apple.

### Windows

| File | What it is |
|---|---|
| `ShapeLogic-{version}-windows-x64-setup.exe` | **Everyone on Windows.** The installer, for 64-bit Windows. |

The installer is not code-signed yet, so Windows SmartScreen may warn you. Choose **More info**, then **Run anyway**.

### Linux

First choose the file for your processor. Run `uname -m` in a terminal. If it prints `x86_64`, use **x64** (Intel or AMD). If it prints `aarch64`, use **arm64** (ARM).

| File | What it is |
|---|---|
| `ShapeLogic-{version}-linux-x64.deb` | **Most people on Ubuntu, Debian and related systems.** A package. Install it with `sudo apt install ./ShapeLogic-{version}-linux-x64.deb`. It brings the libraries it needs and adds ShapeLogic to the application menu. |
| `ShapeLogic-{version}-linux-arm64.deb` | The same, for ARM. |
| `ShapeLogic-{version}-linux-x64.AppImage` | **Any Linux distribution.** A single file that runs without installing. Make it executable with `chmod +x` and run it. It needs FUSE 2 (`libfuse2t64` on Ubuntu 24.04, `libfuse2` on older releases) and `zlib1g-dev`. It is not added to the application menu. |
| `ShapeLogic-{version}-linux-arm64.AppImage` | The same, for ARM. |

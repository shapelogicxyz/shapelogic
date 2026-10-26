#!/usr/bin/env python3
"""Prepare ShapeLogic release files from the Release workflow's artifacts.

The artifacts are either the zips the Actions page downloads
(artifacts/linux-x64.zip, macos-arm64-notarised.zip, ...) or the folders
`gh run download` and actions/download-artifact unpack them into
(artifacts/linux-x64/, macos-arm64-notarised/, ...):

    python3 .github/scripts/prepare-release.py artifacts v1.4 [--out DIR]

The installers come out renamed (ShapeLogic-v1.4-linux-x64.deb, ...) in DIR,
by default release/release_v1.4 under the folder it is run from. The macOS
files come from the *-notarised artifacts (stapled by the Release workflow,
or by the Staple workflow as a fallback), never the plain macos-* ones,
which are unstapled. publish.yml runs it to attach the files to the draft
release. Exits with an error unless every file below is found.
"""

import argparse
import shutil
import sys
import zipfile
from pathlib import Path

# (artifact, extension inside it, release file name after the prefix)
FILES = [
    ("linux-x64", ".AppImage", "linux-x64.AppImage"),
    ("linux-x64", ".deb", "linux-x64.deb"),
    ("linux-arm64", ".AppImage", "linux-arm64.AppImage"),
    ("linux-arm64", ".deb", "linux-arm64.deb"),
    ("macos-arm64-notarised", ".dmg", "macos-arm64.dmg"),
    ("macos-arm64-notarised", ".zip", "macos-arm64.zip"),
    ("macos-x64-notarised", ".dmg", "macos-x64.dmg"),
    ("macos-x64-notarised", ".zip", "macos-x64.zip"),
    ("windows-x64", ".exe", "windows-x64-setup.exe"),
]


def one_of(names: list[str], ext: str, where: str) -> str:
    """The one name with this extension, or exit."""
    matches = [n for n in names if n.endswith(ext)]
    if len(matches) != 1:
        sys.exit(f"Error: expected 1 '*{ext}' in {where}, found {len(matches)}: {matches}")
    return matches[0]


def copy_from_zip(zip_path: Path, ext: str, dest: Path) -> str:
    """Copies the zip's one file with this extension to dest; returns its name in the zip."""
    with zipfile.ZipFile(zip_path) as archive:
        members = {m.filename: m for m in archive.infolist() if not m.is_dir()}
        name = one_of(list(members), ext, str(zip_path))
        # copy the bytes out by name rather than extract, so the path
        # inside the archive never decides where the file lands
        with archive.open(members[name]) as src, open(dest, "wb") as dst:
            shutil.copyfileobj(src, dst)
    return name


def copy_from_dir(folder: Path, ext: str, dest: Path) -> str:
    """Copies the folder's one file with this extension to dest; returns its path in the folder."""
    files = {str(p.relative_to(folder)): p for p in folder.rglob("*") if p.is_file()}
    name = one_of(list(files), ext, str(folder))
    shutil.copyfile(files[name], dest)
    return name


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Extract and rename the ShapeLogic installers from the "
        "Release workflow's artifacts, ready to attach to a release.",
    )
    parser.add_argument("folder", help="the folder holding the artifacts (zips or folders)")
    parser.add_argument("version", help="the release's version, e.g. v1.4")
    parser.add_argument("--out", help="where the files go (default: release/release_<version>)")
    args = parser.parse_args()

    version = args.version
    src_dir = Path(args.folder).resolve()
    if not src_dir.is_dir():
        sys.exit(f"Error: folder '{src_dir}' not found")

    out_dir = Path(args.out) if args.out else Path.cwd() / "release" / f"release_{version}"
    if out_dir.exists():
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True)

    prefix = f"ShapeLogic-{version}"
    missing = []
    for artifact, ext, suffix in FILES:
        dest = out_dir / f"{prefix}-{suffix}"
        zip_path = src_dir / f"{artifact}.zip"
        folder = src_dir / artifact
        if zip_path.is_file():
            name = copy_from_zip(zip_path, ext, dest)
        elif folder.is_dir():
            name = copy_from_dir(folder, ext, dest)
        else:
            missing.append(artifact)
            continue
        print(f"{artifact}: {name} -> {dest.name}")

    print(f"\nRelease files in {out_dir}:")
    for f in sorted(out_dir.iterdir()):
        print(f"  {f.stat().st_size / 1e6:8.1f} MB  {f.name}")

    if missing:
        for artifact in sorted(set(missing)):
            print(f"Error: artifact {artifact} not found in {src_dir}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()

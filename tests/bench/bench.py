#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Solve-speed comparison of the new `bt` with the original `burrTxt`.

Both are run back to back on the same machine, alternating, and the best of
`--repeat` runs is kept: timings drift 5-10% as a machine heats up, so only
numbers from one run of this script compare. Each program reports the time
of its own solve (reduce plus search, without loading), which is what is
compared.

The default set times the assemblers (0 and 1, BurrTools 2) and, with
-d and -R, the take-apart; --plain turns the vector search off in both
programs; --set glar is the long one; --set identical is boxes filled with
many pieces of one shape and with piece ranges (written by the script),
where every cover found is checked against its rotations, and --set
identical-long the same at a size that takes the original a minute a run.
The gate (decision 0007, from stage 8) is "within 5% of the original or
faster" on every row.

    python3 tests/bench/bench.py                    # the default set, 1 and all threads
    python3 tests/bench/bench.py --set glar          # Burr-Glar (3.1 million assemblies, slow)
    python3 tests/bench/bench.py --set identical     # many pieces of one shape, piece ranges
    python3 tests/bench/bench.py --plain             # without the vector search
    python3 tests/bench/bench.py --threads 1 --repeat 5
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent

SUMMARY_RE = re.compile(r"(\d+) assemblies and (\d+) solutions found with (\d+) iterations in ([\d.]+) seconds")

# The twelve pentominoes (from the original's test_solver.cpp).
PENTOMINOES = [
    [".##", "##.", ".#."], ["#####"], ["#...", "####"], ["##..", ".###"],
    ["##", "##", "#."], ["###", ".#.", ".#."], ["#.#", "###"], ["#..", "#..", "###"],
    ["#..", "##.", ".##"], [".#.", "###", ".#."], ["..#.", "####"], ["##.", ".#.", ".##"],
]


def pentomino_file(w: int, h: int, path: Path) -> Path:
    """The pentominoes in a w x h tray as a puzzle file (plain XML)."""
    shapes = [f'    <voxel x="{w}" y="{h}" z="1" type="0">{"#" * (w * h)}</voxel>']
    for art in PENTOMINOES:
        sx, sy = len(art[0]), len(art)
        cells = "".join("#" if art[y][x] == "#" else "_" for y in range(sy) for x in range(sx))
        shapes.append(f'    <voxel x="{sx}" y="{sy}" z="1" type="0">{cells}</voxel>')
    parts = "\n".join(f'        <shape id="{i}" count="1"/>' for i in range(1, 13))
    path.write_text(
        '<?xml version="1.0"?>\n<puzzle version="2">\n  <gridType type="0"/>\n  <colors/>\n'
        f'  <shapes>\n{chr(10).join(shapes)}\n  </shapes>\n'
        f'  <problems>\n    <problem>\n      <shapes>\n{parts}\n      </shapes>\n'
        '      <result id="0"/>\n      <bitmap/>\n    </problem>\n  </problems>\n'
        '  <comment/>\n</puzzle>\n')
    return path


# Shapes for the boxes of the identical sets.
DOMINO = ["##"]
I3 = ["###"]
L3 = ["##", "#."]
L4 = ["###", "#.."]
T4 = ["###", ".#."]
TETROMINOES = [["####"], ["##", "##"], T4, L4, ["##.", ".##"]]


def box_file(path: Path, dims: tuple[int, int, int], parts: list[tuple]) -> Path:
    """A box of dims to fill with the parts, each (art, count) or (art, min, max)."""
    w, h, d = dims
    shapes = [f'    <voxel x="{w}" y="{h}" z="{d}" type="0">{"#" * (w * h * d)}</voxel>']
    lines = []
    for i, part in enumerate(parts, 1):
        art = part[0]
        sx, sy = len(art[0]), len(art)
        cells = "".join("#" if art[y][x] == "#" else "_" for y in range(sy) for x in range(sx))
        shapes.append(f'    <voxel x="{sx}" y="{sy}" z="1" type="0">{cells}</voxel>')
        if len(part) == 2:
            lines.append(f'        <shape id="{i}" count="{part[1]}"/>')
        else:
            lines.append(f'        <shape id="{i}" min="{part[1]}" max="{part[2]}"/>')
    path.write_text(
        '<?xml version="1.0"?>\n<puzzle version="2">\n  <gridType type="0"/>\n  <colors/>\n'
        f'  <shapes>\n{chr(10).join(shapes)}\n  </shapes>\n'
        f'  <problems>\n    <problem>\n      <shapes>\n{chr(10).join(lines)}\n      </shapes>\n'
        '      <result id="0"/>\n      <bitmap/>\n    </problem>\n  </problems>\n'
        '  <comment/>\n</puzzle>\n')
    return path


def puzzle_sets(old_repo: Path, tmp: Path) -> dict[str, list[tuple[str, Path, list[str]]]]:
    """name -> [(label, file, extra args)]."""
    tic = old_repo / "tictest" / "performance" / "puzzles"
    pentomino = pentomino_file(10, 6, tmp / "pentomino_6x10.xmpuzzle")
    default = [
        ("pentomino 6x10 (-r)", pentomino, ["-r"]),
        ("Climburr_OldVer (-r)", tic / "5x5x5_TICs" / "Crowell_Lohe_Burrs" / "Climburr_OldVer.xmpuzzle", ["-r"]),
        ("CoverUp3 (-r)", tic / "ARCparent_Cubes" / "CoverUp3.xmpuzzle", ["-r"]),
        ("CornerCube (-r)", tic / "ARCparent_Cubes" / "CornerCube.xmpuzzle", ["-r"]),
        ("DraculasDentalDesaster", old_repo / "examples" / "DraculasDentalDesaster.xmpuzzle", []),
        ("SolidSixPieceBurrs (-r)", old_repo / "examples" / "SolidSixPieceBurrs.xmpuzzle", ["-r"]),
        ("DemoPieceGenerator", old_repo / "examples" / "DemoPieceGenerator.xmpuzzle", []),
        # the take-apart (stage 4): rotation take-apart of the slow TIC puzzles,
        # and Solid Six Piece Burrs' 588 assemblies taken apart
        ("Climburr_OldVer (-rdR)", tic / "5x5x5_TICs" / "Crowell_Lohe_Burrs" / "Climburr_OldVer.xmpuzzle",
         ["-r", "-d", "-R"]),
        ("CoverUp3 (-rdR)", tic / "ARCparent_Cubes" / "CoverUp3.xmpuzzle", ["-r", "-d", "-R"]),
        ("CornerCube (-rdR)", tic / "ARCparent_Cubes" / "CornerCube.xmpuzzle", ["-r", "-d", "-R"]),
        ("SolidSixPieceBurrs (-rd)", old_repo / "examples" / "SolidSixPieceBurrs.xmpuzzle", ["-r", "-d"]),
    ]
    glar = Path(os.environ.get("BT_BURR_GLAR", Path.home() / "git/burr-tools/burr-tools-clean/examples/Burr-Glar.xmpuzzle"))
    # Assembler 1 with nothing to break the symmetry on: every cover is
    # found in each orientation and checked. The tetrominoes have no
    # assembly (a T too many for the colouring): the search alone.
    identical = [
        ("20 dominoes 2x4x5", box_file(tmp / "domino_2x4x5.xmpuzzle", (5, 4, 2), [(DOMINO, 20)]), []),
        ("12 L-trominoes 3x3x4", box_file(tmp / "ltromino_3x3x4.xmpuzzle", (4, 3, 3), [(L3, 12)]), []),
        ("ranges 3x3x3", box_file(tmp / "range_3x3x3.xmpuzzle", (3, 3, 3),
                                  [(L3, 0, 9), (L4, 0, 6), (I3, 0, 9)]), []),
        ("3 of each tetromino 6x10", box_file(tmp / "tetromino_x3_6x10.xmpuzzle", (10, 6, 1),
                                              [(t, 3) for t in TETROMINOES]), []),
    ]
    identical_long = [
        ("32 dominoes 8x8", box_file(tmp / "domino_8x8.xmpuzzle", (8, 8, 1), [(DOMINO, 32)]), []),
        ("ranges 6x6", box_file(tmp / "range_6x6.xmpuzzle", (6, 6, 1),
                                [(DOMINO, 0, 18), (L3, 0, 12), (T4, 0, 9)]), []),
    ]
    return {
        "default": [e for e in default if e[1].exists()],
        "glar": [("Burr-Glar", glar, [])] if glar.exists() else [],
        "identical": identical,
        "identical-long": identical_long,
    }


def run(binary: Path, puzzle: Path, threads: int, extra: list[str], plain: bool, old: bool,
        timeout: float) -> tuple[float, int, int] | str:
    """(solve seconds, assemblies, iterations), or an error text."""
    argv = [str(binary), "-q", "-t", str(threads), *extra]
    env = dict(os.environ)
    if plain:
        if old:
            # the original's name for the switch, and a renamed checkout's
            env["BURRTOOLS_NO_SIMD"] = "1"
            env["SHAPELOGIC_NO_SIMD"] = "1"
        else:
            argv.append("--no-simd")
    argv.append(str(puzzle))
    try:
        proc = subprocess.run(argv, capture_output=True, text=True, timeout=timeout, env=env, check=False)
    except subprocess.TimeoutExpired:
        return "timeout"
    m = SUMMARY_RE.search(proc.stdout)
    if proc.returncode != 0 or not m:
        return f"exit {proc.returncode}: {(proc.stderr or proc.stdout).strip()[-120:]}"
    return float(m.group(4)), int(m.group(1)), int(m.group(3))


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--old-repo", type=Path,
                   default=Path(os.environ.get("BT_OLD_REPO", ROOT.parent / "burr-tools")))
    p.add_argument("--old-bin", type=Path, help="the original burrTxt (default: its build-rel, else build)")
    p.add_argument("--new-bin", type=Path, default=ROOT / "build" / "release" / "cli" / "shapelogic_cli")
    p.add_argument("--set", choices=["default", "glar", "identical", "identical-long"], default="default")
    p.add_argument("--threads", type=int, nargs="+", default=[1, os.cpu_count() or 1])
    p.add_argument("--repeat", type=int, default=5)
    p.add_argument("--plain", action="store_true", help="without the vector search (BURRTOOLS_NO_SIMD / --no-simd)")
    p.add_argument("--timeout", type=float, default=1800.0)
    args = p.parse_args()

    old_bin = args.old_bin or next((b for b in (args.old_repo / "build-rel" / "burrTxt",
                                                args.old_repo / "build" / "burrTxt") if b.exists()), None)
    if old_bin is None or not args.new_bin.exists():
        print("need both binaries: build the original (just build-release there) and `just build release` here",
              file=sys.stderr)
        return 2
    print(f"old: {old_bin}\nnew: {args.new_bin}\n")

    with tempfile.TemporaryDirectory() as tmpdir:
        entries = puzzle_sets(args.old_repo, Path(tmpdir))[args.set]
        if not entries:
            print("no puzzles found for this set", file=sys.stderr)
            return 2
        print(f"{'puzzle':28} {'threads':>7} {'old s':>9} {'new s':>9} {'new/old':>8}  assemblies")
        failed = False
        for label, puzzle, extra in entries:
            for threads in args.threads:
                best = {"old": None, "new": None}
                counts = {}
                for _ in range(args.repeat):
                    for who, binary in (("old", old_bin), ("new", args.new_bin)):
                        r = run(binary, puzzle, threads, extra, args.plain, who == "old", args.timeout)
                        if isinstance(r, str):
                            print(f"{label:28} {threads:7}  {who} failed: {r}")
                            failed = True
                            break
                        best[who] = r[0] if best[who] is None else min(best[who], r[0])
                        counts[who] = r[1]
                if best["old"] is None or best["new"] is None:
                    continue
                ratio = best["new"] / best["old"] if best["old"] > 0 else float("nan")
                same = "" if counts["old"] == counts["new"] else f"  DIFFER: {counts['old']} != {counts['new']}"
                print(f"{label:28} {threads:7} {best['old']:9.3f} {best['new']:9.3f} {ratio:8.2f}  {counts['new']}{same}")
                failed |= bool(same)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

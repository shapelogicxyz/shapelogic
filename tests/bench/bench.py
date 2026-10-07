#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Solve-speed comparison of the new `bt` with the original `burrTxt`.

Both are run back to back on the same machine, alternating, and the best of
`--repeat` runs is kept: timings drift 5-10% as a machine heats up, so only
numbers from one run of this script compare. Each program reports the time
of its own solve (reduce plus search, without loading), which is what is
compared.

Stage 3: assembly only (assemblers 0 and 1, BurrTools 2), so both run
without -d. The stage 8 gate (decision 0007) is "within 5% or faster".

    python3 tests/bench/bench.py                    # the default set, 1 and all threads
    python3 tests/bench/bench.py --set glar          # Burr-Glar (3.1 million assemblies, slow)
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


def puzzle_sets(old_repo: Path, tmp: Path) -> dict[str, list[tuple[str, Path, list[str]]]]:
    """name -> [(label, file, extra args)]."""
    tic = old_repo / "tictest" / "performance" / "puzzles"
    pentomino = pentomino_file(10, 6, tmp / "pentomino_6x10.xmpuzzle")
    default = [
        ("pentomino 6x10 (-r)", pentomino, ["-r"]),
        ("pentomino 6x10 (-r, bt2)", pentomino, ["-r", "--solver", "bt2"]),
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
    return {
        "default": [e for e in default if e[1].exists()],
        "glar": [("Burr-Glar", glar, [])] if glar.exists() else [],
    }


def run(binary: Path, puzzle: Path, threads: int, extra: list[str], plain: bool, old: bool,
        timeout: float) -> tuple[float, int, int] | str:
    """(solve seconds, assemblies, iterations), or an error text."""
    argv = [str(binary), "-q", "-t", str(threads), *extra]
    env = dict(os.environ)
    if plain:
        if old:
            env["BURRTOOLS_NO_SIMD"] = "1"
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
                   default=Path(os.environ.get("BT_OLD_REPO", ROOT.parent / "burrtools_rotations")))
    p.add_argument("--old-bin", type=Path, help="the original burrTxt (default: its build-rel, else build)")
    p.add_argument("--new-bin", type=Path, default=ROOT / "build" / "release" / "cli" / "bt")
    p.add_argument("--set", choices=["default", "glar"], default="default")
    p.add_argument("--threads", type=int, nargs="+", default=[1, os.cpu_count() or 1])
    p.add_argument("--repeat", type=int, default=3)
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

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Speed and solution counts of ShapeLogic's CLI against upstream BurrTools'
`burrTxt`, or against an earlier ShapeLogic build: run only on request
(`just burrtools-compare`, `just bench`), never by the test suites.

Both programs are run back to back on the same machine, alternating, and
the best of `--repeat` runs is kept: timings drift 5-10% as a machine heats
up, so only numbers from one run of this script compare. Upstream's
burrTxt prints no solve time, so each run is timed from outside (wall
clock, loading included) for both programs. Every row also compares the
assemblies and solutions the two found; a row that differs fails the run.

Upstream BurrTools (../burr-tools, or BURRTOOLS_DIR; built with meson into
build-rel or build) has no take-apart with rotations, no BurrTools 2 solver
and no sliding or stacking puzzles, so only what both can solve is run.

    python3 tests/bench/bench.py --set corpus        # every regression puzzle both can solve
    python3 tests/bench/bench.py                     # the default set, 1 and all threads
    python3 tests/bench/bench.py --set glar          # Burr-Glar (3.1 million assemblies, slow)
    python3 tests/bench/bench.py --set identical     # many pieces of one shape, piece ranges
    python3 tests/bench/bench.py --plain             # without the vector search
    python3 tests/bench/bench.py --threads 1 --repeat 5

--session times the app's way through a solve (a SolveSession: the CLI's
--save, which keeps the solutions sorted and writes the solved file) of two
ShapeLogic CLIs, --old-bin (an earlier build's) and --new-bin: the default
rows go through the CLI's own loop, which the app never runs.

    python3 tests/bench/bench.py --session --old-bin ../before/shapelogic_cli
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
BURRTOOLS = Path(os.environ.get("BURRTOOLS_DIR", ROOT.parent / "burr-tools"))

# upstream's burrTxt ends the line after the iterations; ShapeLogic's adds the solve time
SUMMARY_RE = re.compile(r"(\d+) assemblies and (\d+) solutions found with (\d+) iterations")
# --save's line: the whole solve as the session counts it (prepare, reduce, search, take-aparts)
SESSION_RE = re.compile(r"(\d+) assemblies and (\d+) solutions found in ([\d.]+) seconds")

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


PUZZLES = ROOT / "tests" / "data" / "puzzles"
REGRESSION = PUZZLES / "regression"


def corpus_set() -> list[tuple[str, Path, list[str]]]:
    """Every puzzle of the regression suite's fast, full and slow tiers
    (tests/regression/corpus.txt), solved as upstream can: with -d, without
    the rotations (-R) it does not have; assembly only on the grids it
    cannot take apart (flag A). Its sliding and stacking tiers are left
    out: upstream has neither kind."""
    out = []
    for line in (ROOT / "tests" / "regression" / "corpus.txt").read_text().splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        tier, flags, pattern = line.split(None, 2)
        if tier in ("fast", "full", "slow"):
            for path in sorted(ROOT.glob(pattern)):
                out.append((path.stem, path, [] if "A" in flags else ["-d"]))
    return out


def puzzle_sets(tmp: Path) -> dict[str, list[tuple[str, Path, list[str]]]]:
    """name -> [(label, file, extra args)]."""
    pentomino = pentomino_file(10, 6, tmp / "pentomino_6x10.xmpuzzle")
    default = [
        ("pentomino 6x10 (-r)", pentomino, ["-r"]),
        ("deep_level_with_rotation (-r)", REGRESSION / "deep_level_with_rotation.xmpuzzle", ["-r"]),
        ("benchmark_crowell_never_finishes (-r)", REGRESSION / "benchmark_crowell_never_finishes.xmpuzzle",
         ["-r"]),
        ("benchmark_most_assemblies (-r)", REGRESSION / "benchmark_most_assemblies.xmpuzzle", ["-r"]),
        ("DraculasDentalDesaster", PUZZLES / "DraculasDentalDesaster.xmpuzzle", []),
        ("SolidSixPieceBurrs (-r)", PUZZLES / "SolidSixPieceBurrs.xmpuzzle", ["-r"]),
        ("DemoPieceGenerator", PUZZLES / "DemoPieceGenerator.xmpuzzle", []),
        # the take-apart: the slow puzzles' assemblies and Solid Six Piece
        # Burrs' 588 taken apart (without rotations, which upstream lacks)
        ("deep_level_with_rotation (-rd)", REGRESSION / "deep_level_with_rotation.xmpuzzle", ["-r", "-d"]),
        ("benchmark_crowell_never_finishes (-rd)", REGRESSION / "benchmark_crowell_never_finishes.xmpuzzle",
         ["-r", "-d"]),
        ("benchmark_most_assemblies (-rd)", REGRESSION / "benchmark_most_assemblies.xmpuzzle", ["-r", "-d"]),
        ("SolidSixPieceBurrs (-rd)", PUZZLES / "SolidSixPieceBurrs.xmpuzzle", ["-r", "-d"]),
    ]
    glar = Path(os.environ.get("SHAPELOGIC_BURR_GLAR", BURRTOOLS / "examples" / "Burr-Glar.xmpuzzle"))
    # Assembler 1 with nothing to break the symmetry on: every cover is
    # found in each orientation and checked. The tetrominoes have no
    # assembly (a T too many for the coloring): the search alone.
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
        "corpus": corpus_set(),
        "default": [e for e in default if e[1].exists()],
        "glar": [("Burr-Glar", glar, [])] if glar.exists() else [],
        "identical": identical,
        "identical-long": identical_long,
    }


def run_session(binary: Path, puzzle: Path, threads: int, extra: list[str], plain: bool,
                timeout: float) -> tuple[float, int, int] | str:
    """A solve through the session (--save): (seconds, assemblies, solutions), or an error text."""
    with tempfile.TemporaryDirectory() as out:
        # every solution kept (--limit 0), as far as the session keeps them: the lists' cost counts
        argv = [str(binary), "-t", str(threads), *[a for a in extra if a != "-r"], "--save",
                str(Path(out) / "solved.xmpuzzle"), "--restart", "--limit", "0",
                *(["--no-simd"] if plain else []), str(puzzle)]
        env = dict(os.environ, SHAPELOGIC_SLIDE_DIR=out, SHAPELOGIC_PANEX_DIR=out)
        try:
            proc = subprocess.run(argv, capture_output=True, text=True, timeout=timeout, env=env, check=False)
        except subprocess.TimeoutExpired:
            return "timeout"
    m = SESSION_RE.search(proc.stdout)
    if proc.returncode != 0 or not m:
        return f"exit {proc.returncode}: {(proc.stderr or proc.stdout).strip()[-120:]}"
    return float(m.group(3)), int(m.group(1)), int(m.group(2))


def run(binary: Path, puzzle: Path, threads: int, extra: list[str], plain: bool, old: bool,
        timeout: float) -> tuple[float, int, int] | str:
    """(wall seconds, assemblies, solutions), or an error text."""
    argv = [str(binary), "-q", "-t", str(threads), *extra]
    env = dict(os.environ)
    if plain:
        if old:
            env["BURRTOOLS_NO_SIMD"] = "1"
        else:
            argv.append("--no-simd")
    argv.append(str(puzzle))
    start = time.perf_counter()
    try:
        proc = subprocess.run(argv, capture_output=True, text=True, timeout=timeout, env=env, check=False)
    except subprocess.TimeoutExpired:
        return "timeout"
    wall = time.perf_counter() - start
    m = SUMMARY_RE.search(proc.stdout)
    if proc.returncode != 0 or not m:
        return f"exit {proc.returncode}: {(proc.stderr or proc.stdout).strip()[-120:]}"
    return wall, int(m.group(1)), int(m.group(2))


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--old-bin", type=Path,
                   help="the program to compare with (default: upstream's burrTxt in BURRTOOLS_DIR, "
                        "../burr-tools, under build-rel or build)")
    p.add_argument("--new-bin", type=Path, default=ROOT / "build" / "release" / "cli" / "shapelogic_cli")
    p.add_argument("--set", choices=["corpus", "default", "glar", "identical", "identical-long"], default="default")
    p.add_argument("--filter", help="only rows whose label contains this text")
    p.add_argument("--threads", type=int, nargs="+", default=[1, os.cpu_count() or 1])
    p.add_argument("--repeat", type=int, default=5)
    p.add_argument("--plain", action="store_true", help="without the vector search (BURRTOOLS_NO_SIMD / --no-simd)")
    p.add_argument("--session", action="store_true",
                   help="the app's path (--save, a SolveSession) of two ShapeLogic CLIs; needs --old-bin")
    p.add_argument("--timeout", type=float, default=1800.0)
    args = p.parse_args()

    if args.session and not args.old_bin:
        print("--session compares two ShapeLogic CLIs: give the earlier one as --old-bin", file=sys.stderr)
        return 2
    old_bin = args.old_bin or next((b for b in (BURRTOOLS / "build-rel" / "burrTxt",
                                                BURRTOOLS / "build" / "burrTxt") if b.exists()), None)
    if old_bin is None:
        print(f"upstream's burrTxt not found under {BURRTOOLS} (build-rel or build): build it there "
              "(meson setup build-rel --buildtype=release && meson compile -C build-rel), set BURRTOOLS_DIR, "
              "or give --old-bin", file=sys.stderr)
        return 2
    if not args.new_bin.exists():
        print(f"{args.new_bin} not found: just build release", file=sys.stderr)
        return 2
    print(f"old: {old_bin}\nnew: {args.new_bin}\n(seconds: wall clock, the best of {args.repeat})\n")

    with tempfile.TemporaryDirectory() as tmpdir:
        entries = [e for e in puzzle_sets(Path(tmpdir))[args.set] if not args.filter or args.filter in e[0]]
        if not entries:
            print("no puzzles found for this set", file=sys.stderr)
            return 2
        width = max(28, *(len(label) for label, _, _ in entries))
        print(f"{'puzzle':{width}} {'threads':>7} {'old s':>9} {'new s':>9} {'new/old':>8}  assemblies/solutions")
        failed = False
        for label, puzzle, extra in entries:
            for threads in args.threads:
                best = {"old": None, "new": None}
                counts = {}
                for _ in range(args.repeat):
                    for who, binary in (("old", old_bin), ("new", args.new_bin)):
                        if args.session:
                            r = run_session(binary, puzzle, threads, extra, args.plain, args.timeout)
                        else:
                            r = run(binary, puzzle, threads, extra, args.plain, who == "old", args.timeout)
                        if isinstance(r, str):
                            print(f"{label:{width}} {threads:7}  {who} failed: {r}")
                            failed = True
                            break
                        best[who] = r[0] if best[who] is None else min(best[who], r[0])
                        counts[who] = r[1:]
                    if best["old"] is None or best["new"] is None:
                        break
                if best["old"] is None or best["new"] is None:
                    continue
                ratio = best["new"] / best["old"] if best["old"] > 0 else float("nan")
                found = "/".join(map(str, counts["new"]))
                same = "" if counts["old"] == counts["new"] else \
                    f"  DIFFER: {'/'.join(map(str, counts['old']))} (old)"
                print(f"{label:{width}} {threads:7} {best['old']:9.3f} {best['new']:9.3f} {ratio:8.2f}  {found}{same}")
                failed |= bool(same)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The `shapelogic_cli` command line on the fixtures: options, refusals, the modes.

What it prints is compared with the original burrTxt by the harness
(`just oracle-text`, tests/oracle); this checks what holds without the
original repo: burrTxt's option rules and exit codes, sliding and stacking
puzzles solved with their options, and --save followed by -a and -x.

    python3 tests/cli/test_cli.py --bt build/dev/cli/shapelogic_cli
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
PUZZLES = ROOT / "tests" / "data" / "puzzles"
CUBE = PUZZLES / "CubeInCage.xmpuzzle"  # 96 assemblies, 1 solution (level 4.9.3)

BT: Path = Path()
failures: list[str] = []


def run(*args: str | Path, timeout: float = 300) -> subprocess.CompletedProcess:
    return subprocess.run([str(BT), *map(str, args)], capture_output=True, text=True, timeout=timeout,
                          check=False)


def check(name: str, cond: bool, detail: str = "") -> None:
    if not cond:
        failures.append(f"{name}: {detail}")
        print(f"FAIL {name}  {detail}")


def test_help_and_mistakes() -> None:
    r = run("-h")
    check("help", r.returncode == 0 and r.stdout.startswith("shapelogic_cli [options] file"), r.stdout[:80])
    check("help brick", "brick puzzles:" in run("-h", "brick").stdout)
    r = run("--help", "slider")
    check("help sliding", "sliding puzzles:" in r.stdout and "--nested-slides" in r.stdout and
          "Sliding Puzzle Solver" in r.stdout and "Fast" not in r.stdout, r.stdout[-200:])
    r = run("-h", "stacking")
    check("help stacking", "stacking puzzles:" in r.stdout and "Panex Solver" in r.stdout and
          "--panex-dir" in r.stdout and "--restart" in r.stdout, r.stdout[-200:])
    r = run()
    check("no file", r.returncode == 1 and "shapelogic_cli [options]" in r.stdout, str(r.returncode))
    for args, text in ((["--json", "-x"], "--json cannot be combined"),
                       (["-oa"], "cannot be clustered"),
                       (["-dz"], "unknown option -z"),
                       (["--bogus"], "unknown option --bogus"),
                       (["--solver", "nope"], "unknown solver 'nope' for a brick puzzle"),
                       (["-o", "5"], "there is no problem 5"),
                       (["--restart"], "go with --save"),
                       (["--nested-slides"], "--nested-slides is for sliding puzzles; this is a brick"),
                       (["--partial-nested"], "--partial-nested is for sliding puzzles"),
                       (["--high-memory"], "--high-memory is for sliding and stacking puzzles")):
        r = run(*args, CUBE)
        check(f"refused {args}", r.returncode == 2 and text in r.stderr, f"{r.returncode} {r.stderr!r}")
    # burrTxt ignored the Panex options on other puzzles
    r = run("-q", "--panex-dir", "x", "--panex-disk", "1", CUBE)
    check("--panex-dir on a brick puzzle", r.returncode == 0, f"{r.returncode} {r.stderr!r}")
    r = run("-t", "x", CUBE)
    check("-t not a number", r.returncode == 2 and r.stdout == "-t requires a non-negative number\n",
          repr(r.stdout))
    r = run(PUZZLES / "no-such-file.xmpuzzle")
    check("missing file", r.returncode == 2 and r.stdout.startswith("could not open input file"), r.stdout)


def test_stacking() -> None:
    f = PUZZLES / "test_stacking_hanoi.xmpuzzle"
    swap = PUZZLES / "stacking" / "stack_panex_swap_3.xmpuzzle"
    with tempfile.TemporaryDirectory() as tmp:
        # a Panex folder of the test's own, never the user's cache
        env = ["--panex-dir", str(Path(tmp) / "panex")]
        r = run("-q", *env, f)
        check("stacking without -d", r.returncode == 0 and
              r.stdout.startswith("1 assemblies and 0 solutions found in ") and
              r.stdout.endswith(" seconds (use -d to search for the moves)\n"), repr(r.stdout))
        r = run("-d", "-q", *env, f)
        check("stacking -d", r.returncode == 0 and "1 assemblies and 1 solutions" in r.stdout and
              "stack search: 7 moves deep" in r.stdout, repr(r.stdout))
        r = run("--json", *env, f)
        check("stacking --json", r.returncode == 0 and
              json.loads(r.stdout) | {"solvetime": 0} == {"assemblies": 1, "solutions": 1, "dotlevel": "7",
                                                         "level": 7, "totalmoves": 7, "solvetime": 0},
              r.stdout)
        r = run("-dp", *env, f)
        check("stacking -p", "level: 7\n" in r.stdout and "  1: S3 from rod 1 to rod 3\n" in r.stdout and
              "  7: S3 from rod 1 to rod 3\n" in r.stdout, r.stdout[-300:])
        r = run("-d", "--solver", "panex", *env, f)
        check("Panex Solver without Panex columns", r.returncode == 2 and "Panex Style Columns" in r.stderr,
              f"{r.returncode} {r.stderr!r}")
        r = run("-d", "-q", "--solver", "Panex Solver", *env, swap)
        check("Panex Solver", r.returncode == 0 and "panex search: 42 moves deep" in r.stdout, r.stdout)
        # --restart throws a saved search away: it goes without --save here
        r = run("-d", "-q", "--restart", *env, swap)
        check("--restart on a stacking puzzle", r.returncode == 0 and "stack search: 42 moves deep" in r.stdout,
              f"{r.returncode} {r.stderr!r}")
        check("no saved search left", not any((Path(tmp) / "panex").glob("*/state.bin")))
        check("stacking -a c", run("-a", "c", f).returncode == 0)
        r = run("-R", f)
        check("stacking -R", r.returncode == 2 and "-R and -C are for brick puzzles" in r.stderr, r.stderr)

        # --save: the GUI's Solve as a session runs it (burrTxt2 had no
        # stacking solver), then -a and -x on the file it wrote
        out = Path(tmp) / "hanoi.xmpuzzle"
        r = run("--save", out, "-d", *env, f)
        check("stacking --save", r.returncode == 0 and "problem 0: 1 assemblies and 1 solutions" in r.stdout,
              f"{r.returncode} {r.stdout!r} {r.stderr!r}")
        r = run("--save", out, "-d", *env, out)
        check("stacking --save again", "is already solved" in r.stdout and "use --restart" in r.stdout, r.stdout)
        r = run("--save", out, "--restart", "-d", *env, out)
        check("stacking --save --restart", "1 assemblies and 1 solutions" in r.stdout, r.stdout)
        r = run("-a", "s1", out)
        check("stacking -a s1", r.returncode == 0 and r.stdout == "problem 0\n001: S1 S2 S3 \n", repr(r.stdout))
        r = run("-x", "-p", out)
        check("stacking -x", r.stdout.startswith("level: 7\n  1: S3 from rod 1 to rod 3\n"), r.stdout[:80])
        r = run("--save", out, *env, f)
        check("stacking --save without -d", r.returncode == 1 and "Could not start Solver" in r.stdout and
              "Turn on Find Solutions" in r.stderr, f"{r.returncode} {r.stdout!r} {r.stderr!r}")


def test_sliding() -> None:
    slider = PUZZLES / "test_sliding_solver.xmpuzzle"
    nested = PUZZLES / "sliding" / "test_sliding_nested.xmpuzzle"
    for solver in (None, "sliding", "Sliding Puzzle Solver"):
        args = [] if solver is None else ["--solver", solver]
        r = run("--json", *args, slider)
        j = json.loads(r.stdout) if r.returncode == 0 else {}
        check(f"sliding --json {solver}", j.get("solutions") == 1 and j.get("dotlevel") == "81",
              f"{r.returncode} {r.stdout!r} {r.stderr!r}")
    for name in ("classic", "fast", "deep", "full"):
        r = run("--solver", name, slider)
        check(f"sliding solver name {name}", r.returncode == 2 and
              f"unknown solver '{name}' for a sliding puzzle" in r.stderr and
              "Sliding Puzzle Solver" in r.stderr, r.stderr)
    r = run("-R", slider)
    check("sliding -R", r.returncode == 2 and "-R and -C are for brick puzzles" in r.stderr, r.stderr)
    # the nested puzzle: no solution without nested slides, said so; 47 moves with
    r = run("-q", "-d", nested)
    check("nested without", r.returncode == 0 and "0 solutions" in r.stdout and
          "every reachable arrangement was searched: no solution (without --nested-slides)" in r.stdout,
          r.stdout)
    r = run("--json", "--nested-slides", nested)
    check("nested with", r.returncode == 0 and json.loads(r.stdout).get("dotlevel") == "47", r.stdout)
    r = run("-q", "-d", "--partial-nested", nested)
    check("partially nested", "1 solutions" in r.stdout, r.stdout)
    # --save solves it as the GUI did; -a and -x read the solved file
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "solved.xmpuzzle"
        r = run("--save", out, "-d", "--restart", slider)
        check("sliding --save", r.returncode == 0 and out.exists() and
              "problem 0: 1 assemblies and 1 solutions" in r.stdout, f"{r.returncode} {r.stdout!r} {r.stderr!r}")
        r = run("-a", "s0", out)
        check("sliding -a s0", r.returncode == 0 and r.stdout.startswith("problem 0\n001: S2 "), r.stdout)
        r = run("-x", out)
        check("sliding -x", r.returncode == 0 and r.stdout == "level: 81\n", r.stdout)


NO_RESULT = """<?xml version="1.0"?>
<puzzle version="2">
  <gridType type="0"/>
  <colors/>
  <shapes>
    <voxel x="2" y="1" z="1" type="0">##</voxel>
  </shapes>
  <problems>
    <problem name="open">
      <shapes>
        <shape id="0" count="1"/>
      </shapes>
      <bitmap/>
    </problem>
  </problems>
  <comment/>
</puzzle>
"""


def test_no_result() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        f = Path(tmp) / "open.xmpuzzle"
        f.write_text(NO_RESULT)
        r = run("-q", f)
        check("no result", r.returncode == 2 and "problem 0 (open) has no result shape" in r.stderr,
              f"{r.returncode} {r.stderr!r}")
        r = run("--save", Path(tmp) / "out.xmpuzzle", f)
        check("no result --save", r.returncode == 1 and "Could not start Solver" in r.stdout and
              "result shape must be defined" in r.stderr, f"{r.returncode} {r.stdout!r} {r.stderr!r}")


def test_solving() -> None:
    r = run("--json", "-t", "1", CUBE)
    check("json", r.returncode == 0, r.stderr)
    j = json.loads(r.stdout)
    check("json values", (j["assemblies"], j["solutions"], j["dotlevel"], j["level"], j["totalmoves"])
          == (96, 1, "4.9.3", 4, 16), r.stdout)
    r = run("-dq", "-t", "1", CUBE)
    check("summary", r.stdout.startswith("96 assemblies and 1 solutions found with 483 iterations in ")
          and r.stdout.endswith(" seconds\n"), r.stdout)
    check("using assembler", r.stderr == "using assembler 1\n", r.stderr)
    check("-n", not run("-dqn", "-t", "1", CUBE).stdout.endswith("\n"))
    full = run("-dsp", "-t", "1", CUBE).stdout
    check("puzzle printed", full.startswith(" The puzzle:\n\nshape 0:\n"), full[:40])
    check("level", "level: 4.9.3\n" in full)
    check("-q turns -p off", "puzzle separates" not in run("-pq", "-d", CUBE).stdout)
    check("-p after -q", "puzzle separates" in run("-q", "-dp", CUBE).stdout)
    original = run("-dp", "-t", "1", "--original-drawings", CUBE).stdout
    check("original drawings differ below the root", original != full and
          original.count("\n") == run("-dp", "-t", "1", CUBE).stdout.count("\n"))
    r = run("-q", "-o", "all", "-d", CUBE)
    check("-o all", r.stdout.startswith("problem: ") and "level: 4.9.3" in r.stdout, r.stdout[:80])


def test_save_ask_and_retake() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "solved.xmpuzzle"
        r = run("--save", out, "--restart", "-d", "-t", "2", CUBE)
        check("save", r.returncode == 0 and r.stdout.startswith(f"outputting into {out}\n") and
              "96 assemblies and 1 solutions" in r.stdout, f"{r.returncode} {r.stdout!r} {r.stderr!r}")
        r = run("--save", out, "-d", out)
        check("save again", r.returncode == 0 and "is already solved: 96 assemblies, 1 solutions" in r.stdout
              and "use --restart" in r.stdout, r.stdout)
        r = run("-a", "s0", out)
        check("-a s0", r.stdout == "problem 0\n001: S2 S3 S3 S4 S5 S6 \n", repr(r.stdout))
        r = run("-a", "s1", out)
        check("-a s1", r.stdout.startswith("problem 0\n001: ") and " !" in r.stdout, r.stdout[:60])
        r = run("-x", out)
        check("-x", r.stdout == "level: 4\n", repr(r.stdout))
        r = run("-xsp", out)
        check("-x -s -p", "puzzle separates" in r.stdout and "level: 4\n" in r.stdout)
        # the progress ticker ends with the solve (it once spun at its stop)
        try:
            r = run("--progress", "-dq", CUBE, timeout=60)
            check("--progress ends", r.returncode == 0 and "96 assemblies" in r.stdout, r.stderr[-200:])
            r = run("--progress", "-q", "--save", out, "--restart", "-d", CUBE, timeout=60)
            check("--progress --save ends", r.returncode == 0, r.stderr[-200:])
        except subprocess.TimeoutExpired:
            check("--progress ends", False, "still running after 60 s")
        r = run("--save", out, "--json", out)
        check("save with --json", r.returncode == 2, str(r.returncode))


def main() -> int:
    global BT
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--bt", type=Path, required=True)
    BT = p.parse_args().bt
    for test in (test_help_and_mistakes, test_stacking, test_sliding, test_no_result, test_solving,
                 test_save_ask_and_retake):
        test()
    print(f"{'all passed' if not failures else f'{len(failures)} failed'}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

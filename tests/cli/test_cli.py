#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The `bt` command line on the fixtures: options, refusals, the modes.

What it prints is compared with the original burrTxt by the harness
(`just oracle-text`, tests/oracle); this checks what holds without the
original repo: burrTxt's option rules and exit codes, the refusal of
sliding and stacking puzzles, and --save followed by -a and -x.

    python3 tests/cli/test_cli.py --bt build/dev/cli/bt
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
    check("help", r.returncode == 0 and r.stdout.startswith("bt [options] file"), r.stdout[:80])
    check("help brick", "brick puzzles:" in run("-h", "brick").stdout)
    check("help sliding", "cannot solve sliding" in run("--help", "slider").stdout)
    r = run()
    check("no file", r.returncode == 1 and "bt [options]" in r.stdout, str(r.returncode))
    for args, text in ((["--json", "-x"], "--json cannot be combined"),
                       (["-oa"], "cannot be clustered"),
                       (["-dz"], "unknown option -z"),
                       (["--bogus"], "unknown option --bogus"),
                       (["--solver", "nope"], "unknown solver 'nope' for a brick puzzle"),
                       (["-o", "5"], "there is no problem 5"),
                       (["--restart"], "go with --save"),
                       (["--nested-slides"], "for sliding and stacking puzzles")):
        r = run(*args, CUBE)
        check(f"refused {args}", r.returncode == 2 and text in r.stderr, f"{r.returncode} {r.stderr!r}")
    r = run("-t", "x", CUBE)
    check("-t not a number", r.returncode == 2 and r.stdout == "-t requires a non-negative number\n",
          repr(r.stdout))
    r = run(PUZZLES / "no-such-file.xmpuzzle")
    check("missing file", r.returncode == 2 and r.stdout.startswith("could not open input file"), r.stdout)


def test_sliding_and_stacking() -> None:
    for name in ("test_stacking_hanoi", "test_sliding_solver"):
        f = PUZZLES / f"{name}.xmpuzzle"
        r = run("-q", f)
        check(f"{name} refused", r.returncode == 1 and
              r.stdout == "The puzzles contains features not yet supported by burrTxt\n" and
              "cannot be solved yet" in r.stderr, f"{r.returncode} {r.stdout!r} {r.stderr!r}")
        r = run("--json", f)
        check(f"{name} refused --json", r.returncode == 1 and r.stdout == "", r.stdout)
        check(f"{name} -a c", run("-a", "c", f).returncode == 0)
        r = run("-R", f)
        check(f"{name} -R", r.returncode == 2 and "-R and -C are for brick puzzles" in r.stderr, r.stderr)


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
    for test in (test_help_and_mistakes, test_sliding_and_stacking, test_no_result, test_solving, test_save_ask_and_retake):
        test()
    print(f"{'all passed' if not failures else f'{len(failures)} failed'}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

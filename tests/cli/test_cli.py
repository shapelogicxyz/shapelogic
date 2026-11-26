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
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
PUZZLES = ROOT / "tests" / "data" / "puzzles"
CUBE = PUZZLES / "CubeInCage.xmpuzzle"  # 96 assemblies, 1 solution (level 4.9.3)

BT: Path = Path()
failures: list[str] = []


# search folders of this run's own, never the user's cache: the suites run
# side by side, and two solves of one problem cannot work in the same
# folders (decision 0035)
FOLDERS = tempfile.TemporaryDirectory(prefix="sl_cli_")
ENV = {**os.environ, "SHAPELOGIC_SLIDE_DIR": str(Path(FOLDERS.name) / "sliding"),
       "SHAPELOGIC_PANEX_DIR": str(Path(FOLDERS.name) / "panex")}


def run(*args: str | Path, timeout: float = 300) -> subprocess.CompletedProcess:
    return subprocess.run([str(BT), *map(str, args)], capture_output=True, text=True, timeout=timeout,
                          check=False, env=ENV)


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
                       # given, even as the default, it goes with --save
                       (["--limit", "10"], "go with --save"),
                       (["--limit", "3"], "go with --save"),
                       (["--nested-slides"], "--nested-slides is for sliding puzzles; this is a brick"),
                       (["--partial-nested"], "--partial-nested is for sliding puzzles"),
                       (["--high-memory"], "--high-memory is for sliding and stacking puzzles"),
                       (["--optimized-paths"], "--optimized-paths is for sliding puzzles"),
                       (["--slide-grid", "3"], "--slide-grid is for sliding puzzles"),
                       (["--slide-grid", "1"], "--slide-grid requires a number from 2 to 1024"),
                       # a disk budget is a number of GB from 0 to 1000000
                       (["--panex-disk", "-1"], "--panex-disk requires a number of GB from 0 to 1000000"),
                       (["--panex-disk", "inf"], "--panex-disk requires a number of GB"),
                       (["--panex-disk", "nan"], "--panex-disk requires a number of GB"),
                       (["--panex-disk", "2e7"], "--panex-disk requires a number of GB")):
        r = run(*args, CUBE)
        check(f"refused {args}", r.returncode == 2 and text in r.stderr, f"{r.returncode} {r.stderr!r}")
    # burrTxt ignored the Panex options on other puzzles
    r = run("-q", "--panex-dir", "x", "--panex-disk", "1", CUBE)
    check("--panex-dir on a brick puzzle", r.returncode == 0, f"{r.returncode} {r.stderr!r}")
    for name in ("SHAPELOGIC_SLIDE_DISK_GB", "SHAPELOGIC_PANEX_DISK_GB"):
        r = subprocess.run([str(BT), "-q", str(CUBE)], capture_output=True, text=True, timeout=300,
                           check=False, env={**ENV, name: "-1"})
        check(f"{name}=-1 refused", r.returncode == 2 and f'{name} is "-1"' in r.stderr,
              f"{r.returncode} {r.stderr!r}")
    r = run("-t", "x", CUBE)
    check("-t not a number", r.returncode == 2 and r.stdout == "-t requires a non-negative number\n",
          repr(r.stdout))
    r = run(PUZZLES / "no-such-file.xmpuzzle")
    check("missing file", r.returncode == 2 and r.stdout.startswith("could not open input file"), r.stdout)
    r = run(PUZZLES)
    check("a folder", r.returncode == 2 and r.stdout.startswith("could not open input file"), r.stdout)


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
    # optimized paths: a 4x5 tray fits no grid, so every stop is searched;
    # a grid asked for is used, and its finding no path is said to prove nothing
    r = run("-d", "--optimized-paths", slider)
    check("optimized paths, no grid", r.returncode == 0 and
          "optimized paths: the board fits no grid, so every stop is searched" in r.stdout and
          "level: 81" in r.stdout, r.stdout[-300:])
    r = run("-d", "--slide-grid", "5", slider)
    check("a grid of 5", r.returncode == 0 and
          "optimized paths: the pieces stop only on a 5-cell grid" in r.stdout and
          "no solution with optimized paths (a 5-cell grid); a full search may find one" in r.stdout,
          r.stdout[-300:])
    # the stops of a grid cell told apart, every position numbered in the
    # keys, and the search from the start alone, benchmark switches: the same
    # answer (but for the count of arrangements and the time)
    answers = [[line for line in run("-d", "--nested-slides", "--slide-grid", "2", *extra,
                                     nested).stdout.splitlines() if "iterations" not in line]
               for extra in ([], ["--slide-stops-apart"], ["--slide-plain-keys"],
                             ["--slide-one-end"])]
    check("stops apart", len(answers[0]) > 3 and answers[0] == answers[1], str(answers[1][-3:]))
    check("plain keys", answers[0] == answers[2], str(answers[2][-3:]))
    check("one end", answers[0] == answers[3], str(answers[3][-3:]))
    # the copies of a piece kept as one number in the search's keys, and the
    # keys wider than 128 bits, benchmark switches: the same answer
    for extra, key in ((["--slide-packing", "always"], " of 64 bits, the copies of a piece as one number"),
                       (["--slide-packing", "never", "--slide-key-bits", "192"], " of 192 bits\n"),
                       (["--slide-key-bits", "512"], " of 512 bits, the copies of a piece as one number"),
                       ([], " of 64 bits\n")):
        r = run("-d", "--slide-trace", *extra, slider)
        check("slider " + " ".join(extra), r.returncode == 0 and "level: 81" in r.stdout and
              r.stderr.count("key: ") == 1 and key in r.stderr and "start level 1: " in r.stderr,
              r.stdout[-300:] + r.stderr[:200])
    # the search's log lines come with the trace
    r = run("-d", "--slide-trace", slider)
    check("slider trace has the log", r.returncode == 0 and "INFO: The slide search runs in " in r.stderr,
          r.stderr[:300])
    # the compression of the level files, a benchmark switch: the same answer
    for level in ("0", "19"):
        r = run("-d", "--slide-compress", level, slider)
        check("slider --slide-compress " + level, r.returncode == 0 and "level: 81" in r.stdout,
              r.stdout[-300:] + r.stderr[:200])
    r = run("-d", "--slide-compress", "20", slider)
    check("a compression of 20 is refused", r.returncode != 0 and "requires a number from 0 to 19" in r.stderr,
          r.stderr)
    r = run("-d", "--slide-key-bits", "100", slider)
    check("a key of 100 bits is refused", r.returncode != 0 and "requires 64, 128, 192, 256 or 512" in r.stderr,
          r.stderr)
    r = run("-d", "--slide-packing", "some", slider)
    check("a packing unknown is refused", r.returncode != 0 and "requires auto, always or never" in r.stderr,
          r.stderr)
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


def test_an_empty_folder_variable_is_unset() -> None:
    # SHAPELOGIC_SLIDE_DIR set but empty counts as unset, as in the engine
    # (bt::defaultSearchFolder): the search's folder is the cache folder's,
    # here under a home of the test's own (cleanup item 11.6)
    with tempfile.TemporaryDirectory() as tmp:
        env = {**ENV, "SHAPELOGIC_SLIDE_DIR": "", "HOME": tmp, "XDG_CACHE_HOME": str(Path(tmp) / "xdg"),
               "LOCALAPPDATA": str(Path(tmp) / "local")}
        r = subprocess.run([str(BT), "-q", "-d", PUZZLES / "test_sliding_solver.xmpuzzle"],
                           capture_output=True, text=True, timeout=300, check=False, env=env)
        made = [p for p in Path(tmp).rglob("sliding") if p.is_dir()]
        check("empty SHAPELOGIC_SLIDE_DIR", r.returncode == 0 and len(made) == 1,
              f"{r.returncode} {made} {r.stderr!r}")


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


# A sliding puzzle the app refuses to solve: piece S1 (one cell) has starts
# on two cells. --save refuses it with the app's reason (cleanup item
# 11.5: Document::solveLaunch's checks, app::solveRefusal).
STAMP_OVERFLOW = """<?xml version="1.0"?>
<puzzle version="2" slideMarks="problem">
  <gridType type="5"/>
  <colors/>
  <shapes>
    <voxel x="2" y="2" z="1" name="sg:1:" type="0">####</voxel>
    <voxel x="1" y="1" z="1" type="0">#</voxel>
  </shapes>
  <problems>
    <problem name="two starts">
      <shapes>
        <shape id="1" count="1"/>
      </shapes>
      <result id="0"/>
      <bitmap/>
      <slideMarks start="2,2,0,0"/>
    </problem>
  </problems>
  <comment/>
</puzzle>
"""


def test_save_refuses_as_the_app() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        f = Path(tmp) / "overflow.xmpuzzle"
        f.write_text(STAMP_OVERFLOW)
        r = run("--save", Path(tmp) / "out.xmpuzzle", "--restart", f)
        check("stamp overflow --save", r.returncode == 1 and "Could not start Solver" in r.stdout and
              "S1 has 2 starts" in r.stderr and "solutions found" not in r.stdout, f"{r.returncode} {r.stdout!r} {r.stderr!r}")


# A damaged or hostile file is refused with a line that says why and the
# exit code of a file that cannot be read (cleanup items 16.1, 16.2 and
# 16.5). Before, a gzip file cut short was read as far as it went, numbers
# past an int were undefined, and an exception that left the reader ended
# the program with an abort.
def test_damaged_files() -> None:
    import gzip
    import re
    solved = gzip.decompress((PUZZLES / "PelikanBurr.xmpuzzle").read_bytes()).decode()
    huge = "99999999999999999999"
    cases = {
        "cut short": ((PUZZLES / "PelikanBurr.xmpuzzle").read_bytes()[:-9], "the file is cut short"),
        "not gzip after its first bytes": (b"\x1f\x8b" + b"junk" * 10, "broken gzip data"),
        "a placement past an int": (re.sub(r"<assembly>-?\d+", "<assembly>" + huge, solved, count=1),
                                    "assembly placement out of range"),
        "a state's offset past an int": (re.sub(r"<dx>-?\d+", "<dx>" + huge, solved, count=1),
                                         "number out of range in disassembly"),
        "a shape of a gigabyte": (re.sub(r'<voxel x="\d+" y="\d+" z="\d+"',
                                         '<voxel x="1024" y="1024" z="1024"', solved, count=1),
                                  "not enough voxels"),
        "an element of 5000 attributes": ('<puzzle version="2" ' +
                                          " ".join(f'a{i}=""' for i in range(5000)) + "/>",
                                          "too many attributes"),
    }
    with tempfile.TemporaryDirectory() as tmp:
        for name, (content, why) in cases.items():
            f = Path(tmp) / "damaged.xmpuzzle"
            f.write_bytes(content if isinstance(content, bytes) else content.encode())
            for args in (["-q"], ["-d", "-q"], ["--save", str(Path(tmp) / "out.xmpuzzle")]):
                r = run(*args, f)
                check(f"damaged: {name} {args[0]}",
                      r.returncode == 2 and "could not read" in r.stderr and why in r.stderr and
                      not (Path(tmp) / "out.xmpuzzle").exists(),
                      f"{r.returncode} {r.stderr!r}")


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


def test_new_grids() -> None:
    """The hexagonal and cairo fixtures (the original has no such grids): solved
    again with take-apart, every problem finds what the file holds."""
    import gzip
    import re
    files = sorted((PUZZLES / "new-grids").glob("*.xmpuzzle"))
    check("new grids", len(files) == 2, str(files))
    for f in files:
        xml = gzip.decompress(f.read_bytes()).decode()
        saved = re.findall(r'<problem name="[^"]*" state="2" assemblies="(\d+)" solutions="(\d+)"', xml)
        check(f"{f.name} solved", len(saved) == 2, str(saved))
        for i, (assemblies, solutions) in enumerate(saved):
            r = run("--json", "-o", str(i), f)
            j = json.loads(r.stdout) if r.returncode == 0 else {}
            check(f"{f.name} problem {i}", (j.get("assemblies"), j.get("solutions")) ==
                  (int(assemblies), int(solutions)) and j.get("level", 0) >= 1, r.stdout + r.stderr)


def test_tilted() -> None:
    """The Tilted Square fixtures (decision 0045): Brick files with a tray.
    Each problem, solved as its stored setting says (the pieces turned over
    or one face up), finds the assemblies the file holds; --flip goes over
    the stored setting; --save solves as the GUI does."""
    import gzip
    import re
    files = sorted((PUZZLES / "tilted").glob("*.xmpuzzle"))
    check("tilted", len(files) == 3, str(files))

    def problems(xml: str) -> list[tuple[str, str]]:
        return re.findall(r'<problem name="[^"]*"[^>]* state="2" assemblies="(\d+)"(.*?)</problem>', xml, re.S)

    def assemblies(*args: str | Path) -> int | None:
        r = run("--json", *args)
        return json.loads(r.stdout).get("assemblies") if r.returncode == 0 else None

    for f in files:
        saved = problems(gzip.decompress(f.read_bytes()).decode())
        check(f"{f.name} solved", len(saved) >= 1, str(len(saved)))
        for i, (count, _) in enumerate(saved):
            check(f"{f.name} problem {i}", assemblies("-o", str(i), f) == int(count))

    # Triple Play: problem 1 keeps its pieces one face up (2 assemblies, no
    # symmetry breaker); turned over they go in one way. Problem 0's pieces
    # as drawn do not go one face up.
    tp = PUZZLES / "tilted" / "TriplePlay.xmpuzzle"
    check("--flip on over the stored off", assemblies("-o", "1", "--flip", "on", tp) == 1)
    check("--flip off over the stored on", assemblies("-o", "0", "--flip", "off", tp) == 0)
    r = run("--flip", "on", CUBE)
    check("--flip on a puzzle without a tray", r.returncode == 2 and "--flip is for Tilted Square" in r.stderr,
          r.stderr)
    r = run("--flip", "maybe", tp)
    check("--flip needs on or off", r.returncode == 2 and "requires on or off" in r.stderr, r.stderr)
    # --save: the GUI's solve, each problem as its setting says
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "tp.xmpuzzle"
        r = run("--save", out, "--restart", "-q", "-o", "all", tp)
        check("--save of Triple Play", r.returncode == 0, r.stdout + r.stderr)
        data = out.read_bytes()
        xml = gzip.decompress(data).decode() if data[:2] == b"\x1f\x8b" else data.decode()
        check("--save keeps each problem's setting", [c for c, _ in problems(xml)] == ["1", "2"],
              str(problems(xml)))
        # a tilted tray has no take-apart (the pieces lie flat in it; the
        # app's Solver stays on Assemble Only): -d finds the assemblies and
        # takes none apart, as on a grid without a disassembler, and --save
        # refuses -d with the reason
        r = run("--json", "-o", "1", tp)
        found = json.loads(r.stdout) if r.returncode == 0 else {}
        check("-d takes nothing apart on a tilted tray",
              found.get("assemblies") == 2 and found.get("solutions") == 0 and found.get("level") is None,
              r.stdout + r.stderr)
        r = run("--save", out, "--restart", "-d", "-q", tp)
        check("--save -d of a tilted puzzle is refused",
              r.returncode == 2 and "nothing to take apart" in r.stdout, r.stdout + r.stderr)


def main() -> int:
    global BT
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--bt", type=Path, required=True)
    BT = p.parse_args().bt
    # every test_* function of this file, in the order they are written: a
    # new one runs without being added to a list (cleanup item 8.6)
    tests = [f for name, f in globals().items() if name.startswith("test_") and callable(f)]
    if not tests:
        print("no test_* functions found")
        return 1
    for test in tests:
        test()
    print(f"{'all passed' if not failures else f'{len(failures)} failed'}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

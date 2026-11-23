#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Makes this folder's fixtures: Tilted Square puzzles (decision 0045,
docs/TILTED-SQUARE-PLAN.md stages F and G), made through the engine as the
front end makes them.

- `TriplePlay.xmpuzzle`: "Triple Play (IPP34)" by Goh Pit Khiam and Jerry
  Loo. A 6 by 6 tray with the corner squares (5, 0) and (0, 5) walled, at 1
  down 3 over with an allowance of 0.05: 24 whole cells for three pieces of
  seven, so three stay empty. Problem 0 lets the pieces be turned over (one
  assembly: the two ways are one another turned half round, as the board
  is). Problem 1, "One face up", does not: S4 to S6 are the pieces'
  mirror images, and of the eight mixes of drawn and mirrored pieces the
  one that goes is its pieces (two assemblies: with the turns limited the
  search has no symmetry breaker).
- `EightByEight.xmpuzzle`: an open 8 by 8 tray at 1 down 2 over, strict:
  45 whole cells, filled by five pieces of nine (two assemblies).
- `OtherDiagonal.xmpuzzle` (stage G): Triple Play's pieces in its tray
  with the walls on the other diagonal, the corner squares (0, 0) and
  (5, 5). The best placement has 24 whole cells and takes no pieces (no
  assembly); of the six boards the grid can give (tray.placements) others
  do, which the script checks with tray.fitPlacements.

It also writes the site demo's Tilted Square example,
`site/public/examples/demo/tilted.xmpuzzle`: Triple Play's first problem and
its shapes alone, as the demo's other examples are (web/src/demo/main.ts),
and the copy of `TriplePlay.xmpuzzle` the user guide offers to download,
`site/public/examples/TriplePlay.xmpuzzle`.

    just build release
    python3 tests/data/puzzles/tilted/make_fixtures.py build/release/engine/shapelogic_engine

Names after the engine make those fixtures alone (`triple_play`,
`eight_by_eight`, `other_diagonal`), the others staying as they are.

Made again, only the problems' `timeMs`, the uids and the order of
the solutions found side by side differ. No test runs
this; the tests read what it made (test_puzzle_file.cpp, test_cli.py, the
web engine and e2e tests).
"""

import base64
import gzip
import pathlib
import shutil
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(ROOT / "tests" / "contract"))

from engine_client import Engine  # noqa: E402
from test_engine import wait_state  # noqa: E402

# Triple Play's pieces as 4 by 4 pictures, rows from y = 0, and their
# mirror images (each row read backwards)
TRIPLE_PLAY = ("_#___###_#__##__", "_#___#___#####__", "_#___#####___#__")
MIRRORED = ("__#_###___#___##", "__#___#_###___##", "__#_###___##__#_")

# the 8 by 8 tray's board at 1 down 2 over (its 11 by 11 box, rows from
# y = 0 at the bottom of the picture, as the board's views draw it): each
# digit a piece's cell, '.' a cell the tray cuts
EIGHT = """\
...........
....4......
...4444....
...444522..
..34555222.
..3355522..
.33335522..
..331111...
....1111...
......1....
..........."""


def solve(e, problem, flip):
    """Solves `problem` as the Solver does for a tilted puzzle: assemble
    only, the pieces turned over or not, the settings stored with it."""
    o = {"disassemble": False, "flipPieces": flip}
    e.call("problem.setSolverSettings", {"problem": problem, "settings": o})
    start = len(e.events)
    e.call("solve.start", {"problem": problem, "options": o})
    state = wait_state(e, problem, ("finished", "error"), start)
    e.drain()
    if state != "finished":
        sys.exit(f"the solve ended {state}")
    return e.call("problem.get", {"problem": problem})["assemblies"]


def draw(e, tray, art):
    """A new piece with a 4 by 4 picture's cells, at the first place where
    they are all whole cells of the board."""
    x, y = tray["box"]
    cells = [(k % 4, k // 4) for k in range(16) if art[k] == "#"]
    shape = e.call("shape.create")["shape"]
    for oy in range(-3, y):
        for ox in range(-3, x):
            at = [(ox + cx, oy + cy) for cx, cy in cells]
            if all(0 <= ax < x and 0 <= ay < y and tray["cells"][ay * x + ax] == "w" for ax, ay in at):
                e.call("shape.setCells", {"shape": shape, "tool": "fixed",
                                          "cells": [[ax, ay, 0] for ax, ay in at]})
                return shape
    sys.exit("a piece fits nowhere")


def save(e, path):
    """The document, gzip as the app saves it (no time stamp in the gzip)."""
    data = base64.b64decode(e.call("document.saveBytes")["data"])
    path.write_bytes(gzip.compress(data, mtime=0))
    print(f"wrote {path.relative_to(ROOT)}")


def triple_play(e):
    e.call("document.new", {"grid": "bricks", "tray": {"size": 6, "over": 3, "down": 1,
                                                       "allowance": 0.05}})
    e.call("tray.setWalls", {"squares": [[5, 0], [0, 5]], "wall": True})
    e.call("document.setNotes", {"text": "Triple Play (IPP34) by Goh Pit Khiam and Jerry Loo: "
                                         "three pieces in a tray, the grid turned 1 down 3 over."})
    snap = e.call("document.get")
    tray = snap["tray"]
    first = snap["problems"][0]["id"]
    e.call("problem.rename", {"problem": first, "name": "Triple Play"})
    pieces = [draw(e, tray, art) for art in TRIPLE_PLAY]
    mirrored = [draw(e, tray, art) for art in MIRRORED]
    for s in pieces:
        e.call("problem.setPieceCount", {"problem": first, "shape": s, "min": 1, "max": 1})
    second = e.call("problem.create")["problem"]
    e.call("problem.rename", {"problem": second, "name": "One face up"})

    def take(chosen):
        """Problem 1 with the chosen pieces, in the shapes' order."""
        e.call("problem.removeAllPieces", {"problem": second})
        for s in sorted(chosen, key=(pieces + mirrored).index):
            e.call("problem.setPieceCount", {"problem": second, "shape": s, "min": 1, "max": 1})

    mixes = [[mirrored[k] if (mix >> k) & 1 else pieces[k] for k in range(3)] for mix in range(8)]
    going = []
    for chosen in mixes:
        take(chosen)
        if solve(e, second, False):
            going.append(chosen)
    if len(going) != 1:
        sys.exit(f"{len(going)} mixes go one face up, not one")
    take(going[0])
    counts = (solve(e, first, True), solve(e, second, False))
    print("Triple Play:", counts)
    if counts != (1, 2):
        sys.exit("Triple Play has one assembly, and one face up two")
    save(e, HERE / "TriplePlay.xmpuzzle")
    shutil.copyfile(HERE / "TriplePlay.xmpuzzle", ROOT / "site" / "public" / "examples" / "TriplePlay.xmpuzzle")

    # the demo's example: the first problem and its pieces
    e.call("problem.delete", {"problem": second})
    for s in reversed(mirrored):
        e.call("shape.delete", {"shape": s})
    save(e, ROOT / "site" / "public" / "examples" / "demo" / "tilted.xmpuzzle")


def eight_by_eight(e):
    snap = e.call("document.new", {"grid": "bricks", "tray": {"size": 8, "over": 2, "down": 1}})
    tray = snap["tray"]
    x, y = tray["box"]
    rows = list(reversed(EIGHT.splitlines()))
    if len(rows) != y or any(len(r) != x for r in rows):
        sys.exit("the picture is not the board's box")
    problem = snap["problems"][0]["id"]
    e.call("problem.rename", {"problem": problem, "name": "Fill the tray"})
    for digit in "12345":
        cells = [[cx, cy, 0] for cy in range(y) for cx in range(x) if rows[cy][cx] == digit]
        if any(tray["cells"][cy * x + cx] != "w" for cx, cy, _ in cells):
            sys.exit(f"piece {digit} is off the whole cells")
        shape = e.call("shape.create")["shape"]
        e.call("shape.setCells", {"shape": shape, "tool": "fixed", "cells": cells})
        e.call("problem.setPieceCount", {"problem": problem, "shape": shape, "min": 1, "max": 1})
    n = solve(e, problem, True)
    print("Eight by eight:", n)
    if n != 2:
        sys.exit("the eight by eight tray has two assemblies")
    save(e, HERE / "EightByEight.xmpuzzle")


def other_diagonal(e):
    e.call("document.new", {"grid": "bricks", "tray": {"size": 6, "over": 3, "down": 1,
                                                       "allowance": 0.05}})
    e.call("tray.setWalls", {"squares": [[0, 0], [5, 5]], "wall": True})
    e.call("document.setNotes", {"text": "Triple Play's pieces in its tray with the walls on the other "
                                         "diagonal: they do not fit where the most cells are whole. "
                                         "Tilt & tolerance, Placements… lists the placements that take them."})
    snap = e.call("document.get")
    problem = snap["problems"][0]["id"]
    e.call("problem.rename", {"problem": problem, "name": "Other diagonal"})
    for art in TRIPLE_PLAY:
        e.call("problem.setPieceCount", {"problem": problem, "shape": draw(e, snap["tray"], art),
                                         "min": 1, "max": 1})
    n = solve(e, problem, True)
    boards = e.call("tray.placements")
    start = len(e.events)
    e.call("tray.fitPlacements", {"problem": problem, "options": {"flipPieces": True}})
    end = e.wait_event("tray.fitEnded", lambda q: q["problem"] == problem, start=start)
    e.drain()
    counts = [b["assemblies"] for b in end["boards"]]
    print("Other diagonal:", n, "at the best placement; over the placements", counts)
    if n != 0 or boards["inUse"] != 0 or len(counts) != 6 or counts[0] != 0 or not any(counts):
        sys.exit("the other diagonal's best placement takes no pieces, and others do")
    save(e, HERE / "OtherDiagonal.xmpuzzle")


def main():
    made = {"triple_play": triple_play, "eight_by_eight": eight_by_eight,
            "other_diagonal": other_diagonal}
    names = sys.argv[2:] or list(made)
    if any(n not in made for n in names):
        sys.exit(f"the fixtures are {', '.join(made)}")
    e = Engine(pathlib.Path(sys.argv[1]).resolve())
    try:
        for n in names:
            made[n](e)
    finally:
        e.close()


if __name__ == "__main__":
    main()

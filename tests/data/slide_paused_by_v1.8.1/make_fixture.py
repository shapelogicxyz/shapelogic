#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Makes this folder's fixture: a sliding solve with nested slides and
optimized paths, paused by release v1.8.1's engine (decision 0033).

It builds the cup-and-blocks board of tests/unit/solve/test_sliding_session.cpp
in the engine it is given, solves it under budgets of a byte, so that the
search runs out of disk at its first level, is saved and the solve pauses,
and saves the document. The folder then holds `cup_and_blocks.xmpuzzle` and
the search folder (`slide-<fingerprint>/`) as that engine left them.

The engine must be v1.8.1's (a later one saves another search):

    git archive v1.8.1 | tar -x -C <tree>
    (cd <tree> && just build release)
    python3 make_fixture.py <tree> <tree>/build/release/engine/shapelogic_engine <out>

Made again, only the problem's `timeMs` differs.

No test runs this; the tests read what it made.
"""

import os
import shutil
import sys


def main():
    tree, engine, out = sys.argv[1:4]
    sys.path.insert(0, os.path.join(tree, "tests", "contract"))
    from engine_client import Engine

    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    e = Engine(engine, cwd=tree)
    e.call("settings.set", {"slideFolder": out, "slideDiskGB": 1e-9, "searchMemoryGB": 1e-9})
    e.call("document.new", {"grid": "sliding"})

    def block(w, h, skip=()):
        s = e.call("shape.create", {"size": [w, h, 1]})["shape"]
        cells = [[x, y, 0] for y in range(h) for x in range(w) if (x, y) not in skip]
        e.call("shape.setCells", {"shape": s, "tool": "fixed", "cells": cells})
        return s

    cup = block(3, 3, skip={(1, 0), (1, 1)})
    first = block(3, 3)
    second = block(3, 3)
    unit = block(1, 1)
    tray = e.call("sliding.addStartGoal", {"size": [6, 6]})["shape"]
    problem = e.call("problem.create")["problem"]
    e.call("problem.setResult", {"problem": problem, "shape": tray})
    # a piece's place is its top left cell, y rising upwards
    stamps = [(cup, [0, 2], [3, 2]), (first, [3, 2], [3, 5]), (second, [0, 5], [0, 2]), (unit, [1, 1], [4, 1])]
    for piece, start, goal in stamps:
        for at, isGoal in ((start, False), (goal, True)):
            e.call("sliding.place", {"tray": tray, "problem": problem, "piece": piece, "at": at, "goal": isGoal})
    # the boxes the Solver stores with the problem: Find Solutions and Allow
    # Nested Slides (the grid was this solve's answer to "Use Optimized
    # Paths?", so that box stays off)
    e.call("problem.setSolverSettings", {"problem": problem, "settings": {"disassemble": True, "nestedSlides": True}})
    options = {"disassemble": True, "sliding": {"nestedSlides": True, "optimizedPaths": True}}
    seen = len(e.events)
    e.call("solve.start", {"problem": problem, "options": options})
    state = e.wait_event(
        "solve.state",
        lambda x: x["problem"] == problem and x["state"] in ("paused", "finished", "error"),
        start=seen,
        timeout=60,
    )["state"]
    if state != "paused":
        sys.exit(f"the solve is {state}, not paused")
    e.call("document.save", {"path": os.path.join(out, "cup_and_blocks.xmpuzzle")})
    e.close()


if __name__ == "__main__":
    main()

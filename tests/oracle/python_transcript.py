#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A transcript of what the `burrtools` Python module says, for
`oracle.py python` (deferred part 7): run once with the original's module
and once with the new one on PYTHONPATH, the two transcripts must be the
same, line for line.

It reads each puzzle given (shapes, cells, problems, piece counts, the
reprs), solves each problem for its assemblies and for its solutions, builds
puzzles through the API from a seeded generator (saving them, solving
them), and calls the API with wrong arguments (the exception's type).

With --one-thread the solves run on one thread (the original's module from
BURRTOOLS_THREADS=1, which the harness sets, the new one from threads=1):
the assemblies and solutions are then compared in the order they come, with
their numbers and the search's iterations. Otherwise they are compared as
sorted lists without their numbers (the order depends on the threads).
"""

import argparse
import gzip
import json
import os
import random
import sys
import tempfile

import burrtools

ONE_THREAD = False


def emit(*parts):
    print(json.dumps(parts, separators=(",", ":")), flush=True)


def solve(problem, **kw):
    if ONE_THREAD:
        try:
            return problem.solve(threads=1, **kw)
        except TypeError:
            # the original's module: BURRTOOLS_THREADS=1 holds it to one
            pass
    return problem.solve(**kw)


def solution_row(s):
    row = {
        "placements": [[p.piece_id, p.x, p.y, p.z, p.transformation, p.is_placed]
                       for p in s.placements],
        "has_disassembly": s.has_disassembly,
        "moves_text": s.moves_text,
        "total_moves": s.total_moves,
        "level": s.level,
    }
    if ONE_THREAD:
        row["assembly_number"] = s.assembly_number
        row["solution_number"] = s.solution_number
        row["repr"] = repr(s)
        row["placement_reprs"] = [repr(p) for p in s.placements]
    return row


def solve_rows(label, problem, **kw):
    it = solve(problem, **kw)
    try:
        rows = [solution_row(s) for s in it]
    except RuntimeError as e:
        emit(label, "error", type(e).__name__)
        return
    if not ONE_THREAD:
        rows.sort(key=lambda r: json.dumps(r, sort_keys=True))
    emit(label, "count", len(rows), it.is_finished)
    if ONE_THREAD:
        emit(label, "iterations", it.iterations)
    for r in rows:
        emit(label, r)


def text(f):
    """A text the module gives, or the exception it raises: a file's texts
    are bytes (Prisgon's comment is Latin-1), which both modules hand to
    Python as UTF-8."""
    try:
        return f()
    except UnicodeDecodeError as e:
        return f"{type(e).__name__} at {e.start}"


def describe(tag, puzzle, solve_it=True):
    emit(tag, "puzzle", repr(puzzle), puzzle.num_shapes, puzzle.num_problems, text(lambda: puzzle.comment))
    for s in puzzle.shapes:
        cells = [s.get(x, y, z) for z in range(s.size_z) for y in range(s.size_y) for x in range(s.size_x)]
        emit(tag, "shape", s.index, text(lambda: s.name), list(s.dimensions), text(lambda: repr(s)),
             s.count_filled(), s.count_empty(), cells)
    for p in puzzle.problems:
        counts = [[p.get_piece_min(i), p.get_piece_max(i)] for i in range(puzzle.num_shapes)]
        emit(tag, "problem", p.index, text(lambda: p.name), text(lambda: repr(p)), p.num_pieces,
             p.result_shape_index, counts)
    if not solve_it:
        return
    for p in puzzle.problems:
        if p.result_shape_index < 0 or p.num_pieces == 0:
            continue
        solve_rows(f"{tag} p{p.index} assemblies", p, disassemble=False)
        solve_rows(f"{tag} p{p.index} solutions", p, disassemble=True)
        solve_rows(f"{tag} p{p.index} reduced, mirrors and rotations kept", p,
                   disassemble=False, reduce=True, keep_rotations=True, keep_mirror=True)


def generated(n, seed, tmp):
    """Small puzzles built through the API: a box cut into pieces at random
    (so most have assemblies), some of its cells made variable, and a spare
    cube piece with a range of counts."""
    rnd = random.Random(seed)
    for k in range(n):
        tag = f"generated {seed}/{k}"
        puzzle = burrtools.Puzzle()
        puzzle.comment = f"generated {k}"
        sx, sy, sz = rnd.randint(2, 3), rnd.randint(2, 3), rnd.randint(1, 3)
        cells = [(x, y, z) for x in range(sx) for y in range(sy) for z in range(sz)]
        box = puzzle.add_shape(sx, sy, sz, name="box")
        box.fill(cells)
        # grow the pieces from random seeds, a neighbour cell at a time
        parts = min(rnd.randint(2, 4), len(cells))
        owner = {c: i for i, c in enumerate(rnd.sample(cells, parts))}
        while len(owner) < len(cells):
            x, y, z = rnd.choice(list(owner))
            nb = (x + rnd.choice((-1, 0, 1)), y + rnd.choice((-1, 0, 1)), z + rnd.choice((-1, 0, 1)))
            if nb in cells and nb not in owner and sum(a != b for a, b in zip(nb, (x, y, z))) == 1:
                owner[nb] = owner[(x, y, z)]
        problem = puzzle.add_problem(name=f"problem {k}")
        problem.set_result(box)
        for i in range(parts):
            mine = [c for c, o in owner.items() if o == i]
            lo = [min(c[d] for c in mine) for d in range(3)]
            hi = [max(c[d] for c in mine) for d in range(3)]
            piece = puzzle.add_shape(*(h - l + 1 for h, l in zip(hi, lo)), name=f"piece {i}")
            piece.fill([tuple(c[d] - lo[d] for d in range(3)) for c in mine])
            problem.set_piece_count(piece, 1)
        for _ in range(rnd.randint(0, 2)):
            box.set_state(*rnd.choice(cells), burrtools.Voxel.VARIABLE)
        if rnd.random() < 0.5:
            spare = puzzle.add_shape(1, 1, 1, name="spare")
            spare.fill([(0, 0, 0)])
            problem.set_piece_range(spare.index, 0, rnd.randint(1, 2))
        path = os.path.join(tmp, f"g{k}.xmpuzzle")
        puzzle.save(path)
        with gzip.open(path, "rb") as f:
            emit(tag, "saved", f.read().decode("latin-1"))
        describe(tag, burrtools.load(path))


def errors():
    """The exception each wrong call raises (its type; the texts differ)."""
    def call(label, f):
        try:
            r = f()
            emit("errors", label, "ok", repr(r))
        except BaseException as e:  # noqa: BLE001
            emit("errors", label, type(e).__name__)

    p = burrtools.Puzzle()
    s = p.add_shape(2, 2, 2, "a")
    call("get outside", lambda: s.get(5, 0, 0))
    call("set outside", lambda: s.set(0, 5, 0, 1))
    call("get_state outside", lambda: s.get_state(0, 0, 2))
    # the cells before one outside are filled; the original then leaves
    # this shape's bounding box unrecalculated (its empty cells keep their
    # colours), which the new module does not: no other test on it
    f = p.add_shape(2, 1, 1, "fill")
    call("fill outside", lambda: f.fill([(0, 0, 0), (2, 0, 0), (1, 0, 0)]))
    call("after fill outside", lambda: (f.get(0, 0, 0), f.get(1, 0, 0)))
    call("shape outside", lambda: p.shape(5))
    call("problem outside", lambda: p.problem(0))
    pr = p.add_problem()
    call("result unset", lambda: pr.result_shape_index)
    call("set_result outside", lambda: pr.set_result(4))
    call("get_piece_min outside", lambda: pr.get_piece_min(4))
    call("get_piece_max none", lambda: pr.get_piece_max(0))
    call("solve without result", lambda: list(pr.solve()))
    pr.set_result(s)
    call("solve without pieces", lambda: list(pr.solve()))
    call("set_color 64", lambda: s.set_color(0, 0, 0, 64))
    call("set_color 63", lambda: (s.set_color(0, 0, 0, 63), s.get(0, 0, 0), s.get_color(0, 0, 0)))
    call("set_state", lambda: (s.set_state(1, 1, 1, burrtools.Voxel.VARIABLE), s.get(1, 1, 1), s.is_variable(1, 1, 1)))
    call("set 300", lambda: (s.set(1, 0, 0, 300), s.get(1, 0, 0)))
    call("is_*", lambda: (s.is_filled(0, 0, 0), s.is_empty(0, 1, 0), s.is_variable(0, 0, 0)))
    call("counts", lambda: (s.count_filled(), s.count_empty()))
    call("empty shape", lambda: p.add_shape(0, 0, 0))
    call("load missing", lambda: burrtools.load("/nonexistent/puzzle.xmpuzzle"))
    call("Voxel", lambda: (int(burrtools.Voxel.EMPTY), int(burrtools.FILLED), int(burrtools.VARIABLE)))
    call("names", lambda: sorted(n for n in dir(burrtools) if not n.startswith("_")
                                 and n not in ("import_puzzlesolver3d", "import_puzzlecad")))
    it = pr.solve(disassemble=False)
    it.stop()
    call("next after stop", lambda: next(it))


def main():
    global ONE_THREAD
    ap = argparse.ArgumentParser()
    ap.add_argument("puzzles", nargs="*")
    ap.add_argument("--one-thread", action="store_true")
    ap.add_argument("--generated", type=int, default=0, help="puzzles built through the API")
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--errors", action="store_true")
    a = ap.parse_args()
    ONE_THREAD = a.one_thread
    if a.errors:
        errors()
    with tempfile.TemporaryDirectory() as tmp:
        generated(a.generated, a.seed, tmp)
    for path in a.puzzles:
        describe(os.path.basename(path), burrtools.load(path))
    return 0


if __name__ == "__main__":
    sys.exit(main())

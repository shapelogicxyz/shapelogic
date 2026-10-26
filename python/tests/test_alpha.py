# SPDX-License-Identifier: GPL-3.0-or-later
# What the module has beyond the original's (deferred part 7): grids, the
# solve's threads and copy, stopping a long solve, sliding and stacking
# puzzles, the imports, the Puzzlecad export and grid conversion. The
# expected values are the original's (the oracle's baselines and the
# recordings of tests/data/imports_by_original/).
import gzip
import os
import signal
import subprocess
import sys
import tempfile
import time
import unittest

import shapelogic

DATA = os.path.join(os.path.dirname(__file__), "..", "..", "tests", "data")
PUZZLES = os.path.join(DATA, "puzzles")
BY_ORIGINAL = os.path.join(DATA, "imports_by_original")


def puzzle_path(*parts):
    return os.path.join(PUZZLES, *parts)


def saved_xml(puzzle):
    """The XML the puzzle saves as."""
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "p.xmpuzzle")
        puzzle.save(path)
        with gzip.open(path, "rb") as f:
            return f.read()


def recorded_xml(name):
    with gzip.open(os.path.join(BY_ORIGINAL, name), "rb") as f:
        return f.read()


def dominoes(sx, sy, sz):
    """A box filled with 2x1x1 dominoes: many assemblies, a long search."""
    puzzle = shapelogic.Puzzle()
    box = puzzle.add_shape(sx, sy, sz, name="box")
    box.fill([(x, y, z) for x in range(sx) for y in range(sy) for z in range(sz)])
    domino = puzzle.add_shape(2, 1, 1, name="domino")
    domino.fill([(0, 0, 0), (1, 0, 0)])
    problem = puzzle.add_problem(name="dominoes")
    problem.set_result(box)
    problem.set_piece_count(domino, sx * sy * sz // 2)
    return puzzle


def placements(solutions):
    return sorted(tuple((p.piece_id, p.x, p.y, p.z, p.transformation) for p in s.placements)
                  for s in solutions)


class TestGrids(unittest.TestCase):
    def test_default_grid_is_bricks(self):
        self.assertEqual(shapelogic.Puzzle().grid, "bricks")
        self.assertEqual(shapelogic.load(puzzle_path("PelikanBurr.xmpuzzle")).grid, "bricks")

    def test_every_grid(self):
        for grid in ("bricks", "triangular prisms", "spheres", "rhombic", "tetra-octa",
                     "sliding", "stacking"):
            self.assertEqual(shapelogic.Puzzle(grid).grid, grid)
            self.assertEqual(shapelogic.Puzzle(grid=grid).grid, grid)

    def test_unknown_grid(self):
        with self.assertRaises(ValueError):
            shapelogic.Puzzle("hexagons")

    def test_grid_is_saved(self):
        puzzle = shapelogic.Puzzle("spheres")
        puzzle.add_shape(2, 2, 2).fill([(0, 0, 0)])
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "s.xmpuzzle")
            puzzle.save(path)
            self.assertEqual(shapelogic.load(path).grid, "spheres")

    def test_version(self):
        # the module's version is the git tag's, as CMake reads it
        # (cmake/ProjectVersion.cmake): vX.Y is X.Y.0
        tag = os.environ.get("SHAPELOGIC_VERSION", "")
        if tag == "":
            try:
                tag = subprocess.run(
                    ["git", "describe", "--tags", "--match", "v[0-9]*", "--abbrev=0"],
                    cwd=os.path.dirname(__file__), capture_output=True, text=True, check=True,
                ).stdout.strip()
            except (OSError, subprocess.CalledProcessError):
                self.skipTest("no git tag to compare with")
        parts = tag.lstrip("v").split(".")
        self.assertEqual(shapelogic.__version__, ".".join((parts + ["0"])[:3]))
        # an installed wheel says the same (pyproject.toml takes it from VERSION)
        import importlib.metadata
        try:
            self.assertEqual(importlib.metadata.version("shapelogic"), shapelogic.__version__)
        except importlib.metadata.PackageNotFoundError:
            pass


class TestSolve(unittest.TestCase):
    def setUp(self):
        self.puzzle = shapelogic.load(puzzle_path("PelikanBurr.xmpuzzle"))
        self.problem = self.puzzle.problems[0]

    def test_threads_find_the_same_assemblies(self):
        one = list(self.problem.solve(disassemble=False, threads=1))
        many = list(self.problem.solve(disassemble=False, threads=4))
        self.assertEqual(len(one), 12)
        self.assertEqual(placements(one), placements(many))
        # on one thread they come in order
        self.assertEqual([s.assembly_number for s in one], list(range(1, 13)))

    def test_solve_arguments_are_keywords(self):
        with self.assertRaises(TypeError):
            self.problem.solve(False, False, False, False, 1)

    def test_the_solve_works_on_a_copy(self):
        it = self.problem.solve(disassemble=False, threads=1)
        first = next(it)
        # editing the puzzle meanwhile changes nothing for the solve
        self.problem.set_piece_count(0, 0)
        self.puzzle.add_shape(1, 1, 1, name="extra")
        self.assertEqual(1 + len(list(it)), 12)
        self.assertEqual(len(first.placements), 7)

    def test_solving_leaves_the_puzzle_as_it_was(self):
        before = saved_xml(self.puzzle)
        self.assertEqual(len(list(self.problem.solve())), 1)
        self.assertEqual(saved_xml(self.puzzle), before)

    def test_stop_ends_a_long_solve(self):
        problem = dominoes(2, 4, 6).problems[0]
        it = problem.solve(disassemble=False)
        for _ in range(5):
            next(it)
        started = time.monotonic()
        it.stop()
        self.assertLess(time.monotonic() - started, 10)
        self.assertFalse(it.is_finished)
        # what was queued before the stop still comes, then the end
        left = list(it)
        self.assertLessEqual(len(left), 4)
        with self.assertRaises(StopIteration):
            next(it)

    def test_a_dropped_iterator_stops_its_solve(self):
        problem = dominoes(2, 4, 6).problems[0]
        it = problem.solve(disassemble=True)
        next(it)
        started = time.monotonic()
        del it
        self.assertLess(time.monotonic() - started, 10)

    def test_a_stopped_take_apart(self):
        # stop while the take-apart of each assembly is the work
        it = self.problem.solve(disassemble=True)
        it.stop()
        self.assertEqual(list(it), [])

    @unittest.skipIf(sys.platform == "win32", "no SIGINT to send")
    def test_ctrl_c_stops_a_solve(self):
        # next() waits with the GIL let go, and looks for a signal every
        # 50 ms: Ctrl-C stops the solve and raises KeyboardInterrupt
        script = (
            "import sys; sys.path.insert(0, %r)\n"
            "import test_alpha\n"
            "it = test_alpha.dominoes(2, 4, 6).problems[0].solve(disassemble=False)\n"
            "next(it)\n"
            "print('solving', flush=True)\n"
            "try:\n"
            "    for _ in it: pass\n"
            "except KeyboardInterrupt:\n"
            "    print('interrupted', it.is_finished, flush=True)\n"
        ) % os.path.dirname(os.path.abspath(__file__))
        child = subprocess.Popen([sys.executable, "-c", script], stdout=subprocess.PIPE, text=True)
        try:
            self.assertEqual(child.stdout.readline().strip(), "solving")
            time.sleep(0.3)
            child.send_signal(signal.SIGINT)
            out, _ = child.communicate(timeout=20)
        finally:
            child.kill()
        self.assertEqual(out.strip(), "interrupted False")
        self.assertEqual(child.returncode, 0)

    def test_errors_come_from_next(self):
        puzzle = shapelogic.Puzzle()
        box = puzzle.add_shape(2, 2, 2)
        box.fill([(0, 0, 0)])
        piece = puzzle.add_shape(1, 1, 1)
        piece.fill([(0, 0, 0)])
        problem = puzzle.add_problem()
        problem.set_result(box)
        problem.set_piece_count(piece, 2)
        it = problem.solve()
        with self.assertRaisesRegex(RuntimeError, "Pieces contain too many units"):
            next(it)
        self.assertTrue(it.is_finished)

    def test_unknown_solver_names(self):
        with self.assertRaises(ValueError):
            self.problem.solve(sliding_solver="slow")
        with self.assertRaises(ValueError):
            self.problem.solve(stacking_solver="hanoi")


class TestSliding(unittest.TestCase):
    def test_sliding_solver(self):
        # 81 moves, as the original's burrTxt (the oracle's baseline)
        puzzle = shapelogic.load(puzzle_path("test_sliding_solver.xmpuzzle"))
        self.assertEqual(puzzle.grid, "sliding")
        for solver in ("fast", "deep", "full"):
            solutions = list(puzzle.problems[0].solve(sliding_solver=solver))
            self.assertEqual(len(solutions), 1, solver)
            self.assertTrue(solutions[0].has_disassembly)
            self.assertEqual(solutions[0].moves_text, "81")
            self.assertEqual(solutions[0].level, 81)
            self.assertEqual(solutions[0].total_moves, 81)

    def test_start_layouts_without_the_search(self):
        puzzle = shapelogic.load(puzzle_path("test_sliding_solver.xmpuzzle"))
        layouts = list(puzzle.problems[0].solve(disassemble=False))
        self.assertEqual(len(layouts), 1)
        self.assertFalse(layouts[0].has_disassembly)

    def test_nested_slides(self):
        # the original's burrTxt -d with and without --nested-slides
        for name, plain, nested in (("test_sliding_nested", [], ["47"]),
                                    ("slide_fuzz_05", ["12"], ["8"])):
            problem = shapelogic.load(puzzle_path("sliding", name + ".xmpuzzle")).problems[0]
            self.assertEqual([s.moves_text for s in problem.solve()], plain, name)
            self.assertEqual([s.moves_text for s in problem.solve(nested_slides=True)], nested, name)


class TestStacking(unittest.TestCase):
    def test_hanoi(self):
        puzzle = shapelogic.load(puzzle_path("test_stacking_hanoi.xmpuzzle"))
        self.assertEqual(puzzle.grid, "stacking")
        solutions = list(puzzle.problems[0].solve())
        self.assertEqual([s.moves_text for s in solutions], ["7"])
        self.assertEqual(solutions[0].total_moves, 7)
        self.assertEqual(solutions[0].level, 7)

    def test_panex_swap(self):
        puzzle = shapelogic.load(puzzle_path("stacking", "stack_panex_swap_3.xmpuzzle"))
        for solver in ("stacking", "panex"):
            solutions = list(puzzle.problems[0].solve(stacking_solver=solver))
            self.assertEqual([s.moves_text for s in solutions], ["42"], solver)

    def test_panex_with_a_work_folder(self):
        puzzle = shapelogic.load(puzzle_path("stacking", "stack_panex_jr.xmpuzzle"))
        with tempfile.TemporaryDirectory() as tmp:
            solutions = list(puzzle.problems[0].solve(stacking_solver="panex", work_dir=tmp))
        self.assertEqual([s.moves_text for s in solutions], ["143"])


class TestImports(unittest.TestCase):
    def test_puzzlesolver3d(self):
        puzzle = shapelogic.import_puzzlesolver3d(os.path.join(DATA, "imports", "pieces.puz"))
        self.assertEqual(saved_xml(puzzle), recorded_xml("pieces_puz.xmpuzzle"))

    def test_puzzlecad(self):
        for name in ("cube", "plate"):
            puzzle = shapelogic.import_puzzlecad(os.path.join(DATA, "imports", name + ".scad"))
            self.assertEqual(saved_xml(puzzle), recorded_xml(name + "_scad.xmpuzzle"))

    def test_refused_imports(self):
        with self.assertRaises(RuntimeError):
            shapelogic.import_puzzlesolver3d(os.path.join(DATA, "imports", "comma.puz"))
        with self.assertRaises(RuntimeError):
            shapelogic.import_puzzlecad(os.path.join(DATA, "imports", "nopieces.scad"))
        with self.assertRaises(RuntimeError):
            shapelogic.import_puzzlecad("/nonexistent/x.scad")

    def test_export_puzzlecad(self):
        puzzle = shapelogic.load(puzzle_path("PelikanBurr.xmpuzzle"))
        with open(os.path.join(BY_ORIGINAL, "PelikanBurr.p0.scad")) as f:
            self.assertEqual(puzzle.export_puzzlecad(0, name="PelikanBurr.xmpuzzle"), f.read())
        with open(os.path.join(BY_ORIGINAL, "PelikanBurr.p1.scad")) as f:
            self.assertEqual(puzzle.export_puzzlecad(name="PelikanBurr.xmpuzzle"), f.read())
        self.assertIn("unknown", puzzle.export_puzzlecad(0))

    def test_export_refusals(self):
        with self.assertRaisesRegex(RuntimeError, "only available for cube-grid"):
            shapelogic.load(puzzle_path("Bermuda.xmpuzzle")).export_puzzlecad()
        with self.assertRaisesRegex(RuntimeError, "Nothing to export"):
            shapelogic.Puzzle().export_puzzlecad()
        empty = shapelogic.Puzzle()
        empty.add_shape(1, 1, 1)
        with self.assertRaisesRegex(RuntimeError, "Could not export"):
            empty.export_puzzlecad()
        with self.assertRaises(IndexError):
            shapelogic.load(puzzle_path("PelikanBurr.xmpuzzle")).export_puzzlecad(5)

    def test_convert_grid(self):
        puzzle = shapelogic.load(puzzle_path("CubeInCage.xmpuzzle"))
        self.assertEqual(puzzle.convert_targets, ["rhombic", "tetra-octa"])
        for grid in puzzle.convert_targets:
            converted = puzzle.convert_grid(grid)
            self.assertEqual(converted.grid, grid)
            self.assertEqual(saved_xml(converted), recorded_xml(f"CubeInCage.{grid}.xmpuzzle"))
        # the puzzle itself stays as it was
        self.assertEqual(puzzle.grid, "bricks")

    def test_convert_refusals(self):
        bermuda = shapelogic.load(puzzle_path("Bermuda.xmpuzzle"))
        self.assertEqual(bermuda.convert_targets, [])
        with self.assertRaises(RuntimeError):
            bermuda.convert_grid("rhombic")
        with self.assertRaises(ValueError):
            shapelogic.Puzzle().convert_grid("hexagons")


if __name__ == "__main__":
    unittest.main()

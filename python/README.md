# shapelogic: the Python module

Python bindings for the ShapeLogic core: load, build, save and solve
puzzles from Python. The module keeps the classes, functions and names of
the original BurrTools module (`src/python` there), so scripts written for
it keep working once the import reads `import shapelogic as burrtools`; what is
new is listed below.

```python
import shapelogic

puzzle = shapelogic.load("tests/data/puzzles/PelikanBurr.xmpuzzle")
for sol in puzzle.problems[0].solve(disassemble=True):
    print(sol.level, sol.moves_text, len(sol.placements))   # 98 99.3.4.2 7

puzzle = shapelogic.Puzzle()
box = puzzle.add_shape(2, 2, 2, name="cube")
box.fill([(x, y, z) for x in range(2) for y in range(2) for z in range(2)])
slab = puzzle.add_shape(1, 2, 2, name="slab")
slab.fill([(0, y, z) for y in range(2) for z in range(2)])
problem = puzzle.add_problem(name="assemble 2x2x2")
problem.set_result(box)
problem.set_piece_count(slab, 2)
print(len(list(problem.solve(disassemble=False))))
puzzle.save("cube.xmpuzzle")
```

## The API

As the original's:
- `load(filename)`, `Puzzle.load`, `Puzzle.save`; `Puzzle()` (the cube
  grid), `comment`, `num_shapes`, `num_problems`, `shapes`, `shape(i)`,
  `add_shape(sx, sy, sz, name="")`, `problems`, `problem(i)`,
  `add_problem(name="")`.
- `Shape`: `name`, `index`, `size_x` / `size_y` / `size_z`, `dimensions`,
  `get` / `set` (the raw cell value), `get_state` / `set_state` (`Voxel.EMPTY`,
  `FILLED`, `VARIABLE`), `get_color` / `set_color`, `is_empty` /
  `is_filled` / `is_variable`, `fill(coords)`, `count_filled`,
  `count_empty`.
- `Problem`: `name`, `index`, `num_pieces`, `result_shape_index` (-1 when
  unset), `set_result`, `set_piece_count`, `set_piece_range` (by shape or
  index), `get_piece_min` / `get_piece_max`, and
  `solve(disassemble=True, reduce=False, keep_rotations=False,
  keep_mirror=False)`.
- `SolutionIterator` (what `solve` returns): yields `Solution`s while the
  search goes on (`assembly_number`, `solution_number`, `has_disassembly`,
  `moves_text`, `level`, `total_moves`, `placements` of `Placement`:
  `piece_id`, `x`, `y`, `z`, `transformation`, `is_placed`); `stop()`,
  `is_finished`, `iterations`. Ctrl-C while it waits stops the solve.
- Errors: `RuntimeError` for a file that cannot be read, a cell outside
  its shape, a colour past 63 and a solve that fails (raised by `next()`);
  `IndexError` for a shape or problem that does not exist.

New here:
- `Puzzle(grid)`: any grid by name (`"bricks"`, `"triangular prisms"`,
  `"spheres"`, `"rhombic"`, `"tetra-octa"`, `"sliding"`, `"stacking"`),
  and `Puzzle.grid`; `shapelogic.__version__`.
- `solve(..., *, threads=0)`: the solve's threads (0, the default, every
  core, as the original's module ran).
- Sliding and stacking puzzles solve as the GUI solves them (through the
  core's solve session): `sliding_solver` (`"fast"`, `"deep"` or `"full"`,
  the default), `nested_slides`, `partial_nested`, `high_memory`;
  `stacking_solver` (`"stacking"`, the default, or `"panex"`); `work_dir`,
  where a full sliding search or a Panex search keeps its levels (empty:
  in memory only). Their solutions come when the search ends; a stacking
  solution counts transfers, as `bt` does.
- `import_puzzlesolver3d(filename)`, `import_puzzlecad(filename)`,
  `Puzzle.export_puzzlecad(problem=None, name="")` (the text),
  `Puzzle.convert_targets` and `Puzzle.convert_grid(grid)` (a new
  puzzle): File ▸ Import, File ▸ Export ▸ Puzzlecad and Edit ▸ Convert of
  the app.

Differences from the original module:
- `solve` works on a copy of the puzzle taken when it is called: editing
  the puzzle while iterating does not touch the search (the original's
  thread read the live puzzle), and solving changes nothing in the puzzle
  (the original's reset its shapes' hotspots).
- A `fill` stopped by a cell outside the shape keeps the cells before it
  filled, as the original, but the shape's bounding box is kept up to date
  afterwards (the original's stayed switched off).
- A take-apart stops with the iterator (the original's ran to its end).

## Building

The module is built with the rest of the project when CMake finds
Python's headers (`BT_PYTHON=AUTO`, the default; `ON` requires it, `OFF`
skips it; sanitizer builds and builds for another architecture skip it
under `AUTO`). pybind11 3.0.0 is fetched, the original's release.

```bash
just build release           # build/release/python/shapelogic.*.so
just python-test             # the tests in python/tests, on that module
PYTHONPATH=build/release/python python3 -c "import shapelogic"
```

`just test-all` (ctest) runs the same tests when the module is built.
`oracle.py python` compares the module with the original's (see
tests/oracle/README.md).

## A wheel

`pyproject.toml` builds the wheel with scikit-build-core from the whole
CMake project (one folder up), configured for the module alone:

Its version is the project's, from the git tag: `just python-wheel` writes
it to `python/VERSION` (git-ignored) and builds the wheel in `build/wheel`.
By hand, write that file first:

```bash
python3 -m venv /tmp/bt-venv
git describe --tags --match 'v[0-9]*' --abbrev=0 | sed -E 's/^v//; s/^([0-9]+\.[0-9]+)$/\1.0/' > python/VERSION
/tmp/bt-venv/bin/pip wheel ./python -w dist/      # from the repository root
/tmp/bt-venv/bin/pip install dist/shapelogic-*.whl
/tmp/bt-venv/bin/python -c "import shapelogic; print(shapelogic.__version__)"
```

`just python-wheel` does the first two and checks the installed module
imports and solves. The wheel is for the Python and platform it was built
with; there is no source distribution (the module needs the whole
repository).

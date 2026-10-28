# 0018: The Python module

**Status:** accepted (deferred part 7); `python/` (`src/`, `tests/`,
`pyproject.toml`, `README.md`), `cmake/Python.cmake`,
`tests/oracle/python_transcript.py` and `oracle.py python`.

## Context
The original BurrTools had a Python module (`src/python`, pybind11
3.0.0, meson's `python` option, `auto` by default) with six classes
(`Puzzle`, `Shape`, `Problem`, `Solution`, `Placement`,
`SolutionIterator`), the `Voxel` states and `load`, and eight tests
(`test/python/test_burrtools.py`). Its one client in the repository,
`example_scripts/search_cube_puzzles.py`, builds thousands of small
puzzles and solves them. Its `solve` started a thread that ran the
cube grids' assembler (`findAssembler`, every core) on the live puzzle
and took each assembly apart in the callback (`disassembler_0_c`, no
rotations), handing solutions over through a queue of four.

## Decision
**Port the module over the new core with the original's names**, so its
scripts run unchanged: the same classes, properties, methods, argument
names and defaults, reprs, docstrings, exception types (`IndexError` for
a shape or problem that is not there, `RuntimeError` for a cell outside
its shape, a colour past 63, a file that cannot be read and a failed
solve, raised by `next()`), the queue of four, the GIL let go while
waiting, Ctrl-C stopping the solve. The oracle is the original's module
itself: `oracle.py python` runs one transcript script
(`python_transcript.py`) with each module on PYTHONPATH (the error
cases; 300 puzzles built through the API, saved and solved; each corpus
puzzle of the tier read cell by cell and solved for its assemblies, its
solutions, and reduced with mirrors and rotations kept, on one thread in
order with their numbers and the search's iterations, and on all
threads as sorted lists), and the transcripts must be the same.

**`solve` works on a copy of the puzzle** taken when it is called. The
original's thread read the live puzzle while Python could edit it, and
reset its shapes' hotspots, so a save after a solve differed from one
before. Nothing else changes: brick puzzles run as the original ran them
(`makeAssembler` with the Classic solver, `createMatrix`, `reduce`,
`assemble` with each assembly taken apart in the callback, on a pool of
every core, the take-apart's levels on a pool of their own, as `bt`
does). The take-apart now stops with the iterator (one `StopSource` per
iterator, decision 0004).

**Sliding and stacking puzzles solve through a `SolveSession`**
(decisions 0010, 0014, 0015), as every solve of theirs does: what the
GUI checked before (the start/goal shape, the stamps) and set up (the
start locks, the discs' hotspots) on the copy, then the session's run,
whose solutions are yielded when it ends (a sliding run replaces a
solution by a shorter one to the same picture, so none is final
before). The original's module assembled them as brick puzzles, which
gave no meaningful answer. Their options are keywords of `solve`
(`nested_slides`, `partial_nested`, `high_memory`,
`stacking_solver`, `work_dir`); a stacking solution counts transfers, as
`bt` and the engine do. The module reads no environment variable:
`work_dir` is empty (in memory) unless given.

**Additions** are keywords or new names only: `Puzzle(grid)` and
`Puzzle.grid`, `solve(..., *, threads=0)` (keyword only, so the
original's positional arguments keep their meaning), `__version__`, and
part 6's imports, Puzzlecad export and grid conversion
(`import_puzzlesolver3d`, `import_puzzlecad`, `Puzzle.export_puzzlecad`,
`Puzzle.convert_targets`, `Puzzle.convert_grid`), with the document's
refusals in the original's words. The editing commands of the app layer
(undo, sliding stamps, rod sets) are not bound: the module edits the
model directly, as the original's did.

**Kept as the original:** `fill` stopped by a cell outside the shape
keeps the cells before it filled (but restores the bounding box's
recalculation, which the original left switched off); `set` stores the
value's low byte; a file's texts are bytes, handed to Python as UTF-8
(Prisgon's Latin-1 comment raises `UnicodeDecodeError` in both).

**The build:** `BT_PYTHON` (`AUTO`, `ON`, `OFF`), `AUTO` building the
module when CMake finds Python's headers, except in sanitizer builds
(Python cannot load an instrumented module without the runtime
preloaded) and builds for another architecture (the original skipped
cross builds). pybind11 3.0.0, the original's, is fetched and pinned.
The static core is linked into the module, so the whole build is
position independent when the module is on (a no-op on macOS). The
wheel is built by scikit-build-core from the whole project
(`python/pyproject.toml`, `cmake.source-dir = ".."`, the `python`
install component only); there is no source distribution.

## Consequences
- `just test-all` runs the module's tests (ctest `python`) when it is
  built; `just python-test`, `just python-wheel` (the wheel installed into
  a fresh venv and tested there) and `just oracle-python`.
- The module is checked under ThreadSanitizer and AddressSanitizer by
  hand (a build with `BT_SANITIZE` and `BT_PYTHON=ON`, the runtime
  preloaded into Python's own binary, not the framework's launcher),
  not by `just test-tsan`/`test-asan`: see docs/handoff/deferred-7.md.
- The original's module must be built (`meson compile -C build` in its
  checkout) for `oracle.py python`.

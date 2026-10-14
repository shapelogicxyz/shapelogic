# Handoff: deferred part 7 (the Python module)

Part 7 of docs/DEFERRED-PLAN.md, the last: the original's Python module
(`src/python`, `test/python`) ported over the new core as `python/`, with
the original's names, so its scripts run unchanged; sliding and stacking
puzzles solved through the solve session; part 6's imports, Puzzlecad
export and grid conversion bound too; a wheel. The original module is the
oracle: the same transcript, line for line, from both. No audit after this
part, as the plan says. With it the deferred plan is done.

## Done

One commit, "Deferred 7: …": no `Engine:` or `Core:` commit (nothing in
`core/`, `engine/`, `api/` or `web/` changed).

### The module (decision 0018)
- `python/src/`: `module.cpp` (the original's classes, properties,
  methods, argument names and defaults, docstrings and reprs; the `Voxel`
  states; `load`), `py_puzzle`, `py_shape`, `py_problem` (the puzzle as a
  `shared_ptr<Puzzle>` and an index, as the original's), `py_solution.h`,
  `solution_iterator` (the solve: a copy of the puzzle and a thread of
  its own; a queue of four; the GIL let go while waiting, before the
  queue's lock is taken; Ctrl-C stops it; errors raised by `next()`).
- Brick puzzles solve as the original's iterator solved them: the Classic
  assembler (assembler 0 or 1, `makeAssembler`), `createMatrix` with the
  mirror and rotation flags, `reduce` when asked, `assemble` on every core
  (`threads`), each assembly taken apart in the callback (no rotations,
  the levels on a pool of their own, as `bt`). Sliding and stacking
  puzzles go through a `SolveSession` (the GUI's start/goal and stamp
  checks and its set-up, the discs' hotspots), their solutions yielded
  when the run ends; a stacking solution counts transfers, as `bt` and the
  engine.
- New, as keywords or new names: `Puzzle(grid)` and `Puzzle.grid`,
  `solve(..., *, threads=0, sliding_solver="full", nested_slides=False,
  partial_nested=False, high_memory=False, stacking_solver="stacking",
  work_dir="")`, `__version__`, `import_puzzlesolver3d`,
  `import_puzzlecad`, `Puzzle.export_puzzlecad(problem=None, name="")`,
  `Puzzle.convert_targets`, `Puzzle.convert_grid(grid)` (the document's
  refusals, in the original's words). The handoff of part 6 asked whether
  to bind those: they are bound (a line each over the core's functions,
  and checked against the recordings of part 6).
- The build: `BT_PYTHON` (`AUTO` default, `ON`, `OFF`; `cmake/Python.cmake`):
  `AUTO` builds the module when CMake finds Python 3.10+'s headers, but
  not in sanitizer builds or builds for another architecture (`release-x64`,
  `release-arm64`); the build is then position independent. pybind11
  3.0.0 (the original's, pinned, `cmake/Dependencies.cmake`). The wheel:
  `python/pyproject.toml` (scikit-build-core, the whole project one folder
  up, only the `python` install component, no sdist).
- `just python-test [preset]`, `just python-wheel` (built in a venv,
  installed into a fresh one, the tests run there), `just oracle-python`;
  ctest `python` (in `just test` and `just test-all`) when the module is
  built.
- `python/README.md`: the API, what is new, the differences, building, the
  wheel.

### The harness
- `tests/oracle/python_transcript.py` and `oracle.py python`
  (`just oracle-python`): the same script with each module on PYTHONPATH;
  the transcripts must be equal. It holds the error cases (each wrong
  call's exception type and the cells after it), 300 puzzles built through
  the API from a seeded generator (saved XML and three solves each), and
  each corpus puzzle of the tier read cell by cell and solved three ways
  (assemblies; solutions; reduced with mirrors and rotations kept), on
  one thread in order with numbers and iterations, and on all threads as
  sorted lists. The original's module comes from its meson build folder.

### Tests
- `python/tests/test_shapelogic.py`: the original's eight tests, unchanged
  but for the fixture's place.
- `python/tests/test_alpha.py` (28): the grids; the version against
  `pyproject.toml`; threads give the same assemblies; the arguments after
  the original's four are keywords; the solve works on a copy and leaves
  the puzzle as it was; `stop()` ends a long solve (a 2x4x6 box of
  dominoes) at once, queued solutions still handed out; a dropped
  iterator stops its solve; Ctrl-C (SIGINT to a child) raises
  `KeyboardInterrupt` and stops it; errors come from `next()`; the
  sliding solver (81 moves with each solver; the start layouts alone),
  nested slides (the original's `burrTxt` with and without
  `--nested-slides`: test_sliding_nested none / 47, slide_fuzz_05 12 / 8);
  stacking (Hanoi 7, the 3-disc Panex swap 42 with both solvers, Panex Jr
  143 with a work folder); the imports, export and conversion against
  `tests/data/imports_by_original/`, and their refusals.
- The original's `example_scripts/search_cube_puzzles.py` (`-c 3 -p 3
  --count-all --save-dir`) gives the same output with both modules, and
  its 146 saved puzzles are the same XML.

## Gate results

All on the final tree (2026-10-12, M-series Mac):
- `just test-all` 7/7 (622 s: unit 96 s, unit-slow 487 s, contract 33 s,
  cli 3 s, the new `python` 2.4 s); `just test` twice more, 6/6.
- `just oracle-python` (fast tier and 300 built puzzles): 39/39
  transcripts the same as the original module's (12 s, release).
  `--tier full --generated 0`: 311/317 the same; the other 6 are three
  puzzles (LogisTIC, PackTIC_5, ACornBox1n2) that pass the 10-minute
  limit in both modules, on one thread and on all (the solve with
  mirrors and rotations kept multiplies their assemblies): no difference
  found (47 min).
- `just oracle --tier fast` 19/19 on all threads and with `--threads 1`
  (nothing moved: the dev and release builds are now position
  independent, everything rebuilt); `just oracle-roundtrip` 511/511
  byte-identical.
- The module under ThreadSanitizer and AddressSanitizer + UBSan by hand
  (see "Known issues"): clean. `just test-asan` and `just test-tsan` not
  run: no core, engine or CLI source changed, and those presets build
  nothing new (the module is off in them, and position independence is
  only switched on with it).
- `just python-wheel`: the wheel built, installed into a fresh venv,
  36/36 tests there.
- The original's `search_cube_puzzles.py -c 3 -p 3 --count-all`, release:
  2.81 s with the new module, 2.93 s with the original's (best of three,
  back to back).
- Front-end gates not run: nothing in `web/`, `engine/` or `api/` changed.
- `just check`: not run (LLVM is not installed).

## Differences from the original, and why

1. **`solve` works on a copy of the puzzle** taken when it is called. The
   original's thread read the live puzzle while Python could edit it (a
   data race), and reset its shapes' hotspots, so a save after a solve
   differed from one before.
2. **Sliding and stacking puzzles solve as the GUI solves them** (the
   solve session); the original's module assembled them as brick puzzles.
3. **A stopped iterator stops its take-apart too** (the original's ran to
   the end of the assembly in hand).
4. **`fill` restores the bounding box's recalculation** when a cell is
   outside the shape (the cells before it are filled, as in the
   original); the original left it switched off, so that shape's empty
   cells kept colours set later.
5. **`next()` lets go of the GIL before taking the queue's lock**: the
   original took the lock with the GIL held and let the GIL go inside,
   so two Python threads on one iterator could deadlock.
6. Additions only as keywords and new names (above); `solve`'s four
   original arguments keep their places.

## Found in the original (worth fixing there)

- `SolutionIterator::worker_run` reads and changes (`initHotspot`) the
  live puzzle while Python may edit it.
- `PyShape::fill` leaves `skipRecalcBoundingBox(true)` on when a
  coordinate is outside the shape (the assertion's exception skips the
  reset).
- `SolutionIterator::next` waits for the GIL while holding the queue's
  lock (see 5 above).
- The module hands the file's texts to Python as UTF-8; a Latin-1 comment
  or name (Prisgon's comment) raises `UnicodeDecodeError` when read. Kept
  as it is here.

## Known issues and notes

- `just test-asan` and `just test-tsan` do not cover the module (`AUTO`
  leaves it out of sanitizer builds: Python cannot load an instrumented
  module unaided). It was checked by hand: a build with
  `-DBT_SANITIZE=thread` (and `address,undefined`) `-DBT_PYTHON=ON
  -DBT_BUILD_TESTS=OFF`, target `burrtools`, and Python's own binary
  (`…/Python.framework/Versions/3.14/Resources/Python.app/Contents/MacOS/Python`,
  not the `python3` launcher, which drops `DYLD_INSERT_LIBRARIES`) run
  with `DYLD_INSERT_LIBRARIES=$(clang -print-file-name=libclang_rt.tsan_osx_dynamic.dylib)`
  (ASan: `…asan_osx_dynamic.dylib` and `PYTHONMALLOC=malloc`). The module's
  tests and transcripts of Bermuda, PelikanBurr, DemoPieceGenerator,
  SolidSixPieceBurrs, HexSticks, BallRoom, the sliding and the stacking
  fixtures were clean in both; the one failure, the Ctrl-C test, starts
  its child through the launcher, which loses the preloaded runtime.
- The wheel is tagged for the macOS it was built on (`macosx_26_0` here)
  unless `MACOSX_DEPLOYMENT_TARGET` is set; nothing in the project sets
  one (the engine neither).
- `just check`: not run (LLVM is not installed).
- The module binds the model, not the document: no undo, no sliding stamps
  or rod-set commands (the original's module had none either).

## Brief for what comes next

The deferred plan is done. Next is the first release, as web-8.md's
"Brief for the first release" describes (the other platforms by hand,
signing, the artefacts, a tag, `docs/handoff/release-0.1.0.md`), with
two additions from this plan: the release notes name what the deferred
parts added (sliding and stacking/Panex puzzles, the STL meshes and
exports, the imports, the Puzzlecad export and grid conversion, the
Python module) instead of the interim STL and the unsolvable sliding and
stacking files web-8.md expected; and the Python wheel built on each
platform (`just python-wheel`), if the module is to be released.

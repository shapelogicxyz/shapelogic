# Handoff: deferred part 6 (the imports, the Puzzlecad export, grid conversion)

Part 6 of docs/DEFERRED-PLAN.md: the original's PuzzleSolver3D and
Puzzlecad loaders, its Puzzlecad writer and its grid converter ported to
the core line for line; the document's imports, export and conversion;
three engine methods; and the front end's File ▸ Import ▸ PuzzleSolver3D
and Puzzlecad, File ▸ Export ▸ Puzzlecad and Edit ▸ Convert Brick Grid
Type to Other, no longer greyed. Both programs give the same bytes for
2,059 imports, exports and conversions over the whole corpus. No audit
after this part, as the plan says. Next: part 7, the Python bindings.

## Done

Six commits, in order:
- `c0574ba` **Core: the imports of PuzzleSolver3D and Puzzlecad files,
  the Puzzlecad export, grid conversion**: `core/shapelogic/io/imports.h`
  (`ps3d_file.cpp` from ps3dloader.cpp, `scad_file.cpp` from
  scadloader.cpp), `core/shapelogic/model/grid_convert.h` (converter.cpp),
  `io::readPlainFile`, `Document::importFile`, `importBytes`,
  `convertGrid`, `exportPuzzlecad`; the harness (`imports_old.cpp`,
  `tests/tools/imports.cpp` = `bt_imports`, `oracle.py imports` and
  `record-imports`), the inputs `tests/data/imports/` and the
  recordings `tests/data/imports_by_original/`; the unit tests.
- `9b6da60` **Engine: document.import** (and its path in the main
  process's grants, `web/src/main/path-grants.ts`).
- `cfa069d` **Core: the Puzzlecad export's comments say what it writes
  without a problem** (every shape, the result too: the comments of the
  first commit said otherwise).
- `4b6c560` **Engine: document.exportPuzzlecad**.
- `693a6bc` **Engine: document.convertGrid, engine.info
  grids[].convertsTo**.
- "Deferred 6: …" (this note): the front end, `just oracle-imports`, the
  docs.

### The core (decision 0017)
- `io::readPuzzleSolver3D(text)`: the original's state machine over
  `getline`'s lines (the same lines, a last one without its newline
  too), the size line read by an `istringstream` as the original read it
  (so "2x1x2", "-1", an overflow behave the same), a short row empty to
  its end, a comma refused, a carriage return a filled cell as in the
  original; the problem "Problem", one of each piece; pieces identical
  under rotation (not mirrors) merged into the first, the others named
  "Duplicate" and dropped from the problem, with the original's index
  steps (three in a row merge into one).
- `io::readPuzzlecad(text)`: the original's call finder (comments and
  strings skipped, `*` disabling a call), its parser (named arguments,
  nested arrays to 200 deep, concatenated strings, escapes), layers to a
  shape (`|` rows, `.` empty, `{...}` skipped; a piece without a cell or
  past 256 a side left out), "Piece N" one of each in "Puzzle", the first
  `packing_box` as the result, else a cube of variable cells as long as
  the longest piece.
- `io::writePuzzlecad(puzzle, problem, sourceName)`: the original's
  MagellanTIC.scad layout, the distinct shapes of the problem's parts but
  its result (when none, the other shapes; every shape without a
  problem), each its bounding box's layers; nothing when no shape has a
  cell.
- `convertGrid(puzzle, target)`: bricks to rhombic (5x5x5 cells a cube,
  only the grid's valid cells) and to tetra-octa (3x3x3), the cell values
  with their colours, the names (not the weights, as the original), the
  colours, the notes and popup flag, each problem's definition with its
  name (`addProblem` = `copyDefinition`: no solutions, unsolved).
- Budgets the original lacked: 2^27 cells for an import's shapes and a
  conversion's (decision 0017), a parameter so the tests reach them
  without allocating.
- `Document::importFile`/`importBytes`: a new document, dirty, nothing to
  undo; `convertGrid`: a new document, dirty, refused while solving;
  `exportPuzzlecad`: the original's three refusals in its words
  ("Puzzlecad export is only available for cube-grid puzzles.", "Nothing
  to export.", "Could not export puzzlecad file."), the problem read
  through `readProblem` (allowed while solving).

### The harness
- `oracle.py imports` (`just oracle-imports`, about 7 s): `imports_old`
  (built against the original's library; cb_Export_Scad's two checks
  copied, as they are GUI code) and `bt_imports` (through the new
  `Document`) take the same arguments. Run on: the 13 committed inputs,
  the original's `tictest/openscad/MagellanTIC.scad` and `HoleyTIC.scad`
  (read in place, not copied), and each corpus puzzle (all tiers,
  sliding, stacking, the round trip's extras, the fixtures) converted to
  rhombic, tetra-octa and prisms (refused in both), exported to Puzzlecad
  for each problem and for one past the last, each export imported again,
  and a PuzzleSolver3D file made of each cube-grid puzzle's shapes
  (`ps3d_text`). **2059/2059 the same**: 356 conversions, 410 exports and
  their 410 re-imports, 178 PuzzleSolver3D files, 9 inputs read (7 of
  ours, the original's 2), and 696
  refusals in both (conversions of other grids and to prisms, exports of
  other grids, 6 broken inputs).
- `oracle.py record-imports` → `tests/data/imports_by_original/` (50
  files): the original's saves of the 13 inputs (gzip as it wrote
  them, `<name>_<ext>.refused` for the 6 it refused), and for the 9
  cube-grid fixtures their two conversions and their exports per problem.

### The engine
- `document.import {format: puzzleSolver3D | puzzlecad, path | data}`: a
  new document without a path, the open one gone as with `document.new`;
  the path checked by the main process's grants.
- `document.exportPuzzlecad {problem?, name?}` → `{text}`: the header
  names the document's file, else `name` (no control characters, at most
  255 bytes).
- `document.convertGrid {grid}`: the document replaced, the path kept;
  `unsupported` for a grid the puzzle does not convert to,
  `invalid_state` while solving, `invalid_argument` past the budget.
- `engine.info`'s `grids[].convertsTo`: the dialog's choices.
- The contract suite (`imports_exports_conversions`, and the guards
  test): both imports by path and bytes equal to the original's, the
  refusals, the export of PelikanBurr with and without a problem equal
  to the recordings, a document without a file naming the export's,
  CubeInCage converted to both grids equal to the recordings and saved
  over its file, conversion refused while a run is active and the export
  allowed then. 122 methods, all called.

### The front end (docs/webui-spec.md section 11)
- `web/src/renderer/shell/imports.ts` (`Imports`): File ▸ Import ▸
  PuzzleSolver3D… and Puzzlecad (OpenSCAD)… (the unsaved-changes
  question, the system's open dialog with the original's filters and
  titles, the puzzle shown as the file's name without its extension, the
  Entities workspace brought forward, "Could not load puzzle, sorry!" and
  the engine's reason in a message box), File ▸ Export ▸ Puzzlecad
  (OpenSCAD)… (the engine asked first so a refusal comes before the save
  dialog, then the save dialog "Export Puzzlecad File", the text written
  by the main process; the problem the Puzzle workspace shows, else the
  first), Edit ▸ Convert Brick Grid Type to Other… (`#convertGrid`:
  "Please Select Target grid", radio buttons from `convertsTo` with the
  labels New shows, a line saying the puzzle is replaced and Save writes
  over its file, Abort and Continue; "There are no grids that this
  puzzle can be converted to, sorry" and Ok).
- The bridge: open kinds `ps3d` and `scad`, the export kind `scad`
  (main/app-ipc.ts's dialogs and `ExportGrants`), the commands
  `importPs3d`, `importScad`, `exportScad`, `convertGrid`; the menus
  (main/menu.ts) enable them (Export ▸ Puzzlecad with a shape with cells,
  Convert with a document and nothing running); `later()` is gone, the
  anaglyph modes are the only greyed items left.

### Removed "not yet" notes
The four greyed menu items and main/menu.ts's header comment, the
e2e checks that they were greyed (files.e2e.ts, exports.e2e.ts),
FRONTEND-PLAN.md's rows for the imports, conversion and the Puzzlecad
export, README's and web/README's status lines, ARCHITECTURE's state.

### Tests
- C++: `tests/unit/io/test_imports.cpp` (the original's two cases from
  test_puzzle_model.cpp; each input, conversion and export against the
  recordings, through the document; the refusals and their words; the
  header's name; the budgets; the conversion targets; a gzip file read as
  it is), the conversion refused and the export allowed while solving in
  `test_document_solving.cpp`.
- Front end: `tests/unit/app.test.ts` (the menu items' states, the
  `scad` export grant), `tests/e2e/imports.e2e.ts` (both imports, saved
  as the original's; the refusal; the export with ".scad" added; the
  refusal of a prism puzzle before the dialog; the conversion's dialog,
  Abort, Continue, the save over the file; a grid with nothing to convert
  to).

## Gate results

All on the final core and engine (2026-10-12, M-series Mac):
- `just test-all` 6/6 (622 s; unit 97 s, unit-slow 490 s, contract 33 s
  with 122 methods called); the quick suite (`just test`) passed twice
  more.
- `just test-asan` 6/6 (2,040 s: unit 361 s, unit-slow 1,565 s).
- `just test-tsan`: 82 `[parallel]` cases, then the contract suite
  against the TSan engine, clean (no warnings).
- `just oracle --tier fast`: 19/19 on all threads and with `--threads 1`
  (nothing else moved); `just oracle-roundtrip`: 511/511 byte-identical;
  `just oracle-imports`: 2059/2059.
- Front end, three times each in sequence: `just web-check` clean;
  `just web-test` 323/323 (release engine); `just web-e2e` 73/73 (6.2 to
  6.3 min; 3 new tests, no screenshots changed). `just web-dist --dir` and
  `just web-smoke` not run: no new resource or dependency.
- `just check`: not run (LLVM is not installed).

## Differences from the original, and why

1. **Budgets of cells** for imports and conversions (2^27 cells
   together): a few bytes of a crafted file asked the original for
   gigabytes (decision 0017).
2. **The conversion is refused while solving**: the original replaced
   the puzzle under its solver thread.
3. **An import replaces the document while a solve runs** (its solves
   pause), as `document.open` does; the original refused ("Stop solving
   process first!"). The front end asks about unsaved changes first.
4. **Export ▸ Puzzlecad is greyed without a shape with cells**, as
   Export ▸ STL: the original enabled it with any shape and then said
   "Could not export puzzlecad file.".
5. **The export's save dialog proposes "<puzzle>.scad"** beside the
   puzzle; the original proposed the puzzle's own path, and a name
   accepted as proposed became "Pelikan.xmpuzzle.scad".
6. **The convert dialog says what Continue does** (the puzzle replaced,
   undo afresh, Save over its file): the original said nothing.
7. **The imported puzzle is shown by its file's name** (without the
   extension) rather than "untitled", so Save As proposes that name.

## Found in the original (worth fixing there)

- `cb_Convert` does not check `threadStopped()`: converting while a solve
  runs deletes the puzzle the solver thread works on.
- The PuzzleSolver3D loader reads a Windows line end's carriage return as
  a filled cell (the row's character past the last layer is ignored, but
  a short row's `\r` lands in a cell).
- No limit on the cells a PuzzleSolver3D or Puzzlecad file makes: a
  `PIECE 500x500x500` line and 500 empty rows (about 520 bytes) allocate
  125 MB, and the file may repeat them.
- `cb_Export_Scad` proposes the puzzle's file as the export's name, so a
  name accepted as proposed becomes "name.xmpuzzle.scad".

## Known issues and notes

- `just check`: not run (LLVM is not installed).
- The original's MagellanTIC.scad and HoleyTIC.scad are read in place by
  `oracle.py imports` and not copied into the repository.
- A dropped .puz or .scad file is not imported (the original did not
  either): drops open puzzles and paused solves only.

## Brief for part 7

Part 7 of docs/DEFERRED-PLAN.md, the Python bindings (Opus 5.5 medium, or
Sonnet 5.5 high): a `python/` module on pybind11 (FetchContent, pinned),
built when `BT_PYTHON=ON` or when Python and pybind11 are found, binding
the new core's public API with the original module's names
(`src/python/`: `Puzzle`, `Shape`, `Problem`, `Solution`, `Placement`,
`SolutionIterator`; `load`, `save`, `add_shape`, `add_problem`, the cell
and problem accessors, `solve` with its iterator), and sliding and
stacking where the core exposes them. Port `test/python/test_burrtools.py`
(8 tests) and add tests for what is new; a `just python-test` recipe; a
wheel built and installed into a fresh venv (documented in the module's
README); `just test-all` runs the Python tests when the module is built.
The bindings could also reach this part's `io::readPuzzleSolver3D`,
`readPuzzlecad`, `writePuzzlecad` and `convertGrid`, which the original's
module did not have (decide; say so in the handoff).

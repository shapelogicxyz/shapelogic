# 0017: The imports, the Puzzlecad export and grid conversion

**Status:** accepted (deferred part 6); `core/shapelogic/io/imports.h`
(`ps3d_file.cpp`, `scad_file.cpp`), `core/shapelogic/model/grid_convert.h`,
`core/shapelogic/app/document.h` (`importFile`, `importBytes`, `convertGrid`,
`exportPuzzlecad`), `engine/methods_document.cpp`, `api/document.json`,
`api/engine.json` (`grids[].convertsTo`),
`web/src/renderer/shell/imports.ts`.

## Context
The original read two other programs' files into a new puzzle (File ▸
Import ▸ PuzzleSolver3D, `ps3dloader.cpp`; Puzzlecad (OpenSCAD),
`scadloader.cpp`), wrote a problem's pieces as a puzzlecad file (File ▸
Export ▸ Puzzlecad, `saveOpenScadPuzzle`), and converted a cube-grid
puzzle to the rhombic or the tetra-octa grid (Edit ▸ Convert brick grid
type to other, `converter.cpp`, `convertwindow.cpp`). Each import made a
cube-grid puzzle with one problem and replaced the open one without a
file name, so Save asked where; the conversion replaced the puzzle in
place, its undo history gone, its file name kept. The new core had none
of it: the menu items were greyed.

## Decision
**Port the three files line for line** into the core: the readers and
the writer in `bt/io/imports.h`, the converter in
`bt/model/grid_convert.h`; the document makes documents of them. Their
oracle is `oracle.py imports`: both programs import the committed inputs
(`tests/data/imports/`, made for the loaders' corners) and the
original's Puzzlecad files, and convert, export and re-import every
corpus puzzle, and a PuzzleSolver3D file is made of each cube-grid
puzzle's shapes; the saves must be the same bytes. `record-imports`
keeps the original's results for the unit tests.

**An import is a new document without a path, with unsaved changes**
(`document.import {format, path | data}`), as the original's
`setFileName("")` and `changed = true`: Save never writes over the other
program's file. The file is read as it is (`io::readPlainFile`), as the
original's `std::ifstream`; a gzip-compressed file is not decompressed.
The front end shows it by the file's name without its extension, so Save
As proposes "name.xmpuzzle". Like `document.open`, an import replaces
the open document whatever runs (its solves pause): the front end asks
about unsaved changes first; the original refused while solving.

**Budgets of cells the original lacked.** A PuzzleSolver3D size line of a
few bytes made a shape of up to 500x500x500 cells, a short Puzzlecad file
256x256x256 cells a piece, and a conversion makes 125 rhombic cells of
each cube: a crafted file, or a large cube-grid shape, asked for
gigabytes, which gets the engine killed on macOS (the lesson of the part
3 audit). An import's shapes may hold 2^27 cells together
(`kMaxImportCells`: one shape of the largest size the PuzzleSolver3D
format allows), checked before each shape is made; a conversion's the
same (`kMaxConvertedCells`). Past them the import is refused
(`parse_error`) and the conversion too (`invalid_argument`). No real
puzzle comes near: the corpus's largest conversion makes under a million
cells.

**The conversion replaces the document, keeps its path and is refused
while solving** (`document.convertGrid {grid}`). The document's grid
never changes (a premise of the undo history, decision 0011), so, as the
original, the converted puzzle is a new document: unsaved changes,
nothing to undo, the problems' definitions copied without their
solutions. The path stays, so Save writes the converted puzzle over the
file it came from, as the original kept its file name; the dialog says
so. The original did not check its solver thread here and replaced the
puzzle under it; the new core refuses while anything runs
(`invalid_state`, the front end's "pause first", the item greyed). The
grids a puzzle converts to are a fact of the engine (`engine.info`'s
`grids[].convertsTo`), so the dialog's choices are not computed by the
client.

**The Puzzlecad export answers text** (`document.exportPuzzlecad
{problem?, name?}`); the client writes it through the main process to
the path its save dialog gave, as the other exports. The header names
the document's file, or the export's (`name`) when the document has none,
as the original's `fname.empty() ? f2 : fname`; `name` refuses control
characters, since it goes into a comment line. The problem is the one
the Puzzle workspace shows (the original's problem selector), and no
problem means every shape, the result too, as the original's index past
the last. The export reads its problem through `readProblem`, so it is
allowed while solving, as the original allowed it. The client asks the
engine before the save dialog, so a refusal ("Puzzlecad export is only
available for cube-grid puzzles.", "Nothing to export.") comes first, as
the original's checks did; the item is greyed without a shape with cells
(the original enabled it with any shape and then could not export).

## Consequences
- The menus have no greyed items left from the deferred plan but the
  anaglyph modes, which the plan does not cover.
- `document.convertGrid` is the one call that replaces the document
  while keeping its path; a client must treat its reply as a new
  document (new ids), as after `document.open`.
- A file larger than 256 MiB is refused before it is parsed, as
  `document.open` refuses a gzip stream inflating past it.

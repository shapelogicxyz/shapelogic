# 0048: Variable voxels that cannot stay empty are reduced as filled ones

**Status:** accepted; `core/shapelogic/solve/dlx_matrix.cpp`
(`DlxMatrix::reduce`), `core/shapelogic/solve/assembler_1.cpp`
(`Assembler1::createMatrix`, `prepare`, `reduce`, `savePosition`,
`restorePosition`, `positionKeepsHoleColumns`, `kFilledVariables`),
`core/shapelogic/solve/assembler.h` (`MatrixOptions::variableHoleColumns`),
`core/shapelogic/solve/solve_session.cpp` (`SolveSession::prepareSearch`),
`cli/options.cpp` (`--hole-columns`)

## Context
The user drew a 65 x 65 tray with variable cells and nine rectangles that
fill it exactly (33,530 placements). Optimize took minutes a piece, 11.6
minutes in all with the release build, where the same tray drawn with
normal cells took 26 seconds; both found the same 692 placements worth
keeping.

A result's variable voxels may stay empty, and reduce()'s quick first
sweep (a voxel that every row of a column fills is no other row's) only
went through columns that must be filled. On the variable tray everything
was left to the placement check after it, which places each row against
the whole matrix. But when the pieces have exactly as many cells as the
result, no voxel can stay empty: the variable ones are filled exactly
once, as the filled ones are.

The two assemblers stood differently:
- **Assembler 0** keeps the variable voxels in a ring of their own, and
  its placement check counts the ones left without a row against the
  holes allowed. The check found everything; it was only slow.
- **Assembler 1** has them as columns with a minimum of 0, and its
  reduce() does not count holes at all (only the search does,
  `tooManyHoles`). A 65 x 65 variable tray of nine pieces in three shapes
  (7,569 placements) took 184 s, of which reduce took nearly all, to
  remove 88 placements; the search then went through 280,166 steps.

## Decision
**Assembler 0: the sweep goes through the variable voxels too when no
hole is to spare** (`holes == 0`). Each row it removes is a row the
placement check removes as well (placed, it leaves the swept column
without a row), and that check runs until nothing more goes, so the
reduced matrix is the one it was: the same rows, the same columns, the
same search and the same saved positions. The sweep also stops a column
as soon as its rows have nothing in common (the first and the last row
come first: away from the walls they share the swept voxel alone) and
tells a row of the column by a mark, not by walking the row.

**Assembler 1: such voxels are built as filled ones** (`variableFilled_`:
`holes_ == 0` and the result has variable voxels). The nodes and columns
are the same; the columns have a minimum of 1 and are no hole columns.
reduce() then removes what it removes on a filled result, and the search
has no holes to count. The assemblies are the same; the matrix reduce()
leaves is smaller than before, so the search takes other steps. Its sweep
got the two speed-ups above, which change nothing it removes.

**Saved positions of assembler 1.** A position lists rows of its matrix,
the rows each frame hid among them, and a row the smaller matrix no
longer has cannot be hidden again. So:
- A position saved on the matrix with the voxels filled has 8 added to
  its leading flag (`kFilledVariables`). The versions up to 2.0 refuse a
  flag above 6 (`CannotRestoreSyntax`, and the position is kept, decision
  0041); replaying it on their larger matrix would leave rows in that
  overlap the rows already placed. Positions of every other problem are
  written as before.
- **A position saved with hole columns is carried on on such a matrix**
  (decision 0043: a paused solve is carried on as it ran).
  `SolveSession::prepareSearch` reads the flag of the position the problem
  holds (`Assembler1::positionKeepsHoleColumns`) and builds the matrix
  with `MatrixOptions::variableHoleColumns`; the solve then reduces and
  searches as the version that saved it did, slow reduce included.
- `restorePosition` refuses a position of the other kind
  (`CannotRestoreVersion`), whoever built the matrix.

`--hole-columns` builds assembler 1's matrix the old way in the CLI, to
compare (the answers are the same).

## Measured (release build, one thread)
| Puzzle | Before | After |
|---|---|---|
| 65 x 65 variable tray, nine rectangles (assembler 0) | 696 s | 4.4 s |
| the same tray of normal cells | 26 s | 3 s |
| 65 x 65 variable tray, nine pieces in three shapes (assembler 1) | 184 s | 0.2 s |
| the same tray of normal cells | 3.7 s | 0.2 s |
| 8 x 8 variable tray, sixteen L-tetrominoes (assembler 1; 17,898 assemblies) | 275 s | 0.14 s |

## What was not done
- **A result with holes to spare is reduced as before.** With a staircase
  of ten cells cut off each corner of the same nine rectangles (360 cells
  of the tray stay empty) no voxel has to be filled and the sweep finds
  nothing. The placement check is back to about 25 placements a second;
  its first pass took 17 minutes and removed 11,572 of the 33,530
  placements, and the search after it does not get anywhere (0.0% after a
  minute, on either assembler). The puzzle has packings: a throwaway
  search that branches on the first undecided cell (a piece starts there,
  or the cell stays empty) and counts the cells no remaining piece can
  reach from bitmaps of the free cells found the first in 17 seconds.
  That is the work this case wants: a cheaper count of the cells a
  placement leaves no piece for, and a search that branches on cells.
- Assembler 1's reduce() still does not count holes when some are
  allowed.

# 0027: A sliding problem's starts and goals are its own

**Status:** accepted; `core/shapelogic/model/slide_marks.h` (`SlideMarks`),
`core/shapelogic/model/problem.h` (`slideMarks`, `editSlideMarks`),
`core/shapelogic/model/sliding.h` (`adoptBoardMarks`, `BoardEdit`,
`changeBoard`), `core/shapelogic/io/puzzle_file.cpp` (`<slideMarks>`),
`core/shapelogic/app/document_sliding.cpp`, `engine/methods_sliding.cpp`,
`api/sliding.json`

## Context
The starts and goals of a sliding puzzle were kept on the board: a start
mark and a goal mark per cell of the start/goal shape (decision 0021). A
problem only named the board as its result, so two problems on one board
were one layout: a copy of a problem (which shares its board) changed the
first problem's starts and goals whenever its own were edited. The user
wants a board used by several problems, each with a layout of its own.

## Decision
**The marks belong to the problem.** `Problem` holds a `SlideMarks`: per
cell of its result board, the piece that starts there and the piece whose
goal it is (a mark is the piece's shape index + 1, as before). Every rule
that read the board's marks takes the board (its cells: floor, walls,
corridors) and one problem's marks: the stamps, `startGoalError`, the
start locks of the assembler, the slide search and its goal check, the
saved search's key. A board is only a shape; copying a problem copies its
marks, and the copy is edited apart from the first.

**A board no problem uses has no starts or goals.** Placing a stamp on it
is refused ("No problem uses this board: choose it as a problem's result
first."). A duplicated board is an empty one until a problem is put on
it; a problem that loses its result loses its marks.

**The calls name the problem.** `sliding.place`, `sliding.clearAt`,
`sliding.clone`, `sliding.preview` and `sliding.tray` take `problem`; left
out, it is the first problem on the board. A change to one problem's
starts and goals reports that problem alone as changed.

**An edited board takes every problem's starts and goals along.** Before
an edit of a board's cells (the cell tools, a resize, the shape tasks:
nudge, rotate, mirror, minimise, scale), each marked cell is tagged
through the voxel's mark bytes, which every voxel operation already
carries; after it, the tags say where each cell went
(`sliding::beginBoardEdit`, `readBoardTags`, `applyBoardEdit`). Per
problem, a stamp whose cells all arrived, still as one copy of its piece
on floor, is kept where it now lies; one that lost a cell (erased, cut
off by a smaller board, made a wall) is removed whole, and the notice
names the pieces ("The board changed under S2: the starts and goals that
lost a cell were removed."). A growing board, or an edit beside the
stamps, changes nothing in any problem.

**A problem given another board keeps what fits.** `problem.setResult`
on a sliding puzzle carries the stamps over by their coordinates
(`sliding::changeBoard`): each that lies wholly on the new board's floor
stays, the rest are removed and named in the notice.

**The voxel's mark bytes stay as carriers.** `Voxel::getStartPiece` and
`getGoalPiece` remain for two uses only: the loader reads a file's
`start=` and `goal=` into them, and a board edit tags cells through them.
Outside a load or an edit they are empty on every shape.

**The file.** A problem writes `<slideMarks start="…" goal="…"/>` (the
lists as the voxel's were, without the zeros at the end), and the root
element has `slideMarks="problem"`; `<voxel>` no longer writes `start` or
`goal`. A file without the root attribute is from before (decision 0021,
or the original's by way of `convertLegacyStarts`): its boards' marks are
given to every problem whose result is that board and taken off the
shapes (`sliding::adoptBoardMarks`), so such a file opens as it solved
before, its problems sharing a layout until one is edited. Marks of a
board with no problem are dropped. The original program and versions
before this one do not read the new element; only reading old files is
kept (as decision 0021 chose).

## Consequences
- The older model without start/goal shapes (a tray and a goal shape
  painted in the pieces' colours) has no marks and is untouched.
- A saved slide search's key names the problem's marks, so a search saved
  before this is not carried on.
- `problem.duplicate` on a sliding puzzle gives a problem that can be
  changed freely: the bug this started from.
- The syncing of parts (`syncSlidingParts`) reads each problem's own
  marks: a piece stamped in one problem is no longer made a part of the
  others on the board.

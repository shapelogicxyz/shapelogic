# 0021: Sliding start marks apart from the colors

**Status:** accepted; the marks moved from the start/goal shape to each
problem later (decision 0027: the voxel's bytes are carriers only, and the
file writes `<slideMarks>` in the problem); `core/shapelogic/grid/voxel.h` (`startPiece_`),
`core/shapelogic/model/sliding.h`, `core/shapelogic/io/puzzle_file.cpp`,
`core/shapelogic/solve/assembler.h` (`MatrixOptions::startLocks`),
`core/shapelogic/solve/placement_finder.h`

## Context
The original kept a sliding puzzle's start marks in the cells' colors: a
tray cell's color was the index + 1 of the piece that starts there, and
before a solve every stamped piece was painted in that number (the "start
locks") so that the assembler, matching colors strictly, placed it only
on its start. Decision 0014 kept that, and with it what followed: no
color restrictions on sliding puzzles (the color group hidden, deleting
a color refused), at most 63 stamped pieces (a color is six bits), and
the pieces' colors rewritten by every solve. The user wants color
restrictions on sliding puzzles as on every other grid.

## Decision
**Start marks have a place of their own.** `Voxel` keeps a start mark per
cell beside the goal mark (`startPiece_`, `getStartPiece`,
`setStartPiece`), carried through every operation that carries goal marks
and dropped with the cell. The file writes them as `start="…"` on
`<voxel>`, beside `goal="…"`. The cells' colors are the user's
restriction colors again, and the mark limit is a byte (255 pieces).

**The assembler holds the pieces to their starts without colors.**
`MatrixOptions::startLocks` (with `freeStarts`, `sliding::freeStartParts`)
tells the placement finder to allow a placement only when it lies wholly
on its own piece's start cells, or, for a part with starts for fewer than
all its copies (or none), wholly on cells that are no start: the rule the
colors encoded, per placement instead of per cell pair. The color rules
apply besides, as on every grid; strict colors play no part in a sliding
solve. Nothing paints the pieces: `refreshStartLocks` is gone, and
`prepareForSolving` sets only the maximum of holes.

**Old files convert as they load.** A sliding file in which no shape has a
`start` attribute is the original's (or ours from before): its start/goal
shapes' cell colors become start marks and are cleared, the pieces lose
their lock colors, and the palette goes unless a problem has color rules
of its own (`sliding::convertLegacyStarts`). Files we write are not meant
for the original program: it does not know the attribute, and the user
asked only that the original's files load.

## Consequences
- The document allows deleting colors and setting color rules on
  sliding puzzles; the front end shows the palette, the Paint tool and the
  color-rules matrix as on other grids.
- `tests/data/model_dump.txt` and `app_dump.txt` record the sliding
  fixtures without their lock colors; the byte-for-byte save test skips
  sliding fixtures (their saves are the new form). The oracle's roundtrip
  tier cannot compare sliding files any more; its summary and solved-file
  comparisons still match, since the solve is the same. Its text
  comparison maps the original's drawing of the shapes, which shows the
  marks as colors, onto ours (`marks_as_shapelogic_draws` in
  `tests/oracle/oracle.py`).
- A saved slide search's key (`sliding_search.cpp`, `problemKeyOf`) names
  the start marks, so searches saved before this are not carried on.

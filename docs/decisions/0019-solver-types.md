# 0019: Two solver types, and their names

**Status:** accepted (rename to ShapeLogic)

## Context
The original had three solver types: BurrTools Classic, Andrew Crowell's
take-apart and BurrTools 2, which assembled with dancing cells split over
threads and took apart as Classic did. BurrTools 2 saved a position that
could not be carried on (decision 0009), which made a paused solve of it a
special case in the session, the document's guards (decision 0011) and the
front end.

## Decision
- ShapeLogic has two solver types: `classic`, labelled **Complete
  (ShapeLogic)**, and `crowell`, labelled **Fast (Crowell)**.
- The BurrTools 2 solver is removed with its code (`assembler_bt2`,
  `dancing_cells`, `work_stealing`, the paused-solve guards and their tests),
  and the engine's `bt2` solver value with it.
- The old names are still understood by `solverTypeFromName` ("BurrTools
  Classic", "Andrew Crowell") so that stored settings and scripts keep
  working; "bt2" is not.

Decisions 0009, 0010 and 0011 describe BurrTools 2 as it was ported; where
they differ from this one, this one holds.

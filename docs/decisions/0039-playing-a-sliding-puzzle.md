# 0039: A sliding puzzle played by hand

**Status:** accepted; `core/shapelogic/solve/sliding_play.h` (`playStart`,
`playReach`, `playReplay`, `PlayRules`; defined at the end of
`sliding_search.cpp`), `core/shapelogic/model/sliding.h` (`layoutPieces`,
`layoutCells`, `startLayout`, `layoutRefusal`, `layoutMarks`, `setLayout`),
`core/shapelogic/app/document_sliding.cpp` (`Document::setSlideLayout`),
`api/sliding.json` (`sliding.playStart`, `sliding.playReach`,
`sliding.playReplay`, `sliding.setLayout`), `web/src/renderer/play/`
(`workspace.ts`, `board.ts`, `game.ts`, `follow.ts`, `moves-file.ts`),
`web/src/renderer/shell/layout.ts` (`setWorkspaceShown`) and `keys.ts`
(the Play rows), `web/src/main/app-ipc.ts` (`KINDS.moves`, `.slmoves`)

## Context
A sliding problem with a Start could only be solved by the search. The
user asked to play one by hand: a fourth tab, Play, to the right of
Solver; a problem picked at the top; its pieces on the tray, dragged in
2D; "Solved!" when a Goal is defined and reached; the moves played back,
exported and imported by someone else; the moves counted as sliding
puzzles count them (one full movement, round a corner too, is one move);
Undo and Redo clearly visible next to the board; and a button to save the
board's position as the problem's Start or Goal. The original program has
nothing like it.

Decided with the user: a piece dragged again straight after its own move
goes on with that move (the solver's count, so a game's total compares
with the fewest moves the solver finds); the Play tab is shown only while
a sliding puzzle is open; Save position as Goal gives every piece a goal
where it is.

## Decision
**The engine answers; the page keeps the game.** The rules are the slide
search's own code, asked of one position at a time, and the engine keeps
no game:

- `sliding.playStart`: the board, the pieces where the starts put them
  (in the order a solve numbers them), the goals, or why the problem
  cannot be played (no board, a piece without a start for every copy).
- `sliding.playReach`: everywhere one move can take a piece
  (`reachableShifts`), for the piece alone and, with nested slides, for
  the piece with what is nested in it (the search's mover sets), each
  stop marked when it gives the goal (`GoalCheck`; never without goals).
- `sliding.playReplay`: a list of moves made from a position, each with
  the corner points of its route (`fewestTurns`, as a solution's moves are
  drawn) and whether its position is the goal; it stops at the first move
  that cannot be made. It checks a file's moves, gives the playback its
  routes, and checks a game again after the document changed.
- `sliding.setLayout`: a position as the problem's starts or goals, one
  undo step, refused while solving and for a goal on a corridor; nothing
  changes, and nothing found is lost, when that side is the position
  already.

A session in the engine (as the explorers have) was not taken: a game has
no state the engine needs between calls, and a stateless call cannot go
stale when the document changes.

**A position is a corner for each piece**: the lowest corner of its cells
on the board. Not the hotspot's cell an assembly has: a solve's start
moves hotspots (`prepareShapesForSolving`), which would shift the pieces
under a game. The rules the problem does not fix (nested slides, partial
nesting, vertical moves) are params: the engine does not read a problem's
stored Solver settings, the page does (`readOptions`).

**The game is a value in the page** (`game.ts`): the start, the moves
(`{movers, by}`) and the place in them shown. One place serves Undo, Redo
and the playback. A move made from a place before the end drops the moves
after it. A move merges with the one before it when the same pieces move
(`canMerge`), and a merged move of no distance is taken off. Games are
kept for each problem while the document is open and are not written to
the `.xmpuzzle`: the moves file is how a game is kept or shared. After a
change of the document the start is read again: a game of the same start
is replayed and goes on (or starts anew when a move can no longer be
made); any other start is a new game, which is also how Save position as
Start begins one.

**The board** (`board.ts`) is an SVG drawing of one layer, made for whole
pieces that move (the 2D grid paints cells and has no piece to move). At
the press the page asks `sliding.playReach` once; the drag then walks the
stops a step at a time towards the pointer (`follow.ts`), so a piece
slides along what is in its way and never jumps it. With nested slides
the first step chooses the group: the piece alone when it can step that
way, else with what it carries; Shift chooses the carrying group first.

**Ctrl+Z and Shift+Ctrl+Z are the game's on the Play tab**, never the
document's: a document undo from there would take back an edit out of
sight. Save position as Start or Goal is a document step and is undone
from another tab.

**The moves file** (`.slmoves`, JSON, `moves-file.ts`): format and
version, the puzzle's and problem's names (for the reader), the board, the
rules, the pieces at their start and the moves. A file is taken only for
the same board and the same start, its pieces matched by their cells and
places whatever their order, and only when the engine can make every
move; otherwise the user is told which move fails and why. Export goes
through the save dialog as the other exports do; Import reads the file
chosen in the page.

## Consequences
- A piece stays in its layer when played by hand: the API carries
  `verticalMoves`, the page sends it off, and a deeper board is shown a
  layer at a time. A problem that needs vertical moves cannot be played
  to its goal yet.
- With partial nesting, Shift cannot choose the wider group while the
  wholly nested group can step: the groups are tried in order.
- Play is allowed while a solve runs (the queries change nothing); the
  Save buttons then say to pause first.
- Saving a position removes what the problem had found, as every change
  of its starts or goals does: the page asks first when there is
  something to lose.
- Optimized paths play no part: they narrow a search, not the moves there
  are, so a game may rest a piece where the search would not stop it.
- A problem whose pieces do not all have a start is not played; the tab
  says which piece is missing one and offers the Puzzle tab.
- The website's demo has the tab (it is the same renderer); its Export
  does nothing there, as the demo has no save dialog.

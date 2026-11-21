# 0042: A stacking puzzle played by hand

**Status:** accepted; `core/shapelogic/model/stacking_play.h` (`playStart`,
`playMoves`, `playReplay`, `positionRefusal`, `moveRefusal`, `positionMap`;
defined at the end of `stacking.cpp`),
`core/shapelogic/app/document_stacking.cpp` (`Document::setStacks`),
`api/stacking.json` (`stacking.playStart`, `stacking.playMoves`,
`stacking.playReplay`, `stacking.setStacks`), `web/src/renderer/play/`
(`kind.ts`, `workspace.ts`, `sliding.ts`, `stacking.ts`, `stack-board.ts`,
`stack-look.ts`, `stack-game.ts`, `stack-moves-file.ts`, `room.ts`)

## Context
Decision 0039 gave sliding puzzles a Play tab. The user asked for the same
for stacking puzzles (Tower of Hanoi, Panex and their relatives): a problem
picked at the top, its discs on the rods, moved by hand; "Solved!" at the
Goal; the moves counted, undone, redone, played back, exported and
imported; and the position saved as the problem's Start or Goal. The
original program has nothing like it.

## Decision
**The engine answers; the page keeps the game**, as for a sliding puzzle,
and the engine keeps no game:

- `stacking.playStart`: the rods, the discs numbered as a solve numbers
  them, the start's stacking with where each disc rests, the goal's, or
  why the problem cannot be played.
- `stacking.playMoves`: every transfer a position allows, each with where
  the disc comes to rest and whether it gives the goal. One call answers
  for the whole position (a board has seventeen rods at most), so a press
  on any rod needs no second one.
- `stacking.playReplay`: a list of transfers made from a position, to the
  first the rules refuse, with the reason as a sentence. It checks a
  file's moves, checks a game again after the document changed, and words
  a refusal: the page asks it for the one transfer a player tried.
- `stacking.setStacks`: a position as the problem's start or goal, one
  undo step, refused while the problem is solved and when it is no
  position of all the discs; nothing changes, and nothing found is lost,
  when that side is the position already.

**The rules are the Stacking Solver's.** A transfer is allowed when
`stacking::moveAllowed` allows it, the function `findStackPath` searches
with (the Panex search keeps the same rules, and the tests compare the
two). `moveRefusal` only words a refusal: it asks `moveAllowed` first and
names the rule in its order. A position is the goal when it is the goal's
stacking disc for disc, as both searches end: copies of one disc are told
apart, as the solvers tell them.

**A problem is played once its start is ready, the goal apart.**
`setupError` judges the start and the goal together, so a problem with an
empty goal could not be played. `playStart` asks of the start alone that
it is a position of all the discs under the board's rules; the goal is
played to only when `setupError` is empty, the condition the searches
have. Without it the game counts moves and is never solved, and a goal
that holds discs but is not ready says why (`goalError`).

**A position is each rod's discs by their numbers**, bottom to top; a
move is two rods. Where a disc rests is the engine's answer too: in a
Panex column a disc rests where its size lets it fall to, which the page
does not work out. `playStart`, `playMoves` and `playReplay` give the
height with each disc, and the page's game keeps it with each transfer
(a disc's height does not change while discs come and go above it).

**The Play workspace is shared, the kind of puzzle apart** (`kind.ts`).
`workspace.ts` keeps what every game has: the problems, each problem's
game, Undo and Redo and the playback over one place in the moves, Reset,
Restart, Save position as Start and as Goal, the moves file's dialogs and
the keys. A kind reads a problem's start, draws its board, makes the
moves on it, draws a move again and writes and reads its moves:
`sliding.ts` is decision 0039's code moved, `stacking.ts` is new. The tab
is in the rail while a puzzle of either kind is open.

**Every transfer is one move**, as the solver counts them, so a game's
total compares with the fewest the solver finds. The sliding rule that
the same piece moved again goes on with its move has no place here: a
disc moved twice is two transfers to the solver. Decided here, by the
sliding rule's other half: a disc put straight back on the rod it came
from takes its move off, as a sliding piece put back where its move began
does.

**The board** (`stack-board.ts`, its drawing `stack-look.ts`) is an SVG
drawing of the rods from the front, each disc a bar as wide as its size with its name or, without one,
its size. A Panex column has behind it the steps a disc of each size
rests on and, across the top, the bridge where a raised disc waits. A
press anywhere in a rod's column takes its top disc: dragged, it follows
the pointer and goes onto the rod it is let go over; clicked, it lifts and
goes onto the rod clicked next. The rods it may go to are marked from the
engine's answer. A transfer the rules refuse puts the disc back and tells
the rule in the status bar. A move made again is drawn as a solution's
transfer is: up over the rods, across, down.

**The goal is a small picture beside the board**, not marks on the board:
a goal's discs often stand where other discs are (the Panex swap), where
outlines over them would be read as discs. Hide Goal takes it off; Hide
Disc Labels the labels.

**The moves file** keeps the extension and the format name, with
`kind: "stacking"` and version 2 (`stack-moves-file.ts`): a sliding
puzzle's files stay version 1, and a program that knows only those says
of a stacking file that it is newer than the program. It holds the discs
each rod takes, Panex columns or not, the start as each rod's disc sizes
and the transfers by rod. A file is taken only for the same rods and the
same start, and only when the engine can make every transfer; each reader
tells a file of the other kind what it is for.

## Consequences
- The website's demo has the tab once it is rebuilt (`just site-demo`);
  its Export does nothing there, as for a sliding puzzle.
- Play is allowed while a solve runs; the Save buttons then say to pause
  first, although the engine would refuse only a solve of that problem:
  the page's rule for every edit.
- Saving a position removes the problem's solutions, as an emptied
  stacking does; a search of the old start or goal saved on disk stays in
  its folder until the cache is cleared, since its name is the old
  problem's.
- A game is kept for each problem while the document is open, by its
  rods, discs and start: a change of the rules keeps the game when every
  transfer can still be made, and starts it anew otherwise.
- The outside channel is not drawn; its moves are allowed and marked like
  any other.
- A board is drawn for the largest disc's width: a puzzle with one very
  large disc among small ones has thin columns.

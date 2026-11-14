# 0031: Loose stops: the stops of a grid cell searched as one

**Status:** accepted; `core/shapelogic/solve/sliding_search.h`
(`SlideSearch::grid`, `gridStopsApart`, `looseStops`,
`SlidingOptions::gridStopsApart`), `core/shapelogic/solve/sliding_search.cpp`
(`buildLoose`, `SlideMover::expandLoose`, `SlideSpace::covers`,
`LevelSearch::dropCovered`, `restoreVisited`, `placeChain`), `cli/`
(`--slide-stops-apart`)

## Context
Optimized paths (decision 0029) hold every piece to its offsets on the grid
its board fits. A block has one stop in a cell of the grid. A small piece
has several: one for each hole of a block it could sit in, and those it
starts and ends at. On the user's "Abe 1 to 6 Heart" a small piece that
moves into a cell stops at three or four places there, and each is an
arrangement of its own. The user saw that in the solve sequence and asked
whether the search needs them all: in a cell with no other piece the stops
differ only in which hole the piece would fit, which nothing asks until a
block arrives, and in a cell with a hollow block only the stop inside the
block exists.

The second half the search already did (the other stops are on the block's
cells). The first half is most of what the search holds: with three small
pieces each at one of three stops in empty cells, 27 arrangements are one
situation.

## Decision
**A class of stops is a position of its own.** The stops of a piece that lie
wholly inside one cell of the grid, two or more, are a class (`buildLoose`),
and the class gets a position number after the piece's real ones. A piece at
that number is *loose*: it is at any stop of the class, and the arrangement
stands for the arrangements with it at each. The class's mask is the cells
any of its stops covers, and its core those that all of them cover. Only a
piece with offsets on the grid and no hole of its own has classes (nothing
nests in a loose piece).

**Only with nested slides on a grid.** Loose arrangements make more of the
moves that cannot be made backwards, which are looked for in the levels
kept, and only nested slides keep them. So the search has loose stops when
`nested` and `grid` are set and `gridStopsApart` is not; otherwise, and
when the extra numbers do not fit the key, it tells every stop apart as
before. `SlideSearch::looseStops` says which it was. Then `visited` counts
arrangements that may each stand for several.

**The moves of a loose arrangement** (`expandLoose`) are every arrangement
one move from one of those it stands for, each of which stands only for
arrangements so reached:
- a piece that reaches every stop of a class is told once, loose;
- a piece that moves clear of the cells the loose pieces may cover is worked
  out once, whatever stops those are at;
- a piece that could touch such cells is worked out for each stop of the
  loose pieces it could touch, and where its move depends on them it is
  told with them in place (a block that arrives over a loose piece leaves
  it at the stop inside its hole);
- group moves the same way, with partial nesting too;
- an arrangement the rules cannot take apart (more than `kMaxPlaces` ways
  to place the loose pieces a move touches) is expanded the long way, from
  each arrangement it stands for.
A loose piece moves from any of its stops: they are one move apart, which
is what makes the arrangement one level of the search and the count of
moves exact.

**One-way moves.** A move is told one way when, made backwards, it does not
give the arrangement it came from: a piece that can reach every other stop
of the class it stands in, a move told with a loose piece put in place, and
the group moves that cannot be undone. Such an arrangement may be of any
earlier level, so it is looked for in all of them (the visited sets), not
only in the two newest. The start is in the visited sets now too.

**Covered arrangements are dropped.** A move that depended on a loose piece
leaves it in place, and when the mover leaves again the piece stays in
place, in an arrangement the search already has with the piece loose. Left
alone these are a second, lagging search of the board (on a board with
three small pieces in empty cells the search with loose stops visited more
arrangements than the one without). So each new level is passed through
`dropCovered`: for every arrangement, the arrangements that stand for it and
more (`SlideSpace::covers`: its pieces at a stop of a class made loose,
where no other piece lies on the class's other stops, one at a time and
together, at most six pieces) are looked up in the earlier levels and in
the new level itself, and the arrangement goes when one is there. Every
move from it is a move from that one.

**The path** is traced back through the loose arrangements (`stepBack`
tries the loose and in-place variants of what a move backwards gives, and
checks each by its moves) and then placed (`placeChain`): from the goal
backwards each loose piece gets the stop the next move needs. The path
shown has every piece at a real stop.

**Saved searches.** The fingerprint of a search with loose stops mixes
`kLooseFormat`; without them it is what it was. A search of the same start
saved without loose stops (before this, or with `gridStopsApart`) is
carried on as it was begun. A new search of that start has loose stops.

**The visited sets of a search carried on are restored.** A search saved
between two levels had a level still waiting to go into the visited sets,
which the saved state does not name, and searches saved before this never
had the start there. Carried on, the one-way moves that led into the
missing level were searched all over again: the right answer, with more
work. `restoreVisited` looks for each kept level by its first arrangement
and puts in those that are missing. This holds for every nested search,
loose or not.

`SlideSearch::gridStopsApart` (`SlidingOptions::gridStopsApart`,
`shapelogic_cli --slide-stops-apart`) is the A/B switch.

Since decision 0032 the extra numbers are tried in the wide key before the
loose stops are given up: "when the extra numbers do not fit the key" means
128 bits, not the 64 the stops apart would fit.

## Measurements
"Abe 1 to 6 Heart", nested slides, optimized paths (a 6-cell grid), release
build, one run each; arrangements summed over the levels:

| Problem | Moves | Stops apart | Loose stops | Time apart | Time loose |
|---|---|---|---|---|---|
| Mid 0 to Mid 1 | 14 | 82,456 | 37,602 | 0.05 s | 0.06 s |
| Mid 1 to Mid 2 | 14 | 177,100 | 64,853 | 0.07 s | 0.06 s |
| Mid to End | 16 | 215,063 | 87,073 | 0.10 s | 0.07 s |
| Start to Mid 0 | 31 | 3,115,279 | 991,526 | 0.92 s | 0.54 s |
| Start to End | 40 | 6,861,036 | 2,103,259 | 2.31 s | 1.25 s |

The moves are the same. A hand-made board searched to the end (two cups,
three unit pieces, no path) took 100,800 arrangements apart and 34,508
loose; without `dropCovered` it took 158,016.

## Consequences
- The same fewest moves on a grid in about a third of the arrangements and
  half the time, more where more small pieces are free at once.
- Tests: the hand-made board and random boards of blocks, cups, rings and
  small pieces compare the search with loose stops against the one that
  tells them apart (outcome, moves, every step a real move), with partial
  nesting and one to four threads; a search stopped and carried on visits
  what the whole one does; the hidden fuzz case compared 2,750 boards, with
  the legacy search as a third opinion.
- A full search and a grid search without nested slides are unchanged,
  answer for answer and arrangement for arrangement.
- An optimized solve paused before this is carried on with its stops told
  apart. Solving again from the start uses loose stops.
- Soft spots: `covers` looks at six pieces at most, so with more small
  pieces loose at once some covered arrangements stay (more work, the same
  answer). `dropCovered` looks every new arrangement up in every visited
  set, as the one-way filter does: a cost per level that grows with the
  depth, paid back by the smaller levels on the boards measured.

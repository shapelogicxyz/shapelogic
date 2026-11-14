# 0029: Optimized paths: a sliding search held to the grid its board fits

**Status:** accepted; `core/shapelogic/solve/sliding_search.h`
(`SlideSearch::grid`, `SlidingOptions::optimizedPaths` and `grid`,
`SlideGrid`, `slideGridFor`, `searchGrid`), `core/shapelogic/model/problem.h`
(`slideGrid`), `core/shapelogic/io/puzzle_file.cpp` (`slideGrid=`),
`core/shapelogic/solve/solve_session.cpp`, `engine/methods_sliding.cpp`
(`sliding.gridAdvice`), `api/types.json` (`SlidingOptions.optimizedPaths`,
`ProblemSummary.slideGrid`), `web/src/renderer/solver/workspace.ts`
(`askOptimizedPaths`, `offerFullSearch`), `cli/` (`--optimized-paths`,
`--slide-grid`)

## Context
One move of the slide search is a piece going anywhere it can reach, and
every cell it can stop on is an arrangement of its own (decision 0014).
Many sliding puzzles are boards of blocks: an 18x18 board split into a 3x3
grid of 6x6 cells, say, with blocks that fill one or two cells and small
pieces that sit in the blocks' holes and swap between them. Their solutions
move only whole cells, but the search also stops every block one to five
cells into an empty cell and every small piece anywhere it fits. Almost all
of what it holds is such part-way arrangements. On the user's "Abe 1 to 6
Heart" (ten pieces, nested slides) the 40-move problem took 8,852 s and
5.09 billion arrangements. With every stop held to the grid it took 2.3 s
and 6.9 million arrangements, and found 40 moves. The four smaller problems
of the file found their fewest moves on the grid too.

The coarse step the search had as an A/B switch (`step`, `--slide-step`)
let a piece stop only a multiple of the step from its own start. That held
the blocks to the grid but also the small pieces to their start offsets, so
they could never move between holes whose offsets differ: no path.

Stopping only on the grid is a restriction. A puzzle can need a part-way
stop, a block parked half into a cell to open a corridor, say. Then the
grid's path has more moves than the fewest, or it finds none where a full
search finds one. So it cannot be how every sliding puzzle is solved.

## Decision
**A grid with offsets for each piece.** `SlideSearch::grid` (cells a side)
replaces `step`. A piece may stop only where its place, x and y modulo the
grid, is one of its offsets:
- the offsets at which a piece of the same cells starts or fills a goal
  (so pieces that swap holes share them, and so do copies);
- and those at which it lies wholly inside another piece's outline, off its
  cells, while that piece is at one of its own (a hole that no piece starts
  in).
A piece with no offsets stops anywhere, so the grid never holds a piece
still. Pieces still pass through the cells between. A group moving with
nested slides stops only where every piece of it may stop. The table search
marks each position as a stop or not (`PosTable::rest`), and the legacy
search keeps to the same rule. A saved search's fingerprint mixes the grid
and the offsets. Without a grid it mixes what the step of 1 did, so the
searches saved before this are carried on.

**The grid is found from the board.** `slideGridFor(puzzle, problem)` picks
the largest size above 1 with all of these:
- it divides the floor's bounding box in x and y;
- at least one piece measures a multiple of it in x and y (a block);
- every such piece starts and fills its goals on the grid's lines;
- it leaves fewer stops.
It also counts the stops on the empty board with and without the grid,
summed over the pieces. A problem without start marks, or with no starts,
has no grid.

**Asked, or automatic.** `SlidingOptions::optimizedPaths` (the Solver's
**Automatically use optimized paths**, stored with the problem as
`optimizedPaths`; `bt --optimized-paths`; the Python module's
`optimized_paths`) holds each run's searches to the grid
`slideGridFor` finds (`searchGrid`, once a run). With no grid found, every
stop is searched. With the box off, Solve first calls `sliding.gridAdvice`,
and when the board fits a grid it asks whether to use optimized paths for
this solve. The dialog gives the grid and the stops it leaves, and warns
that the path may be longer and that "no solution" proves nothing. **Use
Optimized Paths** holds this solve, and its Continue, to the grid and leaves
the box as it was. **Full Search** searches every stop. Escape solves
nothing. The box sits above the Solver type. The settings dialog's Search
Options have it too, for new problems.

**The result says how it was found.** The problem keeps the grid with its
results (`Problem::slideGrid`, the file's `slideGrid=`, dropped with the
counts; `ProblemSummary.slideGrid`). The problems list says "40 moves
(optimized)" or "no solution (optimized)". When optimized paths find no
solution, the note says so and that a full search may still find one. The
Solver shows it with **Full Search**, which solves again with every stop.
There is no offer to confirm a found path is the shortest: that would take
as long as the full search does.

`SlidingOptions::grid` stays as the A/B switch (`bt --slide-grid N`): a
grid of that size whether or not the board fits it.

With nested slides the stops a small piece has inside one cell of the grid
are searched as one (loose stops, decision 0031): the same moves in fewer
arrangements.

The search's keys number only the stops the grid leaves a piece (decision
0032), so a board too big for the level search with every stop fits it with
optimized paths.

With nested slides and a goal position for every piece the search goes
from the start and from the goal until the two meet (decision 0033), with
the stops of a cell apart; otherwise from the start alone with loose
stops.

## Consequences
- A board of blocks solves in seconds where it took hours, at the risk of a
  longer path or a missed one, which the dialog and the result's label say.
- A puzzle whose board fits no grid, or a solve with the box off and the
  question answered **Full Search**, is searched as before, answer for
  answer. The oracle's sliding tier is unchanged.
- `ProblemSummary` and `ProblemDetail` have `slideGrid`, and the fake
  engine's recordings were made again.
- A paused optimized solve continued after the file is reopened, with the
  box off, goes on as a full search. Its saved search's fingerprint differs,
  so the start under way is searched again from the beginning.

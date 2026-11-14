# 0033: Nested slides searched from both ends

**Status:** accepted; `core/shapelogic/solve/sliding_search.cpp`
(`SlideMover::movesBack`, `floodShifts`, `canMoveBack`, `nestedBound`,
`kMaxBackNested`, `SlideSpace::backMoves`, `singleGoal` and `goalWays`,
`LevelSearch`'s `Side::visited` and `pending`, `kNestedMeetFrom`,
`kStateMagicBothNested`, `findSlidePath`'s order, `checkSlideMovesBack`),
`core/shapelogic/solve/sliding_search.h` (`SlideSearch::oneEnd`,
`SlidingOptions::oneEnd`, `SlideBackCheck`), `cli/` (`--slide-one-end`)

## Context
When the goal is one arrangement the level search goes on from the goal as
well and stops where the two ends meet: two searches half as deep, which on
a board whose levels grow by a steady factor is a small part of the
arrangements. With nested slides it did not. A piece that carries others
can close round a piece it passes, and that move cannot be made backwards
as it came, so the goal end, which made the same moves as the start end,
would have found paths that do not exist.

The user's "Abe 1 to 6 Heart" and "8 Heart" are all nested slides. Each
level there is about 1.14 to 1.2 times the one before. 6 Heart's problems
took 26 minutes to two and a half hours from the start alone, and one was
not solved. 8 Heart, which decision 0032 put on the level search, was 59
moves deep after 105 s with levels still growing.

Copies of a piece were a second reason for one end: a piece with more than
one goal position made the goal "more than one arrangement", and each copy
of a piece has every copy's goal.

## Decision
**The goal end makes the moves backwards.** `SlideMover::movesBack` gives,
for an arrangement, every arrangement it is one move from:
- A piece that moved alone goes back the way it came. That holds for a
  piece that closed round another on its own too: the forward search moves
  each piece alone and then with what it holds, so the way back is the
  piece alone.
- A group that arrived was its outer piece and *some* of what is nested in
  it now, since it may have closed round the rest. For each outer piece,
  every non-empty part of what it holds is slid away with it (`floodShifts`,
  the forward flood), the rest left in place, and an arrangement counts
  where that part is all the outer piece holds there (`undoable`). From
  there the group's forward move leads here.
- The move is one-way, in the sense the level search uses (the next
  paragraph), when the part is not everything held: no move of this
  arrangement leads back there.

**Each end looks for its one-way moves in its own older levels.** An
arrangement reached by a two-way move is new unless it is in the level at
hand or the one before; one reached by a one-way move may be in any older
level, so the search keeps the older levels merged in a few sorted sets and
looks there. Those sets and the level waiting to go into them are now a
part of each end (`Side::visited`, `Side::pending`), and `step` filters an
end's one-way arrangements against that end's sets. The meeting test is
unchanged: an arrangement of one end's new level found in the other end's
newest two.

**When it runs.** `canMoveBack` (kept as `SlideSpace::backMoves`): nested
slides, the stops apart, whole nesting only (not `partial`), and no piece
that could ever hold more than `kMaxBackNested` (6) others, by
`nestedBound`, a bound from the room in each piece's holes. The parts
double with each piece held. The goal must be one arrangement. Then the
goal end starts with the first level (`kNestedMeetFrom`): with nested
slides every level is kept anyway, and the levels are narrow for a long
way, so there is nothing to wait for. Without nested slides the goal end
starts as before, once a level holds a million arrangements. The loop
grows the narrower end, as it did.

**Both ends before loose stops.** A loose arrangement (decision 0031) has
no backward moves yet, and the two ends must hold the same arrangements to
meet. So a new search with nested slides on a grid tries the tables with
the stops apart and both ends first (`findSlidePath`: 64 bits, then 128);
where that search is not possible (`Tabled::NotThis`: the goal, partial
nesting, the bound) it is searched as before, loose stops from the start
alone. The stops apart cost up to about three times the arrangements a
level; halving the depth is worth far more on every board measured.

**Copies with a goal each are one goal.** `singleGoal` takes the copies of
a piece together: when their goal rows hold them in exactly one way
(`goalWays`, which counts to two and gives up after `kMaxGoalTries`), that
is the goal arrangement. This holds without nested slides too, so a puzzle
of like pieces with a goal each now meets in the middle once it grows
wide.

**The path.** From the meeting arrangement the start half is traced back
as before. The goal half is traced forwards: the arrangement a level
nearer the goal is one forward move away, so `stepBack` looks among the
forward moves there. Every step of a returned path is checked as a move by
the tests.

**Saved searches.** A search saved from both ends with nested slides names
the goal end's visited sets, under a state name of its own
(`kStateMagicBothNested`, `BTSLSV02`). Every other search is saved as it
was. A search saved from the start alone is carried on as it was begun:
with loose stops it stays one-ended; with the stops apart, or without a
grid, it takes up the goal end when it is carried on.

**What the Solver asks.** Every search with nested slides on a grid that
v1.8.1 or an earlier build paused is such a search with loose stops, and
carrying it on can cost hours where a new search takes seconds.
`savedFromStartAlone` answers, for a start, whether the search that would
be carried on was saved from the start alone with loose stops while a new
search would go from both ends: it names the searches as `findSlidePath`
does (`nameSpace`, `tableProbe`) and looks for their folders, searching
nothing. `pendingFromStartAlone` asks it of a paused problem's pending
starts, the engine's `sliding.resumeAdvice` of a problem and the options
Continue would send. Where it is true the Solver's Continue asks first:
Solve Again (the default) or Continue, the second only reasonable for a
search that has run long and is likely near its end. A search paused in
this build is saved from both ends and carried on so (6 Heart's Problem 3
paused after 5 s and carried on: 14.1 s in all against 13.3 s unbroken,
the same 16,058,861 arrangements), so pausing costs nothing and Pause asks
nothing.

**Solve throws the saved searches away.** A sliding solve that starts
afresh now drops the problem's saved searches (`SolveSession::launch`), as
the guide always said and as an abort did. Before, the call sat on the
stacking path only, so a saved search survived Solve and the search of its
start carried it on: after a pause, "solve again" went on one-ended.

`SlideSearch::oneEnd` (`SlidingOptions::oneEnd`, `shapelogic_cli
--slide-one-end`) is the A/B switch: a new search goes from the start
alone, nested or not. A search saved from both ends is carried on with
both.

## The goal end's filter, and what the boards showed
On 6 Heart, and on every generated board of the tests, the goal end's
one-way arrangements are never in its older levels (goal level 23 of
Problem 3: 31,405 reached, 31,405 new; start level 23: 17,546 reached,
1,207 new). That looked like a filter that did nothing, so a build that
kept every arrangement of each end in a set was run on Problem 3 and on
194 generated boards: no arrangement came up twice at either end. It is
how these boards are, not a fault. Going backwards, an arrangement before
a closing move is old only when undoing the closing takes two moves, which
needs a piece that already holds one and closes round another, near the
goal.

A hand-made board has it: a C with a unit in its pocket, a second unit
the two can close round, and a unit that is to end inside a closed ring,
so there is no path. The goal end runs out after 23,969 arrangements, four
of its one-way arrangements found in its older levels; from the start
alone the board is 410,400. Without the filter a search with no path
could go round such arrangements and never end (a found path would still
be the shortest).

## Measurements
Release build, MacBook Air M4 (ten threads), nested slides, optimized
paths, one run each. "Before" is release v1.8.1, from the start alone.

"Abe 1 to 6 Heart":

| Problem | Moves | Before | Now | Arrangements now | Peak memory |
|---|---|---|---|---|---|
| Problem 1 | 50 | not solved | 8.9 s | 28,444,092 | 489 MB |
| Problem 1 modified | 50 | 1,565 s | 5.1 s | 16,973,771 | 270 MB |
| Problem 2 | 56 | 9,172 s | 19.2 s | 55,003,437 | 991 MB |
| Problem 3 | 47 | 2,825 s | 6.1 s | 18,412,361 | 364 MB |

The moves are those of the solutions saved in the file.

The A/B on this build, Problem 3: from the start alone (`--slide-one-end`,
loose stops) 47 moves in 321 s, 398,941,366 arrangements over its levels,
6.6 GB at its peak and 2.1 GB on disk; from both ends (stops apart) 47
moves in 6.1 s, 18,412,361 arrangements, 364 MB, nothing on disk. A
fiftieth of the time and a twentieth of the arrangements.

The hidden fuzz cases: the moves made backwards against the forward moves
on 1,558 generated boards (14,921,744 arrangements, 84,827 one-way moves:
none missing, none extra, none called two-way wrongly); the search from
both ends against the search from the start on 5,669 boards, 4,117 with a
path (the longest 28 moves), 2,019 of them deep enough for a goal level:
the same outcome and the same fewest moves on all.

## Consequences
- A nested puzzle whose pieces all have goal positions is searched to half
  the depth from each end. 6 Heart goes from hours to seconds.
- The run card's "moves deep" is the two ends together, as for other
  puzzles. The path found may differ from an earlier build's; its length
  does not.
- A puzzle with a piece that may end anywhere, with several goal
  positions for a piece that is not a copy, with partial nesting, or with
  a piece that could hold more than six others is searched from the start
  alone, with loose stops on a grid, as before.
- An earlier build does not know `BTSLSV02` and takes such a saved search
  for a damaged one: it wipes the folder and begins again. Going back a
  version with a both-ended nested search paused loses that search, not
  the puzzle.
- A search paused in v1.8.1 or earlier is found again by the name that
  build gave it, and Continue asks before it goes on from the start alone.
  `tests/data/slide_paused_by_v1.8.1/` holds one made by v1.8.1's engine
  (the session, contract and e2e tests carry it on and solve it again).
- Tests: `checkSlideMovesBack` lists every arrangement of small boards and
  checks the two directions against each other; the nested fixtures and
  generated boards give the same fewest moves from one end and from both,
  on one thread and three, every step a move; a search stopped at either
  end, saved and carried on gives the path of an uninterrupted run; a goal
  that is not one arrangement, partial nesting and too many nested pieces
  each fall back to one end and solve; copies with a goal each go from
  both ends; the hand-made board above ends with no path and its goal end
  drops arrangements its older levels hold.
- Soft spots: the stops apart take up to three times the arrangements a
  level where loose stops would do (backward moves for loose arrangements
  would give both). `nestedBound` is a bound, so a piece with roomy holes
  that never holds seven pieces still sends its puzzle to one end. With
  nested slides every level is kept at both ends, so a long search is
  bounded by the disk: about 13 bytes an arrangement on 8 Heart.

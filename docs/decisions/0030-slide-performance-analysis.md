# 0030: The Performance Analysis of a sliding solve

**Status:** accepted; `core/shapelogic/solve/slide_forecast.h`
(`slideForecast`), `core/shapelogic/solve/sliding_search.h`
(`SlideSearch::onDepth`), `core/shapelogic/solve/solve_session.cpp`
(`onSlideDepth`), `core/shapelogic/model/problem.h` (`slideLevelMs`),
`core/shapelogic/io/puzzle_file.cpp` (`slideLevelMs=`), `api/types.json`
(`SlideForecast`, `SolveProgress.slideForecast`,
`ProblemDetail.slideForecast`), `web/src/renderer/solver/performance.ts`,
`web/src/renderer/solver/solutions-card.ts`

## Context
A large sliding search runs for hours or days, one move deep at a time,
and each move takes longer than the one before. The Solver said how deep
the search was ("36 moves deep") and how many arrangements it had seen. It
did not say how long the next moves would take. The user asked for a
forecast, every five moves to 100, worked out again after each move, kept
while a solve is paused (in the crash recovery copy too), and gone once
the solve is through.

## Decision
**The search tells each move; the session times it.** The level search
calls `SlideSearch::onDepth` with its depth (both ends together) as it
begins and after each level. The session times the move from one call to
the next. A depth one more than the times known is a move timed. Any other
depth is a search beginning (a new start, at 0) or carried on (from its
saved depth), and the times are cut to it or padded with untimed moves
(`kUntimedMs`).

**Moves searched before the times were kept are untimed, not zero.** A
paused solve saved without times (by an older version) opens with one
untimed move for each move its search went (`searchDepth`), written `-` in
the file and null in the API; when its search is carried on, the session
pads to the depth the search goes on from. The growth leaves untimed
moves out, and the estimates go on from the newest timed move, so they
start once two moves in a row have been timed. The panel says which moves
have no times. The session now also keeps the depth a slide search went
with the problem (`searchDepth`): the run's end read the counter after
each start had cleared it, so a sliding problem kept none, and a file the
earlier versions paused says nothing of its depth until it is carried on.

**The problem keeps the times.** They live in `Problem::slideLevelMs` and
the file's `slideLevelMs="…"` (milliseconds a move). So the crash recovery
copy, which is the puzzle saved, has them, and so does a saved paused
solve. A search carried on goes on with them. They are dropped when the
solve is through and with the results (Abort, Reset, Solve).

**The forecast is the core's.** `slideForecast(levelMs, runningMs)` gives:
- the growth: the geometric mean of the newest five ratios of moves that
  took 20 ms or more, since the times rise and fall with the levels' sizes;
- every fifth move past the depth to 100, each with its own time and the
  time until the search has searched that deep. The next move takes the
  last move's time times the growth, or as long as it has run when that is
  longer. Each move after it takes the one before's time times the growth.
- times held to a thousand years.
`solve.progress` carries it live (`slideForecast`, with how long the move
under way has run). `problem.get` carries it for a paused solve, with no
move running.

**The Solver shows it under the empty solutions card.** It shows only on a
sliding puzzle whose solve is under way or paused, and only while no
solution is listed. It says the depth, the last move's time, how long the
move under way has run, the growth, and a table: moves deep, that move's
time, and the time from now ("once continued" when paused). It is drawn
again with each progress, and it scrolls when it is taller than the
card. A paused sliding solve's empty card says "Search paused" and to
press Continue.

## Consequences
- The estimates of the first moves swing widely, because a few short moves
  say little of the growth. They settle as the moves grow.
- With several starts the times are those of the start under way, and a
  start that begins drops the last start's times. Starts searched side by
  side share the one depth, as the progress's "moves deep" already did.
- `SolveProgress` and `ProblemDetail` have `slideForecast`, and the fake
  engine's recordings were made again.

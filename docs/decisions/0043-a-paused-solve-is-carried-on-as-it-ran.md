# 0043: A paused solve is carried on as it ran, and a sliding search is never begun again unasked

**Status:** accepted; `core/shapelogic/app/document.cpp`
(`Document::pausedOptions`, the refusal in `solveLaunch`),
`core/shapelogic/solve/solve_session.cpp` (`SolveSession::resumeRefusal`),
`engine/methods_solve.cpp` (`launch`), `core/shapelogic/solve/sliding_search.h`
(`SlideSearch::pickingUpSince`), `core/shapelogic/solve/solve_progress.h`
(`SolveProgress::slidePickingUp`, `slidePickingUpMs`), `api/solve.json`
(`solve.continue`), `api/types.json` (`SolveProgress.slidePickingUp`,
`slidePickingUpMs`),
`web/src/renderer/solver/autosave.ts` (the journal's continue, `takeOver`,
`pickUp`), `web/src/renderer/state/solves.ts` (`Solves.adopt`),
`web/src/renderer/solver/performance.ts` (`PICKING_UP`)

## Context
A sliding search of the user's, 185 moves deep after eight hours (190 GB
of levels), began again at the first move. The page had been reloaded
(View ▸ Reload in development, which the dev server also does when a
source file is saved); the engine and its run went on. The crash recovery
journal of the new page then did what it does: asked for a save point,
wrote the rescue copy, and sent `solve.continue` with "the options the
run was started with". Those were kept by the page alone, and the new
page had none: it sent the options its card showed, without the optimized
paths the user had chosen when Solve asked. A slide search's saved search
is named by the options the search depends on (the nested slides, the
vertical moves, the grid of the optimized paths), so the run looked for a
search of another name, found nothing saved, and searched from the start.
The saved search stayed in its folder, and Pause, then Continue (which
knows the problem's grid) took it up again; the times of 185 moves were
lost.

Two things made that possible. The engine took a run's options afresh
with every `solve.continue` and remembered none, so every caller that
carries a run on had to have kept them. And a continue whose options name
another search was not a continue at all, yet nothing said so.

A third thing confused the user afterwards: after every continue a deep
search spends minutes taking its saved search up (the level files opened,
the visited sets read back, and the level that waited to go into them put
there, which the run that stopped would have done), and through that time
the progress looked like a search that had hung: no move's clock, no
pictures.

## Decision
- **`solve.continue` without options carries a solve paused in memory on
  with the options of its last run** (`Document::pausedOptions`: the
  session keeps them). The journal and the page that takes over after a
  reload send none. A solve with no session paused in memory (left solving
  by a file, never run) gets the defaults, as before and as `solve.start`
  does.
- **`solve.continue` with options that name another sliding search than
  the one saved is refused** (`invalid_argument`, with the options that
  differ): a sliding solve paused in memory, with a saved search of the
  problem in its folder, and nested slides, partially nested slides,
  vertical moves, any orientation, mirrored orientations or the optimized
  paths' grid other than its last run's. To search with other options is
  Abort, then Solve, as it is to start over. With nothing saved there is
  nothing to leave behind, and nothing is refused. The Solver shows the
  refusal in a message box.
- **The run says when it takes its saved search up, and for how long it
  has** (`SolveProgress.slidePickingUp`, `slidePickingUpMs`), and the
  Solver shows "Picking up the saved search… 47 s" in the Performance
  Analysis, the empty solutions card and the view instead of numbers that
  do not move, the seconds counted on. The log of the user's solve shows
  why the count is wanted: of the 56 times the search was taken up, 30
  took under 15 seconds, 12 up to a minute, 12 one to seven minutes and
  two over 13 minutes; the view and the card say that it is mostly
  seconds and now and then minutes.
- **The log says what taking a saved search up cost**: "Carried on the
  saved search in …: … Taken up in N s, of which M s for the visited
  sets." The journal spaces its saves by what a save cost
  (decision 0034) and does not count this time. Most of it is the level
  that waited going into the visited sets, which is the search's own work
  and no cost of the save; what is left over (the sets read back) is to be
  measured with these lines before the spacing is changed.

## What was not done
- The page no longer keeps runs' options for the engine's sake (a first
  fix kept them in its session storage across a reload; the engine's
  memory replaces it).
- The times of the moves a search had made are not restored when a search
  begins again and its old saved search is taken up later: the problem
  keeps one list of times.

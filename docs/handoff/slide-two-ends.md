# Handoff: compact slide keys and nested slides from both ends

The two stages of [docs/SLIDE-TWO-ENDS-PLAN.md](../SLIDE-TWO-ENDS-PLAN.md),
core and CLI only. Stage A (decision 0032, committed as "Solver: a grid's
slide keys number only the stops") put big boards of blocks on the level
search. Stage B (decision 0033) searches nested slides from the start and
from the goal until they meet. The user's "Abe 1 to 6 Heart" goes from
hours to seconds; "Abe 1 to 8 Heart" is the search the plan was written
for, and how it ends is in the plan's Status.

## Done (Stage B)

### The search (`core/shapelogic/solve/sliding_search.cpp`, `.h`)
- `SlideMover::movesBack`: for an arrangement, every arrangement it is one
  move from. A lone piece goes back as it came (`expand` with solos only).
  For each piece that holds others, every non-empty part of what it holds
  is slid away with it (`floodShifts`, split out of the forward flood) and
  an arrangement counts where the piece holds exactly that part
  (`undoable`). One-way when the part is not all it holds.
- `LevelSearch::Side` owns its visited sets and the level waiting to go
  in; `step`, `ageOut`, `restoreVisited` and the loop's merge take the
  side (`insertVisited` its sets). The goal end expands with `movesBack`.
- `canMoveBack` / `SlideSpace::backMoves`: nested, stops apart, whole
  nesting, `nestedBound` (the most pieces that fit one piece's holes, a
  bound) at most `kMaxBackNested` (6).
- `findSlidePath` for a new nested search on a grid: the both-ends tables
  (stops apart) in 64 and 128 bits, then loose stops, then stops apart,
  then the original search. `tableSearch` answers `Tabled::Searched`,
  `NoFit` (try the wide key) or `NotThis` (no such search in any key). The
  saved pass comes first, as in Stage A.
- `kNestedMeetFrom` = 1: with nested slides the goal end starts with the
  first level. Without them it is still a million arrangements.
- `singleGoal` / `goalWays`: copies of a piece with a goal each are one
  goal arrangement when the goal rows hold them in exactly one way. Also
  without nested slides.
- The path's goal half is traced forwards (`stepBack` with `towardGoal`).
- `state.bin`: a both-ended nested search is saved as `BTSLSV02` with the
  goal end's visited sets; everything else as `BTSLSV01`, unchanged.
- A/B: `SlideSearch::oneEnd`, `SlidingOptions::oneEnd`, `shapelogic_cli
  --slide-one-end`.
- `checkSlideMovesBack` (`SlideBackCheck`): the tests' hook. It lists
  every arrangement a small board comes to and checks that the backward
  moves are exactly the forward moves, and that none is called two-way
  wrongly.

### Tests (`tests/unit/solve/test_sliding.cpp`, `tests/cli/test_cli.py`)
- "the moves made backwards are the moves" on six boards, and a hidden
  fuzz case (1,558 boards, 14.9 million arrangements, 84,827 one-way
  moves: clean).
- One end against both on the nested fixtures and generated boards, one
  thread and three (`[parallel]`), every step of each path a move; a
  `[slow]` case with more boards; a hidden fuzz case (5,669 boards, 4,117
  with a path, 2,019 with goal levels: the same outcome and moves).
- The fall-backs to one end (goal not one arrangement, partial nesting,
  too many nested pieces); copies with a goal each from both ends; a
  both-ended nested search stopped at either end, saved and carried on;
  one-ended saves carried on (loose stays one-ended, apart takes up the
  goal end).
- "the goal end looks for its one-way moves in its own older levels": a
  hand-made board with no path, the only one found where the goal end's
  filter has anything to drop (decision 0033 says why the others do not).
- The CLI test solves the nested fixture with `--slide-one-end` and
  expects the answer of the default.

### Continue's question, and Solve's discard (after Stage B)
- `savedFromStartAlone` / `pendingFromStartAlone` (`sliding_search.h`):
  whether the search Continue would carry on was saved from the start
  alone with loose stops where a new search would go from both ends.
  `tableSearch` is split for it: `nameSpace` builds and names a search,
  `tableProbe` says what `tableSearch` would do without searching.
- `Document::slideFromStartAlone`, the engine's `sliding.resumeAdvice`
  (`{problem, options}` gives `{fromStartAlone}`; `api/sliding.json`,
  `docs/API.md`, `web/src/engine/api.generated.ts`).
- The Solver (`web/src/renderer/solver/workspace.ts`): Continue on a
  sliding puzzle goes on with optimized paths when the problem keeps a
  grid (a file opened paused has no run of this session to go by), asks
  the engine, and where the answer is true shows "Solve Again with the
  Faster Search?" (Solve Again, the default; Continue; Escape stays
  paused). Pause asks nothing: a search of this build is saved from both
  ends and carried on so.
- `SolveSession::launch` drops a sliding problem's saved searches when a
  solve starts afresh. It never did (the call sat in `launchStacking`), so
  Solve after a pause carried the saved search on. Found with a real
  v1.8.1 save: "solve again" ran one-ended for five minutes.
- `tests/data/slide_paused_by_v1.8.1/`: the cup-and-blocks board of the
  session tests, built through release v1.8.1's engine, solved with nested
  slides and optimized paths under a disk budget of a byte (so it pauses
  at once), the document saved, with the `slide-…` folder that engine
  left. `make_fixture.py` beside it makes it again.
- Tests: `test_sliding.cpp` ("a search saved from the start alone is told
  where both ends would do"), `test_sliding_session.cpp` ("a paused search
  saved from the start alone is told", "Solve throws the saved search
  away", which fails without the fix, "a search paused in v1.8.1 is told,
  carried on or solved again"), the contract suite
  (`sliding_resume_advice`, and calls in `sliding_grid_advice` and
  `sliding_solve_pause_continue_and_play`), `sliding.e2e.ts` ("a search
  paused in v1.8.1: Continue asks whether to solve again from both ends").
- Checked by hand through the engines: 6 Heart's Problem 3 paused in a
  v1.8.1 engine built from the tag, opened here: the advice is true (false
  without optimized paths or nested slides); carried on it stays
  one-ended; solved again it takes 13.5 s (beside another search) and the
  saved folder is gone. Paused in this build after 5 s and carried on:
  14.1 s in all, the arrangements of an unbroken run, the advice false.

### Docs
Decision 0033; a paragraph each in 0029 and 0031; `docs/ARCHITECTURE.md`
(the sliding search); the guide's `command-line.md` (`--slide-one-end`,
when the search runs from both ends) and `solver-tab.md`; the plan.

## Gate results

On the final tree (2026-11-13, MacBook Air M4):
- `just test-all`: 37/37 (243 s). The quick sliding set three times on the
  release build: 104 cases pass each time.
- `just oracle --tier sliding`: 26/26 match the original on all threads,
  35/35 with `--threads 1`. `just oracle-resume`: 290/290.
- `just test-tsan`: the `[parallel]` cases pass (99 cases), the contract
  suite 0 failed.
- `just test-asan`: 19/20. The contract suite's `stl_exports` passed its
  60 s limit beside the slow shards, as it did in Stage A; the contract
  suite alone on the ASan build passes (71 s). No sanitizer report.
- `just app-check` passes; `just app-test` with the release engine: 418
  tests in 42 files; the e2e spec the change touches
  (`just app-e2e sliding.e2e.ts`): 14 passed. The full e2e suite was not
  run.
- `just site-build`: the site builds and its links check; "the demo is
  the current app" fails, because the core changed since the demo was
  built (it has since Stage A). `just site-demo` at the next release puts
  that right.
- `just check`: not run (LLVM is not installed).
- The recordings of the fake engine were not made again: none holds a
  nested sliding solve.

Continue's question and Solve's discard (2026-11-13, beside the 8 Heart
search):
- `just test-all`: 37/37 (512 s), with the new session and search cases,
  the contract suite (`sliding_resume_advice`) and `api-docs`.
- `just app-check` passes; `just app-test`: 418 tests in 42 files;
  `just app-e2e sliding.e2e.ts`: 15 passed, on the release engine built
  with the fix.
- Not run for this change: the sanitizer builds and the oracle. The
  release CLI was not linked again either (the 8 Heart search was using
  it), so `just build release` is due before the next oracle or CLI
  measurement.

## Measurements
Release build, MacBook Air M4, nested slides, optimized paths. "Abe 1 to
6 Heart": 50, 50, 56 and 47 moves in 8.9 s, 5.1 s, 19.2 s and 6.1 s (v1.8.1:
not solved, 1,565 s, 9,172 s, 2,825 s). Problem 3 with `--slide-one-end` on
this build: 321 s and 399 million arrangements against 6.1 s and 18.4
million. The Klotski benchmarks (`[.bench][sliding]`, no nested slides) are
where they were: 0.99 to 1.08 s before, 0.99 to 1.07 s now.

## Known issues and soft spots
- An earlier build takes a `BTSLSV02` state for a damaged folder and
  begins that search again.
- A search saved one-ended with loose stops (every optimized nested solve
  paused in v1.8.1) stays one-ended when carried on. Continue asks first
  and offers to solve again. The autosave's own continue and the cache
  folder question's Try Again do not ask (neither meets such a search
  unless the user chose Continue on it before).
- Both ends means the stops apart: up to three times the arrangements a
  level where loose stops would do.
- With nested slides every level of both ends is kept, so a long search
  is bounded by the disk.
- `nestedBound` is a bound: a piece with holes for seven small pieces
  sends its puzzle to one end even if they never all nest.

## Next
The plan's "Limits and edge cases to solve after", in its order: longer
solutions than the disk allows, less disk for each arrangement, loose
stops from both ends, tighter packing, goals that are not one arrangement.

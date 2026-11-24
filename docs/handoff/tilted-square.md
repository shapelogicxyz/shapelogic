# Handoff: the Tilted Square mode, stages A to G

The stages of [docs/TILTED-SQUARE-PLAN.md](../TILTED-SQUARE-PLAN.md),
decision 0045. A to E (geometry, document and engine, front end, drawing,
rectangular trays) are described in the plan's Status and in
docs/ARCHITECTURE.md; this note is Stage F's (fixture, guide and finish)
and, at its end, Stage G's (every placement), the last of the plan.

## Done (Stage F)

### Fixtures (`tests/data/puzzles/tilted/`)
- `make_fixtures.py` makes them through the engine as the front end does
  (`python3 tests/data/puzzles/tilted/make_fixtures.py
  build/release/engine/shapelogic_engine`); no test runs it.
- `TriplePlay.xmpuzzle`: "Triple Play (IPP34)" by Goh Pit Khiam and Jerry
  Loo. 6 by 6, walls (5, 0) and (0, 5), 1 down 3 over, allowance 0.05: 24
  whole cells. Problem 0 (pieces turned over): **1 assembly**, not the 2
  the plan's Stage F text says: the two ways are one another turned half
  round, as the board is, and the symmetry breaker keeps one (Stage B found
  the same). Problem 1, "One face up" (`flipPieces` off, stored): S4 to S6
  are the pieces' mirror images; the script tries all eight mixes, checks
  exactly one goes, and gives the problem that mix (S3, S4, S5): 2
  assemblies (no symmetry breaker with the turns limited).
- `EightByEight.xmpuzzle`: an open 8 by 8 tray, 1 down 2 over, strict: 45
  whole cells filled by five pieces of nine (a split of the board, written
  in the script as a picture): 2 assemblies, 0.3 s.
- The script also writes the demo's example,
  `site/public/examples/demo/tilted.xmpuzzle` (Triple Play's first problem
  and its shapes), and the guide's download,
  `site/public/examples/TriplePlay.xmpuzzle`, a copy of the fixture.
- The folder is below the top level, so the tests that compare every
  top-level fixture with the original skip it, as they skip `new-grids/`.

### Tests
- `tests/unit/io/test_puzzle_file.cpp`: the tilted fixtures are saved byte
  for byte as written (uids aside) and read back to themselves.
- `tests/unit/app/test_document_tilted.cpp`: each fixture opens without a
  notice, its board is the tray's (not made again), its pieces fit, and
  each problem solved again with its stored `flipPieces` gives the file's
  assemblies, as a multiset.
- `tests/cli/test_cli.py` `test_tilted`: the CLI solves every problem to
  the file's count (see the fixes below).
- `web/tests/engine/tilted.test.ts`: each fixture through the client,
  opened quietly, each problem solved with `readOptions` and
  `solveOptions` of its stored settings, as the Solver does.
- `web/tests/e2e/tilted.e2e.ts`: a second test opens Triple Play, checks
  each problem's stored switch and count, resets and solves both. With
  the first test (New, tilt, walls, a piece, the Solver's options) it is
  the plan's e2e spec.

### Guide, demo, docs
- `site/src/content/docs/docs/puzzle-types/tilted-square.md`, in the
  sidebar after Stacking and in the overview's table; the demo page's
  table. Pictures `site/src/assets/screens/tilted-*.png`, taken by
  `web/tests/docs/screenshots.docs.ts`'s "the tilted screenshots".
- The demo (`web/src/demo/main.ts`): a Tilted Square example after
  Stacking (`Example.grid` is a `NewKind`, named by `kindName`), played
  as Assemble Only. The demo rebuilt (`just site-demo`, `--check` passed:
  every example opens and plays).
- `docs/ARCHITECTURE.md` (the fixtures folder, the web section),
  `docs/webui-spec.md` section 13, `RELEASE_NOTES.md` (an Unreleased
  section for the next release to retitle).

## Gates
- `just check` on the two changed C++ test sources, `just app-check`.
- The tilted C++ cases (`bt_tests "[tilted]"`), `test_cli.py`, the new
  engine test and the tilted e2e spec: passed.
- `just gate build unit app site`: all passed (test-all on dev, and twice
  on release; app-check, app-test; site-build with the rebuilt demo). The
  tilted e2e spec passed three times.
- Not run: the oracle (nothing it reads changed; `oracle-roundtrip` still
  waits on the reference checkout, as under Stage A), the sanitizers (only
  test code changed in C++), the other e2e specs.

## Fixed after the stage's first pass (the user: "fix the issues")
- **The CLI keeps pieces one face up.** On a puzzle with a tray it
  follows each problem's stored `flipPieces` (`bt::storedFlipPieces`), in
  the plain solve and with `--save`; `--flip on|off` goes over it and is
  refused on a puzzle without a tray. The quarter turns are worked out in
  one place, `bt::trayPlaneTurns` (`solve_session.h`), which `SolveSession`
  and the CLI share. `test_cli.py` `test_tilted` checks every problem, both
  overrides, the refusals and `--save`.
- **A piece that fits nowhere is marked.** Its row says **Doesn't fit**
  (`shapes-list.ts`, `ShapeSummary.fits`), and a tray change or a wall
  that leaves pieces off the board names them in the status bar
  (`state/tilted.ts` `unfitIds`, `unfitNote`; `tilt-card.ts`,
  `tray-view.ts`). Tests: `tilted.test.ts`, the third test of
  `tilted.e2e.ts`.
- **The disassembly player offers no analysis without a take-apart.**
  On a tilted puzzle it says there is nothing to take apart; on Spheres,
  which had the same button, that the grid has no disassembler
  (`solutions-card.ts`).

- **A rare race in a sliding-session test.** Under load, "sliding
  session: starts not searched completely stay counted over a pause"
  failed once: its run finished before `untilSearching` saw a start
  searched, and that wait fails rather than letting the test's loop try
  again. `searchingOrEnded` lets it try again, as the loop meant to.
- Gates of the fixes: `just gate build unit app` (the race above came up
  in its second release run; fixed, then `just test-all release` twice
  passed), `just check` on the changed sources, the tilted, solver,
  entities, shapes and view e2e specs (tilted three times), `just
  site-demo --check` and `just site-build`.

## Known issues
- The site's research page is what the Tilted Square Tutorial header
  opens; the guide page links to it but the app does not link to the guide
  page.

## Done (Stage G: every placement)

### Core
- `tilted::placements(tray, stop, limits)` (`model/tilted_tray.{h,cpp}`):
  every board the tray gives over every place the grid can lie, each
  once, none that is part of another; the best placement's first with its
  shift; `inUse` the board the tray lies at; `complete` false when
  `PlacementLimits` (boards kept, cells looked at) or the stop cut it
  short. It sweeps the lines `bestShift` sweeps (`sweepLines`,
  `piecesOnLine`, now shared): a cell's whole shifts are a closed set, so
  the cells whole anywhere are all whole on one of those lines where
  pieces have begun and the next thing is an end. `bestShift` gives the
  answers it gave (its tests are unchanged and pass).
- Boards are compared as shapes: moved by whole cells or turned by quarter
  turns they are one, and "part of" is meant the same way. Mirror images
  stay apart (a problem may keep its pieces face up).
- `app/placement_fit.{h,cpp}`: `PlacementFit`, a thread that solves one
  problem on each board with an ordinary `SolveSession` (count only) on a
  puzzle of its own. `Document::trayPlacements`, `fitPlacementsStart`,
  `fitPlacementsStop`, `fitPlacementsProgress`, `fittingPlacements`;
  `solving()` is true while one runs (the document is frozen), `poll()`
  notes its end as a revision of the problem alone and keeps how it ended
  until the tray, a piece or the problem changes.
- A board with fewer cells than the pieces, or one a piece fits nowhere
  on, ends a solve in an error (`TooManyUnits`, `CannotPlace`); the fit
  counts it as taking none and goes on.

### Engine
`tray.placements` (the list: shift and whole cells of each board,
`complete`, `inUse`), `tray.fitPlacements` (the job, with the solve's
options), `tray.fitPlacementsStop`, `tray.fitPlacementsStatus`, the events
`tray.fitProgress` (four times a second at most) and `tray.fitEnded`. The
end is told only once the document is free again (a fit that ends between
the poll's taking in and its telling is told at the next poll), so a client
that hears it can change the tray at once. A replaced document's fit gets a
`tray.fitEnded` with `stopped` true.

### Front end
- `entities/placements-dialog.ts` (words in `state/placements.ts`): the
  list with a picture a board (`tray.preview` at its shift), **Count
  assemblies** / **Stop**, **Pieces of** with several problems, **Use**
  (one `tray.set`: the first board the best placement, another a custom
  shift), **Show more** past 40 rows.
- **Placements…** in Tilt & tolerance (`tilt-card.ts`, the Every
  placement row); **Try every placement** in the Solver's empty card after
  a solve with no assembly (`solutions-card.ts`, `workspace.ts`
  `tryPlacements`), counting at once with the options shown.
- The demo refuses the count as it refuses a solve (`engine-bridge.ts`);
  the list works there.

### Fixture, guide, docs
- `tests/data/puzzles/tilted/OtherDiagonal.xmpuzzle` (made by
  `make_fixtures.py other_diagonal`, which now takes the names of the
  fixtures to make): Triple Play's pieces, the walls at (0, 0) and (5, 5).
  Six boards (24, 24, 24, 24, 24 and 23 whole cells); the best takes no
  pieces, the fifth takes them in 1 way, the others in none.
- The guide's page has an **Every placement** section with a picture
  (`tilted-placements.png`, taken by "the tilted placements screenshot"),
  `docs/webui-spec.md` 13.5, `docs/ARCHITECTURE.md`, decision 0045,
  `RELEASE_NOTES.md`, `docs/API.md` (generated).

### Tests
- `tests/unit/model/test_tilted_tray.cpp`: the best placement first and in
  use; each board listed has its count at its shift, a canonical shift,
  and is part of no other; asked again, the same answer; **the board of
  every shift of a net (60 by 60, 80 by 80 for Triple Play's trays) is part
  of a board listed**, for ten trays (open, with an allowance, 1:1, a
  rectangle, corner walls, scattered walls); a straight tray has one
  board; the board in use; the stop and both limits.
- `tests/unit/app/test_document_tilted.cpp`: the other diagonal (the best
  placement solves to 0, the list has 6 boards, the fit's count of each
  board equals the solve of a puzzle made at that placement, the document
  unchanged but for one revision, the board that takes the pieces used and
  solved), the solve's options (turned over and face up, all eight mixes),
  the refusals, a long fit told as it runs and stopped, a document closed
  with its fit running, a board too small for the pieces; the new fixture
  in the fixtures' case.
- `tests/contract/test_engine.py` `tilted_placements`: the four methods
  and both events, the freeze, the stop, the end with a new document.
- `web/tests/unit/renderer/placements.test.ts`,
  `web/tests/engine/tilted.test.ts` (the fixture and its fit through the
  client), `web/tests/e2e/tilted.e2e.ts` (the list from Tilt & tolerance,
  Try every placement, Use, the solve, Undo).
- Added after a review of the stage: `tests/unit/engine/test_engine.cpp`
  "a fit that ends inside a poll is told once the document is free" (the
  end is not told by the poll it ended in, the document still frozen; the
  next poll tells it before its revision, and a `tray.set` then goes
  through; it fails with the hold in `pollFit` taken out); three more
  tests in `tilted.e2e.ts`: the count for the problem chosen with **Pieces
  of**, each with its stored options (Triple Play: 1 assembly turned over,
  2 one face up, on the board in use); a count of twenty dominoes told as
  it runs, stopped ("or more", "Not tried"), a board used while a count
  runs (the count stopped first, one undo step) and the dialog closed
  while counting; a 16 by 16 tray of about a hundred boards shown forty
  at a time with **Show more**.

## Gates (Stage G)
All on 2026-11-23, after the last change, one after the other:
- `just test-all` (21 of 21: the unit shards, the contract suite with its
  coverage of 150 methods, the CLI, the Python module, the API docs),
  `just test-asan` (20 of 20), `just test-tsan` (the `[parallel]` cases,
  the fit's among them, and the contract suite on the TSan engine): passed.
- `just check` on the ten changed C++ sources: no finding.
- `just app-check`; `just app-test` (53 files, 555 tests, the engine tests
  on the release engine): passed.
- The tilted, solver and entities e2e specs (24 tests), three times:
  passed each time.
- `just site-demo` (the demo rebuilt: the renderer changed, and the site's
  test "the demo is the current app" fails until it is), `just site-demo
  --check`, `just site-build`: passed.
- Not run: the oracle (nothing it reads changed; `bestShift`, whose sweep
  was split into shared parts, is pinned by its own tests, which pass
  unchanged; `oracle-roundtrip` still waits on the reference checkout, as
  under Stage A), the benchmarks, the other e2e specs.
- The first `just test-all` of the stage failed in two places, both counts
  of the files in `tests/data/puzzles/tilted` (two, now three):
  `test_puzzle_file.cpp` and `test_cli.py`. Fixed, then everything above
  was run again.

## After the review (2026-11-23)
- In the dialog's pictures the tray's straight squares are not outlined
  (`.plc-row .plc-pic .tpic .tp-sq`): at that size their lines, bright in
  the dark theme, hid the cells. The walls and the outline stay. The
  guide's picture was taken again.
- The tests above. Checked by hand in the built demo, dark theme: the
  list, its pictures and Use work there; Count assemblies says "Searches
  run in the app, not in this demo".
- Gates after these: the tilted e2e spec (7 tests) three times, `just
  app-check`, `just app-test`, `just test-all`, the tilted `[parallel]`
  cases under TSan, `just check` on `test_engine.cpp`, `just site-demo`
  with `--check`, `just site-build`: all passed.

## Known issues (Stage G)
- **Placements…** is the last row of Tilt & tolerance: in a window 800
  pixels high it lies below the left card's visible part, and the section
  must be scrolled to reach it.
- A fit counts every assembly of every board; there is no "stop at the
  first assembly". Stop ends it and keeps the counts.
- A board and its mirror image are both listed, and both tried, even for
  a problem whose pieces may be turned over (their counts are then equal).
- The five boards of 24 cells of Triple Play's trays look much alike at
  the size of the list's pictures: they differ by a cell or two along the
  rim.
- A solve that ends in an error because the pieces have more cells than
  the board in use ("Pieces contain too many units") offers no Try every
  placement; only a solve that ends with no assembly does. With the best
  placement in use no other board has more cells, so nothing is missed
  there; with a custom placement of fewer cells, Placements… in Tilt &
  tolerance is the way.

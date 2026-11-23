# Handoff: the Tilted Square mode, stages A to F

The stages of [docs/TILTED-SQUARE-PLAN.md](../TILTED-SQUARE-PLAN.md),
decision 0045. A to E (geometry, document and engine, front end, drawing,
rectangular trays) are described in the plan's Status and in
docs/ARCHITECTURE.md; this note is Stage F's (fixture, guide and finish)
and the brief for Stage G.

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

## Brief for Stage G (every placement)
As in the plan: `tilted::placements()` (the boards at the crossings of the
sweep lines, equal ones once, parts of others dropped, a limit that says
when it is not complete), `tray.fitPlacements` (a job per problem, on a
scratch copy, with a stop token and progress), "Placements…" in Tilt &
tolerance and "Try every placement" in the Solver after no assembly.
Triple Play with its walls on the other diagonal is the test: the best
placement takes no pieces, others do. `make_fixtures.py` is the place to
add such a fixture.

# Plan: compact slide keys and a two-ended search with nested slides

## Context
Written on 2026-11-13 from an analysis of the two sliding searches in
`core/shapelogic/solve/sliding_search.cpp` and a prototype built outside
the repository. "Status" below says which stages are in the code.

**The problem.** The user's "Abe 1 to 8 Heart" (a 24x18 tray as a 4x3
grid of 6x6 cells, 20 pieces, nested slides, optimized paths) cannot be
solved today, for two reasons:
- The level search packs an arrangement into a 64 or 128 bit key, and
  `buildTables` gives each piece enough bits for every position it fits
  in the empty tray. 8 Heart needs about 170 bits, so `findSlidePath`
  falls back to `legacySearch`: about 11,000 arrangements a second on
  one thread, in memory only, with no depth to show.
- With nested slides the level search runs from the start only. The
  search from both ends is switched off there (`loop`: `!s_.nested`),
  because a group can close round a piece it passes and that move
  cannot be made backwards.

**What the prototype showed.** Two changes, measured with a Release
build of `shapelogic_cli` on a MacBook Air M4:

| Puzzle | Release build | Prototype | Moves |
|---|---|---|---|
| 6 Heart, Problem 1 | unsolved | 10.6 s, 28M arrangements | 50 |
| 6 Heart, Problem 1 modified | 1,565 s, 384M | 6.4 s, 17M | 50 |
| 6 Heart, Problem 2 | 9,172 s, 1,021M | 23.9 s, 55M | 56 |
| 6 Heart, Problem 3 | 2,825 s, 355M | 6.9 s, 18M | 47 |

The three solved problems give the fewest moves their saved solutions
have. On 8 Heart the compact key is 100 bits (4 a block, 6 a small
piece), the search runs at about 1.2 million arrangements a second, and
each level is about 1.14 times the last at both ends. The two-ended run
on Board 1 is in "Status" below.

The prototype's changes are in `tmp/sliding-two-ended-prototype.patch`
(git-ignored; against `sliding_search.cpp`). It is a measuring tool: it
reads environment variables, saves only half of a two-ended search and
has no tests. The stages below build the same two ideas properly.

## Status
- [x] Stage A: compact keys and the build order (2026-11-13; decision
  0032). 8 Heart now runs in the level search: 59 moves deep in 105 s
  from the start alone.
- [ ] Stage B: the two-ended search with nested slides
- 8 Heart Board 1 with the prototype: stopped on 2026-11-13 after 1 h
  24 min, to leave the machine quiet for the stages' measurements. It was
  160 moves deep with no meeting (start level 80: 452,613,104
  arrangements; goal level 79: 398,835,803; 68 GB on disk). Its saved
  search was scratch data and is deleted. The search is run again from
  the start with the real build once Stage B is in.

## Stage A: compact keys and the build order

Core only. No change to any answer: the same levels, arrangement for
arrangement, in fewer bits.

1. **Number only the stops.** With a grid, a key only ever holds a
   piece's stops (`PosTable::rest`) and its loose-class rows (decision
   0031). `PosTable` gets two maps, row to key code and code to row,
   filled after `buildLoose`. The field width comes from the number of
   codes. Without a grid every row is a stop and the maps are the
   identity, so those keys, and the searches saved with them, do not
   change.
2. **One place packs and unpacks.** `SlideSpace::posOf` returns the row
   for a key's code and `SlideSpace::with` writes the code for a row.
   Every site that shifts a row into a key by hand goes through them:
   `expand`, `expandLoose`, `emitSolo`, `singleGoal`, the start key.
3. **Group floods pass through rows that are not stops.** In `expand`
   and `floodGroup` the group's key is only written for a shift where
   every member stands on a stop; the flood itself works on rows.
4. **Build order.** Try loose stops in 64 bits, loose in 128, then
   plain stops in 64 and in 128. Today a loose build that misses 64
   bits drops to plain stops in 64 without trying 128, which with
   compact keys would silently lose the loose stops on a board whose
   loose build needs 65 to 128 bits. (The prototype had 6 Heart at 65
   bits; as built its loose stops fit 64.)
5. **Saved searches.** A grid search saved by an earlier build has the
   old numbering and an old name. As with loose stops
   (`kLooseFormat`), when a folder of the old name exists and the
   search is being carried on, build the tables with the old numbering
   and carry it on as it was begun. (As built: `findSlidePath` first
   looks for a saved search of each form the tables can take, and only
   when the problem has a saved folder at all.)
6. **An A/B switch.** `SlideSearch::plainKeys` (`bt --slide-plain-keys`):
   the old numbering, for the tests and the benchmark. No environment
   variable in `core/`.
7. **Tests** (`tests/unit/solve/test_sliding.cpp`):
   - the level sizes and the path are equal with compact and plain keys
     on every grid fixture, with loose stops and with them apart, on one
     thread and several;
   - a generated board of blocks that needs more than 128 bits plain
     and fits compact is solved by the level search (it reports a
     depth) and agrees with the legacy search on its fewest moves;
   - a search without a grid has the name it had (saved searches
     carried on); a grid search saved with plain keys is carried on;
   - the build order: a board whose loose build needs 65 to 128 bits
     runs with loose stops.
   Make the fixtures with the generator the other sliding tests use.
   The user's Heart files are not test data.
8. **Gates.** `just test-all`, `just test-asan`, `just test-tsan`,
   `just oracle --tier sliding` with all threads and with `--threads 1`,
   `just oracle-resume`, `just check`, and the sliding benchmark with
   and without `--slide-plain-keys`.

## Stage B: the two-ended search with nested slides

1. **Moves made backwards** (`SlideMover::movesBack`). For an
   arrangement Y, every arrangement X that Y is one move from:
   - a piece moved alone goes back as it came (its flood is the same
     from either end);
   - a group that arrived at Y was the outer piece and some of what is
     nested in it at Y, since it may have closed round the rest. For
     each outer piece, each non-empty subset of what is nested in it is
     flooded back with the others left in place, and a stop X counts
     only where `groupAt(X, outer)` is exactly that subset. The move is
     one-way, in the sense the level search uses, when the subset is
     not everything nested at Y.
   The prototype's version is about 120 lines beside `expand`.
2. **The proof of it, as a test.** On small nested puzzles, list every
   reachable arrangement and its moves, and check both directions:
   every forward move X to Y has X in `movesBack(Y)`, and every X that
   `movesBack(Y)` gives has Y in `moves(X)`. A fuzz case over generated
   boards, as the existing fuzz cases are.
3. **Each end keeps its own older levels.** The visited sets and the
   level waiting to go in (`visitedSets_`, `pendingVisited_`) move into
   `Side`. `step` filters an end's one-way moves against that end's
   sets; `ageOut`, `insertVisited`, `evict`, `forEachLiveSet` and the
   loop's merge on the spare thread take the side.
4. **The goal end expands backwards.** `expand` takes the direction and
   calls `movesBack` for the goal end when the search is nested.
5. **When it runs.** With nested slides the search goes from both ends
   when the goal is one arrangement, partial nesting is off, and no
   outer piece holds more nested pieces than a small limit (the subsets
   double with each). It then runs with the stops apart, decided when
   the tables are built, because a loose arrangement has no backward
   moves yet and the two ends must hold the same kind of key. Otherwise
   the search is as today: one end, loose stops.
6. **One goal with like pieces.** `singleGoal` gives up when a piece
   has more than one goal row, which copies of a piece always have. A
   group of k copies with exactly k goal rows is one arrangement: the
   copies take the goal rows in order. This also lets puzzles without
   nested slides that have copies search from both ends.
7. **The path.** From the meeting arrangement the goal end is traced
   forwards: the arrangement of the level before that is one forward
   move away (`stepBack` with the direction). `placeChain` already
   finds one-way moves by trying them forwards. A test checks every
   step of a returned path is a legal move.
8. **Saving and carrying on.** `state.bin` names the goal end's visited
   sets (a new field, read as absent from older states, which have no
   goal end with nested slides). A search saved one-ended with loose
   stops by an earlier build is carried on as it was begun.
9. **An A/B switch.** `SlideSearch::oneEnd` (`bt --slide-one-end`): the
   search from the start only, for the tests and the benchmark.
10. **Tests:**
    - the fewest moves are equal from one end and from both on every
      nested fixture and on generated boards, with `meetFrom = 1`, on
      one thread and several (`[parallel]`, so ThreadSanitizer runs
      them);
    - stopped at either end, saved and carried on, the search gives
      the path of an uninterrupted run;
    - a goal that is not one arrangement, partial nesting, and too many
      nested pieces each fall back to one end and still solve;
    - no path: an end that runs out reports no solution.
11. **What the user sees.** Nothing new to set. The run card's "moves
    deep" is the sum of the two ends, as it is for other puzzles. The
    paths found with optimized paths and nested slides may differ from
    today's (same length), so the fake engine's recordings and any e2e
    step that pins such a path are made again; run only the specs
    touched.
12. **Records.** Decision 0033 (backward moves; when the search runs
    from both ends and why the stops are then apart; decision 0032 is
    Stage A's compact keys), a line in decisions 0029 and 0031,
    `docs/ARCHITECTURE.md`, the user guide where it describes the
    sliding search, and a handoff note.
13. **Gates.** Stage A's, then `just app-check` and `just app-test`
    with the release engine, and the user's 6 Heart problems by hand:
    50, 50, 56 and 47 moves.

## Limits and edge cases to solve after

In the order they are likely to matter.

1. **Longer solutions than disk and time allow.** Both ends grow about
   1.14 times a move on 8 Heart, and nested slides keep every level at
   both ends. At about 13 bytes an arrangement a search to 180 moves
   takes a few hundred GB and several hours. If the solution is much
   longer, a fewest-moves search is out of reach and what is left is a
   search that gives up the fewest moves: goals in stages (a piece
   without goal marks may end anywhere, which the user already does by
   hand) or a search led by an estimate of the moves left.
2. **Less disk for each arrangement.** Three things to look at: the
   visited sets hold a second copy of the levels kept for the path; the
   128-bit delta coding; and whether the path can be traced without
   the old levels, as thinned levels are for other puzzles.
3. **Loose stops from both ends.** Stage B searches the stops apart,
   which is about 1.35 times the arrangements a level on 8 Heart.
   Either backward moves for loose arrangements, or a start end with
   loose stops meeting a goal end without.
4. **Tighter packing.** A field is a whole number of bits for each
   piece. A mixed-radix key (the product of the pieces' stop counts)
   would take 8 Heart from 100 bits to about 88, and put a board a few
   bits over 64 on the narrow key, with half the memory and disk.
5. **Goals that are not one arrangement.** Pieces without goal marks,
   pieces with several goal positions and counted goals search from one
   end. Where the goal arrangements can be listed, the goal end could
   start from all of them.
6. **Partial nesting.** No backward moves for the wider group yet, so
   those puzzles search from one end.
7. **Many pieces nested in one piece.** The subsets double with each
   nested piece. Past the limit the search is one-ended; a smarter
   listing (only subsets that could be a whole nested group somewhere)
   would lift it.
8. **Full Search on big boards.** Without a grid every position is a
   stop, so 8 Heart is still about 170 bits and falls to the legacy
   search. A wider key (192 or 256 bits) would put every puzzle on the
   level search and leave the legacy search as the A/B switch only.
   Until then the legacy search could at least report its depth.
9. **Searches saved by earlier builds.** They are carried on as begun,
   one-ended. To get the two ends the user solves again from the
   start; say so where the Solver offers Continue, if it comes up.

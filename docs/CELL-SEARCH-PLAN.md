# Plan: a search that branches on cells, for results with holes to spare

## Context
Written on 2026-11-23, after decision 0048
(`docs/decisions/0048-variable-voxels-that-must-be-filled.md`), from one
puzzle of the user's and the measurements made on it. Nothing of this plan
is in the code yet; "Status" below says what is.

**The puzzle.** `tmp/SquareDissection.xmpuzzle` (git-ignored; copies of
each version are in `tmp/cell-search/puzzles/`): a 65 x 65 tray of
variable cells and nine pieces, each one piece of its own shape, between
17 x 20 and 24 x 25 cells. It has had three versions:

| Version | Pieces | Cells left empty | Assemblies |
|---|---|---|---|
| `SquareDissection-static` | nine rectangles, tray of normal cells | 0 | none |
| `SquareDissection-variable` | the same, tray of variable cells | 0 | none |
| `SquareDissection-corners` | a staircase of 4 + 3 + 2 + 1 cells cut off each corner of each rectangle | 360 | some (see "The prototype") |

**What decision 0048 did.** The first two versions are solved in seconds
now (the variable tray took 11.6 minutes to reduce before). The rule it
uses is "no hole to spare, so every variable voxel is filled": it does
nothing for the third version.

**What happens on the third version** (release build, one thread, the
MacBook Air M4 the user works on):

| Step | Measured |
|---|---|
| Matrix | 33,530 placements, 300 to 560 cells each: about 15 million nodes |
| Optimize (`DlxMatrix::reduce`), first pass | about 25 placements a second; 17 minutes; 11,572 placements removed, 21,958 left |
| Search without Optimize, assembler 0 | 0.0% after 60 s, nothing found |
| Search without Optimize, assembler 1 (forced, a scratch program) | 0.000000 finished after 45 s, 74,333 steps, nothing found |
| A throwaway search that branches on cells (`tmp/cell-search/pack.cpp`) | first packing after 17 s; 51 packings in 15 minutes; not finished |

Two things are wrong, and the second is the larger.

1. **The placement check is slow.** To test one placement, reduce()
   covers its 300 to 560 columns in the dancing links matrix, which
   unlinks every row that overlaps it (about 20,000 rows of about 450
   nodes) and links them again afterwards: about 40 ms a placement. All
   it wants to know is which cells no other placement can still reach.
2. **The search has nothing good to branch on.** Assembler 0 keeps the
   variable voxels out of the header list, so with every result cell
   variable it branches on pieces only: nine columns of thousands of rows
   each. Its one bound is the count of variable cells left without a row
   against the holes allowed, and with 360 holes allowed that bound says
   nothing until several pieces are down. Assembler 1 has the variable
   voxels as columns with a minimum of 0 and may branch on them, but each
   branch still covers and hides rows of the large matrix, and it got no
   further.

**Why the rule of decision 0048 cannot be stretched.** With holes to
spare a gap beside a piece is allowed: it only uses up holes. In the third
version every packing has exactly 360 empty cells (the tray's 4,225 less
the pieces' 3,865), but which cells they are is free, and the packings
that exist put two pieces' cut corners into each other, which the plain
rectangles could not do. So there is no "this cell must be filled" to
sweep with. The search has to try, and prune by counting.

## Status
- [x] Decision 0048 (2026-11-23): in the working tree, not committed at
  the time of writing. This plan builds on it; commit it first.
- [ ] Stage A: the reach bitmaps
- [ ] Stage B: assembler 2, the cell search on one thread, behind a CLI switch
- [ ] Stage C: its reduce, and the same check as assembler 0's placement pass
- [ ] Stage D: pause, save and carry on
- [ ] Stage E: several threads
- [ ] Stage F: shapes with several pieces, and piece ranges
- [ ] Stage G: chosen by itself: benchmarks, the rule, engine, front end, guide
- [ ] Stage H: stronger bounds (research; may end with "no")

## The two ideas

### 1. The reach of the remaining pieces, from bitmaps
Every placement on every grid is a rotated shape moved by a whole
(x, y, z) in the result's cell array (`PlacementFinder::find`; colors,
`Voxel::onGrid`, start locks and orientation limits only decide which
moves exist). So for one orientation of one part:

- the shape is a list of **runs**: for each line (dy, dz) of its box, the
  stretches of cells along x (a rectangle with cut corners has one run a
  line);
- its placements are an **anchor set**: one bit for each move that has a
  placement.

Keep a set of result cells as a bitmap with each x line starting on a
64-bit word. Then, for a set `free` of cells no piece covers yet:

- **fits** = the anchors whose every run lies in `free`. A run of length
  L fits where L cells in a row are free: `free & free>>1 & … ` in
  log2(L) shift-and-AND steps on the line, shifted to the anchor; the
  runs of a shape AND together, the lines by whole-line offsets.
- **reach** = the cells some fitting placement of some remaining piece
  covers: the same steps backwards (shift and OR).
- **dead** = `free & ~reach`: the cells nothing can cover any more. A
  dead filled voxel ends the branch; dead variable voxels are holes, and
  more of them than the holes left ends it too. A remaining piece with
  no fit ends it as well.

This is the count reduce() and both searches make today (a column left
without a row), without touching a matrix: a few thousand word
operations for all orientations, where a cover walks millions of nodes.
The prototype does it on a 65 x 65 tray with 18 orientations in about 12
microseconds a search node, everything else included.

### 2. Branching on the first undecided cell
Take the first cell in array order that is free and not dead. Then
exactly one of these holds in any assembly that extends the position:

- a remaining piece is placed with **its first cell** (the first cell of
  its first run) on that cell; or
- the cell stays empty (only a variable voxel, and only while holes are
  left).

No placement can cover the cell otherwise: all cells before it are
decided, so a placement covering it has no earlier cell, and its first
cell is this one. That makes the search complete, and each assembly is
reached once (the order of the decisions is fixed by the cells).

The branches at a cell are at most one per orientation of each remaining
part, plus "empty": 19 on the user's puzzle, against thousands of rows of
a piece column. Copies of one shape are not told apart (the branch names
the shape, and a count of copies goes down), so several pieces of a
shape and piece ranges need no extra rule against permuted duplicates.

## The prototype
`tmp/cell-search/` (git-ignored; its README says how to build and run
it). `pack.cpp` is 140 lines: rectangles with staircase corners in a
square tray, one piece a shape, 128-bit lines, no symmetry breaker, no
reduce, no threads. It is there to be read and measured against, not to
be ported line by line.

Checked:
- cut 0 (plain rectangles): 0 tilings for the user's nine rectangles and
  14,280 for `grid65` (1,785 assemblies in ShapeLogic times the 8
  rotations and mirrors; `tile.py`, a separate program, counts the same);
- the first packing of the cut-corner version, drawn and checked by a
  separate script (`check_packing.py`: no overlap, 360 empty cells):
  22x24 at (0,0), 18x25 at (22,0), 25x22 at (40,0), 24x25 at (41,22),
  23x21 at (0,24), 18x20 at (23,25), 17x20 at (0,45), 25x20 at (17,45),
  23x18 at (42,47), each the top left of the piece's box, sizes as
  placed.

Measured on the cut-corner version: the first packing after 1.6 million
nodes (17 s), 51 packings (rotations and mirrors counted apart) after
74 million nodes (15 minutes, about 82,000 nodes a second), **and no
end**. With `grid65`'s rectangles cut the same way it found nothing in a
minute although the plain tilings are packings: which branch comes first
matters, and the bound is weak while many holes are left. Stage H is
about that. What stages A to G promise is therefore: Optimize in seconds,
assemblies found where today nothing is found; **not** that the search
of such a puzzle ends.

## Decisions to keep to

1. **A third assembler, not a fourth search inside assembler 0.**
   `Assembler2` (`core/shapelogic/solve/assembler_2.{h,cpp}`,
   `AssemblerKind::Assembler2`, "assembler 2") implements `Assembler` on
   the shape tables alone. It builds no dancing links matrix: on the
   user's puzzle that is 15 million nodes (about 300 MB) not made. The
   run layer (`SearchRun`, the stop token, the callback, decision 0009)
   and the rotation checks are the ones the other two use.
2. **The same assemblies, in another order.** As a multiset the
   assemblies are those of assembler 0 or 1; the order (and so the
   assembly numbers in a saved file) differs. That is why the choice is
   made by a rule on the problem and never on timing: one file, one
   order, on every machine.
3. **A paused solve is carried on by the assembler that saved it**
   (decision 0043). `chooseAssembler` looks at the version of the
   position a problem holds: "1.5" is assembler 0's, "2.1" assembler
   1's, the new one gets its own. A problem with no position gets the
   rule's answer.
4. **Positions name placements, not rows.** Decision 0048 had to keep a
   second way of building assembler 1's matrix because its positions
   list row numbers of a reduced matrix. Assembler 2's position is the
   list of decisions (a part, an orientation and a move, or "empty"), so
   a later change to what reduce removes cannot break a saved search.
5. **The symmetry breaker and the duplicate checks are shared, not
   written again.** The choice of the symmetry-breaker piece and the
   mirror structure are in `DlxMatrix::prepare` and
   `Assembler1::prepare` today; stage B takes them out into one function
   both those and assembler 2 call. Found assemblies go through
   `RowRotationCheck` (decision 0024) with assembler 2's placement list
   as the rows, or `RotationFilter::rejects` where that does not apply.
6. **Sliding puzzles keep their assemblers.** A sliding tray is all
   optional cells (`isSliding`: every floor cell may stay empty), so a
   rule on holes alone would take every sliding puzzle. Their start
   layouts, counts and order are compared with the original's; they are
   out until someone asks.
7. **The first rule is narrow** (stage G may widen it): every part one
   piece or not (after stage F), not sliding, holes to spare, and more
   voxel columns than the vector search takes (2,048, `simdFits`). Below
   that the vector search already counts holes as a budget and is fast.
8. **Every stage ends as AGENTS.md says**, with one addition for this
   plan: the differential scripts in `tmp/cell-search/` (`compare.py`,
   `compare2.py`: two builds of the CLI on hundreds of random puzzles,
   everything they print compared) are run where a stage says so. They
   caught nothing in decision 0048's work, which is what they are for.

## Who builds each stage

| Stage | Work | Complexity | Model |
|---|---|---|---|
| A | The reach bitmaps: cell sets, shape tables, fits, reach; exact on every grid | Medium to high: shifts across lines and layers, sparse results, grids whose moves are restricted; a wrong bit loses assemblies silently | Fable 5.2 High |
| B | Assembler 2 on one thread, behind a CLI switch; the shared symmetry code | High: a complete search with holes, ranges of "empty" decisions, the symmetry breaker, the stop and callback rules | Fable 5.2 High |
| C | Assembler 2's reduce; the same check as assembler 0's placement pass, with the matrix unchanged | Medium: the check is stage A's; the work is the progress counts and proving "unchanged" with the differential scripts and the oracle | Opus 5.5 High |
| D | Pause, save, carry on; the position format; choosing the assembler by the saved position; progress | High: file compatibility both ways, damaged positions, the session's pause paths | Fable 5.2 High |
| E | Several threads: tasks, a split search stopped and carried on | High: concurrency, ThreadSanitizer, the run layer's split state | Fable 5.2 High |
| F | Several pieces of a shape, piece ranges, the hole limit | Medium to high: the end conditions with ranges, no symmetry breaker, numbering the copies | Fable 5.2 High |
| G | Benchmarks and the rule; `chooseAssembler`; the engine's and the front end's names; guide, decision record, architecture | Medium: much of it on patterns the code has; the benchmark needs care, not invention | Opus 5.5 High |
| H | Stronger bounds, so that searches with many holes end | High and open: research, may find nothing worth keeping | Fable 5.2 High |

The rule behind the choice is the one of `docs/TILTED-SQUARE-PLAN.md`:
Fable where a wrong answer is hard to see (an assembly lost, a saved
search that carries on wrongly, a race), Opus where a mistake shows in a
failing comparison or on the screen. C is Opus's because its whole claim
("the same rows as before") is checked by machines; if stage C's author
finds the matrices differ and cannot say why, that is the point to hand
it to Fable.

Order: A, B, C in that order; D, E, F in any order after C (D before G);
G after D and F; H whenever, after B. After B and C the user's puzzle can
be solved from the CLI; after D and G from the app.

## Stage A: the reach bitmaps

Core only: `core/shapelogic/solve/reach_map.{h,cpp}`, no assembler
changed.

1. **Cell sets.** A set of result cells as words, each x line of the
   result on a word boundary (65 cells: two words a line). And, or,
   and-not, count, first set cell, clear a placement's cells.
2. **Shape tables.** For each part and each transformation
   `PlacementFinder` finds placements for: the runs, the cells' offsets
   (to make an `Assembly` and to clear cells) and the anchor set. Built
   from the finder's lists with the options a matrix is built with
   (`MatrixOptions`: strict colors, start locks, free starts, part
   transformations), so that a placement exists here exactly when
   `DlxMatrix::prepare` or `Assembler1::prepare` would make its row. An
   anchor can be removed (reduce).
3. **Fits and reach.** `fits(free, table)`, `reach(free, tables,
   remaining parts)`, and the three ends of a branch: a dead filled
   voxel, more dead variable voxels than a given number, a part with no
   fit. A result's filled and variable voxels are two cell sets.
4. **Scalar first.** Plain 64-bit words; the run steps by doubling. A
   vector path is a later change with its own switch (AGENTS.md "SIMD and
   platform code").
5. **Tests** (`tests/unit/solve/test_reach_map.cpp`):
   - against a brute-force reference (every placement tested cell by
     cell) on random free sets: every brick fixture, the hexagonal and
     Cairo fixtures of `tests/data/puzzles/new-grids/`, a sphere, a
     rhombic, a tetra-octa and a triangular-prism fixture, a tilted tray,
     results with empty cells inside their box, results 1, 64, 65 and
     129 cells wide (the word edges), three layers deep;
   - the anchor sets hold exactly the placements of assembler 0's matrix
     (`DlxMatrix::placements`) for every fixture with one piece a shape,
     with and without strict colors;
   - a result whose line does not fill its last word: no cell past the
     line is ever free or reached.
6. **A number to hold.** On `SquareDissection-corners`: one reach of all
   18 orientations in at most 25 microseconds (the prototype's node is
   12). Measured by a benchmark case, not asserted in a test.
7. **Gates.** `just test-all`, `just test-asan`, `just check`.

## Stage B: assembler 2 on one thread

1. **The shared symmetry code.** Take the choice of the symmetry-breaker
   shape and the mirror structure out of `DlxMatrix::prepare` and
   `Assembler1::prepare` into one function (same answers: the oracle and
   the unit suite say so before anything else is written).
2. **`Assembler2::createMatrix`.** The unit checks of the other two
   (`TooManyUnits`, `TooFewUnits`, `CannotPlace`), the holes allowed, the
   shape tables of stage A, the symmetry breaker's orientations left out
   as the others leave them out, the `RotationFilter` request. In this
   stage: every part exactly one piece (`everyPartOnePiece`), not
   sliding; otherwise `PuzzleUnhandable`.
3. **`assemble`.** The search of "The two ideas", as a loop over an
   explicit stack (no recursion: it must stop anywhere and be saved in
   stage D): the free set and the holes left per level, the decision
   taken, the decisions still to try. It polls the stop token, a
   callback answering false stops it, the next call carries on (decision
   0009). `iterations()` counts nodes.
4. **An assembly found.** All parts placed: the placements go to
   `RowRotationCheck` or `RotationFilter::rejects` and then to
   `SearchRun::deliver`, as `Assembler0::solution` does.
5. **The order of the branches** is fixed and written down: the parts in
   their order, each part's transformations in theirs, "empty" last.
6. **The debugger and the placement browser.** `currentAssembly()`,
   `debugStep`, `piecePlacement*` (the app's placement browser reads
   them): the first two from the stack, the browser from the anchor
   sets.
7. **The CLI.** `--assembler 0|1|2` among the switches for comparisons:
   takes the named assembler where it can handle the problem (an error
   where it cannot). Nothing chooses assembler 2 by itself yet.
8. **Tests** (`tests/unit/solve/test_assembler_2.cpp`):
   - for every fixture with one piece a shape: the multiset of
     assemblies equals assembler 0's (reduced and not), with a symmetry
     breaker (`Bermuda.xmpuzzle`) and without, with holes (the fixtures
     of test_assembler.cpp's vector-and-plain case), with colors, on the
     new grids;
   - the rectangle and block trays of the decision-0048 tests
     (`rectanglesInTray`, `blocksInTray`), filled and variable, with a
     column of spare cells;
   - a small cut-corner puzzle made by a script (`tests/data/puzzles/`,
     about 30 x 30, cut 2), its assemblies equal to assembler 0's, which
     can still finish it;
   - a stop after each of the first assemblies and a carry-on: nothing
     lost, nothing twice;
   - `[slow]`: `SquareDissection-corners` (as a fixture only with the
     user's leave; otherwise a puzzle of the same build with other
     sizes) gives a first assembly that is a valid assembly.
9. **The scripts.** `compare.py` with `--assembler 2` against assembler
   0: the same number of assemblies on every random puzzle (the output
   differs in iterations, by design).
10. **What it must do when done.** `shapelogic_cli --assembler 2 -q` on
    `SquareDissection-corners` prints its first assembly in under a
    minute on one thread; the static and variable versions say "0
    assemblies" in seconds.
11. **Gates.** `just test-all`, `just test-asan`, `just oracle` (fast,
    full; all threads and `--threads 1`; both take-apart solvers),
    `just oracle-files`, `just oracle-text`, `just check`.

## Stage C: reduce

1. **`Assembler2::reduce`.** For each placement: clear its cells from
   the full result, compute the reach of the other parts, and remove its
   anchor when a filled voxel is dead, more variable voxels are dead than
   holes are allowed, or another part has no fit. Passes until one
   removes nothing. The counts of `PrepareCounters` as the others write
   them (the app shows "Optimize piece 4 of 9, pass 2: …").
2. **The same check for assembler 0.** Its placement pass
   (`DlxMatrix::reduce`, after the sweep) asks the same question of the
   same rows: a header column or too many variable columns left without
   a row. Answer it from the reach bitmaps (anchor sets kept in step
   with the rows removed), and remove the rows as now. The claim to
   prove: **the reduced matrix is the same** (the rule is the same and
   it runs until nothing goes, the argument of decision 0048). The cover
   pass stays behind a CLI switch (`--cover-reduce`) to compare.
3. **Proof by machine.** `compare.py`: the old build against the new on
   600 random puzzles and every fixture and example, everything printed
   equal (rows and columns removed, assemblies, iterations). The oracle
   as in stage B. Pause in one build and carry on in the other (the
   scratch program of decision 0048's check did this for assembler 1).
4. **Numbers to report in the handoff.** `SquareDissection-corners`,
   assembler 0: the first pass today is 17 minutes; expected a few
   seconds a pass. The default bench set must not get slower
   (`tests/bench/bench.py`).
5. **Not here.** Assembler 1's reduce asks another question (weights,
   ranges, and it counts no holes); it stays as it is.
6. **Gates.** As stage B, and the benchmark.

## Stage D: pause, save and carry on

1. **The position.** `savePosition`: a flag and the decisions of the
   stack, each a part, a transformation and a move, or "empty", and per
   level how many branches are done (for `finished()`). Its version
   string is assembler 2's own. `restorePosition`: every decision
   checked before it is used (the part is free, the placement exists and
   fits, the cell is the level's cell), as decision 0009 asks of a
   damaged position.
2. **Older versions must refuse it.** Assembler 0 of the versions up to
   2.0 refuses another version string. Assembler 1 does not look at the
   version: the text must start with a flag it refuses (above 6, as
   decision 0048's does). Test both with the oldest binary at hand, or
   say in the handoff that it was not tested.
3. **Choosing by the position.** `chooseAssembler` takes the puzzle as
   well and, for a problem that holds a position, answers the assembler
   whose position it is. `makeAssembler`, the session
   (`SolveSession::prepareSearch`), the CLI (`cli/main.cpp`), the Python
   module (`python/src/solution_iterator.cpp`) and the app's explorers
   (`core/shapelogic/app/explorers.cpp`) call one or the other; grep for
   every caller.
4. **Progress.** `finished()` from the stack (the share of each level's
   branches done), read from any thread; the session's progress
   (`SolveProgress`) needs nothing new.
5. **Tests.** The pause cases of `test_solve_session.cpp` on a problem
   assembler 2 gets (forced by an option of the test until stage G);
   pauses at random steps, in memory and through a saved file; a damaged
   position refused; a problem with an assembler 0 position carried on
   by assembler 0 although the rule would give it to assembler 2.
6. **Gates.** As stage B, and `just oracle-resume` (it needs the
   original's library built under `../burr-tools/build`; if it is not,
   say so and do not build it unasked).

## Stage E: several threads

1. **Tasks.** The first levels of the tree, until there are enough
   branches (`splitTaskTarget`), each a prefix of decisions; a worker's
   state is a free set and a few counters, copied in microseconds (the
   dancing links searches copy the whole matrix). The split state, the
   pool and the order of delivery are the run layer's
   (`subtree_split.h`, `SplitSearchState`, `ThreadPool`).
2. **A split search stopped.** Saved as its tasks, each a list of
   decisions, and carried on as it ran whatever the thread count is then
   (the rule of `SearchOptions`).
3. **Tests.** `[parallel]`: the multiset on 1, 2, 4 and all threads; a
   puzzle with a symmetry breaker among them (AGENTS.md says why);
   random pauses of a split search. `just test-tsan`.
4. **Gates.** As stage B with `just test-tsan`, and the user's puzzle on
   all threads: the time to its first assembly, reported.

## Stage F: several pieces of a shape, and ranges

1. **Counts.** Each part has a number of pieces between its minimum and
   maximum; a branch at a cell names a part that still has pieces, and
   the count goes down. Copies are numbered in the order they are
   placed.
2. **The end.** With ranges an assembly is: every part at least at its
   minimum, no filled voxel free, and no more empty variable voxels than
   the holes allowed (`getMaxHoles`, or any number when it is not set),
   as assembler 1 counts them. "A part with no fit ends the branch"
   holds only for a part still below its minimum.
3. **No symmetry breaker** with several pieces of the chosen shape or
   with ranges, as in assembler 1: every assembly is checked against its
   rotations.
4. **Tests.** The multisets of assembler 1 on its fixtures
   (`SolidSixPieceBurrs` is `[slow]`), `blocksInTray`, the L-tetromino
   trays of `tmp/cell-search/puzzles/`; `compare2.py` with
   `--assembler 2`.
5. **Gates.** As stage B; `tests/bench/bench.py --set identical`.

## Stage G: chosen by itself

1. **Benchmarks first.** A set `holes` in `tests/bench/bench.py`,
   written by the script: rectangles with cut corners in trays of 20 to
   65 cells a side, polyominoes in trays with 1, 5 and 20 spare cells,
   the hole fixtures there are. For each: assembler 0 or 1 against
   assembler 2, the time to the first assembly and the time to the end
   where there is one, on 1 and all threads.
2. **The rule.** Start from decision 7 of "Decisions to keep to" and
   move its line only where the table says so. Write the table and the
   rule into the decision record. The rule reads the problem only.
3. **The engine and the names.** `AssemblerKind::Assembler2` through
   `assemblerName`, the CLI's "using assembler …" line,
   `api/types.json` if the kind is in it, `just app-gen`, the fake
   engine's recordings (`npm run record` in `web/`), `docs/API.md`
   (`python3 api/gen_api_md.py`).
4. **The front end.** Nothing new to set. Where the Solver names the
   assembler or its steps, the wording for assembler 2; the Optimize
   line as now. The user's puzzle solved in the app, with Pause and
   Continue.
5. **The guide and the records.** `solver-tab.md` (what happens with a
   result that has cells to spare), `command-line.md` (`--assembler`,
   `--cover-reduce`), `docs/ARCHITECTURE.md`, a decision record (the
   next free number), `RELEASE_NOTES.md` when it ships.
6. **Gates.** The C++ gates of stage B, `just app-check`, `just
   app-test`, the e2e specs that solve (not the whole suite unasked).

## Stage H: stronger bounds

Research, with the prototype and stage B to measure on. The question:
can a search with many holes to spare be made to end, or at least to
find its assemblies sooner whichever branch comes first? Ideas to try, in
the order of their cost:

1. **The reduce's knowledge inside the search.** A placement that fits
   but leaves more dead cells than holes are left does not count as
   reaching its cells. One more round of reach per node.
2. **Another cell to branch on.** Not the first free cell but the one
   with the fewest fitting placements over it, every placement over it a
   branch (not only those starting there), plus "empty". More branches a
   node, fewer wasted.
3. **Regions.** Free cells cut off from the rest hold a known number of
   cells; a region none of the remaining pieces fits is all holes at
   once (the reach count finds that) and one that fits only some limits
   which pieces go where.
4. **What the puzzle's own geometry says** (boxes that may only overlap
   at corners) is not for the engine: it knows cells, not rectangles.

Done when one of them ends `SquareDissection-corners` or the cut
`grid65`, or when the handoff says with numbers that none did. Nothing of
it changes which assemblies there are.

## Open questions

- **Does the search of the user's third puzzle end at all in a useful
  time?** Not known. The prototype found 51 packings in 15 minutes and
  was not through.
- **Where the rule's line belongs.** Stage G's table decides; until then
  the narrow rule stands.
- **Results in three dimensions with holes.** The method does not care,
  but nothing was measured: the prototype is flat.
- **The user's puzzle as a fixture.** It is the user's design. Ask
  before it goes into `tests/data/` or the site's examples.

## Not in this plan
- Sliding and stacking puzzles (decision 6).
- A vector (SIMD) path for the reach bitmaps: after stage G, if the
  benchmarks ask for it.
- Assembler 1's reduce with holes allowed (decision 0048, "What was not
  done").
- A cut-off ("stop after the first assembly") in the app beyond what the
  Solver's Limit already does.

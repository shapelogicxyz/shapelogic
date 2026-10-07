# Handoff: stage 3b (assembler 1 and BurrTools 2)

**Date:** 2026-10-07 · **Next:** a separate audit of all of stage 3
(recommended by PLAN.md), then stage 4 (disassembly) in a new session at
extra high effort. The brief is at the end.

## Done
- **Assembler 1** (`core/bt/solve/assembler_1.{h,cpp}`, port of
  `assembler_1_c`, version "2.1"): the matrix with weights, minima and
  maxima (piece counts, ranges with the range column, holes), `reduce`,
  `clumpify`, the plain search (`iterative`, a state machine that stops
  only where it can be saved), the split search (`generateTasksAtDepth`,
  `generateSubtreeTasks`, the worker) through `runTasks()` and
  `SplitSearchState<SearchStacks>`, the vector search (`SimdHuangCover`,
  alone or in each task), saved positions with flags 0, 2 (older row-order
  signatures), 3 and 4, byte for byte. The memory budget of the vector
  search is `SearchOptions::vectorMemoryBudget`
  (`BURRTOOLS_HUANG_MEM_MB`); `makeSimdHuangCover`,
  `simdHuangMemoryEstimate` and `simdHuangFitsMemory` are in
  `simd_huang_cover.h`.
- **BurrTools 2**: `dancing_cells.{h,cpp}` (`DancingCells`, port of
  `bt2Cells_c`), `assembler_bt2.{h,cpp}` (`AssemblerBt2`, port of
  `assembler_bt2_c`, version "2.0-bt2"), `bt2Threads()` (port of
  `bt2ChooseAssemblerWorkers`), and the run layer's second driver
  `runWorkStealing()` in `work_stealing.h` (port of `bt2Assemble`), over the
  `WorkStealingSearch` concept (decision 0009, now complete).
- **Shared parts:** `dlx_matrix.{h,cpp}` (`DlxMatrix`, `DlxNodes`: assembler
  0's matrix, factored out of `Assembler0` and used by BT2 too; the
  original's BT2 had a copy with a brute-force placement scan and an older
  clumpify, which give the same matrix), `rotation_filter.h`
  (`RotationFilter`: the rotation/mirror check all three assemblers had a
  copy of), `SearchRun::deliverFound` (a hand-over for searches that never
  find an assembly twice). `Assembler0` now uses `DlxMatrix` and
  `RotationFilter`; its saved texts, iteration counts and speed are
  unchanged.
- `makeAssembler()` returns all three; `bt` solves every problem (`--solver
  bt2` uses BT2 with `bt2Threads(-t)`; `--vector-memory MB`; exit code 3 is
  gone).
- **Harness:** `oracle.py baseline|compare --solver bt2` (the original's own
  BT2 answers in `baseline/<tier>-bt2.json`; they equal its Classic answers
  on all 180 puzzles); `resume-check` with assembler 1 puzzles (Cube in Cage,
  Demo Piece Generator, Solid Six Piece Burrs) and BT2 modes; both resume
  tools take a solver (`resume_old.cpp` runs BT2 over threads through
  `bt2Assemble`); `record-positions` with a solver column, assembler 1 and
  BT2 positions. `bench.py` has Solid Six Piece Burrs, Demo Piece Generator,
  CornerCube and a BT2 row.
- **Tests:** the original's assembler-1 and BT2 cases of `test_solver.cpp`
  (`test_assembler_1.cpp`, `test_assembler_bt2.cpp`: the seven "Parallel
  assembler 1" cases with Demo Mirror Paradox problem 1 as the
  symmetry-breaker case, the stopped-search resumes, the older flag 2
  position, the memory budget, the vector/plain equivalence rows, the
  pentomino counts with BT2, BT2 stop and continue), and new: random pauses
  on assembler 1 and BT2 (`randomPauses` moved to `solve_test_helpers.h`),
  stop before start, progress read from another thread, exceptions, saved
  positions through the puzzle file (flags 0, 3, 4), damaged positions
  (hand-made and random), BT2 positions and `beginSolving`'s rule, dancing
  cells (Knuth's example, a split at every point finds every cover once,
  stops), the work-stealing driver with a made-up search
  (`test_work_stealing.cpp`). `test_positions.cpp` reads the new recording.
  Fixtures added: CubeInCage, DemoMirrorParadox, PiecesOfEight,
  DemoPieceGenerator, AlPackino, 12PieceSeparation, FourPieceTetrahedron
  (model dump and saved-file recordings re-recorded: additions only).

## Gate results
| Check | Result |
|---|---|
| `just test-all` (dev, warnings as errors) | pass, 272 cases (264 quick, 8 `[slow]`); the quick suite also ten times in a row after the work-stealing fix (difference 9) |
| `just test-asan` (address + undefined) | pass |
| `just test-tsan` | pass, 50 `[parallel]` cases (2745 assertions), with the symmetry-breaker cases of all three assemblers (Bermuda for assembler 0 and BT2, Demo Mirror Paradox problem 1 for assembler 1) |
| `just oracle --fields assemblies`, fast + full + slow, Classic | **180/180** with all threads, **180/180** with `--threads 1`; no `--allow-unported` |
| `just oracle --fields assemblies --solver bt2`, fast + full + slow | **180/180** with all threads, **180/180** with `--threads 1` (against the original's own BT2 baselines) |
| `just oracle-resume` | **395/395**: assembler 0 and 1 positions paused by either program continue in the other (plain, vector, split with and without vector); 76 single-thread pauses saved the same text in both (assembler 0, assembler 1, BT2); 5 pauses after the last piece continued by the new program only (stage 3a's known bug of the original); 84 BT2 positions refused by the new program, as they should be (difference 10) |
| `just oracle-roundtrip` | **365/365** the same XML, **365/365** byte-identical (unchanged) |
| model dump / grid dump vs. the original | identical (model dump re-recorded with the seven new fixtures: additions only) |
| speed (`tests/bench/bench.py`, back to back, quiet machine) | 0.93 to 1.04 of the original on every row, assembler 1 and BT2 included (tests/bench/README.md) |
| `just check` | **not run: LLVM still not installed** |

## Faithfulness beyond the gates
- **Iteration counts:** on all 180 corpus puzzles, one thread, the plain
  search, the vector search and BT2, each with and without reduce: the same
  assembly count and the same iteration count as the original (1080 of
  1080). For assembler 1 that covers its plain and vector searches, for BT2
  that the shared matrix is node for node the original's and the dancing
  cells search is the same.
- **Saved texts:** single-thread pauses of assembler 1 (flags 0 and 3) and
  of BT2 save byte-identical text in both programs.

## Deliberate differences from the original
Assembler 1:
1. **Progress without a lock.** The original's `getFinished()` read
   `finished_a` / `finished_b` under a mutex while the search pushed and
   popped them; the plain search now publishes its fraction every 4096
   steps, at the end of a run and after a restore.
2. **Saved positions are checked before use** (parsed into temporaries;
   stacks, columns, rows, hidden-row batches, progress entries and every
   task checked against the matrix; a split or vector search must stand at
   its start; counts bounded by the text). The original replayed whatever
   it read, out of bounds on a damaged file. Valid positions read the same.
3. **A stopped vector search always continues as one**, also when the next
   run asks for no vector search or less memory (as assembler 0 since 3a);
   the original fell back to another search from the start and reported
   again.
4. **A split search with nothing left** (no tasks, or all done, and not
   stopped) clears its stacks, so `assemble()` returns `Finished`; the
   original only said so through `getFinished() >= 1`.
5. **Nothing is printed:** "range optimisation used min .., max ..",
   "removed .. rows and .. columns" (`reduce()` returns them) and the debug
   traces of `iterative()`.
6. **Threads** from the pool through `runTasks()`; shapes warmed before
   every split search (the original only with the rotation filter on);
   iterations flushed per task. Environment switches are options (see
   decision 0009).
7. Kept as the original: the version of a saved position is not checked;
   the three copies of the search state machine (plain, worker, task
   generation); the plain search hands over assemblies it reaches between a
   pause and the next state it can save (they are not found again).
8. Not ported: `printMatrix`, the recursive `rec()` (under `#if 0`),
   `canPlace` (the placement finder replaced it), `simdCompleted` (unused).

BurrTools 2:
9. **A lost wake-up in the work stealing is fixed.** In `bt2Assemble`, a
   thread that came to wait for work, found the run stopped and left did
   not wake the threads already waiting; a stop comes from another thread
   and notifies nobody. A pause that landed while the last busy thread
   finished its branch left the others asleep and the solve hung. Found by
   the random-pause test (two hangs in about 13 runs of the quick suite),
   reproduced exactly by `test_work_stealing.cpp`; a thread leaving that way
   now wakes the others. **Worth fixing in the original.**
10. **Saved positions of a started search are refused.** The original saves
    BT2's search as an unmoved dancing links position (`0 <iterations> (0
    0)`) and, restoring it, starts the search again while the problem keeps
    what was found: every assembly found before the pause comes twice. The
    new core writes the same text but restores only `0 0 (0 0)`; anything
    else is `CannotRestoreInterrupted`, so `beginSolving()` drops the
    partial results and the solve starts clean. Pause and continue in memory
    loses nothing (parked branches). **Worth fixing in the original.**
11. **Branches are small objects** (a dancing cells search and its progress
    share), not copies of the assembler: no `clonePrepared`,
    `addProgressPeer`, root pointer or stop flag. The driver is in the run
    layer and takes the pool's threads; lent threads count as taking part
    once they start (the pool may run two lent jobs on one thread one after
    the other).
12. Assemblies other threads find after a pause are handed over, as in the
    original (their branch has gone past them); after an abort they are
    dropped (`deliverFound`).
13. The matrix is `DlxMatrix`: `PlacementFinder` instead of the
    brute-force `canPlace` scan and the hashed `clumpify` instead of the
    pairwise one; the same matrix (iteration counts equal everywhere).
14. Not ported: the dancing links search of `assembler_bt2_c`
    (`iterativeMultiSearch`, never called), `reinsert_row`,
    `remainingSearchWork`, `clearProgressPeers` (unused).
15. `debugStep(0)` does nothing (the original's budget 0 meant "no limit"
    and ran the whole search).

Elsewhere:
16. The stage-3a thread-pool test expected four distinct threads for four
    jobs; the pool's threads take lent jobs from one queue, so a thread done
    with a short job may take the next. The test now expects the caller and
    at least one pool thread (it failed about one run in a few dozen).

## Known issues and notes
- **The original's bugs found here** (both worth fixing there): the lost
  wake-up in `bt2Assemble` (difference 9) and BT2 positions that report
  everything twice (difference 10); and, from 3a, the plain search's
  position after the last piece.
- **BT2 cannot continue a saved search.** Stage 5's session should say so
  (an autosave of a BT2 solve only helps in memory; after loading, the
  solve starts over). Saving the dancing cells state would need a new
  format both programs read (decision 0006); not planned.
- **For the session (stage 5):** BT2 takes its thread count from
  `bt2Threads()`, not the budget as it is; assemblies may arrive after the
  session asked for a pause (assembler 1's plain search, BT2's other
  threads) and must be kept; `initHotspot` placement (see stage 3a's note).
- Assembler 1's three copies of the search state machine could become one
  in a later reshaping pass, now that the iteration counts pin them down.
- The AVX2/AVX-512 kernels are still untested on x86-64 (this machine is
  arm64).
- LLVM is still missing: nothing from stages 0 to 3b has been through
  clang-tidy.
- Burr-Glar on one thread is still behind the original (stage 3a's note);
  stage 8.

## Brief for stage 4 (disassembly)

Work in `/Users/colin/git/burr-tools-rotations/burrtools_alpha`; the original
is `../burrtools_rotations`. Read AGENTS.md, docs/ARCHITECTURE.md (the
`solve` and `model` modules), docs/PLAN.md (stage 4), decisions 0004, 0006,
0007 and 0009, and this note. Effort: extra high. Before starting, PLAN.md
recommends a separate high-effort audit of all of stage 3 (the diffs of
3a and 3b, the gate results above, the deliberate differences); if the user
has had one done, apply its findings first.

**Scope.** Port faithfully, onto the rules of decisions 0004 and 0009:
`disassembler.h` (the interface and `disassemblyProgress_c`),
`disassembler_a` (the level-by-level search, spread over threads with the
same result as on one), `disassembler_0` (Classic and BurrTools 2),
`disassembler_crowell` with `crowell_solver.h`, `disassembler_factory`
(Crowell gets its own, the others `disassembler_0`), `disassemblernode`,
`disassemblerhashes`, `movementcache` with `_0` (cubes) and `_1` (prisms),
`movementanalysator`, `grouping`, `rotationrules`, `rotationmoves_0` and
`rotationmoves_crowell`, `disasmtomoves` (stage 6 generalises it into the
playback sampler). About 7900 lines; the model side (`Separation`,
`SeparationInfo`, `Disassembly`, `movesText()`) exists since stage 2.

**What the rules change** (record them as deliberate differences):
- The disassembler's `abort` flag, `stop()` and the analysator's
  `setStopFlag` become a `StopToken` per take-apart; a part that must end
  its own work makes a `StopSource` from it (decision 0009).
- `setHelperPool(helperPool_c*)` becomes the solve's `ThreadPool`, passed
  in; `levelThreadCostUs` (a process-wide atomic in `disassembler_a.cpp`)
  becomes per disassembler or an option.
- Environment switches become option fields: `BURRTOOLS_NO_DISASM_PAR`,
  `BURRTOOLS_NO_DISASM_FAST`, `BURRTOOLS_NO_ROT_FAST`,
  `BURRTOOLS_NO_ROT_ANCHOR`; `BT_ROT_DEBUG` / `BT_ROT_DUMP` print, so they
  go (or become a callback).
- Progress (`getProgress`, atomics) stays readable from any thread.

**CLI:** `bt -d` / `-R` take apart as burrTxt's `asm_cb` does (each
assembly in the callback, `src/burrTxt.cpp` around line 212; the best level
by its `dotlevel` rules; `print_json_result`): solutions, `dotlevel`,
`level`, `totalmoves`, and with `-R` `moves` and `rotations`. The grids
without take-apart (the corpus's `A` puzzles) keep solutions 0 and null
levels. `--solver crowell` must use Crowell's take-apart.

**Harness:** `just oracle` on every field (no `--fields`), all tiers, all
threads and `--threads 1`, for `classic` and `bt2`; add `crowell` to
`--solver` and record its baselines (`oracle-baseline --solver crowell`).
Write the file-level comparison (`oracle.py`): `baseline-files` already
makes the original's solved files with burrTxt2 under `work/old`; the new
side needs a way to write a solved file (a `tests/tools` program or a `bt`
option), and the comparison loads both with the new core and compares
solution by solution (assembly, separation tree, `movesText()`, levels).
Find out whether burrTxt2's solution order depends on threads before
choosing between sequence and set. Mind `initHotspot`: burrTxt calls it
before `createMatrix`, the solve thread (which burrTxt2 uses) after
`setAssembler` (`solvethread.cpp` line 331); assembly positions in the
files depend on it.

**Tests:** port `test/test_disassembly.cpp` (nodes, hashes, the separation
trees of Cube in Cage and Prisgon problem 1, `disasmToMoves_c`, the movement
caches), `test/test_rotation_solvers.cpp` (EnigmaTIC with rotation checks:
Classic, Crowell and BurrTools 2 agree; the rotation rules; a turned scene;
a take-apart over threads equal to one thread; progress and stop. "Solve
progress" is stage 5's) and `test/test_grouping.cpp`. The `[parallel]`
cases must include a take-apart that really spreads over threads, for TSan.
Never use the sliding puzzle "One Way or Another" (copyrighted);
`test/test_sliding_solver.xmpuzzle` stands in where a sliding puzzle is
needed.

Finish with the "Finishing a stage" steps of AGENTS.md, write
`docs/handoff/stage-4.md` with the brief for stage 5, tick stage 4 in
PLAN.md, one commit `Stage 4: ...`. PLAN.md recommends an audit after
stage 4 as well.

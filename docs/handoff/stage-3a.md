# Handoff: stage 3a (the assembler run layer and assembler 0)

**Date:** 2026-10-06 · **Next:** stage 3b (assembler 1 and BurrTools 2), in
a new session at extra high effort. The brief is at the end.

## Done
- **Decision record 0009** (`docs/decisions/0009-assembler-run-layer.md`):
  one run layer for every assembler, one thread pool per solve, A/B
  switches as options. ARCHITECTURE.md describes the `solve` module.
- `core/shapelogic/base/thread_pool.h`: `ThreadPool` (port of `helperPool_c`:
  `run(n, job)` on the caller plus lent threads, `addLoad`, nesting without
  deadlock), `kMaxThreads`, `hardwareThreads()`, `splitThreads()`. No
  process-wide limit, no `SHAPELOGIC_THREADS`. `stop.h` gained
  `StopSource(const StopToken& parent)`.
- `core/shapelogic/solve/`:
  - `search_run.h`: `SearchRun` (a run's own stop source under the
    caller's token; serialised hand-over; `deliverOnce` with signatures),
    `TaskProgress`, `runTasks()`, `effectiveThreads()`.
  - `resume_state.h`: `SplitSearchState<Task>` and the saved-position tail
    (port of `assembler_resume.h`, byte-compatible).
  - `assembler.h`: the `Assembler` interface (`createMatrix`, `reduce`,
    `assemble -> RunEnd`, `finished`, `iterations`, `savePosition`,
    `restorePosition`, placement browser, debugger), `AssemblerError`,
    `MatrixOptions`, `SearchOptions`, and the problem hooks `beginSolving`
    (the original's restore failure rule), `storePosition`,
    `canStartSolving`, `warmShapeCaches`.
  - `assembler_0.h`: `Assembler0`, the port of `assembler_0_c` with its
    plain, split and vector searches.
  - `placement_finder.h`, `simd_bitset.h`, `simd_exact_cover.h`,
    `simd_huang_cover.h` (the Huang cover is ported and tested; assembler 1
    uses it in 3b), `solver_type.h` (`SolverType`, `chooseAssembler`,
    `makeAssembler`).
- `Problem::setAssemblerState()` / `clearAssemblerState()`.
- **`bt` solves** (`cli/main.cpp`): `--json` (implies `-d -q -r` as in
  burrTxt), `-q -n -r -C -d -R -o -t --solver --no-simd --version`.
  Assemblies only: solutions are 0 and levels null until stage 4. Exit code
  3 when the problem needs assembler 1 or BT2.
- **Harness:** `oracle.py compare --fields --threads --allow-unported`;
  `oracle.py resume-check` (`just oracle-resume`; `resume_old.cpp` against
  the original, `bt_resume` here); `oracle.py record-positions`
  (`tests/data/positions_by_original.txt`). `tests/bench/bench.py` (speed,
  back to back).
- **Tests** (Catch2): the original's `test_simd_exact_cover.cpp` (all of
  it, Huang cover included) and the assembler-0 cases of `test_solver.cpp`
  (regression counts, callback stop, serial vs split as multisets, pause
  and continue, restore of a fresh position, thread clamp, the Bermuda
  symmetry-breaker case under TSan, stop-and-resume saved or not, vector
  vs plain search, pentominoes). New: thread pool, stop source from a
  token, a stop before the run starts, random pauses (from the callback
  and from another thread) on all four paths with every other continue
  through a saved position, progress read from another thread, an
  exception in the search, `beginSolving`'s failure rule (flag 1
  interrupted, wrong version, damaged text, a position on a problem not
  being solved), a paused solve through the puzzle writer and reader, a
  finished position, `createMatrix` errors, placements and debugger, the
  original's recorded positions (`test_positions.cpp`). Fixtures added:
  DraculasDentalDesaster, Bermuda, AugmentedSecondStellation (the model
  dump and saved-file recordings were re-recorded to cover them; only
  additions).
- **Faithfulness beyond counts:** the deterministic searches give the
  original's iteration counts exactly (plain and vector search, with and
  without reduce, on every example assembler 0 handles), and single-thread
  pauses save byte-identical positions.

## Gate results
| Check | Result |
|---|---|
| `just test-all` (dev, warnings as errors) | pass, 228 cases (226 quick, 2 `[slow]`) |
| `just test-asan` (address + undefined) | pass |
| `just test-tsan` | pass, 21 `[parallel]` cases (983 assertions), the Bermuda symmetry-breaker case and the random pauses on 6 x 10 pentominoes included |
| `just oracle --fields assemblies --allow-unported`, fast + full + slow | **161/161** match with all threads, **161/161** with `--threads 1`; 19 need assembler 1 (3b) |
| `just oracle-resume` | **200/200**: positions paused by either program continue in the other (plain, vector, split with and without vector); 37 single-thread pauses saved the same text in both; 5 pauses after the last piece continued by the new program only (known bug of the original, below) |
| `just oracle-roundtrip` | **365/365** the same XML, **365/365** byte-identical (unchanged) |
| model dump / grid dump vs. the original | identical (model dump re-recorded with the three new fixtures) |
| speed (`tests/bench/bench.py`, back to back, quiet machine) | default set 0.96-1.01 of the original, plain search 0.97-0.98; Burr-Glar 1.07 on one thread, 0.98 on ten (see tests/bench/README.md) |
| `just check` | **not run: LLVM still not installed** |

## Deliberate differences from the original
1. **No stop flag.** A run stops through its own source under the caller's
   token; nothing is reset. A stop made before a run starts is seen (the
   original lost it unless `armStop()` was called first). `armStop`,
   `beginRun`, `clearStop`, `stop()`, `stopped()` are gone.
2. **`assemble()` returns `RunEnd`**; completion is never read from
   `finished()` (the original's solve thread used `getFinished() >= 1`).
3. **Progress is race-free:** the plain search publishes its fraction every
   4096 steps and at the end of a run; the original's `getFinished()` read
   the search stack unsynchronised.
4. **An exception from the callback** stops the run before the hand-over
   lock is given up, so no other thread delivers anything after it; the
   original stopped only when the exception reached its worker loop.
5. **A plain search stopped right after placing the last piece**
   (`pos == pieces`) saves `(0 0)` as the last entry: the row arrays have
   one entry more. The original read past its arrays when saving (its
   checked build traps) and wrote past them when restoring such a text.
   This is the original's bug, accounted for in `just oracle-resume`.
6. **A saved position of a finished search** (`pos` past the last piece)
   restores as finished; the original read past its row list.
7. **A problem without pieces** ends after reporting the empty assembly;
   the original read `rows[UINT_MAX]`.
8. **A stopped vector search always continues as one,** even if the next
   run asks for no vector search; the original fell back to the plain
   search from the start and reported the skipped assemblies again.
9. **`beginSolving` on a position whose problem is not Solving** (only a
   hand-edited file) returns `CannotRestoreSyntax` and starts over; the
   original asserted.
10. **Threads:** a split search's threads are lent by the pool (fewer when
    it is busy) instead of always `workers - 1` new threads; the task split
    still follows the requested count. No process-wide limit and no
    `SHAPELOGIC_THREADS`; the CLI's `-t` sets the count and the pool size.
11. **Environment switches are options:** `SearchOptions::vectorSearch`
    (`SHAPELOGIC_NO_SIMD`) and `SimdKernels` (`NO_AVX2`, `NO_AVX512`,
    `NO_NEON`); the CLI has `--no-simd`. `SimdExactCover` reports its
    kernel too (the original only had it on the Huang cover).
12. **`reduce()` returns `ReduceStats`** instead of printing "removed ...
    rows and ... columns" to stderr.
13. `assemble()` after a failed `createMatrix()` is a `BT_ASSERT` (the
    original did nothing). Shapes are warmed before every split search (the
    original only with the rotation filter on). A split-search thread adds
    its iterations after each task instead of at the end (same totals).
14. Not ported: `canPlace` and `reinsert_row` (unused by assembler 0;
    `canPlace` is BT2's, 3b), `setNumThreads` / `getNumThreads` (now
    `SearchOptions::threads`), `problem_c::setAssembler` / `getAssembler`
    (the problem holds no assembler; `beginSolving` / `storePosition`).
15. Speed, no behaviour change: `Assembly` keeps a pointer to its grid's
    tables as `assembly_c` did, and the symmetry tables are `constinit`
    (the rotation checks called the lookup millions of times).

The stage-1 **`getHotspot` difference** does not reach the assemblers:
assembler 0 uses `Voxel::transform()`'s result while building the matrix,
and in `Assembly::transform()` a transformation the result cannot take
fails at the bounding box in the original too, so both return false. The
sphere, rhombic and tetra-octa examples match on the harness.

## Known issues and notes
- **The original cannot save** a plain-search position stopped right after
  the last piece (difference 5). Worth fixing in the original
  (`rows.assign(piecenumber + 1, 0)` in `assembler_0_c::createMatrix`).
- **Burr-Glar on one thread is 7% behind** the original (25.6 s vs 27.4 s);
  ten threads are at parity. What is left is spread over the model's
  checked accessors (`Problem::partShape`, `getPartMaximum`) and the voxel
  cache checks in the rotation check of each of the 3.1 million
  assemblies. Stage 8's performance pass; do not chase it in 3b.
- **A vector search stopped again and again sooner than it can re-find
  what it skipped never finishes** (it restarts from the beginning each
  time). The same in the original; a session (stage 5) should not pause
  and continue it in a tight loop.
- The `bt` CLI initialises hotspots (`initHotspot`) before solving as
  burrTxt does; the original's solve thread did it after building the
  matrix. Stage 5 must decide which the session follows (assembly
  positions depend on it) and keep it consistent with stage 4's file
  comparison.
- The AVX2/AVX-512 kernels are compiled and dispatched only on x86-64 and
  could not be run here (arm64); they are the original's code. A run on an
  x86-64 machine (or under Rosetta) is worth doing once.
- LLVM is still missing: nothing from stages 0 to 3a has been through
  clang-tidy.

## Brief for stage 3b (assembler 1 and BurrTools 2)

Work in `/Users/colin/git/burr-tools-rotations/burrtools_alpha`; the original
is `../burrtools_rotations`. Read AGENTS.md, docs/ARCHITECTURE.md (the
`solve` module), docs/PLAN.md (stage 3), decision 0009 and this note. Effort:
extra high.

**Scope.** Port onto the run layer, faithfully, the original's
`assembler_1.{h,cpp}` (3300 lines, version "2.1"), `bt2_dancingcells`,
`bt2_assemble`, `bt2_solver.h` and `assembler_bt2.{h,cpp}` (version
"2.0-bt2"); make `makeAssembler()` return them and let `bt --solver bt2`
use BT2. The Huang cover (`simd_huang_cover.h`) is ported and tested.

**Assembler 1** (ranges, multi-pieces; every problem assembler 0 cannot
take, 19 of the corpus):
- Its split search (`generateTasksAtDepth`, `generateSubtreeTasks`,
  `assemblerWorker_1`, tasks with `task_stack` / `next_row_stack` /
  `column_stack` / `rows` / `hidden_rows`) goes through `runTasks()` and
  `SplitSearchState`; its vector search (`SimdHuangCover`, `solveSubtree`
  with hidden rows) as assembler 0's does.
- Its saved positions: flag 0 (the stacks), 2 (older kind: signatures from
  rows in the plain search's order, `rowOrderSignatures`), 3 (vector skip
  count), 4 (signatures from the sorted assembly). Keep them byte for byte;
  the original test "a parallel search saved by an older version carries
  on" builds a flag 2 text by hand.
- `simdHuangMemoryEstimate` / `simdHuangFitsMemory` and
  `SHAPELOGIC_HUANG_MEM_MB` become a `SearchOptions` field (decision 0009).
- Its `getFinished()` reads `finished_a` / `finished_b` under a mutex while
  the search pushes and pops them: publish the fraction instead, as
  assembler 0's plain search does.
- `assemble()` resets the task progress and copies the base matrix
  (`base_left` ...) on every call; keep the order of those steps.

**BurrTools 2:**
- `assembler_bt2_c` builds the same matrix as assembler 0 but with the
  brute-force `canPlace` scan; `PlacementFinder` was written to give the
  same order. Factor the dancing-links matrix (`generateFirstRow`,
  `addPieceNode`, `addVoxelNode`, `prepare`, `reduce`, `clumpify`, the
  cover operations) out of `Assembler0` into a component both use, and
  check that BT2's matrix comes out node for node the same (iteration
  counts of the original's BT2 runs are a quick check).
- The work-stealing driver `bt2Assemble` becomes a second driver in the run
  layer beside `runTasks()` (decision 0009): same `SearchRun`, the pool's
  threads, the ready queue and parked branches, but no `stop()` /
  `clearStop()` / `addProgressPeer` on the assembler. Keep
  `bt2ChooseAssemblerWorkers`' policy (an explicit count, else hardware
  minus 2, at most 16) as a function the CLI uses.

**Tests:** the remaining assembler cases of `test/test_solver.cpp`: the
seven "Parallel assembler 1 ..." cases (CubeInCage; the symmetry-breaker
one is DemoMirrorParadox, problem 1 — keep it, AGENTS.md), "Assembler 1: a
stopped search resumes ...", "a parallel search saved by an older version
carries on", "the vector search is refused past its memory budget", the
assembler-1 rows of "vector and plain search find the same assemblies"
(PiecesOfEight, DemoPieceGenerator, AlPackino, 12PieceSeparation,
FourPieceTetrahedron, CubeInCage, DemoMirrorParadox, SolidSixPieceBurrs),
the pentomino counts with BT2 (2 and 2339) and "BurrTools 2: a search split
over threads stops and continues". Extend the random-pause test to
assembler 1 and BT2. The "Solver: ..." cases are stage 5.

**Harness:** `just oracle --fields assemblies` must pass on all three tiers
with all threads and with `--threads 1` **without** `--allow-unported`; add
`--solver bt2` runs (a `--solver` option for `compare`). Add assembler-1
puzzles (CubeInCage, SolidSixPieceBurrs, DemoPieceGenerator) and BT2 modes
to `oracle-resume` (`resume_old.cpp` picks the assembler with
`findAssembler`; give both tools a solver argument), and record assembler-1
positions in `positions_by_original.txt`. Bench: add CornerCube,
SolidSixPieceBurrs, DemoPieceGenerator and a BT2 row.

Finish with the "Finishing a stage" steps of AGENTS.md, write
`docs/handoff/stage-3b.md` with the brief for stage 4, tick stage 3 in
PLAN.md, one commit `Stage 3b: ...`. PLAN.md then recommends a separate
high-effort audit of all of stage 3 before stage 4.

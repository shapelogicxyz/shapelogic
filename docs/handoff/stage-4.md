# Handoff: stage 4 (disassembly)

**Date:** 2026-10-07 · **Next:** a separate audit of stage 4 (recommended by
PLAN.md after each solver stage), then stage 5 (the solve session) in a new
session at extra high effort. The brief is at the end.

## Done

### The audit of stage 3 (its own commit, 05088fd)
The audit's six findings (`tmp/audit/stage3report.txt`, local) are fixed,
each with a test that fails without the fix:
1. A plain search paused at its first level between two rows was taken for
   an unstarted one (assembler 0: `atStart()` needs `rows_[0] == 0`;
   assembler 1: `atRoot()` needs the first frame not begun and empty
   stacks).
2. A vector search stopped before its first report left its flag behind;
   the plain search clears it, assembler 0's all-tasks-done return drops
   its tasks.
3. Assembler 1's finished vector search keeps no rows in its position.
4. Assembler 0's `restorePosition` parses into temporaries and covers each
   level of the position, and of every saved task, only after checking it
   (`coverChecked`); a refused position leaves the assembler untouched.
5. Task counts and prefixes in a saved tail are bounded by the text and the
   pieces (`ResumeReader::remaining()`).
6. Assembler 1: a frame's row must be a row of its column; the replay of a
   restored position is checked step by step (`replayChecked`, a task's
   taken back by `undoReplay`): a column named twice had made the search
   loop for ever, found by the new search-after-damage test. The
   CannotRestoreInterrupted message names BurrTools 2. Findings 6b and 6c
   are for stage 5 (see the brief).

Decision 0009 says so. `.gitignore` is the one of `burrtools_rotations`
(with `/puzzles/` anchored, so `tests/data/puzzles/` stays tracked, and this
repository's own entries kept).

### The take-apart (`core/shapelogic/solve/`)
- `disassembler.{h,cpp}`: `Disassembler` (port of `disassembler_c`,
  `disassembler_a_c`, `disassembler_0_c`, `disassembler_crowell_c` and
  `createDisassembler`), `DisassemblerOptions`, `TakeApartResult`,
  `DisassemblyProgress`. The level search over three fronts, spread over the
  pool's threads with the same result as on one; the subproblems with
  `Grouping`.
- `disassembler_node.{h,cpp}` (`DisassemblerNode`, `NodeRef`),
  `node_hash.{h,cpp}` (`NodeHash`, `CountingNodeHash`).
- `movement_analysator.{h,cpp}` (`MovementAnalysator`: the fast and the
  original prepare/close, the bit-mask and the loop checkmovement, the state
  machine with merging, `completeFind`, `findMatching`, timing, stats).
- `movement_cache.{h,cpp}`, `movement_cache_bricks.cpp`,
  `movement_cache_prisms.cpp` (`makeMovementCache`).
- `rotation_rules.{h,cpp}` (`RotationRules`, `collectDebugConflictCells`
  included), `rotation_moves.h`, `rotation_moves_classic.{h,cpp}`,
  `rotation_moves_crowell.{h,cpp}`, `CROWELL.md` (the maintainer notes of
  `crowell_solver.h`, renamed).
- `grouping.{h,cpp}`, `disasm_to_moves.{h,cpp}` (`DisasmToMoves`,
  `FixedPositions`, `PiecePositions`).

### `bt`
`-d` takes each assembly apart in the callback as burrTxt's `asm_cb` did:
solutions, the best level by its dotlevel rules (highest level, then most
moves), `--json` with `dotlevel`, `level`, `totalmoves` and with `-R`
`moves` and `rotations`. Grids without take-apart report solutions 0 and
null levels. `--solver crowell` takes apart with Crowell's search. The
take-apart's levels spread over a pool of their own, as burrTxt's helper
pool. A/B switches: `--no-level-threads`, `--no-fast-analysis`,
`--no-fast-rotations`, `--no-rotation-anchor`.

### Harness
- `oracle.py --solver crowell`, baselines `baseline/<tier>-crowell.json`.
- `oracle.py baseline-files` now always restarts the solve (`burrTxt2 -R`),
  takes `--solver`; `compare-files` (new): `tests/tools/solve_file.cpp`
  (`bt_solve_file`) solves as burrTxt2 does, `tests/tools/solutions.cpp`
  (`bt_solutions`) reads both programs' files with the new core, and the
  saved solutions are compared one by one (see tests/oracle/README.md,
  "Solved files").
- `tests/bench/bench.py`: take-apart rows (`-rdR` on Climburr_OldVer,
  CoverUp3, CornerCube; `-rd` on Solid Six Piece Burrs).

### Tests
`test_disassembly.cpp` (the original's: nodes, hashes, Cube in Cage's and
Prisgon problem 1's trees, `DisasmToMoves`, both movement caches, the
hand-built trees for `movesText`; new: the shorter way kept, the scan
order, the hotspot order), `test_rotation_solvers.cpp` (the original's:
EnigmaTIC with all three solvers, the rotation rules' hash, the prepared
path, the turned scene, threads against one thread, progress and stop; new:
a stop from another thread, the A/B switches give the same trees,
`completeFind`/`findMatching`), `test_grouping.cpp` (the original's).

## Gate results
| Check | Result |
|---|---|
| `just test-all` (dev, warnings as errors) | pass, 336 cases (325 quick, 11 `[slow]`); the quick suite six times in a row after that (about 24 s each) |
| `just test-asan` (address + undefined) | pass |
| `just test-tsan` | pass, 56 `[parallel]` cases (3464 assertions), with take-aparts spread over four threads (Classic and Crowell, rotations on and off) and progress read from another thread |
| `just oracle` every field, fast + full + slow, Classic | **180/180** with all threads, **180/180** with `--threads 1` |
| `just oracle --solver bt2`, every field, all tiers | **180/180** with all threads, **180/180** with `--threads 1` |
| `just oracle --solver crowell`, every field, all tiers (new baselines `baseline/<tier>-crowell.json`) | **178/178** with all threads, **178/178** with `--threads 1`; InsomniAC1 and CoverUp3 not compared: the original's Crowell take-apart did not finish either within an hour |
| `oracle.py compare-files`, all tiers (new) | **180/180** solved files match burrTxt2's, Classic; **180/180** BurrTools 2 |
| `just oracle-resume` | **395/395** (unchanged; 76 single-thread pauses save the same text, 5 after the last piece continued by the new program only, 84 BurrTools 2 positions refused) |
| `just oracle-roundtrip` | **365/365** the same XML, **365/365** byte-identical (the original's solved files regenerated with `-R`) |
| speed (`tests/bench/bench.py`, best of 5) | the take-apart 0.93 to 1.02 of the original's time, the assembly rows 0.98 to 1.03 (tests/bench/README.md) |
| `just check` | **not run: LLVM still not installed** |

## Faithfulness beyond the gates
- **The rotation rules** give the original's answers bit for bit: the
  original's hash over 400 random scenes (3649067536594071595) is
  reproduced.
- **Trees:** every saved solution of the original's solved files (180
  corpus files, burrTxt2 `-R -d`, rotations for the TIC puzzles) is found in
  the new core's with the same separation tree, state by state.
- **Threads:** a take-apart spread over four threads gives the very same
  tree as on one (Classic and Crowell, with and without rotations); the A/B
  switches (`fast` analysis, `fast` turn rules) give the very same trees.
- **Groups:** BrainiAC1 (full tier) has a piece in two groups and matches.

## Deliberate differences from the original
1. **Stopping:** a `StopToken` per take-apart (`disassemble(assembly,
   stop)`), polled by the search, the analysators and the turn generators;
   no `stop()`, no `abort` flag, no `setStopFlag`. `TakeApartResult::stopped`
   tells a stopped take-apart from one that cannot come apart (the original
   returned null for both and the caller looked at its own flags).
2. **Threads:** `DisassemblerOptions::pool` (the solve's `ThreadPool`)
   instead of `setHelperPool`; `levelThreadCostUs` is per disassembler (the
   original's process-wide atomic, set by its tests); `levelThreads`
   (`SHAPELOGIC_NO_DISASM_PAR`).
3. **Environment switches are options:** `analysis.fast`
   (`SHAPELOGIC_NO_DISASM_FAST`), `analysis.rotations.fast`
   (`SHAPELOGIC_NO_ROT_FAST`), `analysis.rotations.anchor`
   (`SHAPELOGIC_NO_ROT_ANCHOR`). `BT_ROT_DEBUG` and `BT_ROT_DUMP` are gone
   (core/ does not print); `collectDebugConflictCells` stays for the GUI.
4. **One class:** `Disassembler` is `disassembler_c` (the interface),
   `disassembler_a_c`, `disassembler_0_c`, `disassembler_crowell_c` and the
   factory: the three differed only in the most new positions taken from a
   position (Crowell 100) and the turn generator, now chosen by
   `DisassemblerOptions::solver`.
5. **Nodes** are held by `NodeRef`, an intrusive counted reference, instead
   of `incRefCount`/`decRefCount` with `new`/`delete`. The count stays
   non-atomic: as in the original a node's count is touched by one thread at
   a time (each level thread expands its own position; what it found is put
   together after the threads are through). TSan agrees.
6. **The arc sweep's scratch** is a member of `RotationRules` (the original
   kept it `thread_local`); each rules object belongs to one thread, as
   before.
7. **Shape caches** are warmed when a disassembler is made (the level
   threads read the shapes through their movement caches).
8. The helpers that `rotationmoves_0.cpp` and `rotationmoves_crowell.cpp`
   each had a copy of (`rotateVector`, `rotateDoubled`,
   `rotationTransformId`) are shared.
9. `progress()` returns the struct (inactive when idle) instead of filling
   one and returning true; `completeFind` returns `NodeRef`s and drops
   duplicates at once.
10. `DisasmToMoves` keeps a copy of the tree as a value; the
    `PiecePositions` getters are const.
11. `bt`'s take-apart pool is separate from the assembly pool, as burrTxt's
    helper pool was (same results; the speed is stage 8's).

## Found in the original
- **The solve thread resets the shapes' hotspots after it made the
  matrix** (`solvethread.cpp` line 331). An assembly's positions are those
  of the hotspots the matrix was made with; the take-apart reads the shapes
  as they are. A shape with its hotspot elsewhere is then taken apart where
  it was not placed: Cube in Cage with one piece's hotspot moved finds 96
  assemblies and 0 solutions through the solve thread alone (the GUI's
  path), 1 through burrTxt and burrTxt2 (both reset the hotspots before; a
  probe against the original's library showed it). Only BallRoom in the
  corpus has a hotspot, and its grid has no take-apart, so the gates do not
  see it; a unit test pins it. **Worth fixing in the original.**
- **burrTxt2's saved solutions depend on the threads:** it keeps the ten
  best by moves, and of several with the same moves those its take-apart
  workers finished first (Solid Six Piece Burrs: `-t 1` and `-t 10` keep
  different ones, each the same run after run). `compare-files` checks that
  the original's are among the new core's, which keeps all of them.

## Known issues and notes
- **Crowell's InsomniAC1 and CoverUp3** run for more than an hour in the
  original (why was not looked into); the baselines record a timeout and `compare` skips what the original did not
  answer. The new program was not run on them.
- **`bt_solve_file`** stands in for the solve session: it follows burrTxt2
  and the solve thread for what the comparison needs (sorting by moves, the
  limit, the numbers), not the thinning of saved assemblies without `-d`
  nor anything about pausing.
- **The dev preset is -O0:** a rotation take-apart of a TIC puzzle is about
  twenty times slower there than in release; keep such work in tests small
  or `[slow]`. Catch splits a test name given on the command line at its
  commas: run such a case by a wildcard ("EnigmaTIC*").
- Unchanged from stage 3: the AVX2/AVX-512 kernels are untested on x86-64
  (this machine is arm64); LLVM is missing, so nothing has been through
  clang-tidy; Burr-Glar on one thread is behind the original (stage 8).
- `Separation` has no `operator==`; the tests and `bt_solutions` compare
  trees through a text of every state.

## Brief for stage 5 (the solve session)

Work in `/Users/colin/git/burr-tools-rotations/burrtools_alpha`; the original
is `../burrtools_rotations`. Read AGENTS.md, docs/ARCHITECTURE.md (the
`solve` and `model` modules), docs/PLAN.md (stage 5), decisions 0004, 0006,
0007 and 0009, this note and `docs/handoff/stage-3b.md` (its notes for the
session). Effort: extra high. If a separate audit of stage 4 has been done,
apply its findings first.

**Scope.** Replace `solvethread.{h,cpp}` (and what is left of
`helperpool`: the pool exists, `bt::ThreadPool`) with a `SolveSession`, one
per problem solve, onto the rules of decisions 0004 and 0009: the state
machine (`SolveState`, `base/solve_state.h`), preparation (matrix, reduce,
the saved position through `beginSolving`), the assembly run, the take-apart
queue and its workers (one `Disassembler` each, the levels over the same
pool, the load the assembly takes accounted with `addLoad`, the queue's
limit and the assembly waiting at a full queue), solution handling (the
solve thread's `processDisassembly`: sorted insertion by moves, level or
rotations, the limit of saved solutions and its dropping rule, the solution
and assembly numbers, `SeparationInfo` when disassemblies are dropped,
pending assemblies across a pause through `Problem::addPending`), stats
snapshots (`solveProgress_c`, `getProgressSnapshot`, the take-apart's
`DisassemblyProgress`), autosave hooks, error capture (an `InternalError`
becomes the Error state). Sliding and stacking stay out (decision of the
plan); their files must still load and save.

**Mind:**
- Reset the hotspots **before** the matrix (as burrTxt and burrTxt2), never
  after it (see "Found in the original"); make the disassemblers after the
  hotspots are set (a hotspot change clears the shape caches the
  disassembler warmed).
- `beginSolving` asserts on a Solving problem without a position (audit 6b):
  store the position before dropping an assembler, or check
  `canStartSolving` first.
- After an abort, BurrTools 2 re-enters the dancing cells frame whose
  callback threw, so a continued run reports that assembly again (audit 6c):
  only matters if a session continues after an abort; say what it does.
- BurrTools 2 cannot continue a saved search, and its saved position cannot
  say that its search is through (the original's format); a pause while the
  take-apart queue drains after a finished BurrTools 2 assembly must not
  lose what was found when the file is reloaded.
- Assemblies may arrive after a pause was asked for (assembler 1's plain
  search, BurrTools 2's other threads): keep them (stage 3b's note).
- A stopped take-apart (`TakeApartResult::stopped`) goes back to the
  pending list, counted; one that cannot come apart is counted as tried.
- The solve thread's tie order is the order its workers finished; a
  deterministic order (by assembly number among equals) would be better and
  is allowed if `compare-files` (which does not depend on it) still passes;
  record it as a deliberate difference.
- The original's take-apart queue had a lost wake-up (fixed there on
  2026-10-05); run the pause/continue stress many times.

**Tests:** port the pause/resume tests of `test/test_solver.cpp`
(including "a pause as assembling starts loses nothing") and "solve
progress: asked for all through a solve, it only goes forward" of
`test/test_rotation_solvers.cpp`; random pause/continue stress, a pause-delay
sweep, TSan clean (`[parallel]` cases that run the session with take-apart
workers and level threads), the harness still green. `bt_solve_file`
(tests/tools) is a stand-in for the session: make it use the session (or
replace it) and keep `compare-files` passing; `bt` may stay as it is until
stage 8.

Finish with the "Finishing a stage" steps of AGENTS.md, write
`docs/handoff/stage-5.md` with the brief for stage 6, tick stage 5 in
PLAN.md, one commit `Stage 5: ...`. PLAN.md recommends an audit after
stage 5 as well.

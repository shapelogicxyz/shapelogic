# Handoff: stage 2 (model and file format)

**Date:** 2026-10-06 · **Next:** stage 3 (assemblers), in a new session at
extra high effort. The brief is at the end.

## Done
- `core/shapelogic/model/`: `Puzzle`, `Problem` (parts, groups, colour rules,
  result/goal, state and counts, saved search position, pending
  assemblies, solutions and their sorting, solver options), `Solution`,
  `Assembly` with `Placement` and `MirrorInfo` (including `transform`,
  `smallerRotationExists`, `createSpace`, `sort`), `Disassembly`, `State`,
  `Separation`, `SeparationInfo` (the data part; the searches are stage 4),
  `XmlElement` / `PreservedXml`. Ported from `puzzle.cpp`, `problem.cpp`,
  `solution.cpp`, `assembly.cpp`, `disassembly.cpp`. All value types;
  `Puzzle` copies deeply (`ClonePtr`, new in `core/shapelogic/base/clone_ptr.h`).
- `core/shapelogic/io/`: the original's pull parser (`xml_pull_parser`), a tree
  reader on top of it (`xml_document`), the original's writer
  (`xml_writer`), zlib file access (`gzip_file`, replaces `gzstream`), and
  the `.xmpuzzle` reader/writer (`puzzle_file`), ported from the load and
  save code of the model files plus `voxel.cpp` and `gridtype.cpp`.
- Decision record **0008** (tree reader, what is preserved where, errors as
  exceptions inside `bt/io` and `Result` at its edge).
- zlib in `cmake/Dependencies.cmake`: the system's, else zlib 1.3.1 built
  from a pinned, hash-checked download (tested by configuring with
  `-DCMAKE_DISABLE_FIND_PACKAGE_ZLIB=ON`: builds, I/O tests pass, zlib linked
  statically).
- Tests: the original's `test_xml.cpp`, `test_roundtrip.cpp` and
  `test_puzzle_model.cpp` ported (left out: the PuzzleSolver3D/OpenSCAD
  import cases and the undo cases, which belong to later stages), and the
  `puzzlesRoundtripEqual` helper (`tests/unit/model/model_test_helpers.h`).
  New: `test_puzzle_file.cpp` (byte-for-byte saves, preservation, the
  reader's refusals, paused-solve data, rotation-solution precedence) and
  `test_model_dump.cpp`. Fixtures in `tests/data/puzzles/` (7 files from the
  original, plus two solved by its `burrTxt2`).
- **Value comparison with the original:** `tests/oracle/model_dump_old.cpp`
  (`oracle.py model-dump`) records level texts, move/rotation counts,
  sequence lengths, `compare`, `validSolution`, `createSpace`, `transform`
  for every rotation, `smallerRotationExists` (both modes) and all eight
  sort orders for the fixtures: **2175 lines, all identical**; a mutated
  line is reported, so the check is live. `oracle.py record-saves` records
  the original's saves of the fixtures; the unit test checks ours are the
  same bytes.
- **File round trip** (`just oracle-roundtrip`, new): every corpus file (all
  three tiers), the six sliding/stacking/rotation fixtures and the 180
  solved files from `oracle.py baseline-files --tier all` are loaded and
  saved by both programs.

## Gate results
| Check | Result |
|---|---|
| `just test-all` (dev, warnings as errors) | pass, 167 cases / 16346 assertions |
| `just test-asan` (address + undefined) | pass |
| `just test-tsan` | pass (still 1 `[parallel]` case) |
| `just build release` | pass |
| `just oracle-roundtrip` | **365/365** the same XML as the original's save, **365/365 byte-identical**; every new save is a fixpoint and reads back in the original to the original's own save. 179 differ from their input only by what the original also normalises (`timeMs`, default `solutions="0"`) |
| model dump vs. the original | 2175/2175 lines identical |
| grid dump vs. the original | 7014/7014 (unchanged) |
| `just oracle` | 0/19, as expected: no solver yet |
| `just check` | **not run: LLVM still not installed** |

`baseline-files` now runs the assemble-only ("A") puzzles without `-d`
(the original aborts on take-apart for those grids), so all 180 solved
files exist.

## Deliberate differences from the original
1. **Numeric character references of 128 and above are written as UTF-8**;
   the original cut them to one byte.
2. **Bytes are read as unsigned on every platform.** With signed `char` (the
   original on macOS and x86) non-ASCII bytes were rejected in names,
   counted as whitespace, and a 0xFF byte ended the file.
3. **Assembly and solution counts of a problem are read as 64-bit**
   (`strtoull`); the original used `atoi` and wrapped above 2^31.
4. **`setShapeMinimum` for a shape that is not yet a part** puts the new
   unplaced pieces after the others; the original inserted them one part's
   count past the end, which asserted when the problem had saved solutions.
5. **`removeShape` leaves an unset result unset**; the original decremented
   0xFFFFFFFF in memory (invisible, loading reset it).
6. **Copying a `Puzzle` copies everything**, solutions included (the
   original's copy constructor dropped them). `Puzzle::addProblem(copy)`
   drops parts, result and goal that name shapes the target puzzle does not
   have (the original kept dangling ids).
7. `removeColor(0)` is a `BT_ASSERT`; the original erased at a wild offset.
8. Documents nested more than 5000 deep are refused (decision 0008).
9. A sub-separation before any state is a parse error (the original read
   past the empty state list); an unknown element inside `<state>` is
   ignored (the original's loop walked into it). Unknown elements inside a
   problem's `<shapes>` and `<solutions>` are ignored (the original threw).
10. `SeparationInfo::movesText()` of an empty tree is "" (the original
    returned an uninitialised buffer).
11. The model has no atomics, locks or shared solution pointers (the
    original's problem had them for its solver thread and viewers).
    `getShapeMinimum/Maximum` no longer assert the shape index.
12. Not ported yet: `setAssembler` / `getAssembler` (stage 3), stacking rod
    sets and maps (kept as XML, see below), `adoptShapes` and the rod-set
    functions of `puzzle_c` (later stages).

## Known issues and notes for later stages
- **Stacking puzzles:** shape edits do not update the preserved
  `<stacking>` element (the original's `noteShapeRemoved`, `noteShapeSwap`,
  `trimStacks`). Until stacking is ported, the app layer (stage 6) must not
  offer shape removal, swapping or count changes on stacking puzzles.
- **Thread safety:** solutions are a plain `std::vector<Solution>` in the
  problem. The original let a running solver append while the GUI read
  (with a mutex and `shared_ptr`s). Stage 3 must not write into a `Problem`
  that others read; stage 5 designs the hand-over.
- `Problem::placementAllowed` takes the `Puzzle` for its colour count; the
  assemblers call it in hot loops, so cache what it needs there.
- LLVM is still missing: none of stages 0 to 2 has been through clang-tidy.

## Brief for stage 3 (assemblers)

Work in `/Users/colin/git/burr-tools-rotations/burrtools_alpha`; the original
is `../burrtools_rotations`. Read AGENTS.md, docs/ARCHITECTURE.md,
docs/PLAN.md (stage 3 details), this note, and decisions 0004, 0005, 0007
and 0008. Effort: extra high.

**Size.** About 15,000 lines: `assembler.{h,cpp}` (470+350),
`assembler_resume.h`, `assembler_0` (2300), `assembler_1` (3300),
`simd_exact_cover` and `simd_huang_cover` (both used by `assembler_0` and
`_1`), `bt2_*` and `assembler_bt2` (2700), `solvertype.h`, the assembler
part of `gridtype.cpp`, `helperpool.{h,cpp}`. Plan the 3a/3b split from the
start: **3a** = the shared parallel/resume layer, `assembler.cpp`,
`assembler_0` with the SIMD covers, the `bt` CLI and the harness changes;
**3b** = `assembler_1` and BT2 (dancing cells). Each half ends with the full
"Finishing a stage" steps and its own commit (`Stage 3a: ...`).

**Design first (decision 0004).** One shared layer for parallel search and
for pause/resume, instead of each assembler's own copy: work splitting
(`splitSearch`, `clonePrepared`, `remainingSearchWork`, parked searches),
iteration accounting and progress, the stop token polled in the search
loops (no assembler keeps a stop flag; nothing resets a token), and a
thread pool with a global thread budget in `core/shapelogic/base` (replacing
`helperpool.cpp` and `SHAPELOGIC_THREADS`). Write the design down in
ARCHITECTURE.md (and a decision record if it settles something lasting)
before porting the assemblers onto it. Read how the original's
`solvethread.cpp` drives the assemblers, but leave the session itself to
stage 5.

**What stage 2 leaves for this stage:**
- **Saved search positions** (decision 0006: a search paused in one program
  continues in the other). `Problem::getAssemblerState()` /
  `getAssemblerVersion()` hold the `<assembler>` element's text and
  `version` attribute as read; the writer emits them only while the problem
  is `Solving` and both are non-empty. The original's `assembler_c::save`
  wrote that element itself and `setPosition(string, version)` restored it;
  the port needs the same text format and version strings per assembler,
  plus a way to store a paused position into the problem (a setter for
  the two strings) and the original's failure rule: a position that cannot
  be restored (other version, corrupt, an interrupted parallel search)
  drops all solutions and the saved state (`problem_c::setAssembler`).
  `ERR_CAN_NOT_RESTORE_INTERRUPTED` is the case that cost assemblies in the
  original: test it.
- `PendingAssembly` items (assemblies found but not yet taken apart when a
  solve paused) are read and written; the take-apart queue that fills them
  is stage 5, but their numbering must stay compatible.
- `Assembly::transform`, `smallerRotationExists` (both modes), `sort`,
  `validSolution`, `createSpace` and `MirrorInfo` are ported and match the
  original value for value (model dump), so the assemblers can call them.
- **`getHotspot` difference from stage 1**: for a transformation a sphere
  shape cannot take, the original filled the hotspot from the untransformed
  shape unless `getBoundingBox` had been asked first; the port reports
  failure either way. Check whether any assembler path called `getHotspot`
  without `getBoundingBox` on such a transformation; if one did, the
  harness on the sphere examples will show it.
- **`warmCaches()`** must be called on every shape (pieces and result)
  before worker threads read them: `selfSymmetries`, `getHotspot` and
  `getBoundingBox` fill caches lazily.
- The assemblers must not write into a `Problem` that other threads read:
  collect results per run and hand them over (the session is stage 5; for
  now the CLI owns the problem).

**The `bt` CLI** gains enough to solve with burrTxt-compatible `--json`
(assemblies only until stage 4): the harness reads the last line starting
with `{`. For the assemble-only grids it must report assemblies with
solutions 0 and no level (stage 0 handoff, issue 2). Add a thread-count
option, so the harness can run 1 and N threads.

**Harness.** `oracle.py compare` checks every field in `COMPARED`; until
stage 4 only `assemblies` can match. Add a way to compare assemblies only
(e.g. `--fields assemblies`) and to pass the thread count, then the gate is
assembly counts equal on fast + full, with 1 thread and with all threads,
and TSan clean. Compare assembly **multisets** between serial and parallel
runs in unit tests (AGENTS.md), and include a puzzle with a symmetry
breaker (e.g. `examples/Bermuda.xmpuzzle`) in the `[parallel]` cases.
`just oracle-roundtrip` must stay at 365/365. Speed is tracked from this
stage (`tests/bench/`); memory notes: Burr-Glar (upstream clone,
3.1 million assemblies) and the `[.bench]` cases of the original's test
binary.

**Port the tests:** the assembler cases of `test/test_solver.cpp` (41 cases
in all; the pause/resume ones may need the stage-5 session and can wait)
and `test/test_simd_exact_cover.cpp`.

Finish with the "Finishing a stage" steps of AGENTS.md and write
`docs/handoff/stage-3.md` (or `stage-3a.md` / `stage-3b.md`) with the brief
for what follows. PLAN.md recommends a separate high-effort audit after
each solver stage.

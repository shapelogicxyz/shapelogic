# Handoff: stage 1 (grids and symmetry)

**Date:** 2026-10-06 · **Next:** stage 2, in a new session (high effort). The planned
1+2 session was split here because it ran long; the brief is at the end.

## Done
- `core/shapelogic/grid/`: `GridType`/`GridKind`, the abstract `Voxel` and the five
  grid classes (`VoxelBricks`, `VoxelPrisms`, `VoxelSpheres`, `VoxelRhombic`,
  `VoxelTetraOcta`), `Symmetries` with one table-driven implementation over
  the three generated table sets, `VoxelTable`, `Bitfield<N>`. Ported from
  `voxel*.cpp`, `gridtype.cpp`, `symmetries_*.cpp`, `voxeltable.cpp`,
  `bitfield.h`, `tools/intdiv.h`. Meshes and XML are not here (stage 6 and
  `bt/io` in stage 2).
- Generated tables `tabs_0/1/2/*.inc` copied unchanged; the `symcalc.inc`
  decision trees converted from hand-made `new`/`delete` to `same(t)` calls
  by a checked sed pass (`core/shapelogic/grid/TABLES.md`).
- `core/shapelogic/base/int_cast.h` (`asInt`, `asUnsigned`) for the conversions the
  original left implicit.
- Tests: the original's `test_voxel.cpp`, `test_voxel_connect.cpp`,
  `test_symmetries.cpp`, `test_grid_invariants.cpp` ported (51 cases, all
  passing unchanged in substance, including the pinned 576/144 composition
  counts), plus `test_grid_port.cpp` (7 new cases) and the value comparison
  `test_grid_dump.cpp`.
- **Value comparison with the original:** `tests/unit/grid/grid_dump.h` dumps
  the symmetry tables (hashed), neighbours, `validCoordinate`/`onGrid`,
  `transformPoint`, and for 12 generated shapes per grid: symmetry group,
  every transformation (size, hotspot, box, content, normalised number),
  the per-transformation box/hotspot queries, `minimizePiece`, `connected`,
  `fillHoles`, `actionOnSpace`, `scale` (both modes, 2 and 3) with
  `scaleDown`, `translate`, `resizeInclude`, the mirror transformation, and
  `VoxelTable` lookups. Recorded from the original by
  `tests/oracle/grid_dump_old.cpp` (`oracle.py grid-dump`), compared by the
  unit test: **7014 lines, all identical** (prism and sphere floating-point
  rotations included). A mutated line is reported, so the check is live.

## Gate results
| Check | Result |
|---|---|
| `just test-all` (dev, warnings as errors) | pass, 74 cases / 15449 assertions |
| `just test-asan` | pass |
| `just test-tsan` | pass (still 1 `[parallel]` case; threads arrive in stage 3) |
| `just build release` | pass |
| grid dump vs. the original | 7014/7014 lines identical |
| `just oracle` | 0/19, as expected: no solver yet |
| `just check` | **not run: LLVM still not installed** |

## Deliberate differences from the original
1. **`getHotspot` for a transformation the shape cannot take** (spheres
   only). The original filled the hotspot half of its cache from the
   untransformed copy and reported success, unless `getBoundingBox` had been
   asked for that transformation first. The port fills both halves together
   and reports failure for both. **Stage 3: check whether any assembler path
   called `getHotspot` without `getBoundingBox` on a failing transformation**
   (the grid dump asks in the order that makes the original consistent).
2. **The cube transform drops the transformation cache.** The original's
   `voxel_0_c::transform` kept the cached hotspots/boxes of the old
   orientation (the prism and sphere transforms cleared them). Copies always
   started with fresh caches, which is why it did not show.
3. **`unionintersect` compares positions as signed numbers**; the original
   mixed unsigned and signed, which only differs for negative offsets (its
   only caller, the GUI, passes non-negative ones).
4. `voxel_c::copy()` (copy all but the name; unused, and it kept a stale
   cache) was not ported. Copy constructors keep the caches instead of
   resetting them: they describe the same content and hotspot.
5. `transformPoint` with an out-of-range transformation, and the sphere
   grid's odd-parity input, are `BT_ASSERT`s (the original threw
   `bt_te` too, so behaviour is the same: an exception).

## Known issues and notes for later stages
- **Caches are not thread-safe.** `selfSymmetries()`, `getHotspot()` and
  `getBoundingBox()` write mutable caches. Call `warmCaches()` before
  sharing a shape between threads (stage 3).
- `calculateSymmetry` asserts `symmetryKnown` (48 or 240 extra transforms
  per uncached shape), as the original did in its builds (asserts on). If it
  shows in profiles (stage 8), it can move to a test.
- Unknown grid `type` attributes must be rejected by the reader (stage 2):
  `GridType` itself trusts its kind.
- LLVM is still missing, so none of the code has been through clang-tidy.

## Brief for stage 2 (model and file format)

Work in `/Users/colin/git/burr-tools-rotations/burrtools_alpha`; the original
is `../burrtools_rotations`. Read AGENTS.md, docs/ARCHITECTURE.md,
docs/PLAN.md (stage 2 details), this note, and decisions 0005 to 0007.

**Scope** (`core/shapelogic/model/`, `core/shapelogic/io/`): port `puzzle.cpp`,
`problem.cpp`, `solution.cpp`, `assembly.cpp`, the data part of
`disassembly.cpp` (`separation_c`, `separationInfo_c`, `state_c`, including
the rotation fields `dt` / `rotation` that stage 4 needs; the take-apart
algorithms are stage 4), `tools/xml.cpp` and `tools/gzstream.cpp`. Port
`test/test_xml.cpp`, `test_roundtrip.cpp`, `test_puzzle_model.cpp` and the
puzzle comparison helper at the end of `test/test_helpers.h`.

**What stage 1 found out about the format** (read the original files too,
these are pointers, not the whole story):
- The original loaders skip every element they do not know
  (`skipSubTree`), so "preserved verbatim" (decision 0006) cannot come from
  porting the pull parser as it is. Recommended, and worth a decision record
  0008: read the file into a small DOM (element: name, ordered attributes,
  children, text), let the model readers take what they know, and keep
  unknown elements and attributes on the owning model object to write back.
  Port `xmlWriter_c` exactly: its output format (header line, one space of
  indent per level, `<tag/>` for empty tags, the five escapes, no mixed
  content) is what "same formatting as the original" means.
- Parser behaviour to keep: the five named entities and numeric character
  references; text is merged across comments and CDATA; whitespace-only text
  between elements is ignored; a newline inside an attribute value becomes a
  space; \r\n becomes \n. Numeric references above 255 were truncated to a
  byte by the original; write them as UTF-8 and note it.
- Elements the original reads that this core does not model yet, to keep
  verbatim: `puzzle/rodSets`, `problem/stacking`. Model the rest:
  `problem/assembler` (version attribute + raw text: the saved search
  position, stage 3 resumes from it), `problem/pending` (items with an
  `assembly`), `solverOptions` (a `std::map`, so attributes are written
  sorted), `solutions` vs `solutionsWithRotations` (a reader that finds the
  latter drops the classic block), voxel attributes `goal`, `diskSize`,
  `color` (already fields of `Voxel`).
- Problem attributes: `name`, `state` (only 1..3 accepted), and when not
  unsolved `assemblies`, `solutions`, `time`, `timeMs` (wins over `time`),
  `searched`, `searchDepth`, `triedApart`; `maxHoles`. Writer order in
  `puzzle`: gridType, colors, shapes, rodSets, problems, comment; in
  `problem`: shapes (the parts), result, goal, stacking, solverOptions,
  bitmap, assembler (only while solving), pending, solutions.
- The voxel reader must keep the original's checks (each size <= 0x10000,
  at most 0x40000000 cells, colour <= 63, exact cell count, type 0), and
  reject unknown grid types. Reading is where `Result<T>` errors come from.
- `problem_c` holds a reference to its puzzle. In the port a `Problem` is a
  value inside `Puzzle`; operations that need shapes or colours take the
  puzzle (or live on `Puzzle`). Port the problem editing operations
  (`setShapeMinimum/Maximum`, `removeShape`, `exchangeShapes`,
  `exchangeParts`, colour rules, groups, solution sorting) with the solution
  and assembly operations they call; the solver hooks (`setAssembler`,
  `incNumAssemblies` and the like) wait for stages 3 and 5.
- zlib: add it in `cmake/Dependencies.cmake` (`find_package(ZLIB)` with a
  pinned FetchContent fallback, so MinGW builds work). Reading must accept
  both gzip and plain XML, as the original does.

**Gate** (decision 0006): a test or script that loads and saves every corpus
puzzle (all three tiers of `tests/oracle/corpus.txt`, plus
`examples/SliderByGiiker.xmpuzzle`, `examples/PanexLevel3to10.xmpuzzle`,
`test/test_sliding_solver.xmpuzzle`, `test/test_sliding_nested.xmpuzzle`,
`test/test_stacking_hanoi.xmpuzzle`, `test/test_rotation_solvers.xmpuzzle`,
and solved files with solutions from `oracle.py baseline-files`) and checks
the saved file is semantically identical (XML trees equal, ignoring
attribute order and whitespace-only text) to the input and to what the
original writes for the same file. Simplest way to get the original's
saves: a small `tests/oracle/roundtrip_old.cpp` (`puzzle_c::load` then
`save`) built against the original's library, exactly as
`oracle.py grid-dump` builds `grid_dump_old.cpp` (reuse
`build_against_old`). Never use the commercial sliding puzzle "One Way or
Another" as a fixture; `test/test_sliding_solver.xmpuzzle` is the sliding
test puzzle.

Finish with the "Finishing a stage" steps of AGENTS.md (one commit,
`Stage 2: ...`) and write `docs/handoff/stage-2.md` with the brief for
stage 3 (assemblers; extra high effort per PLAN.md). Points for that brief:
the `getHotspot` difference above, `warmCaches()` before threads, and the
saved assembler positions from `problem/assembler`.

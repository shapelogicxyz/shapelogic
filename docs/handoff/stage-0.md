# Handoff: stage 0 (bootstrap)

**Date:** 2026-10-06 · **Next:** stages 1 and 2 (one session, high effort)

## Done
- Repository: git (`main`), CMake presets `dev` / `release` / `asan` /
  `tsan`, justfile, `.clang-format`, `.clang-tidy`, `.editorconfig`,
  GPL-3.0-or-later (COPYING, SPDX headers), AUTHORS, README.
- `core/bt/base`: `StopSource`/`StopToken`/`StopKind`, `SolveState` +
  `SolveStateCell` with the transition table, `Result<T>`/`Error`/`Errc`,
  `BT_ASSERT`/`InternalError`, `kVersion`. These implement decisions 0004
  and 0005 and are what every later stage builds on.
- `engine/` and `cli/` placeholders (`--version` only).
- Tests: 15 Catch2 cases in `tests/unit/base/`, including the "stop requested
  before the work starts" race from the original.
- Comparison harness `tests/oracle/` with the original's answers recorded:
  fast 19 puzzles, full 158 TIC puzzles, slow 3 puzzles, **0 errors**
  (`baseline/*.json`). `just oracle` runs end to end (0/19 match until the
  solver exists, as expected).
- Docs: AGENTS.md (conventions, carried over from the original's hard-won
  rules), ARCHITECTURE.md, PLAN.md, decisions 0001 to 0007, webui-spec.md
  (from Girish Sharma's mockup, with gaps), core-api.md (draft operation list).

## Gate results
| Check | Result |
|---|---|
| `just test-all` (dev, warnings as errors) | pass |
| `just test-asan` | pass |
| `just test-tsan` | pass (1 `[parallel]` case) |
| `just build release` | pass |
| `just check` (clang-tidy) | **not run: LLVM not installed** (`brew install llvm`) |
| harness baselines | 180 puzzles recorded, 0 errors |

## Known issues and findings
1. **LLVM is not installed** on this Mac, so `just check` and `just format`
   cannot run. Install with `brew install llvm` (no PATH change needed; the
   recipes look in `/opt/homebrew/opt/llvm/bin`). Run both once installed;
   the stage 0 code has not yet been through clang-tidy.
2. **Bug in the original `burrTxt --json`:** it aborts (uncaught
   `assert_exception: cache`) on every spheres, rhombic and tetra-octa puzzle,
   because `--json` forces take-apart on grids that do not support it. The
   harness marks those 9 examples `A` (assemble only) and reads the
   original's `-q` summary. The new `bt --json` must report assemblies only
   for such grids instead of failing. Worth fixing in the original too.
3. The mockup cites an unpublished spec (ids like OQ-19, C14). Ask Girish
   Sharma for it before stage 6/7; webui-spec.md section 7 lists the gaps.
4. The bundled sliding and stacking examples (SliderByGiiker,
   PanexLevel3to10) are excluded from the corpus until those features are
   ported.

## Brief for stages 1 and 2

Work in `/Users/colin/git/burr-tools-rotations/burrtools_alpha`; the original
is `../burrtools_rotations`. Read AGENTS.md, docs/ARCHITECTURE.md,
docs/PLAN.md (stage details 1 and 2), this note, and decisions 0005 to 0007.

**Stage 1, grids and symmetry** (`core/bt/grid/`):
- Port the grid types and voxel spaces from the original `src/lib/`:
  `voxel.{h,cpp}`, `voxel_0` to `voxel_4`, `gridtype.cpp`, `voxeltable.cpp`,
  `symmetries_0/1/2` and the `tabs_0` to `tabs_4` tables (copy the generated
  tables as they are). Leave mesh generation (`getDrawingMesh`,
  `voxel_2_mesh.cpp`, `halfedge/`) for stage 6.
- Include the editing operations that live in `voxel_c`: `transform`,
  `translate`, `scale`, `scaleDown`, `resize`/`resizeInclude`,
  `minimizePiece`, `fillHoles`, `actionOnSpace`, hotspot, cell state and
  colour, weight, name, symmetry calculation, `unionintersect`.
- Modern shape: value types, `std::unique_ptr` for the polymorphic voxel
  spaces (a `clone()` instead of `gridType_c::getVoxel` returning raw
  pointers), `Result<T>` for failures, `BT_ASSERT` for invariants.
- Port `test/test_voxel.cpp`, `test_voxel_connect.cpp`, `test_symmetries.cpp`,
  `test_grid_invariants.cpp` (and the helpers they need), plus new tests that
  check symmetry results and transforms against values dumped from the
  original for every grid type.

**Stage 2, model and file format** (`core/bt/model/`, `core/bt/io/`):
- Port `puzzle.cpp`, `problem.cpp`, `solution.cpp`, `assembly.cpp` and the
  data part of `disassembly.cpp` (`separation_c`, `state_c`; the take-apart
  algorithms are stage 4), and `tools/xml.cpp` + `tools/gzstream.cpp`. Add
  zlib in `cmake/Dependencies.cmake`.
- Unknown and not-yet-supported content (sliding, stacking, saved assembler
  positions) is preserved verbatim on save (decision 0006).
- Port `test_xml.cpp`, `test_roundtrip.cpp`, `test_puzzle_model.cpp`. Copy the
  puzzle files the tests need into `tests/data/`.
- **Gate:** a test or script that loads and saves every corpus puzzle (all
  three tiers plus the sliding and stacking examples) and checks the saved
  file is semantically identical to the input and to what the original writes
  (`burrTxt2` can produce the original's saved files; see
  `tests/oracle/oracle.py baseline-files`).

Finish with the "Finishing a stage" steps in AGENTS.md for each stage
(one commit per stage), and write `docs/handoff/stage-2.md` (and
`stage-1.md` if the session is split) with the brief for stage 3.

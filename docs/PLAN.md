# Plan: rebuilding the BurrTools core

## Goal
A clean, maintainable C++ core for BurrTools, designed for a web front end
from the start, that gives the same answers as the original application.
The original (`../burrtools_rotations`) is the reference throughout.

**In scope (first complete version):** all five grids (cubes, triangular
prisms, spheres, rhombic tetrahedra, tetrahedra-octahedra); shapes and
colours; problems (result, piece counts and ranges, groups, colour rules);
the Classic, Crowell and BT2 assemblers; disassembly including rotation
moves; solutions; `.xmpuzzle` compatibility; the engine API; the `bt` CLI.

**Later:** sliding puzzles, stacking/Panex, STL and GLB export, imports
(PuzzleSolver3D, Puzzlecad), grid conversion, Python bindings. Lua scripting
is dropped. Files with these features load and save without loss (0006).

## Stages and sessions

Each session starts fresh, reads AGENTS.md, ARCHITECTURE.md and the newest
handoff note, and ends with the "Finishing a stage" steps in AGENTS.md.

| Session | Stage | Work | Effort | Gate |
|---|---|---|---|---|
| 1 | 0 | Bootstrap: build, conventions, base types, harness, docs | high | unit tests, harness baselines recorded |
| 2 | 1 | Grids and symmetry tables, voxel editing ops | high | ported grid/voxel/symmetry tests pass; table checks against the original |
| 2 | 2 | Model and `.xmpuzzle` read/write | high | every corpus file round-trips; semantic match with the original's saves |
| 3 | 3 | Shared parallel/resume layer + assemblers | extra high | `just oracle` assembly counts match (fast + full, 1 and N threads); TSan clean |
| 4 | 4 | Disassembly, rotation moves, movement caches | extra high | summary **and** file comparison match on fast + full + slow |
| 5 | 5 | SolveSession: prepare, pause, continue, abort, workers, stats | extra high | random pause/continue stress, pause-delay sweep, TSan clean, harness still green |
| 6 | 6 | App layer: Document, undo/redo, editing commands, fit stats, meshes, playback, explorer | high | unit tests per command; undo/redo round-trips; mesh checks |
| 7 | 7 | Engine: JSON-RPC, schema, events, binary frames, paging | extra high (API design), high (implementation) | contract tests drive bt-engine as a subprocess; scripted session passes |
| 7 | 8 | `bt` CLI, benchmarks, performance pass | high | burrTxt-compatible `--json`; within 5% of the original's speed or faster |

Stage 3 is split in two sessions: 3a (the shared run layer, `assembler_0`,
the SIMD covers, the `bt` CLI and the harness) and 3b (`assembler_1` and
BT2).

Recommended audit after each solver stage (3, 4, 5) and after stage 7's API
design: a separate review at the highest available effort or model, given
the diff, the gate results and this plan, reporting findings with concrete
failure scenarios.

## Status

- [x] **Stage 0**: bootstrap (see docs/handoff/stage-0.md)
- [x] **Stage 1**: grids and symmetry (see docs/handoff/stage-1.md)
- [x] **Stage 2**: model and file format (see docs/handoff/stage-2.md)
- [x] **Stage 3a**: run layer, assembler 0, SIMD covers, `bt` solving (see docs/handoff/stage-3a.md)
- [x] **Stage 3b**: assembler 1 and BurrTools 2 (see docs/handoff/stage-3b.md)
- [x] Audit of stage 3: six findings fixed (commit 05088fd; see docs/handoff/stage-4.md)
- [x] **Stage 4**: disassembly (see docs/handoff/stage-4.md)
- [x] Audit of stage 4: three findings fixed (commit 60dc717; see docs/handoff/stage-5.md)
- [x] **Stage 5**: solve session (see docs/handoff/stage-5.md)
- [x] Audit of stage 5: six findings fixed (commit db3cb40; see docs/handoff/stage-6.md)
- [x] **Stage 6**: app layer and meshes (see docs/handoff/stage-6.md)
- [x] **Stage 7**: engine, its API schema and contract tests (see docs/handoff/stage-7.md)
- [ ] Stage 8: CLI and performance

## Stage details

### Stage 1: grids and symmetry
Port from `src/lib/`: `voxel.{h,cpp}`, `voxel_0` … `voxel_4` (cubes, prisms,
spheres with `voxel_2_mesh` left for stage 6, rhombic, tetra-octa),
`symmetries_0/1/2` and the `tabs_*` tables, `gridtype.cpp`, `voxeltable.cpp`.
Include the editing operations that live in `voxel_c`: `transform`,
`translate`, `scale`, `scaleDown`, `resize`/`resizeInclude`, `minimizePiece`,
`fillHoles`, `actionOnSpace` (surface inner/outer), hotspot, colours per
cell, symmetry calculation. Port `test/test_voxel.cpp`,
`test_voxel_connect.cpp`, `test_symmetries.cpp`, `test_grid_invariants.cpp`.
Check symmetry tables against values dumped from the original.

### Stage 2: model and file format
Port `puzzle.cpp`, `problem.cpp`, `solution.cpp`, `assembly.cpp`,
`disassembly.cpp` (data structure; the algorithms come in stage 4),
`tools/xml.cpp`, `tools/gzstream.cpp` (zlib). Preserve unknown XML (0006).
Port `test_xml.cpp`, `test_roundtrip.cpp`, `test_puzzle_model.cpp`. Gate:
load + save every corpus file in both programs; compare semantically.

### Stage 3: assemblers
Design the shared parallel/resume layer first (decision 0004), then port
`assembler.cpp`, `assembler_0`, `assembler_1`, `simd_exact_cover`,
`simd_huang_cover`, `bt2_*`, `assembler_bt2`. Saved positions stay
compatible with the original. The `bt` CLI gains enough to solve with
`--json` (assemblies only until stage 4). Port `test_solver.cpp` assembler
cases and `test_simd_exact_cover.cpp`. The design is decision 0009; the
gate adds `just oracle-resume` (saved positions continue in both programs).

### Stage 4: disassembly
Port `disassembler_0`, `disassembler_a`, `disassembler_crowell`,
`disassemblernode`, `disassemblerhashes`, `movementcache*`,
`movementanalysator`, `grouping`, `rotationrules`, `rotationmoves_*`,
`disasmtomoves`. Write the file-level harness comparison. Port
`test_disassembly.cpp`, `test_rotation_solvers.cpp`, `test_grouping.cpp`.

### Stage 5: solve session
Replace `solvethread.cpp` / `helperpool.cpp`: one session object per problem
solve, the state machine, a shared worker pool, the take-apart queue, stats
snapshots, autosave hooks, error capture. Port the pause/resume tests from
`test_solver.cpp`, including "a pause as assembling starts loses nothing".

### Stage 6: app layer
`Document` (puzzle + undo history), commands for every UI action in
docs/webui-spec.md, queries (fit statistics), drawing meshes (`halfedge/`,
`getDrawingMesh`, `voxel_2_mesh`), the playback sampler generalised from the
original's `gltfexport.cpp`, placement/movement explorer data. Done; `grid.geometry` (the
grid editors' 2D cells), `solution.analyse` and the export of paused solves
moved to stage 7 (docs/handoff/stage-6.md).

### Stage 7: engine
JSON Schema in `api/`, `bt-engine` JSON-RPC dispatcher, events, binary
frames, paged solution lists, generated `docs/API.md`, contract tests.
Done in one session (decision 0012), with what stage 6 left: the grid
editors' layer drawing, the analysis of saved assemblies, the paused
solve files and the stored solver settings. The recommended review of
the API design was not held; an audit of stage 7 is worth it.

### Stage 8: CLI and performance
Complete `bt` (burrTxt-compatible options and `--json`), benchmark scripts
in `tests/bench/`, profile and close any gap to the original.

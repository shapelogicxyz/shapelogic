# Architecture

BurrTools Next is the core of the BurrTools puzzle designer and solver,
rebuilt so a web front end can drive it. This document describes **what
exists now** and the rules that hold everywhere. Each stage updates it.

*Current state: stage 1 (grids and symmetry). `core/bt/base` and `core/bt/grid` have code.*

## Big picture

```
 web front end (later: Electron + TypeScript + three.js)
        │  JSON-RPC 2.0 over stdio, binary frames for meshes   (decision 0003)
        ▼
 bt-engine  (engine/)        bt  (cli/, batch solving, burrTxt-compatible --json)
        │                         │
        └──────────┬──────────────┘
                   ▼
            libbtcore (core/)
   app     Document, undo/redo, editing commands, queries, playback, explorer
   solve   assemblers, disassemblers, rotation rules, SolveSession
   mesh    half-edge meshes and drawing meshes for all grids
   io      XML, gzip, .xmpuzzle reader/writer
   model   Shape, Colour, Problem, Assembly, Disassembly, Solution, Puzzle
   grid    grid types, voxel spaces, symmetry tables
   base    StopToken, SolveState, Result/Error, BT_ASSERT, thread pool
```

Dependencies point downwards only: `app` may use everything below it, `base`
uses nothing. `core/` has no UI, does not print, and has no global state.

## Directory layout

| Path | Contents |
|---|---|
| `core/bt/<module>/` | the library, headers and sources together; include as `"bt/<module>/x.h"` |
| `engine/` | `bt-engine` (stage 7) |
| `cli/` | `bt` (solving from stage 3, full CLI in stage 8) |
| `api/` | JSON Schema of engine methods and events (stage 7); see `api/README.md` |
| `tests/unit/<module>/` | Catch2 tests, one binary `bt_tests` |
| `tests/oracle/` | the comparison harness against the original BurrTools |
| `tests/bench/` | benchmarks against the original (stage 3 onward) |
| `tests/data/` | test fixtures (puzzle files copied from the original) |
| `docs/` | this file, PLAN.md, decisions/, handoff/, webui-spec.md, core-api.md |
| `cmake/` | warnings, sanitizers, dependencies |

## Modules

### base (exists)
- `stop.h`: `StopSource` / `StopToken` / `StopKind` (decision 0004). One source
  per run, requests are never withdrawn, child sources for stage-level stops.
- `solve_state.h`: `SolveState` and `SolveStateCell`, the transition table.
  Only a run's worker writes it.
- `error.h`: `Errc`, `Error`, `Result<T>` (`std::expected`), `fail()`.
- `assert.h`: `BT_ASSERT`, `BT_ASSERT_MSG`, `InternalError`; on in every build.
- `version.h`: `kVersion`, from CMake.
- `int_cast.h`: `asInt()` / `asUnsigned()`, the spelled-out int/unsigned
  conversions that ported code needs under `-Wsign-conversion`.
- Planned for stage 3: a shared thread pool with a global thread budget
  (replacing the original's `helperpool.cpp` and `BURRTOOLS_THREADS`).

### grid (exists)
Ported from the original `voxel*.cpp`, `symmetries_*.cpp`, `tabs_*`,
`gridtype.cpp`, `voxeltable.cpp`; checked value for value against the
original (`tests/data/grid_dump.txt`, see "Testing against the original").
- `grid_type.h`: `GridKind` (the numbers are the file format's) and
  `GridType`, a small value type: capabilities, `getSymmetries()`, and
  `makeVoxel()`, the factory for voxel spaces. Sliding and stacking grids
  exist as kinds (cube spaces) so their files load; their solvers are later.
- `voxel.h`: `Voxel`, the abstract voxel space: cells (state + colour per
  byte), bounding box, hotspot, name, weight, the per-transformation hotspot
  and bounding-box queries, comparison, connectivity, and the editing
  operations (`translate`, `resize`, `minimizePiece`, `fillHoles`,
  `actionOnSpace`, `scale`, `scaleDown`, `resizeInclude`, `unionintersect`).
  It also carries the sliding/stacking data (goal marks, disc size, shape
  colour) so files round-trip. Copy with `clone()`; the grid is stored by
  value, so a voxel space never dangles.
- `voxel_bricks`, `voxel_prisms`, `voxel_spheres`, `voxel_rhombic`,
  `voxel_tetra_octa`: one class per grid (`voxel_0` … `voxel_4` in the
  original); rhombic and tetra-octa derive from bricks and lay a 5- or
  6-aligned super-grid over cubes.
- `symmetries.h`: `Symmetries`, the transformation and symmetry-group tables
  of a grid (`transAdd`, `minimizeTransformation`, `calculateSymmetry`, …).
  `symmetries_impl.h` is the one table-driven implementation; `symmetries_0/1/2.cpp`
  instantiate it over the generated tables in `tabs_0/1/2/` (copied as they
  are; see `core/bt/grid/TABLES.md`). The instances are immutable statics.
- `voxel_table.h`: `VoxelTable`, a hash table that finds a shape in any
  orientation; it takes a callback from index to shape.
- `bitfield.h`: `Bitfield<N>`, a constexpr bit set (the sphere tables).

**Caches and threads.** `Voxel` computes its symmetry group and the hotspot
and bounding box of each transformation lazily inside const functions. A
shape read by several threads must have `warmCaches()` called first (the
assemblers in stage 3 do this before sharing shapes); editing a shape
invalidates the caches.

**Differences from the original** (deliberate; listed in
docs/handoff/stage-1.md): `getHotspot` of a transformation the shape cannot
take reports failure regardless of call order; the cube transform drops the
transformation cache; `voxel_c::copy()` (unused) was not ported.

### model and io (stage 2)
Value-type puzzle model and the `.xmpuzzle` reader/writer with unknown content
preserved (decision 0006). From `puzzle.cpp`, `problem.cpp`, `solution.cpp`,
`assembly.cpp`, `disassembly.cpp`, `tools/xml.cpp`, `tools/gzstream.cpp`.

### solve (stages 3 to 5)
A shared parallel/resume layer and the assemblers (`assembler_0`,
`assembler_1` with SIMD covers, BT2 dancing cells); disassemblers, movement
caches, rotation rules; `SolveSession` replacing `solveThread_c`.

### mesh and app (stage 6)
Drawing meshes for every grid; `Document` with transactional undo/redo;
editing commands; fit statistics; the playback sampler (meshes plus piece
poses at any step); explorer data.

## Testing against the original

- `tests/oracle/` runs the original programs (decision 0007): the summary
  comparison of `bt` with `burrTxt` (from stage 3), and recorders that
  compile a dump tool against the original's library
  (`oracle.py grid-dump` writes `tests/data/grid_dump.txt`).
- The unit tests compare with those recordings
  (`tests/unit/grid/test_grid_dump.cpp`); they never read the original repo.

## Rules that hold everywhere

See AGENTS.md for the full list. The load-bearing ones:
1. Cancellation only through `StopToken`; nothing resets a stop request.
2. Only a run's worker writes its `SolveState`.
3. Recoverable errors are `Result<T>`; broken invariants are `BT_ASSERT`.
4. No owning raw pointers; value types and `unique_ptr`.
5. No global state or environment switches in `core/`.
6. Ported code matches the original on the harness before it is reshaped.

## Build and test

`just build` / `just test` / `just test-all` / `just test-asan` /
`just test-tsan` / `just oracle` / `just check`. Presets in
`CMakePresets.json`; strict warnings are errors in `dev` and `release`.

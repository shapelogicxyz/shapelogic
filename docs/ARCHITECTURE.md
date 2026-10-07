# Architecture

BurrTools Next is the core of the BurrTools puzzle designer and solver,
rebuilt so a web front end can drive it. This document describes **what
exists now** and the rules that hold everywhere. Each stage updates it.

*Current state: stage 0 (bootstrap). Only `core/bt/base` has code.*

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
- Planned for stage 3: a shared thread pool with a global thread budget
  (replacing the original's `helperpool.cpp` and `BURRTOOLS_THREADS`).

### grid (stage 1)
Grid types (cubes, triangular prisms, spheres, rhombic tetrahedra,
tetrahedra-octahedra), voxel spaces with their editing operations, symmetry
and transformation tables. From the original `voxel*.cpp`,
`symmetries_*.cpp`, `tabs_*`, `gridtype.cpp`.

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

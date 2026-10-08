# BurrTools Alpha

A rebuild of the core of [BurrTools](https://burrtools.sourceforge.net/), the
designer and solver for interlocking (burr) puzzles: the puzzle model, the
`.xmpuzzle` file format, the assembly and disassembly solvers, and an engine
process that a new web front end will drive.

The original application stays the reference: every ported solver must give
the same answers on a corpus of 180 puzzles before it counts as done.

**Status:** stages 0 to 7 of 8 done: grids, model and file format, the
assemblers, the take-apart (with rotation moves), the solve session, the
app layer (document, undo, commands, meshes, playback, explorers) and
`bt-engine`, the JSON-RPC engine the front end talks to; `bt` solves and
takes apart as the original's `burrTxt`. Next: the complete `bt` CLI and
the performance pass (stage 8). See [docs/PLAN.md](docs/PLAN.md).

## Building

Requirements: CMake 3.28+, Ninja, a C++23 compiler (Apple clang 17+, GCC 13+,
MinGW-w64 GCC 13+), Python 3.10+ and [just](https://github.com/casey/just).
Optional: LLVM for `just check` / `just format`.

```bash
just build        # build/dev
just test         # unit tests
just oracle       # compare with the original BurrTools (needs ../burrtools_rotations built)
```

Outputs: `build/<preset>/engine/bt-engine` (the engine: JSON-RPC 2.0 on
stdin and stdout), `build/<preset>/cli/bt` (command-line solver),
`build/<preset>/tests/bt_tests`.

## Documentation

- [docs/PLAN.md](docs/PLAN.md): stages and status
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): how the code is organised
- [docs/decisions/](docs/decisions/): why it is organised that way
- [docs/webui-spec.md](docs/webui-spec.md): what the web front end does
- [docs/API.md](docs/API.md): the engine's methods and events (generated from [api/](api/))
- [docs/core-api.md](docs/core-api.md): the draft API.md replaced
- [AGENTS.md](AGENTS.md): conventions for everyone working on the code

## Licence

GPL-3.0-or-later (see COPYING). The algorithms are ported from BurrTools by
Andreas Röver and contributors; see AUTHORS.

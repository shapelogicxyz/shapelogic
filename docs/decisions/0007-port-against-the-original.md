# 0007: Port faithfully and prove it against the original

**Status:** accepted (stage 0); amended 2026-11-27 (the recorded answers are the reference: `tests/regression/`)

## Context
The solvers encode decades of edge cases. A rewrite that reinvents them
risks subtly different answers that tests written from scratch would not
catch. The original application is available and runs.

## Decision
- **Faithful first, restructured second.** An algorithm is ported with its
  behavior intact, proved, and only then reshaped. No algorithmic changes
  during a port; performance work comes after the gate passes.
- **The original is the oracle.** `tests/oracle/oracle.py` runs the original
  `burrTxt` and the new `bt` on the corpus (`tests/oracle/corpus.txt`: the
  bundled examples, the 158 + 3 TIC puzzles) and compares assembly counts,
  solution counts and best levels; from stage 4 also every solution and its
  disassembly, via solved files from `burrTxt2`.
- A solver stage is finished only when the harness shows **no differences**
  on the fast and full tiers, with one and with many threads.
- Speed: within 5% of the original or faster on the benchmark set, measured
  back to back on one machine (stage 8 gate; tracked from stage 3).
- Each ported file names its source file(s) in the original.
- Known bugs of the original are not copied. They are listed in the stage
  handoff and the harness accounts for them explicitly (e.g. the "A" flag in
  corpus.txt: the original `burrTxt --json` aborts on grids without
  take-apart).

## Consequences
- The original repo must stay buildable next to this one while porting.
- The harness grows with the port: summary comparison from stage 3, file
  comparison from stage 4, engine scenario tests from stage 7.

## Amendment (2026-11-27): the recorded answers are the reference
The port has been built and tested on its own long enough that its own
numbers can be trusted, and the original it was made from will not be
checked out beside it any more: `../burr-tools` follows upstream BurrTools,
which has no sliding or stacking puzzles, no take-apart with rotations and
another level text.

- `tests/oracle/` and the programs it drove (`tests/tools/`) are removed.
  Before that, the CLI was checked one last time against the original's
  files with the harness, on every tier (180 brick, 35 sliding, 41
  stacking puzzles), all threads and one, Classic and Crowell, and the
  Panex Solver: no differences.
- **The regression suite** (`tests/regression/`) is the gate now. It solves
  a corpus of this repo's files and compares with the answers recorded from
  the original (or ShapeLogic's own, marked, where the original had none).
  It reads and runs nothing else. A change that alters an answer on
  purpose records the new one (`--update`) and says why.
- The full tier keeps ten of the 158 TIC puzzles, chosen for what each
  tests and copied under names that say so; the slow tier keeps two of
  three. The rest add little but time.
- The `--json` best level of equals is now the greater level text (burrTxt
  kept the first, which on several threads is whichever take-apart ended
  first), so an answer is the same on any number of threads.
- **Upstream BurrTools** is compared with only on request: `just
  burrtools-compare` times both programs on every regression puzzle both
  can solve and compares their assembly and solution counts. No suite runs
  it, and its speed is not a gate. Speed is gated against an earlier
  ShapeLogic build (`bench.py --session`).
- The unit tests' recordings from the original (`tests/data/`) stay: they
  are fixed data, read like any fixture.

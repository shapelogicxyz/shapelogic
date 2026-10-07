# 0007: Port faithfully and prove it against the original

**Status:** accepted (stage 0); `tests/oracle/`

## Context
The solvers encode decades of edge cases. A rewrite that reinvents them
risks subtly different answers that tests written from scratch would not
catch. The original application is available and runs.

## Decision
- **Faithful first, restructured second.** An algorithm is ported with its
  behaviour intact, proved, and only then reshaped. No algorithmic changes
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

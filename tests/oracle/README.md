# Comparison harness ("oracle")

The original BurrTools is the specification. This harness runs the original
`burrTxt` and the new `bt` on the same puzzles and compares their answers.
A solver stage is done only when the harness reports no differences
(docs/decisions/0007-port-against-the-original.md).

## Setup

- The original checkout at `../burrtools_rotations` (or `BT_OLD_REPO`), built
  with `just build` there, so `build/burrTxt` and `build/burrTxt2` exist.
- The new `bt` is built by `just oracle` itself (release preset).

## Corpus and tiers

[`corpus.txt`](corpus.txt) lists the puzzles, by path in the original repo:

| Tier | Puzzles | Time (original, M-series Mac) | When |
|---|---|---|---|
| fast | 19 bundled examples and fixtures | ~2 s | every run |
| full | 158 TIC puzzles with rotation take-apart | ~23 s | before finishing a solver stage |
| slow | Climburr_OldVer, CoverUp3, CornerCube | ~7 s | benchmarks, final checks |

Flags per puzzle: `R` = rotation take-apart (`-R`), `A` = assemble only (the
grid has no take-apart). See `corpus.txt` for why `A` puzzles read the
original's text summary: its `--json` aborts on those grids, a known bug of
the original CLI.

## Commands

```bash
just oracle                       # compare the fast tier
just oracle --tier fast full      # several tiers
just oracle --filter Pelikan -v   # one puzzle, show matches too
just oracle-baseline --tier full  # re-record the original's answers
python3 tests/oracle/oracle.py baseline-files --tier fast   # solved files from burrTxt2, into work/
```

## Levels of comparison

1. **Summary** (stage 3 onward): `--json` totals. Assembly and solution counts,
   the best level (`dotlevel`, `level`, `totalmoves`, and with `-R` also
   `moves` and `rotations`). The original's answers are committed in
   `baseline/<tier>.json`; `solvetime` is recorded but never compared.
2. **Files** (stage 4 onward): every solution and its disassembly tree, from
   the solved puzzle files burrTxt2 writes (`baseline-files`). The new core
   loads both its own and the original's solved file and compares solution by
   solution. The comparison is written in stage 4.

Speed is compared separately (tests/bench), with both binaries run back to
back on the same machine; numbers in the baseline files are only a guide.

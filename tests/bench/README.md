# Benchmarks

Solve-speed comparisons between the new `bt` and the original `burrTxt`,
run back to back on the same machine, alternating, best of `--repeat` runs.
Timings drift 5-10% as a machine heats up, and a busy machine (a sanitizer
run in the background) skews them far more: never compare against numbers
from an earlier run, and benchmark on a quiet machine.

```bash
just build release                               # the new bt
(cd ../burrtools_rotations && just build-release) # the original, build-rel (no assertions)
python3 tests/bench/bench.py                     # default set, 1 and all threads
python3 tests/bench/bench.py --plain             # without the vector search
python3 tests/bench/bench.py --set glar          # Burr-Glar, 3.1 million assemblies (minutes)
```

Each program reports the time of its own solve (reduce plus search, without
loading the file), which is what is compared. The new release build keeps
`BT_ASSERT` on; the original's `build-rel` has its assertions off.

Sets (assembler 0 puzzles while assembler 1 is not ported; puzzles that need
a part not ported yet are listed and skipped):
- `default`: the 6 x 10 pentominoes (written by the script), the oracle's
  slow tier (Climburr_OldVer, CoverUp3; CornerCube needs assembler 1),
  DraculasDentalDesaster;
- `glar`: `Burr-Glar.xmpuzzle` from the upstream burr-tools clone
  (`BT_BURR_GLAR` overrides the path).

The stage 8 gate (decision 0007) is "within 5% of the original or faster".

## Results

Stage 3a, 2026-10-06, M-series Mac (10 cores), assembly only:

| puzzle | threads | original s | new s | new/old |
|---|---|---|---|---|
| pentomino 6x10 (-r) | 1 | 0.946 | 0.945 | 1.00 |
| pentomino 6x10 (-r) | 10 | 0.190 | 0.191 | 1.01 |
| pentomino 6x10 (-r), plain search | 1 | 1.485 | 1.438 | 0.97 |
| pentomino 6x10 (-r), plain search | 10 | 0.283 | 0.274 | 0.97 |
| CoverUp3 (-r) | 1 | 0.050 | 0.048 | 0.96 |
| CoverUp3 (-r) | 10 | 0.051 | 0.050 | 0.98 |
| Burr-Glar | 1 | 25.6 | 27.4 | 1.07 |
| Burr-Glar | 10 | 7.89 | 7.71 | 0.98 |

Burr-Glar on one thread spends a quarter of its time in the rotation check
of each of its 3.1 million assemblies (`Assembly::smallerRotationExists`);
what is left of the gap is spread over the model's checked accessors
(`Problem::partShape`, `getPartMaximum`) and the voxel cache checks. Stage 8
looks at it.

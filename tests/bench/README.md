# Benchmarks

Solve-speed comparisons between the new `bt` and the original `burrTxt`,
run back to back on the same machine, alternating, best of `--repeat` runs.
Timings drift 5-10% as a machine heats up, and a busy machine (a sanitizer
run in the background) skews them far more: never compare against numbers
from an earlier run, and benchmark on a quiet machine.

```bash
just build release                               # the new bt
(cd ../burrtools_rotations && just build-release) # the original, build-rel (no assertions)
python3 tests/bench/bench.py                     # default set, 1 and all threads, best of 5
python3 tests/bench/bench.py --plain             # without the vector search
python3 tests/bench/bench.py --set glar          # Burr-Glar, 3.1 million assemblies (minutes)
```

Each program reports the time of its own solve (reduce plus search, without
loading the file), which is what is compared. The new release build keeps
`BT_ASSERT` on; the original's `build-rel` has its assertions off.

Sets:
- `default`: the 6 x 10 pentominoes (written by the script; also with
  BurrTools 2), the oracle's slow tier (Climburr_OldVer, CoverUp3,
  CornerCube), DraculasDentalDesaster, and the assembler 1 examples
  SolidSixPieceBurrs and DemoPieceGenerator;
- `glar`: `Burr-Glar.xmpuzzle` from the upstream burr-tools clone
  (`BT_BURR_GLAR` overrides the path).

The gate (decision 0007, from stage 8; AGENTS.md "Finishing a stage") is
"within 5% of the original or faster" on every row of the three runs.

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

Stage 3b, 2026-10-07, same machine, assembly only (best of 3):

| puzzle | threads | original s | new s | new/old |
|---|---|---|---|---|
| pentomino 6x10 (-r) | 1 | 0.949 | 0.956 | 1.01 |
| pentomino 6x10 (-r) | 10 | 0.193 | 0.198 | 1.03 |
| CornerCube (-r) | 1 | 0.013 | 0.013 | 1.00 |
| CornerCube (-r) | 10 | 0.015 | 0.015 | 1.00 |
| SolidSixPieceBurrs (-r) | 1 | 0.083 | 0.077 | 0.93 |
| SolidSixPieceBurrs (-r) | 10 | 0.030 | 0.028 | 0.93 |
| SolidSixPieceBurrs (-r), plain search | 1 | 0.289 | 0.281 | 0.97 |
| SolidSixPieceBurrs (-r), plain search | 10 | 0.065 | 0.064 | 0.98 |
| pentomino 6x10 (-r), plain search | 1 | 1.487 | 1.435 | 0.97 |
| pentomino 6x10 (-r), plain search | 10 | 0.296 | 0.282 | 0.95 |

DemoPieceGenerator and the rest take a few milliseconds and say nothing.
Assembler 0 is as fast as before it moved onto `DlxMatrix`.

Stage 4, 2026-10-07, same machine, best of 5; the take-apart rows (`-d`,
`-R` for rotations) are new:

| puzzle | threads | original s | new s | new/old |
|---|---|---|---|---|
| Climburr_OldVer (-rdR) | 1 | 1.395 | 1.348 | 0.97 |
| Climburr_OldVer (-rdR) | 10 | 0.282 | 0.287 | 1.02 |
| CoverUp3 (-rdR) | 1 | 5.108 | 4.862 | 0.95 |
| CoverUp3 (-rdR) | 10 | 1.197 | 1.135 | 0.95 |
| CornerCube (-rdR) | 1 | 12.553 | 12.056 | 0.96 |
| CornerCube (-rdR) | 10 | 5.112 | 4.866 | 0.95 |
| SolidSixPieceBurrs (-rd) | 1 | 0.086 | 0.080 | 0.93 |
| SolidSixPieceBurrs (-rd) | 10 | 0.032 | 0.030 | 0.94 |
| pentomino 6x10 (-r) | 1 | 0.948 | 0.976 | 1.03 |
| pentomino 6x10 (-r) | 10 | 0.185 | 0.186 | 1.01 |

A best of 3 just before, on a machine warm from the sanitizer runs, had the
pentomino rows at 1.04 to 1.09: compare back to back, best of 5.

Stage 8, 2026-10-08, same machine, best of 5, the three runs back to back
on a quiet machine (`bench.py`, `--plain`, `--set glar`). The gap of
Burr-Glar on one thread (1.06 at the start of the stage) is closed: the
profile (macOS `sample`) showed the rotation check of each of its 3.1
million assemblies paying for small out-of-line calls that the original's
build, with its assertions off, did not make: `GridType::getSymmetries()`
(a switch and a function per call, from `BT_ASSERT`s and
`normalizeTransformation`), `Problem::getPartMaximum` / `partShape`, the
voxel transformation cache's lookup, and an `Assembly` copied for every
symmetry checked. They are inline now (the tables behind
constant-initialised pointers, the cache's bound checked against its size)
and the check reuses one scratch assembly; the assertions stay on and no
answer changes (the oracle is green).

| puzzle | threads | original s | new s | new/old |
|---|---|---|---|---|
| pentomino 6x10 (-r) | 1 | 0.980 | 0.939 | 0.96 |
| pentomino 6x10 (-r) | 10 | 0.185 | 0.179 | 0.97 |
| CoverUp3 (-r) | 1 | 0.050 | 0.050 | 1.00 |
| CornerCube (-r) | 10 | 0.015 | 0.015 | 1.00 |
| SolidSixPieceBurrs (-r) | 1 | 0.082 | 0.066 | 0.80 |
| SolidSixPieceBurrs (-r) | 10 | 0.029 | 0.024 | 0.83 |
| Climburr_OldVer (-rdR) | 1 | 1.396 | 1.341 | 0.96 |
| Climburr_OldVer (-rdR) | 10 | 0.281 | 0.267 | 0.95 |
| CoverUp3 (-rdR) | 1 | 5.097 | 4.857 | 0.95 |
| CoverUp3 (-rdR) | 10 | 1.173 | 1.113 | 0.95 |
| CornerCube (-rdR) | 1 | 12.516 | 12.005 | 0.96 |
| CornerCube (-rdR) | 10 | 5.055 | 4.851 | 0.96 |
| SolidSixPieceBurrs (-rd) | 1 | 0.085 | 0.069 | 0.81 |
| SolidSixPieceBurrs (-rd) | 10 | 0.031 | 0.027 | 0.87 |
| **plain search** (`--plain`) | | | | |
| pentomino 6x10 (-r) | 1 | 1.479 | 1.430 | 0.97 |
| pentomino 6x10 (-r) | 10 | 0.273 | 0.258 | 0.95 |
| SolidSixPieceBurrs (-r) | 1 | 0.286 | 0.268 | 0.94 |
| SolidSixPieceBurrs (-r) | 10 | 0.056 | 0.054 | 0.96 |
| CoverUp3 (-rdR) | 1 | 5.104 | 4.862 | 0.95 |
| CornerCube (-rdR) | 1 | 12.627 | 12.082 | 0.96 |
| CornerCube (-rdR) | 10 | 5.141 | 4.904 | 0.95 |
| **Burr-Glar** (`--set glar`) | | | | |
| Burr-Glar | 1 | 25.316 | 21.202 | 0.84 |
| Burr-Glar | 10 | 7.048 | 5.898 | 0.84 |

The rows left out are under 0.06 s and equal (1.00), or the few
milliseconds of Demo Piece Generator and Draculas Dental Desaster; every
row of the three runs is in the gate. The 1.03 is a 0.27 s run on ten
threads (0.99 in the run at the start of the stage, 1.01 in the `--plain` run).

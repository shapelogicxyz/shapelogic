# Benchmarks

Solve-speed comparisons between the new `bt` and the original `burrTxt`,
run back to back on the same machine, alternating, best of `--repeat` runs.
Timings drift 5-10% as a machine heats up, and a busy machine (a sanitizer
run in the background) skews them far more: never compare against numbers
from an earlier run, and benchmark on a quiet machine.

```bash
just build release                               # the new bt
(cd ../burr-tools && just build-release)          # the original, build-rel (no assertions)
python3 tests/bench/bench.py                     # default set, 1 and all threads, best of 5
python3 tests/bench/bench.py --plain             # without the vector search
python3 tests/bench/bench.py --set glar          # Burr-Glar, 3.1 million assemblies (minutes)
python3 tests/bench/bench.py --set identical     # boxes of many pieces of one shape, piece ranges
python3 tests/bench/bench.py --set identical-long  # the same, a minute a run in the original
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
- `identical`, `identical-long`: boxes written by the script, filled with
  many pieces of one shape or with piece ranges (assembler 1 with no
  symmetry breaker: every cover is found in each orientation and checked),
  and one with no assembly at all (the search alone).

`--plain` sets the original's `BURRTOOLS_NO_SIMD` (until 2026-11-09 the
script set only the renamed `SHAPELOGIC_NO_SIMD`, which the original does
not read: the plain rows of the tables before that date compare the plain
search here with the original's vector search where it has one).

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

## Idea 5 (docs/V2-IDEAS-PLAN.md, decision 0024), 2026-11-09

M4 MacBook Pro, 4 performance and 6 efficiency cores (ten threads are
worth about six of one). The original is `build-rel`.

**Is dancing cells faster?** V2's own solver code (`dancingcell.cpp`, and
its `dancinglink.cpp`) compiled at -O3 and fed the placement matrix
assembler 1 builds (the placements from its placement browser; a part an
item with its minimum and maximum, a cell an item covered once), against
assembler 1's vector and plain searches on that matrix. "Search" counts
covers and does nothing with them; all find the same number.

| box | covers | assembler 1 vector, search s | plain | V2 dancing cells | V2 dancing links |
|---|---|---|---|---|---|
| 20 dominoes 2x4x5 | 535,229 | 0.094 | 0.187 | 0.137 | 0.049 |
| 12 L-trominoes 3x3x4 | 471,296 | 0.110 | 0.282 | 0.249 | 0.117 |
| ranges 3x3x3 | 716,835 | 0.485 | 1.681 | 1.424 | 0.877 |
| 3 of each tetromino 6x10 | 0 | 23.3 | 55.9 | 36.7 | 31.1 |
| 32 dominoes 8x8 | 12,988,816 | 2.13 | 5.05 | 3.73 | 1.32 |
| ranges 6x6 | 22,553,280 | 5.27 | 21.1 | 15.1 | 6.89 |

Dancing cells loses to the vector search on every row and to V2's own
dancing links on every row. The time was elsewhere: for 32 dominoes in
8 x 8 the search was 2.1 s of a 58 s solve; building an assembly for each
cover took 16.6 s and checking it against its rotations 39 s.

**The rotation check on the rows** (one thread, s; `--no-row-check` is the
check on built assemblies with the new sort):

| puzzle | before | sort only | row check |
|---|---|---|---|
| 20 dominoes 2x4x5 | 1.13 | 0.75 | 0.19 |
| ranges 3x3x3 | 1.32 | 1.29 | 0.57 |
| 32 dominoes 8x8 | 58.1 | 36.7 | 8.9 |
| ranges 6x6 | 40.2 | 36.0 | 9.9 |
| Burr-Glar | 21.4 | 21.4 | 15.4 |

**Threads.** Busy is CPU time over wall time, the cores in use on
average. Before, with the tasks split finer up front (`--split-tasks 256
--split-depth 6`), and with tasks that split while they are searched:

| puzzle | threads | before s (busy) | 256 tasks s (busy) | splitting s (busy) |
|---|---|---|---|---|
| Burr-Glar | 4 | 17.8 (1.3) | 7.7 (3.8) | 5.4 (3.7) |
| Burr-Glar | 10 | 5.95 (7.0) | 5.5 (8.3) | 4.6 (8.0) |
| pentominoes 3x4x5 | 4 | 3.76 (3.8) | 4.12 (3.9) | 3.51 (4.0) |
| pentominoes 3x4x5 | 10 | 2.24 (8.8) | 2.32 (8.9) | 2.03 (9.7) |
| 3 of each tetromino 6x10 | 10 | 4.06 (9.1) | 4.17 (9.3) | 3.76 (9.7) |

(Burr-Glar's "before" has the old rotation check, the others are the
search alone.) A search of the pentominoes box on four threads stopped
every 0.25 s did not finish in a minute before; stopped every 0.05 s it
now finishes in 3.9 s with the iterations of the 3.7 s run that is never
stopped.

**Against the original** (`bench.py`, best of 3; the long set best of 2).
The default set: every row at 1.00 or below; the pentominoes 0.96 on one
thread and on ten, Solid Six Piece Burrs (-r) 0.57 and 0.63 (0.80 and 0.83
before), the take-apart rows 0.95. `--plain`, now plain against plain:
the pentominoes 0.96 and 0.89, Solid Six Piece Burrs 0.87 and 0.88; one
row above 1.00, CoverUp3 (-r) on ten threads at 0.052 s against 0.053.

| puzzle | threads | original s | new s | new/old |
|---|---|---|---|---|
| Burr-Glar | 1 | 25.401 | 15.558 | 0.61 |
| Burr-Glar | 4 | 21.461 | 5.326 | 0.25 |
| Burr-Glar | 10 | 7.325 | 4.600 | 0.63 |
| 20 dominoes 2x4x5 | 1 | 1.495 | 0.190 | 0.13 |
| 20 dominoes 2x4x5 | 4 | 0.525 | 0.076 | 0.14 |
| 20 dominoes 2x4x5 | 10 | 0.395 | 0.080 | 0.20 |
| 12 L-trominoes 3x3x4 | 1 | 1.036 | 0.172 | 0.17 |
| 12 L-trominoes 3x3x4 | 10 | 0.264 | 0.050 | 0.19 |
| ranges 3x3x3 | 1 | 1.715 | 0.563 | 0.33 |
| ranges 3x3x3 | 10 | 0.372 | 0.099 | 0.27 |
| 32 dominoes 8x8 | 1 | 71.026 | 8.903 | 0.13 |
| 32 dominoes 8x8 | 10 | 21.573 | 2.561 | 0.12 |
| ranges 6x6 | 1 | 54.436 | 9.852 | 0.18 |
| ranges 6x6 | 10 | 12.618 | 4.489 | 0.36 |
| 3 of each tetromino 6x10 | 1 | 23.159 | 23.347 | 1.01 |
| 3 of each tetromino 6x10 | 4 | 6.211 | 6.371 | 1.03 |
| 3 of each tetromino 6x10 | 10 | 4.085 | 3.938 | 0.96 |

The tetromino box has no assembly: the search alone, which the original
has too. It first read 1.03 on one thread and 1.07 on four: the
bookkeeping the vector searches keep for handing branches on cost every
search 2 to 3%. They are compiled twice now (`search<Split>`), and a
search that does not split is the code it was; one that does pays about
2% for it, which the hand-offs win back from five threads or so.

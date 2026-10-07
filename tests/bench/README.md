# Benchmarks

From stage 3: solve-speed comparisons between the new `bt` and the original
`burrTxt`, run back to back on the same machine (timings drift 5-10% as a
machine heats up, so never compare against numbers from an earlier run).

Planned set (decision 0007, stage 8 gate: within 5% or faster):
- the oracle's slow tier: Climburr_OldVer, CoverUp3, CornerCube (`-R`);
- the full TIC tier as a whole;
- `examples/SolidSixPieceBurrs.xmpuzzle`, `DemoPieceGenerator.xmpuzzle`;
- single-thread and all-thread runs.

Both programs take the same thread limit (`-t n`) so the comparison is fair.

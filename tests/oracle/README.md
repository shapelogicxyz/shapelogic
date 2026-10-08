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
just oracle --tier all --threads 1
                                  # every field, bt -t 1
just oracle --tier all --solver bt2
                                  # BurrTools 2 against the original's BT2
just oracle --tier all --solver crowell
                                  # Andrew Crowell's take-apart against the original's
just oracle-baseline --tier fast --solver bt2
                                  # record those (baseline/<tier>-<solver>.json)
python3 tests/oracle/oracle.py baseline-files --tier all   # solved files from burrTxt2 -R, into work/old
python3 tests/oracle/oracle.py compare-files --tier all -j 4
                                  # the new core's solved files against those (stage 4)
just oracle-resume                # paused searches continue in the other program
just oracle-baseline --tier full  # re-record the original's answers
python3 tests/oracle/oracle.py baseline-files --tier all    # solved files from burrTxt2, into work/
just oracle-roundtrip             # the file round trip (stage 2 onward), see below
```

## Solved files (stage 4 onward)

`baseline-files` runs the original's `burrTxt2 -R -d` (with `--rotations`
for `R` puzzles, without `-d` for `A` puzzles) on a copy of each corpus file
and keeps the solved file it writes beside it (`work/old/<path>ttt`).
`compare-files` does the same with the new core (`bt_solve_file`, which
runs a `SolveSession` as burrTxt2 runs its solve thread: the matrix, reduce,
the saved position, the search, the take-apart queue and its workers,
solutions sorted by moves) into `work/new`, reads both files with the new
core (`bt_solutions`) and compares problem 0: the state, the assembly and
solution counts, and the saved solutions. The original keeps the ten best
by moves, and which of several with the same moves it keeps depends on the
order its threads took them apart; so the new file keeps every solution
(`--limit 0` with `-d`), the original's moves must be the top of the new
list, and each solution the original kept must be in the new file with the
same assembly, separation tree (every state of every node), level text and
move counts. `A` puzzles compare the counts only (their saved assemblies
keep the solve thread's limit of ten).
`--solver crowell|bt2` uses `work/old-<solver>` and `work/new-<solver>`.

## Recorded values for unit tests

Parts that the CLI cannot reach are compared through recordings: a tool is
compiled against the original's `build/libburr_core.a`, run once, and its
output committed under `tests/data/`. The unit tests compare the port with
it.

```bash
python3 tests/oracle/oracle.py grid-dump     # tests/data/grid_dump.txt (stage 1)
python3 tests/oracle/oracle.py model-dump    # tests/data/model_dump.txt (stage 2)
python3 tests/oracle/oracle.py app-dump      # tests/data/app_dump.txt (stage 6)
python3 tests/oracle/oracle.py record-saves  # tests/data/saved_by_original/ (stage 2)
python3 tests/oracle/oracle.py record-positions  # tests/data/positions_by_original.txt (stage 3)
```

`grid_dump_old.cpp` and `tests/unit/grid/test_grid_dump.cpp` run the same
code (`tests/unit/grid/grid_dump.h`) through two small adapters, so the
recording and the check cannot drift apart. `model_dump_old.cpp` and
`tests/unit/model/test_model_dump.cpp` are two copies of one dump (the
model APIs differ too much for adapters): change both together. So are
`app_dump_old.cpp` and `tests/unit/app/test_app_dump.cpp`: the meshes of
every shape (drawing and flat face by face, as hashes; of the wire frame
only the Euler characteristic, because the original's hole filling visits
faces in address order and so merges different faces from run to run),
the status window's table, the fit line, and the playback tracks of the
first solutions (gltfexport.cpp's sampler, copied into the dump tool: it
lives in an anonymous namespace there). The dumps and `record-saves` run
over the fixtures in `tests/data/puzzles/`.

## File round trip (stage 2 onward)

`just oracle-roundtrip` loads and saves every file with the original
(`roundtrip_old.cpp`, built against its library) and with the new core
(`tests/tools/roundtrip.cpp`, `build/release/tests/bt_roundtrip`): all corpus
tiers, the sliding, stacking and rotation fixtures the corpus leaves out
(`ROUNDTRIP_EXTRA` in `oracle.py`), and the original's solved files under
`work/old` (run `baseline-files --tier all` once). For each file:

1. the new save is the same XML as the original's (attribute order and
   whitespace-only text ignored), and it reports how many are byte-identical;
2. the new save loads and saves to itself;
3. the original reads the new save to the same file it wrote itself.

It also counts the files whose saves differ from their input; those are the
normalisations the original makes too (`timeMs`, default attributes), listed
with `-v`.

## Saved search positions (stage 3 onward)

`just oracle-resume` builds `resume_old.cpp` against the original's library
and runs it beside `bt_resume` (`tests/tools/resume.cpp`, the same commands
on the new core; both take the solver: `classic` or `bt2`, the assembler
being the one `findAssembler` picks). For each puzzle it pauses at three
points in each program and continues the position in the other: what was
reported before the pause and after must be every assembly exactly once.
Single-thread pauses must also save the very same text in both programs.

- `RESUME_SET`: assembler 0 puzzles, with the Classic solver on four
  searches (plain and vector on one thread, split over 4 threads with and
  without the vector search), and with BurrTools 2 on 1 and 4 threads.
- `RESUME_SET_ASSEMBLER1`: assembler 1 puzzles (Cube in Cage, Demo Piece
  Generator, Solid Six Piece Burrs) on the four Classic searches.

Two cases are accounted for explicitly:
- when assembler 0's plain search stops right after placing the last piece
  (`pos` equal to the number of pieces), the original reads one entry past
  its row list while saving, and writes past it while restoring; its
  checked build traps. The new core writes `(0 0)` for that entry. Such
  pauses are continued by the new program only and counted as known;
- BurrTools 2 saves the original's dancing links position, which its
  dancing cells search never moves (`0 <iterations> (0 0)`). One thread must
  save the same text in both programs; the new program must refuse to
  continue any such position (`CannotRestoreInterrupted`: the original took
  it up as a fresh search and reported everything again). Counted apart.

`oracle.py record-positions` writes `tests/data/positions_by_original.txt`
(`POS <fixture> <solver> <threads> <plain> <reduce> <stopAfter>`, the text,
the assemblies before the pause) for `test_positions.cpp`: assembler 0 and
BurrTools 2 on Pelikan and Dracula, assembler 1 on Cube in Cage and Demo
Piece Generator.

`bt` exited with code 3 when a problem needed a part not ported yet (stage
3a); since stage 3b every problem has its assembler, and `compare` keeps its
`--allow-unported` for later parts.

## Levels of comparison

1. **Summary** (stage 3 onward): `--json` totals. Assembly and solution counts,
   the best level (`dotlevel`, `level`, `totalmoves`, and with `-R` also
   `moves` and `rotations`). The original's answers are committed in
   `baseline/<tier>.json`; `solvetime` is recorded but never compared.
2. **Files** (stage 4 onward): every solution and its disassembly tree, from
   the solved puzzle files burrTxt2 writes (`baseline-files`) and those the
   new core writes the same way (`compare-files`); see "Solved files".

Speed is compared separately (tests/bench), with both binaries run back to
back on the same machine; numbers in the baseline files are only a guide.

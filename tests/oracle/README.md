# Comparison harness ("oracle")

The original BurrTools is the specification. This harness runs the original
`burrTxt` and the new `shapelogic_cli` on the same puzzles and compares their answers.
A solver stage is done only when the harness reports no differences
(docs/decisions/0007-port-against-the-original.md).

## Setup

- The original checkout at `../burrtools_rotations` (or `BT_OLD_REPO`), built
  with `just build` there, so `build/burrTxt` and `build/burrTxt2` exist.
- The new `shapelogic_cli` is built by `just oracle` itself (release preset).

## Corpus and tiers

[`corpus.txt`](corpus.txt) lists the puzzles, by path in the original repo:

| Tier | Puzzles | Time (original, M-series Mac) | When |
|---|---|---|---|
| fast | 19 bundled examples and fixtures | ~2 s | every run |
| full | 158 TIC puzzles with rotation take-apart | ~23 s | before finishing a solver stage |
| slow | Climburr_OldVer, CoverUp3, CornerCube | ~7 s | benchmarks, final checks |
| sliding | the 3 sliding fixtures and 33 puzzles made with the original's code | ~1 min full, seconds fast/deep | sliding work (deferred part 1) |
| stacking | the 2 stacking fixtures and 39 puzzles made with the original's code | seconds | stacking work (deferred part 3) |

Flags per puzzle: `R` = rotation take-apart (`-R`), `A` = assemble only (the
grid has no take-apart). See `corpus.txt` for why `A` puzzles read the
original's text summary: its `--json` aborts on those grids, a known bug of
the original CLI. Sliding puzzles have their own flags: `N` nested slides,
`P` partially nested, `L` too long for the full search (fast and deep
only), `O` compared on one thread only (see "Sliding puzzles" below);
so have stacking puzzles: `Q` not for the Panex Solver, `L` no `-o all`
text run, `I` a setup that is not ready (see "Stacking puzzles" below). A
path beginning `alpha:` is a file of this repo, not of the original's.

## Commands

```bash
just oracle                       # compare the fast tier
just oracle --tier fast full      # several tiers
just oracle --filter Pelikan -v   # one puzzle, show matches too
just oracle --tier all --threads 1
                                  # every field, bt -t 1
just oracle --tier all --solver crowell
                                  # Andrew Crowell's take-apart against the original's
just oracle-baseline --tier fast --solver crowell
                                  # record those (baseline/<tier>-<solver>.json)
python3 tests/oracle/oracle.py baseline-files --tier all   # solved files from burrTxt2 -R, into work/old
python3 tests/oracle/oracle.py compare-files --tier all -j 4
                                  # the new core's solved files against those (stage 4)
python3 tests/oracle/oracle.py baseline-text --tier all    # burrTxt's text output (stage 8)
just oracle-text --tier all       # bt's text output against it, byte for byte
just oracle-resume                # paused searches continue in the other program
just oracle-baseline --tier full  # re-record the original's answers
python3 tests/oracle/oracle.py baseline-files --tier all    # solved files from burrTxt2, into work/
just oracle-roundtrip             # the file round trip (stage 2 onward), see below
just oracle-imports               # imports, Puzzlecad export, grid conversion (deferred part 6)
just oracle-python                # the Python module against the original's (deferred part 7)
```

## Solved files (stage 4 onward)

`baseline-files` runs the original's `burrTxt2 -R -d` (with `--rotations`
for `R` puzzles, without `-d` for `A` puzzles) on a copy of each corpus file
and keeps the solved file it writes beside it (`work/old/<path>ttt`).
`compare-files` does the same with the new core (`bt --save`, burrTxt2's
mode, which runs a `SolveSession` as burrTxt2 runs its solve thread: the matrix, reduce,
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
`--solver crowell` uses `work/old-<solver>` and `work/new-<solver>`.

## Text output (stage 8 onward)

`shapelogic_cli` prints what burrTxt prints. `baseline-text` runs the original's
`burrTxt` (build/, one thread) on every corpus puzzle in several ways and
records each run's stdout, stderr and exit code: the hashes in
`baseline/text-<tier>[-<solver>].json` (committed), the text itself under
`work/text-old*` (to show where a later run differs). `compare-text`
(`just oracle-text`) runs `shapelogic_cli` the same way and compares byte for byte,
with the solve time blanked. The runs (`text_runs` in oracle.py):

| run | arguments | input |
|---|---|---|
| `solve` | `-t 1 -dsp` (`-R` for R puzzles; `-s` only for A puzzles) | the corpus file |
| `quiet` | `-t 1 -q -n -r` (with `-d`) | the corpus file |
| `all` | `-t 1 -q -o all` (with `-d`; fast tier only: some TIC files hold further problems that run for many minutes) | the corpus file |
| `retake` | `-t 1 -x -sp` (not for A puzzles) | burrTxt2's solved file (`work/old`) |
| `retake-saved` | `-t 1 -x` (not for A puzzles) | the corpus file |
| `ask` | `-a s1` | burrTxt2's solved file |
| `comment` | `-a c` | the corpus file |

So `baseline-files --tier all` must have run first (for the solved files).
Three things are accounted for: the original dies (an assertion,
SIGABRT) when it draws a take-apart (`-p`) of an assembly with a piece
that is not placed (Demo Mirror Paradox, Demo Piece Generator, Solid Six
Piece Burrs: piece ranges), since its `print()` transforms every piece;
such a run is recorded by what it wrote before it died, and bt's output
must begin with those bytes (and bt must end normally). Further, `shapelogic_cli` runs with `--original-drawings`, which
draws the take-apart's parts as burrTxt did (with the wrong shapes below
the first separation; `shapelogic_cli` draws each piece as itself otherwise); and the
original's assembler 1 line "range optimisation used min …, max …" on
stderr is dropped (bt does not print it). One thread, because the order in
which solutions are printed is the order the search hands them over.

## Sliding puzzles (deferred part 1)

`--tier all` leaves the `sliding` tier out: run it by name. The full
search is the default (`--solver full` is the same, with the same
baselines); `--solver fast` and `--solver deep` run the other two:

```bash
python3 tests/oracle/oracle.py sliding-puzzles      # tests/data/puzzles/sliding/ from the original's code
just oracle-baseline --tier sliding                 # burrTxt --json (and --solver fast, deep)
just oracle --tier sliding --threads 1              # every puzzle; without --threads 1, those not flagged O
python3 tests/oracle/oracle.py baseline-files --tier sliding
python3 tests/oracle/oracle.py compare-files --tier sliding
python3 tests/oracle/oracle.py baseline-text --tier sliding
just oracle-text --tier sliding
python3 tests/oracle/oracle.py record-slide-routes  # tests/data/slide_routes.txt
```

`sliding-puzzles` builds `sliding_puzzles_old.cpp` against the original's
library: the puzzles of its `test/test_sliding.cpp` (and a few fuzzed
ones), made by its own sliding code and saved. The baselines are the
original's runs on one thread: when a puzzle has several start layouts,
which start's path the original keeps for an ending depends on the order
the starts arrive in, so on several threads its counts vary from run to
run (flag `O`; such puzzles are compared with `--threads 1` only, the
others on any number). burrTxt2 refuses `-d` on the sliding grid, so the
solved files come from the original's solve thread
(`slide_solve_old.cpp`, as the GUI's Solve ran it) and are compared state
by state with `bt --save`'s. `record-slide-routes` (`slide_routes_old.cpp`)
solves twelve of the puzzles with the original and records every move's
route and the player's positions at quarter steps, with the solved files
in `tests/data/slide_solved_by_original/`; `test_slide_routes.cpp` checks
the port against them. Never add the commercial puzzle "One Way or
Another".

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
python3 tests/oracle/oracle.py record-analysed   # tests/data/analysed_by_original/ (stage 7)
python3 tests/oracle/oracle.py stl-dump      # tests/data/stl_dump.txt (deferred part 5)
python3 tests/oracle/oracle.py stl-solution  # tests/data/stl_solution_by_original/ (deferred part 5)
python3 tests/oracle/oracle.py record-imports  # tests/data/imports_by_original/ (deferred part 6)
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
over the fixtures in `tests/data/puzzles/`. `record-analysed`
(`analyse_old.cpp`) takes the saved assemblies of the fixtures with a
take-apart apart again as the GUI's "Add disassembly" for all solutions
did (the old trees dropped first; with rotations and Crowell's take-apart
for the rotation fixture) and saves them;
`tests/unit/app/test_analysis_files.cpp` does the same with the new core's
`solution.analyse` and compares the files byte for byte.

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
on the new core; both take the solver: `classic`, the assembler
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

`shapelogic_cli` exited with code 3 when a problem needed a part not ported yet (stage
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

## Stacking puzzles (deferred part 3a)

`--tier all` leaves the `stacking` tier out too: run it by name. The
default solver is the Stacking Solver (burrTxt's); `--solver panex` runs
the Panex Solver on the puzzles it takes (flag `Q` marks the others). Each
run gets a Panex folder of its own (`SHAPELOGIC_PANEX_DIR` under
`work/panex-tmp`), so the user's cache and other runs' saved searches are
never touched:

```bash
python3 tests/oracle/oracle.py stacking-puzzles      # tests/data/puzzles/stacking/ from the original's code
just oracle-baseline --tier stacking                 # burrTxt --json (and --solver panex)
just oracle --tier stacking                          # all threads; --threads 1 the same
python3 tests/oracle/oracle.py baseline-files --tier stacking   # the original's solve thread (stack_solve_old)
python3 tests/oracle/oracle.py compare-files --tier stacking
python3 tests/oracle/oracle.py baseline-text --tier stacking
just oracle-text --tier stacking
python3 tests/oracle/oracle.py panex-resume          # stopped in one program, carried on in the other
python3 tests/oracle/oracle.py blockpack-check       # packed by one program, unpacked by the other
```

`stacking-puzzles` builds `stacking_puzzles_old.cpp` against the original's
library: the puzzles of its `test/test_stacking.cpp` (Hanoi, the distance
rule, Panex Jr with and without its pocket, the Panex swaps with and
without the outside channel, plain rods under every rule, the classic
towers, nine rods) and a few more (copies of a disc, a pocket of two, the
start equal to the goal, no path, a setup that is not ready, two boards).
burrTxt2 has no stacking solver, so the solved files come from the
original's solve thread as the GUI's Solve ran it (`stack_solve_old.cpp`)
and are compared state by state with `bt --save`'s. Flag `L`
(PanexLevel3to10: its later problems run for hours) leaves out the `-o
all` text run; flag `I` (a setup that is not ready) leaves out the solved
files, since the GUI refused to solve it. burrTxt's `-a s1` dies on a
solved stacking file (it draws the assembly into a result shape the
problem does not have); `shapelogic_cli` prints what it printed before and no drawing,
which the text comparison accounts for as it does the other crashes.

`panex-resume` runs five Panex searches (the 5-disc swap, Panex Jr, the
4-disc swap with the outside channel, plain rods under the distance rule,
Panex 6 of PanexLevel3to10) three ways (the levels kept in memory; kept on
disk, every level; kept on disk thinned to a budget): straight in both
programs (the same path and counts), then stopped at a sweep of depths in
both (`panex_resume_old` and `bt_panex_resume`, `stopAtDepth`): the saved
folders must be byte for byte alike (but for the solving time in
`state.bin`), and each must carry on in both programs to the same path.

## The STL meshes (deferred part 5)

`stl-dump` builds `stl_dump_old.cpp` against the original's library and
records, for every shape of the top-level fixtures with its grid's
default parameters, the triangles the original's STL writer writes (its
binary file read back: the distinct vertices and the triangles as index
triples; for meshes of more than 5,000 triangles, the sphere grid's, the
box and the sums), the faces of the view's "STL" style (their attributes
as a hash, the box), and the text STL of the first small shape.
`tests/unit/mesh/test_stl_dump.cpp` makes the same dump with the new core
and compares it line by line, numbers within 1e-5 (the two programs write
the same bytes on this Mac; the tolerance is for other compilers), and
checks every file is a closed, consistently oriented surface. The large
fixtures (12PieceSeparation, AugmentedSecondStellation, the BallRooms) are
its `[slow]` case: the Minkowski mesher takes 50 s on one shape in the dev
build. `stl-solution` records what the original's Export Solution Pieces
to STL writes for CubeInCage's first solution (the dialog's naming copied
from `stlexportsolution.cpp`, which is GUI code; the files written by the
original's exporter): `tests/unit/app/test_stl_export.cpp` and the front
end's e2e test compare the names and bytes.

## Imports, the Puzzlecad export and grid conversion (deferred part 6)

`imports_old.cpp` (built against the original's library) and
`tests/tools/imports.cpp` (`bt_imports`, through the new core's
document) take the same arguments: `ps3d|scad <in> <out>` imports a file
and saves the puzzle, `scad-export <puzzle> <problem> <name>` writes the
Puzzlecad file (cb_Export_Scad's checks copied, as they are GUI code),
`convert <puzzle> <grid> <out>` converts and saves; exit 3 is a refusal.

```bash
python3 tests/oracle/oracle.py imports         # both programs, compared (~10 s)
python3 tests/oracle/oracle.py record-imports  # tests/data/imports_by_original/
```

`imports` runs both tools on the inputs of `tests/data/imports/` (small
PuzzleSolver3D and Puzzlecad files made by hand for the loaders' corners),
the original's `tictest/openscad/*.scad` (read in place), and every corpus
puzzle (all tiers, sliding, stacking, the round trip's extras, the
fixtures): converted to the rhombic and tetra-octa grids and to one the
original refuses, exported to Puzzlecad for each problem and for none,
each of the original's exports imported again, and a PuzzleSolver3D file
made of each cube-grid puzzle's shapes (`ps3d_text`) imported. The saves
must be the same bytes, the exports the same text, and a refusal must be a
refusal in both. `record-imports` keeps the original's results for the
committed inputs and the cube-grid fixtures in
`tests/data/imports_by_original/` (saves gzip-compressed as the original
wrote them, `<input>_<ext>.refused` for a refusal) for
`tests/unit/io/test_imports.cpp`.

## The Python module (deferred part 7)

The original's own Python module is the oracle (its meson build folder,
`burrtools*.so`: build it with `meson compile -C build` in the original's
checkout). `python_transcript.py` writes one JSON line per thing it reads
through the module on PYTHONPATH; `oracle.py python` runs it with each
module and the two transcripts must be the same, line for line.

```bash
just oracle-python                          # the fast tier and 300 built puzzles (~35 s)
just oracle-python --tier full --generated 0
python3 tests/oracle/oracle.py python --new-module build/dev/python --filter Pelikan -v
```

A transcript holds: the error cases (each wrong call's exception type,
and what the cells say after them); puzzles built through the API from a
seeded generator (a box cut into pieces at random, some cells variable,
a spare cube with a range of counts), their saved XML, and their solves;
and for each corpus puzzle of the tiers (brick tiers only: the original's
module assembled sliding and stacking puzzles as brick puzzles) every
shape cell by cell, every problem with its piece counts, the reprs, and
three solves of each problem (assemblies; solutions with their
take-aparts; reduced with mirrors and rotations kept). Each is run twice:
on one thread (the original's module held to it by `SHAPELOGIC_THREADS=1`,
the new one by `threads=1`), compared in order with the numbers and the
search's iterations; and on all threads, compared as sorted lists without
the numbers. A file's texts that are not UTF-8 (Prisgon's comment) raise
`UnicodeDecodeError` in both modules; the transcript records that.

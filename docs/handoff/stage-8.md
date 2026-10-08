# Handoff: stage 8 (the `bt` CLI and performance)

**Date:** 2026-10-08 · **Next:** the plan's nine stages are done. What
comes next is the web front end (Electron + TypeScript + three.js over
`bt-engine`), then the later features of PLAN.md; see "What comes next"
at the end.

## Done

### `bt`, the command line (`cli/`)
A port of the original's `burrTxt` for brick puzzles (`src/burrTxt.cpp`
and `src/lib/print.cpp`) and of `burrTxt2` (`src/burrTxt2.cpp`) as a mode
of it. Files: `options.h` (the options and burrTxt's rules), `text_print.h`
(the drawings), `dot_level.h` (the level text as `--json` reads it),
`main.cpp` (the modes).
- **burrTxt's options:** `-h [TYPE]` (brick, sliding, stacking), `--json`,
  `--progress` (stderr, once a second, burrTxt's line), `-d`, `-p`, `-s`,
  `-q` (turns `-p` and `-s` off, so order matters, as in burrTxt), `-n`,
  `-o n`, `-o all`, `-x`, `-a s0 / s1 / c`, `-r`, `-R`, `-C`, `-t n`,
  `--solver` (checked once the puzzle's type is known, with burrTxt's
  messages for another type's names), short options combined (`-dR`),
  `-o` and `-a` refused inside a cluster, `--json` refused with `-a`,
  `-x`, `-o all`, `-p`, `-s`. The exit codes are burrTxt's (0; 1 for no
  file and for `--json` errors; 2 for a mistake).
- **The text** is burrTxt's byte for byte: " The puzzle:" and every shape
  and problem, "start reduce" / "finished reduce", "problem: <name>" with
  `-o all`, "level: …" per solution, the assembly (`-s`) and the
  take-apart (`-p`) drawn layer by layer, the summary line with the
  iterations; on stderr "using assembler …" and "removed N rows and M
  columns" (the original's library printed them; the core returns them
  and the CLI prints them). Checked by the new harness command (below):
  **1081 of 1081** runs identical over the 180 corpus puzzles.
- **`-x`** takes the saved solutions apart again with one `Disassembler`
  (`-R` and `--solver` as given; its levels on a pool), as burrTxt did,
  printing the root's moves; the analysis of stage 7 (`app::Analysis`) is
  the GUI's "Add disassembly" and stores separations, which `-x` does not.
- **`-a`**: the comment, the saved solutions' pieces, with `s1` their
  assemblies drawn.
- **`--save FILE`** is burrTxt2's mode: a `SolveSession` (reduce, a paused
  solve carried on unless `--restart`, the solutions sorted by moves and
  kept up to `--limit n`, 10 by default, 0 all), the puzzle written to
  FILE; its options as long ones: `--restart` (burrTxt2 `-R`),
  `--count-only` (`-c`), `--keep-mirrors` (`-m`), `--keep-rotations`
  (`-r`), `--drop-disassemblies` (`-p`), with `-d`, `-R` (burrTxt2's
  `--rotations`), `-C`, `-t`, `-o n` / `-o all` (`-b`), `--solver`,
  `--progress` (burrTxt2's line) and `-q`; burrTxt2's messages ("outputting
  into", "is already solved …", "error in solver"), and a summary line per
  problem. **Decided:** `bt` takes this mode, so the harness tool
  `bt_solve_file` went; `oracle.py compare-files` solves with
  `bt --save` now (180/180 as before).
- **Refused:** sliding and stacking puzzles with burrTxt's message for a
  puzzle it cannot handle ("The puzzles contains features not yet
  supported by burrTxt", on stdout, or stderr with `--json`) and why on
  stderr, exit 1 (the stacking fixture aborted before); `-a` still reads
  them, `-R`/`-C` give burrTxt's "for brick puzzles" message, and their
  options (`--nested-slides`, `--high-memory`, the Panex ones) are refused
  as not supported yet. A problem without a result shape is refused
  (exit 2; the original's assemblers failed an assertion).
- **Kept:** `--json` byte-compatible (the oracle reads it), the A/B
  switches (`--no-simd`, `--no-level-threads`, `--no-fast-analysis`,
  `--no-fast-rotations`, `--no-rotation-anchor`, `--vector-memory`),
  `--version`.
- `tests/cli/test_cli.py` (ctest `cli`, in the quick suite): the option
  rules and exit codes, the refusals, a problem without a result, the
  solve's text, the drawings with and without `--original-drawings`,
  `--save` then `-a` and `-x` on the saved file, and that a `--progress`
  run ends (a bug of this stage's first version: the ticker spun at its
  stop, since `condition_variable_any::wait_for` with a stop token
  answers false both on a timeout and on a stop).

### The harness: text output (`oracle.py baseline-text` / `compare-text`, `just oracle-text`)
burrTxt's runs recorded once (hashes in `baseline/text-<tier>.json`,
committed; the text under `work/text-old` for showing a difference), `bt`
compared byte for byte, stdout, stderr and exit code, the solve time
blanked. Per puzzle on one thread: `-dsp` (`-s` for assemble-only grids),
`-q -n -r -d`, `-q -o all -d` (fast tier), `-x -sp` on burrTxt2's solved
file, `-x` on the corpus file, `-a s1` on the solved file, `-a c`. Also
recorded for the fast tier with `--solver bt2` and `crowell`. See
tests/oracle/README.md ("Text output") for what is accounted for.

### Performance
`tests/bench/bench.py` (the default set, `--plain`, `--set glar`; best of
5 by default now): every row within the gate. Burr-Glar on one thread
was 1.06 at the start of the stage; the profile showed its rotation check
(every one of 3.1 million assemblies) paying for out-of-line calls the
original's build, its assertions off, never made. Changed, without an
answer changing:
- `GridType::getSymmetries()` is inline: the three tables behind
  constant-initialised pointers (`detail::kSymmetriesTable0/1/2`, in
  place of the functions `symmetriesTable0/1/2`).
- `Voxel::getHotspot` / `getBoundingBox` / `selfSymmetries` /
  `normalizeTransformation` are inline; the transformation cache's bound
  is checked against its size instead of through the grid's tables.
- `Problem::getPartMinimum` / `getPartMaximum` / `getShapeIdOfPart` are
  inline (with their `BT_ASSERT`).
- `Assembly::smallerRotationExists` reads the result's symmetry once and
  reuses one scratch assembly for every transformation; `transform` finds
  the part's shape through the inline accessors.
Burr-Glar is 0.84 of the original now on 1 and on 10 threads, Solid Six
Piece Burrs 0.80 (tests/bench/README.md has the tables).

### From stage 7
- `bt-engine` puts stdin and stdout in binary mode on Windows (`_setmode`),
  for its frames; not built on MinGW yet, so untested.
- **The TypeScript types are left to the front end's first stage:** the
  schema in `api/` is the source; generating them belongs with the
  client that uses them (its tooling, its naming).

## Gate results
| Check | Result |
|---|---|
| `just test-all` (dev, warnings as errors) | pass: 444 cases (18 `[slow]`), `api-schema`, `api-docs`, `contract`, `cli`; 325 s |
| quick suite repeated | 4 more times in dev, 3 in release: all pass |
| `just test-asan` (address + undefined) | pass: every case, `[slow]` included, the contract and `cli` suites against the ASan builds; 19 min |
| `just test-tsan` | pass: 69 `[parallel]` cases, no report; the contract suite against the TSan engine, 0 failed |
| `just oracle` every field, all tiers, Classic | **180/180** all threads, **180/180** `--threads 1` |
| `just oracle --solver bt2`, every field, all tiers | **180/180** all threads, **180/180** `--threads 1` |
| `just oracle --solver crowell`, every field, all tiers | **178/178** all threads, **178/178** `--threads 1` (InsomniAC1 and CoverUp3: the original's hour-long timeout) |
| `just oracle-files --tier all` (now with `bt --save`) | **180/180** Classic, **180/180** BurrTools 2 |
| `just oracle-resume` | **395/395** (76 single-thread pauses the same text) |
| `just oracle-roundtrip` | **365/365** byte-identical |
| `just oracle-text --tier all` (new) | **1081/1081** runs identical, Classic (180 puzzles); fast tier **115/115** with BurrTools 2 and **115/115** with Crowell |
| benchmarks (`bench.py`, `--plain`, `--set glar`; best of 5, quiet machine) | every row within 5% or faster: worst 1.03 (a 0.27 s run), Burr-Glar **0.84** on 1 and 10 threads (tests/bench/README.md) |
| `just check` | **not run: LLVM still not installed** |

## Deliberate differences from burrTxt / burrTxt2
1. **Drawings below the first separation** show each piece as itself
   (see "Found in the original"); `--original-drawings` draws as burrTxt
   did, for the harness.
2. **Drawing a take-apart with unplaced pieces** works (burrTxt died).
3. **Sliding and stacking puzzles are refused** (burrTxt solves them);
   exit 1.
4. **`-x` on a grid without a take-apart** prints nothing (burrTxt
   crashed); `-d` there reports no solutions (as `--json` did since stage
   3; burrTxt aborted).
5. **`-o` takes a number or `all`** and refuses anything else (burrTxt's
   `atoi` made 0 of it); an unknown long option and a second file name are
   refused (burrTxt took an unknown option as the file, and the last file).
6. **`--progress` works with `--json`** (burrTxt silenced stderr for
   `--json`; `bt` has nothing on stderr to silence).
7. **stderr messages begin "bt:"** where burrTxt's began "burrTxt:"; the
   assembler 1 line "range optimisation used min …, max …" is not printed.
8. **`--save` replaces burrTxt2:** a named output file instead of
   `<file>ttt`, long options where burrTxt2's single letters clashed with
   burrTxt's, `-o all`, a summary line per problem.

## Found in the original
- **`print()` of a take-apart draws the wrong shapes below the first
  separation** (`print_rec` in src/lib/print.cpp): a node's n-th piece is
  drawn with the shape of the problem's n-th piece (`pieces[pc]`), right
  only at the root. In Cube in Cage the cube's pieces float outside the
  cage once it comes apart. The fix there: `pieces[s->getPieceName(pc)]`
  and the name from the same. Worth fixing in the original.
- **`print()` of a take-apart fails an assertion for an assembly with a
  piece that is not placed** (piece ranges: Demo Mirror Paradox, Demo
  Piece Generator, Solid Six Piece Burrs with `-dp`): it transforms every
  piece of the problem. burrTxt dies with SIGABRT. Worth fixing there.
- burrTxt's `-a` has a fourth answer, the number of solutions of each
  problem, which no argument reaches (`-a` with anything but s0, s1, c is
  refused). Left as it is.
- The original's assemblers fail an assertion on a problem without a
  result shape (burrTxt dies; the GUI checked first).

## Known issues and notes
- **Windows** is still unbuilt: the engine's binary stdio and the CLI are
  untested on MinGW; AVX2/AVX-512 kernels untested on x86-64.
- `just check` has never run: LLVM is not installed on this Mac.
- The text harness compares one-thread runs only (with threads the order
  of the solutions printed is the order the threads hand them over, in
  both programs); `-o all` runs on the fast tier only (some TIC files hold
  further problems that run for many minutes).
- The text baseline needs burrTxt2's solved files (`baseline-files --tier
  all`) for its `-x` and `-a` runs.
- Unchanged from stage 7: requests to the engine are serial; the dt-less
  playback orientation (stage 7's "Found in the original") is for the
  front end's player; `solutionsVersion` starts at 0 for a loaded file;
  most declared error pairs of the API are never observed by the contract
  tests.

## What comes next
The plan (PLAN.md) is complete: the core, the engine and the CLI give the
original's answers on the whole corpus and are as fast or faster. Next:

1. **The web front end**, a plan of its own (docs/webui-spec.md, from
   Girish Sharma's mockup at grssam.github.io/burr-tools; decision 0003):
   Electron + TypeScript + three.js, talking to `bt-engine` over JSON-RPC.
   Its first stage: the TypeScript types generated from `api/`, the engine
   client (lines, frames, events, the `engine.hello` protocol), and the
   shell with the 3D view drawing `shape.mesh` frames; then the entity,
   puzzle and solver workspaces, playback (`playback.*`, with the dt-less
   orientation fix of stage 7's handoff as a deliberate difference), the
   explorers. An audit of stage 7's API against the front end's needs is
   worth doing when it starts (the error pairs, `document.save`'s reply).
2. **The later features** of PLAN.md, each its own stage with the same
   harness discipline (port faithfully, prove against the original):
   sliding puzzles (the sliding solver and its full search), stacking and
   Panex, STL and GLB export (the meshes of `cubemesh.cpp`, `minkmesh.cpp`
   with manifold), the imports (PuzzleSolver3D, Puzzlecad), grid
   conversion, Python bindings. Their files already load and save without
   loss (decision 0006); `bt` and the engine refuse to solve them until
   then.
3. **Housekeeping:** a MinGW build and run of the suite (Windows is a
   supported platform but has never been built), clang-tidy once LLVM is
   installed, and the two print bugs above reported to the original.

# Regression suite

`regress.py` solves every puzzle of [`corpus.txt`](corpus.txt) with
`shapelogic_cli` and compares its answers with the ones recorded in
`baseline/`. Every puzzle is a file of this repository. The suite reads and
runs nothing else, so it needs no other program.

Most answers were recorded from the original BurrTools while the port was
checked against it (decision 0007, amended 2026-11-27). Before that harness
was retired, the CLI was checked one last time against the original's
programs and files, on every tier, solver and thread count, with no
differences. The rest are ShapeLogic's own answers, where the original gave
none or a wrong one. Each summary entry says which in `from` (`original` or
`shapelogic`).

## Commands

```bash
just regress                          # the fast tier's summaries
just regress --tier all               # fast, full and slow
just regress --tier sliding --threads 1
just regress --tier stacking --solver panex
just regress --tier all --solver crowell
just regress --filter PelikanBurr -v  # one puzzle, matches shown too
just regress-text --tier all          # the text runs, byte for byte
just gate regression                  # every run above, as the gate runs them
```

- `summary` (`just regress`): the `--json` answer, meaning the assemblies,
  solutions, level text, level, total moves, slides and rotations.
  The solve time is shown beside the recorded one, never compared.
- `text` (`just regress-text`): the text output of several runs of each
  puzzle on one thread. The runs are the solve with its drawings
  (`-dsp`), the quiet summary, every problem of the file (`-o all`, on
  the fast, sliding and stacking tiers), redoing the take-apart of the
  saved solutions (`-x`) and the comment (`-a c`). Only hashes are
  committed. Each run's text is kept under `build/regression/text-recorded`
  when it is recorded, so that a later run on the same machine can show
  the first line that differs.

A run's last line reads "N of M recorded answers compared, K differ". A
recorded entry that is not compared fails the run, and so does a corpus
pattern that matches no file.

## When an answer changes on purpose

A change that fixes a wrong answer records the new one for the puzzles it
touches, and its commit says why:

```bash
python3 tests/regression/regress.py summary --tier fast --filter HexSticks --update
python3 tests/regression/regress.py text --tier fast --filter HexSticks --update
```

`--update` prints each field that changed, old and new, and marks the
entry `"from": "shapelogic"`. Sliding puzzles with several starts (flag
`O`) are recorded on one thread (`--threads 1`).

## Tiers and flags

See the header of [`corpus.txt`](corpus.txt).

- `fast`: BurrTools' examples and the original's rotation fixture.
- `full`: ten take-apart cases with rotations, under
  `tests/data/puzzles/regression/`, named by what each one tests.
- `slow`: the two benchmarks there.
- `sliding`: the sliding fixtures and the puzzles the original's sliding
  code made.
- `stacking`: the stacking fixtures, a Panex file, and the puzzles the
  original's stacking code made.

Each sliding or stacking run gets a search folder of its own
(`SHAPELOGIC_SLIDE_DIR`, `SHAPELOGIC_PANEX_DIR`), so the user's cache is
never touched.

## Comparing with BurrTools

Only on request: `just burrtools-compare` times upstream BurrTools'
`burrTxt` and ShapeLogic's CLI on every puzzle of the fast, full and slow
tiers that both can solve, and compares their assemblies and solutions
([`tests/bench/README.md`](../bench/README.md)). No suite runs it.

The unit tests' recordings under `tests/data/*_by_original/` and similar
were made from the original by the retired harness (`tests/oracle/`, in
git history up to 632af17). They are fixed data now, read like any other
fixture.

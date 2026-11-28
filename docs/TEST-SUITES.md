# Test suites and how long they take

Every suite the repo has, the commands that run several of them, and the
time each took when it ran alone. Say the expected time before starting
one, and when a run is drastically different (over 1.5 times or under half),
find out why before trusting it: load from another session, a cold build, a
real slowdown or speedup. Update a time here only when the cause is
understood and the run's result was good.

**Measured:** 2026-11-27, on the branch `feature/sl-names` (after the rename
to sl), on a MacBook Air M4 (4 performance and 6 efficiency cores, 32 GB),
nothing else running. Builds were warm unless a row says otherwise. Each
suite ran alone, one after another. Times are wall clock, in seconds, with
minutes beside the long ones.

## The suites

| Suite | Command | Time | What it checks |
|---|---|---|---|
| Names guard | `just check-names` | 0 s | no tracked file uses a name from before the rename to sl |
| Unit, contract, CLI, Python, quick regression | `just test` | 79 s | ctest without the `[slow]` cases: the unit shards, the contract suite, the CLI text test, the Python module, `api-docs`, `regression-fast` |
| Everything in ctest | `just test-all` | 239 s (4.0 min) | the same with every `[slow]` case, one ctest test each |
| Everything in ctest, release build | `just test-all release` | 67 s | the same on the release preset (the gate runs it twice) |
| Address and undefined-behavior sanitizers | `just test-asan` | 805 s (13.4 min) | the whole ctest set on the asan build; the longest suite |
| ThreadSanitizer | `just test-tsan` | 355 s (5.9 min) | the `[parallel]` cases, then the contract suite against the TSan engine |
| Python module | `just python-test` | 4 s | `python/tests` on the dev module |
| Python wheel | `just python-wheel` | 40 s | the wheel built, installed in a fresh venv, tested there |
| Regression, all tiers | `just regress --tier all` | 5 s | the solver's answers against the recorded ones, all threads |
| Regression, sliding tier | `just regress --tier sliding` | 54 s | the sliding-game solver, all threads |
| Regression text | `just regress-text --tier all` | 18 s | the CLI's text output against the recorded runs, byte for byte |
| Check | `just check` | 173 s (2.9 min) | the names guard, then clang-tidy over 226 files |
| Web static checks | `just app-check` | 16 s | tsc, ESLint, Prettier |
| Web unit and engine tests | `just app-test` | 29 s | Vitest: the unit tests and the tests against the release engine |
| End to end | `just app-e2e` | 150 s (2.5 min) | every Playwright spec, 161 tests, side by side |
| End to end, timed | `just app-e2e-timed` | 6 s | the one perf test, alone |
| Packaged app build | `just app-dist --dir` | 11 s | builds the unpacked app (needed by the smoke test) |
| Packaged app smoke | `just app-smoke` | 3 s | the packaged app starts and works |
| Site | `just site-build` | 8 s | the site and guide build, link check, Help URLs, the demo-is-current test |
| BurrTools comparison | `just burrtools-compare` | 7 s | speed and counts against upstream on every puzzle both can solve, best of 3, 1 and 10 threads |
| Speed benchmark | `just bench` | 18 s | the default set against upstream, best of 5, 1 and 10 threads |

`just burrtools-compare` and `just bench` run only on request. Before each
`just burrtools-compare`, delete the build folders in `../burr-tools` and
build again from nothing (`meson setup build-rel --buildtype=release &&
meson compile -C build-rel`, inside `../burr-tools`). That clean build took
52 s on 2026-11-27 and is not in the 7 s or the 18 s. `just bench` ran on
that same fresh build.

More benchmark runs, all on that same fresh upstream build, run on request:

| Command | Time | What it checks |
|---|---|---|
| `just bench --set identical` | 308 s (5.1 min) | boxes of many pieces of one shape, ShapeLogic against upstream, best of 5, 1 and 10 threads |
| `just bench --set identical-long` | 1000 s (16.7 min) | the same on two larger boxes; the README's "about a minute a run" matches |
| `just bench --set glar` | 190 s (3.2 min) | Burr-Glar, 3.1 million assemblies: about 16 s a run on one thread, 3 s on ten, so the README's "minutes" a run is out of date |
| `just bench --session --old-bin <earlier build>` | 20 s | the app's way through a solve (`SolveSession`) of two ShapeLogic builds; run against the cleanup branch's release CLI from before the rename |

Variants inside the regression suite that `just gate` runs, with the times
they took inside the gate: `--threads 1` 21 s; `--solver crowell` 3 s and
4 s on one thread; `--tier sliding --threads 1` 249 s; `--tier stacking`
2 s; `--tier stacking --solver panex` 1 s; `regress-text --solver crowell`
4 s; `regress-text --tier sliding` 255 s; `regress-text --tier stacking`
2 s; `regress-text --tier stacking --solver panex` 1 s.

## The commands that run several

| Command | Time | Runs |
|---|---|---|
| `just test` | 79 s | the ctest set without `[slow]` (see the table above) |
| `just test-all [preset]` | 239 s dev, 67 s release | the whole ctest set |
| `just test-asan` | 805 s | `just test-all asan` (an alias) |
| `just app-e2e` | 150 s + 6 s | every spec, then the timed test (`just app-e2e-timed`) alone; with spec names only those, and no timed test |
| `just check` | 173 s | `just check-names`, then clang-tidy |
| `just gate` (default groups) | 2452 s (40.9 min) | `build`, `unit`, `sanitizers`, `python`, `regression`, `app`, `check`: 26 suites |
| `just gate all` | 2627 s (43.8 min) | the default groups plus `e2e`, `site`, `dist` and `burrtools` (175 s more) |
| `just gate <groups>` | the sum of its groups | only the groups named |

The gate's groups and what each one takes (in the gate, seconds):

| Group | Time | Suites |
|---|---|---|
| `build` | 2 s | `just build dev`, `just build release` (warm) |
| `unit` | 387 s | `test-all` 251, `test-all release` 67 and again 69 |
| `sanitizers` | 1165 s | `test-asan` 810, `test-tsan` 355 |
| `python` | 48 s | `python-test` 5, on release 2, `python-wheel` 41 |
| `regression` | 619 s | 13 runs of `just regress` and `just regress-text`; the sliding ones dominate (249 and 255 s) |
| `app` | 47 s | `app-check` 17, `app-test` 30 |
| `check` | 184 s | `just check` |
| `e2e` | 146 s | `just app-e2e` (the gate does not run the timed test) |
| `site` | 7 s | `just site-build` |
| `dist` | 15 s | `just app-dist --dir` 11, `just app-smoke` 4 |
| `burrtools` | 7 s | `just burrtools-compare` (on the build it finds; the gate does not rebuild upstream) |

A full gate on a cold tree is longer: the first builds of the dev,
release, asan and tsan presets add the time of four builds from scratch (a
warm gate has them at 1 to 2 s). When the files change a lot, expect the
sanitizer suites to grow first.

## Older figures

The notes before this file said about 75 minutes for `just gate` and about
7 minutes for `just app-e2e`. The gate's table has been 41 to 46 minutes
since the oracle was retired, and the e2e suite about 2.5 minutes since its
tests run side by side.

---
title: Code Testing Suites
description: "Every kind of test ShapeLogic has, by name: what each one checks, the command that runs it, which ones `just test` and `just test-all` include, and what to run after a change."
---

ShapeLogic has several groups of tests: the core's own tests, sanitizer
builds, the Python package, code checks, the app, the website, the
regression suite, and the comparison with BurrTools. Each test below has
a name you can refer to it by and the command that runs it. All the commands
are `just` recipes from the root of the repository.

## Everything at once: `just gate`

`just gate` runs the suites one after another and ends with a table of what
passed. A suite that fails does not stop the run. Each suite's output is in
`build/gate/<nn>-<name>.log`, the table in `build/gate/summary.txt` (or
under `GATE_OUT` when it is set, so two gates at once keep their own), and
the command fails if any suite did. It takes about an hour and a quarter; run it
on a quiet machine.

| Command | What runs |
|---|---|
| `just gate` | The groups `build`, `unit`, `sanitizers`, `python`, `regression`, `app` and `check`. |
| `just gate unit regression` | Only the groups named. |
| `just gate all` | Those and `e2e` (every Playwright spec), `site`, `dist` and `burrtools` (**burrtools-compare**). |

`tests/gate.sh` lists what each group runs.

None of the suites writes in your cache folder. Each engine a test starts
gets search folders of its own (`SHAPELOGIC_SLIDE_DIR`,
`SHAPELOGIC_PANEX_DIR`), because two solves of one problem cannot share a
folder.

## The core tests: `just test` and `just test-all`

These are CTest tests, defined in `tests/CMakeLists.txt`.
`just test-all` runs all of them; `just test` runs all but **slow**, for a
quick check while you work.

| Name | What it checks | `just test` | `just test-all` |
|---|---|---|---|
| **unit** | The Catch2 tests in `sl_tests`, except the `[slow]` ones: the C++ core (grids, the puzzle model, the solvers, sliding and stacking puzzles, the document and its commands) and the engine run in process. They run in shards, `unit-0` to `unit-23`, side by side. | yes | yes |
| **slow** | The `[slow]` Catch2 cases, each taking more than about a second: bigger puzzles, long searches, saved searches. Each is a test of its own, `slow: <case>`, so the longest start first. | no | yes |
| **api-schema** | The engine API's schema files, `api/*.json`, are valid and use only what the validator understands. | yes | yes |
| **api-docs** | `docs/API.md` matches `api/*.json`. It fails until `python3 api/gen_api_md.py` is run after a schema change. | yes | yes |
| **contract** | Runs `shapelogic_engine` as a separate process and checks every reply and event against `api/`. It also fails if any method is never called. | yes | yes |
| **cli** | The command line tool, `shapelogic_cli`, on the test puzzles: its options, refusals, output and exit codes. | yes | yes |
| **python** | The Python module's tests in `python/tests`. Runs only when the module was built, which needs Python's headers. | yes | yes |

Both commands build first and take a build preset: `just test-all release`
runs the same tests on the release build. `just test-all` on the dev preset
takes about four minutes on an M-series Mac, most of it **slow**.

The test binary takes Catch2 tags, so you can run part of **unit** or
**slow** directly: `build/dev/tests/sl_tests "[solve]"` runs one
module's cases, `"[parallel]"` the threaded ones, `"~[slow]"` everything
but the slow cases, and a test's name runs that test alone.

## Sanitizers

| Name | Command | What it checks |
|---|---|---|
| **asan** | `just test-asan` | All the core tests in a build with the address and undefined-behavior sanitizers: memory errors and undefined behavior. |
| **tsan** | `just test-tsan` | The `[parallel]` unit cases (each a test of its own, side by side) and the **contract** test in a ThreadSanitizer build: data races between threads. |

Both build their own preset (`build/asan`, `build/tsan`) and are much
slower than `just test-all`.

## The Python package

| Name | Command | What it checks |
|---|---|---|
| **python-test** | `just python-test [preset]` | The **python** tests alone, on a preset's module. |
| **python-wheel** | `just python-wheel` | Builds the installable package (a wheel, in `build/wheel`), installs it into a fresh virtual environment and runs the tests there. |

## Code checks

| Name | Command | What it checks |
|---|---|---|
| **clang-tidy** | `just check` | Static analysis of the project's C++ sources: every finding is an error. `just check sliding_search` checks only the sources whose path matches. Needs LLVM (`brew install llvm`). |
| **old names** | `just check-names` (and first in `just check`) | No tracked file has a name of the kinds the rename to `sl` and `SHAPELOGIC_` took out (the macros', the namespace's, the targets', the environment variables', the IPC channels' and the temp folders' old prefixes), but the ones that stay: file signatures such as `BTSOLVE2`, the original's own names, a CSS class. The dated records and generated files are not looked at. The patterns and the allow list are in `tests/check-names.sh`. |

The checks are listed in `.clang-tidy`, with the reason for each one that is
left out; `tests/unit/.clang-tidy` leaves out a few more for the tests.
Where the analysis cannot see why code is right, the line above it says so:
`// NOLINTNEXTLINE(check): reason`. Homebrew's LLVM is not on the `PATH`
by default; the recipe finds it in `/opt/homebrew/opt/llvm/bin` anyway.

The sources' layout is not checked: `.clang-format` describes the style
(Google's, 100 columns) for editors and for the fixes clang-tidy applies,
and no recipe reformats the tree.

## The app

| Name | Command | What it checks |
|---|---|---|
| **app-check** | `just app-check` | TypeScript type checks (the main process, the renderer and the e2e tests), ESLint and the Prettier format check. |
| **app-test** | `just app-test` | The Vitest tests in `web/tests/`: unit tests that need no engine (including the check that `api.generated.ts` matches `api/*.json`), and engine tests against the release engine. Build it first with `just build release`; without it the engine tests are skipped. |
| **app-e2e** | `just app-e2e [args]` | Builds the app and drives it with Playwright, comparing reference screenshots. About seven minutes. Arguments go to Playwright: `-g picking` runs the tests matching a name, `--update-snapshots` records the screenshots again. |
| **app-smoke** | `just app-smoke` | Starts the packaged app and checks it runs. Run `just app-dist --dir` first. |

## The website

| Name | Command | What it checks |
|---|---|---|
| **site-build** | `just site-build` | Builds the site and checks every internal link. Its tests also check that the demo was built from the current app and that every page the app's Help menu opens exists. |
| **demo-check** | `just site-demo --check` | Rebuilds the browser demo (needs Emscripten) and drives it in Google Chrome. |

## The regression suite

The solver's answers are checked against answers recorded earlier. Most
were recorded from the original BurrTools while ShapeLogic was ported from
it, and the rest are ShapeLogic's own, marked as such. The suite,
`tests/regression/`, solves a corpus of puzzle files that are all in this
repository, and it never runs or reads another program.
`tests/regression/README.md` describes the corpus, its tiers and its
flags.

| Name | Command | What it checks |
|---|---|---|
| **regress** | `just regress [--tier …]` | Solve answers (assembly and solution counts, levels, moves, rotations) against the ones recorded in `tests/regression/baseline/`. Tiers: `fast` (the default), `full`, `slow`, `sliding`, `stacking` and `all` (fast, full and slow). Other options: `--threads 1`, `--solver crowell`, `--solver panex` (stacking), `--filter <name>`, `-v`. |
| **regress-text** | `just regress-text [--tier …]` | The command line tool's text output of several runs of each puzzle, byte for byte, against the recorded runs. |

Every run ends with a line such as `31 of 31 recorded answers compared, 0
differ`. A recorded answer that is not compared, or a corpus line that
finds no file, fails the run.

When a change fixes a wrong answer, record the new one for the puzzles it
touches with `--update` (for example `python3 tests/regression/regress.py
summary --tier fast --filter HexSticks --update`), and say why in the
commit.

## Comparing with BurrTools

This runs only when you ask for it; no suite or gate group runs it by
default.

| Name | Command | What it checks |
|---|---|---|
| **burrtools-compare** | `just burrtools-compare` | Upstream BurrTools' `burrTxt` and ShapeLogic's command line tool on every regression puzzle both can solve: the time of each (wall clock, best of 3, on one thread and on all) and whether their assembly and solution counts agree. |
| **bench** | `just bench [--set …]` | Speed benchmarks against upstream BurrTools: `--set glar` a long set (minutes), `--set identical` boxes of many pieces of one shape, `--plain` without the vector search. `python3 tests/bench/bench.py --session --old-bin <an earlier shapelogic_cli>` times two ShapeLogic builds instead. |

Both need upstream BurrTools checked out at `../burr-tools` (or wherever
`BURRTOOLS_DIR` points) and built there with meson into `build-rel/`.
Upstream has no take-apart with rotations and no sliding or stacking
puzzles, so those are left out. Run them on a quiet machine, never beside a
sanitizer run. `tests/bench/README.md` has the details.

## What to run after a change

| You changed | Then run |
|---|---|
| a method or event in `api/*.json` | `python3 api/apischema.py`, a contract call in `tests/contract/test_engine.py`, `python3 api/gen_api_md.py` (**api-docs** fails on a stale `docs/API.md`), `just app-gen` (**app-test** fails on stale types) |
| what the engine sends | `npm run record` in `web/`, which records the fake engine's sessions the unit tests replay |
| the C++ of a solver | `just test-all`, `just test-asan`, `just test-tsan`, `just check` on the files you touched, and the **regress** tiers it could touch (**regress-text** for what the command line tool prints) |
| a lot, or you are about to release | `just gate` |
| the app's UI | `just app-check`, `just app-test`, `just app-e2e` (look at changed screenshots first), `just docs-screenshots` if the guide shows it, `just site-demo` (**site-build** fails on a stale demo) |
| a page of this guide | `just site-build`; do not rename `/docs/` or `/docs/getting-started/`, which the app's Help menu opens |
| a stage's worth of work | the "Finishing a piece of work" list in `AGENTS.md`: then `docs/ARCHITECTURE.md`'s map lines and rules, and the plan |

The rules for writing tests (tags, fixtures, comparing assemblies) are in
[Conventions and Further Reading](/docs/code-base/conventions/). The
commands themselves, with the build presets and environment variables, are
in [Tool Commands and Environment Variables](/docs/code-base/commands/).

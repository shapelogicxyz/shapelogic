---
title: Code Testing Suites
description: "Every kind of test ShapeLogic has, by name: what each one checks, the command that runs it, which ones `just test` and `just test-all` include, and what to run after a change."
---

ShapeLogic has several groups of tests: the core's own tests, sanitizer
builds, the Python package, code checks, the app, the website, the
comparison with the original BurrTools, and benchmarks. Each test below has
a name you can refer to it by and the command that runs it. All the commands
are `just` recipes from the root of the repository, except the benchmarks.

## The core tests: `just test` and `just test-all`

These seven are CTest tests, defined in `tests/CMakeLists.txt`.
`just test-all` runs all of them; `just test` runs all but **unit-slow**,
for a quick check while you work.

| Name | What it checks | `just test` | `just test-all` |
|---|---|---|---|
| **unit** | The Catch2 tests in `bt_tests`, except the `[slow]` ones: the C++ core (grids, the puzzle model, the solvers, sliding and stacking puzzles, the document and its commands) and the engine run in process. | yes | yes |
| **unit-slow** | The `[slow]` Catch2 cases, each taking more than about a second: bigger puzzles, long searches, saved searches. | no | yes |
| **api-schema** | The engine API's schema files, `api/*.json`, are valid and use only what the validator understands. | yes | yes |
| **api-docs** | `docs/API.md` matches `api/*.json`. It fails until `python3 api/gen_api_md.py` is run after a schema change. | yes | yes |
| **contract** | Runs `shapelogic_engine` as a separate process and checks every reply and event against `api/`. It also fails if any method is never called. | yes | yes |
| **cli** | The command line tool, `shapelogic_cli`, on the test puzzles: its options, refusals, output and exit codes. | yes | yes |
| **python** | The Python module's tests in `python/tests`. Runs only when the module was built, which needs Python's headers. | yes | yes |

Both commands build first and take a build preset: `just test-all release`
runs the same tests on the release build. `just test-all` on the dev preset
takes about ten minutes on an M-series Mac, most of it **unit-slow**.

The test binary takes Catch2 tags, so you can run part of **unit** or
**unit-slow** directly: `build/dev/tests/bt_tests "[solve]"` runs one
module's cases, `"[parallel]"` the threaded ones, `"~[slow]"` everything
but the slow cases, and a test's name runs that test alone.

## Sanitizers

| Name | Command | What it checks |
|---|---|---|
| **asan** | `just test-asan` | All seven core tests in a build with the address and undefined-behaviour sanitizers: memory errors and undefined behaviour. |
| **tsan** | `just test-tsan` | The `[parallel]` unit cases and the **contract** test in a ThreadSanitizer build: data races between threads. |

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
| **clang-tidy** | `just check` | Static analysis of the project's C++ sources. Needs LLVM (`brew install llvm`). |

`just format` reformats the C++ sources with clang-format. It changes files
rather than checking them.

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

## Comparison with the original BurrTools

The original BurrTools is the reference: ported code must give the same
answers. The comparison harness, `tests/oracle/`, runs the new code on a
corpus of puzzles and compares the results with the original's.
`tests/oracle/README.md` describes the corpus and its tiers.

The harness needs a checkout of the original next to the repository, at
`../burr-tools`, or wherever `BT_OLD_REPO` points. Build it there with
`just build`, which puts `burrTxt` and `burrTxt2` in its `build/`. The
harness reads the corpus's puzzle files from that checkout.

| Name | Command | What it checks |
|---|---|---|
| **oracle** | `just oracle [--tier …]` | Solve answers (assembly and solution counts, levels, moves) against the original's answers recorded in `tests/oracle/baseline/`. Tiers: `fast` (the default), `full`, `slow`, `sliding`, `stacking` and `all`. Other options: `--threads 1`, `--solver crowell`, `--filter <name>`, `-v`. |
| **oracle-resume** | `just oracle-resume` | Pauses searches in one program and carries them on in the other: saved positions work both ways. |
| **oracle-roundtrip** | `just oracle-roundtrip` | Loads and saves every corpus file in both programs and compares the files. |
| **oracle-files** | `just oracle-files` | Solves as the original's `burrTxt2` does and compares the solved files. Run `python3 tests/oracle/oracle.py baseline-files --tier all` once first. |
| **oracle-text** | `just oracle-text` | The command line tool's text output against the original's, byte for byte. Run `python3 tests/oracle/oracle.py baseline-text --tier all` once first. |
| **oracle-imports** | `just oracle-imports` | Imports, the Puzzlecad export and grid conversion against the original's. |
| **oracle-python** | `just oracle-python` | The Python module's results against the original module's. |

`just oracle-baseline` records the original's answers again. It is rarely
needed.

Every **oracle** run ends with a count such as `24/24 match the original`.
Read it: a puzzle the harness cannot find in the checkout is skipped with a
`corpus pattern matched nothing` warning, so without the checkout the
`fast`, `full` and `slow` tiers compare nothing and still report success
(`0/0`). The commands that run the original program stop with an error
instead.

## Benchmarks

| Name | Command | What it checks |
|---|---|---|
| **bench** | `python3 tests/bench/bench.py` | The new solvers' speed against the original's: within 5% of it, or faster. `--plain` runs without the vector search, `--set glar` a long set (minutes). |

There is no `just` recipe. Build both programs first: `just build release`
here and `just build-release` in `../burr-tools` (the benchmark uses its
`build-rel/`). Run it on a quiet machine, never beside a sanitizer run.

## What to run after a change

| You changed | Then run |
|---|---|
| a method or event in `api/*.json` | `python3 api/apischema.py`, a contract call in `tests/contract/test_engine.py`, `python3 api/gen_api_md.py` (**api-docs** fails on a stale `docs/API.md`), `just app-gen` (**app-test** fails on stale types) |
| what the engine sends | `npm run record` in `web/`, which records the fake engine's sessions the unit tests replay |
| the C++ of a solver | `just test-all`, `just test-asan`, `just test-tsan`, and the **oracle** tiers it could touch |
| the app's UI | `just app-check`, `just app-test`, `just app-e2e` (look at changed screenshots first), `just docs-screenshots` if the guide shows it, `just site-demo` (**site-build** fails on a stale demo) |
| a page of this guide | `just site-build`; do not rename `/docs/` or `/docs/getting-started/`, which the app's Help menu opens |
| a stage's worth of work | the "Finishing a stage" list in `AGENTS.md`, then `docs/ARCHITECTURE.md`, a hand-off note and the plan |

The rules for writing tests (tags, fixtures, comparing assemblies) are in
[Conventions and Further Reading](/docs/code-base/conventions/). The
commands themselves, with the build presets and environment variables, are
in [Tool Commands and Environment Variables](/docs/code-base/commands/).

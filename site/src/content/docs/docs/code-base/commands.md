---
title: Build, Test and Tool Commands
description: "The just recipes, build presets, npm scripts and environment switches used to build, test and package ShapeLogic."
---

Everything goes through `just`. The `justfile` at the root of the repository
is the documented interface; do not invent ad-hoc build steps when a recipe
exists. Typing `just` alone lists the recipes.

## What you need

| Tool | For |
|---|---|
| CMake 3.28 or newer, Ninja, a C++23 compiler | the core, the engine and the command line tool |
| `just` | running the recipes |
| Python 3 | the contract tests, the CLI tests, the harness, the API generator |
| Node 22 | the app; Node 22.22 or newer for the website |
| LLVM (optional) | `just check` (clang-tidy) and `just format` (clang-format) |
| Emscripten (optional) | `just site-demo`, the engine as WebAssembly |
| The original BurrTools checkout (optional) | the comparison harness; at `../burrtools_rotations` or `BT_OLD_REPO` |

## A first build

```bash
just build              # the dev preset, into build/dev
just test               # unit and contract tests, without the slow cases
just build release      # the optimised engine the app and its tests use
just app-install        # the app's packages (npm ci)
just app-dev            # run the app against the dev engine
```

## The core

```bash
just build [preset]     # dev (default), release, asan, tsan; macOS also release-x64, release-arm64
just test [preset]      # unit + contract tests without [slow]; the quick loop
just test-all [preset]  # everything; run before calling work done
just test-asan          # Address + UndefinedBehavior sanitizers
just test-tsan          # ThreadSanitizer on the [parallel] cases, then the contract suite
just check              # clang-tidy over the project's own sources
just format             # clang-format
just clean              # remove every build directory
```

The presets are in `CMakePresets.json`: `base` (shared settings), `dev`,
`release`, `release-x64`, `release-arm64`, `asan`, `tsan` and `wasm`.
Warnings are errors in `dev` and `release`. Each preset builds into
`build/<preset>/`: the test binary at `build/<preset>/tests/bt_tests`, the
engine at `build/<preset>/engine/shapelogic_engine`, the CLI at
`build/<preset>/cli/shapelogic_cli`.

The test binary is Catch2, so it takes tags: `build/dev/tests/bt_tests
"[solve]"` runs one module's cases, `"[parallel]"` the threaded ones,
`"~[slow]"` everything but the slow cases.

## The comparison harness

The original BurrTools is the specification. The harness runs the original
`burrTxt` and the new `shapelogic_cli` on the same puzzles and compares the
answers; `tests/oracle/README.md` explains the corpus and its tiers.

```bash
just oracle [--tier …]  # assembly and solution counts against burrTxt
just oracle-resume      # pause searches in one program, continue in the other
just oracle-roundtrip   # load and save every corpus file in both programs, compare
just oracle-files       # solve as burrTxt2 does, compare the solved files
just oracle-text        # the CLI's text output against burrTxt's, byte for byte
just oracle-imports     # imports, Puzzlecad export and grid conversion against the original's
just oracle-python      # the Python module's transcripts against the original module's
just oracle-baseline    # re-record the original's answers (rarely needed)
```

## The Python module

```bash
just python-test [preset]   # python/tests on a preset's module
just python-wheel           # the wheel in build/wheel, installed into a fresh venv and tested
```

The module is built when CMake finds Python's headers (`BT_PYTHON=AUTO`);
set `-DBT_PYTHON=ON` to require it or `OFF` to skip it.

## The app

```bash
just app-install        # npm ci in web/
just app-gen            # web/src/engine/api.generated.ts from api/*.json
just app-check          # type checks (node, renderer and e2e projects), ESLint, Prettier
just app-test           # unit tests; the engine tests too when build/release exists
just app-dev            # the app with live reload (electron-vite dev)
just app-e2e [args]     # build, then the Playwright end-to-end tests; args go to Playwright
just app-dist [--dir]   # the packaged app for this platform, in web/dist; --dir unpacked only
just app-smoke          # the packaged app's smoke test (after just app-dist --dir)
```

Inside `web/`, the npm scripts behind these are `gen`, `check`, `test`,
`dev`, `e2e`, `dist`, and also `format` (Prettier writes), `record` (the
fake engine's sessions recorded again after a change to what the engine
sends), `build` (electron-vite into `out/`) and `demo` (the website's demo).

`just app-e2e -g picking` runs the tests matching a name;
`--update-snapshots` records the reference screenshots again. The app
recipes clear `ELECTRON_RUN_AS_NODE`, which some editors' terminals set and
which turns Electron into plain Node.

## The website

```bash
just site-dev           # http://localhost:4321 with live reload
just site-build         # build, check the links, check the app's Help URLs
just docs-screenshots   # retake the guide's screenshots from the built app (after just build release)
just site-demo [--check] # rebuild the browser demo (needs Emscripten); --check drives it in Chrome
```

Inside `site/`, `npm run dev`, `npm run build` and `npm test` are the
scripts behind these.

## Releases

```bash
just release             # the next version and the release steps
just release v1.4        # a new release: check, push the tag, make the draft release
just release v1.3.1      # a patch: updates the published v1.3 release when published
just release-fetch v1.4  # a copy of a release's installers in release/v1.4
```

[Creating a New Release](/docs/code-base/releasing/) explains the whole
flow.

## Environment switches

| Variable | Effect |
|---|---|
| `BT_OLD_REPO` | where the original BurrTools checkout is, for the harness |
| `BT_ENGINE` | the engine binary the app in development and the engine tests use; otherwise `build/dev/engine/shapelogic_engine`, else the release one |
| `BT_CALL_LOG` | a file every call the renderer makes is logged to, with how it ended; the e2e tests read it |
| `BT_AUTOSAVE_SECONDS` | shortens the Solver's autosave from 20 minutes, for the e2e tests |
| `BT_USER_DATA` | moves the app's data directory (settings, recent files, the rescue copy); the only switch a packaged app takes |
| `BT_PACKAGED_APP` | another executable for `just app-smoke` to test |
| `SHAPELOGIC_VERSION` | the version to build (`v1.4` or `1.4.0`) instead of the newest git tag's; the Release workflow sets it from the tag |
| `CSC_NAME`, `CSC_LINK`, `CSC_KEY_PASSWORD`, `APPLE_API_KEY`, `APPLE_API_KEY_ID`, `APPLE_API_ISSUER` | code signing and notarisation for `just app-dist`; see `web/README.md` |

A packaged app ignores every development switch but `BT_USER_DATA`
(`web/src/main/security.ts`). Nothing in `core/` reads the environment: the
original's environment switches became fields of the options structs.

## When you change something

| You changed | Then run |
|---|---|
| a method or event in `api/*.json` | `python3 api/apischema.py`, a contract call in `tests/contract/test_engine.py`, `python3 api/gen_api_md.py` (the `api-docs` test fails on a stale `docs/API.md`), `just app-gen` (a web test fails on stale types) |
| what the engine sends | `npm run record` in `web/` (the fake engine's recordings) |
| the C++ of a solver | `just test-all`, `just test-asan`, `just test-tsan`, the oracle tiers it could touch |
| the app's UI | `just app-check`, `just app-test`, `just app-e2e` (look at changed screenshots first), `just docs-screenshots` if the guide shows it, `just site-demo` (the demo's staleness test fails otherwise) |
| a page of this guide | `just site-build`; do not rename `/docs/` or `/docs/getting-started/`, which the app's Help menu opens |
| a stage's worth of work | the "Finishing a stage" list in `AGENTS.md`, then `docs/ARCHITECTURE.md`, a hand-off note and the plan |

# AGENTS.md: working on ShapeLogic

ShapeLogic is a rebuild of the ShapeLogic puzzle solver's core (C++23)
with an engine process for a future web front end. It was ported from
BurrTools and checked against it until 2026-11-27 (decision 0007,
amended). The answers recorded then are the reference now: the regression
suite (`tests/regression/`) compares the solver with them, and no suite
reads or runs BurrTools. Upstream BurrTools (`../burr-tools`, or
`BURRTOOLS_DIR`) is compared with only on request (`just
burrtools-compare`).

Read before starting any task:
1. This file.
2. The active plan for the work: the `docs/*-PLAN.md` the user names, or
   the one whose Status has open stages.
3. In [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), the section of each
   module the work changes: its map says which file holds what, and its
   rules what must stay true. Not the whole file.
4. The decision records in [docs/decisions/](docs/decisions/) that those
   sections name.

[docs/PLAN.md](docs/PLAN.md) is the finished porting plan and
[docs/handoff/](docs/handoff/) the notes of its stages: history, read only
when a question needs it. For the front end, docs/FRONTEND-PLAN.md,
docs/webui-spec.md and docs/API.md are references to search (by method,
section or term), not documents to read whole.

## Commands

Use `just`. Do not invent ad-hoc build steps when a recipe exists.

```bash
just build [preset]     # dev (default), release, asan, tsan (macOS: release-x64, release-arm64); output in build/<preset>
just test               # unit + contract tests without [slow], side by side; the quick loop
just test-all           # everything; run before calling a stage done
just gate [groups]      # every suite one after another, with a table of what passed (tests/gate.sh; ~75 min)
just test-asan          # Address + UB sanitizers
just test-tsan          # ThreadSanitizer on [parallel] cases
just regress [--tier …] # the solver's answers against the recorded ones (tests/regression)
just regress-text       # the CLI's text output against the recorded runs, byte for byte
just burrtools-compare  # on request only: speed and counts against upstream BurrTools
just bench [--set …]    # speed benchmarks (tests/bench/README.md)
just python-test [preset] # the Python module's tests (python/tests; the module builds when Python's headers are found)
just python-wheel       # its wheel in build/wheel, installed into a fresh venv and tested there
just check [files]      # clang-tidy, every finding an error (needs LLVM installed); with names, only those sources
just app-install        # the web front end's packages (npm ci; Node 22)
just app-gen            # web/src/engine/api.generated.ts from api/*.json
just app-check          # the front end's type checks, ESLint, Prettier
just app-test           # its unit tests, and engine tests against build/release
just app-dev            # run the app (electron-vite dev)
just app-release        # the same with the release engine (built first): the fast one for long solves
just app-e2e            # build the app, run its Playwright e2e tests side by side, then the timed one alone
just app-e2e-timed      # only the timed e2e test (perf.e2e.ts), alone
just app-dist [--dir]   # the packaged app for this platform (electron-builder), in web/dist
just app-smoke          # the packaged app's smoke test (after just app-dist --dir)
just site-build         # the website and user guide (site/): build, link check, the Help URLs
just site-dev           # the site with live reload
just docs-screenshots   # retake the guide's screenshots from the built app
just site-demo          # rebuild the site's demo (site/public/demo/): the engine as WebAssembly under the renderer (needs Emscripten)
just release [vX.Y]     # start a release (no version: the steps); notes in release/notes/vX.Y.md, git-ignored; vX.Y.Z: a patch that updates the vX.Y release
just release-fetch vX.Y # a copy of a release's files in release/vX.Y
```

## How to port code from the original

1. **Faithful first, restructured second.** Port the algorithm with its
   behavior intact, prove it with tests and the regression suite, then
   reshape it. Never "improve" an algorithm while porting it.
2. **Cite the source.** At the top of each ported file, name the original
   file(s) it comes from, e.g. `// Ported from src/lib/voxel_0.cpp`.
3. **Port the tests too.** The original's `test/*.cpp` cases for the area
   come across (adapted to the new API), then new ones are added.
4. **Gate on the regression suite** (`tests/regression/`), and on unit
   tests against the recordings under `tests/data/`.
5. **File compatibility is one way** (decision 0006, amended). ShapeLogic
   must open every file the original wrote (convert on load); the original
   need not read ShapeLogic's saves, for any feature. Never add shims, dual
   encodings or limits so that the old program can read new files.
   Saved searches (sliding level files, `state.bin`, Panex folders) and
   their folder names stay readable by the new code.

## Code rules

- C++23, `namespace sl`. Files are `snake_case`, types are `PascalCase`,
  functions and variables `camelCase`, constants `kPascalCase`.
- Ownership: value types and `std::unique_ptr`. No owning raw pointers, no
  `new`/`delete`, no hand-written copy assignment that lists members.
- Errors: recoverable problems return `sl::Result<T>` (base/error.h); broken
  invariants use `SL_ASSERT` (base/assert.h), which is on in every build.
  Never put side effects inside `SL_ASSERT`.
- Cancellation: long work takes an `sl::StopToken` and polls it. No search
  has a stop flag of its own, and nothing resets a token (decision 0004). A
  search that must end its own run makes a `StopSource` from the token it
  was given, one per run (decision 0009).
- Solve state: one writer at a time for a `SolveStateCell`: the run's
  worker while it runs, the owner under the session's control lock before
  the run's thread starts and after it is joined; other threads read it and
  stop runs through a `StopSource`.
- No global state and no environment-variable switches inside `core/`.
  Options are passed in explicitly (A/B switches for benchmarking become
  fields of the options struct, set by the CLI).
- `core/` never prints. Diagnostics go through return values or callbacks.
- Every public declaration has a short doc comment saying what it is for.
  Comments explain why, not what.
- Concurrency: shared state is `std::atomic` or guarded by a mutex, always.
  Hand work to the solve's `sl::ThreadPool` (passed in, never global) rather
  than starting raw `std::thread`s; thread counts are clamped to
  `kMaxThreads`.

## Tests

- Catch2 v3, in `tests/unit/<module>/test_*.cpp`, one binary `bt_tests`.
- Tags: the module (`[grid]`, `[model]`, `[solve]`…), `[slow]` for cases over
  about a second (skipped by `just test`), `[parallel]` for cases that run
  threads (the ThreadSanitizer build runs exactly these).
- Test solver results with invariants: assembly counts, solution counts,
  levels and moves text (`movesText()`), and compare assembly **multisets**
  between serial and parallel runs, never only counts (a run that loses one
  assembly and duplicates another passes a count check).
- Never assert exact iteration counts; they measure search-tree visits and
  change with valid optimisations.
- The `[parallel]` cases must include a puzzle whose result has a symmetry
  breaker (e.g. `examples/Bermuda.xmpuzzle`): only those puzzles exercise the
  rotation checks from worker threads, so a TSan run without one reports no
  races while real ones remain.
- Fixtures live in `tests/data/`. No test reads another checkout.
  `tests/data/puzzles/new-grids/` holds the hexagonal and cairo puzzles,
  made by this engine (the original has no such grids, so the tests that
  compare every top-level fixture with the original's recordings skip
  that folder; the new grids' own checks are in `test_puzzle_file.cpp`
  and `tests/cli/test_cli.py`).
- Per-fixture checks belong in the C++ tests and the regression suite,
  which go through every file. The web tests (engine and e2e)
  sweep one fixture of each grid, `web/tests/helpers/fixtures.ts`'s `SWEEP`:
  what the front end does with a puzzle depends on its grid, not on which
  puzzle it is. Add a file there only when its content takes the front end
  down a path the others do not, and say which.
- The tests run side by side: ctest runs the quick `bt_tests` cases in
  shards (`unit-N`, `BT_UNIT_SHARDS`, the cases shuffled with a fixed
  seed), each `[slow]` case as a test of its own (`slow: <case>`; in the
  TSan build each `[parallel]` one too), beside the contract suite, which
  runs its tests at once (`--jobs`); the e2e tests run in E2E_WORKERS
  apps at once. So no test may depend on another's
  order or files, and only the `timed` e2e project may assert on time. A
  unit test's temp folder is a `ScratchDir` (`tests/unit/scratch_dir.h`),
  never a fixed name: other sessions' runs share the temp folder.
- The Python module (`python/`, decision 0018): `python/tests/` (unittest;
  ctest `python` when the module is built, so `just test-all` runs it),
  the original's tests ported as they were plus new ones; the module
  keeps the original module's names, so change them only with a reason.
- No test writes in the user's cache folder, and no two solves share a
  search folder (decision 0035: a second solve of a problem in the same
  folder is refused). The engine's default folders follow
  `SHAPELOGIC_SLIDE_DIR` and `SHAPELOGIC_PANEX_DIR`: the unit tests set them
  per process (`tests/unit/test_search_folders.cpp`), the contract client
  per engine, the CLI's tests and the web engine tests per run, the
  regression suite per sliding or stacking run. A new suite that starts
  an engine or the CLI does the same.
- `just check` passes and must stay so. The checks are in `.clang-tidy`
  (each one left out has its reason there; `tests/unit/.clang-tidy` leaves
  out a few more). Where the analysis cannot see an invariant, say why on
  the line above: `// NOLINTNEXTLINE(check): reason`; never a bare NOLINT.
- The engine: `tests/unit/engine/` drives it in process; `tests/contract/`
  (ctest `contract`) drives `shapelogic_engine` as a subprocess and checks every
  reply and event against `api/`. A new method needs its schema entry, a
  contract call that succeeds (the coverage check) and
  `python3 api/gen_api_md.py` (ctest `api-docs` fails on a stale
  `docs/API.md`); see `api/README.md`.

## SIMD and platform code

- Use `__attribute__((target("avx2")))` on individual functions, never
  `#pragma GCC target`. Gate every ISA path behind runtime detection and keep
  a scalar fallback. Supported: macOS arm64/x86-64, Linux x86-64, Windows
  x86-64 (MinGW).

## Finishing a piece of work

The C++ side, in this order:
1. `just test-all`; `just test-asan`; `just test-tsan` for changes to
   threaded code. Run the quick suite (`just test`) a few times: a rare
   hang shows only so.
2. The regression runs the change can touch (`tests/regression/README.md`):
   `just regress --tier all`, with all threads and with `--threads 1`,
   `--solver classic` and `crowell`; `--tier sliding` and `--tier
   stacking` (and `--solver panex`) for those solvers; `just regress-text`
   for what the CLI prints (or `just gate regression`, all of them). An
   answer changed on purpose is recorded with `--update` and said in the
   commit.
3. `just check` on the changed files.
4. For solver changes, the speed of two builds (`tests/bench/bench.py
   --session --old-bin <the earlier build's shapelogic_cli>`, and the
   regression suite's solve times): no slower than before, one solve at a
   time on a quiet machine, never beside a sanitizer run. Upstream
   BurrTools is timed only when the user asks (`just burrtools-compare`).

Then, for any work:
- `docs/ARCHITECTURE.md`: a map line for each file added, moved or
  removed, and the module's rules changed where an invariant changed (no
  new prose sections; the map test fails on a source file without a line).
- The active plan's Status and Done lines.
- The work left ready to commit, with the message it would get (CLAUDE.md:
  no commits unless the user asks).

## The web front end (`web/`)

Electron + TypeScript over `shapelogic_engine` (decisions 0003, 0012, 0013), built
in the stages of [docs/FRONTEND-PLAN.md](docs/FRONTEND-PLAN.md) on Girish
Sharma's mockup ([docs/mockup/](docs/mockup/)). Read before a front-end
task what the list at the top says (the `web` section of
docs/ARCHITECTURE.md for the folders the work changes), and decisions
0012 and 0013. docs/webui-spec.md (what each screen does) and docs/API.md
(every method and event) are searched, not read whole. `web/README.md`
says how to run it.

**Rules.**
- The engine is the truth: the UI never computes what the engine can
  answer (cells, fits, counts, meshes, sorts).
- Every mutating call waits for its revision (decision 0012's order: the
  reply, then its `document.changed`, then the poll's events); no state the
  engine holds is duplicated except as a mirror refreshed by
  `document.changed`.
- `invalid_state` is shown as "pause first" (decision 0011), never retried.
- The renderer never sees Node or Electron (only `window.bt`, through
  `renderer/engine-proxy.ts`); frames are never parsed in the renderer by
  hand, only through the client's views (`engine/frames.ts`).
- Keys come from the one table (`renderer/shell/keys.ts`, stage 1b).
- The mockup's wording is kept unless the original's is more exact.
- Files derived from the mockup or the original cite their source at the
  top (`// From docs/mockup/index.html: renderShapes, renderInsp`), as
  ported C++ does.
- An engine addition a stage needs is its own commit on the C++ side
  (`Engine: <method>`), following api/README.md's "Adding a method", then
  `just app-gen`; never silently.

**Code.** TypeScript, strict (`noUncheckedIndexedAccess` on); ESLint's
strict type-checked rules and Prettier decide the rest. Relative imports
name their `.ts` file and only erasable syntax is used (no `enum`,
`namespace` or parameter properties): Node runs the tools by stripping
types. `src/engine/` is platform-free (Node only in `node-transport.ts`;
`tsconfig.web.json` compiles it without Node's types, ESLint refuses Node
imports there). Each file starts with the SPDX line and a comment saying
what it is for; public declarations have doc comments.

**Tests.** Vitest: `web/tests/unit/` (fast, no engine; the scripted
transport and the fake engine's recordings), `web/tests/engine/` (the
release engine as a child process; skipped with a message when it is not
built), `web/tests/e2e/` (Playwright, from stage 1b). After a change to
what the engine sends, `npm run record` (in `web/`) records the fake
engine's sessions again. A timing assumption in an engine test is scaled
to the release build (the 2x4x5 dominoes box runs about 9 s on one
thread).

**Finishing a piece of front-end work**, in this order:
1. `just app-check`, `just app-test` (with the release engine built, so
   the engine tests run) and the e2e spec files the change touches
   (`npx playwright test tests/e2e/<file>` in `web/` after `npm run
   build`); the whole e2e suite (`just app-e2e`) only when the user asks,
   and say what was left unrun. A flaky test is a bug of the work.
2. After a core or engine change, the C++ steps above for what changed
   (`just test-all`, the contract tests, `python3 api/gen_api_md.py`, the
   regression runs it could touch), then `just app-gen` and the staleness test.
3. The reference screenshots reviewed when they changed.
4. Then the steps for any work above (ARCHITECTURE.md's map and rules, the
   plan, ready to commit). An engine addition is its own commit before the
   front end's (`Engine: <method>`); an audit's fixes one of their own after
   it.

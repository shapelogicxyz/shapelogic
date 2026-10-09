# AGENTS.md: working on BurrTools Alpha

BurrTools Alpha is a rebuild of the BurrTools puzzle solver's core (C++23)
with an engine process for a future web front end. The original application
(`../burrtools_rotations`, override with `BT_OLD_REPO`) is the **reference**:
ported code must give the same answers on the comparison corpus.

Read before starting any task (for the web front end, see "The web front
end" below instead):
1. [docs/PLAN.md](docs/PLAN.md): the stages, what is done, what is next.
2. [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): the layout and the rules.
3. The newest note in [docs/handoff/](docs/handoff/).
4. The decision records in [docs/decisions/](docs/decisions/) that touch your area.

## Commands

Use `just`. Do not invent ad-hoc build steps when a recipe exists.

```bash
just build [preset]     # dev (default), release, asan, tsan; output in build/<preset>
just test               # unit + contract tests without [slow]; the quick loop
just test-all           # everything; run before calling a stage done
just test-asan          # Address + UB sanitizers
just test-tsan          # ThreadSanitizer on [parallel] cases (from stage 3)
just oracle [--tier …]  # compare the new bt with the original burrTxt
just oracle-resume      # pause searches in one program, continue in the other
just oracle-roundtrip   # load + save every corpus file in both programs, compare
just oracle-files       # solve as burrTxt2 does, compare the solved files (stage 4)
just oracle-text        # bt's text output against burrTxt's, byte for byte (stage 8)
just oracle-baseline    # re-record the original's answers (rarely needed)
just check              # clang-tidy (needs LLVM installed)
just format             # clang-format
just web-install        # the web front end's packages (npm ci; Node 22)
just web-gen            # web/src/engine/api.generated.ts from api/*.json
just web-check          # the front end's type checks, ESLint, Prettier
just web-test           # its unit tests, and engine tests against build/release
```

## How to port code from the original

1. **Faithful first, restructured second.** Port the algorithm with its
   behaviour intact, prove it with tests and the harness, then reshape it.
   Never "improve" an algorithm while porting it.
2. **Cite the source.** At the top of each ported file, name the original
   file(s) it comes from, e.g. `// Ported from src/lib/voxel_0.cpp`.
3. **Port the tests too.** The original's `test/*.cpp` cases for the area
   come across (adapted to the new API), then new ones are added.
4. **Gate on the harness** (`tests/oracle/`). Where it cannot reach yet,
   write a unit test that compares against values taken from the original.
5. **Keep `.xmpuzzle` compatible both ways** (decision 0006).

## Code rules

- C++23, `namespace bt`. Files are `snake_case`, types are `PascalCase`,
  functions and variables `camelCase`, constants `kPascalCase`.
- Ownership: value types and `std::unique_ptr`. No owning raw pointers, no
  `new`/`delete`, no hand-written copy assignment that lists members.
- Errors: recoverable problems return `bt::Result<T>` (base/error.h); broken
  invariants use `BT_ASSERT` (base/assert.h), which is on in every build.
  Never put side effects inside `BT_ASSERT`.
- Cancellation: long work takes a `bt::StopToken` and polls it. No search
  has a stop flag of its own, and nothing resets a token (decision 0004). A
  search that must end its own run makes a `StopSource` from the token it
  was given, one per run (decision 0009).
- Solve state: only the run's worker writes a `SolveStateCell`; other threads
  read it and stop runs through a `StopSource`.
- No global state and no environment-variable switches inside `core/`.
  Options are passed in explicitly (A/B switches for benchmarking become
  fields of the options struct, set by the CLI).
- `core/` never prints. Diagnostics go through return values or callbacks.
- Every public declaration has a short doc comment saying what it is for.
  Comments explain why, not what.
- Concurrency: shared state is `std::atomic` or guarded by a mutex, always.
  Hand work to the solve's `bt::ThreadPool` (passed in, never global) rather
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
- Fixtures live in `tests/data/`. Copy them from the original repo; do not
  read the original repo from unit tests (only the harness does that).
- The engine: `tests/unit/engine/` drives it in process; `tests/contract/`
  (ctest `contract`) drives `bt-engine` as a subprocess and checks every
  reply and event against `api/`. A new method needs its schema entry, a
  contract call that succeeds (the coverage check) and
  `python3 api/gen_api_md.py` (ctest `api-docs` fails on a stale
  `docs/API.md`); see `api/README.md`.

## SIMD and platform code

- Use `__attribute__((target("avx2")))` on individual functions, never
  `#pragma GCC target`. Gate every ISA path behind runtime detection and keep
  a scalar fallback. Supported: macOS arm64/x86-64, Linux x86-64, Windows
  x86-64 (MinGW).

## Finishing a stage

Every stage ends with, in this order:
1. `just test-all`, `just test-asan` (and `just test-tsan` from stage 3),
   `just oracle` for what is ported (from stage 3 on the fast, full and slow
   tiers, with all threads and with `--threads 1`; from stage 4 every field,
   for `--solver classic`, `crowell` and `bt2`), `just oracle-resume`
   (from stage 3), `just oracle-files --tier all` (from stage 4),
   `just oracle-text --tier all` (from stage 8; after
   `oracle.py baseline-text --tier all`),
   `just oracle-roundtrip` (from stage 2; run
   `oracle.py baseline-files --tier all` once so the original's solved
   files are included), `just check` if LLVM is installed, and from
   stage 8 the benchmarks (`tests/bench/bench.py`: the default set,
   `--plain`, `--set glar`; within 5% of the original or faster, on a
   quiet machine, never beside a sanitizer run). Run the quick
   suite a few times: a rare hang shows only so.
2. `docs/ARCHITECTURE.md` updated to describe what now exists.
3. `docs/handoff/stage-N.md`: what was done, gate results, known issues,
   and the brief for the next stage.
4. `docs/PLAN.md`: tick the stage.
5. One commit for the stage (message: `Stage N: <summary>`).

## The web front end (`web/`)

Electron + TypeScript over `bt-engine` (decisions 0003, 0012, 0013), built
in the stages of [docs/FRONTEND-PLAN.md](docs/FRONTEND-PLAN.md) on Girish
Sharma's mockup ([docs/mockup/](docs/mockup/)). Read before a front-end
task: this file, docs/ARCHITECTURE.md (its `web` section), the plan,
docs/webui-spec.md, docs/API.md, decisions 0012 and 0013, and the newest
`docs/handoff/web-*.md`. `web/README.md` says how to run it.

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
  `just web-gen`; never silently.

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

**Finishing a front-end stage**, in this order:
1. `just web-check`, `just web-test` (with the release engine built, so
   the engine tests run) and `just web-e2e` (from stage 1b), each three
   times (a flaky test is a bug of the stage); after a core or engine
   change, the C++ finishing steps above for what changed (`just
   test-all`, the contract tests, `python3 api/gen_api_md.py`, the oracle
   tiers it could touch), then `just web-gen` and the staleness test.
2. The reference screenshots reviewed when they changed (from stage 1b).
3. `docs/ARCHITECTURE.md` updated (its `web` section: what exists).
4. `docs/handoff/web-N.md`: what was done, gate results, what the mockup
   did differently and why, known issues, the brief for the next stage.
5. docs/FRONTEND-PLAN.md: tick the stage.
6. One commit for the stage (`Web N: <summary>`); engine additions in
   their own commits before it (`Engine: <method>`); an audit's fixes in a
   commit of their own after it.

# AGENTS.md: working on BurrTools Alpha

BurrTools Alpha is a rebuild of the BurrTools puzzle solver's core (C++23)
with an engine process for a future web front end. The original application
(`../burrtools_rotations`, override with `BT_OLD_REPO`) is the **reference**:
ported code must give the same answers on the comparison corpus.

Read before starting any task:
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
just oracle-baseline    # re-record the original's answers (rarely needed)
just check              # clang-tidy (needs LLVM installed)
just format             # clang-format
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
   `just oracle-roundtrip` (from stage 2; run
   `oracle.py baseline-files --tier all` once so the original's solved
   files are included), `just check` if LLVM is installed. Run the quick
   suite a few times: a rare hang shows only so.
2. `docs/ARCHITECTURE.md` updated to describe what now exists.
3. `docs/handoff/stage-N.md`: what was done, gate results, known issues,
   and the brief for the next stage.
4. `docs/PLAN.md`: tick the stage.
5. One commit for the stage (message: `Stage N: <summary>`).

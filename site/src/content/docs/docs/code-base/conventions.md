---
title: Conventions and Further Reading
description: "The code rules, testing rules and porting rules of the ShapeLogic repository, and the documents inside it that go deeper."
---

The rules below are the ones that shape the code most. The complete list
is `AGENTS.md` at the root of the repository, which every change is
expected to follow.

## The original is the reference

ShapeLogic is a port of BurrTools. The port is **faithful first,
restructured second**: an algorithm is ported with its behaviour intact,
proved with tests and the comparison harness, and only then reshaped.
Nothing is "improved" while it is being ported.

- Each ported file names its source at the top, for example
  `// Ported from src/lib/voxel_0.cpp`. Files taken from the design
  mockup do the same: `// From docs/mockup/index.html: renderShapes`.
- The original's tests come across with the code, then new ones are added.
- `.xmpuzzle` files stay compatible both ways. Saves are byte-identical to
  the original's, and what this version does not model is kept and
  written back in place.
- Deliberate differences from the original are listed in the hand-off
  notes and in decision 0013 (the app's differences from the original GUI).

## C++ rules

- C++23, `namespace bt`. Files are `snake_case`, types `PascalCase`,
  functions and variables `camelCase`, constants `kPascalCase`.
- Ownership is value types and `std::unique_ptr`. No owning raw pointers,
  no `new` or `delete`.
- Recoverable problems return `bt::Result<T>`; broken invariants use
  `BT_ASSERT`, which is on in every build and never has side effects
  inside it.
- Long work takes a `bt::StopToken` and polls it. No search has a stop
  flag of its own and nothing resets a token.
- Only a run's worker thread writes its `SolveStateCell`; other threads
  read it.
- No global state and no environment variables inside `core/`. Options are
  passed in explicitly. `core/` never prints.
- Shared state is `std::atomic` or behind a mutex, always. Threads come
  from the solve's `bt::ThreadPool`, never from a raw `std::thread`.
- SIMD paths use `__attribute__((target("avx2")))` on individual functions,
  are gated behind runtime detection and keep a scalar fallback.
- Every public declaration has a short doc comment saying what it is for.
  Comments explain why, not what.

## TypeScript rules

- Strict TypeScript with `noUncheckedIndexedAccess`; ESLint's strict
  type-checked rules and Prettier decide the rest.
- Relative imports name their `.ts` file, and only erasable syntax is used
  (no `enum`, `namespace` or parameter properties), so Node can run the
  tools by stripping types.
- `src/engine/` is platform-free: Node appears only in `node-transport.ts`,
  and ESLint refuses Node imports elsewhere in it.
- The renderer never sees Node or Electron, only `window.bt` and
  `window.btApp`. Frames are never parsed by hand in the renderer.
- Every mutating call waits for its revision. `invalid_state` is shown as
  "Pause the solve first" and never retried.
- Keyboard shortcuts come from the one table in `renderer/shell/keys.ts`.
- Each file starts with the SPDX line and a comment saying what it is for.

## Testing rules

- Catch2 tests live in `tests/unit/<module>/test_*.cpp`, tagged by module
  (`[grid]`, `[solve]`, ...), `[slow]` for cases over about a second, and
  `[parallel]` for cases that run threads. `just test` skips `[slow]`; the
  ThreadSanitizer build runs exactly `[parallel]`.
- Solver results are tested with invariants: assembly counts, solution
  counts, levels and moves text. Serial and parallel runs are compared as
  **multisets** of assemblies, never by counts alone.
- Exact iteration counts are never asserted; they change with valid
  optimisations.
- The `[parallel]` cases include a puzzle whose result has a symmetry
  breaker, so that the rotation checks run from worker threads under
  ThreadSanitizer.
- Fixtures live in `tests/data/`; unit tests never read the original
  repository. Only the harness does.
- A new engine method needs a schema entry, a successful contract call and
  a regenerated `docs/API.md`.
- The app's Vitest suites are `web/tests/unit/` (no engine) and
  `web/tests/engine/` (the release engine as a child process); Playwright
  drives the built app in `web/tests/e2e/`. A flaky test is a bug.

## How work is organised

The core was built in numbered stages (`docs/PLAN.md`), the app in its own
(`docs/FRONTEND-PLAN.md`), and the deferred features in parts
(`docs/DEFERRED-PLAN.md`). Every stage ends the same way: the full test
and harness gates, `docs/ARCHITECTURE.md` updated to describe what now
exists, a hand-off note in `docs/handoff/`, the plan ticked, and one
commit. Decisions that shape the code are written up as numbered records
in `docs/decisions/` with their context, the decision and its
consequences.

## Further reading, in order

1. `AGENTS.md`: the working rules and the command list.
2. `docs/ARCHITECTURE.md`: every module, what it holds and the rules it
   keeps. Long, and the most complete description of the code.
3. `docs/API.md`: the engine's methods and events.
4. The decision records most worth reading first:
   - `0001-language-and-build.md`: why C++23 and CMake.
   - `0003-engine-process-json-rpc.md` and `0012-engine-protocol.md`: the
     engine and its protocol.
   - `0004-cancellation-and-solve-state.md`: stop tokens and the solve
     state machine.
   - `0009-assembler-run-layer.md`: one run layer for every assembler.
   - `0010-solve-session.md`: the solve session.
   - `0011-document-undo-and-solving.md`: the document, undo by saved
     parts, edits while solving.
   - `0013-web-front-end.md`: the app's shape and its differences from the
     original.
5. The newest hand-off notes in `docs/handoff/`: what was done last, known
   issues, and what was left for next.
6. `web/README.md`, `python/README.md`, `api/README.md`,
   `tests/oracle/README.md`, `site/README.md`: each part's own notes.

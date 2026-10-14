# 0004: One cancellation model and one solve state machine

**Status:** accepted (stage 0); `core/shapelogic/base/stop.h`, `core/shapelogic/base/solve_state.h`

## Context
In the original BurrTools, stopping a solve went through three mechanisms at
once: a `stopPressed` flag in the solve thread, an `action` value written by
both the GUI and the worker thread, and an `abbort` flag inside each
assembler that `assemble()` cleared on entry. Two real bugs came out of this
(October 2026):
- a Pause pressed just as assembling began was **lost**, because the
  assembler reset its abort flag after the request;
- a Pause while the parallel search was still generating its task list left
  a **partial task list** that the next run took for the whole search,
  finishing "solved" with most assemblies missing.
Each assembler also copied its own parallel and resume code.

## Decision
**Cancellation**
- `bt::StopSource` (owner) and `bt::StopToken` (reader). One source per run;
  a continued run gets a new source. **Nothing ever resets a request**, so a
  stop made before a search starts is still seen when it starts.
- `StopKind`: `Pause` (stop where it can be continued) or `Abort` (throw the
  run away). Abort overrides Pause.
- `child()` sources: stopped with their parent, or alone (an autosave pause
  stops only the assembly stage while take-apart work finishes).
- Search code takes a `StopToken` and polls it. **No component has a stop
  flag of its own.**
- Work that is interrupted while building state that is only valid when
  complete (e.g. a task list) **discards** that state; a resumable position
  is only ever written from a consistent point.

**Solve state**
- `bt::SolveState`: Idle, Preparing, Assembling, Draining, Paused, Finished,
  Error, with the transition table in `solve_state.h`. A move outside the
  table is a bug (`BT_ASSERT`).
- **Only the run's worker writes the state.** Other threads read it and stop
  runs through the StopSource. "Stopping…" is an active state plus a
  requested stop, not a state.
- `Finished` is reached only when the search itself reports completion.

**Shared parallel and resume layer** (stage 3)
- The assemblers share one task-splitting, worker and resume layer that
  implements the rules above once; each assembler supplies only its search.

## Consequences
- Pause/continue correctness is testable in one place, with stress tests that
  stop at many random points (stage 5 gate).
- Every new long-running operation (analysing a disassembly, a future Panex
  search) gets cancellation for free by taking a StopToken.

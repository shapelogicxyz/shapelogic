# 0009: One run layer for the assemblers, one thread pool for a solve

**Status:** accepted (stage 3a); `core/bt/solve/search_run.h`,
`core/bt/solve/resume_state.h`, `core/bt/solve/assembler.h`,
`core/bt/base/thread_pool.h`

## Context
In the original, assembler_0, assembler_1 and the BT2 driver each carried
their own copy of the parallel search: an `abbort` flag cleared on entry
(the lost-pause bug of decision 0004), raw `std::thread`s, a task list with
completion flags and reported signatures, the progress counters, and the
code that writes these into a saved position. The copies had drifted apart
three times. Thread counts came from a process-wide limit
(`setSolveThreadLimit`), the `BURRTOOLS_THREADS` variable and `-t`, and A/B
switches from environment variables read inside the library
(`BURRTOOLS_NO_SIMD`, `NO_AVX2`, `NO_AVX512`, `NO_NEON`,
`BURRTOOLS_HUANG_MEM_MB`). Decision 0004 asked for one shared layer.

## Decision
**A run.** Every `Assembler::assemble(callback, options, stopToken)` call is
one run (`SearchRun`). The run makes its own `StopSource` as a child of the
caller's token (`StopSource(const StopToken&)`), and the search polls only
that token. A callback answering false stops the run as a Pause; an
exception in any thread stops it as an Abort and is rethrown to the caller
once every thread is through (requested before the hand-over lock is given
up, so nothing is delivered after it). Nothing is reset: a continued search
is the next `assemble()` call with a new token. `assemble()` returns
`RunEnd::Finished` only when the search itself is through; callers never
infer completion from progress.

**The hand-over.** The callback is never called by two threads at once. A
search split over threads hands an assembly over once: under the lock it is
dropped when the run is stopped (the next run finds it again) or when its
signature was reported before (a task searched again after a stop), and
otherwise recorded and delivered. Signatures are the original's FNV-1a over
the placements.

**The resumable state** of a split or vector search is a
`SplitSearchState<Task>`: the tasks (subtree prefixes), one completion flag
each, the reported signatures, and for the vector search the number of
assemblies already reported. It lives in the assembler across runs and is
what a saved position of such a search holds (flags 2, 3, 4, in the
original's text, byte for byte: `resume_state.h`). A task is marked complete
only by a thread whose search of it returned while the run was not stopped.
A stopped search carries on the way it ran, whatever the next run's thread
count; a stopped vector search always continues as one (it cannot be
skipped forward by the plain search).

**Progress** is readable from any thread: iterations and the fraction are
atomics written by the searching threads. Split searches report per task
(`TaskProgress`, the original's weighted units); the plain search publishes
its fraction every 4096 steps and at the end of a run (the original read its
search stack unsynchronised from the GUI thread).

**Threads** come from one `bt::ThreadPool`, made by whoever owns the solve
(the CLI now, the solve session in stage 5) with the solve's budget, and
passed in `SearchOptions`. `run(n, job)` runs the job on the caller and on as
many lent threads as cores are free; work on threads of its own reports its
load. There is no process-wide limit and no environment variable: the CLI
maps `-t`, `0` meaning the hardware's threads. Thread counts are clamped to
`kMaxThreads` (256) before any thread is made. Shapes are warmed
(`Voxel::warmCaches`) before a search spreads over threads.

**A/B switches** are fields of `SearchOptions`: `vectorSearch`
(`BURRTOOLS_NO_SIMD`) and `SimdKernels` (`NO_AVX2`, `NO_AVX512`, `NO_NEON`);
in stage 3b also the vector search's memory budget (`BURRTOOLS_HUANG_MEM_MB`).

**BT2 (stage 3b)** uses the same run, hand-over and pool. Its work stealing
(`bt2Assemble`: split a search, run slices, give branches to idle threads,
park unfinished branches across a pause) becomes a second driver beside
`runTasks()`, over an interface with `splitSearch`, `searchFinished`,
`assembleLimited` and the parked branches, replacing `clonePrepared` /
`addProgressPeer` and the assembler's own stop flag.

## Consequences
- The pause, continue and save rules are implemented once and tested once
  (stop before start, random pauses on every path, saved positions both
  ways with the original: `just oracle-resume`).
- The CLI and later the session decide thread counts; the core never reads
  the environment.
- Searches that report by tasks and the plain search share one progress
  contract; a front end polls `finished()` and `iterations()` freely.

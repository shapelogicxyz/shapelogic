# 0009: One run layer for the assemblers, one thread pool for a solve

**Status:** accepted (stage 3a), completed in stage 3b;
`core/bt/solve/search_run.h`, `core/bt/solve/work_stealing.h`,
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
(the CLI, the solve session) with the solve's budget, and passed in
`SearchOptions` (the session keeps the take-apart's pool apart from the
assembly's: decision 0010). `run(n, job)` runs the job on the caller and on
as many lent threads as cores are free; work on threads of its own reports
its load. There is no process-wide limit and no environment variable: the
CLI maps `-t`, `0` meaning the hardware's threads. Thread counts are clamped
to `kMaxThreads` (256) before any thread is made. Shapes are warmed
(`Voxel::warmCaches`) before a search spreads over threads.

**A/B switches** are fields of `SearchOptions`: `vectorSearch`
(`BURRTOOLS_NO_SIMD`), `SimdKernels` (`NO_AVX2`, `NO_AVX512`, `NO_NEON`) and
`vectorMemoryBudget`, the memory assembler 1's vector search may take
(`BURRTOOLS_HUANG_MEM_MB`).

**BT2** uses the same run, hand-over and pool. Its work stealing
(`bt2Assemble`: split a search, run slices, give branches to idle threads,
park unfinished branches across a pause) is the run layer's second driver,
`runWorkStealing()` in `work_stealing.h`, over an interface (a concept) the
search provides: `searchSlice` (assembleLimited), `branchFinished`
(searchFinished), `splitBranch` (splitSearch, which also counts the new
branch in the progress), `retireBranch`, and `takeParked` / `park`. A branch
is a small object of the assembler (a dancing cells search and its share of
the progress), not a copy of the assembler as in the original, so
`clonePrepared`, `addProgressPeer`, the root pointer and the stop flag are
gone. The caller's thread starts with the root; the threads lent by the
pool count as taking part once they have started, so one that starts late
finds the run over and goes (the pool may run two lent jobs one after the
other on one thread). BurrTools 2's branches never visit a place twice, so
what a thread finds after another has paused the run is still handed over
(`SearchRun::deliverFound`; dropped only after an abort): it would
otherwise be lost. `bt2Threads()` keeps the original's choice of thread
count (the count asked for, else the hardware's less two, at most 16); the
CLI applies it.

**Saved positions of BT2.** The original saves BT2's search as assembler
0's dancing links position, which its dancing cells search never moves:
always `0 <iterations> (0 0)`. Restored, it starts the search again, while
the problem keeps what was found, so every assembly found before the pause
is reported twice. The new core writes the same text, but restores only a
search that has not started (`0 0 (0 0)`); any other BT2 position is
`CannotRestoreInterrupted`, so `beginSolving()` drops the partial results
and the solve starts clean. Pausing and continuing in memory loses
nothing (the parked branches).

**Saved positions are checked before they are used.** The original
replayed whatever a position held; a damaged one read and wrote out of
bounds. Both assemblers parse into temporaries and change nothing unless the
whole position is good; a bad one is `CannotRestoreSyntax`. Assembler 1
checks that the stacks fit the matrix and each other (states, columns,
rows, hidden-row batches, progress entries, a frame's row in its column, a
split or vector search at its start, every task), then replays them step
by step, each step checked (a column closed must be open, a column's rows
taken out once, a row hidden must be whole in the matrix; a task is
replayed and taken back). Assembler 0 covers each level of the position,
and of every saved task, only after checking that its column is one the
search could choose there and its row one of that column's rows still in
the matrix. Task counts and prefixes are bounded by the text and the
pieces. (The audit of stage 3 found the gaps: a damaged assembler 0
position crashed, a damaged assembler 1 one could loop for ever.)

**Where a search stands.** The plain searches go back to their first
level between two of its rows (assembler 0: pos 0 with a row still
covered; assembler 1: one frame with the first column still closed): that
is not a search that has not started, which only an untouched matrix is;
such a position continues as the plain search whatever the next run asks
for. A plain search clears the flag a vector search left when it was
stopped before it reported anything; a search that is through says so in
its saved position (assembler 0 drops its tasks, assembler 1 its rows),
however it ran.

## Consequences
- The pause, continue and save rules are implemented once and tested once
  (stop before start, random pauses on every path, saved positions both
  ways with the original: `just oracle-resume`).
- The CLI and later the session decide thread counts; the core never reads
  the environment.
- Searches that report by tasks and the plain search share one progress
  contract; a front end polls `finished()` and `iterations()` freely.

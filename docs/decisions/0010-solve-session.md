# 0010: The solve session

**Status:** accepted (stage 5); `core/bt/solve/solve_session.h`,
`core/bt/solve/solve_progress.h`

## Context
The original ran each solve run in a `solveThread_c`, made anew for every
Solve or Continue, while the problem held the assembler between runs and
was read and written by the GUI thread and the solve threads at once
(atomics for the counts, a recursive mutex for the solutions, the live
assembler's position written into a file being saved). Its stop went
through a flag, the solve thread's action value, the assembler's own stop
and the disassemblers'; the GUI thread joined the take-apart workers itself.
Decisions 0004 and 0009 set the rules for stops, states and threads; the
model was made lock-free in stage 2 on the understanding that the session
would decide how a running solve hands its results over.

## Decision
**One session per problem solve, across its runs.** `SolveSession` lives
from Solve to the end of the solve; the search (the assembler) lives in it
between runs, not in the problem. Each run is a thread of the session's
own. The owner's calls (`start`, `prepare`, `resume`, `abort`, `wait`,
`setOptions`) come one at a time; `pause`, `pauseAtSavePoint`, the state,
progress, statistics, failure and `readProblem` may come from any thread.

**State.** The first move of a run (to Preparing, or Assembling for a
paused one) is made by the call that starts it, before the run's thread
exists, so the caller sees an active state at once; after that only the
run's thread writes the state. Between runs the owner's calls move it
(Finished or Error to Idle; abort to Idle).

**Stops.** One `StopSource` per run with a child for the assembly search
alone (the save point, the original's `stopSoft`). Callers only ask; the
run's thread winds the run down. A stop is asked for under the take-apart
queue's mutex and its waits are woken, so none misses it.

**Results.** While a run is active the session alone writes its problem,
under its own mutex; others read through `readProblem(f)`. The saved
position belongs to the paused state: a run clears it when it starts
assembling and writes it when it pauses; a puzzle saved while a solve runs
has none (save at a save point instead).

**Threads.** The session makes two pools per run from
`SolveOptions::threads`: the assembly search's (the original's assembler
threads) and the take-apart's (the original's `helperPool_c`), which counts
the load of the assembly and of the busy take-apart workers and lends
threads to the levels. One pool for both would keep the assembly's lent
threads while it waits at a full queue, so the levels could not use those
cores; this is the one exception to decision 0009's one pool per solve.
The take-apart workers are threads of the session's own, one
`Disassembler` each.

**BurrTools 2** keeps its saved-position format (decision 0009), with one
addition: a search that is through is saved as the dancing links position
after the last piece, which assembler 0 also writes when through, and is
restored as through. A pause while its last assemblies are being taken
apart then survives a reload. Such files are one-way: the original's
`setPosition` reads past its row array on any position after the last
piece (its own assembler 0 one too) and crashes, so they continue in the
new core only (stage 5 audit).

**A solve with assemblies waiting is not through.** Counted pending
assemblies (queued or stopped take-aparts of a paused run) keep the solve
open when a run does not take apart: it ends Paused with the search
through, and a run that takes apart finishes it.

## Consequences
- A front end (the engine, stage 7) starts, pauses, continues and aborts
  without knowing about threads, reads progress freely, and reads results
  through one lock. Autosave is a policy of the front end: pause at a save
  point, save, resume.
- The puzzle must not be edited while a run is active (the session resets
  hotspots and writes the problem); the app layer (stage 6) guards it.
- Pause/continue correctness is tested against straight solves by the
  saved solutions as multisets, with random pauses, a delay sweep and
  ThreadSanitizer.

# 0015: Stacking and Panex puzzles in the core and the CLI

**Status:** accepted (deferred part 3a; amended by part 3b: the document
and the engine); `core/bt/model/stacking.h`,
`core/bt/model/stacking_types.h`, `core/bt/solve/stack_search.h`,
`core/bt/solve/panex_search.h`, `core/bt/io/blockpack.h`, the stacking run
of `core/bt/solve/solve_session.cpp`, `core/bt/io/paused_solve.h`,
`core/bt/app/document_stacking.cpp`, `engine/methods_stacking.cpp`,
`api/stacking.json`.

## Context
The original's stacking puzzles (`src/lib/stacking.{h,cpp}`, `panex.{h,cpp}`,
`blockpack.{h,cpp}`, the solve thread's `runStacking`, the GUI's Solve,
Continue and Abort branches, the BTSOLVE2 `searchz/` sections of its
paused-solve export) were the last solver of BurrTools: discs on rods,
moved one top disc at a time, solved by a breadth-first search over
stackings. The Panex search searches from both ends, keeps only the newest
levels in memory, saves every few levels on disk, traces its path back
between them, and stops and carries on from a folder per puzzle. The
original's Stacking Solver runs on that same search whenever the puzzle
fits it (eight rods, 25 symbols), and on its own `findStackPath`
otherwise. The original read `BURRTOOLS_PANEX_DIR`,
`BURRTOOLS_PANEX_DISK_GB`, `BURRTOOLS_PANEX_LOG` and
`BURRTOOLS_NO_TOWER_RULE` from the environment, ran its steps on threads
of its own, and its solve thread ended every stacking run Finished, a
stopped search too. Decisions 0004, 0009 and 0010 rule out all three as
they were. Until this part the new core kept rod sets and stacks as
preserved XML (decision 0008) and refused stacking solves.

## Decision
**The model goes in `model/`, the searches in `solve/`.** `Puzzle` holds
the rod sets (`stacking::RodSet`; a stacking puzzle has the default one
from its making or its reading) and `Problem` the rod set it uses and the
start and goal stacks (`stacking::StackMap`), kept in step by the shape
operations the original hooked (a removed shape's discs go, swapped
shapes swap, a lowered count takes the copies off: `trimStacks`).
`bt::stacking` (`model/stacking.h`) holds the board's rules, the discs,
the editing of the stacks (`placeDisk`, `liftTop`, `moveDisk`,
`dropInstance`, `syncMaps`), `setupError`, `layoutBoard`, `moveAllowed`,
`searchInput` and the path's frames (`pathSeparation`: three a transfer).
The file is read and written as the original wrote it, so saves stay
byte-identical; what the original dropped from `<rodSets>` and
`<stacking>` (unknown attributes and children) is dropped here too: the
format is the original's, not ours to extend there. `solve/stack_search.h`
is `findStackPath`; `solve/panex_search.h` the Panex search with its saved
searches (`savedSearch`, `savedMs`, `searchFolder`, `discardSaved`), byte
for byte the original's files (`BTPANEX4`, BTPANEX3 read, the level
files, the folder named by the same fingerprint), so a search stopped by
one program carries on in the other. `io/blockpack.h` packs a saved
search's files for an export, byte for byte the original's packing.

**Stacking runs go through `SolveSession`** (decision 0010).
`SolveOptions.stacking` (`stacking::StackingOptions`: the solver, high
memory, the search's own 20-minute autosave, the work folder and disk
budget, the tower rule) carries what the solve thread's flags did. A run
does what `runStacking` did: the problem's solutions go, one start (the
file's start stacks) is searched, a path found is the one solution with
the start stacking as its assembly, the original's notes word for word as
`note()`, the progress has stage `Other` with the stackings found, the
depth both ends have searched and the moves traced back (`searchTraced`,
`panexSearch`). Launch checks what the GUI's Continue checked (a rod set,
Find Solutions or Just Count, `setupError`, the Panex Solver's
`unsupported`), puts the discs' hotspots at their centres (only where
they are not: the document does it before the first run), and runs with
`resume` always, as both stacking solvers did: `start()` (Solve) throws
a saved search away first, `resume()` (Continue) carries it on. `abort()`
throws it away too (cb_BtnAbort). There is nothing to prepare
(`prepare()` is Unsupported), and a save point does nothing: the GUI
never autosaved a stacking solve, its search saves itself.

**A stopped search is not through.** The solve thread ended a stopped
Panex search Finished; here the session ends Paused (decision 0004:
Finished only when the search says so) and `resume()` carries it on. The
problem is written as the solve thread left it (solved, one assembly, no
solution, the time), so files are the original's; on a reloaded file a
new session's `resume()` carries the saved search on, since what is saved
is the search's folder, not the problem.

**No environment, no raw threads, no printing in the core.** The work
folder and the disk budget are options: `bt` takes `--panex-dir` and
`--panex-disk`, else `BURRTOOLS_PANEX_DIR` and `BURRTOOLS_PANEX_DISK_GB`,
else the user's cache folder's `panex`; an empty folder saves nothing
(the search still solves, by halves when its levels outgrow memory). The
level log is a callback (`bt --panex-log`), the tower rule a switch (`bt
--no-tower-rule`). The search's steps run on a `ThreadPool`: a step the
original gave each thread a slice of takes its slices from a counter (the
pool may lend fewer threads), and the level the original wrote on a thread
of its own is written by the first job of the next level's expansion
(which then helps with the level), or at once when something needs the
file; the pool has a core more than the levels' threads for that job, as
the original had its writer thread. The files and the paths are the same
on any number of threads.

**The paused-solve file carries the saved search** (BTSOLVE2's
`searchz/<name>` sections, decision 0006): `io::savePausedSolve` writes
the folder's `state.bin` and, if wanted, its levels (in name order),
packed, streamed into the exclusive temporary file;
`io::loadPausedSolve` unpacks them into the folder the import is told
(emptied first, as the original did). `Document::exportPaused` judges a
stacking problem paused as the GUI did (a saved search in the work
folder); `importPaused` is given the work folder.

**The playback plays transfers**: a stacking solution's steps are its
transfers (three frames each, as the original's slider counted them),
its frames are places, not shifts, and the poses are centred on the board
(`stackingCentre`), there being no result shape.

**`bt` solves stacking puzzles as burrTxt did**: `--solver` stacking or
panex, `--high-memory`, `--restart`, the Panex folder and disk, Ctrl-C
stopping and saving a Panex search (a second one quits), the summary and
search lines word for word, `-p` the transfer plan, `-x`; `--save` runs a
session (burrTxt2 had no stacking solver).

**Deliberate differences**, each from a rule above or a defect of the
original:
1. A stopped search ends the session Paused, not Finished (the problem's
   state as the original's).
2. The Panex search's parallel steps and its level writer run on a pool
   (the files and paths the same).
3. `prepare()` is refused and a save point does nothing on a stacking
   solve (the GUI had no Prepare for it and did not autosave it).
4. An empty work folder saves nothing where the original fell back on the
   cache folder; the programs pass the folder in.
5. `bt --save -d` solves stacking puzzles (burrTxt2 had no stacking
   solver; the GUI's Solve ran the solve thread).
6. `bt -a s1` does not draw a stacking assembly: the original drew it into
   the result shape a stacking problem does not have and died of an
   assertion (the harness compares what it printed before).
7. An export puts the levels in name order (the original in the folder's
   order, which an import does not depend on).

## Part 3b: the document and the engine

**The document's stacking commands are the GUI's callbacks**
(`document_stacking.cpp`: mainwindow_rods.cpp's rod sets, the disc list,
the stacking branches of the Problems tab), each one undo step: the GUI's
own rod history (rodPast, rodFuture, forty deep) is the document's undo.
Rod sets have ids beside the puzzle's, as shapes, colours and problems do
(decision 0011; `r<n>` in the engine, labels R1, R2), and the list
commands save the whole puzzle for undo. The rules of a rod set are
written as cb_RodField wrote them (the counts clamped, the pocket only
with Panex columns, the outside channel with three rods or more, "can
move over" written off), the reads show them as its fields did. Which
edits drop a problem's solutions follows each callback: new rules
(cb_RodField) and an emptied stacking (changeProblem) do; a disc's size,
a disc moved, placed or taken off (through the counts' own rules), a
board chosen do not. The rod sets are not changed while anything is
solved (the GUI greyed them out while a solve ran), a problem's stacks
not while it is (decision 0011).

**What the GUI hid on a stacking puzzle is refused** (`invalid_argument`),
as part 1 refused what it greyed out on sliding puzzles (decision 0014):
the cell editor and the tool tabs (the disc list replaced them), a new
shape of a given size (New made a disc of size 1), and the Problems
tab's brick commands, whose buttons did the stacking ones there (the
result, the counts and ranges, the parts' order, the groups). The
stacking branch of cb_ColSel (painting a disc's cells in a constraint
colour) could not be reached (the colour list is hidden on stacking
puzzles) and is not ported: a disc's colour is the shape's own colour
(`shape.setColour`, the Color button the GUI kept).

**A paused stacking solve is its saved search.** The engine's settings
hold its folder and disk budget (`panexFolder`, default the user's cache
folder's `panex` as the CLI's and the original's; `panexDiskGB`), given to
every stacking solve and to whatever looks for a saved search:
`solve.status.savedSearch` (the description the GUI's Solve asked with,
the time it holds, the bytes of its levels; none while the problem's run
is active), `solve.exportPaused` (with or without the levels) and
`solve.importPaused`. `solve.abort` also takes a search saved on disk
that no session holds (after a restart, an import, a reopened file),
throwing it away with the problem's results, as the GUI's Abort was
offered for it (abortable()); decision 0012's rule that an abort needs a
solve under way stands, a saved search being one. `solve.savePoint` is
refused (`unsupported`) on a stacking solve rather than answered and
never paused: the GUI never autosaved one, its search saves itself every
20 minutes (`StackingOptions.autosave`) and when it stops.

**The solver a request does not name is the one the GUI offered**: the
original's menu had one entry for a stacking problem, the Panex Solver
for a rod set with Panex columns, else the Stacking Solver
(syncSolverTypeMenu); `engine.info.stackingSolvers` lists both.

**A stacking solution counts transfers**: its row's moves, steps and
text are its transfers, as the original showed them (logicalMoves) and
as the playback steps, not the three frames of each.

**The reads the editor and the player need** are the engine's
(`stacking.board`: the rods' names and capacities, the stacks, where
rods and discs stand, the validity bar's sentence, the centre the
playback's poses are placed around; `stacking.rodSetLayout`, the rod set
drawn alone as showRodSet drew it), so a front end computes none of them.

**The front end** keeps its stacking notices until part 4 builds the
editor: `web/src/renderer/state/kinds.ts` holds the one rule (the Solver
disabled, New not offering the grid), which part 4 removes.

## Consequences
- The oracle harness has a `stacking` tier (the fixtures and 39 puzzles
  built with the original's code from its `test_stacking.cpp`,
  `oracle.py stacking-puzzles`), compared for both solvers in the
  summary, the solved files state by state (the original's solve thread,
  `stack_solve_old.cpp`) and the text; `oracle.py panex-resume` stops
  Panex searches in one program at a sweep of depths and carries them on
  in the other (the saved folders byte for byte the same);
  `oracle.py blockpack-check` packs with one program and unpacks with the
  other.
- Part 3b gave the document its stacking commands and the engine
  `stacking.*`, `SolveOptions.stacking`, the Panex folder and disk budget,
  the saved search in `solve.status` and its abort without a session; the
  contract suite pauses a Panex solve, finds its saved search in another
  engine and carries it on. Part 4 builds the front end on them.

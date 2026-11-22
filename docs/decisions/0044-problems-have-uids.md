# 0044: Every problem has a uid, and its saved searches are named by it

**Status:** accepted; `core/shapelogic/model/problem.h` (`Problem::getUid`,
`renewUid`, `newProblemUid`, `copyDefinition`), `core/shapelogic/model/puzzle.cpp`
(`Puzzle::addProblem`), `core/shapelogic/io/puzzle_file.cpp` (`readProblem`,
`giveUids`, `writeProblem`), `core/shapelogic/solve/sliding_search.cpp`
(`problemKeyOf`), `core/shapelogic/solve/panex_search.cpp` (`fingerprintOf`)

## Context
The user's "1 to 8 Heart" puzzle has three problems; the first two, "Board 1
- Start / Goal" and "Board 2 - Start / Goal", are alike in every cell: the
same board, start and goal marks, pieces and counts. Board 1 was paused
after a sliding search of 187 moves; Board 2 was never solved. At the next
start the app said the cache folder held a "Paused sliding solve of Abe.1 to
8 Heart.xmpuzzle, Board 2 - Start / Goal".

A sliding search's folder (`slide-<key>`) and a stacking search's were named
by the problem's contents alone (`problemKeyOf`, `fingerprintOf`), so two
problems alike in every cell had one folder. When a sliding or stacking
puzzle opens, the window asks the engine for each problem's folders
(`solve.searchFolders`) and tells the main process whose they are
(`renderer/solver/cache-links.ts`); the main process keeps one link a folder
(`main/cache-files.ts`), so the last problem asked, Board 2, named the
folder. The same sharing let the copy carry the first one's paused search
on as its own, and a solve of either throw the other's away (decision 0035
refused the second solve while the first ran, but a pause gave the folder
to whichever came next).

## Decision
**A problem has a uid**: 64 bits, saved as the `uid` attribute of its
`<problem>` element (16 hex digits), kept through renames, moves, undo and
saves. A new problem (`Puzzle::addProblem`) and a copy of one
(`copyDefinition`: Duplicate, a grid conversion, an assembly's new problem)
get a random one. The original reads the file as before: it knows no such
attribute and drops it.

**The uid names the problem's saved searches**: it is mixed into
`problemKeyOf` (which names the sliding folders, the state's problem key and
the solve's lock file) and into the stacking search's `fingerprintOf`. Two
problems alike in every cell have folders of their own, and a folder is
only ever found by the problem that made it; the cache manager can no longer
name another problem for it.

**A problem read without a uid gets one made from its place in the file and
its definition** (`giveUids`, FNV-1a over the index and the problem's
definition as it is written), not a random one: the same file opened again,
or in a second window, names its problems' searches as before, though it
was never saved with uids; two problems alike in every cell are still told
apart by their places. A uid an earlier problem of the file already has (a
problem copied by hand in the XML) is replaced the same way.

**A problem paused by a version before uids keeps none** (uid 0), and a
search without a uid is named as those versions named it, so the solves
paused before this change carry on. It is a problem read without a uid
whose state is Solving; of two such problems alike in every cell only the
first keeps none (they shared one folder, and the file cannot say whose it
was). It is saved without a uid and read again the same way, and it keeps
none until a load finds it no longer paused. The test is
`sliding session: a copy of a problem paused before uids does not take its
search`, on the v1.8.1 fixture with an unsolved copy of its problem.

**A copy of the file keeps the uids** (Save As, or a copy made outside the
app): a paused solve carries on in the file under its new name, which is
the usual reason for Save As. Such copies share their saved searches as
before; decision 0035's lock keeps two solves out of one folder.

## Consequences
- The searches saved before this change are found by the problems they
  belong to. A copy of a problem that was paused before uids (Board 2 above)
  no longer finds the first one's search, and the cache manager names the
  first one again once the file is opened.
- Tests that compare a save with the original's, or with a fixture written
  before uids, leave the uid attributes out (`test::withoutUids`, and the
  same in the contract and Python tests, the oracle and the e2e tests).
- `stacking document: identical problems share their saved search` became
  `identical problems have their own saved searches`; the contract case
  `one_solve_of_a_problem_at_a_time` and the session's lock case now say a
  copy of a problem has its own lock.

## What was not done
- **The engine's API does not show the uid.** Nothing in the window needs
  it: the folders `solve.searchFolders` gives are already the problem's
  own, and the window tells their link when a document opens or a run
  ends, with the problem's name of the moment.
- **Save As does not give new uids**: a paused solve in the new file would
  lose its saved search unless its folders, often many GB, were copied.

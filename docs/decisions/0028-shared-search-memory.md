# 0028: The searches running at once share the search memory

**Status:** accepted; `core/shapelogic/base/memory_share.h` (`MemoryShare`),
`core/shapelogic/base/system.h` (`folderWriteError`),
`core/shapelogic/solve/sliding_search.cpp` (`SetStore`, `LevelSearch`),
`core/shapelogic/solve/panex_search.cpp` (`Searcher::budget`),
`core/shapelogic/solve/stack_search.cpp`,
`core/shapelogic/solve/solve_session.cpp` (`assembleAndTakeApart`,
`runStacking`, `folderWritable`), `engine/engine.h` (`State::memory`),
`engine/convert.cpp`, `api/solve.json` (`solve.status.folderFailure`),
`web/src/renderer/solver/workspace.ts` (`askFolder`)

## Context
The document runs several solves at once, one per problem (decision 0010),
and the Solver tab lets the user start a second while the first runs.
Each sliding or stacking search was given the whole Search memory setting
(`searchMemoryGB`; automatic, half the machine's memory) when it started.
Two such searches at once could take all of the machine's memory between
them and push it into swap. Threads need no such care: a search's workers
sleep when idle and take their work in pieces, so the system shares the
cores of two solves well, and a solve that ends leaves the other every
core at once.

A sliding search also counted on its folder in the cache folder (where its
levels go once they outgrow memory, and where it saves itself) without
looking first. When the folder could not be written (a drive that is gone,
a folder without write access), it ran on in memory alone until its
budget ran out, and could not save itself.

## Decision
**Search memory is shared.** The setting is what the sliding and stacking
searches running at once share. The engine holds one `MemoryShare` (the
setting in bytes) and every solve's options hold it. A sliding run that
takes apart, and a stacking run, hold a lease on it while they run; each
search's budget is the total over the leases held, read again as the
search goes. A search starting shrinks every other search's part at once,
and one ending (finished, paused, cleared) gives its part back. A change
of the setting reaches the searches already running. Without a share (the
CLI, the Python module) a search keeps the budget its options give it.

**Each search keeps to its part where it decides where its data goes.**
The sliding level search reads it each time it makes a set and at each
level's eviction (`evict()`), so a smaller part sends its older levels to
its files at the next level: it searches the same arrangements, slower.
It is never paused or restarted for it, so no level's work is lost. The
Panex search reads it at each level: its frontiers must be in memory, so
when they outgrow a smaller part it stops at its limit, saved, and the
note says that other solves share the memory and Continue carries it on
once they have finished. The plain stacking search and the legacy slide
search (text keys) read it every 1024 arrangements and stop at it as they
did at a fixed limit.

**A search folder that cannot be written pauses the run before it
begins.** A sliding run that takes apart, and a run on the Panex search,
first make their folder and write a file in it (`folderWriteError`). When
that fails the run pauses where it stands (nothing searched, the results
as they were) and `solve.status.folderFailure` names the folder and what
was wrong. The Solver tab then asks: Choose Folder… (Settings' cache folder
choice, then Continue), Try Again (Continue as it is: the drive is back),
or Clear Solve. Escape leaves it paused for Continue. It asks after any
run's end, so a run the autosave carried on, or one of a problem not
shown, is asked about too.

## Consequences
- Two large sliding searches at once take as much memory as one did; each
  is slower while the other runs, from the disk it writes instead.
- A Panex search can stop sooner when another search starts beside it. It
  is saved, so nothing is lost; a plain stacking search (one the Panex
  search cannot hold) cannot be carried on, as before at its limit.
- Each window has its own engine and its own share: two windows each
  share the whole setting between their own searches.
- `bad_alloc` in a run already ended it in error rather than ending the
  engine; its message is now "The solver ran out of memory."
- The check is only at a run's start. A folder that goes away while a
  search runs ends as before (the search's write fails: the start is cut
  short, with its error in the note).

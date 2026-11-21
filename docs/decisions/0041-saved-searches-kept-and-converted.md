# 0041: Saved searches are kept when they cannot be read, and an earlier version's slide search is carried on

**Status:** accepted; `core/shapelogic/solve/slide_levels.h` (`kOlderSetMagic`,
`SetFault`, `KeySet::reload`, `reloadOlder`, `older`, `fault`, `faultError`,
`appendOlder`), `core/shapelogic/solve/sliding_search.cpp`
(`LevelSearch::start`, `resume`, `Resumed`, `convertOlder`),
`core/shapelogic/solve/sliding_search.h` (`SlideSearch::savedUnread`,
`convertedFiles`, `unconvertedFiles`),
`core/shapelogic/solve/solve_session.cpp` (`onSlidingAssembly`, `launch`,
`slidingSummary`, `runStacking`), `cli/main.cpp` (`slideSearch`, the lines
after a search that was cut, `cannotRestore`);
`core/shapelogic/solve/panex_search.cpp` (`Store::loadState`, `solve`;
`PanexSearch::savedUnread`), `core/shapelogic/io/paused_solve.cpp`
(`loadPausedSolve`), `core/shapelogic/solve/assembler.cpp` (`beginSolving`,
`errorMessage`)

## Context
Another user ran "Board 2 - Start / Goal" of the "1 to 8 Heart" puzzle
(nested slides, optimized paths) for days on an external drive: 97 + 97
moves deep, 58,834,035,828 arrangements visited, about 500 GB of files. The
solve was paused and carried on within the same minute, and the log said:

> The saved search in …/slide-3d7827785f568d1a could not be carried on:
> 001163.keys is not a whole level file (106498787990 bytes), one of the
> sets of what the start end has visited. It is deleted, and the search
> starts again from the beginning.

Two things were wrong.

- **The file was whole.** A set's index (the first key of each block of 128
  keys) was read back in one call, and macOS refuses a read of 2 GB or
  more. With the 128-bit keys of that search the index passes 2 GB at 17.2
  billion keys, and with nested slides the search merges everything an end
  has visited into a few sets: this one held a good part of the start end's
  29 billion. (The same puzzle searched for a few minutes keeps a visited set of
  28 million keys in 6.6 bytes a key; closer together they take less, so
  106 GB is well over 17.2 billion keys.) Every pause before it had been
  carried on, because the set had not grown that far. Decision 0040 reads
  in pieces of at most 1 GB, so that cause is gone.
- **The search was deleted for it.** Whatever could not be read, the search
  wiped its folder and began again, and the solve went on as if nothing had
  happened. A drive that drops out for a moment would have done the same,
  and so would decision 0040 itself: it changed the level files' format,
  and had a saved search of an earlier version deleted and searched afresh.

## Decision
**A saved search that cannot be carried on is kept, and nothing is
searched.** A saved search is days of work, and what cannot be read now may
be read later: the drive is back, the disk has room again, another version
of the program runs. `LevelSearch::start` no longer wipes the folder: the
search ends with the outcome Disk, `SlideSearch::savedUnread` set and the
reason in `error`, every file as it was found (`resume` lets go of the sets
it opened without deleting them). The folder is still cleared in one case:
it holds files and no `state.bin`, the leftovers of a search that was
killed before it ever saved, which nothing can carry on.

- **The solve pauses with the start waiting**, as for a search out of disk,
  and its note says why and what to do: *Continue tries to carry the saved
  search on again; Solve throws it away and searches from the beginning.*
  The crash recovery copy's save point does not carry such a solve on by
  itself.
- **Solve does throw it away.** A solve begun anew drops the problem's
  saved searches (`discardSaved`), but that reads each folder's state to
  tell whose it is, so a folder whose state cannot be read is in nobody's
  list and would stop every solve of its start. The searches of a solve
  begun anew therefore run with `resume` off, which clears the start's
  folder whatever is in it; only a solve carried on (Continue) carries
  saved searches on. The CLI's `--restart` does the same.
- **The reason is told as it is.** `KeySet::fault` says whether the system
  would not open or read the file (`Unread`, with the system's own error
  text from `faultError`: "Invalid argument" would have named the read of
  2 GB), whether it was read and is not a whole set (`Damaged`), or whether
  it is a later version's (`Newer`); a file that is gone is "missing".

**A search saved by an earlier version is carried on.** The versions up to
1.9 wrote a level file as its blocks, then the index (`BTSLKEY2`); the
state file and the name of a search's folder have not changed since, so
this version finds such a search as its own.

- **The files are read as they are.** `KeySet::reload` takes a file that
  ends `BTSLKEY2` as one whose chunks were all left as they are: the chunk
  tables are made from the block offsets, about 256 KB a chunk, and every
  reader works as on a file of this version (`older()` tells them apart).
- **They are written again on carry-on** (`convertOlder`), each under its
  own name: the blocks go into a new file beside it, in compressed chunks
  (`appendOlder`), which then takes the old one's name. The state names the
  same files throughout, so it is true at every moment, and a carry-on cut
  short by a pause, a crash or a full disk leaves a search that the next
  one goes on with: the files not yet written again are still read as they
  are, and a new file left unfinished is removed with the leftovers. The
  smallest files go first, for the room each gives back, several at once on
  the search's pool, at the compression level of the levels kept. A file
  that cannot be written again (no room) stays, and the log says so.
- **Nothing goes back.** Once written again, the files are not read by 1.9
  (file compatibility is for puzzle files, and one way).

**The files a state names stay until the next save, all of them.** A search
with nested slides that is carried on may merge a level into its visited
sets at once (`restoreVisited`), which lets the sets it merged go; `pin`
held only the sets still in use. It now holds every file the state names,
so a crash before the next save leaves the saved search whole.

**The same holds of every other solver's saved work.** Wherever a solve
found saved work it could not use, it threw it away; none does now.

- **The Panex search** (and the Stacking Solver, which runs it) cleared its
  folder when the state there did not load. Now `panex::solve` ends with
  the outcome Error, `PanexSearch::savedUnread` and the reason (the state
  could not be read, is cut short, is damaged, is another search's, or is a
  later version's), and the folder stays as it is. Levels with no state are
  still cleared: a search that never saved itself. A state of version 3 is
  read as before. The level files were never the danger: a missing one is
  searched again, and a damaged one ends the search with its folder kept.
- **A stacking solve that ends in an error with a saved search still in its
  folder pauses.** It used to finish: the problem was kept as solved with
  no solution, under a note that the search could not be traced back or
  saved. Now it is Paused as after a stop, whether the saved search could
  not be read or is the last one written before the error; Continue tries
  it again and Solve, which drops the folder by its name
  (`panex::discardSaved`), searches from the beginning.
- **Importing a paused solve** (`.slsolve`) emptied the problem's search
  folder first and unpacked into it, so a file cut short cost the saved
  search that was there. The file's search is now unpacked beside the
  folder (`<folder>.import`) and takes its place once the whole file is
  read; an import that fails removes only what it unpacked.
- **The brick solvers keep a paused solve in the puzzle** (the assembler's
  position and what was found so far). `beginSolving` dropped both when the
  position could not be restored (another version's, damaged, or a search
  that cannot be carried on from a file: decision 0009), as the original
  did, and the next Continue searched from the start. It now returns the
  error and leaves the problem as it was: the solutions found can still be
  looked at and saved, and the file can go back to the version that wrote
  it. Continue ends in the same error each time; Solve removes what the
  problem held before it searches (so nothing is reported twice) and starts
  from the beginning. The messages say so, and the CLI adds that
  `--restart` starts over.

## Measurements
Saved searches written by the 1.9 CLI (built from the tag), carried on by
this version (a debug build at low priority, beside another solve):

| search | stopped at | files | before | after | time |
|---|---|---|---|---|---|
| Climb Pro 24, both ends, levels thinned to every 8th | 86 + 44 moves | 21 | 971 MB | 538 MB | 76 s |
| 1 to 8 Heart, Board 2, nested slides, optimized paths | 52 + 51 moves | 108 | 1.33 GB | 741 MB | 119 s |

Both were found by name, written again and carried on, and the levels each
made next are those of the search without the conversion: the Climb Pro
levels have the counts of the same search run without a stop, and the next
four Heart levels have the arrangements and one-way moves that 1.9 itself
came to when it carried the same saved search on.

The search paused by 1.8.1 through its engine that the tests keep
(`tests/data/slide_paused_by_v1.8.1`) is carried on the same way, its two
files written again.

## Consequences
- A carry-on of a large search saved by 1.9 first takes the time to write
  its files again (the log tells how far it is every half minute), and the
  room for the largest file's compressed copy beside it.
- A saved search that cannot be read stays on the disk until the user
  chooses: nothing deletes it but Solve, an abort, or the user in View ▸
  Manage Cache and Autosave Files. The sweep that deleted sliding
  searches untouched for a month (`sweepSaved`, at the start of every
  sliding run, unasked) is gone: a search paused for a long time is the
  user's to keep, and the notice at each start lists it.
- A brick solve whose position cannot be restored no longer starts over by
  itself on the second Continue: the user presses Solve. This differs from
  the original on purpose.
- While a paused solve is imported, a folder `<name>.import` stands beside
  the search folder for a moment.
- The Panex search has no test of a write that fails (it has no gate like
  the slide search's); the pause after an error is tested with a damaged
  level instead.
- Tests: an earlier version's file read (every key, keys looked up, a
  cursor, the index let go and read back) and written again; a file of a
  later version, a damaged one and a missing one each told; a saved search
  with a level file cut short or gone, kept over two tries, then carried on
  with the file back or thrown away by a search that does not carry on; a
  folder with no state cleared; a state cut short kept; searches with their
  files as 1.9 wrote them (nested slides, levels kept, one end and both)
  carried on to the end, for one level, stopped before the files are
  written again, and with no room to write them; the session pausing with
  the saved search kept, Continue trying again, Solve throwing it away; a
  Panex state with a layer it cannot hold, cut short and of a later
  version, each kept over two tries by the search and by the solve, then
  thrown away by Solve; levels with no state cleared; a damaged Panex level
  pausing the solve; an import cut short leaving the saved search that was
  there; a brick position that cannot be restored kept with what was found,
  over two tries, then searched from the start by Solve.

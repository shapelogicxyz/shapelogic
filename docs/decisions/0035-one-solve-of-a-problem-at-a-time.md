# 0035: One solve of a problem at a time

**Status:** accepted; `core/shapelogic/base/file_lock.{h,cpp}` (`FileLock`),
`core/shapelogic/solve/sliding_search.{h,cpp}` and
`core/shapelogic/solve/panex_search.{h,cpp}` (`solveLockPath`),
`core/shapelogic/solve/solve_session.{h,cpp}` (`lockSearches`, `leave`,
`searchLock_`, `kSolvedElsewhere`), `cli/main.cpp` (`lockSearches`),
`engine/engine.cpp` (the default folders), `api/solve.json` (`io_error`)

## Context
A sliding search keeps its levels in a folder named by the search
(`slide-<key>` in the work folder), and a Panex search in one named by the
problem. The name is how a paused search is found again, and it is the
same for every solve of the same problem: in two windows of the app, in the
app and `shapelogic_cli`, in two runs of the CLI. Two such solves at once
worked in one folder. Each carried on what the other had saved, wrote over
its levels, and removed the folder when it was through; a Solve that
starts afresh deleted the other's saved searches first. The harness met it
as three text runs of one puzzle side by side: wrong answers, and once a
crash (signal 11).

## Decision
**A solve holds a lock on its problem's search folders from its start to
the end of its run.** The lock is a file in the work folder:
`solve-<problem key>.lock` for a sliding problem (the key that names the
problem's saved searches, so it covers the folders of all its starts),
`<folder>.lock` beside a Panex search's folder. It is taken only when the
solve has such folders: a sliding solve that searches, a stacking solve
the Panex search takes, with a work folder set. A solve with nothing on
disk shares nothing and takes no lock.

**The lock is the system's, not a marker file** (`FileLock`): `flock` on
the open file, on Windows a file opened with no sharing and removed on
close. The system lets it go when its holder ends in any way, so a crash
never leaves a problem locked. Two holders in one program are refused as
two programs are. The file is there only while the lock is held: the
holder removes it before letting go, and a lock taken on a file that was
just removed does not count (the file must still be the one its path
names).

**A second solve is refused, before anything is dropped.**
`SolveSession::start`, `prepare` and `resume` take the lock first and
answer `IoError` ("another solve of this problem is under way…") when
another holds it: the problem's results and saved searches stay as they
are. The run lets the lock go as it ends, before the state says it is over
(`leave`), so whoever sees a solve paused can start another at once. A
paused solve holds nothing, so of two paused solves of a problem either
may carry on, and the other is refused meanwhile. An abort of a solve that
is not running throws its saved searches away only when no other solve
works in them. The CLI's own solves (without `--save`: `-d`, `-x`) take
the same lock and end with the message and exit code 1.

**The same problem is the same lock, whatever file it is in.** The key
names the board, the marks, the pieces and their counts, as the saved
searches' does: a copy of the file, or a copy of the problem, would share
the folders, so it shares the lock. (In one document a second sliding
solve was refused already, while another reads the shapes.)

**The engine's default folders follow `SHAPELOGIC_SLIDE_DIR` and
`SHAPELOGIC_PANEX_DIR`**, as the CLI's do, before the user's cache folder.
The app sets its folders itself, so nothing changes there. The tests use
it: every contract engine, every run of the unit tests, the CLI's tests
and the web engine tests get folders of their own, where before they
shared the user's cache folder and left files in it. The harness gives
each sliding run its own folder the same way.

## Consequences
- `solve.start`, `solve.prepare` and `solve.continue` can fail with
  `io_error`; the app shows the engine's message in the status bar, as it
  does for other refusals.
- A lock file in a cache folder is not content: the app's scan of the
  folders (what they hold, moving them) leaves `*.lock` out.
- Two solves of small sliding puzzles that never reach the disk are
  refused as well when they share a work folder: the lock goes by the
  folders a solve may use, not by what it has written so far.
- A network drive without `flock` gives no lock (`Failed`): the solve
  goes on unlocked, as before this decision.
- Tests: `test_file_lock.cpp`; the sliding and stacking session tests hold
  the problem's lock and check the refusals, that nothing is dropped, and
  that an abort leaves the other solve's searches; the contract test
  `one_solve_of_a_problem_at_a_time` runs two engines on one folder.

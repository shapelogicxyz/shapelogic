# 0024: The rotation check on matrix rows; tasks that split while they are searched

**Status:** accepted (idea 5 of docs/V2-IDEAS-PLAN.md);
`core/shapelogic/solve/row_rotation_check.h`,
`core/shapelogic/solve/search_run.h` (`SplitTasks`),
`core/shapelogic/solve/subtree_split.h`,
`core/shapelogic/solve/resume_state.h`,
`core/shapelogic/base/save_cadence.h`,
`web/src/renderer/solver/autosave.ts`

## Context
Idea 5 of the BurrTools V2 comparison asked three things: SQLite as the
store, Knuth's dancing cells as a third assembler for problems with many
pieces of one shape, and splitting the search on demand for threads. Each
was to be measured before anything was built.

**Dancing cells.** V2's own solver (its `dancingcell.cpp`, and its dancing
links for comparison) was run on the placement matrices assembler 1
builds, for boxes filled with many pieces of one shape and with piece
ranges (`tests/bench/README.md`, "Idea 5"). Search against search, the
vector search of assembler 1 is faster than V2's dancing cells on every
one of them (1.5 to 3 times), and V2's dancing links is faster than its
dancing cells. What made V2 look fast on such puzzles was not the search:
of the 58 s ShapeLogic took for 32 dominoes in an 8 x 8 tray, 2 s were
the search and 56 s were spent on each of the 13 million covers found:
building an `Assembly`, turning it by each of the result's 16 symmetries
and sorting it each time (a bubble sort, 32 pieces of one shape). With
several pieces of a shape there is no symmetry breaker, so every cover is
found in every orientation and all but one are dropped by that check.

**Threads.** The split into tasks before the search starts keeps 8.5 to 9
of 10 cores busy on searches whose subtrees are alike, but a search with
one heavy subtree idles: Burr-Glar on four threads ran on 1.3 cores. More
tasks up front cure that and cost the balanced searches 5% (the split is
made by the plain search's choice of column, a weaker one than the vector
search makes). And a stop threw away what every thread had done in the
task it was in: a split search stopped more often than its tasks take
never finished. The autosave stops the assembly every 20 minutes, so a
solve long enough to have tasks of more than 20 minutes could not end with
autosave on.

## Decision
**No dancing cells assembler.** It fails the condition the plan set (a
measured win). The cost per cover is removed instead:

- `Assembly::sort` sorts the pieces of a part by insertion up to eight
  and with `std::sort` beyond. Equal placements cannot be told apart, so
  the order is the one the bubble sort gave.
- `RowRotationCheck` answers `RotationFilter::rejects()` from the rows of
  a cover without building the assembly. Per row and per symmetry of the
  result it holds what the row's placement becomes, as a number that
  orders as `Placement` does; the entry is worked out the first time it is
  asked for, by turning an assembly that holds only that piece with
  `Assembly::transform`, so the table says what the assembly's own check
  would compare. A cover and its turned copy are compared part by part,
  the pivot piece first, by the smallest number of each part (the first of
  the sorted pieces); only a tie looks further. A turned copy that needs a
  mirrored piece swapped for its mirror partner goes to
  `Assembly::smallerUnder`, the one-transformation body of
  `smallerRotationExists`. Only a cover that is kept is built. The check
  is not used for the complete rotation check, nor when the pivot piece
  is one of several of its shape. `SearchOptions::rowRotationCheck`
  (`--no-row-check`) turns it off: an A/B switch, the answers are the
  same, and the tests compare both ways on every fixture.

**Tasks split while they are searched** (`SplitTasks`, the run layer's
task list). A worker whose task still has branches it has not tried

- hands them on as new tasks when another worker has nothing to do and
  nothing is queued, and goes on with the branch in hand, which its task
  now is (the first level of the task that has such branches gives all of
  them: the largest pieces first);
- when the run is stopped, replaces its task by what is left of it, so
  that the next run searches exactly that.

Workers without a task wait for a hand-off; the run is through when
nothing is queued and nobody searches. Finished tasks are dropped from
the list at a stop (`SplitSearchState::prune`), their weight kept for the
progress.

What a task is stays what each assembler had, so the saved positions
keep their layout:

- Assembler 0: a prefix of (column, row) steps. The plain worker reads
  the untried rows from its stack; the vector search names its rows by
  their node in the piece's column. Both give prefixes either can search.
- Assembler 1, plain worker: at a stop its stacks take the task's place,
  with the number of frames the task started at (`base`); it stops where
  the stacks say everything (a frame not begun, or one waiting for the
  frame above it). It does not hand branches on.
- Assembler 1, vector search: a branch is its rows and the rows out of
  play in it (the search keeps the pieces of a shape in order by taking
  the earlier placements of that shape out of play), a task without
  frames, which only the vector search can take. A search with such
  tasks left runs the vector search whatever the options say (a search
  carries on the way it ran, decision 0009).

An assembly found after the run is stopped is not handed over
(`SearchRun::deliverOnce`); its node is left as a branch not begun, so the
next run finds it, and one handed over just before is passed over by its
signature.

**Saved positions.** Assembler 0's flag 2 text is unchanged but for more
and deeper tasks. Assembler 1 writes flag 5 (and 6 for a search with an
older version's signatures) when a task is not the stacks at a subtree's
root: each task then starts with its base. Both append ` W <dropped>
<weight>...` after the signatures, the tasks' weights for the progress; a
reader without it stops before it. Older ShapeLogic versions and the
original refuse flags 5 and 6 as a position they cannot read, and the
solve starts over. That narrows decision 0006's "a search paused in one
program can be continued in the other": every position the original and
older versions wrote is still read and carried on; the original need not
read what a split search of this version leaves.

**Storage.** `.xmpuzzle` stays the document and no database is added
(docs/V2-IDEAS-PLAN.md, 5a). The autosave's pause at a save point is the
crash safety; with tasks left where they stood it no longer costs the
work in hand.

**One thread, and no signatures where none are needed** (added with the
crash recovery journal below). The vector search on one thread was the one
search left that began again after a stop (it kept only how many assemblies
it had reported, flag 3). It is now a split search of one task, the whole
search, in both assemblers: stopped, it is left as its untried branches and
carries on from them. A flag 3 position of the original or an older version
still continues the old way. A stop that finds the one task untouched puts
the search back at its start.

With every task left exactly and no two tasks holding the same assembly,
nothing is found twice, and `SplitSearchState::exact` says so: such a search
hands assemblies over without looking them up in, or adding them to, the
set of signatures (`SearchRun::deliverUnlessStopped`), which for millions
of assemblies cost more than the search. Assembler 0's tasks are always so
(a search loaded with signatures in its file keeps to them); assembler 1's
only when the search is one task, because the tasks its plain search makes
overlap when the vector search takes them. An assembly not taken because
the run is stopped leaves its node as a branch not begun; one that was
taken does not, so it is not found again.

**The crash recovery journal** (in the place of the Solver's Autosave).
With a save point free of cost, the fixed 20 minutes went: the first save
comes a minute into a solve, each next one when the time since the last is
at least a minute and at least 50 times what the last save took (its pause,
the take-aparts it waited for, the write, the restart), and only when the
solve has moved (`web/src/renderer/solver/cadence.ts`; the stacking and
sliding searches' own saves keep to the same rule,
`core/shapelogic/base/save_cadence.h`).

It writes the rescue copy, never the puzzle's file, and it is not an
option. The autosave saved the document to its file, which put everything
unsaved there without being asked: the solve's partial results, and the
edits made before Solve was pressed, over a file that may have been opened
only to try something; in a synced folder it was an upload a minute, and a
position this version leaves the original cannot always read. Those were
the reasons to turn it off, and all of them come from where it wrote. The
rescue copy (stage 6) already holds a document with the file it belongs to
and reopens as that document with its unsaved changes; written at a save
point it holds the solve's position too, so the solve is continued after a
crash. With the user's file untouched nothing is gained by turning the
journal off: the Solver has no box for it, the settings no default, and the
original's stored Autosave box is read and written back and not looked at
(a stacking search always saves itself). What is given up: the copy is on
this machine, so a solve is no longer mirrored into a file that may be
backed up elsewhere unless the user saves it (File ▸ Save during a solve
pauses at a save point, saves, and carries on).

There is no journal file beside the document either: a position can only
be taken by pausing the search, so solutions appended between two pauses
would be found again on recovery, and an append-only file would be no more
than this, with a format of its own. Appending to the puzzle's own file is
out for another reason: its XML ends with the root element's close, so
anything appended is no longer a document, and writing in place gives up
the atomic save. The settings dialog has a Search Options page for what a
problem's options start at.

## Consequences
- Problems with many pieces of one shape solve 4 to 6 times faster on one
  thread (8 x 8 dominoes 58 s to 9 s) and Burr-Glar's 3.1 million
  assemblies a quarter faster; on several threads the idle cores are in
  use (Burr-Glar on four threads 17.8 s to 5.4 s).
- A paused solve carries on with no work lost, whatever the pause comes
  from; the tests stop a search every twentieth of its work and bound the
  work it does in all.
- `--split-tasks` and `--split-depth` (`SearchOptions::splitTasks`,
  `splitDepth`) set how finely the split is made up front, for
  benchmarks.
- A stop that comes while assembler 1 still makes its tasks has them made
  again by the next run, as before.

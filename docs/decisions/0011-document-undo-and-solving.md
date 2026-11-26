# 0011: The document: undo by saved parts, ids, and edits while solving

**Status:** accepted (stage 6); `core/shapelogic/app/document.h`,
`core/shapelogic/app/document_internal.h`

## Context
The front end edits one puzzle through commands (core-api.md, sections 1
to 4), each one undo step, grouped for a drag; it names shapes, colors,
problems and solutions by ids that survive reordering, and follows a
revision number. It also solves problems: decision 0010's `SolveSession`
holds its `Puzzle&` and `Problem&` for its whole life, resets hotspots as
a run starts, writes the problem while a run is active, and keeps a
paused search in memory (BurrTools 2's only there, decision 0009).

The original's undo (`shapeHistory_c`) snapshot the shapes (sharing the
unchanged ones) and the problems' definitions after every recorded
action; undo restored them and lost the solutions. Its GUI refused file
operations while the solve thread existed (`threadStopped`), grayed out
the editing of shapes and problems the running solve used, and removed
all solutions of a problem when it or a shape it used was edited
(`changeProblem`, `changeShape`). Its problems were heap objects the
puzzle pointed to, so adding a problem never moved the one being solved.

## Decision
**Undo by saved parts.** A step keeps, for each part of the document it
changed, that part as it was the first time the step changed it: a shape
(a copy of its voxel space), a problem (a copy, solutions and all), a
color's value, the notes, a shape added at the end (nothing until undone),
or the whole puzzle with the id lists (for changes to the lists of shapes,
colors and problems, which renumber everything). Undo swaps the saved
parts back in, last first, keeping what was there; redo swaps again,
first first: one operation both ways, exact by construction (every
command undone gives `writePuzzle` byte for byte, a test for each). Within
a step a part is saved once; after a whole-puzzle part nothing more is
needed. A group (`beginGroup` / `endGroup`) is one step; groups do not
nest. Steps beyond the undo depth go, oldest first. A command checks
what it is given before it changes anything, so a refused command
changes nothing; one that throws (a broken invariant) has its parts
swapped back.

**Ids.** Shapes, colors and problems have ids in lists beside the
puzzle's, kept in the whole-puzzle parts. A solution's id is a runtime
field of `Solution` that its `Problem` gives as the solution arrives (from
a file, or from a solve) and that stays with it through sorting and
thinning: the session adds and moves solutions on its own thread, so the
document could not follow them from outside. Ids are never reused within
a document (a problem).

**Edits while solving.** While any run is active the document refuses
(`InvalidState`) edits of shapes, of the shape, color and problem lists,
of the problems being solved, undo, redo and saving; the notes, a
color's value and the other problems can still be edited, and every
problem can be read (the solving one through `readProblem`). Before a
run starts the document resets the hotspots that a run would reset and
warms every shape's caches, so runs only read shapes and the document can
read them (meshes, the status table) meanwhile. Simpler than the
original's per-shape rule, and safe: a run's start reads every shape's
hotspot.

A problem can still be added at the end, new or a copy (of the problem
being solved too, its definition read through `readProblem`): the puzzle
keeps each problem in a place of its own (`ClonePtr`), so adding one
moves none a session holds, and the step saves no copy of the puzzle
(an appended-problem part, as for a shape added at the end) that would
read a problem while its run writes it. The sessions go on.

**Edits between runs** follow the original: an edit of a shape removes
the solutions of every problem that uses it (`changeShape`); an edit of a
problem's definition does what the original's did (the bulk range, the
color rules, removing all pieces and a new result remove its solutions;
a piece count leaves the problem Unknown, its solutions kept); either way
the problem's session ends and its paused search goes. A change of the
lists (and every undo or redo) ends every session, because the sessions
hold references into the puzzle (a problem added at the end does not):
a paused solve carries on from the position it wrote into its problem
when it paused, through a new session.
A paused BurrTools 2 search cannot (decision 0009): while one is paused,
such changes are refused until it is aborted or finished; a new shape at
the end does not need the sessions to end and goes on.

## Consequences
- Memory per step is what the step changed; a step that changes a list
  copies the puzzle (solutions included), the price of exactness. The
  undo depth bounds it.
- Solving does not record undo steps; aborting a solve cannot be undone
  (as in the original), and undoing an older step can bring back the
  solutions it saved. (Amended below: solving records a step.)
- The engine (stage 7) maps `InvalidState` to "pause first" in the UI.

## Amendment: solving records an undo step (decided 2026-11-24)
Solving records an undo step at each run's start (decided 2026-11-24 by
the user; built 2026-11-25, cleanup item 15.5). Without one, an undo after
a solve could throw its results away: the undo of an older step that held
the problem (a Solver option, any change of the lists) put the problem
back as it was before the solve and moved the solutions into the redo
stack, where the next edit dropped them; and the undo of an edit of a
shape whose problem was unsolved gave the old shape back under the
solutions found for the new one.

- A run that starts (Solve, Prepare, Continue) records "Solve P1", and a
  take-apart of saved solutions "Take apart P1" (the problem's label as it
  is then): a step that holds the problem as it was, solutions and all, as
  any step that touches a problem does, and the shapes whose hotspot the
  start reset. Undo is last in, first out, so it takes a run's results out
  before the edits made before the run, and gives `writePuzzle` byte for
  byte; redo brings them back. Undo while a run or a take-apart works is
  refused as before.
- While that step is on top of the undo stack, the problem's next run of
  the same kind records no other: the state to go back to is the one the
  step holds. A solve carried on run after run (the autosave) so copies
  its solutions once, and the undo of a solve that was paused and carried
  on takes all of it out. A solve does not only add solutions (Solve
  drops the old ones, the sorted orders insert, the limit thins), so the
  step keeps the whole problem, not a count to cut the list back to.
- A run that starts leaves nothing to redo, whether it recorded a step or
  not: a redone edit of a shape would stand beside this run's solutions.
- A launch that is refused records nothing.
- A problem swapped in by undo or redo counts its solution ids and its
  `solutionsVersion()` on from the one it replaces, so a solve undone and
  run again gives its solutions no id an undone one had.
- Still not undoable: aborting a solve, and what a search keeps in its
  folders (a sliding solve's levels, a Panex search). A paused solve
  undone and redone carries on from the position in its problem; a search
  whose folder a later Solve emptied starts over, as after any Solve.
- The document stays changed after the undo of a run (`solveDirty_`): a
  run with its step already on top records none, so the steps alone
  cannot say whether a file has what the document holds.

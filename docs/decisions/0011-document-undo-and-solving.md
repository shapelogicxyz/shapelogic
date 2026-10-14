# 0011: The document: undo by saved parts, ids, and edits while solving

**Status:** accepted (stage 6); `core/shapelogic/app/document.h`,
`core/shapelogic/app/document_internal.h`

## Context
The front end edits one puzzle through commands (core-api.md, sections 1
to 4), each one undo step, grouped for a drag; it names shapes, colours,
problems and solutions by ids that survive reordering, and follows a
revision number. It also solves problems: decision 0010's `SolveSession`
holds its `Puzzle&` and `Problem&` for its whole life, resets hotspots as
a run starts, writes the problem while a run is active, and keeps a
paused search in memory (BurrTools 2's only there, decision 0009).

The original's undo (`shapeHistory_c`) snapshot the shapes (sharing the
unchanged ones) and the problems' definitions after every recorded
action; undo restored them and lost the solutions. Its GUI refused file
operations while the solve thread existed (`threadStopped`), greyed out
the editing of shapes and problems the running solve used, and removed
all solutions of a problem when it or a shape it used was edited
(`changeProblem`, `changeShape`). Its problems were heap objects the
puzzle pointed to, so adding a problem never moved the one being solved.

## Decision
**Undo by saved parts.** A step keeps, for each part of the document it
changed, that part as it was the first time the step changed it: a shape
(a copy of its voxel space), a problem (a copy, solutions and all), a
colour's value, the notes, a shape added at the end (nothing until undone),
or the whole puzzle with the id lists (for changes to the lists of shapes,
colours and problems, which renumber everything). Undo swaps the saved
parts back in, last first, keeping what was there; redo swaps again,
first first: one operation both ways, exact by construction (every
command undone gives `writePuzzle` byte for byte, a test for each). Within
a step a part is saved once; after a whole-puzzle part nothing more is
needed. A group (`beginGroup` / `endGroup`) is one step; groups do not
nest. Steps beyond the undo depth go, oldest first. A command checks
what it is given before it changes anything, so a refused command
changes nothing; one that throws (a broken invariant) has its parts
swapped back.

**Ids.** Shapes, colours and problems have ids in lists beside the
puzzle's, kept in the whole-puzzle parts. A solution's id is a runtime
field of `Solution` that its `Problem` gives as the solution arrives (from
a file, or from a solve) and that stays with it through sorting and
thinning: the session adds and moves solutions on its own thread, so the
document could not follow them from outside. Ids are never reused within
a document (a problem).

**Edits while solving.** While any run is active the document refuses
(`InvalidState`) edits of shapes, of the shape, colour and problem lists,
of the problems being solved, undo, redo and saving; the notes, a
colour's value and the other problems can still be edited, and every
problem can be read (the solving one through `readProblem`). Before a
run starts the document resets the hotspots that a run would reset and
warms every shape's caches, so runs only read shapes and the document can
read them (meshes, the status table) meanwhile. Simpler than the
original's per-shape rule, and safe: a run's start reads every shape's
hotspot.

**Edits between runs** follow the original: an edit of a shape removes
the solutions of every problem that uses it (`changeShape`); an edit of a
problem's definition does what the original's did (the bulk range, the
colour rules, removing all pieces and a new result remove its solutions;
a piece count leaves the problem Unknown, its solutions kept); either way
the problem's session ends and its paused search goes. A change of the
lists (and every undo or redo) ends every session, because the sessions
hold references into the puzzle: a paused solve carries on from the
position it wrote into its problem when it paused, through a new session.
A paused BurrTools 2 search cannot (decision 0009): while one is paused,
such changes are refused until it is aborted or finished; a new shape at
the end does not need the sessions to end and goes on.

## Consequences
- Memory per step is what the step changed; a step that changes a list
  copies the puzzle (solutions included), the price of exactness. The
  undo depth bounds it.
- Solving does not record undo steps; aborting a solve cannot be undone
  (as in the original), and undoing an older step can bring back the
  solutions it saved.
- The engine (stage 7) maps `InvalidState` to "pause first" in the UI.

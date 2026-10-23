# 0014: Sliding puzzles in the core, the engine and the CLI

**Status:** accepted (deferred part 1); `core/shapelogic/model/sliding.h`,
`core/shapelogic/solve/sliding_search.h`, `core/shapelogic/solve/slide_levels.h`,
`core/shapelogic/app/document_sliding.cpp`, `engine/methods_sliding.cpp`,
`api/sliding.json`

## Context
The original's sliding puzzles (`src/lib/sliding.{h,cpp}`,
`slidelevels.h`, the sliding branches of `solvethread.cpp` and of the GUI)
were the newest part of ShapeLogic: a start/goal shape is a tray whose
empty cells are walls and whose variable cells are corridors; each cell's
colour is the start mark of a piece (its shape index + 1) and its goal
mark the goal of one; each start/goal shape has a problem of its own. The
solve thread's assembler found the start layouts (the pieces painted in
the colours of their starts), and inside its callback a breadth-first
slide search took each layout to the goal. The original read
`SHAPELOGIC_SLIDE_DIR` and `SHAPELOGIC_SLIDE_DISK_GB` from the environment,
traced on stderr, ran the full search on threads of its own and changed
the puzzle (start locks, the maximum of holes) from the GUI as a solve
started. Decisions 0004, 0010 and 0011 rule out all four as they were.

## Decision
**The model goes in `model/`, the search in `solve/`.** `bt::sliding`
(`model/sliding.h`) holds what a puzzle file holds and the GUI edited:
start/goal shapes and their user names, stamps, `syncSlidingProblems`,
the start locks, the checks (`startGoalError`, `stampOverflowMessage`),
the older goal-map model the solver still reads, and
`prepareForSolving` (the maximum of holes and the start locks).
`solve/sliding_search.h` holds the search (`findSlidePath`, the three
limits, the memory budget, nested and partially nested slides), the full
search's level files (`slide_levels.h`) and the routes the player follows
(`slideRoute`, `applySlideRoutes`). The file format did not change: what
the original saved, the model now reads instead of preserving it, and the
saves stay byte-identical.

**Sliding runs go through `SolveSession`** (decision 0010), not a side
path. `SolveOptions.sliding` (`SlidingOptions`: the solver, nested and
partially nested slides, high memory, the work folder and disk budget,
the A/B switches) carries what the solve thread's flags did. The session
forces what the solve thread forced (Classic; mirrors and rotations kept;
no complete rotations; strict colours), runs the slide search in the
assembler's callback on a pool of its own, puts a stopped search's start
back as pending (not counted), lets the search under way finish at a save
point, keeps the shorter path of two that end alike (`finalPlacementKey`),
and gives the original's summary as `note()`. The search polls only its
`StopToken` (decision 0004); the full search spreads its levels over a
`ThreadPool` and gives the same path on any number of threads.

**No environment and no printing in the core.** The work folder and the
disk budget are options: `bt` reads `SHAPELOGIC_SLIDE_DIR` and
`SHAPELOGIC_SLIDE_DISK_GB` (the cache folder's `sliding` by default, from
`base/system`'s `userCacheDirectory()`), the engine has the settings
`slideFolder` and `slideDiskGB`. The original's level trace is a callback
(`SlideSearch::onLevel`); `bt --slide-trace` prints it.

**The document keeps sliding puzzles as the GUI did** (decision 0011):
the start/goal commands (`addStartGoal`, `placeStamp`, `clearStampAt`)
are undoable steps; the shape commands keep the stamps and the problems
in step (a deleted or moved shape's marks renumbered, a stamped piece
moved past the 63rd place losing them with a notice, since a mark is six
bits, a duplicated or renamed start/goal shape keeping its number apart
from the user's name, an edit that changes a stamped piece's shape taking
its stamps off with a notice); what the GUI greyed out or hid is refused
(choosing the result, groups, the count of a piece a stamp requires, new,
copied and deleted problems, deleted colours: the cell colours are the
start marks); a sliding shape keeps its layers on open (see
"Shapes of more than one layer" below). Solve's setup
(`prepareForSolving`) is part of the launch, not an undo step, as it was
part of the original's solve: it runs after every check that can refuse,
so a refused launch changes nothing.

**The engine: a `sliding.*` namespace** (`api/sliding.json`), not more
`problem.*` methods: the start/goal editor works on a shape, the tray, not
on a problem, and its reads (`sliding.tray`: each cell's start and goal,
the validity bar's error, the overflow message, the floor;
`sliding.preview`: where a click would put a piece and why it does not
fit) are its own. Shape summaries give a start/goal shape's number
(`startGoal`) apart from its name, which is the user's part; parts say
when a stamp requires them; `document.changed` and the snapshot of a
document just opened carry the notices. All additive.

**`bt` solves sliding puzzles as burrTxt did**, with `--solver`
fast/deep/full, `--nested-slides`, `--partial-nested` and
`--high-memory`; `-x` and `--save` too (burrTxt2 refused `-d` on the
sliding grid, which has no take-apart; `bt --save -d` runs the solve
thread's sliding run, which is what solved such puzzles in the GUI).

**Deliberate differences**, each from a rule above or a defect of the
original:
1. With nested slides, a puzzle of more than 64 pieces uses the text-key
   search: the original's position tables overran their arrays there.
2. The full search's level threads are a `ThreadPool` job (one job saves
   the finished level, then expands), not threads of its own.
3. A start lock that is already right is not written again (writing it
   dropped the shape's caches; the answers are the same).
4. The original solver is nondeterministic on several threads when a
   puzzle has more than one start layout: which start's path is kept for
   an ending depends on the order the starts arrive in. The new core is
   faithful to that; the harness compares such puzzles on one thread (the
   corpus flag `O`).

**Shapes of more than one layer** (added after the port). The original
locked a sliding shape to one layer (the GUI's `lockZ`, `flattenPieces`
on load), though its search already took a tray of several layers with
each piece kept on its own layer. Sliding shapes now keep their layers:
nothing is flattened on open, the Z size and the tasks that add layers
are offered, and a new shape or start/goal shape is still one layer to
begin with. A stamp marks every cell of its piece, in every layer. A
click on a layer places the piece by what that layer shows: its slice
there with the slice's top-left cell under the mouse, at the lowest
height that puts a layer of the piece there and fits (`stampPiece` and
the `sliding.*` methods take [x, y, z]; [x, y] is layer 0; on a tray of
one layer this is the original's rule). On a tray of
several layers a move may also go up and down (six directions, not
four), so a piece can be lifted out of a box over the others and put
down elsewhere; routes and the player follow it in z. On a tray one
layer deep a step up or down never fits, so every flat puzzle is
searched, and answered, as the original searched it; the corpus's two
trays of two layers hold a piece as tall as the tray, so their answers
stay too. Pieces still only translate: a puzzle whose goal needs a piece
turned has no sliding path.

## Consequences
- The oracle harness has a `sliding` tier: the fixtures and puzzles built
  with the original's code from its `test_sliding.cpp`
  (`oracle.py sliding-puzzles`), compared for each solver in the summary,
  the solved files and the text; the routes of the original's solutions
  are recorded (`oracle.py record-slide-routes`) for the playback.
- The front end has the start/goal editor (the deferred plan's part 2,
  docs/webui-spec.md section 8): New offers the sliding grid, the trays
  are edited, stamped, solved with the sliding options and played with
  their walls.
- Stacking puzzles stay unsupported (`unsupported`); their solver is the
  plan's part 3, under the same rules (decision 0015: the core and `bt` in
  part 3a, the engine in part 3b).

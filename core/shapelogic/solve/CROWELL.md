# Andrew Crowell's take-apart: notes for maintainers

Read this before changing `rotation_moves_crowell.*`, the Crowell parts of
`disassembler.*`, or how `SolverType::Crowell` is wired. Complete (ShapeLogic)
(`RotationMovesClassic`, the Classic `Disassembler`) must stay a complete
search; Crowell's heuristics go in the Crowell files only.

There are two solver types: Complete (ShapeLogic) and Fast (Crowell).

Reference implementation: the Fortran `Rotationfile.f90` in the original
repository's `tictest/fortran/` (PieceBuilder, SimpleRot, CheckAvailMoves,
SolverRotate, SolverFull, FreePieceTestFull, MinRotSol).

Ported from `src/lib/crowell_solver.h` of the original (notes only, no code),
with the names of this codebase. The GUI and CLI references are the
original's; in ShapeLogic the CLI is `bt --solver crowell -R`.

## How the engine is selected

GUI: Solver tab → "Solver Type" dropdown (Crowell), above Sort by.
     mainwindow.cpp CreateSolveTab / cb_BtnCont / Add Disasm.
CLI: ./build/burrTxt -dRr --solver "Fast (Crowell)" <file>
     also: --solver crowell
Factory: createDisassembler(problem, checkRotations, SolverType::Crowell)
         → new Disassembler (SolverType::Crowell)
         which constructs Disassembler(..., SolverType::Crowell)
         which constructs MovementAnalysator(..., SolverType::Crowell)
         which new RotationMovesCrowell instead of RotationMovesClassic.

Assembly (DLX) is assembler_0_c / assembler_1_c, same as Classic.
Only take-apart / 90° generation differs.
Check Rotations must still be on or no 90° moves are generated.

## Feature status

For each item: IMPLEMENTED (how) or NOT IMPLEMENTED (what to do). File names in items 1 to 22 are the original\'s where
they describe its history.

### 1. Isolate Crowell from Complete (ShapeLogic)
IMPLEMENTED.
New files only: rotationmoves_crowell.{h,cpp}, disassembler_crowell.{h,cpp},
disassembler_factory.{h,cpp}, solvertype.h, this file.
Classic constructors default to SOLVER_CLASSIC. Do not add Crowell caps or
subset skips inside RotationMovesClassic or Disassembler (Classic).

### 2. Skip largest remaining piece (Fortran PieceBuilder cage)
IMPLEMENTED in RotationMovesCrowell::initFind / nextSubsetMask.
Among non-removed pieces in the current subproblem, count filled voxels
(countFilled). The max count wins; ties keep the highest index.
nextSubsetMask rejects any bitmask that has that piece's bit set.
Result: all non-empty subsets of the other n-1 pieces (2^(n-1)-1 groups).
That matches PieceBuilder for P pieces: singles of the P-1 smaller pieces,
the (P-1)-group, and k-subsets of those P-1 (k=2..P-2).
To change tie-break to Fortran SORT-by-size then Iorder(P): sort remaining
indices by voxel count ascending and drop Iorder[n-1], not "last max index".

### 3. Sparse pivots (Fortran SimpleRot CxVec / CyVec, Rmult=1)
IMPLEMENTED in rebuildPivotCells.
For in-plane bbox [umin,umax] x [vmin,vmax], emit:
  voxel centres (2u, 2v), SW corners (2u-1, 2v-1),
  max-v edge (2u-1, 2*vmax+1), max-u edge (2*umax+1, 2v-1),
  far corner (2*umax+1, 2*vmax+1).
Axis coordinate of the pivot is 2*amin (min axial coord of moving cells).
Classic instead walks every half-index from 2*umin-1 to 2*umax+1.
To match Rmult>1 (large-puzzle thinning): step u,v by Rmult and add the
extra halfway boundary points from SimpleRot (Rotationfile.f90 ~1590).
Read Rmult from puzzle input or a constant; Fortran PuzzleInp default is 1.

### 4. Axis-level prune before pivots
IMPLEMENTED in startAxis via RotationRules::axisBlocked.
Once per (subset, axis): sandwich-start + perp-plane on cached start/occupied.
If blocked, skip every pivot/sense on that axis (Fortran SimpleRot Rfail=1
then CYCLE to next D1).
Sandwich-end, axis-cross, and arc-sweep still run per candidate in
allowRotation (they need the end pose or the pivot).
To prune more: call axis-cross without a pivot if you define a pivot-free
variant, or fail the axis when sandwich-end would fail for all pivots
(not generally true).

### 5. DctMax = 3 accepted rotations per direction
IMPLEMENTED. RotationMovesCrowell::kDctMax in rotation_moves_crowell.h; dirCounts[axis*2+sense]
incremented on each successful tryCurrentCandidate; skipCurrentDirection
skips that direction once count >= 3.
Fortran: DctMax = 3 "different rotations in the same direction" inside
SimpleRot's pivot loop (EXIT when Dct >= Dctmax).
Directions here are 6 values: ±90° about X/Y/Z, not Fortran's 1..6 mapping
but the same idea. To match Fortran numbering exactly, map
D1=Z/Y/X and D2=CCW/CW onto DisassemblerNode::kRotationDirBase + axis*2 + sense; no
search-behavior change needed unless you also skip "same pieces" counts
the way TrackCurr stores piece ids.

### 6. Nkeep = 100 successors per BFS node
IMPLEMENTED in Disassembler::searchLevels (kCrowellMaxSuccessors) (Disassembler::kCrowellMaxSuccessors).
After 100 *new* non-separation neighbours are seen, stop calling find()
for that node. Duplicates do not count. A separation still returns
immediately if found before the cap.
Fortran CheckAvailMoves Nkeep=100 is the total PossMoves slot count for
linear+rotation together; overflowing STOPs the program. We silently
drop extras. To match overflow: assert or log when added > 100.
Linear moves are generated first (movementAnalysator states 0-2), then
rotations (state 3), so the cap usually hits during rotations.

### 7. Skip reverse of last rotation
IMPLEMENTED in skipCurrentDirection.
If searchnode arrived by a 90° edge, skip the opposite sense on the same
axis when the current subset includes getRotPiece().
Fortran TrackCurr(1)<0 compares piece id lists, not only primary rotPiece.
To match: store the full subset mask (or piece ids) on the node when a
rotation is applied, and skip reverse only when the moving set is equal.
Linear reverse skip (Fortran SimpleMove TrackCurr for slides) is NOT done;
Cutler find() does not consult the previous slide direction. To add it:
in MovementAnalysator::find, skip nextdir when it is the opposite of
searchnode->getDirection() for the same pieces — only under SolverType::Crowell
so Classic stays complete. Prefer a fortran-only hook rather than editing
the Cutler state machine if you can filter after newNode().

### 8. Geometric rotation legality (bevel arc, sandwich, perp, axis-cross)
IMPLEMENTED by calling the shared RotationRules::allowRotation after a
candidate pose is built. That is the same physics Classic uses:
  end-overlap, sandwich start, sandwich end, perp-plane, axis-cross,
  24-step beveled arc (NrotSteps), then 3×8 mid-turn wiggles.
Fortran SimpleRot inlines equivalent tests (in-plane, PerPplane 1/2, 90°
SAT, then stepped arc). Do not fork a second geometry copy unless you are
matching a Fortran discrepancy; change rotation_rules.cpp so both engines
stay consistent.
NOT separately tunable: NrotSteps, remove=0.0405, HALFTHICK, wiggle
amounts live in rotation_rules.cpp (ARC_STEPS, BEVEL_REMOVE). To expose
Fortran PuzzleInp "Remove for Bevel" / NrotSteps: add parameters on
RotationRules and pass them from RotationMovesCrowell only.

### 9. Linear / sliding moves
NOT a Fortran port. MovementAnalysator still uses Bill Cutler matrices
(prepare / checkmovement) for both solver types.
Fortran SimpleMove walks each cluster from PieceBuilder along 6 directions
with a linear voxel multiplier `delta`.
To port SimpleMove: add a fortran-only slide generator (new file) and
call it from MovementAnalysator::find when solverType==SolverType::Crowell,
either instead of or before Cutler. You would need per-piece world cells
(already in rotationMoves_fortran collectWorldCells) and collision with
the packed occupancy. Keep Cutler as Classic.

### 10. First-piece vs full take-apart (Fortran Fast=1 / SolverFull)
PARTIAL. Fortran SolverRotate Fast=1 stops when the first piece is free,
then SolverFull loops SolverRotate on the remainder until Pleft pieces
remain (default 1).
Our searchLevels returns at the first isSeparation() node, then
Disassembler::checkSubproblems recursively disassembles both parts.
That is the same overall "piece by piece until one remains" structure,
but the split is Cutler's separation test (piece translated far), not
Fortran FreePieceTestFull AABB isolation.
To match Fast/Pleft exactly: stop recursion when pieces.size() <= Pleft,
and treat AABB-disjoint groups as separations (see feature 11).

### 11. AABB cluster split (FreePieceTestFull 2/3/4 piece clusters)
NOT IMPLEMENTED.
When pieces have already moved apart (mcheck: bbox exceeds ss+1), Fortran
finds a 2-, 3-, or 4-piece subset whose combined AABB does not overlap the
rest, recursively SolverSetup's each side with Tlimit=2, and either Kill-
injects that path or replaces the move list with PieceBuilder on one side.
This is a major pruning / shortcut and is not in Disassembler (SolverType::Crowell).
To implement: after each new node (or when generating moves), compute per-
piece world AABBs. If a subset of 2..4 pieces has no AABB overlap with the
complement, treat it as a separation and call checkSubproblems (or run a
nested searchLevels on each side). Need a grid size `ss` (Fortran puzzle
bounding cube). Watch recursion: Fortran uses a 2s timeout on the nested
solve; without that, nested cluster solves can explode.

### 12. MinRotSol (second search minimizing rotation count)
NOT IMPLEMENTED.
Fortran main program after SolverFull calls MinRotSol which re-searches
with AllowRots toggled (0 or 2) to find a path with fewer rotations.
To implement: after a successful disassemble(), run a second BFS that
prefers linear edges (higher cost on rotation edges, or disallow
rotations until linear fails). Store the better Separation. Do this
only in Disassembler (SolverType::Crowell) so Classic still returns the first
shortest combined path.

### 13. OrientationCheck
NOT IMPLEMENTED.
Fortran, if CheckO>0, enumerates orientations of the whole assembly and
solves each. ShapeLogic already enumerates assemblies via DLX; different
placements are different assemblies. Do not port OrientationCheck unless
you need Fortran's specific orientation generator (modulefile.f90
OrientationCheck) for puzzles supplied as a single grid, not .xmpuzzle.

### 14. Time limit (Tlimit)
NOT IMPLEMENTED.
Fortran SolverRotate aborts with Unsolveable=2 after Tlimit seconds
(PuzzleInp / SlidingCubePuzzles Tlimit=300).
To implement: in searchLevels, if aborted() is not enough, compare
steady_clock against a limit stored on Disassembler (SolverType::Crowell). Wire a
GUI/CLI timeout later; until then Classic/Fortran both rely on the
existing abort() from the solve thread Stop button.

### 15. State canonicalization (subtract min xyz)
NOT IMPLEMENTED.
Fortran shifts every state so min x,y,z is 0 before hashing into States.
Our nodeHash uses absolute piece coordinates from disassemblerNode_c.
Translation-identical poses can be stored twice, inflating the BFS.
To implement: before insert/contains, subtract min hotspot (or min voxel)
from all piece positions on a copy used only for hashing/equality, OR
add a canonical hash in disassemblerNode_c used only by the Fortran
disassembler. Changing node equality globally would affect Classic.

### 16. Unbounded BFS memory (MaxStates / LvlMax)
NOT IMPLEMENTED as a cap.
Fortran grows States from 500 by +500; LvlMax starts at 100.
Our queues grow without a hard cap (Nkeep only limits branching).
To implement: if openlist[new] size or closed entries exceed a constant,
stop expanding (treat as unsolvable) or drop the oldest front earlier.

### 17. Do not rotate a lone unit cube
IMPLEMENTED. startAxis skips subsets with cachedStart.size() <= 1, same
idea as Classic tryCurrentCandidate and Fortran PID voxel count == 1.

### 18. Cache moving/static cells per subset
IMPLEMENTED. loadSubsetCells fills cachedStart / cachedOccupied once per
bitmask so axis prune and every pivot share the same vectors. Classic
rebuilds cells inside every tryCurrentCandidate. Safe to extend with a
voxel-count cache for countFilled.

### 19. Compound (multi-piece) rotations
IMPLEMENTED as rigid rotation of each allowed subset (all bits except
the largest piece). Fortran SimpleRot also rotates a cluster passed in
as Pieces(1:NumP). Same idea. We do not require the pieces to be
face-connected; Fortran's cluster is whatever PieceBuilder listed.
To require connectivity: when building a subset, flood-fill moving cells
and skip if more than one component.

### 20. Plots / Saved/ / gnuplot / MakePlot
NOT IMPLEMENTED. Fortran writes Saved/ and calls DrawStart/DrawMoves.
ShapeLogic already animates via Separation / DisasmToMoves. No port.

### 21. LockedN / timeout heuristic unsolvable
NOT IMPLEMENTED. Commented-out block in SolverRotate around time-5s.
Ignore unless Magellan-class puzzles hang; then port LockedN from
modulefile.f90 as an optional unsolvable detector.

### 22. Linear-first, return on separation
IMPLEMENTED via existing MovementAnalysator::find order (removes, then
group removes, then 1-step slides, then rotations) plus searchLevels
returning on isSeparation(). Fortran CheckAvailMoves: SimpleMove all
clusters first, return if Solved; then SimpleRot. Equivalent ordering.

## Files to edit when extending Crowell (do not touch Complete (ShapeLogic))


rotation_moves_crowell.cpp  — subsets, pivots, DctMax, reverse skip, axis prune
disassembler.cpp   — Nkeep, BFS policy, AABB split, MinRotSol, timeout
rotation_rules.cpp          — shared geometry; changing this affects Classic too
movement_analysator.cpp     — only the `if (solverType == SolverType::Crowell)`
                             construction of RotationMovesCrowell; do not
                             put Fortran caps in the Cutler state machine

Constants: RotationMovesCrowell::kDctMax (rotation_moves_crowell.h), Disassembler::kCrowellMaxSuccessors
(disassembler.h). Fortran sources: DctMax=3, Nkeep=100, NrotSteps=24,
remove=0.0405, Fast=1, Pleft=1, Rmult=1, delta=1.0, Tlimit=300.

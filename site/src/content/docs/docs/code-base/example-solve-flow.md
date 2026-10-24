---
title: Example Solve Flow
description: "What the code does, function by function, when you press Solve on a Brick puzzle in the Solver tab, from the button in the web page to the search threads and back."
---

This page follows one press of **Solve** through every layer of the code.
The setting is the simplest and most common one: a Brick puzzle (the cube
grid) is open, the Solver tab shows a problem with a result shape and some
pieces, and the options are the defaults a new problem gets: **Assemble
Only**, the **Complete (ShapeLogic)** solver, **Strict Color Restrictions**
on, no limit changes. A section at the end covers what **Assemble and
Disassemble** adds.

![The Run section: Solve, Pause, Continue and Clear, the progress bar, and the counters.](../../../../assets/screens/guide/solver-run.png)

Three processes are involved. The **renderer** is the sandboxed web page
in the window. The **main process** is Electron's Node side, which owns the
engine. The **engine** is `shapelogic_engine`, the C++ process that holds the
puzzle and runs the search. The whole trip is:

```
renderer           main process        engine (main thread)        engine (run thread)
click Solve
SolverWorkspace.run
EngineProxy.call ─▶ serveEngine ─▶ Engine::handleLine
                                   solveStart (methods_solve.cpp)
                                   Document::solveStart
                                   SolveSession::start ──────────▶ SolveSession::run
                 ◀─ reply {revision}                                 prepareSearch
                 ◀─ document.changed                                   makeAssembler, createMatrix, reduce
Store reloads the snapshot                                           assembleAndTakeApart
                                   Engine::poll (every 50 ms)          Assembler0::assemble
                 ◀─ solve.state, solve.progress,                         onAssembly → Problem::addSolution
                    solve.solutionsAdded                               endRun → Finished
Solves, RunCard, SolutionPager     Document::poll takes the end in
                 ◀─ solve.state finished, document.changed
the list fills; the run is over
```

## 1. The click, in the renderer

The Run section of the Solver's left card is drawn by `RunCard` in
`web/src/renderer/solver/run-card.ts`. Its `renderRun()` writes the four
buttons as HTML, each with a `data-r` attribute (`start`, `pause`,
`continue`, `abort`), and decides which are enabled: **Solve** is on when
the problem has a result and nothing is already calling, running or
analysing.

`RunCard.wire()` listens for clicks on the section and reads the button's
`data-r`. For `start` it calls `ws.run('start')` on the `SolverWorkspace`.

## 2. `SolverWorkspace.run('start')`

`web/src/renderer/solver/workspace.ts`. The method:

1. Takes the problem shown (`this.current`, an id such as `p1`) and returns
   at once if a call, a run or an analysis is already under way.
2. Builds the options with `optionsNow()`, which calls `solveOptions()` in
   `solver/settings.ts`. That function maps the card's check boxes to the
   API's `SolveOptions`: `solver`, `disassemble`, `checkRotations`,
   `justCount`, `justLevels`, `keepMirrors`, `keepRotations`,
   `completeRotations` (the card's Deep Symmetry Check), `strictColours`,
   `sortBy` (made `unsorted` when there is no disassembly to sort by),
   `keepAtMost` and `keepEach`. The thread count is left to the engine's
   settings.
3. Sets `this.calling`, drops the selected solution and tells the player to
   show nothing (`player.show(null, null)`): a Solve throws the old
   solutions away, so nothing of them is read from now on. Re-renders the
   card, which greys the buttons.
4. Calls the engine:

   ```ts
   await this.edits.track(
     this.store.engine.call('solve.start', { problem, options }).then((r) => {
       this.solves.launch(problem, solver);   // before the run's events: the reply comes first
       this.launched.set(problem, options);
       return r;
     }),
   );
   ```

   `edits.track` counts the call as an edit under way (the stage shows
   `data-busy`). An error is shown by `reportError`; `finally` clears
   `calling` and renders again.

## 3. Across the process boundary

`store.engine` is an `EngineProxy` (`web/src/renderer/engine-proxy.ts`).
Its `call()` picks the next id, keeps the promise's `resolve` and `reject`
in a map, and sends `{kind: 'call', id, method: 'solve.start', params}` to
`window.bt`. The preload script (`web/src/preload/index.ts`) forwards that
over Electron IPC as `ipcRenderer.send('bt:call', request)`.

In the main process `web/src/main/index.ts` has registered
`ipcMain.on('bt:call', ...)`, which hands the message to the window's
`serveEngine` (`web/src/main/engine-ipc.ts`). It checks that the method is
one of the schema's (`METHOD_NAMES` from `api.generated.ts`), lets
`admit` refuse a call naming a file the window was never given (not the
case here), and calls `host.call(method, params)`.

The `EngineHost` (`engine-host.ts`) owns this window's `EngineClient`
(`web/src/engine/client.ts`). `EngineClient.call` queues the call in order
and writes one line to the child process's stdin through `NodeTransport`:

```json
{"jsonrpc":"2.0","id":12,"method":"solve.start","params":{"problem":"p1","options":{"solver":"classic","disassemble":false,"checkRotations":false,"justCount":false,"justLevels":false,"keepMirrors":false,"keepRotations":false,"completeRotations":false,"strictColours":true,"sortBy":"unsorted","keepAtMost":100,"keepEach":1}}}
```

## 4. The engine runs the request

`engine/engine.cpp`. A reader thread puts each line of stdin into a bounded
queue. The main thread's loop in `Engine::run` pops the line and calls
`Engine::handleLine`, which parses it with nlohmann/json, checks the
envelope (`readRequest`), looks the method up in the method table and
calls it: `solveStart` in `engine/methods_solve.cpp`.

`solveStart` is a one-liner over `launch(s, p, Launch::Start)`:

1. `p.id("problem", IdKind::Problem)` reads `p1`; `p.value("options")`
   takes the options; `p.finish()` refuses any unknown field.
2. `solveOptionsFrom(s, given)` (`engine/convert.cpp`) turns the API's
   names into the core's `bt::SolveOptions`: `strictColours` becomes
   `matrix.strictColors`, `sortBy` becomes `order`, `keepAtMost` becomes
   `solutionLimit`, and `threads` starts from the engine's settings (the
   Settings dialog's worker threads).
3. `d.problemIndex(problem)` checks the problem exists;
   `defaultStackingSolver` does nothing on a Brick puzzle.
4. `d.solveStart(problem, options)` on the `Document`.
5. `s.watches[problem].launched = true`, so the next poll tells the run's
   state even if the run is already over by then.
6. Returns `s.changed()`, which is `{"revision": N}`.

## 5. `Document::solveStart`, in the core

`core/shapelogic/app/document.cpp`. `solveStart` calls `solveLaunch(problem,
options, LaunchMode::Start)`:

1. `problemIndex` again, then `checkProblemFree`: the problem is not being
   solved or analysed already.
2. Refused if a group of edits is open (a drag in progress).
3. The sliding-puzzle checks are skipped: this is a Brick puzzle.
4. If nothing else is solving, `prepareShapesForSolving()`: every shape's
   hotspot is put at its initial place and `warmCaches()` computes the
   symmetry group and the per-rotation bounding boxes, so that search
   threads only read the shapes from now on.
5. `sessionFor(index, options)` finds or makes the problem's
   `SolveSession` (one per problem, kept in `sessions_`), then
   `s.setOptions(options)` and `s.start()`.
6. On success: `solveDirty_ = true` (the document has unsaved changes),
   the problem goes into `runsStarted_` (so `poll()` will notice the
   run's end), and `lastChange_` becomes `{solutions, problems, [problem]}`
   with `changed()` bumping the revision.

The `Document` is called from the engine's main thread only. From here the
run belongs to the session.

## 6. `SolveSession::start`, the launch

`core/shapelogic/solve/solve_session.cpp`. `start()` is `launch(Mode::Start)`
under the session's control mutex:

1. Refused if a run is active. The previous run's thread, if any, is
   joined.
2. The original GUI's checks: the grid has an assembler
   (`kCapAssemble`), a result shape is defined, the problem has pieces.
   With disassembly on, the grid must have a disassembler (cubes and
   prisms).
3. Start mode throws the past away: `problem_.removeAllSolutions()` under
   the results mutex, `dropAssembler()`. A `Finished` or `Error` state goes
   back to `Idle`.
4. `canStartSolving(problem_)` (`solve/assembler.cpp`) confirms the problem
   is in a state a fresh solve can begin from.
5. A `Plan` is made: the options, `prepare = true` (no assembler is kept
   from a pause), `prepareOnly = false`.
6. A new `StopSource` for this run (`runStop_`), with a child
   `assemblyStop_` for the assembly search alone (what an autosave pause
   stops). Nothing is ever reset: a stop asked for before the search
   starts is still seen when it starts.
7. `action_` is set from the options: `SaveAssemblies` here (Assemble
   Only, not Just Count). With disassembly it would be `SaveSolutions`.
8. Every counter and timer of the run is reset.
9. **The state moves to `Preparing` before the thread exists**, so that the
   caller sees an active state as soon as `start()` returns. Then
   `runner_ = std::thread([this, plan] { run(plan); })`.

`start()` returns a success `Result`, which travels back up through
`Document::solveLaunch` and `launch` in the engine.

## 7. The reply and the first event

Back in `Engine::handleLine`, the reply is written to stdout, then
`state_.flushEvents()` and `announceChange(state_)` (`engine/events.cpp`)
send the `document.changed` for the revision the launch made:

```json
{"jsonrpc":"2.0","id":12,"result":{"revision":42}}
{"jsonrpc":"2.0","method":"document.changed","params":{"revision":42,"what":["problems","solutions"],"shapes":[],"problems":["p1"]}}
```

In the main process `FrameParser` (`web/src/engine/framing.ts`) splits
stdout into lines; `EngineClient` matches the reply to call 12 by its id
and settles it before the event behind it is dispatched (`OrderedInbox`,
`dispatch.ts`). `serveEngine` posts `{kind: 'reply'}` then
`{kind: 'event'}` to the window on the `bt:message` channel, and
`EngineProxy.deliver` resolves the promise from step 2 and then calls the
`document.changed` handlers.

Two things happen in the renderer:

- The `.then` in `SolverWorkspace.run` calls `solves.launch(problem,
  'classic')`: the problem's `SolveRecord` in `state/solves.ts` is marked
  `launched`, so the workspace knows a run is coming even before its first
  `solve.state`.
- `Store` (`state/store.ts`) answers `document.changed` by reloading the
  snapshot with `document.get`. The new snapshot says `solving: true`,
  and every workspace freezes: the shapes, the lists and the problem being
  solved refuse edits with "Pause the solve first" before any call.

## 8. The run thread: preparing the search

Meanwhile, in the engine process, the run thread is in
`SolveSession::run(plan)`:

1. Each shape's hotspot is reset to its initial place (the matrix places
   pieces by their hotspots).
2. `prepareSearch(plan)`:
   - `makeAssembler(puzzle_, problem_, o.solver)` in
     `solve/solver_type.cpp` chooses the assembler through
     `chooseAssembler`: `Assembler0::canHandle(problem)` is true when
     every part is exactly one piece, which is the common Brick case, so
     an `Assembler0` is made (Knuth's dancing links over a `DlxMatrix`).
     A problem with piece counts or ranges gets `Assembler1` (a cover
     with counts). The solver type, Classic or Crowell, does not change
     the assembler; it changes the take-apart.
   - `publishAssembler(a.get())` so that `progress()` can read it from
     other threads while it is built.
   - `a->createMatrix(o.matrix)`: for every piece and every orientation of
     its symmetry group, `PlacementFinder` lists the positions where the
     piece fits inside the result; each becomes a row of the matrix, the
     columns being the pieces and the result's cells (variable cells may
     stay empty, up to the problem's holes). With strict colours on, a
     coloured cell only fits a result cell of the same colour. A piece that
     fits nowhere is an `AssemblerError`; the run then ends in `Error`
     with a `SolveFailure` carrying the original's message.
   - `a->reduce(runToken())`, unless `reduce` is off: placements that can
     never be part of an assembly are removed. It polls the run's token.
   - `beginSolving(problem_, *a)` (`solve/assembler.cpp`) marks the problem
     as `Solving`. On a Continue it would restore the saved position; here
     there is none.
   - The assembler is kept as `assembler_` for the rest of the solve.
3. `assembleAndTakeApart(plan)`.

## 9. The run thread: the assembly search

`assembleAndTakeApart`:

1. The state moves from `Preparing` to `Assembling`. The problem's saved
   position is cleared (the running assembler is the position now).
2. Threads: the budget is `options.threads`, or every hardware thread when
   it is 0. A `ThreadPool assemblyPool(assemblyThreads)` is made and put
   into `SearchOptions` along with the SIMD switches.
3. Nothing is pending from an earlier pause, so the search starts from
   scratch:

   ```cpp
   end = assembler_->assemble([this](Assembly a) { return onAssembly(std::move(a)); },
                              search, plan.assemblyToken);
   ```

   `Assembler0::assemble` runs on the shared run layer (`solve/search_run.h`,
   decision 0009): a `SearchRun` makes its own stop source as a child of
   the token it was given and polls only that. Depending on the matrix and
   the thread count it runs the plain dancing-links search on the calling
   thread, splits the search tree into subtree tasks over the pool's
   threads, or uses the vector search (`SimdExactCover`, with AVX2,
   AVX-512 or NEON kernels chosen at run time).
4. Every assembly the search completes first goes through the
   `RotationFilter`: an assembly that is a rotation or mirror image of one
   already found is dropped unless Keep Rotated or Keep Mirror is on. The
   rest are handed over **once and serialised** to the callback, so
   `onAssembly` is never running on two threads at the same time.

## 10. `onAssembly`: storing what was found

`SolveSession::onAssembly(Assembly)`:

- If a pause was asked for meanwhile, the assembly goes to the problem's
  pending list for the next run and the callback returns `true` without
  counting it.
- With `action_ == SaveAssemblies`, under the results mutex, every
  `keepEach`-th assembly is stored with `problem_.addSolution(assembly)`
  as a solution without a disassembly.
- `problem_.incNumAssemblies()`, then `trimSavedSolutions()` keeps the
  saved list at the **Limit** by thinning it evenly, and `applyLiveSort()`
  keeps a sort the user asked for while the run is going.

Each `addSolution` moves the problem's `solutionsVersion()`, a counter
the engine reads to know when to tell the app that the list changed.

## 11. The engine polls while the run goes

The engine's main thread is free all this time. The loop in `Engine::run`
waits at most 50 ms for a line while a solve runs, then calls
`Engine::poll` → `pollEvents` (`engine/events.cpp`):

1. `Document::poll()` first: it takes in the results of background work
   that has ended. Nothing yet.
2. For the watched problem, `pollSolve`:
   - The state changed from what was last told (or the watch says
     `launched`), so `solve.state {problem: "p1", state: "preparing"}` is
     sent; on a later poll `"assembling"`.
   - Every 250 ms while active, `solve.progress` with
     `Document::solveProgress`, which is `SolveSession::progress()`: the
     stage, the elapsed time, the assembly fraction and iteration count,
     the thread count, the assemblies and solutions so far, and an
     `activity()` text.
   - `readProblem` reads the problem's `solutionsVersion()`, saved count
     and totals without disturbing the run (the session writes the problem
     under its own mutex; `readProblem` reads under it). When the version
     moved, `solve.solutionsAdded {problem, saved, solutionsVersion,
     assemblies, solutions}`.
3. `announceChange` sends a `document.changed` only if the revision moved,
   which it has not.

## 12. The renderer shows progress

`Solves` (`web/src/renderer/state/solves.ts`) listens to the events and
updates the problem's `SolveRecord`: `state` and `phase` (`active`),
`progress`, `added`. Every change is emitted to its subscribers. The
`SolverWorkspace` subscribed in its constructor:

- On any change of the problem shown it calls `runCard.render()`.
  `RunCard.renderRun()` reads `ws.counts()` and `ws.activity()` from the
  record's progress and writes the Activity line, the progress bar, the
  Assemblies and Solutions counters, the time used and the time left. It
  also sets the card's `data-state`, `data-phase` and `data-end`
  attributes, which the end-to-end tests read. **Solve** is now disabled
  and **Pause** enabled (the record's phase is `active` and the state is
  one of `preparing`, `assembling`, `draining`).
- On `solutionsAdded` it calls `pager.added(version)`. The `SolutionPager`
  (`solver/pager.ts`) marks the pages it holds as old and asks the engine
  again for the rows in sight with `solution.page {problem, sortBy,
  offset, limit: 100}`; the engine sorts and pages the list under
  `readProblem`. `SolutionsCard` renders the rows as they arrive, so the
  list fills while the search runs.
- The status bar (`updateStatus`) shows the problem's line: assemblies
  found and the activity.

## 13. The run ends

When the search has visited the whole tree, `Assembler0::assemble`
returns `RunEnd::Finished` (only the search itself may say so). Back in
`assembleAndTakeApart`:

1. No stop was requested, so the Drain phase runs: with nothing to take
   apart it is immediate.
2. `endRun(RunEnd::Finished)`: under the results mutex the run's time is
   added to the problem, the search statistics are recorded, and because
   the search finished and nothing is pending, `problem_.finishedSolving()`
   marks the problem `Solved`. The state moves to `Finished`. The thread
   returns.

## 14. The engine tells the end

At the next poll:

1. `Document::poll()` sees that the problem in `runsStarted_` has a session
   that is no longer active. It records a change `{solutions, problems,
   [p1]}`, which bumps the revision and sets the dirty flag.
2. `pollSolve` sends `solve.state {state: "finished"}`, a last
   `solve.progress` (a run's end always gets one) and, if the version
   moved since the last telling, a last `solve.solutionsAdded`.
3. `announceChange` sends the `document.changed` for the revision step 1
   made.

The order matters and is a rule of the protocol: **a run's last state
comes before the `document.changed` of its end**. The app treats that
`document.changed` as the end of the run.

## 15. The renderer finishes

- `Solves.state('finished')`: not an active state, and the record's phase
  was `active`, so the phase becomes `ending`. The following
  `document.changed` that names `p1` sets the phase to `none`, records
  the `endRevision` and emits `ended`; `whenEnded()` promises resolve
  (the autosave waits on them).
- `Store` reloads the snapshot. `solving` is false again, so the
  workspaces unfreeze. The problem's summary now says `solved` with its
  counts.
- `SolverWorkspace` handles `ended`: with no solution selected it calls
  `pageChanged()`, the pager refreshes at the final `solutionsVersion`,
  and the Run section shows **Solve** and **Continue** as the state
  allows (Continue is off: a finished solve has nothing to carry on;
  Solve starts over).
- The window's title gets its dirty mark: the solutions are unsaved
  changes until **File ▸ Save** writes them into the `.xmpuzzle`.

When you click a row, `Player` (`solver/player.ts`) reads it with
`solution.get` and fetches each piece's mesh in its assembly orientation
with `shape.mesh {transformation}`; `SolutionScene`
(`view3d/solution.ts`) draws the pieces in three.js. With a disassembly it
also calls `playback.open` and samples the poses for the player.

## With Assemble and Disassemble

Choosing **Assemble and Disassemble** sets `disassemble: true`. The flow
above gains a second pipeline inside the same run:

- In step 6 the `action_` becomes `SaveSolutions`, and the session checks
  that the grid has a disassembler (`Disassembler::canDisassemble`: cubes
  and prisms).
- In step 9, before the search starts, `startWorkers()` makes a second
  `ThreadPool` for the take-aparts and one `Disassembler` per worker
  (`chooseWorkers` picks how many from the thread budget), each with the
  options' rotations (`checkRotations`) and solver (Classic is complete;
  Crowell takes a limited number of successors from each position and has
  its own turn generator). Each worker runs `workerLoop` on a thread of
  its own.
- In step 10, `onAssembly` no longer stores the assembly. It makes a
  `Task {assembly, assemblyNumber, solutionNumber}` and `enqueue`s it on
  a bounded queue (at least 64 entries). When the queue is full the
  search thread waits, lending its cores to the take-aparts meanwhile. A
  one-piece assembly needs no take-apart and is counted at once.
- `workerLoop` pops a task and calls `processTask`, which calls
  `disassembler.disassemble(task.assembly, runToken())`
  (`solve/disassembler.cpp`): a breadth-first search over positions,
  level by level, with `MovementAnalysator` finding the moves from each
  position (Bill Cutler's algorithm over a `MovementCacheBricks`), and
  `RotationRules` and a `RotationMoves` generator adding 90 degree turns
  when Check Rotations is on. A position where a group of pieces comes
  free splits the problem in two, each taken apart in turn. The result is
  a `TakeApartResult`: a `Separation` tree, or none.
- An assembly that does not come apart is counted as inseparable and
  dropped. One that does goes to `addSolution(task, separation)`: with
  Sort by **Moves** (the default) it is inserted after every solution with
  as many or more moves (`sumMoves`), by binary search, equals ordered by
  assembly number; the list is cut at the **Limit**. With **Just Levels**
  only a `SeparationInfo` (levels and move counts) is kept, not the tree.
- The progress event now also carries `disasmCompleted`, `disasmPending`,
  `disasmWorkers` and the take-aparts under way, which the Run section
  shows. The status line adds "N can be disassembled".
- In step 13, when the search is through but tasks are still queued, the
  state moves to `Draining` until `waitDrained()` returns; then
  `stopWorkers()` joins the worker threads.

## Pause, Continue and Clear, briefly

**Pause** is `solve.pause` → `Document::solvePause` → `SolveSession::pause()`,
which asks the run's `StopSource` for a `Pause`. The search returns at a
point it can be saved from, queued take-aparts go back to the problem's
pending list, `storePosition` writes the assembler's position into the
problem (so the file can be saved), and the state is `Paused` with the
assembler kept in memory. **Continue** is `solve.continue` →
`solveResume` → `resume()`, which carries on with the same assembler, or
from the position a reopened file holds. **Clear** is `solve.abort`, a stop
of kind `Abort`: everything is thrown away and the state is `Idle`.

## Where this is tested

- `tests/unit/solve/test_solve_session.cpp`: the session's runs, pauses
  and continues, including stress tests of random pause timing.
- `tests/unit/app/test_document_solving.cpp`: solving through the
  `Document`, with the freeze of edits.
- `tests/unit/engine/test_engine.cpp` and `tests/contract/test_engine.py`:
  `solve.start` and its events, in process and as a subprocess.
- `web/tests/engine/solving.test.ts` and `solver.test.ts`: the client
  against the real engine.
- `web/tests/e2e/solver.e2e.ts`: the Solver tab driven in the built app,
  reading the card's `data-*` attributes.
- `just oracle`: the counts against the original BurrTools.

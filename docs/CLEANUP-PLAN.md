# Plan: code cleanup and optimization

## Context

Written on 2026-11-24 from a read-only review of the whole tree at commit
fbfbd2e. The review had eleven areas, one reviewer each: the sliding search;
the assemblers and the take-apart; the solve session, stacking and Panex; the
document layer; the engine, CLI, Python module and API; the lower core
modules (model, io, grid, mesh); the Electron main process, preload, engine
client and demo; three parts of the renderer (Solver, Play and the
explorers; the shell, Puzzle and exports; Entities and the 3D view); and the
tests and the build. Nothing was built or run; timings come from the build's
own logs (`build/release/.ninja_log`, `build/dev/Testing/Temporary/CTestCostData.txt`,
`build/tilted-gate/*.log`). The findings that matter most were checked again
in the code; those are marked ✓. The rest are a reviewer's reading of the
code: specific and plausible, not proven. Stage 2 was added the same day at
the user's request, after two Windows users could not use v2.1 (see there).
The same day the stages were put in a new order: Opus does the early work,
Fable takes the stages that need it later on, and Fable sessions review some
of Opus's early stages ("Fable checkpoints").

What the review judged, and what this plan keeps to: agents are the only
readers and writers of this code (the user said so when asking for the
review). Nothing here is about how code reads to a person (names, comments,
layout, splitting a function so it reads better). Structure counts where it
helps an agent change the code correctly and cheaply:
- one source of truth for every fact, or a test that ties the copies
  together (a fact kept by hand in several places drifts, and several
  already have);
- files that nearly every feature must edit: they cost context to read and
  parallel sessions collide in them; split where the history shows a seam;
- invariants an agent could break without noticing, made checked
  (`static_assert`, `BT_ASSERT`, a type, a test);
- docs that contradict the code (an agent believes them).

Beside that: bugs that lose the user's work or give wrong answers,
robustness against damaged files, the app's health on users' machines, and
speed on hot paths, in the test loop and in the build.

## How to work through this plan

These rules hold for every item. They come from AGENTS.md, CLAUDE.md and the
user's standing instructions.

1. **Confirm first.** Each item came from a read-only review. Before
   changing anything, read the code it names and confirm the problem. Line
   numbers are as of fbfbd2e; find the code by the function and file names,
   which are given for that reason. If the finding is wrong or already
   fixed, tick the item and write `Not needed (date): why.` under it.
2. **One item, one change.** Keep each item's change to what it says. When
   an item turns out to need more (a core change, an engine method, long
   runs, another item first), tell the user at once in a short message:
   what grew, why, and roughly how long it will take. Then carry on; the
   user stops it if they want to.
   **Code decisions are the agent's.** The user asks for features and
   checks the app for bugs, and trusts the agent with the code: structure,
   refactors, CI, internal design (said on 2026-11-24). Decide those
   questions yourself and write the choice and its reason in the Done line.
   Tell the user about any change users would notice. Ask only when an item
   would change what the app does in a way this plan does not already say.
3. **Answers stay identical.** The core is compared with the original
   BurrTools by the oracle (`tests/oracle/`). Speed and structure items must
   not change any assembly, solution, move or level count, or the text and
   files the oracle compares. Never assert exact iteration counts (AGENTS.md).
4. **File compatibility is one way.** ShapeLogic must open every file the
   original wrote (convert on load); the original need not read ShapeLogic's
   saves, for any feature. Never add shims, dual encodings or limits so the
   old program can read new files. The oracle round trip's "the original
   reads the new save" check is not a requirement. Saved searches (sliding
   level files, `state.bin`, Panex folders) and their folder names must stay
   readable by the new code unless an item says otherwise.
5. **Benchmarks.** `tests/bench/bench.py` (the default set, `--plain`,
   `--set glar`): one solve at a time, nothing else running, never beside a
   sanitizer run; within 5% of the original or faster. Write the numbers
   (before and after) into the item's Done line.
6. **Builds and other sessions.** Several sessions work in this checkout at
   once and share `build/<preset>/` and `web/out/`. Before an e2e or engine
   test run, check that no other build or e2e is under way (`ps` for
   `cmake --build`, `electron-vite build`, `playwright`). A failure at app
   launch (`#stage` or `data-engine` not found) is most likely another
   session's build: rerun that test alone before debugging it. Never rebuild
   `build/release` while a solve runs. `git status` may hold other sessions'
   changes: touch and report only your own files.
7. **The renderer reloads the user's window.** `just app-dev` and
   `just app-release` run the app through the Vite dev server: saving any
   file under `web/src/renderer/` reloads the user's open window at once.
   Before editing renderer files, check for a running app
   (`pgrep -fl "electron-vite dev"`, `pgrep -fl shapelogic_engine`). If a
   solve may be running, tell the user that the window will reload; for
   anything touching the solver, autosave or solve state, copy `web/`
   (without `node_modules`, `out`, `dist`) into the scratchpad beside
   symlinks of the repo's other top-level folders, symlink `node_modules`,
   and build and run the e2e specs there with
   `SHAPELOGIC_VERSION=<the engine's version>` (the copy has no git tag and
   would build as 0.0.0, which the engine refuses). Copy the finished files
   in together.
8. **Tests.** Run `just test` while working and `just test-all` before
   calling an item done. For the front end: `just app-check`, `just app-test`
   and only the e2e spec files the change touches
   (`npx playwright test tests/e2e/<file>` in `web/` after `npm run build`);
   never the whole e2e suite (`just app-e2e`) unless the user asks, and say
   what was left unrun. `just gate` (about 75 minutes) only when the user
   asks for every suite.
9. **Checks.** `just check <regex of the changed C++ files>` stays green; a
   suppression needs its reason on the line
   (`// NOLINTNEXTLINE(check): reason`). The front end: `just app-check`.
   American spelling everywhere, docs included: the web unit test
   `spelling.test.ts` scans every file in the repo; a line that must keep a
   British spelling ends with `british-ok`.
10. **Engine methods.** A new method or parameter follows api/README.md's
    "Adding a method": the schema entry, a contract call that succeeds,
    `python3 api/gen_api_md.py`, then `just app-gen`.
11. **Branches.** Before a split (stages 13, 21), a large move (stage 20) or
    the rewrite of a shared doc (stage 3), run `git worktree list`, and
    `git log main..<branch> --stat` for each open branch. At the time of
    writing they are `feature/cell-search` and
    `feature/v2-idea-3-cutting-plane-editor`.
    If a branch edits a file the item moves or rewrites, do the item anyway.
    In its Done line, write which branch it is and which new files that
    branch's hunks belong in, so its merge can re-apply them.
12. **No commits.** Never commit, amend, rebase, switch branches or push
    unless the user asks. When a stage is done, leave it ready to commit and
    give the message it would get: `Cleanup N: <summary>`.
13. **Recording.** When an item is done, change its box to `[x]` and add a
    line under it: `Done (date): what changed; the tests added; the gates
    run and their results; anything that differs from the item.` That line
    and the Status section are this plan's record (no separate handoff
    note).
14. **ARCHITECTURE.md.** After stage 3 it is a map and a set of rules: when
    an item adds, removes or moves a file, change its map line; when it
    changes an invariant, change that rule. No new prose sections.
15. **Names.** Project files never name the user or the people who report
    problems: write "the user", "a Windows user".

## Status

Stages 1 to 14 need only Opus 5.5 High, with Fable 5.2 High reviewing some
of them (the checkpoints). Stages 15 to 20 need Fable. Stage 21 is Opus
again, and comes last.

- [x] Stage 1: checks that cannot pass on nothing, and that end
- [ ] Stage 2: startup health: logs and messages that tell why the app cannot work
- [ ] Stage 3: the agent docs: trimmed, and true (decided: D2)
- [ ] Checkpoint A (Fable): review of stage 3
- [ ] Stage 4: bugs that lose work: the small fixes
- [ ] Checkpoint B (Fable): review of stage 4
- [ ] Stage 5: smaller bugs: core, engine, Python, CLI
- [ ] Checkpoint C (Fable, only if needed): review of stage 5
- [ ] Stage 6: renderer bugs
- [ ] Stage 7: main-process bugs
- [ ] Stage 8: test and build speed; tests that cannot clash
- [ ] Checkpoint D (Fable): review of stage 8
- [ ] Stage 9: CI and release pipeline
- [ ] Stage 10: front-end speed and memory
- [ ] Stage 11: one source of truth: core, engine, API, Python, CLI
- [ ] Checkpoint E (Fable, only if needed): review of stage 11
- [ ] Stage 12: one source of truth: front end
- [ ] Stage 13: splits: front end
- [ ] Stage 14: dead code and the last traps
- [ ] Stage 15: subtle bugs: the engine's end, one solve setup, change reports, two solves at once, the Solve undo step (decided: D1)
- [ ] Stage 16: damaged and hostile files
- [ ] Stage 17: core speed
- [ ] Stage 18: the engine stays responsive
- [ ] Stage 19: names and audits that must not drift
- [ ] Stage 20: core deduplication under benchmarks
- [ ] Stage 21: splits: core

## Who builds each stage

| Stage | Work | Complexity | Model |
|---|---|---|---|
| 1 | The oracle and the web tests fail when they compared or ran nothing; time limits | Low | Opus 5.5 High |
| 2 | Startup health logs, the engine-failure message, Windows diagnostics | Medium: much of it Windows behavior that only the user can try | Opus 5.5 High |
| 3 | AGENTS.md's read list; ARCHITECTURE.md as a map and rules; stale docs | Medium: judging which prose holds facts the code does not | Opus 5.5 High |
| A | Review of stage 3: the docs kept every fact | Medium | Fable 5.2 High |
| 4 | The small fixes for bugs that lose work: saved-search writes, Panex's failed save and Abort, the "saved" flag, rescue size | Low to medium: each comes with a test that shows a mistake | Opus 5.5 High |
| B | Review of stage 4 | Medium | Fable 5.2 High |
| 5 | Small core, engine, Python and CLI bugs | Low to medium | Opus 5.5 High |
| C | Review of stage 5, only if needed | Low to medium | Fable 5.2 High |
| 6 | Renderer bugs | Medium | Opus 5.5 High |
| 7 | Main-process bugs | Medium | Opus 5.5 High |
| 8 | Test shards, the version define, scratch folders, per-run copies, helpers | Low to medium | Opus 5.5 High |
| D | Review of stage 8: nothing stopped running | Medium | Fable 5.2 High |
| 9 | CI pinning and automatic re-runs | Low | Opus 5.5 High |
| 10 | Front-end speed and memory | Medium | Opus 5.5 High |
| 11 | Single sources in core, engine, API, Python, CLI | Medium | Opus 5.5 High |
| E | Review of stage 11, only if needed | Medium | Fable 5.2 High |
| 12 | Single sources in the front end | Medium | Opus 5.5 High |
| 13 | Front-end splits | Medium | Opus 5.5 High |
| 14 | Dead code; tests that keep the page and the code in step | Low | Opus 5.5 High |
| 15 | The engine's end, one solve setup, change reports, two solves at once, the Solve undo step | High: each can pass its tests and still be wrong | Fable 5.2 High |
| 16 | Damaged and hostile files | Medium: a missed bound is invisible until a bad file comes | Fable 5.2 High |
| 17 | Core speed | Medium to high: identical answers, measured | Fable 5.2 High |
| 18 | The engine's request thread and Stop | High: stops in the middle of builds and merges | Fable 5.2 High |
| 19 | Saved-search names, the change audit, names pinned by tests | High: a mistake orphans users' saved searches | Fable 5.2 High |
| 20 | Assembler and take-apart deduplication | High: speed and exactness of ported searches | Fable 5.2 High |
| 21 | Core splits | Medium: mechanical, checked by the tests and the oracle | Opus 5.5 High |

The rule behind the choice is the one of `docs/TILTED-SQUARE-PLAN.md`: Fable
where a wrong answer is hard to see (a saved search that carries on
wrongly, an assembly lost, a race, a missed bound), Opus where a mistake
shows in a failing test or on the screen. That gives 107 items to Opus and
36 to Fable, plus the checkpoints. The five fixes in stage 4 are small, and
each comes with a test that shows a mistake, so they stay early with Opus.
The two bugs that lose work and need Fable are 15.1 and 15.2.

Sometimes an Opus session meets a wrong answer it cannot explain: a count
or a path that differs, or a test that fails for a reason it cannot name.
It then stops that item, writes what it saw under it, and leaves it for a
Fable session. Start each stage's session with the model this table names
(`/model`).

## Decisions

**Decided by the user on 2026-11-24:**
- **D1. Solving records an undo step.** Today an undo after a solve can
  throw its results away (see 15.5). Each solve run and each take-apart
  records a step ("Solve P1", "Take apart P1"), so undo removes the results
  first and redo brings them back. This amends decision 0011. Item 15.5.
- **D2. ARCHITECTURE.md becomes a map and a set of rules,** and AGENTS.md
  stops asking every session to read it (and PLAN.md) whole. Stage 3.

**Left to the agent by the user on 2026-11-24.** The user's words: "AI is
managing all the code … backend and logic changes I want to trust Claude
to make". These items go ahead as written, and each says how to decide its
details:
- **D3. CSS and markup with their modules** (13.1): yes. Add screenshot
  tests of the dialogs before moving their CSS.
- **D4. CI and release** (stage 9): pin the macOS runner image and its
  Xcode. Build for the lowest macOS that the app's Electron version
  supports, or the lowest the code builds for without changing a result if
  that is higher. Re-run release jobs that got no runner from a workflow.
- **D5. Per-mode facades for `document.h`** (21.6): yes, after 21.1 to 21.5.
- **D6. Merging the duplicated assembler and take-apart code**:
  yes, under the benchmarks (stage 20). AGENTS.md allows reshaping a port once it is
  proven. An item that cannot keep the benchmarks within noise is dropped
  and recorded: `Not done (date): the numbers`.

## Order, and work side by side

- Run the stages in their numbers' order, one stage per session. Stages 1
  to 14 are Opus's. Stages 15 to 20 are Fable's, so one switch of model
  covers them all. Stage 21 is Opus's again, last, because it moves code
  that stages 15 to 19 edit first.
- Checkpoints A, B and D (Fable) review stages 3, 4 and 8 soon after each
  is done; C and E run only if needed ("Fable checkpoints" below). A
  checkpoint can run beside the next Opus stage when they edit different
  files; their test runs follow rule 6.
- Stage 1 first: every later stage is checked by these suites.
- Stage 2 next: real users are affected now.
- Stage 3 early: it makes every later stage cheaper to start. It rewrites
  files every session reads, so do it when no other session is mid-change
  in them (`git status` shows their edits). Open branches will conflict in
  `docs/ARCHITECTURE.md` at their merge; rule 11 says what to record. The
  fix is easy: take the new file and add the branch's map lines.
- Stage 4 then: the fixes for bugs that lose work that are small enough for
  Opus.
- Stages 4 and 5 (core and engine) and stages 6 and 7 (front end) touch
  different files and can run in separate sessions at the same time. The
  core stages rebuild `build/release`, which the web engine and e2e tests
  use, so keep to rule 6.
- Stage 8 changes the test machinery: not beside another stage's test runs.
  Every later stage's tests run faster after it.
- Stage 9 any time.
- Stage 10 after stage 6 (10.1 after 6.11).
- Stage 11 after stage 5; stage 12 after stages 6 and 7; stage 13 after
  stages 10 and 12.
- Stage 14 any time; its deletions in the renderer follow rule 7.
- Stage 15 after stages 4 and 5; 15.3 comes before 19.2's audit.
- Stage 16 after stage 4 (both edit the saved-search readers and writers).
- Stages 17 and 18 after stages 4 and 8; stage 19 after stages 11 and 15;
  stage 20 after stage 17.
- Stage 21 last. Stages 15 to 19 edit `sliding_search.cpp`,
  `solve_session.cpp`, `puzzle_file.cpp` and `document.h` in place; the
  splits then move finished code.

## Fable checkpoints

Opus does stages 1 to 14. A Fable 5.2 High session reviews some of those
stages soon after they are done, to check that the work holds up where tests
are thin (added at the user's request on 2026-11-24):
- Checkpoints A, B and D always run.
- C and E run only if an earlier checkpoint found a real problem: a bug that
  the stage's own tests did not catch.

Each checkpoint, in one Fable session:
1. Read this plan's rules, the reviewed stage's items and their Done lines.
2. Find the stage's changes. If the stage was committed, use
   `git show <its commit>` (message `Cleanup N: …`). If not, use `git diff` on
   the files its Done lines name; other sessions' changes may be in the
   tree.
3. For each item, check that:
   - the problem was real (read the code as it was);
   - the change fixes it and keeps what its *Keep* line says;
   - the new test fails without the fix: revert the fix in a scratch copy
     (for the front end, rule 7's copy) and run that test.
4. Look for what the tests do not reach: error paths, the other platforms,
   the edge cases in the item's text.
5. Run the stage's gates.
6. Under each item, add `Review (date, Fable): OK`, or what was wrong and what
   this session changed. Fixes stay ready to commit with the stage, or as
   `Cleanup N: review fixes`.
7. Tell the user in a few lines what was checked and what was found. If a
   real problem got past Opus, run the optional checkpoints, and consider
   moving later items of the same kind to Fable (write the change into "Who
   builds each stage"). If A, B and D find nothing beyond small notes, skip
   C and E.

## Stage 1: checks that cannot pass on nothing, and that end

Every later stage is judged by these suites. On 2026-11-22 the oracle's full
tier compared no puzzle at all and still passed.

- [x] **1.1 The oracle fails when it compared nothing** (trap; S) ✓
  - *Where:* `tests/oracle/oracle.py`: `load_corpus()` (79-110; the warning
    "corpus pattern matched nothing" at about 91); `cmd_compare()`
    (314-400), `cmd_compare_files()` (595-638), `cmd_compare_text()`
    (822-915), `cmd_roundtrip()` (1184-1252) with `roundtrip_one()`
    (1140-1183), `cmd_imports()` (1378-) with `imports_one()` (1309-1339);
    `tests/oracle/corpus.txt:74` (the full tier is one pattern:
    `tictest/performance/puzzles/**/*.xmpuzzle`).
  - *Problem:* a pattern that matches nothing is a warning and is dropped.
    The compare commands walk the corpus entries, never the baseline's
    keys, so an empty selection prints `0/0 match` and exits 0.
    `build/tilted-gate/oracle-roundtrip.log` (2026-11-22) shows ten
    patterns that matched nothing, among them the full tier's only line
    (158 puzzles in `tests/oracle/baseline/full.json`). Round trip and
    imports count "both programs refuse the input" as agreement even when
    the input file does not exist.
  - *Change:*
    1. `load_corpus()`: collect the unmatched patterns; after the loop,
       print each and exit 2, unless a new flag `--allow-missing` is given
       (for a reference checkout known to be partial).
    2. Every compare command: build the set of baseline keys of the
       selected tiers and solver (`baseline_path()`, `text_baseline_path()`,
       `files_dir()`); a baseline key with no compared row is a failure,
       listed as "in the baseline, not compared: <rel>"; a selection of
       zero rows is a failure ("nothing was compared").
    3. `roundtrip_one()` and `imports_one()`: a missing input is a failure
       ("missing input"), never "both refuse".
    4. The last line of each command says how many were compared:
       "N of M baseline entries compared, K differ".
  - *Keep:* exit 0 on a full match; the baselines' format.
  - *Tests:* the oracle has no unit tests. By hand: point `--old-repo` at an
    empty folder in the scratchpad; `compare`, `compare-text`,
    `compare-files`, `roundtrip` and `imports` must each exit non-zero.
    Write the commands and exit codes into the Done line.
  - *If the full tier then fails* because its pattern matches nothing in
    the reference checkout: stop and tell the user. Do not look for the
    files outside the repo (the reference checkout is outside it).
  - Done (2026-11-24): confirmed: against `../burr-tools` ten patterns match
    nothing (`test/test_rotation_solvers`, all four `tictest/` patterns, the
    three sliding and two stacking fixtures of the original), so the full and
    slow tiers compared 0 puzzles and passed. `tests/oracle/oracle.py`:
    - `load_corpus()` lists every unmatched pattern and exits 2;
      `--allow-missing` (on every command that reads the corpus) makes them
      warnings.
    - `compare`, `compare-files` and `compare-text` load the baselines of
      the selected tiers (`solver_tiers()`, now also `select()`'s tier rule),
      not only of the tiers that matched.
      - A baseline entry that was not compared is `NOT COMPARED`, a failure.
        `compare-files` uses the summary baselines for its list, since
        `work/old` is not kept by tier, and leaves out `I` entries.
        `compare-text` also fails a run in the baseline that it no longer
        makes (`NOT RUN`).
      - Under `--allow-missing`, only entries whose file is absent from the
        checkout are excused (`MISSING`); one that is there but was not
        compared still fails.
    - A run that compared nothing fails.
    - `roundtrip_one()` and `imports_one()` give `MISSING INPUT` (a failure)
      for an input that does not exist. `imports` no longer drops a missing
      `IMPORT_SCAD_EXTRA` file without a word. `python` fails when no corpus
      puzzle was run.
    - Each command's last line: "N of M baseline entries compared, K
      differ" (inputs, jobs, transcripts for the commands with no baseline).
    - tests/oracle/README.md says so.
    By hand, `--old-repo` an empty scratch folder:
    - `compare --tier all`, `compare-text --tier all` and
      `compare-files --tier all`: exit 2 (28 patterns);
    - `roundtrip`, `imports` and `python`: exit 2 (no original library or
      module, before the corpus);
    - with `--allow-missing --tier full`: `compare`, `compare-text` and
      `compare-files` exit 1 ("nothing was compared", "0 of 158 baseline
      entries compared");
    - the paths the empty folder cannot reach, called directly:
      `not_compared()` fails a present-but-uncompared entry even under
      `--allow-missing`, and `roundtrip_one()` and `imports_one()` give
      `MISSING INPUT` for an absent file.
    The real checkout:
    - `just oracle --tier fast` and `--tier full`: exit 2;
    - with `--allow-missing`: fast 18 of 19 compared, 0 differ; stacking 38
      of 41 (1 OLD FAILED, 2 missing); stacking panex 10 of 11; sliding 23 of
      35 (9 one-thread only, 3 missing); all exit 0.
    **The full and slow tiers now fail until the reference checkout holds
    `tictest/` and the missing `test/` and `examples/` files.** Its
    `build/` is also missing (no `burrTxt`, no `libburr_core.a`), so
    `roundtrip`, `imports`, `python` and the `baseline-*` commands cannot
    run either. Told the user; nothing was looked for outside the repo.

- [x] **1.2 Web tests and the smoke test fail under the gate instead of skipping** (trap; S)
  - *Where:* `web/tests/global-setup.ts` (17-26: prints "the engine tests
    are SKIPPED" and goes on); the ten `describe.skipIf(ENGINE === null)`
    in `web/tests/engine/*.test.ts` (grep `skipIf(ENGINE`);
    `web/tests/e2e/packaged.smoke.ts:77`; `tests/gate.sh`.
  - *Problem:* `just app-test` exits 0 with every engine test skipped when
    `build/release` has no engine; `just app-smoke` exits 0 with no
    packaged app. The gate's table then reads "pass".
  - *Change:* a variable `BT_REQUIRE_ALL=1`: global-setup throws (the run
    fails) instead of warning when the engine is missing; the smoke test
    fails instead of skipping; `tests/gate.sh` exports it for its groups.
    Without the variable, local runs behave as now.
  - *Tests:* in `web/`: `BT_REQUIRE_ALL=1 BT_ENGINE=/nonexistent npx vitest run tests/engine`
    fails; without `BT_REQUIRE_ALL` it skips with the message.
  - Done (2026-11-24): with `BT_REQUIRE_ALL=1`:
    - `web/tests/global-setup.ts` throws when there is no engine;
    - `packaged.smoke.ts` throws when there is no packaged app;
    - `web/tests/e2e/global-setup.ts` throws when there is no engine or no
      `out/` build. Added beyond the item: the gate's e2e group runs
      `just app-e2e`, which does not build the engine, so every spec would
      skip and the gate would read PASS.
    `tests/gate.sh` exports the variable. Differs from the item's test: an
    explicit `BT_ENGINE` that does not exist was already used as given (the
    helpers skip only when the default engine is missing), so
    `BT_ENGINE=/nonexistent` without the variable fails rather than skips,
    as before. The setup's message now says so instead of "SKIPPED".
    Checked:
    - `BT_REQUIRE_ALL=1 BT_ENGINE=/nonexistent npx vitest run tests/engine`:
      exit 1;
    - the default engine moved aside: without the variable 39 skipped, exit
      0; with it, exit 1;
    - the smoke test with no app: skipped, exit 0; with the variable, exit 1;
    - `layout.e2e.ts` with no build: skipped, exit 0; with the variable,
      exit 1;
    - `just app-check` passes, and `just app-test` passes 569 of 569, also
      with `BT_REQUIRE_ALL=1`.

- [x] **1.3 Every test run has a time limit** (stability; S)
  - *Where:* `tests/CMakeLists.txt` (79-104: no TIMEOUT except the
    `python` test's, at 112); `CMakePresets.json` (no test timeout);
    `tests/gate.sh:66` (each step runs without a limit).
  - *Problem:* a deadlocked shard stalls `just test` for good; in the gate
    the output goes to a log, so the summary table never prints.
  - *Change:*
    1. A TIMEOUT on every ctest test: its recorded cost
       (`build/<preset>/Testing/Temporary/CTestCostData.txt`) times 3,
       at least 120 s; a cache variable `BT_TEST_TIMEOUT_SCALE` (1 by
       default, 6 in the asan and tsan presets) multiplies it.
    2. `--durations yes` on the `bt_tests` commands, so a log shows the
       last case that finished.
    3. `gate.sh`: each step under a limit. macOS has no `timeout` by
       default: run the step in its own process group in the background,
       wait with a deadline, kill the group when it passes, and record
       "timeout" in the table.
  - *Note:* item 8.1 changes the shards; set these limits generously, or do
    8.1 first.
  - Done (2026-11-24):
    - `tests/CMakeLists.txt`: a `bt_test_timeout()` helper. The limits, at
      least three times the longest time seen (the dev cost data and a run
      beside the oracle): quick shards 600 s (seen 140 s), slow shards
      1200 s (311 s), contract 300 s (4 s in the cost data, 41 s beside the
      shards), cli, api-schema and api-docs 120 s, python 600 s (as
      before).
    - `BT_TEST_TIMEOUT_SCALE` (1; 6 in the asan and tsan presets) multiplies
      them. Checked with `ctest --show-only=json-v1`: every one of dev's 21
      tests has a limit, and asan's and tsan's are ×6 (slow shards
      7200 s; asan's slowest was recorded at 929 s).
    - `--durations yes` on both `bt_tests` commands; a shard's log lists
      each case's time.
    - `tests/gate.sh`: each step runs in its own process group under a
      watcher that stops the group after `GATE_STEP_TIMEOUT` seconds (7200
      by default; the longest steps take about 20 minutes). The table says
      `TIMEOUT`, and Ctrl-C stops the running step too. Tested on a copy
      with fake steps and a 3 s limit: `sleep 60` and a step whose children
      sleep were stopped after 3 s and marked TIMEOUT, later steps ran, the
      table printed, and no process was left; a TERM to the gate stopped
      its step.
    - `tsan`'s `bt_tests "[parallel]"` runs outside ctest; only the gate's
      step limit covers it.
    - `just test` passes (13 of 13) and `just test-all` passes (21 of 21,
      353 s, beside oracle runs).

## Stage 2: startup health: logs and messages that tell why the app cannot work

Two Windows users could not use v2.1:
- One user's log had `ERROR [window 1] Engine Start Failed: spawn UNKNOWN`.
- Another user wrote that v2.1 "is either broken or quarantined. can't open
  a file, yet. The software installed and runs, but once I'm in I can't do
  anything else (open, new, etc)". There is no log; that user later fixed
  it, and the app now works.

So far the problem has been seen only on Windows. The user is getting the
app signed so that security software stops blocking it. This stage makes
the next report tell whether the fault is in the app or in the system
blocking it.

What the code does today:
- **The start.** `NodeTransport.start()` (`web/src/engine/node-transport.ts`)
  launches the engine twice:
  - once with `execFile(enginePath, ['--version'])` (`engineVersion()`, 19-38);
  - then `spawn(enginePath)` for the session (constructor, 55-92).
- **What a failure keeps.** A failure keeps only `error.message`:
  - from the version check, `"<path> --version: spawn UNKNOWN"`;
  - from the second launch, the client's `"the engine ended (status null: spawn UNKNOWN)"`
    (`engine/client.ts:552`).
- **Windows errors.** Node reports `UNKNOWN` (errno -4094) when Windows refuses
  `CreateProcess` with an error it has no name for. That is typically
  security software, Smart App Control or a code-integrity policy blocking
  an unsigned program, or a damaged executable. The Windows error code
  itself is lost.
- **The log.** `EngineHost` (`web/src/main/engine-host.ts`) turns a failed
  start into `{state: 'failed', reason}`. `main/index.ts:455-464` logs
  "Engine Start Failed" with that reason. The app log starts with one "App
  Started" line (`main/index.ts:777-781`), and the machine's description
  (`machineText()`, `main/app-log.ts:585-610`) is logged only with Debug
  logging on.
- **The screen.** The renderer shows the failure only in the status line,
  as "The engine could not be kept running: <reason>"
  (`renderer/shell/app.ts:94`).
- **Menu commands are held silently.** Every document command (Open, New,
  Open Recent, Import) that comes before the first document is held, with
  no word, until a document arrives (`renderer/shell/menus.ts:118-127`,
  `DOCUMENT_COMMANDS` at 38). With no engine, none ever arrives. That
  matches the second report exactly.

Design rules for this stage:
- **No slower start.** Cheap checks run at every start and are logged. The
  only work after the first window shows is hashing the engine's file.
- **No system tools unasked.** The app runs no system tool (PowerShell,
  `wevtutil`, `reg`) by itself. These run only when the user asks for
  diagnostics: an unsigned program that starts PowerShell is just what
  security software watches for, and running it could make the blocking
  worse.
- **Privacy.** The log stays on the user's machine until the user sends
  it. Diagnostics name only ShapeLogic's own files, the system's version,
  and the security products' names and states. Copied text shortens the
  home folder to `~`.

- [ ] **2.1 A health report in the log at every start** (health; M)
  - *Where:* `web/src/main/index.ts` (777-781, the "App Started" line; the
    window and engine start at 388, 420-470), `web/src/main/app-log.ts`
    (`machineText()` 585-610, logged only with Debug logging), a new
    `web/src/main/health.ts`.
  - *Change:* right after "App Started", log one "App Health" block (a few
    info lines, every start, not only with Debug logging):
    - versions: ShapeLogic, Electron, Chromium, Node (`process.versions`);
    - the system: `os.type()`, `os.release()`, `os.version()` (Windows:
      the edition and build), `process.arch`, `os.arch()`,
      `app.runningUnderARM64Translation`, the locale
      (`app.getSystemLocale()`), and `machineText()` (cores, memory, free
      space), moved out of the debug-only path;
    - the install: `process.execPath`, `process.resourcesPath`,
      packaged or not, per-user (under `%LOCALAPPDATA%\Programs`) or
      per-machine (`Program Files`), and warnings for a path with
      characters outside ASCII, a path longer than 200 characters, or an
      app run from a temporary or download folder (`%TEMP%`, `Downloads`;
      a zip opened without extracting runs from `%TEMP%`);
    - the engine file (and the CLI's, if present): the path, whether it
      exists, its size and mtime, whether it can be opened and its first
      bytes read (the PE `MZ`, Mach-O or ELF magic), and on Windows
      whether it carries the Mark of the Web (read the alternate stream
      `<path>:Zone.Identifier` with `fs.readFileSync`; log its `ZoneId`
      only, not its URLs);
    - the folders: user data, logs, the cache root and the slide and Panex
      folders, the temp folder: each exists or was made, is writable (make
      and remove a small probe file), and its free space;
    - the GPU: `app.getGPUFeatureStatus()`'s webgl, webgl2 and
      gpu_compositing (after `app.whenReady()`).
    - The engine file's SHA-256, compared with the hash recorded at
      packaging (2.6), is computed after the first window shows (it is
      tens of MB), then logged ("engine file matches the release" or "does
      not match: damaged or changed by other software").
  - *Keep:* the "App Started" line as it is (tools may look for it); the
    log's line format (`formatLogLine`).
  - *Tests:* `web/tests/unit/health.test.ts`: the report from a fake `fs`
    and `os` (an engine missing, a path with non-ASCII characters, a Mark
    of the Web, an unwritable folder) gives the expected lines; the start
    path calls it once.

- [ ] **2.2 A failed engine start keeps every detail and says the likely cause** (health; M)
  - *Where:* `web/src/engine/node-transport.ts` (`engineVersion()` 19-38:
    `reject(new Error(`${enginePath} --version: ${error.message}`))`; the
    constructor's `'error'` handler 81-87: `this.spawnError = e.message`;
    `start()` 96-107); `web/src/main/engine-host.ts` (`start()` 78-96 and
    `doLaunch()` 224-276: `{ state: 'failed', reason: e.message }`;
    `scheduleRestart()` 299-327); `web/src/engine/client.ts:552` (the
    "the engine ended (…)" text); `web/src/main/index.ts:455-464` (the
    log lines).
  - *Change:*
    1. Keep the error's fields: Node's spawn errors carry `code` (e.g.
       `UNKNOWN`, `ENOENT`, `EACCES`, `EPERM`, `EBUSY`), `errno` (`UNKNOWN`
       is -4094 on Windows), `syscall` and `path`. `engineVersion()` and
       the `'error'` handler keep them in a `SpawnFailure` object
       (platform-free type in `web/src/engine/`) with `which: 'version' | 'session'`,
       saying which of the two launches failed, and the time since the
       start. A failed version check by timeout (`execFile`'s
       `killed: true`, `signal: 'SIGTERM'`) is `code: 'TIMEOUT'`.
    2. `EngineStatus`'s failed state gets an optional `failure` with those
       fields (the bridge types in `web/src/engine/bridge.ts` /
       `app-bridge.ts`), so the renderer can show it.
    3. A table of likely causes, `web/src/main/start-diagnosis.ts`, keyed by
       platform and code, written in plain words:
       - `UNKNOWN` (Windows): "Windows refused to start the engine. This is
         usually security software (an antivirus, Smart App Control or a
         company's application control) blocking a program that is not
         signed, or a damaged engine file.";
       - `ENOENT`: "the engine's file is missing: security software may
         have quarantined it, or the install is incomplete";
       - `EACCES`/`EPERM`: "Windows denied running the engine: security
         software or the file's permissions";
       - `EBUSY`: "the file is locked, often by an antivirus scanning it;
         try again";
       - `TIMEOUT`: "the engine started but did not answer within 10
         seconds; security software may be holding it";
       - a version mismatch: "another version of the engine is installed;
         reinstall ShapeLogic".
    4. When the engine starts but ends before its hello, decode Windows
       exit codes (Node gives them as unsigned 32-bit numbers; log decimal
       and hex). Use a table in the same file:
       - 0xC0000135 (3221225781), a library is missing;
       - 0xC000007B (3221225595), the file is damaged or for another processor;
       - 0xC0000142 (3221225794), a library failed to start;
       - 0xC0000005 (3221225477), the engine crashed;
       - 0xC000001D (3221225501), an instruction this processor lacks;
       - 0xC0000409 (3221226505), the engine stopped itself on a broken check;
       - 0xC0000022 (3221225506), access denied;
       - 0xC0000428 (3221226536), Windows could not verify the program's
         signature (code integrity).
    5. The log line becomes, for example:
       `ERROR [window 1] Engine Start Failed: spawn UNKNOWN (errno -4094, syscall spawn, the version check) at C:\…\shapelogic_engine.exe, 120 ms after start. Likely cause: …`,
       plus one line with the last engine stderr text if any.
  - *Note:* item 10.10 removes the `--version` launch. Do this item first,
    so that the reports from v2.2 users say which of the two launches
    failed. If it is mostly the second, a scanner that lets the first run
    and then blocks is likely.
  - *Tests:* unit tests for the cause table and the exit-code decoder;
    `web/tests/unit/host.test.ts`: a fake start that rejects with
    `{code: 'UNKNOWN', errno: -4094, syscall: 'spawn'}` gives a failed status
    whose `failure` has those fields and `which`.

- [ ] **2.3 The window says when the engine cannot start; held commands are not lost silently** (health, bug; M)
  - *Where:* `web/src/renderer/shell/app.ts` (`statusLine()` 80-106: the only
    place a failed engine shows); `web/src/renderer/shell/menus.ts`
    (118-127: document commands held until the first document); the
    renderer's engine status (`store.status`); `web/src/main/engine-host.ts`
    (after five quick deaths the host gives up: `scheduleRestart()`).
  - *Problem:* the second report above: the window works but Open, New and
    the rest do nothing, because they wait for a document an engine that
    never started will never send.
  - *Change:*
    1. When the engine's status is failed, or the first document has not
       come 15 seconds after the page loaded, show a dialog (through
       `openDialog`, `shell/dialog.ts:137`): "The engine could not start",
       the cause in plain words (2.2), what to try (Windows: open Windows
       Security ▸ Protection history, restore and allow ShapeLogic if it
       was quarantined; allow it in other antivirus programs; Smart App
       Control blocks programs that are not signed), and buttons:
       - **Try Again**: a new bridge call that resets the host's quick-death
         count and launches again;
       - **Copy Details** (2.4);
       - **Show Log**: opens the Log File Manager;
       - **Close**.
       Show it once per failure, not on every status change.
    2. A document command that comes while the engine has failed is not
       held: it shows the same dialog. Commands held while the engine is
       still starting stay held, as now. Log each held and each dropped
       command at info level.
    3. The status line keeps its text and adds "(see Help ▸ Copy Diagnostics)".
  - *Tests:*
    - e2e (a new `web/tests/e2e/engine-failure.e2e.ts`): start with `BT_ENGINE`
      pointing at a missing file; the dialog shows the ENOENT cause; File ▸
      Open shows the dialog again instead of nothing.
    - Unit: the menus drop, rather than hold, a document command once the
      status is failed.

- [ ] **2.4 Help ▸ Copy Diagnostics, and the Windows security checks behind it** (health; M)
  - *Where:* a new menu item in `web/src/main/menu.ts` and its command
    (`web/src/engine/app-bridge.ts` COMMANDS, the menus' handlers); a new
    `web/src/main/diagnostics.ts`; the Log File Manager
    (`renderer/shell/log-files.ts`) and the dialog of 2.3 get a Copy
    Details button.
  - *Change:*
    1. On request (the menu item, or the buttons), main builds a text
       report:
       - the health report (2.1);
       - the last engine failure with its cause (2.2);
       - the last 200 lines of the app log;
       - the count and dates of crash dumps (2.7);
       - on Windows, the security checks below.
       It puts the text on the clipboard and offers **Save Diagnostics…**
       to a `.txt` file. Paths shorten the home folder to `~`.
    2. The Windows checks run only on request, each with a 5-second
       timeout, through `execFile` of the system's own tools:
       - Microsoft Defender's recent events about ShapeLogic's files:
         `wevtutil qe "Microsoft-Windows-Windows Defender/Operational" /q:"*[System[(EventID=1116 or EventID=1117 or EventID=1118 or EventID=1121 or EventID=1123)]]" /c:50 /rd:true /f:text`.
         These are 1116, a detection; 1117, an action such as quarantine;
         1118, a failed action; 1121, blocked by an attack surface
         reduction rule; 1123, blocked by Controlled Folder Access. Keep
         only the events whose text names `shapelogic` (any case) or the
         install folder.
       - Code integrity and Smart App Control blocks:
         `wevtutil qe "Microsoft-Windows-CodeIntegrity/Operational" /q:"*[System[(EventID=3077 or EventID=3076 or EventID=3033)]]" /c:50 /rd:true /f:text`,
         filtered the same way.
       - AppLocker (Windows Pro and Enterprise):
         `wevtutil qe "Microsoft-Windows-AppLocker/EXE and DLL" /q:"*[System[(EventID=8004)]]" /c:20 /rd:true /f:text`,
         filtered.
       - Crashes Windows recorded: the Application log's "Application Error"
         and "Windows Error Reporting" events (1000, 1001) that name
         `shapelogic_engine.exe` or `ShapeLogic.exe`.
       - Smart App Control's state: `reg query HKLM\SYSTEM\CurrentControlSet\Control\CI\Policy /v VerifiedAndReputablePolicyState`
         (0 off, 1 on, 2 evaluation).
       - Installed antivirus products and their states:
         `powershell -NoProfile -NonInteractive -Command "Get-CimInstance -Namespace root/SecurityCenter2 -ClassName AntivirusProduct | ForEach-Object { $_.displayName + ' ' + $_.productState }"`
         (names and states only). Never pass `-ExecutionPolicy Bypass` or
         an encoded command: security software flags those patterns.
       - Controlled Folder Access:
         `powershell -NoProfile -NonInteractive -Command "(Get-MpPreference).EnableControlledFolderAccess"`
         (0 off, 1 on, 2 audit).
       Some logs need an administrator. An "access is denied" answer is
       recorded as such and the report goes on. If these Microsoft tools
       themselves fail to start with `spawn UNKNOWN` or `EPERM`, say so in
       the report: that is a strong sign the app as a whole is being
       restricted.
    3. On macOS (signed and notarized already), only on request: the app's
       `com.apple.quarantine` attribute (`xattr -p`) and
       `spctl --assess --type execute -vv` of the app. Linux: nothing extra.
  - *Tests:* unit tests of the event filter (fake `wevtutil` output with
    and without ShapeLogic's files), of the home-folder shortening, and
    that nothing runs a system tool before the user asks (spy on
    `execFile`).

- [ ] **2.5 Writes that Windows refuses say why** (health; S to M)
  - *Where:* the engine's file errors (`core/shapelogic/io/gzip_file.cpp`:
    "could not open … for writing" and the other `fail(Errc::IoError, …)`
    texts); the main process's writes (`web/src/main/export-file.ts`,
    `state-store.ts`, `rescue.ts`, `cache-files.ts`); the messages the
    renderer shows for a failed save or export.
  - *Problem:* Microsoft Defender's Controlled Folder Access (Windows
    Security ▸ Ransomware protection) blocks programs it does not know
    from writing into Documents, Desktop, Pictures and other folders; an
    unsigned app is such a program. Today the message does not say why a
    save failed.
  - *Change:*
    1. Core: add the system's reason to every IoError text: `: ` plus
       `std::error_code`'s message (or `std::strerror(errno)` right after
       the failing call) and the code, e.g. "could not open
       C:\…\puzzle.xmpuzzle for writing: Access is denied (5)".
    2. Main and renderer: when a save or export fails with access denied
       on Windows (EACCES/EPERM in main, or the engine's text with code 5),
       the message adds: "Windows may be blocking ShapeLogic from writing
       to this folder (Controlled Folder Access). Allow ShapeLogic in
       Windows Security ▸ Virus & threat protection ▸ Ransomware
       protection, or save to another folder." Log it with the folder.
  - *Keep:* the error kinds (Errc) and the engine's error codes; only the
    texts grow.
  - *Tests:* core: a write into a read-only folder gives a message with the
    system's reason (POSIX: chmod; skip on Windows CI if it cannot be set
    up); web unit: the Windows hint is added for an access-denied failure.

- [ ] **2.6 The release records the engine's hash, and the engine says hello in the log** (health; S)
  - *Where:* `web/tools/dist.ts` (packaging); `engine/main.cpp` (start);
    `engine/app_log.cpp` (the engine's log line format that
    `parseEngineLine`, `web/src/main/app-log.ts:130`, reads);
    `core/shapelogic/base/system.cpp` (`raiseOpenFileLimit`).
  - *Change:*
    1. `dist.ts` writes `engine.sha256` (the hex digest, and the CLI's in
       a second line) next to the engine in the app's resources; 2.1
       compares with it.
    2. The engine, when started without `--version`, writes one info line
       through its log channel. The line gives:
       - its version and build type;
       - the vector paths it chose (AVX2 or scalar on x86-64, NEON on arm64);
       - the open-file limit it got;
       - its slide and Panex folders, and whether they exist and are
         writable. The engine learns these after `settings.set`, so log that
         part when the folders are set.
    3. Main logs the time from spawn to the engine's hello.
  - *Tests:* `web/tests/unit/dist.test.ts` (the hash file is written and
    matches); a contract test that the first stderr line parses as an info
    line with the version.

- [ ] **2.7 Crash dumps kept locally, and the startup's phases timed** (health; S)
  - *Where:* `web/src/main/index.ts` (startup, window creation 388 and 688,
    `win.on('closed')` 512); the renderer's first-document moment (Store's
    first snapshot).
  - *Change:*
    1. `crashReporter.start({ uploadToServer: false })` before `app.whenReady()`,
       so crashes of the main, renderer and GPU processes leave minidumps in
       `app.getPath('crashDumps')`. The diagnostics (2.4) count them and give
       their dates. The engine is not an Electron process; its crashes show
       through 2.2's exit codes and, on Windows, 2.4's Application log events.
    2. One "Startup" info line per window, with the milliseconds from the app's start
       to: the window shown, the page loaded (`did-finish-load`), the
       engine spawned, its hello, and the first document shown (the
       renderer tells main once, through an existing or new
       `bt:ready` message).
  - *Tests:* unit test of the line's format; the e2e start (any spec) shows
    a Startup line in its log (the e2e apps log to `BT_USER_DATA`).

- [ ] **2.8 The update check logs why it failed** (health; S)
  - *Where:* `web/src/main/updates.ts` (88 lines; the fetch is given:
    Electron's `session.fetch`).
  - *Problem:* a firewall or a proxy that inspects TLS shows here, not in
    the engine (the engine uses no network: only its stdin and stdout).
  - *Change:* log each check's outcome once per start at info level: the
    HTTP status, or the error's name and code (`ENOTFOUND`, `ECONNRESET`,
    `ETIMEDOUT`, a certificate error such as
    `UNABLE_TO_GET_ISSUER_CERT_LOCALLY`, which points to a proxy that
    rewrites TLS). The diagnostics (2.4) include the last one.
  - *Tests:* `web/tests/unit/updates.test.ts` with a stubbed fetch that
    rejects with each kind.

- [ ] **2.9 A troubleshooting page in the guide, and a checklist for the user to try on Windows** (health; S)
  - *Where:* the user guide in `site/` (Astro; `just site-build` checks
    links and Help URLs); the help-link table (today in four places:
    `web/src/engine/app-bridge.ts:316`, `web/src/main/security.ts:33-42`,
    `web/src/main/app-ipc.ts:486-497`, `web/src/demo/demo-app.ts:65-77`;
    item 12.3 makes it one).
  - *Change:*
    1. A guide page, "When ShapeLogic does not start on Windows". It gives:
       - what the messages of 2.2 mean;
       - how to look in Windows Security ▸ Protection history and restore or
         allow ShapeLogic, and how to allow it in other antivirus programs;
       - what Smart App Control and Controlled Folder Access are;
       - how to send Help ▸ Copy Diagnostics.
       The dialog of 2.3 and the cause texts link to it as a new help link.
    2. A checklist in this item's Done line for the user to run in the
       Windows virtual machine (agents cannot):
       - install the build;
       - rename the engine's file (the ENOENT path);
       - deny it execution with `icacls <engine> /deny *S-1-1-0:(X)` (the
         EACCES or EPERM path);
       - if the machine has AppLocker (Pro), block the engine's path with
         a rule (a policy block: expect `UNKNOWN` or `EPERM`);
       - each time, check the dialog, the log lines and Copy Diagnostics.
  - *Tests:* `just site-build`.

**Gates for stage 2:** `just app-check`, `just app-test`, the e2e specs
touched (engine-failure.e2e.ts and the specs whose files changed), the
contract test of 2.6, `just test` (for 2.5's core texts), `just check` on
the changed C++ files, `just site-build`.

## Stage 3: the agent docs: trimmed, and true (D2, decided)

The docs are what every session reads first. Today AGENTS.md has every
session read `docs/PLAN.md`, `docs/ARCHITECTURE.md`, the newest handoff and
the area's decisions before any task. A front-end task adds
`docs/FRONTEND-PLAN.md`, `docs/webui-spec.md` and `docs/API.md`. That is
over 100,000 tokens before work starts, and `ARCHITECTURE.md` alone is
about 43,000.

- [ ] **3.1 AGENTS.md: what to read, and how a piece of work ends** (docs; S)
  - *Where:* `AGENTS.md`: lines 9-14 (the read list), 66 (".xmpuzzle compatible both ways"),
    81 (the SolveStateCell rule; see 3.3), "Finishing a stage" (the stage-era
    conditions such as "from stage 3", "from stage 8"), "The web front end"
    (its own read list and finishing steps).
  - *Change:*
    1. The read list becomes:
       - this file;
       - the active plan for the work (the `docs/*-PLAN.md` the user names,
         or the one whose Status has open stages);
       - in `docs/ARCHITECTURE.md`, the section of each module the work
         changes: its map says which file holds what;
       - the decisions that section names.
       `docs/PLAN.md` leaves the list: it is the finished porting plan, kept as
       history. For the front end, `FRONTEND-PLAN.md`, `webui-spec.md` and
       `API.md` become references to search, not documents to read whole.
    2. "Finishing a stage" says the current gates once, without stage
       numbers:
       - C++: `just test-all`; `just test-asan`; `just test-tsan` for changes
         to threaded code; the oracle tiers the change can touch; `just
         check` on the changed files; the benchmarks for solver changes
         (rule 5 above);
       - front end: `just app-check`, `just app-test`, and the e2e specs
         the change touches (the whole suite only when the user asks).
       Then: ARCHITECTURE.md's map lines and rules updated (no prose), the
       plan's Status and Done lines, and the work left ready to commit
       (CLAUDE.md).
    3. Line 66 becomes the one-way rule (rule 4 of "How to work through
       this plan" above, in AGENTS.md's words), and decision 0006 gets a
       line "Amended 2026-11-21: compatibility is one way…".
  - *Keep:* AGENTS.md's code rules, test rules, commands and SIMD rules.

- [ ] **3.2 ARCHITECTURE.md: a map and the rules** (docs; M)
  - *Where:* `docs/ARCHITECTURE.md` (2,556 lines; edited in 44 of the 88
    commits of November 2026, so parallel sessions collide there).
  - *Change:* rewrite it as:
    - the big picture (keep the diagram) and the directory layout table;
    - for each module (base, grid, model, io, mesh, solve, app, engine,
      cli, python, web):
      - a map: one line per file saying what it is for, e.g.
        "`solve/panex_search.{h,cpp}`: the Panex search and its saved
        searches (decision 0015)";
      - the module's invariants and rules: who writes what, the threads,
        the file formats and what must stay readable, what must stay
        identical with the original;
      - the decisions that govern it, by number and title;
    - the rules that hold everywhere (keep), testing against the original
      and build and test (keep the facts, drop the history).
    Drop:
    - the "Current state" paragraph (history; it still says "stage 8");
    - per-file prose that restates what the code and its header comment
      say;
    - the stage labels ("stage 6:").
    Keep every fact the code does not state: invariants, rules that span
    files, reasons (pointing at decisions). When unsure whether a paragraph
    says something the code does not, keep it, shortened.
  - *How:* start the map from the files themselves (AGENTS.md requires each
    file to begin with a comment saying what it is for): a scratch script
    can print `path: first comment line` per module. Take the invariants
    from the current text.
  - *A test keeps it so:* a unit test (`tests/` Python or a web unit test)
    that fails when a source file under `core/`, `engine/`, `cli/`,
    `python/src/` or `web/src/` has no map line, when a map line names a
    file that does not exist, or when the file passes 800 lines. A new file
    then costs one map line, and prose does not grow back.
  - *Branches:* open branches that edit this file will conflict at their
    merge. Record them as rule 11 says. The merge's fix is to take the new
    file and add the branch's map lines.

- [ ] **3.3 Docs that contradict the code** (docs; S)
  - Each of these is wrong today; fix the text (code changes that go with
    some of them are in the items named):
    - The SolveStateCell rule. "Only the run's worker writes a
      SolveStateCell" (`AGENTS.md:81`, `core/shapelogic/base/solve_state.h:7`
      and 46, `docs/ARCHITECTURE.md:106` and 2530) is not so:
      - `launch` (`solve_session.cpp:387`, 522), `launchStacking` (564, 608)
        and `abort` (259) write it on the owner's thread, safe only because
        `controlMutex_` and the join serialize them.
      - Write it as: "one writer at a time: the run's worker while it runs,
        the owner under `controlMutex_` before and after."
      - The code check is 5.11.
    - The Panex autosave interval. "Every 20 minutes" is in
      `core/shapelogic/solve/stack_search.h:101`,
      `engine/methods_solve.cpp:64`, `api/solve.json:32` (so also
      `docs/API.md`) and decision 0015:54. Since 4b6a2c8 the Panex search
      saves on SaveCadence: a 60-second floor, then 50 times the last
      save's duration (`panex_search.cpp:1266`).
    - The new grids' take-apart. `api/types.json:28` (and so `docs/API.md`
      and `web/src/engine/api.generated.ts`) says hexagonal and cairo
      puzzles are "not taken apart". `core/shapelogic/grid/grid_type.cpp:42-43`
      gives them `kCapDisassemble`.
    - CROWELL.md. Agents are told to read `core/shapelogic/solve/CROWELL.md`
      before changing the Crowell code, but:
      - item 15 says nodes hash absolute coordinates, so translated poses
        are stored twice; they compare positions relative to piece 0
        (`disassembler_node.cpp:74-131`);
      - item 18 says Classic rebuilds cells for each candidate; it caches
        them (`rotation_moves_classic.cpp:141-153`, 346-356);
      - it names ARC_STEPS and BEVEL_REMOVE; the constants are `kArcSteps`
        and `kBevelRemove`.
    - `core/shapelogic/solve/simd_exact_cover.h:91-93` says the larger sizes
      "serve assembler 1"; they serve only a unit test.
    - Decision 0012:216 says long work is kept in jobs, but
      `explore.openAssembly` is not until 18.1. Decision 0012:205 says the
      explorers refuse sliding puzzles; `core/shapelogic/app/explorers.cpp:80-99`
      opens them. Add "Amended" lines.
    - `docs/ARCHITECTURE.md` (if 3.2 has not replaced these lines):
      - 394 says saves are "byte-identical to the original's"; saves now
        add `uid`, and the tests compare `withoutUids`
        (`tests/unit/io/test_puzzle_file.cpp:92`);
      - 1650 and 1687 say only the undo depth and threads go to the engine;
      - 1671 names `pausedBt2`;
      - 1939 describes `ws.kind`, which 14.1 removes;
      - 1077-1079 describe the shared-search guard (see below).
    - `core/shapelogic/grid/TABLES.md` ends by saying the mesh tables "come
      over" in stage 6 (done long ago).
    - The shared-search guard. Its comments (`core/shapelogic/app/document.h:632-637`,
      675-677, 700-705, 945-953; `core/shapelogic/app/document_stacking.cpp:136-171`)
      say copies of a problem share a saved-search folder.
      - Since decision 0044 the uid names the folder (`panex_search.cpp:1655-1659`;
        `tests/unit/app/test_document_stacking.cpp:771` asserts copies have
        their own).
      - The guard can now fire only for two problems paused before uids with
        equal stacks (`core/shapelogic/io/puzzle_file.cpp:948-990`).
      - Reword the comments to that legacy case and add a test of it. If the
        legacy case cannot happen, remove the guard, and say so in the Done
        line.
    - `core/shapelogic/solve/solve_session.cpp:73-76` (the first paragraph of
      `chooseWorkers`'s comment gives the workers "the take-apart share" of a
      thread limit; the code gives them all of it);
      `core/shapelogic/solve/solve_session.h:527` (a comment stranded above
      `logMutex_`).
    - `web/README.md:123` says the demo opens
      `examples/PelikanBurr.xmpuzzle`; `web/src/demo/main.ts:63-93` opens one
      of eleven `examples/demo/<key>.xmpuzzle`.
    - Doc comments above the wrong declaration:
      `web/src/renderer/shell/prefs.ts:10`,
      `web/src/renderer/shell/app.ts:64`,
      `web/src/renderer/puzzle/workspace.ts:223`,
      `web/src/renderer/view3d/view.ts:486-491` (`setSolution`'s comment
      above `setDisplay`).
  - *Verify:* `python3 api/gen_api_md.py` and `just app-gen` after the
    schema text changes (the api-docs and staleness tests); the spelling
    test.

**Gates for stage 3:** the map test of 3.2, `ctest -R api-docs`,
`just app-test` (the spelling and staleness tests), `just site-build` if the
guide's links changed.

## Checkpoint A (Fable): review of stage 3

The procedure is in "Fable checkpoints". What to look at:
- **Every fact the old `docs/ARCHITECTURE.md` stated that the code does not
  is still stated.**
  - List the old text's invariants, rules that span files, file-format and
    compatibility statements, thread rules and decision pointers. Read it
    with `git show <the commit before stage 3>:docs/ARCHITECTURE.md`.
  - Check that each one is in the new rules, or in the header comment of the
    file it concerns, or is no longer true (say why).
- The map test passes. Twenty map lines picked at random match their files'
  header comments.
- AGENTS.md's new read list and finishing steps keep every rule of
  CLAUDE.md and of this plan's "How to work through this plan".
- Each fix of 3.3 matches the code it describes.

## Stage 4: bugs that lose work: the small fixes

Small fixes for bugs that lose work. Each is a few lines and comes with a
test that shows a mistake, so Opus makes them. The two such bugs that
need Fable (the engine ending without pausing its solves, and one solve
setup for four programs) are 15.1 and 15.2.

- [ ] **4.1 Saved-search state and level files: check the write after the file is closed, and make it durable** (bug; S to M) ✓
  - *Where:*
    - `core/shapelogic/solve/sliding_search.cpp` `writeState()` (4096-4138).
      Its check `if (!out) return false;` (4130) runs inside the block,
      before the `std::ofstream` closes.
    - `core/shapelogic/solve/panex_search.cpp` `Store::saveState()`
      (636-666, check at 658) and `writeKeys()` (586-593, the level files:
      `return static_cast<bool>(out)` while open).
    - The sliding level files are written in `slide_levels.h`; check their
      writer the same way.
    - The helpers that do it right are in `core/shapelogic/io/gzip_file.h`:
      `writeFileStreamed(path, std::function<bool(std::ostream&)>)` and
      `writeFileWhole()`, implemented in `gzip_file.cpp:205` and 286. They
      check each write, close, then rename.
  - *Problem:*
    - The bytes still buffered are written when the stream's destructor
      closes the file, and that error is lost. A sliding `state.bin` is a few
      hundred bytes, so on a full disk none of it reaches the file before the
      close.
    - `writeState` then returns true, and `rename(state.tmp, state.bin)`
      puts an empty file over the last good state. `run()` logs "saved"
      (4332-4341), and `pin()` (4935) releases the files the old state named.
    - The next Continue says "its state.bin could not be read". A search
      of days is lost, which is exactly what decision 0041 is there to
      prevent.
    - A level file cut short the same way counts as whole. Every later
      Continue then fails with "could not trace its path back".
    - Nothing in `core/` calls fsync, so a power cut right after the rename
      can also leave an empty `state.bin`.
  - *Change:*
    1. The `put`/`get` helpers (sliding_search.cpp:4073-4084,
       panex_search.cpp:514-522) take `std::ostream&` and `std::istream&`.
    2. Both searches write `state.bin` through `io::writeFileStreamed`, with a
       lambda that writes and returns whether the stream is still good. Panex
       writes its level files (`writeKeys`) the same way. The sliding level
       writer follows the same rule: check after the close, rename after the
       check.
    3. `writeFileStreamed`, `writeFileWhole` and `writeGzipFile` sync the file
       before the rename (POSIX `fsync`; macOS `fcntl(fd, F_FULLFSYNC)`,
       falling back to `fsync`; Windows `_commit`). On POSIX they also sync
       the folder after the rename (open it read-only and `fsync`).
    4. Measure: a sliding solve that saves often, timed alone before and
       after. If `F_FULLFSYNC` costs more than 1% of the solve, use plain
       `fsync` for the level files and keep `F_FULLFSYNC` for `state.bin`.
       Write the numbers in the Done line.
    5. The leftover sweep (`sliding_search.cpp:4605-4613`, and Panex's)
       must also remove `writeFileStreamed`'s own temporary file names (look
       at what `gzip_file.cpp` names them), or a crash mid-save leaves
       them for good.
  - *Keep:* the bytes of `state.bin` and of the level files: old searches
    carry on.
  - *Tests:* for each search, a unit test where the state write fails. Make
    the folder read-only after the first save (POSIX; skip on Windows), then
    pause again, and check that:
    - the save is reported as failed;
    - the old `state.bin` is untouched;
    - a Continue still resumes from it.
    Existing guards: the resume cases in `tests/unit/solve/test_sliding.cpp`
    and `test_sliding_session.cpp:1511` (the v1.8.1 fixture), the save and
    resume cases in `test_stacking.cpp` and `test_stacking_session.cpp`,
    and `just oracle-resume`.

- [ ] **4.2 Panex: a Pause whose save fails deletes the search folder** (bug; S) ✓
  - *Where:* `core/shapelogic/solve/panex_search.cpp`: the `haltAndSave`
    lambda (1258-1265: `search_.saved = saveState()`); the end of `solve()`
    (1903-1912): `if (searcher.store != nullptr && !search.saved && search.outcome != Outcome::Error) { store.remove(); }`,
    where `remove()` deletes the whole folder.
  - *Problem:* when the save made by a Pause (`Outcome::Stopped`) or by a
    memory stop (`Outcome::Memory`) fails, the end of `solve()` deletes the
    folder, the last good autosave included. The same disk failure inside
    the loop goes through `diskFailed()` (`Outcome::Error`) and keeps the
    folder. The comment above ("A finished search, found or not, needs
    nothing saved") covers only the finished case. The sliding search does
    it right: it falls back to its last save (`pinned_`,
    `sliding_search.cpp:4342`).
  - *Change:* in `haltAndSave`, when `saveState()` fails, return what
    `diskFailed()` returns, so the folder stays and the session reports the
    disk problem (`solve_session.cpp:1040-1055` has the words). An older
    `state.bin` still resumes correctly: the levels it lacks fall back to
    solving by halves (`panex_search.cpp:1025-1031`). Correct the comment.
  - *Tests:* in `tests/unit/solve/test_stacking_session.cpp` (the damage
    table at 315 is a model), make the folder read-only before a Pause.
    Afterwards the folder and its old `state.bin` are still there, the
    outcome names the disk, and Continue resumes from the older state.

- [ ] **4.3 Panex: Abort writes the whole search before it is deleted** (bug, speed; S)
  - *Where:* `panex_search.cpp` `haltAndSave` (1258-1265: saves on every
    stop); `core/shapelogic/solve/solve_session.cpp:249-253` (`abort()`,
    then `discardSaved`); `engine/engine.cpp` (`solve.abort` joins the run
    on the request thread).
  - *Problem:* on an Abort the search writes its kept levels and its whole
    frontier (gigabytes, deep in a search), and the session then deletes
    them. Meanwhile the engine answers nothing. The sliding search skips its
    save on an abort (`sliding_search.cpp:4324`).
  - *Change:* in `haltAndSave`, save only when the stop is not an abort
    (find where the search reads its stop kind: the token or StopSource it
    was given, `StopKind::Abort` in `base/stop.h`). On an abort, set the
    outcome and return without writing.
  - *Tests:* "abort throws the saved search away" in
    `test_stacking_session.cpp` still passes; add a check that an aborted
    run writes no file into the folder after the abort was asked (compare
    the files' sizes and mtimes before and after).

- [ ] **4.4 The document says it is saved when it is not, after more edits than the undo depth** (bug; S) ✓
  - *Where:* `core/shapelogic/app/document.cpp`:
    - `save()`: `cleanStamp_ = undo_.empty() ? 0 : undo_.back().stamp;` (232);
    - `dirty()` (246-249: `current = undo_.empty() ? 0 : undo_.back().stamp`);
    - `setSettings()` (251-256) and `pushStep()` (533-539), which drop the
      oldest steps.
    The depth test is `tests/unit/app/test_document.cpp:690-698`.
  - *Problem:*
    - An empty undo stack always counts as stamp 0, the stamp of the file
      as opened.
    - Open a file and make 26 edits at depth 25: edit 1's step is dropped.
      Undo 25 times: the stack is empty, so `dirty()` is false while edit 1
      is still applied. `web/src/renderer/shell/files.ts:129` then closes the
      window without asking.
    - The reverse also happens: when the step whose stamp was saved has been
      dropped, undoing back to the saved state says it is changed.
  - *Change:* a member `baseStamp_` (0 when a file is opened). In `pushStep`
    and `setSettings`, before dropping the oldest step, set
    `baseStamp_ = undo_.front().stamp`: the state at the bottom of the stack
    is the state after that step. One helper,
    `currentStamp() { return undo_.empty() ? baseStamp_ : undo_.back().stamp; }`,
    is used by `dirty()` and `save()`. Grep `stamp` for every other reader
    (open, openBytes, redo) and check each.
  - *Tests:* extend the depth test:
    - 26 edits at depth 25, then 25 undos: `dirty()` is true;
    - 8 edits, save, 25 more edits at depth 25, then 25 undos: `dirty()` is
      false (the state is the saved one).

- [ ] **4.5 Rescue copies over about 48 MiB cannot be reopened** (bug; S)
  - *Where:*
    - `core/shapelogic/app/document.cpp` `saveBytes()` (239-244: returns
      `io::writePuzzle(*puzzle_)`, the plain XML);
      `engine/methods_document.cpp` `documentSaveBytes` (172-176: base64 of
      it);
    - `engine/engine.h:227` `kMaxLine` (64 MiB, the longest request line),
      copied as `web/src/engine/client.ts:36`;
    - `web/src/renderer/shell/rescue.ts:207` (calls `document.saveBytes` at
      most every 2 s while the document is changed);
      `web/src/main/rescue.ts:58-89` (hashes it, writes it with
      `writeFileSync`);
    - the reopen: `web/src/renderer/state/store.ts:177-183`, then
      `document.openBytes {data: base64}`.
  - *Problem:* base64 makes the XML a third longer. A document whose XML is
    over about 48 MiB (many saved disassemblies, pictures) is rescued, but
    its reopen is refused with "the line is too long". The unsaved work is
    lost exactly when the copy is needed.
  - *Change:*
    - An optional parameter `gzip` (boolean, default false) on
      `document.saveBytes` (`api/document.json`; rule 10). When it is true,
      gzip the bytes: use the gzip helper in `io/gzip_file.h` or
      `base/compress.h`. `openBytes` already accepts gzip (it gunzips when
      the magic is there): check that path.
    - The rescue passes `gzip: true`.
    - The demo (`web/src/demo/main.ts:677`) gunzips when the bytes start
      with the gzip magic, so it works either way.
    - The larger fix, a rescue copy that never crosses IPC, is item 10.11.
  - *Tests:*
    - contract: `saveBytes {gzip: true}` round-trips through `openBytes`;
    - web unit: `rescue.ts` asks with `gzip: true` (the scripted transport);
    - a document over 64 MiB would be slow in the quick loop: tag it
      `[slow]`, or run it once by hand and write that into the Done line.

**Gates for stage 4:** `just test-all`, `just test-asan`,
`just oracle-resume`, the contract suite (4.5's parameter), `just check`
on the changed files, `just app-test` and the web unit tests touched
(4.5).

## Checkpoint B (Fable): review of stage 4

The procedure is in "Fable checkpoints". What to look at:
- **4.1:**
  - The writers write the same bytes as before: the same `put` calls in the
    same order, read from the diff.
  - Old searches still resume (the v1.8.1 fixture test, `just oracle-resume`).
  - The new failure tests fail when the fix is reverted.
  - The fsync branches build on all three systems (CI).
  - The leftover sweep removes the new temporary names.
- **4.2 and 4.3:** each outcome's message in the session
  (`solve_session.cpp:1040-1055`) is the right one, and an aborted search
  writes nothing.
- **4.4:** every place that reads a stamp agrees with `currentStamp()`.
- **4.5:** `document.openBytes` reads the gzip bytes, the demo still opens
  and saves a puzzle, and `docs/API.md` was regenerated.

## Stage 5: smaller bugs: core, engine, Python, CLI

Small bugs, each with a test of its own. The three that need Fable
(change reports, two sliding solves at once, the Solve undo step) are
15.3 to 15.5. Item 5.12 came from the damaged-files work; 11.6 uses its
helper.

- [ ] **5.1 The goal check can write past its array** (bug, UB; S) ✓
  - *Where:* `core/shapelogic/solve/sliding_search.cpp` `GoalCheck::reached()`
    (252-270: `std::array<unsigned int, 64> filled{}` indexed by
    `idx(group_[i])`); the constructor (145-170 makes a group for each part
    that has goals for fewer than its copies); `core/shapelogic/model/sliding.cpp:40`
    (`kMaxMark = 255`, private to that file).
  - *Problem:* the comment "only the first 63 shapes have marks" was true
    when marks were 6 bits (46a1016); since 1991b14 marks go to 255. A
    problem with more than 64 parts that each have goals for only some of
    their copies writes past the array, on the search's hot path.
  - *Change:* export `kMaxMark` from `model/sliding.h` (or `slide_marks.h`);
    size the array `kMaxMark + 1` and zero only the first `need_.size()`
    entries (the check runs for every emitted arrangement, so don't clear
    1 KiB each time); `BT_ASSERT(need_.size() <= kMaxMark + 1)` in the
    constructor.
  - *Tests:* a unit test with 70 parts that each have goals for some of
    their copies, solved to its goal; `just test-asan` (it would have
    reported the overflow).

- [ ] **5.2 `ThreadPool::run` leaves tasks that point into a dead stack frame when queueing runs out of memory** (bug; S)
  - *Where:* `core/shapelogic/base/thread_pool.cpp` `run()` (104-123):
    `lent_ += k`, the latch, then an `emplace_back` per task under `m_`.
  - *Problem:* a `bad_alloc` while the queue grows unwinds `run()` with some
    tasks queued. Those tasks hold `&latch` and `&job` of the dead frame, so
    a pool thread later uses freed stack memory, and `lent_` stays raised
    for the pool's life. `bad_alloc` is an ordinary way for a solve to end
    (`solve_session.cpp:1167`).
  - *Change:* on an exception while pushing, pop the tasks this call pushed
    (`m_` is still held, so no thread has taken them), take `k` off `lent_`,
    and rethrow.
  - *Tests:* forcing `bad_alloc` there is not worth a hook; review, plus
    `just test-tsan`.

- [ ] **5.3 The session's two run-reset blocks have drifted** (bug, combine; S)
  - *Where:* `core/shapelogic/solve/solve_session.cpp` `launch()` (450-518)
    and `launchStacking()` (568-606).
  - *Problem:* about 30 identical lines, already different:
    - `assemblerThreads_` is reset to 1 in one and to 0 in the other;
    - `searchTraced_` is reset only in the stacking one;
    - `atSavePoint_` and `action_` are reset only in `launch`;
    - neither clears `slidePickingUpSince_`. The slide search sets that clock
      in `resume()` and clears it only after `resume()` returns
      (`sliding_search.cpp:4409`, 4510). If `resume()` throws, a later Solve
      run shows "picking up" for its whole length (`progress()`, 2045-2050).
  - *Change:* one private `resetRun()` with the common lines and
    `slidePickingUpSince_.store(0)`; the kind's own lines stay in each
    caller. Pick `assemblerThreads_`'s reset value by reading what
    `progress()` shows before a run's threads start, and write the choice
    and the reason in the Done line.
  - *Tests:* the progress checks in `test_solve_session.cpp`,
    `test_sliding_session.cpp` and `test_stacking_session.cpp`; add a case:
    after a sliding resume that fails (a damaged state, as the damage cases
    make), a fresh Solve reports no "picking up" phase.

- [ ] **5.4 A Stacking Solver run on the Panex search says it waits for the search to stop** (bug; S; confirm first)
  - *Where:* the rule "this stacking solve runs on the Panex search" is written
    in three places: `core/shapelogic/solve/solve_session.cpp:979`, 1941-1942
    and `cli/main.cpp:570-571`. `progress().panexSearch` (`solve_session.cpp:1993`)
    is set from the solver choice alone. The front end uses it in
    `web/src/renderer/solver/settings.ts`:
    - 680 reads it as "the search saves itself";
    - 596 shows "Tracing the path".
  - *Problem:* pausing a Stacking Solver run that runs on the Panex search
    shows "waiting for the search to stop", and "Tracing the path" never
    appears.
  - *Change:* one predicate
    `bool runsOnPanexSearch(const Puzzle&, const Problem&, const SolveOptions&)`
    in `solve/panex_search.h`, used at the three places, and
    `progress().panexSearch` from it. If the front end needs "saves itself"
    as a separate fact, add a field to SolveProgress and the schema
    (`api/solve.json`; rule 10).
  - *Tests:* `test_stacking_session.cpp`: `panexSearch` is true for a stacking run
    that runs on the Panex search; a web unit test of `settings.ts`'s text.

- [ ] **5.5 Piece counts wrap at 65535** (bug; S)
  - *Where:* `core/shapelogic/app/document_commands.cpp`: the 0xFFFF checks at
    1109, 1203, 1355, 1390 and 1643. `addOneOfEach` (1248) and
    `document_stacking.cpp` `placeOneDisk` (103) have none.
    `core/shapelogic/model/problem.h:61-63`: a part's counts are
    `unsigned short` (`problem.cpp:303`).
  - *Problem:* adding one to a part already at 65535 wraps its maximum to 0,
    while its solutions gain a slot.
  - *Change:* `kMaxPartCount` in `model/problem.h`, defined from the field's
    type, with a `static_assert` that ties them. Use it at all seven places,
    with a refusal in the two that lack one (`InvalidArgument`, "a part holds
    at most 65535 pieces"). Add `BT_ASSERT` in `Problem::setShapeMinimum`
    and `setShapeMaximum`. `engine/methods_problem.cpp:12` and
    `engine/methods_solution.cpp:128-129` use the constant.
  - *Tests:* `test_document.cpp`: `addOneOfEach` on a part at 65535 is refused
    and leaves the document as it was.

- [ ] **5.6 Solution numbers are 32-bit while the counts are 64-bit** (bug; S to M)
  - *Where:* `core/shapelogic/model/solution.h:83-84`;
    `core/shapelogic/model/problem.cpp:610`, 619-620, 635-636
    (`static_cast<unsigned>`); `core/shapelogic/io/puzzle_file.cpp:749-750`
    (`atoi`).
  - *Problem:* a solution found after 2^32 assemblies is stored, sorted and
    saved with its number wrapped.
  - *Change:* `std::uint64_t` throughout; read the number with `strtoull`
    (16.2 later bounds every number the reader takes); write it in decimal
    as today.
    The output is the same below 2^32.
  - *Tests:* a solution numbered 2^32 + 5 round-trips through write and
    read; `just oracle-files --tier fast` is unchanged.

- [ ] **5.7 The uniform mesher loses vertices that lie close together** (bug; S)
  - *Where:* `core/shapelogic/mesh/uniform_mesh.cpp` `NearVertices` (83-114):
    one id per 1e-4 key cell (`emplace` ignores a second id). Vertices
    match within 1e-5 (`core/shapelogic/mesh/cell_geometry.cpp:23`).
  - *Problem:* two different vertices in one key cell (bevel or offset values
    between 1e-5 and about 1e-4 apart) keep only the first id. The second is
    never found again, so faces do not weld and the mesh falls back to flat
    cells.
  - *Change:* map each key to a small vector of ids. Look in the neighboring
    key cells as now (check that it does) and match within 1e-5.
  - *Tests:* a mesh with two vertices 5e-5 apart in one key cell welds
    correctly (`tests/unit/mesh/`); the STL tests and the oracle's STL dumps
    are unchanged.

- [ ] **5.8 The app log names the wrong options for `solve.continue`** (bug; S)
  - *Where:* `engine/app_log.cpp:174-189` works the options out again with
    `solveOptionsFrom`. Since 9034b03, `launch` (`engine/methods_solve.cpp:24-27`)
    uses `pausedOptions()` when `solve.continue` gives none.
  - *Problem:* the log line of a Continue names the engine's default solver
    and threads, not the ones the solve carries on with.
  - *Change:* `launch` keeps the options it used in the engine's State, and
    `logCall` prints those for `solve.start` and `solve.continue`.
  - *Tests:* in `tests/unit/engine/`, continue a paused solve without options;
    the logged line names the paused options.

- [ ] **5.9 Python: `stop()` and the iterator's destructor wait for the worker while holding the GIL** (bug; S)
  - *Where:* `python/src/module.cpp:105`; `python/src/solution_iterator.cpp`
    (100-114, `stop`; 161-163, the destructor).
  - *Problem:* `createMatrix`, and the wait after an abort, cannot be
    interrupted, so `del it` or Ctrl-C freezes every Python thread until
    they end.
  - *Change:* `py::call_guard<py::gil_scoped_release>()` on the `stop` binding;
    the destructor releases the GIL around the join. While it is released,
    touch no Python object.
  - *Tests:* `python/tests/`: start a slow solve, call `stop()` from a second
    thread while a third Python thread counts; the count goes on during the
    stop.

- [ ] **5.10 Python refuses to export sliding puzzles to Puzzlecad** (bug; S)
  - *Where:* `python/src/py_puzzle.cpp:129-146` (refuses sliding puzzles);
    `core/shapelogic/app/document.cpp:143-180` (has allowed them since
    1991b14).
  - *Change:* drop Python's own check and call the core's rule. If the rule
    sits inside `Document`, move it to a free function that both call (see
    11.5).
  - *Tests:* `python/tests/`: exporting a sliding fixture to Puzzlecad works.

- [ ] **5.11 `SolveStateCell::advance()` checks that it is the only writer** (trap; S)
  - *Where:* `core/shapelogic/base/solve_state.{h,cpp}`: `advance()` loads,
    asserts the transition, then stores. The writers are listed in 3.3.
  - *Problem:* a second concurrent writer would pass unnoticed.
  - *Change:* `advance()` uses `compare_exchange_strong` from the state it
    loaded, with `BT_ASSERT` on failure ("two writers of a solve state").
    The rule's text is in 3.3.
  - *Tests:* `just test-tsan`; the session tests.

- [ ] **5.12 Disk-budget arguments are cast without bounds** (stability; S)
  - *Where:* `cli/main.cpp:244` and 341 cast with
    `static_cast<uint64_t>(strtod(gb) * 1e9)`. That is undefined for:
    - negative values (0 on arm64, 2^63 on x86-64);
    - inf;
    - values over 1.8e10.
    `engine/convert.cpp:669-671` does the same conversion; the engine's
    settings ranges keep it in range today.
  - *Change:* one `std::optional<std::uint64_t> gbToBytes(double)` in
    `base/`. It refuses NaN, negatives, inf and values over a cap (1e6 GB).
    The CLI refuses a bad value with a message, and `convert.cpp` uses it
    too.
  - *Tests:* `tests/cli/test_cli.py`: the disk option (find its name in
    `cli/options.cpp`) with -1 is refused with exit code 2.

**Gates for stage 5:** `just test-all`, `just test-asan`, `just test-tsan`
(5.2, 5.11), `just oracle --tier sliding` (5.1), `just oracle-files --tier fast`
(5.6), `just python-test` and `just oracle-python` (5.9, 5.10), the
contract suite, the CLI tests (5.12), `just check` on the changed files,
and for 5.4 `just app-test` and the stacking e2e spec.

## Checkpoint C (Fable, only if needed): review of stage 5

The procedure is in "Fable checkpoints". What to look at:
- **5.1:** nothing is added to the work per arrangement; only `need_.size()`
  entries are zeroed.
- **5.2:** the unwinding runs under the pool's lock and leaves `lent_` as
  before.
- **5.11:** the compare-exchange's memory order matches the old store's.
- **5.9:** nothing touches a Python object while the GIL is released.
- **5.6:** numbers at and above 2^32 read and write in decimal, and old files
  read as before.
- **5.7:** the neighbor search still looks in the adjacent key cells.

## Stage 6: renderer bugs

Every item here edits `web/src/renderer/`: follow rule 7 before saving.

- [ ] **6.1 Revision waits that ignore a new document or a restarted engine** (bug; S to M) ✓
  - *Where:*
    - `web/src/renderer/puzzle/workspace.ts`:
      - `waiting` (114) is `{ revision, done }[]`, with no generation;
      - the load's error path (562-582) returns on `superseded`, `not_found`
        or `disconnected` without releasing anything;
      - `release()` (623-629) releases by revision only; `whenShown()` is at
        631-642.
    - `web/src/renderer/solver/workspace.ts` has the same pattern (181,
      1095-1114; its load error path at 1040-1046).
    - The right pattern is in `web/src/renderer/state/store.ts`:
      `whenRevision()` (139-146) stores the generation, and the snapshot
      handler (322-328) also releases the waits of another generation.
  - *Problem:* two reviewers found this one separately.
    - A Puzzle or Solver wait is released only by a later load at a revision
      at least its own. If the engine dies while an edit settles, the load
      fails with `disconnected` and nothing is released. The restarted
      engine counts revisions from 0, so the wait never resolves.
    - `Edits.mutate` then never reaches `add(-1)`, and `edits.idle` stays
      false. The rescue copy needs `edits.idle` (the quiet test in
      `shell/app.ts:216`), so no rescue copy is written for the rest of the
      session.
    - `setOption`'s `editingOptions` also stays true
      (`solver/workspace.ts:1268-1281`), and a 300 ms timer re-arms forever
      (`shell/rescue.ts:196-202`).
  - *Change:*
    1. One helper, `web/src/renderer/state/revision-waits.ts`: a class with
       `wait(revision, generation)` returning a promise, `release(revision, generation)`
       (it releases every wait of another generation, and the waits at or below
       `revision`), and `clear()`. Store and both workspaces use it.
    2. Each workspace calls `release` on every load's result, success or
       failure, with the generation the load was for, and on every new
       generation (Store's `'document'` change).
    3. Coalesce keys. Puzzle's loads use bare keys (`'problem.get'`,
       `'problem.fit'`, `'stacking.board'`, `'sliding.tray'`), while the
       Solver prefixes `'solver:'`. A new call that reuses a bare key would
       supersede Puzzle's load and cause the same hang.
       - Prefix Puzzle's keys with `'puzzle:'`.
       - Add a unit test that collects the coalesce key literals in
         `web/src/` (find the option's name in `engine/client.ts`) and fails
         on a key used in two files.
  - *Tests:* `web/tests/unit/` with the scripted transport: an edit's reply
    comes, then the engine restarts before the reload's reply (`disconnected`).
    The edit's promise resolves and `edits.idle` becomes true. Then the e2e
    specs `puzzle.e2e.ts` and `solver.e2e.ts`.

- [ ] **6.2 Commands that replace the document run under open dialogs and popovers** (bug; S)
  - *Where:*
    - `web/src/renderer/shell/menus.ts`: the constructor (112-128:
      `app.onCommand`, then `this.run(c)`); `run()` (228-305: `runAction`
      for key actions, then a switch for `openPath`, `close`, the imports
      and the rest); `DOCUMENT_COMMANDS` (38).
    - `web/src/renderer/shell/keyboard.ts` `runAction()` (67-73) refuses
      while `modal()`, except Settings and the key list.
    - `web/src/renderer/shell/files.ts`: the drop handler (546-559).
    - `web/src/renderer/shell/layout.ts:115`: `setTab` never calls `closePop`.
    - The dialogs affected: `export/stl-dialogs.ts` (114-374, 377-535) and
      `puzzle/pieces-card.ts` (241-348).
  - *Problem:* File ▸ Open is a key action, so it is refused while a dialog
    is open. But Open Recent, Import, a file opened from the system
    (`openPath`) and a drop replace the document under the dialog.
    - Ids restart per document (`s1`, `p1`). Export STL… or Export All then
      writes the new document's `s1` under the old shape's name.
    - A Puzzle right-click menu, or Set range, acts on the new problem.
    - Only `imports.ts` and the Notes dialog check `store.generation` again.
  - *Change:*
    - While `dialogOpen()` (`shell/dialog.ts:102`) or a popover is open,
      Menus' command handler and the drop handler hold document commands.
      They run when the dialog closes: add a small `onDialogsClosed(cb)` to
      `dialog.ts`, and reuse Menus' `waiting` list.
    - `closePop()` runs in `setTab` and on every new generation.
  - *Tests:* `files.e2e.ts`: open Export STL, send Open Recent through the
    app's command channel (see how the existing specs drive menus), and
    check that the open waits until the dialog closes; a unit test of the
    queue.

- [ ] **6.3 Dialogs and edits that keep a shape id across a change of document** (bug; S)
  - *Where:*
    - `web/src/renderer/entities/workspace.ts`:
      - `editImage` (451-480) waits up to 5 s for the pictures, then uses the
        shape it captured before;
      - `editBorderImage` (495-524; 508 reads the current `cells.all` after
        the wait);
      - the 3D edit's apply (720) reads `store.selected` after an await.
    - `web/src/renderer/shell/piece-images.ts:251`.
    - `web/src/renderer/entities/edit3d.ts` (150-163: its Target has no
      shape id).
    - The image dialog (`entities/image-dialog.ts`) stays open across
      File ▸ Open; nothing closes it.
  - *Problem:* ids repeat in each document (`state/store.ts:57-60`), so Apply
    can change the new document's shape that has the same id. A 3D edit can
    apply a cell worked out on one shape to another.
  - *Change:* capture `store.generation` with the shape id when the dialog
    opens or the edit starts. After each await, refuse when the generation
    differs: close the dialog with "The document changed; nothing was
    applied". Carry the shape id in Edit3D's Target, and use it instead of
    `store.selected`.
  - *Tests:* `piece-images.e2e.ts`: open the image dialog, open another
    document through the command channel, press Apply; the new document is
    unchanged.

- [ ] **6.4 One modal system: the confirm dialogs and the bulk range dialog** (bug, combine; S)
  - *Where:*
    - `web/src/renderer/shell/confirm.ts` (`askConfirm`), called from
      `puzzle/slide-card.ts:422`, 436, `solver/workspace.ts:1730`, 1753, 1843
      and `play/workspace.ts:391`, 433, 504.
    - `puzzle/pieces-card.ts:270-348`: the bulk range dialog, with its own
      DOM.
    - `shell/settings.ts:1152-1173`: a copy of `dialog.ts:233`'s `trapTab`.
    - `shell/log-files.ts:238-295`: a menu of its own, because `#pop`
      (z-index 20) sits under `.dlg-scrim` (90).
    - `shell/app.ts:399`: `setModal(() => settingsOpen() || dialogOpen())`
      does not see `askConfirm`.
  - *Problem:*
    - Behind "Delete 3 solutions?" or "Restart the game?", Ctrl+Z, Ctrl+1 to
      4, Del and Ctrl+O still act. The restart callback then restarts the
      game it captured when it asked.
    - Tab walks out of the confirm.
    - The bulk range dialog moves no focus into itself. Enter on the
      still-focused "Set range for all pieces" button opens a second dialog;
      the first is orphaned, and Esc closes only the second.
  - *Change:*
    - Rebuild `askConfirm` on `openDialog` (`dialog.ts:137`) with its two
      buttons, so `modal()` sees it.
    - Rebuild the bulk range dialog on `openDialog`, with the id `pzBulk`
      and an answer handler that keeps it open for the steppers.
    - Export `trapTab` and use it in `settings.ts`.
    - Give the log manager's menu the popover helper, stacked above an open
      dialog (a class on `#pop` while a dialog is open).
  - *Tests:* the e2e helper `confirm` (`web/tests/e2e/solver.ts:241`) and
    `stacking.e2e.ts:668` match the confirm's markup; move them to
    openDialog's markup. Add an e2e test: with "Delete 3 solutions?" open,
    Ctrl+Z changes nothing.

- [ ] **6.5 The Manage menu offers Analyse on sliding and stacking puzzles** (bug; S) ✓
  - *Where:* `web/src/renderer/solver/solutions-card.ts`: `MENU` (44-45, one
    HTML string); the filter at 787,
    `MENU.replace(/<button data-k="ad[cam]">.*?<\/button>/g, '')`; the
    player's rule (560: `family === 'brick' && canDisassemble`). In the core,
    `Document::analyseStart` has no check for a tilted tray.
  - *Problem:*
    - The regex has matched nothing since 98cc596 put `title=` after
      `data-k`, so sliding and stacking puzzles show the three Analyse items,
      which the engine refuses. Spheres have no disassembler.
    - A Tilted Square puzzle is on the Brick grid, so a real analysis runs
      there, while the player hides its take-apart button for it.
  - *Change:*
    - Build the menu from a table (`k`, label, title, key text,
      `needsTakeApart`), filtered by one exported rule `canTakeApart(ws)`
      that the player uses too. No regex over HTML.
    - In the core, `analyseStart` refuses a tilted puzzle with the reason,
      as the player does.
  - *Tests:* `sliding.e2e.ts` and `tilted.e2e.ts`: open Manage and expect no
    `[data-k^="ad"]`. On a brick puzzle all three are there.

- [ ] **6.6 Play: Pause then Play starts a second playback chain; one failed step stops Undo and Redo** (bug; S)
  - *Where:* `web/src/renderer/play/workspace.ts`: pause and play (321-330);
    the step chain (345-382: `stepping = stepping.then(step).finally(...)`).
  - *Problem:*
    - `pause()` only clears `playing`. The old chain's pending
      `setTimeout(next, 160)`, or a running step's `.then`, sees `playing`
      true again and goes on beside the new chain. Playback then runs twice
      as fast, and moves jump without being drawn.
    - The chain keeps a rejection. After one throw every later step is a
      no-op, and each `void queue()` is an unhandled rejection.
  - *Change:*
    - A playback counter: play bumps it and captures it, `next()` stops when
      it differs, and pause bumps it.
    - `.catch(…)` before `.finally`, reporting the error, so the chain
      survives a failure.
  - *Tests:* `play.e2e.ts` and `stack-play.e2e.ts`: press Space three times
    quickly. Each move is drawn once and in order. Check order and count,
    not time: only the timed project may assert on time.

- [ ] **6.7 Late replies applied after the drawer closed or after a newer load** (bug; S)
  - *Where:*
    - `web/src/renderer/explore/explorer.ts` (383-428, 494-503): an
      assembler handle that arrives after the drawer was closed is stored,
      and the engine keeps that puzzle copy. The file's header (24-26) says
      handles close with the drawer.
    - `web/src/renderer/solver/workspace.ts` `load()` (1025-1038): it writes
      board, savedSearch, slideError and continueStartsOver, and restyles the
      player, before checking `n === this.loads` at 1074.
  - *Change:*
    - Explorer: a token that close bumps, checked after each await; a late
      handle is closed (find the call that releases one in `explorer.ts`).
    - Workspace: test `n === this.loads`, and the generation, before writing
      any state.
  - *Tests:* a unit test: close the drawer before the open's reply comes;
    the handle is closed. `explore.e2e.ts` as before.

- [ ] **6.8 The ARIA label attribute was misspelled by the spelling commit** (bug; S) ✓
  - *Where:* `web/src/renderer/index.html:529` (`aria-labeledby="bigTipMsg"`,
    from 0bd5a36); `web/tests/unit/spelling.test.ts` (`WORDS`, line 47, and
    the matcher, `isBreak()` at 117-128).
  - *Problem:* `aria-labeledby` is not an ARIA attribute, so the big-grid
    alert has no accessible name. The spelling test would flag the right
    spelling.
  - *Change:* restore the ARIA spelling, `aria-labelledby`, and teach the spelling test that `labelled` followed at once by `by` (the ARIA attribute's name, which no one can rename) is allowed. british-ok
  - *Tests:* `spelling.test.ts`, with a case for the attribute.

- [ ] **6.9 The Flat style leaks edge geometries on every refresh** (bug; S) ✓
  - *Where:* `web/src/renderer/view3d/pieces.ts`:
    - `set()` (245-282) calls `shapeDraw()` for each laid item and adds
      `new LineSegments2(d.edges, …)`;
    - `shapeDraw()` (564-571) disposes `have.edges` of a shape it already
      has, and builds new ones.
    Also `web/src/renderer/view3d/board.ts`: `BoardScene.clear` (187-193)
    and `faceUniforms()` (158).
  - *Problem:*
    - With k items of one shape (the selected piece and its copies), each
      item's `shapeDraw` disposes the edges the item before it uses. Items 1
      to k-1 then draw disposed geometries.
    - three.js uploads those again when they are drawn, and nothing disposes
      them afterward, so their GPU buffers stay.
    - Each refresh leaks k-1 geometries per shape, and builds k where one
      would do.
    - `BoardScene.clear` never disposes the palette texture of the board's
      colors.
  - *Change:* in `set()`, build each shape's edges once per call (a map for
    the call, or rebuild only when `m.built` changed), and dispose the old
    ones after `clearItems()`. Dispose the palette texture in
    `BoardScene.clear`.
  - *Tests:* a unit test (`web/tests/unit/renderer/`) that counts geometry
    disposals and live geometries across two `set()` calls with a repeated
    shape (three.js geometry objects work without WebGL); `puzzle.e2e.ts`
    (`data-scene`) and the Flat screenshots.

- [ ] **6.10 Two mesh loaders let a stale mesh land** (bug; S)
  - *Where:* `web/src/renderer/view3d/trays.ts:151` and
    `web/src/renderer/view3d/board.ts:285` return when the key equals the one
    shown, before they bump `loads` (154, 288). `view3d/view.ts:865-868` and
    956-959 guard against this.
  - *Problem:* a mesh still in flight for another key lands over the right
    one, for example when the style is toggled and back (or the theme, for
    the trays).
  - *Change:* bump `loads` before the early return in both, so a reply in
    flight is stale. The shared loader of 12.9 later replaces all four.
  - *Tests:* `sliding.e2e.ts` (`data-tray`), `stacking.e2e.ts` (`data-board`).

- [ ] **6.11 The 3D view stays blank after the GPU resets; Export STL previews keep WebGL contexts** (bug; S)
  - *Where:*
    - `web/src/renderer/view3d/renderer.ts` (63-68, 186-194): there is no
      `webglcontextlost` or `webglcontextrestored` listener anywhere in
      `web/src`.
    - `web/src/renderer/export/stl-preview.ts` (55, 106-114): `dispose()`
      calls `gl.dispose()`, not `forceContextLoss()`.
  - *Problem:*
    - three.js restores the context and uploads everything again, but only
      on the next render. Rendering is on demand, so after a GPU reset the
      view stays blank until something changes. Today the needless
      pointer-move renders of 10.1 hide this.
    - Each STL preview's context lives until garbage collection. Chromium
      keeps about 16 live contexts and drops the oldest, which is the main
      view's.
  - *Change:*
    - On the main canvas, `webglcontextlost` calls `preventDefault()` so the
      context can come back, and `webglcontextrestored` asks for a render
      and redraws the ViewCube.
    - In `stl-preview.ts`'s `dispose()`, call `this.gl.forceContextLoss()`
      after `dispose()`.
  - *Tests:* an e2e test that uses `WEBGL_lose_context` (`loseContext()`,
    then `restoreContext()`) on the main canvas and checks that a frame was
    drawn afterward (a data attribute the renderer sets per frame; add one
    if there is none).

- [ ] **6.12 The image dialog keeps its pictures after it closes** (bug, memory; S)
  - *Where:* `web/src/renderer/entities/image-dialog.ts`:
    - 924-929: the resize listener is removed only when a later resize finds
      the dialog gone;
    - 133, 136 and 154: each chosen file is decoded twice, and the first
      bitmap is never closed;
    - 272-273: each Manual drag move resets `canvas.width` and `height`,
      which reallocates the canvas.
  - *Change:*
    - In `openDialog(...).finally(...)` (it settles on every way of closing,
      `dialog.ts:129` and 200), remove the listener and `close()` the
      dialog's own bitmaps. Never close one that the picture cache
      (`shell/piece-images.ts`) still serves.
    - Decode each file once.
    - Set the canvas size only when it changes.
  - *Tests:* `piece-images.e2e.ts`.

- [ ] **6.13 The explorer drops piece names; an engine restart flashes an error** (bug, combine; S)
  - *Where:*
    - `web/src/renderer/explore/explorer.ts:67-125` has a copy of
      `problemPieces` without `name`, so the explorer's 3D labels lose the
      piece names (compare `solver/player.ts:698-723` and 456).
    - The "quiet error" test (superseded, not_found, disconnected) is
      written seven times: `solver/workspace.ts:970` and 1041,
      `solver/player.ts:769`, `explore/explorer.ts:1258`,
      `solver/stats-panel.ts:127`, `puzzle/workspace.ts:570` and
      `entities/shape-cells.ts:177`.
    - `solver/pager.ts:201-206` lacks `disconnected`.
  - *Change:*
    - Export `problemPieces` from one module (`solver/player.ts` or a
      `state/` helper) and use it in the explorer.
    - One `isQuietError(e)` in `state/` (or next to `engine-proxy.ts`),
      used at the eight sites, the pager included.
  - *Tests:* `explore.e2e.ts`: the labels show the names; a unit test of
    `isQuietError`.

**Gates for stage 6:** `just app-check`, `just app-test`, and the e2e specs
the items name, one file at a time.

## Stage 7: main-process bugs

- [ ] **7.1 A crashed or hung renderer leaves a dead window** (bug; S) ✓
  - *Where:* `web/src/main/index.ts`: window creation (388, 688) and the
    window's events (481-578). There is no `'render-process-gone'` or
    `'unresponsive'` handler; `isCrashed()` is used only for the close (502).
    The page re-attaches on a reload (`renderer/shell/rescue.ts:92`).
  - *Problem:* the engine still holds the unsaved document and any running
    solve, and a reload would re-attach to both. But the user can only close
    the window, and nothing is logged.
  - *Change:*
    - On `'render-process-gone'` with any reason other than `'clean-exit'`:
      log the reason and exit code at error level, then call
      `contents.reload()`, at most three times in 60 s per window. Past that,
      ask through a native dialog: Reload or Close.
    - On `'unresponsive'`: a native dialog, Wait or Reload. Close it on
      `'responsive'`.
  - *Tests:* an e2e test that calls `webContents.forcefullyCrashRenderer()`
    (through `electronApp.evaluate`) and checks that the window reloads with
    its document (the rescue and re-attach path).

- [ ] **7.2 Main-process failures never reach the app log** (bug; S)
  - *Where:*
    - `web/src/main/app-log.ts:171-176`: `EngineLines.end()` is unit-tested
      but never called, so an engine's last stderr text without a newline (a
      crash message) is lost.
    - `web/src/main/index.ts:427-437`: nothing subscribes to
      `host.onProblem`. Its problems only reach `console.error`, which a
      packaged app does not show:
      - `bad-frame`, which kills the engine;
      - `lost-reply`;
      - `line-too-long`;
      - a throwing handler.
    - `web/src/main/state-store.ts:235` and `index.ts:561`: a failed
      `state.json` write goes to the console only.
    - `web/src/engine/node-transport.ts:120-139`: `onExit`, `onData` and
      `onStderr` are single-slot setters, as in the wasm and recording
      transports.
  - *Problem:* the log then says "Engine Stopped … SIGKILL" with no reason.
    The obvious fix, `t.onExit(() => lines.end())`, would silently replace
    the client's own exit handler.
  - *Change:*
    - `teeStderr` (`app-log.ts:183`) wraps `onExit` too, and calls
      `lines.end()` before passing the exit on.
    - Subscribe to `host.onProblem` and log each problem at warn level with
      its kind and detail.
    - Log the state store's and the engine start's failures through the app
      log (2.2 covers the engine start's detail).
    - A second subscription to a single-slot setter throws ("onExit is
      already set") in all three transports, so the trap shows in tests.
  - *Tests:* `app-log.test.ts` (end on exit; a problem is logged);
    `host.test.ts`; a transport test that a second `onExit` throws.

- [ ] **7.3 The two cache modules disagree about lock files** (bug, combine; S)
  - *Where:*
    - `web/src/main/cache-folder.ts` (103, 207-230, 251-255, 387) skips every
      `*.lock` (since 9a443f2).
    - `web/src/main/cache-files.ts` (44-62, 217-234, 491) skips only
      `solve-<key>.lock` and `<key>.lock` (since 56ebb75).
    - The error-code helper is copied a third time in
      `web/src/main/export-file.ts:43`.
  - *Problem:* these are written in both modules:
    - the search-folder name patterns (`slide-<hex16>` and `<hex16>`);
    - the lock rule;
    - the SOLVING message;
    - the recursive folder walk;
    - the error-code helper.
    `cache-files.ts` lists and sweeps any other `.lock` file, which can
    remove a lock that another solve holds (decision 0035).
  - *Change:* one `web/src/main/search-folders.ts` that holds:
    - the name patterns;
    - `isLockFile(name)`, true for every `*.lock` (the safe rule);
    - the SOLVING text;
    - the walk;
    - the error-code helper.
    Both cache modules and `export-file.ts` import it.
  - *Tests:* `cache-folder.test.ts` and `cache-files.test.ts`, plus a new
    case: a `foo.lock` is never listed or swept.

- [ ] **7.4 One way to replace a file in the main process** (bug, combine; M)
  - *Where:* five writers that replace a file through a temporary one:
    - `web/src/main/state-store.ts:228-237`;
    - `rescue.ts:58-67`;
    - `app-log.ts:344-345`;
    - `cache-files.ts:127-142`;
    - `export-file.ts:83-114` (`writeFileSafely`).
  - *Problem:*
    - Some of them are synchronous and some not. All use a fixed `<file>.tmp`
      name, and none syncs before the rename.
    - `writeFileSafely` first deletes any `<target>.tmp`. So when two windows
      export STL into one folder at once, each deletes the other's temporary
      file, and the first rename installs the second's partial bytes.
  - *Change:* one `replaceFile(path, data, options)` in `web/src/main/`:
    - async;
    - a unique temporary name beside the target
      (`<name>.<pid>.<random>.tmp`);
    - write, `FileHandle.sync()`, rename;
    - on an error, remove the temporary file.
    Add a synchronous twin only where a caller must be synchronous (check
    the state store at quit). The exports keep their link and permission
    handling on top of it.
  - *Tests:* the export, state-store and rescue unit tests; a new test: two
    `replaceFile` calls to one target at once both settle, and the target
    holds one of the two contents whole.

- [ ] **7.5 The demo reports a wrong error code** (bug; S)
  - *Where:* `web/src/demo/engine-bridge.ts:89` sends -32602 (invalid_params)
    with errc `'invalid_argument'`, whose code is -32001.
    `web/src/main/engine-ipc.ts:75` hard-codes a code.
  - *Change:* take the code from `ERROR_CODES` (`api.generated.ts`) by errc
    at both sites.
  - *Tests:* `bridge.test.ts`.

**Gates for stage 7:** `just app-check`, `just app-test`, the e2e specs the
items name.

## Stage 8: test and build speed; tests that cannot clash

- [ ] **8.1 Balance the test shards** (speed; S) ✓
  - *Where:* `tests/CMakeLists.txt` (79-89) makes eight shards of `"~[slow]"`
    and eight of `"[slow]"`, each a slice of the shuffled case list.
    `justfile:32`: test-tsan runs one unsharded process.
  - *Problem:* `build/dev/Testing/Temporary/CTestCostData.txt` averages
    about 47 runs:
    - `unit-slow-1` takes about 289 s, and the whole dev run about 293 s.
      One shard sets the length of every `just test-all`.
    - The unit shards take 44 to 109 s (about 600 s in all), so `just test`
      waits about 109 s for work that ten cores finish in about 60 s.
    - In the ASan build `unit-slow-1` took 1,005 s, the whole run
      (`build/tilted-gate/b-test-asan.log`).
    - Only about 41 of 836 cases are `[slow]`.
  - *Change:*
    1. Register each `[slow]` case as its own ctest test with Catch2's CMake
       module: `catch_discover_tests(bt_tests TEST_SPEC "[slow]" TEST_PREFIX "slow: " …)`
       (check that the Catch2 the build fetches provides it). Give it
       `WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}` and the label `slow`.
       ctest's cost data then starts the longest first.
    2. Raise `BT_TEST_SHARDS` to about 24 for the `"~[slow]"` shards.
    3. If a case still takes most of a shard (run
       `bt_tests "[slow]" --durations yes`), split it into sections or cases
       that check the same things.
    4. Shard `test-tsan`'s `[parallel]` cases the same way (`justfile:32`).
  - *Keep:* the shuffle seed of the shards; `just test` still skips `[slow]`.
  - *Verify:* run `just test-all` twice and write the wall times before and
    after in the Done line. Run `just test-asan` once as well. It is long, so
    say so before it starts, and run nothing heavy beside it.

- [ ] **8.2 A new tag recompiles every file** (speed; S)
  - *Where:*
    - `core/CMakeLists.txt:112`:
      `target_compile_definitions(btcore PUBLIC BT_VERSION_STRING="${PROJECT_VERSION}")`;
    - `core/shapelogic/base/version.h:9`:
      `inline constexpr std::string_view kVersion = BT_VERSION_STRING;`;
    - `cmake/ProjectVersion.cmake:40-45`;
    - the users of `bt::kVersion`: `cli/options.cpp:277`, `engine/main.cpp`,
      `engine/methods_engine.cpp`, `python/src/module.cpp:49` and
      `tests/unit/base/test_error.cpp`. Assembler 0's and 1's `kVersion`
      are their own saved-position versions, not this one.
  - *Problem:* the define is on all 204 compile commands of `build/release`,
    though only 5 files read it. There were 12 tags between 2026-10-31 and
    11-23, and each rebuilt everything in every preset.
  - *Change:* a new `core/shapelogic/base/version.cpp` with
    `std::string_view versionString()` returning `BT_VERSION_STRING`. The
    define is set on that one file only (`set_source_files_properties(…
    COMPILE_DEFINITIONS …)`). `version.h` declares the function, and the
    five users call it.
  - *Verify:* in a scratch build folder, change the version input (see
    `ProjectVersion.cmake` for an override; never make a git tag) and check
    that one file recompiles.

- [ ] **8.3 One scratch-folder helper for the C++ tests** (stability, combine; S)
  - *Where:*
    - 26 fixed folder names in the shared temp folder:
      - `tests/unit/solve/test_sliding_session.cpp` (15
        `bt_test_slide_session_*` folders, 357-1780);
      - `tests/unit/app/test_document.cpp:73` and 703;
      - `test_analysis_files.cpp:230` and 254;
      - `tests/unit/io/test_puzzle_file.cpp:419` (and its `freshDir` at 54);
      - `tests/unit/base/test_memory_share.cpp:85`;
      - `tests/unit/app/test_document_solving.cpp:255`.
    - Eight helpers that make scratch folders:
      - `test_slide_key.cpp:40` (the same as `test_sliding.cpp:88`);
      - `test_stacking_session.cpp:44`;
      - `test_document_stacking.cpp:45`;
      - `test_file_lock.cpp:18`;
      - `test_stacking.cpp:51`;
      - `test_roundtrip.cpp:28`;
      - `test_engine.cpp:339`, 421 and 603.
  - *Problem:* the temp folder is per user, so every session shares it. Each
    fixed folder is removed at the start of its test. That is safe within
    one ctest run, but two `bt_tests` processes at once (dev and release,
    or ASan in one session and a test in another) wipe each other's folders
    or run into decision 0035's lock.
  - *Change:*
    - `tests/unit/scratch_dir.h`: `class ScratchDir` with
      `explicit ScratchDir(std::string_view tag)`, `path()` and a destructor.
      The folder is the temp folder / tag-pid-8 random hex digits; it is made
      on construction and removed in the destructor, ignoring errors.
    - Use it everywhere.
    - Fold the three identical `addSized` copies at the same time (grep
      `addSized`).
  - *Verify:* `grep -rn temp_directory_path tests/unit` matches only
    `scratch_dir.h`. `just test-all` passes while a second `bt_tests` from
    another build folder runs at the same time.

- [ ] **8.4 Long test runs use live binaries and folders that other sessions rebuild** (stability; M)
  - *Where:*
    - `web/package.json:22-27`: `build`, `e2e`, `e2e-timed`, `dist` and
      `docs-shots` all rewrite `web/out`.
    - `web/tests/e2e/app.ts:29-39` and 124, and `files.e2e.ts:1183`, launch
      `out/main/index.js` and `build/release/engine/shapelogic_engine`
      directly.
    - `web/tests/helpers/engine.ts:18-25`: vitest starts an engine per test
      from `build/release`.
    - `tests/oracle/oracle.py` (1209-1210, 1390-1391, 540, 837, 1187): fixed
      `work/` subfolders are removed or rewritten on each run, and
      `work/*_old` is recompiled on each run.
    - `tests/gate.sh:49-51`: `rm -rf build/gate`.
    - `justfile:13-15`: no lock around `cmake --build`.
  - *Problem:* the user runs several sessions on one checkout.
    - A `just build release` or an e2e build in another session during a run
      swaps or half-writes these files. The result is launch failures, or
      old and new engines mixed in one run.
    - Two oracle runs clobber each other's work folders.
    - Two gates wipe each other's logs (one session wrote
      `build/tilted-gate` by hand to avoid it).
  - *Change:*
    1. The vitest and e2e global setups copy the engine into a temp folder
       for the run, point `BT_ENGINE` at the copy, and remove it at the end.
    2. electron-vite's `outDir` comes from `BT_OUT_DIR` (default `out`), so
       an e2e build goes to a folder of its own. `web/tests/e2e/app.ts`
       exports one `APP_MAIN` for the three launch sites.
    3. `oracle.py` uses `work/run-<pid>/` for its outputs (keep a `--work`
       flag), and builds the `*_old` drivers once into a cache keyed by
       their sources' hash.
    4. `gate.sh` takes `GATE_OUT` (default `build/gate`).
    5. The build recipe takes a lock per preset (`build/<preset>.lock`: a
       mkdir lock works on macOS and Linux; Windows builds are in CI), so two
       builds of one preset wait for each other.
  - *Verify:* `just build release` while a vitest engine run goes; both
    pass.

- [ ] **8.5 One set of e2e helpers** (stability, combine; S)
  - *Where:*
    - `settled` at `web/tests/e2e/editor.ts:95`, `files.e2e.ts:113`,
      `keymap.e2e.ts:94` and `windows.e2e.ts:52` (`===` in some, `>=` in
      others);
    - `tmpLog` at `explore:52`, `sliding:81` and `solver:58`;
    - `attr` four times, `docOf` three times, `windowCount` twice;
    - PNG writers at `piece-images:24`, `tray-border:23` and
      `web/tests/docs/guide-pages.docs.ts:396`;
    - the engine path lookup four times (`web/tools/record-sessions.ts:51`
      uses `??`, so an empty `BT_ENGINE` becomes a path).
  - *Problem:*
    - `counted` (`app.ts:443`) ignores the refusals a busy machine causes.
      Five places still use the October filter
      `errc !== null && errc !== 'superseded'`, so they fail on exactly
      those races:
      - `solver.e2e.ts:65` (used five times);
      - `sliding.e2e.ts:1428` and 1691;
      - `shapes.e2e.ts:200`;
      - `puzzle.e2e.ts:42`.
    - `files.e2e.ts:483` sleeps 4,500 ms to outlast `CLOSE_TAKEN_MS = 4000`
      (`web/src/main/index.ts:145`), with nothing tying the two.
  - *Change:*
    - Export these helpers once from `web/tests/e2e/app.ts` (or a
      `helpers/e2e.ts`).
    - Add `web/tests/helpers/png.ts` and `helpers/engine-path.ts`.
    - Use `counted` at the five places.
    - Export `CLOSE_TAKEN_MS` and derive the sleep from it (plus 500 ms).
  - *Verify:* run each touched spec on its own.

- [ ] **8.6 Tests and sources registered by hand** (trap; S)
  - *Where:*
    - `tests/CMakeLists.txt:4-69`, the `bt_tests` source list: 39 commits
      since September, and some lines out of order at 17-20;
    - `tests/cli/test_cli.py:379-380`, a tuple of the eight test functions;
    - `core/CMakeLists.txt`'s source list: 29 changes since September, all
      one-line additions.
  - *Problem:* a new `test_*.cpp` that is missing from the list is never
    built, run or checked. A new `test_x` that is missing from the tuple
    never runs. Nothing reports either (everything is registered today).
  - *Change:*
    - `bt_tests`: `file(GLOB_RECURSE … CONFIGURE_DEPENDS tests/unit/*.cpp)`
      (`tests/unit` holds no helper `.cpp`).
    - `test_cli.py` collects the `test_*` functions from `globals()`.
    - `core/` keeps its explicit list, plus a configure-time check that every
      `.cpp` under `core/shapelogic` is listed (a glob compared with the
      list; FATAL_ERROR names the missing file).
  - *Verify:* add a scratch test file and configure: it is built. Then
    delete it.

- [ ] **8.7 `just check` skips two folders** (trap; S)
  - *Where:* `justfile:106`, the file pattern (unchanged since stage 0).
    `python/src` (5 files) and `tests/tools` (6 files) are in
    `compile_commands.json` but never checked.
  - *Change:* add both to the pattern, and fix what clang-tidy reports (or
    suppress with reasons, as AGENTS.md says). `just check` must be green
    again in the same change.

**Gates for stage 8:** `just test-all` (twice, timed), `just app-test`, the
e2e specs touched one by one, `just check`, the CLI tests, one oracle tier
run in a work folder of its own.

## Checkpoint D (Fable): review of stage 8

The procedure is in "Fable checkpoints". What to look at:
- **Nothing stopped running.** Compare these lists before and after the
  stage:
  - `bt_tests --list-tests`, all cases and `[slow]`;
  - `ctest -N` in each preset;
  - the vitest files and test counts;
  - `test_cli.py`'s functions;
  - the e2e spec list.
  Every test that ran before runs after.
- **The time limits.** They never stop a normal run (compare with
  CTestCostData). A hang is stopped, and the gate's table says "timeout".
- **Builds that clash.** Two builds of one preset wait for each other, and a
  vitest engine run passes while `just build release` runs.
- **1.2 still works.** `BT_REQUIRE_ALL=1` still fails a run without an
  engine.

## Stage 9: CI and release pipeline (D4: the agent decides)

- [ ] **9.1 macOS builds on a pinned image with a deployment target** (stability; S)
  - *Where:* `.github/workflows/release.yml:272` and `staple.yml:39`
    (`macos-latest`). `release.yml:291-294` builds macOS with warnings as
    errors, unlike Linux (196) and Windows (251). No
    `CMAKE_OSX_DEPLOYMENT_TARGET` is set anywhere; the local build shows
    `minos 26.0`.
  - *Problem:* when GitHub moves the label, a new Xcode's warnings fail the
    build at tag time. The engine's minimum macOS is whatever the runner
    has, while the Electron app itself starts on older systems.
  - *Change:*
    - Pin `runs-on` in `release.yml` and `staple.yml` to an explicit image:
      the newest stable macOS image GitHub lists at the time, not a beta.
      Select that image's default Xcode explicitly
      (`sudo xcode-select -s /Applications/Xcode_<version>.app`), so a change
      of image or Xcode is a change in this repo.
    - Set `CMAKE_OSX_DEPLOYMENT_TARGET` in the macOS release presets
      (`CMakePresets.json`) to the lowest macOS the app's Electron version
      supports; find it in the release notes of the Electron version in
      `web/package.json`. Then the engine never needs a newer system than
      the app.
      - Some libc++ parts need a newer system at run time. If the code uses
        them, choose the lowest version it builds for without changing any
        result.
      - Write the version and the reason in the Done line, and tell the
        user which macOS versions the release then runs on.
    - A CI step checks the engine's and the CLI's `minos`
      (`vtool -show-build` or `otool -l`).
  - *Risk:* library features newer than the target become compile errors,
    which show where the target must be.

- [ ] **9.2 Release jobs that got no runner are run again** (stability; S)
  - *Where:* `.github/workflows/release.yml` (269-277, 462-471); no workflow
    listens on `workflow_run`.
  - *Problem:* today a macOS job that got no runner is re-run by hand, about
    twelve times, ten minutes apart.
  - *Change:* a `.github/workflows/retry.yml` on `workflow_run` (workflows:
    Release; types: completed). When:
    - the run failed;
    - its attempt number is below 12;
    - every failed job ran no steps (ask the jobs API);
    it sleeps 600 s and runs `gh run rerun <id> --failed`, with permission
    `actions: write`. The publish job runs again with it.
  - *Risk:* a real build failure has steps, so it is never retried.
  - *After:* once a release has used it, tell the user that the manual
    re-runs are no longer needed.

## Stage 10: front-end speed and memory

Rule 7 holds for every renderer item here.

- [ ] **10.1 Every pointer move over the 3D view renders a full frame** (speed, bug; M)
  - *Where:*
    - `web/src/renderer/view3d/view.ts`:
      - the pointer handlers (1590-1606);
      - `ndcAt` (1531-1540);
      - the picking (1450-1529);
      - `pieceOnScreen` (1263-1314).
    - `web/src/renderer/entities/edit3d.ts` (136, 172-214: `show()` rewrites
      the Edit button and always calls `setPreview`, which asks for a
      render).
    - `web/src/renderer/entities/toolbar.ts:149-169`.
    - `web/src/renderer/view3d/viewcube.ts:367-384` (each redraw reads
      `getComputedStyle` about ten times).
    - `web/src/renderer/entities/workspace.ts:246` (Edit3D is the view's edit
      host on every tab).
  - *Problem:*
    - On the Entities tab each move calls `ndcAt` three times. Each call does
      a `getBoundingClientRect`, a full `scene.updateMatrixWorld` and a new
      Raycaster.
    - `Edit3D.show` then rewrites the Edit button and asks for a render. The
      next frame is a full WebGL render, a ViewCube redraw and four dataset
      writes.
    - With the movement explorer open, every frame runs `pieceOnScreen` for
      each piece (`frame()` plus `computeBoundingBox` over its vertices,
      `view3d/solution.ts:377`).
    - A bug as well: on the Solver tab, `setCursor('')` (`edit3d.ts:194`)
      overwrites the Alt-hover `grab` cursor that `view.ts:1603` sets just
      before (the call comes from 1605). That cursor never shows.
  - *Change:*
    - Call `edit.hover` only on the Entities tab.
    - `Edit3D.show` compares {button, cursor, ghosts} with its last call and
      returns when nothing changed.
    - `setPreview` asks for a render only when the preview changed.
    - Keep one Raycaster, and cast one ray per event.
    - The ViewCube redraws only when the camera's orientation, zoom or
      projection, the theme or the hover changed (cache the computed style
      per theme).
    - Work out the pieces' screen positions only when the poses or the camera
      change.
  - *Order:* do 6.11 (the context restore) first: these needless renders hide
    the blank-after-reset bug.
  - *Tests:*
    - the e2e specs `picking`, `entities` (`data-preview`), `view`, and
      `solver` and `explore` (`data-piece-screen`), and the screenshot tests;
    - a unit test: pointer moves with nothing under the pointer ask for no
      render.

- [ ] **10.2 `lookChanged` runs at least three times per snapshot, and rebuilds the overlays each time** (speed; M)
  - *Where:*
    - `web/src/renderer/view3d/view.ts`:
      - `reload()` calls it twice (880, 916, 925);
      - `documentChanged` → `showRodSet` → `setBoard(null)` calls it again
        for every puzzle that is not stacking (699-704);
      - `lookChanged` itself is at 1104-1204;
      - `floorOfMesh` scans the voxel array (625).
    - `web/src/renderer/entities/workspace.ts:339` → 881.
    - `web/src/renderer/view3d/overlays.ts:133-196` and `view3d/preview.ts:116-183`.
  - *Problem:*
    - Each run clears and remakes up to five line geometries and materials,
      plus the slab. During a stroke that happens for every engine batch.
    - `Preview.set` does the same on each hover change: four GPU objects per
      ghost cell.
    - three.js deletes a program when the last material using it is
      disposed, so clearing and then creating can force a shader relink on
      the next frame.
  - *Change:*
    - Keep the overlay objects and update their geometry and uniforms,
      keyed on (extent, slab, colors).
    - Key the preview on its ghosts, and cache its materials by (fill,
      alpha, dashed).
    - `setBoard(null)` does nothing when the board is already null.
    - One `lookChanged` per snapshot.
  - *Tests:* the screenshot tests, `entities.e2e.ts`, `view.e2e.ts`.

- [ ] **10.3 The shape view fetches its mesh again at every revision** (speed; S)
  - *Where:* `web/src/renderer/view3d/view.ts`:
    - `reload` (878, 884, 895) keys and asks for the mesh at the document's
      revision;
    - `shapeRevision` (930-938) is used at 971, `trays.ts:158` and
      `board.ts:291`;
    - `web/src/renderer/view3d/meshcache.ts:49`.
  - *Problem:* any change (a rename, a weight, a problem edit, a solve's end)
    asks for `shape.mesh` again, rebuilds it and uploads it, even while the
    Puzzle or Solver tab hides the shape view.
  - *Change:*
    - Key it by `shapeRevision(shape.id)`.
    - Keep `#vp[data-revision]` as the snapshot's revision, and set it on the
      early return too.
  - *Risk:* this relies on the engine naming every shape whose mesh changed.
    The pieces already rely on it, and tray changes name every shape
    (`core/shapelogic/app/document_tilted.cpp:402-404`).
  - *Tests:* `perf.e2e.ts` expects exactly one build when a shape is
    selected; also check the SVG export.

- [ ] **10.4 Cards re-render whole on every snapshot, keyed by hand-written lists** (speed, trap; M)
  - *Where:*
    - `web/src/renderer/entities/workspace.ts` (315-357, 385-394);
    - `entities/shapes-list.ts` (109-200);
    - `entities/editor-card.ts` (192-293);
    - `entities/grid2d.ts` (440, 503-531);
    - `entities/tilt-card.ts:54-59` (`tiltKey` lists nine tray fields by
      hand).
  - *Problem:*
    - During a stroke, every row of the shapes list and seven parts of the
      editor card are rewritten, including a `scrollIntoView` that forces a
      layout. Neither shows cells.
    - `Grid2D.paint` runs at least three times per reload, and each time it
      resets the picture's pattern with the whole data URL, which can be
      megabytes.
    - A field the tilt card shows but `tiltKey` misses goes stale with no
      error.
  - *Change:* one pattern for every card:
    - render from a plain view model, and skip when `JSON.stringify(model)`
      equals the last one (`borderKey` already does this);
    - rebuild the picture's pattern only when the picture's hash changed;
    - call `scrollIntoView` only when the selection changed.
  - *Keep:* the guards for renaming in place and for typing a disc size.
  - *Tests:* the e2e specs `shapes`, `entities`, `keys` and `piece-images`.

- [ ] **10.5 Hidden tabs reload after every edit** (speed; S to M)
  - *Where:*
    - The Puzzle tab:
      - `web/src/renderer/puzzle/workspace.ts` (133-139, 492-583):
        `documentChanged` runs `load()` and `render()` whichever tab is
        shown. That means `problem.get` and `problem.fit`, on sliding
        puzzles also `sliding.tray` and `shape.get`, and five cards rebuilt.
      - `puzzle/slide-card.ts:161-201`.
    - The Solver tab:
      - `web/src/renderer/solver/workspace.ts` (278-281, 905-940, 981-1053,
        291-298, 633-653): `problem.get`, `solve.status`, and `sliding.tray`
        or `stacking.board` on every reload. `foundOf` reads `problem.get`
        again, plus `solution.page` on sliding and stacking puzzles, for
        every solved problem at every revision.
      - `solver/run-card.ts:163-226`.
      - The live preview stays subscribed and keeps drawing.
    - Play already defers this with a `stale` flag
      (`play/workspace.ts:80`, 120-125, 175-180).
  - *Change:*
    - Puzzle and Solver, when not shown: mark the data stale and return;
      load when the tab is shown (the layout listener).
    - The Solver reads `found` again only for the problems that a
      `document.changed` names with solutions or problems.
    - The live preview stops outside the Solver.
  - *Risk:* e2e helpers wait on `#leftP[data-revision]` and
    `#leftS[data-revision]` after edits made on another tab (`solverSettled`
    and `runEnded`, `web/tests/e2e/solver.ts:41-72`). Either make the helpers
    show the tab first, or keep the attribute up to date cheaply. Decide
    which, and say so in the Done line.
  - *Tests:* the puzzle and solver e2e specs.

- [ ] **10.6 The picture caches never let go** (memory; M)
  - *Where:*
    - `web/src/renderer/shell/piece-images.ts` (30, 140-143, 186-196):
      `ready` holds an ImageBitmap and a base64 data URL for every hash and
      stack key, for the window's whole life, across documents.
    - `web/src/renderer/view3d/material.ts` (185-205): `textures`, a GPU
      texture with mipmaps per key.
    - `view.ts:907`.
    - `view3d/renderer.ts:224`: the `view3d:mesh-build` performance measures
      are never cleared, unlike the render measures.
  - *Problem:* a stack's key includes each picture's `src`. So every applied
    change to a stack, or to a picture smaller than its box, adds a composite
    (up to 2048², about 16 MB) and a texture (about 22 MB).
  - *Change:*
    - On each document change, keep only the hashes and keys that the
      snapshot's `images` and `border.images` name.
    - For the rest, `dispose()` the texture, then `close()` the bitmap.
      Never close a bitmap an open dialog shows.
    - Clear the mesh-build measures as the render measures are cleared.
  - *Tests:* `piece-images.e2e.ts`, `tray-border.e2e.ts`, the play specs.

- [ ] **10.7 A MutationObserver on the whole page rescans the document** (speed; S)
  - *Where:*
    - `web/src/renderer/shell/theme.ts` (103-145, 191-202) watches
      childList, subtree and `title` changes. In Minimal density it runs two
      `querySelectorAll` over the whole document; with "Show tooltips" off
      it scans every `[title]`.
    - `web/src/renderer/shell/tooltip.ts:94` rewrites `title` on every
      pointerover, so each hover fires the observer.
  - *Change:* `tooltip.ts` holds every element's title itself (moved once
    into a data attribute). It shows nothing when tips are off, and the
    control's caption in Minimal density. Then the observer and `data-tip`
    go.
  - *Tests:* `tooltips.e2e.ts`.

- [ ] **10.8 Solver playback does needless work every frame** (speed; S)
  - *Where:*
    - `web/src/renderer/solver/player.ts` (608-668):
      - `track.at()` runs twice per frame (623 and 504);
      - `ensureChunks` builds promises and a Set every frame;
      - `mark()` runs twice.
    - `view3d/view.ts:551-560`.
    - `view3d/solution.ts:384-401`: `poseSolution` writes every pose into
      `#vp` attributes for the e2e tests.
  - *Change:*
    - Work out the poses once per frame.
    - Call `ensureChunks` only when the chunk changes.
    - Write the test attributes only on pause, seek and end. Check the specs
      that read poses: if one reads them while playing, let it pause first.
  - *Tests:* `keymap.e2e.ts` (`data-t`), the solver specs' poses helper,
    `explore.e2e.ts`.

- [ ] **10.9 The app log blocks the main process** (speed; M)
  - *Where:*
    - `web/src/main/app-log.ts`:
      - 376-389: one `appendFileSync` per stderr line of every window's
        engine;
      - 152-168: `EngineLines.carry` grows without limit on a line that
        never ends;
      - 421-504: the Log File Manager's search runs a regex and
        `toLowerCase()` over every line of a file of up to 100 MB on each
        keystroke, and the first page caches the whole file's lines for the
        rest of the run.
    - `web/src/renderer/shell/log-files.ts:96-99`.
  - *Change:*
    - One open file handle per log, with lines written in batches per tick.
      Flush synchronously on quit and on the crash paths.
    - Cap `carry` at 64 KiB, then write it out as a line marked "(cut)".
    - Store each line's level when it is read.
    - When the search text grows, narrow the previous matches instead of
      scanning again.
    - Wait 200 ms after typing before searching.
    - Drop the cache when the dialog closes.
  - *Tests:* `app-log.test.ts`, `logs.e2e.ts`.

- [ ] **10.10 Every engine start launches the engine twice** (speed; S; after 2.2)
  - *Where:* `web/src/engine/node-transport.ts` (`engineVersion()` 19-38, and
    `start()` 96-107: a `--version` launch before each start);
    `web/src/main/index.ts:433`; `engine/methods_engine.cpp:43`
    (`engine.hello` returns the same version).
  - *Change:*
    - After the start, compare hello's version with `__BT_VERSION__`, and
      refuse there.
    - Keep `engineVersion` only for `web/tools/dist.ts` and the tests.
    - Do this after 2.2 has reached users: their reports say which launch
      fails, and this removes the first.
  - *Tests:* `host.test.ts:65-68` (the `/--version/` expectation for a
    missing engine becomes the spawn error).

- [ ] **10.11 Rescue copies without the document crossing IPC** (speed; M; after 4.5)
  - *Where:* `web/src/renderer/shell/rescue.ts` (32, 207, 224) →
    `web/src/main/app-ipc.ts:440-452` → `web/src/main/rescue.ts:58-67`.
  - *Problem:* at most every 2 s per changed window, the whole document goes
    engine → main → renderer → main as base64. Main then decodes it, hashes
    it and writes it with `writeFileSync`, all inside the IPC handler.
  - *Change:*
    - `bt:rescue` carries only the record: the name, the path the copy
      stands for, and whether it is changed.
    - Main asks that window's engine to write the copy itself, through a new
      method `document.saveRescue {path}` (gzip, atomic, through
      `io::writeGzipFile`; rule 10). The order is kept: the invoke arrives
      before any later `bt:call`.
    - The reopen opens the path, with the record's "stands for" and
      "changed". Give `document.open` the parameters `openBytes` has for
      this (check them).
  - *Tests:* the rescue specs in `files.e2e.ts`; a contract test of
    `saveRescue`.

**Gates for stage 10:** `just app-check`, `just app-test`, the e2e specs each
item names, `perf.e2e.ts` through `just app-e2e-timed` (it runs alone), and
the screenshot tests.

## Stage 11: one source of truth: core, engine, API, Python, CLI

This stage's three items that need Fable (Continue's refusal and the
folder name, the change audit, the names pinned by tests) are stage 19.

- [ ] **11.1 A test that ties the API schema to the C++** (combine, trap; M)
  - *Where:*
    - The error table has five copies:
      - `engine/rpc.h:33-45` and `rpc.cpp:10-21`;
      - the contract suite's Python client (`engine_client.py:25-40`, in
        `tests/contract/`);
      - `api/apischema.py:30`;
      - `web/tools/gen-api.ts:54-75`;
      - `api/gen_api_md.py:40-52`.
    - Enum tables are written inline, though `engine/convert.h:33` says
      "each value once":
      - ImageFit twice (`engine/methods_shape.cpp:151-160` and 194-195);
      - the border enums (`methods_shape.cpp:267-271`);
      - `methods_solution.cpp:20`, 84, 122 and 142;
      - `methods_problem.cpp:186`;
      - `methods_document.cpp:77`;
      - `events.cpp:190`.
    - The output names come from the core's file-format strings
      (`imageFitName`), so renaming a file format changes the API.
    - `kSolverBoxes` (`engine/convert.cpp:605`) copies `SolverSettings`'
      booleans by hand.
    - The thread limit 256 is written in `api/engine.json:108` and 139,
      `api/solution.json:122`, `api/types.json:478` and
      `methods_solution.cpp:148`, which should use `kMaxThreads`.
    - The disk settings are capped at 1e9 in `methods_engine.cpp:123` and
      126; the schema has no maximum.
    - Defaults exist only as description text.
  - *What the checks catch today:* strict validation of every reply and
    event, `engine.info`'s methods against the schema's, every method called
    once with success, every errc declared, and generated files that are
    not stale.
  - *What they miss:* the parameters' side (enum values, ranges and
    defaults are checked only when a test happens to send them), output
    values no test emits, and the error tables' copies among themselves.
  - *Change:*
    1. One error table, `api/errors.json` (or a `$defs` in `engine.json`).
       `apischema.py`, `gen_api_md.py`, `gen-api.ts` and the contract client
       read it.
    2. Each enum the engine reads or writes gets one Names table in
       `engine/convert.{h,cpp}`, used by the methods files. The API names are
       an explicit table, apart from the file format's names.
    3. `methods_solution.cpp` uses `kMaxThreads`.
    4. A Catch2 test, `tests/unit/engine/test_api_tables.cpp`, loads
       `api/*.json` from the source folder, as other tests find their data,
       and compares:
       - every Names table with the schema's enum;
       - `kSolverBoxes` with the schema's solver options;
       - the numeric maxima (threads, and 11.2's limits) with the C++
         constants;
       - the error table with `toRpc`.
  - *Tests:* that test; the contract suite; `ctest -R api-docs`.

- [ ] **11.2 Limits as core constants, emitted for the web** (combine, bug; M)
  - *Where:*
    - Disc size 100, rods 16, heights 64: `core/shapelogic/app/document_stacking.cpp:37-39`,
      `engine/methods_stacking.cpp:81-103`, `api/stacking.json:93` and
      436-443, `web/src/renderer/state/stacking.ts:18-19`,
      `web/src/renderer/entities/rods-card.ts:124-140`.
    - Tray 64 and allowance 0.25: `core/shapelogic/model/tilted_tray.h:31-35`,
      `api/types.json:278-283`, `web/src/renderer/state/tilted.ts:31-33`.
    - Cells 1<<24: `core/shapelogic/app/document_commands.cpp:44`,
      `engine/methods_shape.cpp:22`.
    - The piece count is 5.5's.
  - *Problem:* the engine refuses 17 rods while the document clamps the count
    (`document_stacking.cpp:72`). A copy missed anywhere drifts silently.
  - *Change:*
    - The core's constants are the source, and the engine uses them.
    - 11.1's test checks the schema's maxima against them.
    - `just app-gen` (`web/tools/gen-api.ts`) emits the schema's maxima as
      named constants (for example `LIMITS.stacking.rods`), and the web uses
      those instead of its own numbers.
    - One behavior out of range: refuse. The document refuses too, as the
      engine does today.
  - *Tests:* 11.1's test; the staleness test of the generated file; the web
    cards' unit tests use `LIMITS`.

- [ ] **11.3 Grid kinds: compile-time checks now, one table of traits next** (combine, trap; S then M) ✓
  - *Where:*
    - `core/shapelogic/grid/grid_type.h:30-42` holds the `enum GridKind`
      (0 to 8) and, separately, `inline constexpr int kNumGridKinds = 9;`.
      `core/shapelogic/io/puzzle_file.cpp:182` refuses a type at or above
      `kNumGridKinds`.
    - Adding a grid touches about 35 places in 18 files:
      - grid: `grid_type.h` (the enum, the count, `getSymmetries`),
        `grid_type.cpp` (`toString`, the caps, `makeVoxel`,
        `derivedSymmetries`), `symmetries_derived.cpp`;
      - mesh: `grid_geometry.cpp` (three switches) and
        `grid_geometry_tables.h`, `stl.cpp:118` and 136,
        `voxel_mesh.cpp:185` and 239, `mink_mesh.cpp:1582`;
      - solve: `movement_checks.cpp` (two switches);
      - app: `shape_tools.cpp` (about nine places), `layer_geometry.cpp`
        (two);
      - outside the core: `engine/convert.cpp:19`, `api/types.json:28`
        (then `docs/API.md` and `api.generated.ts`), the docstring at
        `python/src/module.cpp:229`;
      - web: `view3d/extent.ts`, `shell/new-grids.ts`, `demo/main.ts`,
        `entities/inspector.ts:200`;
      - tests: `tests/unit/grid/grid_test_helpers.h:21` and 28, and
        `web/tests/helpers/fixtures.ts`'s `SWEEP`.
  - *Problem:*
    - Adding `GridKind = 9` without raising `kNumGridKinds` makes
      `readGridType` refuse the files `writePuzzle` has just saved.
    - About half of the places are not checked by the compiler:
      - the ternaries at `grid_type.cpp:72` and `shape_tools.cpp:165-166`
        send any third derived grid to the hexagonal tables;
      - `scales` (`shape_tools.cpp:503-504`) lists the grids it excludes, so
        a new grid gets scale buttons by default;
      - the mesher is chosen by the same if-chain twice (`stl.cpp` and
        `voxel_mesh.cpp`);
      - several `default:` branches (`shape_tools.cpp:494`, 727 and 740;
        `layer_geometry.cpp:210` and 254) quietly give a new grid the cube's
        behavior.
  - *Change now (S):*
    - Derive `kNumGridKinds` from the enum (add a last enumerator, or
      `static_assert(kNumGridKinds == static_cast<int>(GridKind::Cairo) + 1)`
      beside it).
    - `static_assert(std::size(kAllGrids) == kNumGridKinds)` in the test
      helper.
    - Switches over `GridKind` list every kind and have no `default:`, under
      a warning or clang-tidy check that fails on a missing case. The
      ternaries become switches.
  - *Change next (M):*
    - One `constexpr GridTraits` row per kind in `grid/grid_traits.h`:
      - the API name and the label;
      - the caps and the mesher;
      - whether it is cube-like, its default size and size unit;
      - whether it scales;
      - its symmetry accessor and its conversion targets.
      The engine's and Python's names come from it.
    - `engine.info` sends each grid's cell-to-mesh matrix and its extent rule.
      Then `web/src/renderer/view3d/extent.ts`, a tested copy of
      `mesh/grid_geometry.cpp:61-145`, can go, or stay checked against
      `engine.info` in `web/tests/engine`.
    - What stays per grid: the voxel class, the lattice, the face polygon,
      the matrices, the movement directions, the editor's corners and tool
      tables, and the New dialog's entry.
  - *Tests:* the `engine.info` contract test; `ctest -R api-docs`;
    `just oracle-python`; the web engine tests of the client's grid tables
    (`web/tests/engine/editor.test.ts`, `solver.test.ts`).

- [ ] **11.4 Code copied between the new grids; marks on grids that have none** (combine, bug; S to M)
  - *Where:*
    - `core/shapelogic/grid/voxel_hex.cpp:173-210` is the same as
      `voxel_bricks.cpp:361-397`.
    - The transforms in `voxel_hex.cpp:48-136`, `voxel_cairo.cpp:133-210`
      and `voxel_prisms.cpp:72-176` differ only in the step that fits the
      result into the box.
    - `core/shapelogic/mesh/grid_geometry_7.cpp` and `grid_geometry_8.cpp`
      are the same prism extrusion.
    - Floor division is written twice (`grid/types.h:43`,
      `mesh/grid_geometry_tables.h:24`), and `wholeUnits` twice
      (`mesh/grid_geometry.cpp:21`, `app/layer_geometry.cpp:165`).
    - Only `VoxelBricks` moves start and goal marks when it rebuilds
      `space_` (`voxel_bricks.cpp:69`, 252 and 331). The transforms of the
      prisms, spheres, hex and cairo grids, and the scales of the prisms,
      rhombic and tetra-octa grids, do not. Yet
      `core/shapelogic/io/puzzle_file.cpp:441-444` reads marks on every
      grid.
  - *Problem:* marks read on a grid that never uses them end up the wrong
    length after a transform. That is a stale vector, and a later index past
    its end is UB.
  - *Change:*
    - One rebuild helper for the new grids (hex and cairo) that also moves
      marks.
    - A cube `resizeInclude` in the base class.
    - A `prismFace(polygon, …)` helper for geometries 7 and 8.
    - One floor division and one `wholeUnits`.
    - Read marks only on the Sliding grid: refuse or drop them elsewhere
      (first grep `tests/data` for marks on other grids).
    - `BT_ASSERT(marks_.empty() || marks_.size() == voxels_)` in
      `recalcBoundingBox`.
  - *Keep:* the ported grids' code (prisms are a port; change them only if
    the answers are certain to stay identical: otherwise leave them).
  - *Tests:* the grid tests (`test_grid_invariants`, `test_grid_oracle`), the
    mesh tests, the oracle's grid dump.

- [ ] **11.5 Python and the CLI use the Document's rules** (combine; S to M)
  - *Where:* `python/src/solution_iterator.cpp:286-313` copies `solveLaunch`'s
    checks (`core/shapelogic/app/document.cpp:793-808`) and
    `prepareShapesForSolving` (714-726). The CLI's `--save`
    (`cli/main.cpp:1245-1311`) skips the checks. Also 5.10.
  - *Change:* free functions in the core (in `app/` or `solve/`, called by
    `Document` too): one gives why a problem cannot be solved (an empty
    result, no pieces, …), one does the preparation. Python and the CLI call
    them.
  - *Tests:* `python/tests/`; `tests/cli/test_cli.py`: a problem the app
    refuses is refused by `--save` with the same reason.

- [ ] **11.6 Search folders and disk budgets from one helper** (combine; S)
  - *Where:* `engine/engine.cpp:27-45` (an empty `SHAPELOGIC_*_DIR` counts as
    unset, so the cache folder is used); `cli/main.cpp:220-245` and 333-342
    (an empty value means "no folder"); `python/src/module.cpp:206`
    (`work_dir=""`).
  - *Change:* one function in the core that takes the environment's value as
    a parameter (the core reads no environment; the callers pass it in),
    with one rule everywhere: empty means unset, the engine's rule. Use
    5.12's `gbToBytes` for the budgets.
  - *Tests:* `tests/unit/test_search_folders.cpp`; `test_cli.py`.

- [ ] **11.7 Sliding: the order of attempts is written twice** (combine; S)
  - *Where:* `core/shapelogic/solve/sliding_search.cpp`:
    `savedFromStartAlone` (6826-6855) and `findSlidePath` (6892-6923; the
    comment at 6893 reads "what findSlidePath asks…"). The `table` and
    `would` lambdas are copies.
  - *Problem:* if one order changes, the advice Continue gives
    (`sliding.resumeAdvice`) goes wrong silently.
  - *Change:* one function returns the ordered attempts ({TableForm,
    savedOnly}). `findSlidePath` runs `tableSearch` over them, and
    `savedFromStartAlone` runs `tableProbe` over them.
  - *Tests:* `test_sliding.cpp:4141`, `test_sliding_session.cpp:1511`.

- [ ] **11.8 Sliding: fields set after `build()`, options copied by hand, two compression constants** (trap; S)
  - *Where:* `space.partial` and `backMoves` are set after `build()` by both
    builders (`sliding_search.cpp:6256` and 6961). `slideSearchFor` copies
    SlidingOptions field by field (6597-6618). `kSlideCompression`
    (`sliding_search.h:103`) repeats `levels::kKeptCompression`
    (`slide_levels.h:87`).
  - *Problem:* a new builder that forgets the two fields silently loses
    partial nesting. A new A/B field in SlidingOptions that is not copied
    compiles and does nothing.
  - *Change:* `build()` takes `partialNested` and `backMoves`; both option
    types embed one settings struct that is copied whole; one compression
    constant.
  - *Tests:* `test_sliding.cpp`; the nested both-ends fuzz test.

- [ ] **11.9 Movement values as named constants** (trap; S)
  - *Where:*
    - `kFree` is defined at `core/shapelogic/solve/movement_cache.h:32`, but
      the literal 32000 is used in `movement_cache_bricks.cpp:42-81`,
      `movement_cache_prisms.cpp:63` and `movement_cache_derived.cpp:52`.
    - The closure skips at `movement_analysator.cpp:59`, 91 and 138 are exact
      only if no value is above `kFree`.
    - The removal step 30000 and the "removed" test `>= 10000` are at
      `movement_analysator.cpp:573`, 646 and 792-810.
    - The state encoding (`20000 *` at `disassembler.cpp:589`; `> 10000` at
      `core/shapelogic/model/disassembly.cpp:89` and
      `disasm_to_moves.cpp:319-367`) is the original's file format: keep its
      values.
  - *Change:* named constants in one header, with `static_assert`s of how
    they relate; `BT_ASSERT(v <= kFree)` on `getMoValue`'s cache-miss path.
  - *Keep:* every value.
  - *Tests:* `test_movement_checks`; the oracle's file comparison.

- [ ] **11.10 Small engine copies, and one trap** (combine, trap; S)
  - *Where:*
    - The check of a name's control characters appears twice:
      `engine/methods_document.cpp:136-143` and `methods_export.cpp:102-109`.
    - The shape-id list parser and `kMaxShapes` appear twice:
      `methods_document.cpp:19` and 112-129; `methods_export.cpp:30` and
      132-146.
    - "An rgb or null" is parsed twice (`methods_shape.cpp:136-144` and
      275-287), and int to Rgb is written twice (`convert.cpp:187-189` and
      243-245).
    - `convert.cpp:444` spells out `trayPlacementNames()` again.
    - `playProblem` and the replay result are written once for sliding and
      once for stacking: `methods_sliding.cpp:304` and 442,
      `methods_stacking.cpp:283` and 366.
    - `cli/options.cpp:538` decides whether `--limit` was given by comparing
      it with its default, 10, so an explicit `--limit 10` counts as not
      given.
    - `bt_busy` is exported by `engine/wasm_main.cpp`, and the demo never
      calls it.
    - `stackingSetRules` reads its defaults from its own output JSON through
      the const `operator[]` (`methods_stacking.cpp:80-91`). With nlohmann's
      JSON that is undefined for a missing key, so renaming a field there
      becomes UB.
  - *Change:*
    - One helper for each of these duplicates.
    - The CLI keeps an `std::optional` for whether `--limit` was given.
    - Remove `bt_busy` (check `site/` and `web/src/demo/` for callers first).
    - `stackingSetRules` takes its defaults from the core's struct.
  - *Tests:* the contract suite; `test_cli.py` with `--limit 10`.

- [ ] **11.11 Tray helpers in the undo header; tray math and nudge codes written twice** (combine; S)
  - *Where:*
    - `core/shapelogic/app/document_internal.h:63-90` holds the tray helpers;
      `placement_fit.cpp:5` includes the undo header only for
      `fillTrayBoard`.
    - The frame turn in `document_tilted.cpp:41-55` repeats
      `core/shapelogic/model/tilted_tray.cpp:107-116`.
    - `document_tilted.cpp:265` decides nudges with
      `code <= 5 || (code >= 27 && code <= 32)`, while `nudge()` in
      `app/shape_tools.cpp:31-107` is the authority.
  - *Change:*
    - Move the helpers to `app/tray_board.h`.
    - Export the frame turns from `tilted_tray.h`.
    - Export `isNudge(grid, task)` from `shape_tools.h` and use it.
  - *Tests:* `test_document_tilted.cpp`.

- [ ] **11.12 Analysis and PlacementFit: one background-job skeleton** (combine; M)
  - *Where:* `core/shapelogic/app/analysis.{h,cpp}` and
    `placement_fit.{h,cpp}` (its header says "the pattern is Analysis");
    `document.h:986-997`; `document.cpp:54-59`, 637-663 and 1190-1250.
  - *Problem:*
    - They share the same thread, stop, active and done counters, failure
      code and progress code.
    - Both pairs of maps are listed by hand in `solving()`,
      `checkProblemFree()`, `poll()` and `~Document`, so a third kind of job
      would edit all four.
  - *Change:* a `BackgroundJob` base (the thread, a StopSource, the state,
    the progress), and one jobs map keyed by (problem, kind) that the four
    places walk.
  - *Tests:* the `[parallel]` cases under `just test-tsan`;
    `test_analysis_files.cpp`; the placement-fit tests.

**Gates for stage 11:** `just test-all`, `just test-tsan`, the contract
suite, `ctest -R api-docs`, `just app-gen` and its staleness test,
`just python-test`, `just oracle-python`, the CLI tests, the oracle tiers
the items touch, `just check`.

## Checkpoint E (Fable, only if needed): review of stage 11

The procedure is in "Fable checkpoints". What to look at:
- **11.1:** the test compares what it says it compares. Break one Names table
  on purpose in a scratch copy, and the test fails.
- **11.2:**
  - every limit refuses out-of-range values in the engine and in the
    document;
  - the web's `LIMITS` come from the generated file.
- **11.3:** the `static_assert`s fail when a GridKind is added without
  changing the count. Try it in a scratch copy.
- **11.5 and 11.6:** Python, the CLI and the app give the same refusals and
  use the same folders.
- **11.7 and 11.8:** `just oracle --tier sliding` gives the same results.

## Stage 12: one source of truth: front end

- [ ] **12.1 One record per setting** (combine; M)
  - *Where:*
    - `web/src/renderer/shell/prefs.ts`: the type (20-112), the defaults
      (115-170), the choices (202-216), the chain of checks (238-266; 260
      hard-codes 100_000 next to `LIMIT_MAX`).
    - `shell/settings.ts`:
      - each row's `opts` repeats the choices (207-547);
      - the clamps repeat the ranges (599, 949-953);
      - the prefs-to-engine map is written twice (826-850, 962-970).
    - `shell/app.ts:144-150`: the same map a third time.
    - `solver/settings.ts`:
      - 60-341: BoxName, SearchOptions, `DEFAULT_OPTIONS`, `defaultsOf`, the
        box sets, `readOptions`' list and `writeOptions`;
      - 116-136, 159-190 and 480: the Search Options' defaults again, mapped
        from `optX` to `x` by hand.
    - `solver/run-card.ts` (296-314, 332-381): its own lists of which
      switches each puzzle type shows.
    - `solver/option-text.ts:13-52`.
    - `web/src/main/index.ts:775` and 868 read `debugLog` by name.
    - `state-store.ts` is generic and needs no change.
  - *Problem:*
    - A boolean takes three edits; an enum four that must agree; a ranged
      number five; an engine setting eight; a Search Option about nine,
      across five files.
    - BoxName changed in nine commits, and c716c2f touched all five files.
    - `SLIDING_BOXES` and `STACKING_BOXES` look like the source of what each
      puzzle type shows, but run-card writes its own lists. Only
      `sliding.test.ts:175-198` checks `SLIDING_BOXES`, and it would pass if
      the card dropped a switch.
    - No test checks the `opt*` defaults against `DEFAULT_OPTIONS`.
  - *Change:*
    - One record per setting in `prefs.ts`: key, default, kind (bool, enum
      or number), choices or min and max, the engine's name if any, label
      and help, as `as const` with `satisfies`.
    - Derive the rest from those records: the Prefs type, the defaults, the
      checks, the settings rows' choices and clamps, and the one
      prefs-to-engine map (used by `settings.ts` and `app.ts`).
    - For the search options, one ordered `BOXES` table: name, pref key, the
      puzzle types it shows on and is stored for, and its default.
      `readOptions`, `writeOptions`, `defaultsOf`, run-card, `option-text`
      and the settings page all walk it. `normalize` stays as code.
    - Keep the old stored names working (`prefs.ts:241-244`).
  - *Tests:* a unit test that every default passes its own check, and that
    run-card shows each BOXES entry for its puzzle types (a DOM test).
    `journal.test.ts` and `tilted.test.ts` use `DEFAULT_OPTIONS`.

- [ ] **12.2 A typed table of the IPC channels** (combine; M)
  - *Where:* `web/src/main/app-ipc.ts` (202-661), `web/src/main/index.ts`
    (215, 257, 471, 549, 581), `web/src/preload/index.ts` (32-290),
    `web/src/engine/app-bridge.ts`; 49 channel names written out in
    `web/tests/unit/security.test.ts`.
  - *Problem:*
    - The channels: 14 `on` and 13 `handle` channels from renderer to main,
      and 4 from main to renderer (`bt:message`, `bt:command`,
      `bt:read-only`, `bt:prefs`). Each name is spelled separately in main
      and in the preload.
    - Seven channels carry 21 `op` strings.
    - The preload asserts results with 22 `as` casts (the export result's
      type is written twice), and nothing ties them to what main returns.
    - The sender check `ours(e)` is written 26 times.
    - No test checks that every channel has a handler; a missing `send`
      handler is a silent no-op.
    - A typical feature (9cb5374, 74ae8d7, dd1ce80, 422fdb5) edits
      app-bridge, the preload, app-ipc, demo-app and `index.ts`, which fills
      AppIpcHost's 20 members.
  - *Change:*
    - One platform-free table in `app-bridge.ts`: each channel's kind,
      arguments and result, as TypeScript types keyed by channel.
    - Typed `invoke` and `send` helpers in the preload.
    - A `handle(channel, guard, fn)` in main that does the sender check
      once.
    - A unit test that every table key is registered in main and exposed in
      the preload.
    - Item 13.4 then splits `app-ipc.ts` by its groups.
  - *Tests:* `security.test.ts` (built from the table, not the 49 names),
    `preload.test.ts`, the files and logs e2e specs.

- [ ] **12.3 Runtime guards that the compiler checks** (combine, trap; S) ✓
  - *Where:*
    - `web/src/engine/app-bridge.ts`:
      - `isMenuState` (686-702) checks a hand list of booleans that lacks
        `scad`, which 1991b14 added to MenuState;
      - `COMMANDS` (603-637);
      - `FILE_KINDS`, `isExportKind` and `isOpenKind` (338-365);
      - `isMeshStyle` (645-647), against the generated MeshStyle
        (`api.generated.ts:657`).
    - HelpLink is spelled in four places: `app-bridge.ts:316`,
      `web/src/main/security.ts:33-42`, `web/src/main/app-ipc.ts:486-497`,
      `web/src/demo/demo-app.ts:65-77`.
  - *Problem:* forgetting an edit fails silently:
    - the preload drops an unknown command;
    - a new MeshStyle makes main reject every menu state, so the menus
      freeze;
    - a new help link is ignored in the app and opens `/docs/` in the demo.
  - *Change:*
    - Write each guard from a table the compiler checks: for example a
      menu-state table that `satisfies Record<…, true>` over MenuState's
      boolean keys, or unions defined from `as const` arrays.
    - `isHelpLink` becomes `Object.hasOwn(HELP_URLS, v)`, with the table
      (site paths and the origin) moved into `app-bridge.ts` for main and
      the demo.
    - `gen-api.ts` emits value arrays for string enums (for example
      `MESH_STYLES`), and `isMeshStyle` uses them.
  - *Tests:* `app.test.ts` (it clicks every menu item); `security.test.ts`'s
    help-link test; a test that `isMenuState` accepts a full MenuState built
    from the type's keys.

- [ ] **12.4 One registry of event handlers** (combine, bug; S)
  - *Where:* `web/src/engine/client.ts` (238-256, 513-528),
    `web/src/main/engine-host.ts` (151-177, 232-239: no guard, so one
    throwing host handler skips the rest for that event),
    `web/src/renderer/engine-proxy.ts` (75-102, 145-170).
    `web/src/main/engine-ipc.ts` is platform-free and the demo imports it
    (`demo/engine-bridge.ts:17`), but it sits in `main/`, which needs a lint
    exception (`web/eslint.config.js:64-75`).
  - *Change:*
    - One `Handlers` class in `web/src/engine/`: on, off and emit, with each
      handler guarded and errors reported through a callback. All three use
      it.
    - Move `engine-ipc.ts` to `web/src/engine/serve.ts`, so the
      platform-free rule covers it, and drop the lint exception.
  - *Tests:* `client.test.ts`, `bridge.test.ts`, `host.test.ts` (a throwing
    handler does not stop the others).

- [ ] **12.5 One set of controls** (combine; M)
  - *Where:*
    - Steppers: ten templates in six files (`shell/settings.ts:626`, 631 and
      645; `entities/editor-card.ts:329`; `entities/tilt-card.ts:110`;
      `puzzle/piece-card.ts:60` and 79; `puzzle/pieces-card.ts:286`;
      `solver/run-card.ts:330-331`), each with its own data names, parsing
      and clamping.
    - Switches: ten `role="switch"` markups in seven files, and Space or
      Enter handled eight times (for example `settings.ts:1146`,
      `run-card.ts:560`, `tilt-card.ts:322`).
    - Rename in place twice: `puzzle/problems.ts:139-185` and
      `entities/shapes-list.ts:411-460`.
    - List arrow keys three times: `shell/cache-files.ts:99-114`,
      `shell/files.ts:228-237`, `shell/log-files.ts:275-286`.
    - The shortcut list rendered twice: `shell/help.ts:20-35` and
      `shell/settings.ts:753-773`.
    - Two color pickers: the hidden inputs at `index.html:152-165`, and
      `shell/color-picker.ts`.
    - `play/workspace.ts:824` checks `e.key` again inside the `activate`
      action.
  - *Change:* `web/src/renderer/shell/widgets.ts` with:
    - `stepperHtml(spec)` and one delegated handler that parses and clamps
      from the spec;
    - `switchHtml(spec)`, and one keydown handler in `keyboard.ts` that
      clicks a focused `[role=switch]` on Space or Enter;
    - `renameInPlace`, `listKeys`, and `keysHtml(query)` from `keys.ts`;
    - one color picker: `color-picker.ts` stays, and the hidden inputs go.
    Move the sites over one card at a time.
  - *Tests:* the entities, puzzle, solver and keys e2e specs.

- [ ] **12.6 Menu commands from one table; the shell's helpers once** (combine; S)
  - *Where:*
    - `web/src/renderer/shell/menus.ts` (64-98, 232-305: 17 identical switch
      cases) and `shell/app.ts` (272-362: 25 commits, mostly wiring).
    - `baseName` is defined six times: `topbar.ts:15`, `imports.ts:38`,
      `rescue.ts:37`, `files.ts:47`, `export/stl-dialogs.ts:50` and
      `export/exports.ts:45`.
    - Other copies:
      - the `.xmpuzzle` stem is computed five times;
      - the document's name twice (`topbar.ts:34`, `files.ts:106`);
      - the Save / Don't Save question twice (`files.ts:132`, 422);
      - the overwrite-and-write loop twice (`stl-dialogs.ts:240-279`,
        444-487);
      - the export dispatch twice (`app.ts:275-287`, 310-321).
  - *Problem:* a command without a key needs a name in `app-bridge.ts`, a
    main-menu entry, a MenuHandlers member, a switch case and a closure in
    `app.ts`.
  - *Change:*
    - MenuHandlers becomes a `Record` over the command names (the compiler
      checks that each command has one), called as `this.h[c.cmd](c)`.
    - `shell/paths.ts` for `baseName` and the stem.
    - `Store.documentName`.
    - One Save-changes question, one overwrite-and-write loop and one export
      dispatch.
  - *Tests:* `app.test.ts`; the files and exports e2e specs.

- [ ] **12.7 Solver and Play rules in one place** (combine; S)
  - *Where:*
    - File ▸ Export's enabling: `shell/menus.ts:191-195`, `shell/app.ts:69-73`
      and `solver/solutions-card.ts:486-489`.
    - The solver-name fallbacks: `solver/workspace.ts:887-901` and
      `run-card.ts:326-337` and 350-353.
    - "Ready to solve": `workspace.ts:827-829` and `run-card.ts:413-415`.
    - `stateOf`: `workspace.ts:408-414` and `autosave.ts:249-254` (it belongs
      in Solves).
    - The transport glyphs: `solutions-card.ts:48-57` and
      `play/workspace.ts:42-51`.
    - `problemPieces` and the quiet-error test are 6.13.
  - *Change:* one export each, in the module that owns the idea.
  - *Tests:* the solver unit tests and solver e2e specs.

- [ ] **12.8 The front end's grid and tool rules in one place** (combine, trap; S)
  - *Where:*
    - The cube-grid test (bricks, sliding, stacking) is written six times:
      `entities/workspace.ts:67`, `entities/edit3d.ts:41`,
      `view3d/view.ts:1500` and 1536, `view3d/overlays.ts:43`,
      `entities/grid2d.ts:302`.
    - The plane-to-axis map is copied at `entities/workspace.ts:178` (the
      same as `overlays.ts:25`).
    - `view3d/tray3d.ts:117`'s `CELL_RGB` repeats `styles.css:1502` and
      1507.
    - Tool keys come from three places apart from the key table
      (`shell/keys.ts:243-253`): `TOOLS[].k` (`entities/editor-state.ts:20-37`),
      a map by index (`entities/workspace.ts:917-918`), and O, P and E
      written into `entities/toolbar.ts:95-99` and 169.
  - *Problem:* a missed copy drifts with no error: rulers missing or no click
    on empty space, old colors in 3D, wrong key labels. Reordering `TOOLS`
    silently remaps the keys.
  - *Change:*
    - One `isCubeGrid()` beside `gridExtent` (`view3d/extent.ts`).
    - The plane map exported from `overlays.ts`.
    - The checker colors as CSS custom properties, read with `css()`.
    - `TOOLS` gets an `action` field (a KeyAction), and its key labels come
      from a `keyLabel()` beside `accelerator()` in `keys.ts`.
  - *Tests:* `entities.e2e.ts` (it presses 1, 2 and 4); new checks in
    `view3d.test` and `keys.test`.

- [ ] **12.9 One shared mesh loader** (combine; M; after 6.10)
  - *Where:* `web/src/renderer/view3d/view.ts` (858-926, 949-1068),
    `view3d/trays.ts` (132-301) and `view3d/board.ts` (271-365).
    `disposeBuilt` is copied at `view.ts:1686`, `trays.ts:323` and
    `board.ts:392`, and written out in seven more places:
    - `view3d/scene.ts:169`;
    - `solver/player.ts` (three places);
    - `solver/live-preview.ts` (two);
    - `explore/explorer.ts:1145`;
    - `export/stl-preview.ts:126`.
  - *Problem:* each consumer builds and uploads its own copy of the same
    mesh.
  - *Change:* `web/src/renderer/view3d/shape-meshes.ts`:
    - it gives out shared meshes for (shape, `shapeRevision`, style), with a
      count of their users;
    - it bumps its counter on every call, so stale replies are dropped;
    - it releases a mesh when its last user does.
    Export `disposeBuilt` from `mesh.ts` and use it at the ten places.
  - *Tests:* `sliding.e2e.ts` (`data-tray`), `stacking.e2e.ts`
    (`data-board`), `puzzle.e2e.ts`, `perf.e2e.ts` (exactly one build when a
    shape is selected).

- [ ] **12.10 The two play boards share their frame code** (combine, trap; M)
  - *Where:*
    - Copies in `web/src/renderer/play/board.ts` (182-224, 262-314, 816-838)
      and `play/stack-board.ts` (77-111, 174-223, 479-498):
      - the ResizeObserver logic (busy, resized, catchUp);
      - the pointer wiring;
      - `endDrag` and `cancelDrag`;
      - `animate()`'s frame loop and easing.
      `room.ts` shares only the layout.
    - Six toggle-button renderers: `play/sliding.ts:490-544` and
      `play/stacking.ts:425-441`.
    - `play/sliding.ts:361` parses `board.ts:351`'s `translate(...)` string
      again. If that format changes, every piece silently drops out of the
      goal picture.
  - *Change:*
    - `play/board-shell.ts` with the shared frame code and
      `animateFor(ms, at)`.
    - One `toggleButton` helper.
    - `PlayBoard.offsetOf(i)` instead of the regex.
  - *Tests:* the play and stack-play e2e and unit tests.

- [ ] **12.11 Small copies outside the renderer** (combine; S)
  - *Where:*
    - The binary location rule (the `.exe` suffix, plus
      `build/<preset>/{engine,cli}/`) is written in seven places:
      - `web/src/main/engine-path.ts:13-46`;
      - `web/src/main/cli-tool.ts:24-47`;
      - `web/tools/dist.ts:70-80` and 113-114;
      - `web/tools/record-sessions.ts:51`;
      - `web/tests/helpers/engine.ts:23`;
      - `web/tests/global-setup.ts:21`;
      - `web/tests/e2e/app.ts:34`.
    - `inputsHash` is copied in `web/tools/build-demo.ts:52-73` and
      `site/tests/demo.test.mjs:20-41`.
    - The base64 helpers:
      - `toBase64` is exported twice (`web/src/engine/recorder.ts:35`,
        `web/src/renderer/state/store.ts:354`), with a local copy in
        `entities/image-dialog.ts:116`;
      - `fromBase64` is exported twice (`recorder.ts:43`,
        `shell/piece-images.ts:36`);
      - there is an inline copy in `app-bridge.ts:441-445`.
    - The version pattern is written twice: `web/src/main/updates.ts:27` and
      `web/tools/project-version.ts:19`.
  - *Change:*
    - `web/src/engine/base64.ts` (platform-free) for the base64 helpers.
    - The binary rule in one module that main, the tools and the tests can
      import (the tools run with type stripping: check the imports work).
    - `inputsHash` in a small module that `site/tests` can import.
    - One version pattern.
  - *Tests:* `dist.test.ts` and the unit tests that use them.

- [ ] **12.12 Status colors as tokens** (combine; S)
  - *Where:* `styles.css` and the TS that writes colors. `var(--warn,#c80)`
    names a token that is never defined. The status colors are literals
    (#2e8b57 three times, #c02828 twice, five different oranges) with no dark
    theme versions.
  - *Change:* `--ok`, `--warn` and `--error` on `:root`, with dark theme
    values; replace the literals.
  - *Tests:* the screenshot tests, light and dark.

**Gates for stage 12:** `just app-check`, `just app-test`, the e2e specs the
items name, the screenshot tests, `just site-build` (12.11's demo hash).

## Stage 13: splits: front end

- [ ] **13.1 CSS and markup with their modules** (separate; M per area; D3: decided)
  - *Where:* `web/src/renderer/styles.css` (51 commits since September, the
    most of any hand-written file) and `index.html` (30). For example,
    ff4fce7 (the tray border) added `#secBorder` and `#borderColorPick` to
    the page, rules to the sheet, and `border-card.ts`.
  - *Problem:*
    - All card skeletons are static markup in one page, and all rules sit in
      one sheet. So each feature edits the page, the sheet and its module,
      and parallel sessions collide in both files.
    - Rules outlive their markup: `.slv-dbg` stayed after 678c328 renamed
      the class.
    - Of 1,474 rule blocks, about 600 use a selector token that exactly one
      TS module writes (`solutions-card.ts` 71, `pieces-card.ts` 55,
      `settings.ts` 40, `editor-card.ts` 31, and so on). About 300 more are
      used only by `index.html`'s static markup.
  - *Change:* the renderer is plain Vite (the renderer entry in
    `web/electron.vite.config.ts`, and `web/demo.vite.config.ts`), so a
    module can `import './x.css'`. Builds bundle it into the page's sheet,
    and dev injects it; the CSP's `style-src` already allows inline styles,
    but check that dev injection works under it.
    - Keep in `styles.css`: the tokens, the shared controls (`.btn`, `.seg`,
      `.step`, `.switch`), the layout and the density variables.
    - Move each area's rules, with their density overrides, to its module,
      one area per commit.
    - Each card module renders its own section into an empty mount point in
      `index.html`.
  - *Risk:*
    - The cascade: at equal specificity later rules win, and the density
      overrides must follow the base rules. The imports must keep
      `styles.css` first.
    - The screenshot tests cover only `#vp`, `#garea`, `#rightP` and the
      stacking pages. Before moving a dialog's CSS, add screenshots of three
      dialogs.
    - `security.test.ts:336` reads only `styles.css`; extend it to every
      `.css` file.

- [ ] **13.2 `solver/workspace.ts` split by concern** (separate; M)
  - *Where:* `web/src/renderer/solver/workspace.ts` (2,154 lines; 37 commits
    since September).
    - Its commits' changed lines, by method: the constructor in 15 commits,
      `run` 13, `activity` 9, `launchOptions` 7, `emptyText` 7, `details` 6.
    - The sliding features (c716c2f, 265176f, 57b9aa9, 17f28ad, a1e4927,
      9034b03) mostly touched two of these groups.
    - The differences between assembly, sliding and stacking are branches
      inside shared methods (`run`, `activity`, `details`, `launchRun`), so
      splitting by puzzle type would cut those methods apart.
  - *Change:* two new modules:
    - `solver/run-text.ts`, free of the DOM and working over a narrow
      read-only view. It takes lines 655-901 (`tagHtml`, `emptyText`,
      `statusLine`, `counts`, `savingFor`, `activity`, `details`,
      `solverLabel`) and `solver/settings.ts:557-732`, which is status
      wording sitting in the options' model file.
    - `solver/run-questions.ts`: 1293-1382, 1443-1526 and 1564-1646
      (`tellNote`, `offerFullSearch`, `askFromStartAlone`,
      `askOptimizedPaths`, `askFolder`).
    About 1,450 lines remain. The analysis part (1901-2032) could follow.
  - *Tests:* the solver, sliding, stacking, prepare-details and performance
    unit tests; the Activity and Details checks in the solver, sliding and
    stacking e2e specs.

- [ ] **13.3 `view.ts`: one layer per puzzle kind** (separate; L; after 10.1 to 10.3)
  - *Where:* `web/src/renderer/view3d/view.ts` (1,712 lines; 27 commits).
    - Each puzzle-kind commit (4cb6e2b, 18945de, c201e81, dc30490, 26857b5,
      ff4fce7) adds:
      - a wiring in the constructor;
      - a `setResolution` line;
      - branches in `lookChanged` and `draw`;
      - a sync method;
      - `vp.dataset` lines.
      These sit at 253-347, 834-846, 1104-1204, 1263-1314, 565, 602, 717, 759
      and 1423-1683.
    - The 3D-editing commits (b8d5f37, 1fe748d) edit the picking and pointer
      block.
    - `entities/workspace.ts:315-383` calls each card by hand from three
      refresh paths, each with a different subset of the cards.
  - *Change:*
    - One `ViewLayer` interface ({group, setResolution, sync, describe}),
      implemented per puzzle kind in its own file. `resized`, `lookChanged`
      and `draw` walk the layers.
    - The picking and pointer handlers go to `view3d/pointer.ts`.
    - The mesh loaders become 12.9's.
    - One keyed `refresh()` over all the cards in `entities/workspace.ts`.
  - *Tests:* all the view e2e specs and the screenshot tests.

- [ ] **13.4 `app-ipc.ts` split by its groups** (separate; M; after 12.2)
  - *Where:* `web/src/main/app-ipc.ts` (670 lines; 22 commits) and `index.ts`
    (931; 24 commits).
  - *Change:* one module per group, each with its own small dependency
    interface instead of AppIpcHost's 20 members:
    - dialogs and exports;
    - the store, recent files and updates;
    - rescue;
    - the cache;
    - the log;
    - the CLI;
    - the window: close, goal and help.

**Gates for stage 13:** `just app-check`, `just app-test`, the e2e specs of
each moved area, the screenshot tests (light and dark).

## Stage 14: dead code and the last traps

- [ ] **14.1 Delete dead code** (trap; S)
  - Before deleting each item, confirm with `git grep` across `core`,
    `engine`, `cli`, `python`, `tests`, `web` and `site` that it has no
    caller:
    - ✓ `RotationRules::collectDebugConflictCells`
      (`core/shapelogic/solve/rotation_rules.cpp:1055-1186`), and the
      `CellSet*` out-parameters it threads through the hot rule helpers
      (635, 681, 729). `docs/handoff/stage-4.md` says it "stays for the
      GUI", which never used it.
    - `MovementAnalysator::Stats`, `getStats` and `resetStats`
      (`movement_analysator.h:109-117`), with the counters bumped on every
      `checkmovement` call.
    - `Voxel::unionintersect` (`core/shapelogic/grid/voxel.cpp:606`) and
      `getDiagonal`, if 17.2 has not already removed them.
    - `Document::requireShape` and `requireProblem` (`document.h:955-956`),
      if 19.2 has not.
    - SolveSession's `assemblerKind_`: written, never read.
    - In `web/src/renderer/solver/workspace.ts`, `ws.kind` and `kindOf`:
      `problem.kind` is still awaited in every load (1007, 1055-1079), but
      nothing has read the answer since 18945de. Removing it saves an engine
      call per load. The file's header (line 9) describes it too.
    - From the BurrTools 2 removal (ef67313): `Edits.solves`
      (`state/edits.ts:18-24`) and `SolveRecord.solver` (`state/solves.ts:72`,
      200).
    - `SLIDING_ONLY` and `STACKING_BOXES`, unless 12.1 uses them;
      `PROBLEM_CHIP` (`puzzle/counts.ts:17`); `logsViewable`
      (`shell/dialog.ts:97`).
    - `bt_busy` (`engine/wasm_main.cpp`), if 11.10 has not removed it.
    - CSS and markup:
      - 18 class tokens that appear in no TS string or class attribute:
        `axf`, `cmat`, `crul`, `rrul`, `gwrap`, `mg`, `mnote`, `ph2`, `pm`,
        `prow`, `pz-pieces`, `pzo`, `pzsec`, `pzt-wrap`, `slv-dbg`,
        `slv-sum`, `sol`, `tname`;
      - 3 unused ids in `index.html`: `gridHelp`, `sgStart`, `sgGoal`.
      The `lf-*`, `ax-*` and `tp-*` classes are built in templates, so they
      are live.

- [ ] **14.2 Tests that keep the page and the code in step** (trap; S)
  - *Change:* a unit test, `web/tests/unit/renderer/page-ids.test.ts`, that
    checks:
    - every literal passed to `byId` (412 calls, 187 ids) is an id in
      `index.html` or in a TS template;
    - every class token in the CSS (`styles.css`, and the modules' CSS after
      13.1) appears in a TS string, a template or `index.html`. Classes that
      are built from prefixes (`lf-`, `ax-`, `tp-`) go on an allow list.

- [ ] **14.3 Leftover references in the docs** (docs; S)
  - *Change:* after 14.1, grep `docs/` and the code's comments for the
    deleted names and update or remove what names them. That includes
    `docs/handoff/stage-4.md`; handoff notes are history, so add a dated
    note there instead of rewriting it.

**Gates for stage 14:** `just test-all`, `just check`, `just app-check`,
`just app-test`, the e2e specs of the areas whose code was deleted.

## Stage 15: subtle bugs: the engine's end, one solve setup, change reports, two solves at once, the Solve undo step (D1, decided)

Each of these can pass its tests and still lose a user's work or give a
wrong answer later, so they need Fable. 15.1 and 15.2 are the bugs that
lose work which stage 4 left; 15.3 to 15.5 come from the smaller bugs.

- [ ] **15.1 The engine ends without pausing its solves** (bug; M)
  - *Where:*
    - `engine/main.cpp`: no SIGPIPE handling. The `Engine` lives in a block
      so that its Document's destructor pauses and saves the solves.
    - `engine/engine.cpp`:
      - `Engine::run()` (411-437): `poll()` runs once more after the input
        ends, and nothing in the loop catches;
      - `handleLine()` (255-283): `logCall`, `flushEvents` and
        `announceChange` run outside the handler's try.
    - `engine/output.cpp:24` (`out_.write`).
    - `core/shapelogic/app/document.cpp:1212`: `Document::poll()` has a
      `BT_ASSERT_MSG`, which throws `InternalError`.
    - `core/shapelogic/base/assert.h:9-10` promises "the engine process keeps
      running".
    - The front end: `web/src/main/index.ts` (`win.on('closed')` 512-529:
      `hosts.delete(...)` then `void host.stop()`; `app.on('will-quit')`
      661-666) and `web/src/main/engine-host.ts` `stop()` (105-132: closes
      stdin, kills after 10 s).
  - *Problem:*
    - (a) Nothing ignores SIGPIPE. When the app crashes or is force-quit,
      the engine's next progress write raises SIGPIPE and kills it.
    - (b) An `InternalError` thrown by `poll()`, or by the steps after a
      handler, reaches `std::terminate`.
    - Either way the Document's destructor never runs, so the solves are not
      paused: a Panex search loses what it did since its last autosave, and
      a sliding search loses its current level.
    - (c) On quit, the app's process ends while each engine, its stdin just
      closed, still waits for its solves to pause (decision 0012). The 10 s
      kill never runs, and the orphan's output has no reader. A hung orphan
      keeps its solve locks (decision 0035).
  - *Change:*
    1. `engine/main.cpp` (not `wasm_main.cpp`):
       `std::signal(SIGPIPE, SIG_IGN)` under `#ifndef _WIN32`.
    2. `output.cpp`: after each write and flush, a failed stream (a closed
       pipe shows as one once SIGPIPE is ignored) sets a flag. `run()`
       ends its loop on it, so the destructor pauses and saves.
    3. `run()`: no `poll()` once the input has ended. The loop's body (pop,
       `handleLine`, `poll`) goes in a try that catches `InternalError` and
       `std::exception`, writes `shapelogic_engine: ERROR: …` to the log
       stream and goes on. In `handleLine`, the steps after the handler get
       the same treatment.
    4. Front end:
       - `index.ts` keeps the hosts that are stopping in a Set;
       - `before-quit` waits for each one's `stop()`, capped at 15 s
         (`e.preventDefault()`, then `app.quit()` again once they are done);
       - `stop()` already kills after 10 s.
  - *Tests:*
    - `tests/contract/test_engine.py`: start a stacking or sliding solve that
      runs for seconds (the suite has such fixtures), close the client's end
      of the engine's stdout, then its stdin. The search folder then holds a
      `state.bin` written after the close: the solve was paused and saved.
    - `web/tests/unit/host.test.ts`: quit waits for a host whose `stop()` is
      still pending (a fake host).

- [ ] **15.2 One solve setup for the session, the CLI, the explorer and Python; Python's Tilted Square answers** (bug, combine; M) ✓
  - *Where:*
    - `core/shapelogic/solve/solve_session.cpp` (345-367):
      - the sliding rules: the solver is Classic; `keepMirrors`,
        `keepRotations`, `completeRotations = false`;
      - `strictColors`, `startLocks` and `freeStarts` from
        `sliding::usesStartMarks`;
      - `sliding::partTransforms(…, anyOrientation, mirroredOrientations)`;
      - otherwise, on a tilted tray with flipPieces off,
        `trayPlaneTurns(puzzle_, problem_)`.
    - `cli/main.cpp` (733-755): the same rules, plus `variableHoleColumns`
      and the non-sliding `strictColors` from its options, and the tray
      rule with `o.flip.value_or(bt::storedFlipPieces(problem))`.
    - `core/shapelogic/app/explorers.cpp` (83-99): the sliding rules with
      the orientation options false and false, after `prepareForSolving`.
      It has no tray rule.
    - `python/src/solution_iterator.cpp` (206-208): only `keepMirrors` and
      `keepRotations`.
    - `trayPlaneTurns` is declared in `solve_session.h:151`.
  - *Problem:* four hand copies of the rules that build a solve's matrix.
    - Python's never applies the tray rule and has no flip option. A Tilted
      Square problem stored with "pieces may be turned over" off gives
      different solutions in Python than in the app and the CLI.
    - The explorer has no tray rule either, so its placements on such a
      problem differ from the solve's.
  - *Change:*
    1. A core function beside `trayPlaneTurns`, in `solve/solve_session.h`
       or a new `solve/solve_setup.{h,cpp}` (it must not need a session):
       ```cpp
       struct SolveSetup { MatrixOptions matrix; SolverType solver; };
       struct SetupFlags { bool anyOrientation = false; bool mirroredOrientations = false;
                           std::optional<bool> flipPieces; /* empty: the stored value */ };
       SolveSetup solveSetup(const Puzzle&, const Problem&, MatrixOptions base,
                             SolverType solver, const SetupFlags&);
       ```
       It applies the sliding rules (forcing Classic) or the tray rule
       exactly as the session does today.
    2. The session, the CLI (its own `variableHoleColumns` and non-sliding
       `strictColors` go in `base`), the explorer (flags false and false,
       flip as stored) and Python all call it.
    3. Python gets an option `flip_pieces` (default `None`: the stored
       value) on its solve entry point, documented in `python/README.md`.
       The module keeps the original's names; this is a new name with a
       reason: Tilted Square did not exist in the original.
  - *Keep:* everything the session and the CLI give today (the oracle tiers,
    the tilted tests). The explorer changes only for tilted puzzles with
    flipping off, which is the fix.
  - *Tests:*
    - `python/tests/`: solve a fixture from `tests/data/puzzles/tilted/` that
      has flipping off and compare the count with the CLI's `--json`
      (record the CLI's number in the test);
    - a C++ unit test that `solveSetup` gives the session's matrix options
      for a sliding, a tilted and a plain problem.
  - *Verify:* `just test-all`, `just oracle --tier fast`,
    `just oracle --tier sliding`, `just oracle-python`, `just python-test`.

- [ ] **15.3 Change reports that miss or overstate what changed** (bug; M)
  - *Where:*
    - `core/shapelogic/app/document.cpp` `changed()` (541-545) assigns
      `lastChange_.what = what` rather than ORing.
    - `core/shapelogic/app/document_commands.cpp`:
      - `deleteShape` (206) reports no solutions, though
        `Problem::removeShape` erases the solutions that place the shape;
      - the group commands (`setGroupCount`, `setGroup`, `deleteGroup`)
        report solutions they leave alone;
      - `setPieceCount`'s sliding branch (1148) drops the bits that
        `syncSliding` set.
  - *Problem:* the front end mirrors engine state and refreshes it by
    `document.changed`'s `what` (AGENTS.md's rule). A missing bit leaves a
    stale mirror. After deleting a shape, the Solver's list can still show
    the removed solutions until something else refreshes it.
  - *Change:*
    - `changed()` ORs into `lastChange_.what`. Check that the step's commit
      reads `lastChange_` after every helper ran, and fold `lastChange_.what`
      into the step there.
    - `deleteShape` reports solutions when any problem's `solutionsVersion()`
      changed.
    - The group commands drop the solutions bit if they never change
      solutions.
    - Item 19.2's audit test then keeps all of this checked.
  - *Tests:* `test_document.cpp`: deleting a placed shape reports solutions;
    the group commands do not. In the contract suite, check `document.changed`'s
    `what` for those calls.

- [ ] **15.4 `solveLaunch` still prepares sliding solves the way it did before eb70072** (trap; S; Fable: two solves at once; a behavior change: tell the user)
  - *Where:* `core/shapelogic/app/document.cpp` `solveLaunch` (789-828,
    841-858); `needsPreparing` (1571-1590), which repeats `syncMaxHoles`
    (1492-1514); `core/shapelogic/model/sliding.cpp:1592-1599`
    (`prepareForSolving`, which since eb70072 only sets the problem's
    maximum of holes).
  - *Problem:* `solveLaunch` still behaves as if preparing changed shapes:
    - its comment says it sets start locks and changes shapes;
    - it refuses a second sliding solve ("…changes shapes another solve
      reads: pause it first"), though only the problem being solved changes;
    - it keeps a dead loop that adds color ids;
    - it reports shapes, colors and solutions as changed, so the front end
      refreshes all of them for nothing.
  - *Change:* first confirm that `prepareForSolving` touches only the problem
    being solved. Then drop the refusal and the loop, report
    `kChangeProblems` for that problem, and give `needsPreparing` and
    `syncMaxHoles` one shared "holes this problem needs" function.
  - *Behavior change:* a second sliding solve (of another problem) can start
    while one runs. Say so in the Done line and to the user.
  - *Tests:* `test_document_sliding.cpp`: two sliding problems solved at once,
    each with its own search folder (decision 0035).

- [ ] **15.5 Solving and taking apart record an undo step** (bug; M; D1, decided 2026-11-24) ✓
  - *Where:*
    - `core/shapelogic/app/document.cpp`:
      - `touchProblem()` (395-405) copies the whole problem, solutions
        included, into the open step;
      - `undo()` (547-568) and `redo()` swap the step's parts back whole;
      - `solveLaunch` (789-828) and `analyseStart` start a run or a
        take-apart and record no step.
    - Decision 0011 ("Solving does not record undo steps… undoing an older
      step can bring back the solutions it saved", 85-87).
    - `web/src/renderer/solver/workspace.ts:1246-1279` (the Solver tab's
      options are undoable edits).
  - *Problem:*
    - (a) Toggle a Solver option: that makes a step holding the problem.
      Solve for an hour, then press Ctrl+Z. The problem as it was before
      the solve comes back, the solutions move into the redo step, and the
      next edit throws them away. The same happens after any whole-puzzle
      step (every sliding stamp, deleting or moving a shape).
    - (b) Edit a shape whose problem is unsolved (no problem part is saved),
      solve, then undo: the old shape comes back, but the solutions found
      for the edited shape stay.
  - *Change:*
    1. When a run starts (Start, Prepare, Continue), record a step "Solve
       <problem name>" that holds the problem as it is then. If the top
       step is already a Solve step of the same problem and no other step
       came since, record nothing: the restore point is the same.
    2. `analyseStart` records "Take apart <problem name>" the same way.
    3. Undo is last in, first out, so it removes a run's results before
       the edits made before the run; redo brings them back. An undo while
       a solve runs is still refused (`checkNotSolving`).
    4. The undo label the front end shows must name the step. Check how
       steps are labeled and shown (grep `label` in `document.h` and the
       undo items in `web/src/renderer/`), and add labels if steps have
       none.
    5. Check `dirty()` and `solveDirty_` with the new steps: a solve makes
       the document changed as before.
    6. Cost: the step copies the problem with its solutions as they are at
       the run's start, as `touchProblem` does today. If solves only ever
       append solutions (confirm in `Problem` and the session), a cheaper
       part is possible: keep the count before the run, move the solutions
       after it into the step on undo, and back on redo. Write down which
       one was built and why.
    7. Amend decision 0011 with a dated line: "Solving records an undo step
       at each run's start (decided 2026-11-24)".
  - *Tests:* `tests/unit/app/test_document_solving.cpp`:
    - option, solve, undo: the solutions go with the undo, and redo brings
      them back;
    - solve, undo, then an edit: redo is cleared and the solutions are gone,
      as asked;
    - undo while solving is refused as before;
    - a take-apart, then undo: the disassemblies it found are removed;
    - the tests that count steps after solving (213-242) change: the new
      counts and the reason go in the Done line.
    - Contract: the undo label after `solve.start`.
  - *Model:* Fable (undo exactness).

**Gates for stage 15:** `just test-all`, `just test-asan`,
`just test-tsan`, the contract suite, `just oracle --tier fast` and
`--tier sliding`, `just python-test`, `just oracle-python`, `just check`
on the changed files, `just app-test`, and the solver, sliding and
stacking e2e specs (15.3, 15.5).

## Stage 16: damaged and hostile files

Puzzle files are passed between people, and `.slsolve` imports carry saved
searches from other machines. A damaged or hostile file must be refused with
a message: never a crash, a hang or all the memory.

- [ ] **16.1 The XML reader: a quadratic duplicate check, and no limit on elements** (stability; S)
  - *Where:* `core/shapelogic/io/xml_pull_parser.cpp:417-423` compares every
    pair of attributes for a duplicate, and copies both names each time
    (`getAttributeName` returns by value). `core/shapelogic/io/xml_document.cpp:31-64`
    builds the whole tree before anything is checked.
  - *Problem:*
    - 10^6 attributes on one element are about 11 MB of text, far under the
      256 MiB inflate cap, and take about 5×10^11 comparisons. They run on
      the engine's only request thread.
    - `<a/>` (4 bytes) becomes a 104-byte element plus vector slack, so a
      gzip of about 300 KB can grow to 7 to 10 GB.
  - *Change:*
    - The duplicate check with a hash set of views into the parser's buffer;
      keep the message (it names the repeated attribute).
    - An element budget in `readElement`: 16 million elements, then a
      ParseError "too many elements".
    - An attribute budget per element (4096).
    - Make the budgets a parameter (`XmlLimits`) so a test can use small
      ones.
  - *Keep:* the messages for ordinary errors (`tests/unit/io/test_xml.cpp`).
  - *Tests:* `test_xml.cpp`: 50,000 attributes parse at once; with a budget
    of 1,000 elements, 1,001 are refused. Then `just oracle-roundtrip` (the
    corpus still loads).

- [ ] **16.2 `readPuzzle` allocates before it validates, and lets exceptions escape** (stability; S)
  - *Where:* `core/shapelogic/io/puzzle_file.cpp`:
    - the voxel reader (374-382): `<voxel x="1024" y="1024" z="1024" type="0">#</voxel>`
      passes the 2^30 cap, zero-fills 1 GiB, then fails on the short text;
    - the sliding problem's search depth (1067-1070):
      `searchDepth="2147483648"` makes `ms.assign` write 16 GiB, and at 2^61
      or more it throws `length_error`;
    - the entry point (2086-2092) catches only `ReadError`;
    - number reading (142) adds digits in `int`, so a long run of digits
      overflows a signed int (UB);
    - placements (514-515) take any coordinate, and
      `core/shapelogic/model/assembly.cpp:642` subtracts them in `int`.
    `cli/main.cpp` has no try around the load, so the CLI aborts.
  - *Change:*
    - Require the voxel text's length to cover `sx*sy*sz` before
      `makeVoxel` (check the format: one character per cell).
    - Cap the search depth at what the sliding search can use (find its
      constant) before `assign`.
    - Read numbers into 64 bits with an overflow check, and refuse values
      outside `int`.
    - Bound placement coordinates (for example ±2^20).
    - Catch `std::exception` at the entry point and return ParseError
      ("the file could not be read: …").
    - In the CLI, catch at `main` and print the error with the exit code its
      other read errors use.
  - *Tests:* `tests/unit/io/test_puzzle_file.cpp`: each hostile piece of text
    is refused with ParseError; run under `just test-asan`.

- [ ] **16.3 Sliding level files: reading trusts the index and the raw blocks** (stability; S)
  - *Where:* `core/shapelogic/solve/slide_levels.h`:
    - `reload` (509-541) checks only the last entry of each table, not that
      the tables rise or that blocks sit inside chunks (`reloadOlder` does
      check, 892-899);
    - `Reader::block` (626-652) runs `getVarint(p, n); keys.resize(n); keys[0] = k;`:
      with `n == 0` it writes past an empty vector, and a garbage `n` asks
      for gigabytes;
    - `getVarint` (142-152) has no end bound, and a shift of 64 or more is
      UB for 64-bit keys;
    - the raw chunks (973-976) go unchecked. The zstd checksum covers only
      compressed chunks, so these have none:
      - chunks zstd could not shrink;
      - files written at compression 0;
      - every BTSLKEY2 file of v1.9 and earlier, which resume reads.
  - *Change:*
    - Validate the tables once in `reload`, in O(blocks): offsets rise, each
      block lies inside its chunk, each chunk inside the file. Otherwise
      report Damaged, the existing state for an unreadable level.
    - In `block()`: require 1 ≤ n ≤ `kBlockKeys`, bound the varints by the
      block's end, and return false on a bad block.
    - `getVarint` takes an end pointer and stops after 10 bytes.
  - *Keep:* the format; good files read as before (the checks are per block,
    not per key).
  - *Tests:* extend "a damaged key set file is told"
    (`tests/unit/solve/test_sliding.cpp:4931`) with a compression-0 file and
    a file with a corrupted offset; both are reported damaged, under
    `just test-asan`.

- [ ] **16.4 Panex: a damaged `state.bin` writes out of bounds in `decode`** (stability; S) ✓
  - *Where:* `core/shapelogic/solve/panex_search.cpp`:
    - `Rules::decode` (197-216) starts at `unsigned int r = rods - 1;` and
      runs `r--` at each separator. A key with more than rods-1 separators
      wraps `r` to 0xFFFFFFFF, and `s.disc[r][…]` writes out of bounds on a
      worker's stack in `nextLevel` (977).
    - `loadState` (727-745) checks each layer's count against the file
      size, not the keys.
  - *Problem:* `state.bin` files come from other machines inside `.slsolve`
    imports (`core/shapelogic/io/paused_solve.h`); a run of 0xFF bytes is
    enough.
  - *Change:* in `solve()`, after loading, check every key of the five layers
    and `meetAt` once:
    - exactly rods-1 separators;
    - each disc number below `rules.discs`;
    - each rod's count within its height.
    A bad key makes the saved search damaged, through the same path as an
    unreadable file. `decode` stays branch-free.
  - *Tests:* the damage table in `tests/unit/solve/test_stacking_session.cpp:315`:
    add "0xFF over the first key at offset 80", expected to be damaged.

- [ ] **16.5 One reader per format: gzip and paused solves** (stability, combine; S)
  - *Where:*
    - `core/shapelogic/io/gzip_file.cpp`: `readMaybeGzipFile` (115-140)
      uses gzread, which reads a cut-short stream as its end (Z_BUF_ERROR is
      never checked), while `gunzipIfCompressed` (382-416) refuses that
      stream. `openBytes` and the paused solves use the latter.
    - `core/shapelogic/io/paused_solve.cpp`: `parsePausedSolve` (180-222) is
      used only by tests (`tests/unit/app/test_analysis_files.cpp:293-303`);
      the app loads through 224-330.
  - *Problem:* opening a truncated file from disk reads as far as it goes
    and fails later with an XML message, or not at all if the cut falls
    after the root closes. Also, the tests check a copy of the paused-solve
    reader, not the one the app uses.
  - *Change:*
    - `readMaybeGzipFile` becomes `readPlainFile` plus `gunzipIfCompressed`:
      one inflate path, which refuses a cut-short stream with "the file is
      cut short".
    - One paused-solve reader over `std::istream`, used by the app and the
      tests.
  - *Tests:* a truncated gzip file is refused with that message;
    `test_analysis_files.cpp` runs the app's reader.

**Gates for stage 16:** `just test-all`, `just test-asan`,
`just oracle-roundtrip`, `just oracle-resume`, `just check` on the changed
files, the CLI tests.

## Stage 17: core speed

All answers stay identical: the oracle and the benchmarks decide (rules 3
and 5).

- [ ] **17.1 After the goal is found, the last level is scanned again on one thread** (speed; S) ✓
  - *Where:* `core/shapelogic/solve/sliding_search.cpp`:
    - `goalFound()` (6096-6132) walks `fwd_.cur` from key 0 with `movers_[0]`
      until a key has a move to the goal;
    - the parallel first match to reuse is stepBack's fallback (5975-6018);
    - the goal hit itself is found in expand (5056-5092).
  - *Problem:* in a one-end search all threads stop at the first goal hit.
    `goalFound` then scans the level again from its start, on one thread,
    without polling the stop token and without progress. That takes about
    as many times as long as there are threads. One-end searches are the
    big ones: any goal that is not one arrangement, partial or loose nesting,
    `oneEnd`.
  - *Change:* a parallel first match like stepBack's:
    - threads claim blocks in order;
    - the lowest key with a goal move is kept under a mutex;
    - a thread stops at a block whose first key is above the best so far;
    - then work out the goal key for that one key.
    Poll the stop token per block, and report a phase ("finding the move to
    the goal") through `slide_phase.h`.
  - *Keep:* the same path, found from the lowest such key as now: the test
    "the search finds the same path on any number of threads"
    (`tests/unit/solve/test_sliding.cpp:1336`).
  - *Tests:* that test, the Klotski 81-move test, `just oracle --tier sliding`.

- [ ] **17.2 `Voxel::set` rescans the whole shape for every cell** (speed; S) ✓
  - *Where:* `core/shapelogic/grid/voxel.cpp` `set()` (99-107) calls
    `recalcBoundingBox()` each time, a full scan.
    - Loops of `set` without the skip bracket:
      - `voxel.cpp:435` (`fillHoles`);
      - `core/shapelogic/model/puzzle.cpp:119` and 121 (`removeColor`);
      - `core/shapelogic/io/ps3d_file.cpp:130` and `io/scad_file.cpp:553`
        (the imports).
    - `setMark` (`voxel.cpp:445-458`) assigns and rescans on every clear.
      It is called that way in loops that clear all marks:
      `io/puzzle_file.cpp:1467` (whose loop condition rescans too) and
      `model/sliding.cpp:1667`, 1800 and 1820.
    - The bracket today is hand-written true/false pairs:
      - `puzzle_file.cpp:383` and 480;
      - `model/assembly.cpp:656` and 680;
      - `model/grid_convert.cpp:63` and 89;
      - `scad_file.cpp:771` and 775;
      - `model/stacking.cpp:375` and 388.
    - A scoped guard exists only in `python/src/py_shape.cpp:101-104`.
  - *Problem:* Fill holes on a hollow 30³ box takes about 6.5×10^8 steps,
    and a 100³ imported piece about 10^12. `kMaxImportCells` limits memory,
    not time.
  - *Change:*
    - Move the guard into `voxel.h`. It saves the skip state it found and
      sets skip; on destruction it restores that state and, when it was the
      outermost, recalculates once.
    - Use it at every site above, replacing the hand pairs.
    - Add `Voxel::clearMarks()` (one pass) for the loops that clear marks.
    - Delete the unused `Voxel::unionintersect` (`voxel.cpp:606`) and
      `getDiagonal` (confirm there are no callers).
  - *Keep:* the bounding box: the final recalculation does the same work
    once.
  - *Tests:* the grid tests; `just oracle-imports`; Fill holes on a hollow
    40³ box completes in the quick loop.

- [ ] **17.3 Union-find without path compression, and `shape.info` works out every shape's row** (speed; S) ✓
  - *Where:* `core/shapelogic/grid/voxel.cpp` (332-337: `root` walks the
    chain; copies at 386-391 and 421-426); `core/shapelogic/app/queries.cpp:104-108`
    (`connected` five times per shape, for every shape, on each `shape.info`);
    `engine/methods_shape.cpp:429` (builds the whole table to return one
    row).
  - *Problem:* the links form a chain in visit order, so each find walks
    back sy·sz cells. The outside node's chain grows by one per boundary
    cell, which makes it O(B²). That is about 10^8 steps for a 30³ shape and
    10^10 for 100³, on each `shape.info`.
  - *Change:* one shared `root()` with path halving, used by the three
    copies. `shape.info` computes only the asked shape's row (a per-shape
    function in `queries`).
  - *Keep:* the partitions; the answers do not change.
  - *Tests:* the grid tests; a 60³ shape's connectivity in the quick loop.

- [ ] **17.4 The goal test decodes pieces that cannot fail** (speed; S)
  - *Where:* `sliding_search.cpp`: `isGoal`'s single-piece loop (1892-1896),
    against the packs' loop (1897-1901: `if (!pk.hasGoal) continue;`). It is
    called for every emitted arrangement (5058).
  - *Change:* in `layout()`, list the single pieces whose `atGoal` can fail
    (their table has a 0) and loop over those only.
  - *Keep:* the answers (it is a pure AND).
  - *Verify:* bench a sliding search with pieces that have no goal, before
    and after; the oracle's sliding tier.

- [ ] **17.5 Nested slides: `groupAt` runs for pieces that can hold nothing** (speed; S)
  - *Where:* `sliding_search.cpp`: `expandAt` (2023-2059), `movesBack`
    (2088-2094), `groupAt` (2870-2902).
  - *Change:*
    - A `holds[i]` flag (`outlines[i].size() > cells[i].size()`), set in
      `SlideSpace::build`.
    - Skip `groupAt` for pieces without it, whose group would be just
      themselves.
    - Leave `expandLoose` as it is: its `looseNear` check runs before the
      members check.
  - *Tests:* the `checkSlideMovesBack` tests (`test_sliding.cpp:3265`, 3293
    and 4640), the nested both-ends fuzz test, `just oracle --tier sliding`.

- [ ] **17.6 Panex copies its whole frontier at each save, and holds two after a resume** (speed, memory; S) ✓
  - *Where:* `core/shapelogic/solve/panex_search.cpp`:
    - the `saveState` lambda (1235-1252) runs `st.fwdPrev = fwd.prev; …`
      for all five layers;
    - the resume (1156-1163) copies out of `const SavedState* resumeFrom`;
    - `resumeState` lives until `solve()` returns (1873);
    - `inMemory` and `room` (1181-1187, 1274-1277) count one copy.
  - *Problem:*
    - Every autosave and every pause doubles the search's memory while it
      writes.
    - A resumed run keeps the loaded frontier beside its own copy for its
      whole length. So after "outgrew its memory … press Continue", the
      resumed search starts at about twice its budget. With High Memory it
      swaps, or ends in `bad_alloc` and "ran out of memory" instead of a
      pause.
  - *Change:*
    - `Store::saveState` takes the header fields and `const Layer&`
      references (a view struct) instead of a `SavedState` holding copies.
    - `resumeFrom` becomes non-const, and the layers are moved out of it.
  - *Keep:* the file's bytes.
  - *Tests:* "a stopped search carries on where it stopped" and "tracing
    back between saved levels" in `test_stacking.cpp`; `just oracle-resume`.

- [ ] **17.7 Assembler 1 keeps a second copy of its matrix** (speed, memory; S to M)
  - *Where:* `core/shapelogic/solve/assembler_1.cpp:2447` (`base_ = links_;`
    in `assemble()`). Its readers are 1510 (a worker, `n_ = parent_.base_`),
    1821 and 1998 (task generation); `assembler_1.h:256-259`.
  - *Problem:*
    - The plain single-thread search and the vector search never read
      `base_`.
    - Still, every `assemble()` (every pause and resume, every journal save
      point) copies six node arrays (24 bytes per node), and the copy lives
      as long as the assembler.
    - The plain search runs when the matrix is too big for the vector
      budget, so the biggest matrices pay double.
  - *Change:*
    - Workers copy `links_`; confirm nothing changes it while they run.
    - Task generation works on a local copy made inside
      `parallelMultiSearch` only.
    - Drop `base_`.
  - *Tests:* `test_assembler_1`, `test_split_tasks`, `just oracle-resume`,
    `just oracle --tier full` with all threads and with `--threads 1`; bench
    `--plain`.

- [ ] **17.8 Playback clones and turns every piece for every sample** (speed; S)
  - *Where:* `core/shapelogic/app/playback.cpp:325-338`: `evaluate()` runs
    `clone()` and `transform(t)` for each piece and sample only to read the
    turned hotspot and matrix (`recalcSpaceCoordinates` depends on the grid
    alone). Also `engine/methods_playback.cpp:83-108` and
    `web/src/renderer/solver/poses.ts:18-20` (a chunk is 257 samples times
    the pieces).
  - *Change:* cache the hotspot and matrix for each (piece, transform), next
    to `orientCenter`, which already caches per transform.
  - *Keep:* the poses, number for number.
  - *Tests:* `tests/unit/app/test_playback_explorers.cpp`; the web engine
    pose tests.

- [ ] **17.9 Quadratic loops over solution lists** (speed; S)
  - *Where:*
    - `core/shapelogic/app/document_commands.cpp:1548-1577`: "Remove
      assemblies" with All, Before or WithoutDisassembly calls
      `removeSolution` (a vector erase) in a loop;
    - `core/shapelogic/model/problem.cpp:664-671`;
    - `core/shapelogic/app/document.cpp:1215-1222`: `poll()` finds each
      analysis result by scanning from the start, which is O(n²) for "take
      apart all".
  - *Change:*
    - `Problem::removeSolutionsIf(pred)` in one pass, keeping `sortedUpTo_`
      right (count the kept ones before it).
    - An id-to-index map in `poll()`.
  - *Tests:* the deleteSolutions tests; `tests/unit/app/test_analysis_files.cpp`.

- [ ] **17.10 Undo steps copy every saved solution, and the depth counts steps, not bytes** (speed, memory; M)
  - *Where:*
    - `core/shapelogic/app/document.cpp`: `touchProblem()` (395-405) copies
      the problem with its solutions; the whole-puzzle steps (403, 464);
      `pushStep` (533-539).
    - `core/shapelogic/model/problem.h:446`.
    - `api/engine.json:107`: the undo depth goes up to 10000.
    - Steps that change no solution but copy them all:
      - `renameProblem` and `setSolverOptions` (every Solver option);
      - `setGroupCount`, `setGroup` and `deleteGroup` (`setPartGroup` only
        touches `groups`, `problem.cpp:516-536`);
      - `setProblemRodSet` and `moveDisc`.
    - Whole-puzzle copies:
      - adding, copying, renaming and moving rod sets
        (`document_stacking.cpp:246-361`);
      - `moveProblem` and `addColor`;
      - every sliding stamp (`document_sliding.cpp:62`).
  - *Problem:* a problem whose "keep all" solutions are copied by 25 to 500
    steps reaches gigabytes. Decision 0011 says "memory per step is what the
    step changed".
  - *Change:*
    - Part kinds that save only what these steps change:
      - `ProblemSettings`: the problem's fields without its solutions,
        swapped field by field;
      - `RodSet`;
      - `ProblemOrder`: a permutation.
      Use them in the commands above.
    - A byte budget in `pushStep`, beside the depth. Each part reports its
      size, and while the steps' total is over the budget (say 512 MiB), the
      oldest steps go (with 4.4's `baseStamp_` rule).
    - This also makes 15.5's steps cheaper where it fits.
  - *Keep:* the "undo gives writePuzzle byte for byte" tests of each command:
    they guard exactness.

- [ ] **17.11 The stop token walks its parent chain at every search step** (speed; S to M; measure first)
  - *Where:* `core/shapelogic/base/stop.h:34-44`: `effective()` does an
    acquire load and a pointer hop per level. It is polled at every step in:
    - `assembler_0.cpp:188` and 469;
    - `assembler_1.cpp:1249` and 1549;
    - `simd_exact_cover.cpp:374` and 533;
    - `simd_huang_cover.cpp:611` and 879;
    - `movement_analysator.cpp:785`.
    The chain has two levels in the CLI and three in the app
    (`solve_session.cpp:453-454` plus SearchRun).
  - *Change, only if the benchmark shows a cost:* either
    - (a) a request is pushed down to the children when it is made (a child
      list under a mutex; a request stores to itself and every descendant),
      so a poll is one load; or
    - (b) poll every 64 to 256 steps, with counters that exist
      (`assembler_0.cpp:221` and 492, `simd_exact_cover.cpp:383`).
  - *Keep:* decision 0004's rules: requests are never withdrawn, and
    children follow their parents.
  - *Measure:* `bench.py`'s default set with a three-level token (the CLI
    has two; build a three-level chain behind a bench switch if needed). If
    the cost is under 1%, write "Not needed" with the numbers.

- [ ] **17.12 Assembler 1 keeps its nodes' up and down links in separate arrays** (speed; M; bench first)
  - *Where:* `core/shapelogic/solve/assembler_1.h:95-101`, compared with
    `dlx_matrix.h:32-40`, which interleaves them ("saves a register in the
    cover loops"). The loops that touch them: `hideRow` and `coverColumnRows`
    (`assembler_1.cpp:117-164`).
  - *Change:* interleave them as `DlxNodes` does (mechanical). Keep it only if
    `bench.py --plain` and `--set identical` show it faster.

- [ ] **17.13 The take-apart allocates for every candidate move** (speed; M; bench first)
  - *Where:* `core/shapelogic/solve/disassembler_node.h:40-42` and
    `disassembler_node.cpp:15` (two allocations per node);
    `disassembler.cpp:176` (an `unordered_set` per position on the parallel
    path). Most candidates are dropped at once as known positions (314-318).
  - *Change:*
    - One allocation per node: inline storage for small piece counts, or a
      free list per analysator (all the nodes of a part have the same piece
      count).
    - A scratch set per slot for `seen`, or no `seen` for Classic, where the
      merge's `take()` already removes duplicates.
  - *Bench:* `-d -R`.

- [ ] **17.14 Sliding: one flood for shifts and groups** (combine, speed; S to M)
  - *Where:* `core/shapelogic/solve/sliding_search.cpp`:
    - `floodShifts` (2657-2733) and `floodGroup` (2753-2818) are near-copies;
    - "others_ = occupied_ less the group" is written three times (2048,
      2108, 2357);
    - the epoch reset is written twice (1993, 2428).
  - *Change:* one shift flood, a template on what blocks a cell and what is
    soft, with a callback for each stop; two small helpers for the others
    set and the epoch reset. Keep it a template, not a function pointer or
    `std::function`: it runs for every arrangement.
  - *Keep:* the moves and their order, so the paths and counts stay the
    same (`test_sliding.cpp`, the nested both-ends fuzz test,
    `just oracle --tier sliding`); and the speed (time a sliding solve
    alone, before and after).
  - *Order:* before 21.1, which then only moves code.

**Gates for stage 17:** `just test-all`, `just test-tsan`, the oracle tiers
each item names (`fast`, `full`, `sliding`, with all threads and
`--threads 1`), `just oracle-resume`, `just oracle-imports` (17.2), and
`bench.py` with the before and after numbers.

## Stage 18: the engine stays responsive

The engine answers on one thread. Work that runs there for minutes, or
ignores Stop, freezes every call the window makes to that engine, Pause and
Abort included.

- [ ] **18.1 `explore.openAssembly` builds a matrix on the request thread, holding the solve's lock** (stability, speed; S then M)
  - *Where:* `engine/methods_explore.cpp:46-64` (`reduce` defaults to true,
    and the front end sends none: `web/src/renderer/explore/explorer.ts:393`);
    `core/shapelogic/app/document.cpp:1343` (`make` runs inside
    `readProblem`, under the session's `resultsMutex_`);
    `core/shapelogic/app/explorers.cpp:101` and 106 (`reduce(StopToken())`).
  - *Problem:*
    - On a big problem it blocks the request thread for minutes: Pause,
      Abort, events and every other call wait, and nothing can stop it.
    - It also stalls a running solve of that problem, which waits on
      `resultsMutex_`.
  - *Change:*
    - First (S): copy the puzzle under the lock and build outside it.
    - Then (M): make it a job like the analysis, with a StopSource,
      progress and an `explore.ready` event (`api/explore.json`; rule 10).
      The drawer shows "preparing" and a Cancel button.
    - The decision text this contradicts is fixed in 3.3.
  - *Tests:* a contract test: during `openAssembly` on a big fixture,
    `engine.info` answers (for the job form); `explore.e2e.ts`.

- [ ] **18.2 `solution.page` sorts the whole list again for every page** (speed; S to M)
  - *Where:*
    - `engine/methods_solution.cpp:33-61`.
    - The comparator `comp2Moves` (`core/shapelogic/model/problem.cpp:684`)
      calls the recursive `sumMoves`, which runs `getRotations` over every
      state (`core/shapelogic/model/disassembly.cpp:119-158`) on each
      comparison.
    - The pager (`web/src/renderer/solver/pager.ts:156-171`) asks for every
      visible page on each `solutionsAdded`, up to four times a second.
  - *Problem:* with "keep all" and thousands of solutions, each page costs
    hundreds of milliseconds, and the solve's worker waits on the lock
    meanwhile.
  - *Change:*
    - Work out each solution's sort keys once, and sort indexes by key.
    - Keep the order in the engine's state, keyed by (problem,
      `solutionsVersion`, `sortBy`, descending), and drop it when the
      version changes.
  - *Keep:* the order, with the same ties (a stable sort).
  - *Tests:* `tests/unit/engine/`: the pages are the same before and after
    for each `sortBy` on a fixture with many solutions.

- [ ] **18.3 Pause and Abort wait for the matrix to be built** (stability; M)
  - *Where:* `core/shapelogic/solve/assembler.h:159` (`createMatrix` takes no
    stop token); `core/shapelogic/solve/solve_session.cpp:694-711`
    (`prepareSearch`); `engine/engine.cpp:413-428`.
  - *Problem:* `abort()` joins the run on the request thread. While a long
    matrix build runs (the Solver shows "N placements so far",
    `web/src/renderer/solver/settings.ts:619`), every call waits until the
    build ends.
  - *Change:*
    - Pass the run's StopToken into `createMatrix`, and into the placement
      finder under it.
    - A stopped build returns a new `AssemblerError::Stopped`, which
      `prepareSearch` treats as its existing stop check at 711.
  - *Scope:* this reaches into the three assemblers' matrix code. That is
    known and planned here, so it needs no warning under rule 2.
  - *Keep:* the results of runs that are not stopped (the oracle tiers).

- [ ] **18.4 Sliding: Stop and Abort wait for the visited-set merge and for the path's trace** (stability; M)
  - *Where:* `core/shapelogic/solve/sliding_search.cpp`:
    - `insertVisited` (5394-5460) runs as `extra` inside expand's `pool.run`
      (5043-5049);
    - `restoreVisited`, while resuming (4806-4836);
    - the trace across thinned levels (`step(ball, …, false, false, …)` at
      6060).
  - *Problem:* none of these polls the token, and deep in a search a merge
    takes minutes.
  - *Change:*
    - Poll the token in `insertVisited`'s merge loop, and drop the partial
      set on a stop. Its inputs stay listed until the merge is whole (5397),
      and `restoreVisited` puts a missing level back when the search
      carries on.
    - Honor Abort in the trace's expand.
  - *Tests:* a test that stops during a merge (a test-only option that
    stops after N merged keys), then continues to the same path.

**Gates for stage 18:** `just test-all`, `just test-tsan`, the contract
suite, `just oracle --tier fast` and `--tier sliding`, `just oracle-resume`,
`just app-test` and `explore.e2e.ts` (18.1).

## Stage 19: names and audits that must not drift

The three single-source items that need Fable: a mistake in any of them
can orphan users' saved searches, or leave the screen stale, without
failing a test.

- [ ] **19.1 Continue's refusal and the saved search's folder name can disagree** (trap; S)
  - *Where:* `core/shapelogic/solve/solve_session.cpp` `resumeRefusal`
    (160-188; its list of the options that name a saved slide search is at
    172-180); `sliding_search.cpp` `nameSpace` (6262-6300: what goes into
    the folder's name).
  - *Problem:* an option added only to the name makes Continue start over
    silently, the failure decision 0043 describes.
  - *Change:* one list of the fields that name a search, used both by
    `nameSpace` and by a new `sliding::namesDiffer(a, b)`, which
    `resumeRefusal` calls.
  - *Tests:* for each field in that list, changing it makes `resumeRefusal`
    refuse (a loop over the list).

- [ ] **19.2 Document commands: one way to report a change, and an audit test** (combine, trap; M; after 15.3)
  - *Where:*
    - `core/shapelogic/app/document.cpp` `changed()` (541-545; see 15.3).
    - 61 StepGuard sites: `document_commands.cpp` 38, `document_stacking.cpp`
      15, `document_sliding.cpp` 6, `document_tilted.cpp` 1, `document.cpp` 1.
    - About 100 id lookups, each with a three-line early return; 39
      `checkNotSolving` and 23 `checkProblemFree` calls; 39 hand-set lists of
      `lastChange_` ids.
    - Pairs that differ only in the field they touch:
      - `setShapeWeight` and `setShapeColor` (`document_commands.cpp:317-357`);
      - `renameProblem` and `setSolverOptions` (981-1001, 1501-1521);
      - `moveProblem` and `moveRodSet` (953-979;
        `document_stacking.cpp:332-361`).
    - `requireShape` and `requireProblem` are never used
      (`document.h:955-956`).
  - *Change:*
    - Templates `editShapeField`, `editProblemField` and `moveInList` for
      the pairs.
    - A lookup helper that returns `Result<T&>`, for the id lookups.
    - Delete the two unused helpers.
    - An audit test, `tests/unit/app/test_change_audit.cpp`:
      - a table of commands, each making a typical call on a fixture;
      - for each, compare before and after: each problem's
        `solutionsVersion()`, the shapes' bytes, the lists' sizes, the
        colors and the problem settings;
      - assert that `lastChange_.what` covers every category that changed;
      - for categories named but unchanged, a warning list in the test
        output.
  - *Tests:* the audit test; the contract suite's `document.changed` checks.

- [ ] **19.3 Names worked out from code or bytes, pinned by tests** (trap; S)
  - *Where:*
    - `core/shapelogic/solve/sliding_search.cpp` `Fingerprint::mix`
      (3118-3132) hashes `sizeof(T)` raw bytes; `problemKeyOf` (3140) and
      `nameSpace` (6265-6312) use it.
    - Panex's `fingerprintOf` mixes 8 bytes per value.
    - `core/shapelogic/io/puzzle_file.cpp` `giveUids` (958-983): a problem
      without a stored uid gets one hashed from `writeProblem`'s bytes, and
      the uid names its saved sliding and Panex searches
      (`sliding_search.cpp:3143`, `panex_search.cpp:1728`).
    - The known-attribute lists: `puzzle_file.cpp:469-472` with
      `isImageAttribute`, 994-996, 1367 and 757.
  - *Problem:*
    - Changing an accessor's return type renames every saved-search folder:
      Continue starts over and the old folder is orphaned. Only one name is
      pinned by a test (`slide-cafc026be534476e`, nested and grid).
    - Any change to what the writer emits changes the uids worked out for
      the original's files, which orphans their paused searches after an
      upgrade. `test_puzzle_file.cpp:660` checks this only within one build.
    - If a writer emits an attribute that its reader's list lacks, the
      reader keeps it as "extra", and the next save writes it twice. The
      parser then refuses that file.
  - *Change:*
    - `Fingerprint::mix` through fixed-width overloads (8, 16, 32 and 64
      bits), with explicit conversions at the callers that keep today's
      bytes. Check every call's current width, so no name changes.
    - Tests that pin the folder names of a plain board, a board with packed
      keys and a Panex puzzle.
    - A test that pins one original fixture's worked-out uid to its value.
    - A `BT_ASSERT` in `core/shapelogic/io/xml_writer.cpp` (56, 65) that a
      tag never gets the same attribute twice.
  - *Tests:* the new pins; `tests/unit/test_search_folders.cpp`.

**Gates for stage 19:** `just test-all`, the contract suite,
`tests/unit/test_search_folders.cpp`, `just oracle-resume`, `just check`
on the changed files.

## Stage 20: core deduplication under benchmarks (D6: decided)

The assemblers and the take-apart are faithful ports, benchmarked against the
original (within 5%, or faster). Each item must leave the oracle's answers
identical and the benchmarks within noise. Measure before and after on a
quiet machine, one solve at a time. An item that cannot keep the benchmarks
within noise is dropped: revert it, tick it, and write
`Not done (date): <the numbers>`.

- [ ] **20.1 Crowell: use the prepared rotation API, and share Classic's helpers** (speed, combine; S to M)
  - *Where:*
    - `core/shapelogic/solve/rotation_moves_crowell.cpp` (96-117, 205,
      292-303).
    - `rotation_rules.cpp` (903-915, 919-952, 751-754).
    - The prepared API (`rotation_rules.h:74-79`): `setBodies`,
      `preparedAxisBlocked`, `allowPrepared`.
  - *Problem:*
    - For each pivot and sense, `tryCurrentCandidate` allocates `combinedEnd`
      and calls `allowRotation`. That call:
      - compares the whole occupied list to find its cache
        (`cachedOccupancy`);
      - repeats the sandwich and perpendicular-plane checks that
        `startAxis`'s `axisBlocked` already passed (`perpPlaneClear`
        allocates four vectors);
      - builds `Occupancy(startCells)`;
      - builds and sorts `staticSquares(occ, axis)`.
    - `loadSubsetCells` walks every piece's shape box again for each subset.
    - Copies of Classic's code:
      - `collectWorldCells` (`rotation_moves_classic.cpp:112-137` and
        `crowell.cpp:50-75`, identical);
      - the moving body's box in the turn plane (classic 171-200, crowell
        126-155);
      - building the turned node (classic 281-331, crowell 308-357);
      - turning a cell, written three ways: `rotateCellCrowell`
        (`crowell.cpp:18`), `RotationRules::rotateCell`
        (`rotation_rules.cpp:962`), and `rotateDoubled` with
        `rotateVector90` (`classic.cpp:42-99`).
  - *Change:*
    - `setBodies` once per subset, `preparedAxisBlocked` in `startAxis`,
      and `allowPrepared(…, true)` per candidate.
    - Cache the pieces' cells at `initFind`, as Classic does (`pieceCells_`).
    - Move the shared helpers into `RotationMoves` (`rotation_moves.h`), with
      one cell turn.
  - *Tests:* `just oracle --solver crowell` with rotations (`-R`);
    `test_rotation_solvers.cpp`; the bench with rotations.

- [ ] **20.2 One take-apart pool for the CLI and the app** (combine, speed; L)
  - *Where:* `core/shapelogic/solve/take_apart_pool.cpp` (115-154, 170-260),
    used only by the CLI (`cli/main.cpp:881`); `solve_session.cpp`
    (1490-1528, 1760-1814), the app's.
  - *Problem:*
    - Both carry:
      - the bounded queue;
      - the "behind" mode that lends the search's cores;
      - the limit on workers beside the search;
      - `addLoad` around each take-apart.
    - Their catch-up thresholds differ: `window_/8`
      (`take_apart_pool.cpp:234`) against `queueLimit_/4`
      (`solve_session.cpp:1787`).
    - Only the pool has the batching and the look-before-sleep that its
      header says millions of tiny take-aparts need. The session takes a
      lock and sends a notify for each assembly.
    - `tests/bench/bench.py` times only `shapelogic_cli`, so the app's path
      is outside the 5% rule.
  - *Change:*
    - One pool with the session's extras, used by both: on a stop, the
      assemblies not started and those stopped go back as pending, and
      results are numbered.
    - A bench mode for the session's path: a CLI switch that solves through
      SolveSession, or a small tool in `tests/tools`.
  - *Tests:* `test_take_apart_pool`, `test_solve_session` (`[parallel]`), the
    web engine tests; bench both paths.

- [ ] **20.3 Assembler 1: one search machine** (combine; L)
  - *Where:* `core/shapelogic/solve/assembler_1.cpp`: `iterative`
    (1232-1485), `Worker::search` (1541-1726) and `generateTasksAtDepth`
    (1815-2002). Its header (4-6) says they "stay three state machines, as
    there" (the original has three).
  - *Problem:* the bodies of states 0 to 7 are the same in all three, apart
    from the hooks around them (progress stacks, the debugger, stop states,
    delivery, the cutoff). Every fix has to be made three times.
  - *Change:* one `template <class Hooks>` machine with inline hooks, and
    three Hooks types. Check that the generated code is as fast (bench
    `--plain` and the default set).
  - *Tests:* the oracle's iteration counts for the plain search,
    `just oracle-resume`, `test_assembler_1`.

- [ ] **20.4 Assemblers 0 and 1: one matrix builder and one adapter to the run layer** (combine; M)
  - *Where:* the copies (assembler 0's, then assembler 1's):
    - `addToCache`: `dlx_matrix.cpp:26` and `assembler_1.cpp:38`;
    - `isSliding`: 39 and 50;
    - `rowIndexOf`: 98 and 245;
    - `clumpify`: 121 and 772;
    - the mirror-pair analysis: 283-357 and 472-553;
    - the placement loop: 365-439 and 556-640;
    - `getInt`: `assembler_0.cpp:24` and `assembler_1.cpp:65`;
    - `prepareRowCheck` and `rowCheckRejects`: `assembler_0.cpp:95-129` and
      `assembler_1.cpp:263-296`;
    - `reportParallelSolution`: 627 and 1755;
    - the TaskSearch adapter: 818-906 and 2115-2204;
    - `simdSearch`: 1007 and 2381.
    Assembler 0 also writes its column choice with the hole check three
    times (`assembler_0.cpp:229-265`, 506-537, 698-728).
  - *Change:*
    - `solve/matrix_build.{h,cpp}` with the shared matrix-building functions.
    - One split-search adapter, a template on the Task type.
    - An inline `DlxNodes::chooseColumn`.
  - *Speed:* none of this is in a hot loop except the column choice, which
    stays the same once inlined.
  - *Tests:* the oracle's iteration counts, `just oracle-resume`,
    `test_assembler*`.

- [ ] **20.5 The Huang cover's kernels: one pre-filter, and every kernel tested** (combine, trap; S)
  - *Where:* `core/shapelogic/solve/simd_huang_cover.cpp` (119-131, 164-176,
    205-217, 265-277: the chosen-row, shape and range pre-filter is pasted
    into each kernel); `tests/unit/solve/test_simd_exact_cover.cpp:487`
    (tests the dispatch only); `cli/main.cpp:440-458` (`--no-simd` turns off
    the vector search and the kernels together).
  - *Problem:*
    - A fix to one copy reaches only one platform.
    - Nothing checks the covers of the Huang scalar kernel. The AVX kernels'
      covers are checked only by the x86 CI unit tests, with no comparison
      against the scalar one.
    - The scalar kernels never search a real puzzle.
  - *Change:*
    - One `inline` pre-filter function with no target attribute; it inlines
      into each kernel.
    - A test "every kernel gives the same covers" for the Huang cover, as
      the movement kernels have (`test_take_apart_pool.cpp:176`).
    - An assembler-level test with `kernels = none()`, comparing assembly
      multisets with the default run.
  - *Tests:* the new tests; the bench.

**Gates for stage 20:** `just test-all`, `just test-tsan`, `just oracle` on
the fast, full and slow tiers with all threads and `--threads 1`, for
`--solver classic` and `crowell`; `just oracle-resume`;
`just oracle-files --tier all`; `bench.py` (the default set, `--plain`,
`--set glar`), with the numbers in the Done lines.

## Stage 21: splits: core

Each split moves code without changing it. It comes last because stages
15 to 19 edit these files in place first; this stage then moves finished
code. Check open branches first (rule 11). Count the tests before and
after: every case must still run.

- [ ] **21.1 `sliding_search.cpp` split along its seams, and compiled in parts** (separate, speed; M)
  - *Where:* `core/shapelogic/solve/sliding_search.cpp` (7,362 lines; 29
    commits since September). Its templates (1236-6380) sit inside the
    anonymous namespace (52-6489). `eachKey` (6396-6420) instantiates them for
    five key types: `uint64_t`, `__int128`, `BigKey<3>`, `BigKey<4>` and
    `BigKey<8>`.
  - *Build cost:* `build/release/.ninja_log` shows 38.8 s and then 70.3 s for
    this one file (51 s and 81 s with tsan). The next largest of our files
    takes 8.9 s, and all 205 take 475 s in total. `sliding_search.h` is
    included by 59 files.
  - *Seams.* The history shows them:
    - a1e4927, 24272bd and 422fdb5 changed only lines 3000-6200;
    - 445bf4b (612 lines added) changed only 1000-2600, plus `eachKey`;
    - 4abf08d, 3218c98 and fbfbd2e changed only the play.
    The parts, which go into `bt::sliding::detail` where they leave the
    anonymous namespace:
    - the board rules (54-753: GoalCheck, cellsOf and outlineOf, StopGrid,
      reachableShifts, fewestTurns, nestedGroup) go to `slide_board.{h,cpp}`;
    - the play (7128-7362) goes to `sliding_play.cpp`. Its header exists,
      and ARCHITECTURE.md (749) already says the play is defined there;
    - the tables, keys and moves (916-3091: PosTable, SlideSpace,
      SlideMover, chain) go to `slide_space.h`;
    - the set storage (3093-3958: SearchFolder, SetStore, runMerge,
      RunBuilder) and the state file (3959-4199) go to `slide_store.h` and
      `slide_saved.cpp`;
    - LevelSearch, the naming and the public API stay, about 3,000 lines.
  - *Shared with the Panex and stack searches.* They include
    `sliding_search.h` only for these, which move to `base/` (for example
    `base/search_memory.h` and `base/binary_io.h`):
    - `memoryBudget`, `memoryStates` and `kSearchMemoryBytes`
      (`sliding_search.h:62-80`, `.cpp:6579-6595`); `Searcher::budget()`
      (`panex_search.cpp:814-818`) repeats the shared-memory branch;
    - the `put`/`get` helpers (`panex_search.cpp:514-522`,
      `sliding_search.cpp:4073-4084`);
    - the FNV-1a hashing. Panex's `fingerprintOf` mixes 8 bytes per value,
      and the sliding Fingerprint mixes `sizeof(T)`: a shared hasher must
      keep both forms, so no folder name changes (19.3 pins them).
  - *The near-copies* (`floodShifts` and `floodGroup`, the others set and
    the epoch reset) are folded before this item, in 17.14 (Fable's). This
    item only moves code.
  - *Compiling in parts:*
    - Move the Key templates into an internal header.
    - Declare `extern template` for `tableSearch` and `tableProbe`, and for
      whatever else `eachKey` instantiates.
    - Add one small file per key width with the explicit instantiations
      (keep the `__SIZEOF_INT128__` branch). The five then build side by
      side.
  - *Keep:* the behavior. Names that leave the anonymous namespace must not
    clash with Panex's own `put`, `get` and state helpers.
  - *Verify:*
    - `just test-all`, `just oracle --tier sliding`, `just oracle-resume`;
    - a sliding solve timed alone, before and after (rule 5);
    - `just check` on the new files;
    - the release build time of these files, in the Done line.

- [ ] **21.2 `test_sliding.cpp` split** (separate, speed; S to M)
  - *Where:* `tests/unit/solve/test_sliding.cpp` (5,851 lines; 85 cases in one
    file).
  - *Seams:*
    - The KeySet cases (1076, 2118, 4791-5116, 5714) need only
      `slide_levels.h`: they go to `test_slide_levels.cpp`.
    - The saved, resume and disk cases (1779-2086, 2631, 2984, 3558,
      3743-4141, 4684, 5222-5662) are interleaved with the search cases;
      they go to `test_sliding_saved.cpp`.
    - The stamp and mark cases (1492-1719), whose code is in
      `model/sliding.cpp`, go to `tests/unit/model/test_slide_marks.cpp`.
    - The shared helpers (`solveRun`, `SlideFixture`, `oneWayPuzzle`, and
      8.3's `ScratchDir` in place of `scratchDir`) go to
      `tests/unit/solve/sliding_test_helpers.h`.
  - *Keep:* every case. List them before and after
    (`bt_tests --list-tests`), and the two lists must match.

- [ ] **21.3 `solve_session.cpp`: the sliding and stacking flows in files of their own** (separate; M)
  - *Where:* `core/shapelogic/solve/solve_session.cpp` (2,319 lines). Of its
    14 commits since 2026-11-12, 10 changed `onSlidingAssembly`, 7 `launch`,
    6 `progress`, and 5 each `slidingSummary` and `runStacking`.
    - Sliding:
      - about 25 members used only by sliding (`solve_session.h:536-610`);
      - `onSlidingAssembly` (1269-1410), `onSlideDepth`, `slidingSummary`
        (1434-1488) and `resumeRefusal` (160-188);
      - the slide setup in `launch` (346-363, 433-443, 471-497);
      - the slide part of `progress()` (2014-2064).
    - Stacking: `launchStacking`, `runStacking` and `endStackingRun`
      (531-611, 975-1149).
  - *Change:* the sliding state and its functions go in a `SlideRun` member,
    defined in `solve_session_sliding.cpp` (declared in the header or an
    internal header). The stacking flow goes in `solve_session_stacking.cpp`.
    It stays the same class in other files. The seams are the launch setup,
    `onAssembly`, the end-of-run stats, the note, `progress` and
    `resumeRefusal`.
  - *Keep:* the behavior (a mechanical move).
  - *Tests:* the session tests, `just oracle-files --tier fast`,
    `just test-tsan`.

- [ ] **21.4 `convert.cpp`: its solve part in its own file** (separate; S)
  - *Where:* `engine/convert.cpp` (994 lines; 33 commits since September, the
    most of any C++ file). Its commits' hunks land in:
    - `solveOptionsFrom` and `kSolverBoxes` (8 or more);
    - `progressJson` (6 or more);
    - `shapeSummary` (4);
    - `trayJson` (3);
    - the names (3).
    They nearly always come with `solve_progress.h`, `solve_session.h` and
    `types.json`: this is the file every reported field passes through.
  - *Change:*
    - Lines 595-943 (the stored settings, the solve options in, the
      progress, stats and failures out) go to `engine/convert_solve.cpp`.
    - `trayJson` goes beside `readTraySettings` and `placementFitJson` in
      `engine/methods_tray.cpp`.
    - A table of fields would not save the three edits (the header, the
      converter, the schema), so none is planned.

- [ ] **21.5 `puzzle_file.cpp` split by element** (separate; M)
  - *Where:* `core/shapelogic/io/puzzle_file.cpp` (2,204 lines). Each of its
    last twelve commits edited the reader and the writer of the same element
    (for example ff4fce7, 0aa9c93, 068cbba). The solution and separation code
    (484-800, 1607-1779) changed once.
  - *Change:* one file per element, each holding the element's reader, its
    writer and its list of known attributes:
    - `io/puzzle_file_voxel.cpp`;
    - `_solution.cpp`;
    - `_problem.cpp` (with stacking, slide marks and uids);
    - `_tray.cpp`.
    The access structs, Keeper and the slot enums go in
    `io/puzzle_file_internal.h`. This is not a split into reading and
    writing, which would cut every lockstep edit across two files.
  - *Keep:* the bytes (`just oracle-roundtrip`, `test_puzzle_file`).

- [ ] **21.6 `document.h`: per-mode facades** (separate; L; D5: decided; after 21.1 to 21.5)
  - *Where:* `core/shapelogic/app/document.h` (1,000 lines; 28 commits since
    September, the most in `app/`).
    - About 38 files compile it: six in `app/` through `document_internal.h`,
      all 19 native engine files through `engine/engine.h` and
      `engine/rpc.h`, and 13 tests.
    - About 15 of its last 27 commits added declarations for one mode (11
      sliding, 3 tilted, 1 stacking).
    - `document_commands.cpp` holds 17 tilted, 15 sliding and 16 stacking
      checks inside generic commands.
    - For scale: the Tilted Square stages touched 126 files (core 15, engine
      8, api 7, cli 3, `web/src` 36, web tests and recordings 15, C++,
      contract and CLI tests 12, docs and site 18, demo and screenshots 15).
  - *Change:*
    - Per-mode facades (`SlidingEdits`, `StackingEdits`, `TrayEdits`):
      friend classes of Document, declared beside their mode's `.cpp` and
      included only by their engine methods file and their tests.
    - The refusals by mode become one table (command × mode → reason) behind
      a single `refuse(cmd, shape)`.
    - The web's `Family` plus its separate `tilted` flag could become one
      mode that the engine reports.

**Gates for stage 21:** `just test-all`, `just test-tsan`, the oracle tiers
the moved code serves (`sliding`, `fast`, `files`, `roundtrip`),
`just oracle-resume`, the contract suite, `just check` on the new files,
the release build's time.

## Not in this plan

- The website (`site/`) and the research pages: not reviewed, apart from
  2.9's guide page.
- The bodies of most tests: reviewed only for their structure and helpers.
- What the review found sound needs no change. For example:
  - the engine's line and queue caps, and its backpressure;
  - the Electron security checklist (sandbox, contextIsolation, CSP,
    navigation and permission guards, fuses);
  - the XML depth cap;
  - the stop token's rules;
  - document saves (an exclusive temporary file, then a rename);
  - the SetStore locks;
  - the movement kernels' tests against scalar code;
  - `autosave.ts`'s generation checks.
- Formatting and naming: not enforced on purpose (`just format` was removed
  on 2026-11-15).
- Unity builds and precompiled headers: after `sliding_search.cpp` (21.1),
  the slowest of our files takes about 11 s in dev, so they would not save
  much.

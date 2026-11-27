# Plan: rename the leftover "bt" names to "sl" / "shapelogic"

## Context

Written on 2026-11-27 from a scan of the tracked tree at main 345a0ee, with
the names that `feature/cleanup-and-optimize` adds (30 commits ahead then).
Nothing was changed or built for it.

"bt" is left over from BurrTools, the program ShapeLogic grew from. The
program, the include folder (`core/shapelogic/`), the Python module
(`shapelogic`), the shipped executables (`shapelogic_cli`,
`shapelogic_engine`) and most environment variables (`SHAPELOGIC_*`) already
carry the new name. The C++ namespace, the macros, the CMake targets and
options, a set of environment variables and the web app's IPC names do not.
Agents read `bt` as a second project name, copy it into new code, and the
processes in a running build are called `bt_tests`.

Size at 345a0ee: about 3,500 occurrences of 132 distinct names in about 450
files. Nearly every change swaps one token for another, so a script does the
editing and the compilers find what it misses.

Step 0 scanned again on 2026-11-27 at e7e7176 (the tip of
`feature/cleanup-and-optimize`): 3,207 occurrences in 528 files, in the 164
rows of `tmp/bt-names/map.tsv`. The oracle and its tool binaries were gone by
then, and the scan found names the first one missed; the lists below are as
step 0 left them.

This is a separate plan from docs/CLEANUP-PLAN.md. It touches almost every
file that plan's branch changes, so it starts only after that branch is
merged (see "Before step 1").

## How to work through this plan

1. **One step, one commit.** Each step leaves the tree building and every
   suite it names passing. Do not start a step until the previous one is
   committed. Commits need the user's go-ahead: when a step is done and
   tested, leave it in the tree and give the user the commit message.
2. **Rename by exact token, never by substring.** Work from the map in
   `tmp/bt-names/map.tsv` (step 0), one old name and its new name per line,
   matched as whole words. A broad `bt` search also matches `btnSettings`,
   `subtree`, `doubt`, random base64 and puzzle names. A whole-word match
   misses a name that touches a letter: `-DBT_WERROR` (after `-D`),
   `\nBT_ENGINE` (after an escape in a string) and `libbtcore`. The map has
   a row of its own for each of these forms.
3. **Use a script, then read the diff.** The rename script
   (`tmp/bt-names/rename.py`, git-ignored) takes step numbers or old names
   and a list of paths, rewrites whole-word matches and prints a count for
   each name (`-n` only counts, `-n -v` also shows each line that would
   change). Check the counts against the map: over the whole tree
   (`rename.py -n <step> .`) they are equal. Then read `git diff --stat`
   and skim the diff for changes inside strings that are not names
   (messages, paths of other programs, test data). `rename.py --scan` says
   what is left: the map's rows that still occur, step by step, and every
   name of the old kinds that neither the map nor "What stays" covers (none
   after step 0). `tmp/bt-names/make_map.py` holds the rows
   and writes the map with its counts again.
4. **Docs move with the code.** In each step, rename that step's names in
   the live docs as well, so no doc names something the code no longer has.
   Every tracked file is live unless "What stays" lists its path, so
   `rename.py <step> .` reaches them all: AGENTS.md, CLAUDE.md, README.md,
   the READMEs under web/, tests/, python/ and api/, the notes beside the
   code (`core/shapelogic/**/*.md`), `.claude/skills/`,
   docs/ARCHITECTURE.md, docs/API.md, docs/core-api.md, docs/webui-spec.md,
   the website's code-base pages (`site/src/content/docs/docs/code-base/`),
   and the plans under docs/ that still have open items: at step 0
   docs/CLEANUP-PLAN.md (10.10), docs/CELL-SEARCH-PLAN.md and
   docs/TILTED-SQUARE-PLAN.md. In those, a name that is gone from the code
   keeps the name it had (see "What stays"). The files that stay: this
   plan (it names the old names on purpose; the script skips it),
   `docs/decisions/` and `docs/handoff/` (dated records of what was true
   then), the finished plans (docs/PLAN.md, docs/DEFERRED-PLAN.md,
   docs/FRONTEND-PLAN.md, docs/SLIDE-TWO-ENDS-PLAN.md,
   docs/V2-IDEAS-PLAN.md), RELEASE_NOTES.md and `release/`.
5. **Generated files are regenerated, not edited.** The demo in
   `site/public/demo/` (`index.html`, `manifest.json` and `assets/`) comes
   from `just site-demo`, and `web/src/engine/api.generated.ts` from
   `just app-gen`. The site's test compares a hash of the demo's inputs
   (`CMakeLists.txt`, `CMakePresets.json`, `cmake/`, `core/`, `engine/`,
   `api/`, `web/src`) with the one in `manifest.json`, and every step
   edits some of them: end each step with `just site-demo`, in the step's
   commit, or `just site-build` fails.
6. **Nothing else builds while a step runs.** Renamed targets and options
   break the build folders other sessions use (`build/dev`, `build/release`,
   `web/out`). Check that no other session is building or running tests
   before a step that reconfigures or rebuilds, and tell the user before
   starting.
7. **Tell the user about anything they would notice:** the env vars and
   CMake options they type (step 4 and step 3 list them), and any name that
   shows in the app.

## What stays as it is

`tmp/bt-names/stays.tsv` has this list as the script reads it: the names
it never replaces and the paths it never edits, each with its reason.

- **On-disk signatures.** Saved files on users' disks carry these (at the
  start of a file or of a packed section, a key set file at its end), and
  renaming one makes those files unreadable. Step 0 found these eight and
  no other:
  `BTSOLVE2` (`io/paused_solve.cpp`, the original's .slsolve format),
  `BTZ1` (`io/blockpack.cpp`), `BTPANEX3` and `BTPANEX4`
  (`solve/panex_search.cpp`), `BTSLKEY2` and `BTSLKEY3`
  (`solve/slide_levels.h`), `BTSLSV01` and `BTSLSV02`
  (`solve/slide_saved.cpp`). All but `BTSOLVE2` are written as char arrays
  (`{'B', 'T', ...}`), so a text search for the word misses them; they are
  safe from the script for the same reason, but leave their names and
  comments alone too. The tests write the same bytes and two signatures
  the readers must refuse, `BTSOLVE1` (`test_analysis_files.cpp`) and
  `BTSLKEY4` (`test_slide_levels.cpp`): those stay as well. Nothing else
  that is saved carries a `bt` name: not the folder names of saved
  searches, the app's stored settings, the API or the Python module.
- **BurrTools itself.** Credits (AUTHORS, README), the compatibility work
  (ShapeLogic opens BurrTools' files), the benchmark that runs the original
  (`just burrtools-compare`, `BURRTOOLS_DIR`), comments like "after X of
  BurrTools V2", the paths to the original's checkouts (`burrtools_alpha`,
  `burrtools_rotations`, `../burr-tools`), and
  the original's own names where the code or a doc cites them: its
  environment variables and macros (`BURRTOOLS_THREADS`,
  `BURRTOOLS_NO_SIMD`, `BURRTOOLS_SLIDE_DIR`, `BT_ROT_DEBUG` and
  `BT_ROT_DUMP` in `solve/rotation_rules.h`, `bt_assert`), its functions
  and files (`bt_message`, the `cb_Btn*` callbacks, `bt2_assemble.h`) and
  its BT2 solver (`BT2`, `bt2`, `pausedBt2`).
- **Names that users' files and set-ups carry:**
  - the solver names `btclassic` and `burrtoolsclassic`
    (`solve/solver_type.cpp`) stay as aliases; `"bt2"` stays in the test
    that checks it is refused;
  - `BT_USER_DATA` is renamed, and the app keeps reading the old name when
    the new one is not set (step 4). It is the one switch a packaged app
    takes, every release since v1.0 has it and the website names it: a
    user who set it would otherwise start with the default data folder,
    their settings and recent files seemingly lost, and nothing would say
    why.
- **Names that are gone from the code.** A doc that records the removal of
  a name keeps the name it had; renaming it would describe something that
  never existed. In docs/CLEANUP-PLAN.md: `BT_OLD_REPO`, `BT_TEST_SHARDS`,
  `bt_busy`, `bt_test_slide_session_*`, `bt:log`, `bt:rescue`, `bt:ready`.
  The tool binaries of the oracle (`bt_roundtrip`, `bt_resume`,
  `bt_panex_resume`, `bt_blockpack`, `bt_imports`, `bt_solutions`) and
  `BT_V2_REPO` are named only in the dated docs now.
- **The CSS class `bt`** (`.cfm .bt`, `<span class="bt">`, the tests'
  `#newPuzzle .bt button`): the mockup's name for a dialog's button row
  and the brand text, not BurrTools. The same for `btn`, `btns`,
  `btnNew`, `abtn`, `gbtn`, `cachebt` (buttons), `bthick` (a border's
  thickness) and `btoa`.
- **Puzzle and test data:** every `.xmpuzzle` file, `PelikanBurr`,
  `SolidSixPieceBurrs` (a burr is a kind of puzzle), everything under
  `tests/data/` and `tests/regression/baseline/`, the fake engine's
  recordings (`web/fake-engine/recordings/`), the lock files of npm.
- `.xmpuzzle` tag and attribute names.

## The rename map

These are the names step 0 found at e7e7176, by kind. `tmp/bt-names/map.tsv`
has one row for each (old, new, step, count, the files a row is limited to,
a note); the count beside a step here is its occurrences in the files that
are renamed.

| Kind | Old | New | Step |
|---|---|---|---|
| C++ macros (834) | `BT_ASSERT`, `BT_ASSERT_MSG` | `SL_ASSERT`, `SL_ASSERT_MSG` | 1 |
| | `BTE_CHECK`, `BTE_TRY`, `BTE_CAT`, `BTE_CAT_`, and the macros' own variable `bteResult` | `SLE_CHECK`, `SLE_TRY`, `SLE_CAT`, `SLE_CAT_`, `sleResult` | 1 |
| | `BT_SIMD_NEON`, `BT_SIMD_X86` | `SL_SIMD_NEON`, `SL_SIMD_X86` | 1 |
| | `BT_VERSION_STRING` | `SL_VERSION_STRING` | 1 |
| C++ namespace (1,262) | `bt::` (also `::bt::`), `namespace bt` (also `namespace bt::app {`, `using namespace bt`, the closing comments) | `sl::`, `namespace sl` | 2 |
| CMake targets (160 with the next three) | `btcore`, `btengine`, `bt_tests`; `libbtcore` in comments and docs | `sl_core`, `sl_engine`, `sl_tests`; `libsl_core` | 3 |
| CMake functions and variables | `bt_set_warnings`, `bt_test_timeout`, `bt_test_sources`, `bt_core_found`, `bt_core_listed` | `sl_set_warnings`, `sl_test_timeout`, … | 3 |
| CMake options | `BT_PYTHON`, `BT_PYTHON_MODULE`, `BT_SANITIZE`, `BT_WERROR`, `BT_BUILD_TESTS`, `BT_UNIT_SHARDS`, `BT_TEST_TIMEOUT_SCALE` | `SL_PYTHON`, … (as `SL_VERSION` already is) | 3 |
| | the same after `-D`: `-DBT_WERROR`, `-DBT_PYTHON` (`-DBT_SANITIZE`, `-DBT_BUILD_TESTS`: no occurrence now) | `-DSL_WERROR`, … | 3 |
| Environment (194) | the app's: `BT_ENGINE` (and `\nBT_ENGINE` in `web/tests/global-setup.ts`), `BT_USER_DATA`, `BT_CALL_LOG`, `BT_AUTOSAVE_SECONDS`, `BT_BACKGROUND`, `BT_PACKAGED_APP`, `BT_OUT_DIR` | `SHAPELOGIC_ENGINE`, `SHAPELOGIC_USER_DATA`, … | 4 |
| | the tests': `BT_REQUIRE_ALL`, `BT_DUMP_OUT`, `BT_PANEX_BENCH_N`, `BT_MINK_STRESS`, `BT_BURR_GLAR` | `SHAPELOGIC_REQUIRE_ALL`, … | 4 |
| Web IPC (628 with the next four) | 51 channels `bt:*` (`bt:call`, `bt:check-updates`, …), the renderer's window event `bt:cache-deleted`, and `bt:cli-tool`, which a test checks is not handled | `sl:*` | 5 |
| Web globals and types | `window.bt`, `window.btApp`, `btApp`, `BtApp`, `BtBridge`; the global's name as a string and a key (`'bt'`, `readonly bt:`) and the tests' variables for it | `window.sl`, `window.slApp`, `slApp`, `SlApp`, `SlBridge`; `'sl'`, … | 5 |
| Web tests' CLI helper | `btJson` (it runs `shapelogic_cli --json`) and the variable holding its result (`const bt =`, `bt.assemblies`, `bt.solutions`, `bt.totalmoves`) | `cliJson`, `fromCli` | 5 |
| Web scheme and defines | `bt-app://` (and `bt-apps`, a near miss a test refuses), `__BT_VERSION__`, `__BT_DEMO_BASE__`, vite plugins `bt-demo-page`, `bt-dev-csp`, the console prefix `bt engine proxy:` | `sl-app://`, `sl-apps`, `__SL_VERSION__`, …, `engine proxy:` | 5 |
| WASM exports | `_bt_start`, `_bt_write`, `_bt_poll`, `_bt_take`, `_bt_taken_data`, `_bt_taken_size`, `_bt_log_take`, `_bt_log_data`, `_bt_log_size`, and the C functions without the underscore (`engine/wasm_main.cpp`) | `_sl_*`, `sl_*` | 5 |
| Test temp names (129 with the next two) | `bt_test_` (`ScratchDir`'s prefix), the web tests' `bt-e2e…`, `bt-session-`, `bt-cache-` and 11 more | `sl_test_`, `sl-e2e…`, … (as `sl-vitest-` already is) | 6 |
| The CLI's old name | `bt` for `shapelogic_cli` in comments, prose and test titles ("solved as bt solves it", `` `bt --json` ``), in the 10 files the map names; `BT`, `--bt` and `.bt` in `tests/cli/test_cli.py` and its ctest line | `shapelogic_cli`; `CLI`, `--cli`, `.cli` (as `regress.py --cli`) | 6 |
| Small leftovers | `bt/io`, `bt/solve/x.h` in comments (the include folder's old name); the local variable `bt` in `mesh/cube_mesh.cpp`; `bt-venv` | `shapelogic/io`, …; `bestTransform`; `sl-venv` | 6 |

Why these: `sl` and `SL_` are as short as what they replace, and `SL_VERSION`
already uses the prefix. The C++ code has no `sl` namespace or macro to clash
with (two tests have a local variable `sl`; step 2 renames those first), and
step 0 found none of the new names in use. Environment variables share a
process with other programs, so they take the long `SHAPELOGIC_` prefix,
which 19 variables already use. Libraries become `sl_core` / `sl_engine`
because `shapelogic_engine` is already the shipped executable. A name that
holds the command-line tool or its output says so (`CLI`, `cliJson`,
`fromCli`) rather than `sl`. No old name is kept as an alias (code, scripts
and docs all change together), except the two kinds under "What stays" that
users' files and set-ups carry: the solver names and `BT_USER_DATA`.

What changed from the first table, and why: `BT_TEST_TIMEOUT_SCALE` is a
CMake cache variable, not an environment variable (step 3, `SL_`);
`bt_roundtrip` and the other tool binaries, `BT_OLD_REPO`, `BT_V2_REPO`,
`_bt_busy`, `BT_TEST_SHARDS` and the fixed test folder names are gone from
the code; `btJson` becomes `cliJson` and `BT` becomes `CLI`, not `slJson`
and `SL`; there are 51 channels, not 34.

## Who does each step

Most steps are mechanical: a script edits and the compilers and tests check
the result, so Opus 5.5 High does them. Fable 5.2 High does the two steps
where a wrong call could slip past every test: step 0, which decides what
must not be renamed (a renamed file signature makes users' saved files
unreadable, and no test opens an old user's file), and step 7, which
reviews the whole branch before it merges.

| Step | What | Risk | Model |
|---|---|---|---|
| 0 | Scan, sort every name into "rename" or "stays", write the map and the script | Medium: judging which names files on disk or other programs depend on | Fable 5.2 High |
| 1 | C++ macros | Low: the compiler finds every miss | Opus 5.5 High |
| 2 | The C++ namespace | Low: the compiler finds every miss; the most files | Opus 5.5 High |
| 3 | CMake targets, functions and options | Low to medium: an option a preset passes and no CMake file reads is ignored, not an error | Opus 5.5 High |
| 4 | Environment variables | Medium: a missed variable is ignored silently, not an error | Opus 5.5 High |
| 5 | The web app and the WASM engine | Medium: IPC names are strings, and the scheme is the renderer's origin | Opus 5.5 High |
| 6 | Leftovers and the guard in `just check` | Low | Opus 5.5 High |
| 7 | Review of the whole branch, `just gate`, merge | Medium | Fable 5.2 High |

## Steps

### Before step 1

- [ ] `feature/cleanup-and-optimize` is merged into main, and no other
      branch with large changes is open (check `git worktree list` and ask
      the user about any open branch). Do the work on a branch of its own,
      `feature/sl-names`, in a worktree beside the repo base.

### Step 0: scan and map (Fable 5.2 High)

- [x] Scan the tree at the starting commit for every name of the kinds
      above (`bt`, `bt_*`, `Bt*`, `bt*` + capital, `BT_*`, `BTE_*`, `bt-*`,
      `bt:*`), outside `site/public/demo/assets/`. Write
      `tmp/bt-names/map.tsv` (old, new, step, count) and sort out each name
      the table above does not have: rename it, or add it to "What stays"
      with the reason.
- [x] Write `tmp/bt-names/rename.py`: whole-word replace over the paths a
      step gives, skipping the paths under "What stays" and the docs that
      stay, printing a count for each name.
- [x] Nothing to commit in step 0 if the table needed no change; otherwise
      the updated table in this file is the commit.
- Done (2026-11-27, at e7e7176; nothing built or run): `map.tsv` has 164
  rows (3,207 occurrences in 528 files: step 1 834, step 2 1,262, step 3
  160, step 4 194, step 5 628, step 6 129) and `stays.tsv` 28 names and 20
  paths; `rename.py --scan` finds no name of the old kinds outside the
  two. The table, "What stays" and the steps below were changed for what
  the scan found (the list is under the table). If the branch moves to a
  later commit before step 1, run `make_map.py` and `rename.py --scan`
  again: both files are git-ignored and belong to this worktree.

### Step 1: C++ macros (Opus 5.5 High)

Scope: `core/`, `engine/`, `cli/`, `python/`, `tests/`, `.clang-tidy`, the
CMake files that pass `BT_VERSION_STRING`, the live docs.

- [x] Rename the macros (`BT_ASSERT*`, `BTE_*` with `bteResult`,
      `BT_SIMD_*`, `BT_VERSION_STRING`): `rename.py 1 .`. The new names are
      as long as the old, so the aligned `\` of `engine/rpc.h` stay.
- [x] Test: `just build dev`, `just build release`, `just check`,
      `just test`, `just python-test`.
- [x] Commit: "Rename the BT_ / BTE_ macros to SL_ / SLE_".
- Done (2026-11-27): 834 replacements in 115 files, as the map counts;
  only the dated docs keep the old names. Dev and release built from
  clean without a warning, `just test` 31 of 31, `just python-test` 43,
  `just check` clean (226 files); the demo rebuilt.

### Step 2: the C++ namespace (Opus 5.5 High)

Scope: every C++ file, the Python bindings, the live docs.

- [x] Rename the local variables called `sl` in
      `tests/unit/model/test_tilted_tray.cpp` and
      `tests/unit/app/test_tools_queries.cpp` first.
- [x] Rename `namespace bt` and its nested namespaces, `bt::` and
      `using namespace bt`: `rename.py 2 .`. Its two rows (`bt::`,
      `namespace bt`) also catch `namespace bt::engine {`, `::bt::` and
      the closing comments (`}  // namespace bt`); the scan found no other
      form.
- [x] Test: as step 1, plus `just site-demo` (the WASM build compiles the
      engine too) and the contract tests within `just test`.
- [x] Commit: "Rename the C++ namespace bt to sl".
- Done (2026-11-27): the locals became `slider` and `slope`; no other `sl`
  identifier or macro was in the C++. 1,262 replacements in 398 files
  (`bt::` 471, `namespace bt` 791), as the map counts; only the dated docs
  and this plan keep the old namespace. "a `sl::StopToken`" became "an" in
  AGENTS.md and the site's conventions page. Dev and release built without
  a warning, `just test` 31 of 31, `just python-test` 43, `just check`
  clean (226 files), `just site-build` 8 of 8; the demo rebuilt.

### Step 3: CMake targets, functions and options (Opus 5.5 High)

Scope: `CMakeLists.txt` files, `cmake/`, `CMakePresets.json`,
`python/pyproject.toml` (the wheel's build passes `BT_PYTHON` and
`BT_BUILD_TESTS`), `.github/`, the live docs.

- [x] Rename the targets, the functions and variables, and the options
      with their `-D` forms: `rename.py 3 .`.
- [x] No script names a `bt` binary by its path any more (step 0: the
      oracle's tools are gone; `bt_tests` is named by CMake, the docs and
      two comments, all in the map), and no ctest name is built from one.
      Check that this is still so.
- [x] Build folders: a configured folder keeps the old options in its
      cache and the old binaries beside the new ones. With nothing else
      building, configure each preset fresh (`just clean`, or delete one
      preset's folder at a time) and build again.
- [x] Test: `just build dev`, `just build release`, `just test`,
      `just check`, `just python-wheel` (it reads `pyproject.toml`'s
      options).
- [x] CMake does not refuse an option nothing reads (a preset's
      `SL_WERROR` beside a `BT_WERROR` in the CMake files builds without
      `-Werror`, with at most a warning). So check what the presets
      pass arrived: `-Werror` in `build/dev/compile_commands.json`, and
      after `just build asan`, `-fsanitize=address` in
      `build/asan/compile_commands.json` and time limits six times dev's
      in `ctest --preset asan --show-only=json-v1` (`SL_SANITIZE`,
      `SL_TEST_TIMEOUT_SCALE`).
- [x] `.github/workflows/release.yml` passes `-DBT_WERROR=OFF`. CI runs it
      only on a tag or by hand. Ask the user whether to dispatch it once
      on the branch; otherwise the next release is its first run, and say
      so in the Done line.
- [x] Tell the user the renamed options (`-DSL_PYTHON=…` and so on).
- [x] Commit: "Rename the bt CMake targets and BT_ options to sl / SL_".
- Done (2026-11-27): 160 replacements in 25 files, as the map counts
  (and "a" became "an" before `sl_test_timeout()` in docs/CLEANUP-PLAN.md).
  No script names a test binary by its path (the justfile, `gate.sh` and
  CI go through ctest; the others name `shapelogic_cli` or
  `shapelogic_engine`), and no ctest name is built from one. The dev,
  release and wasm folders were deleted and configured fresh: their
  caches hold only `SL_` options, and dev and release build
  `tests/sl_tests`, `libsl_core.a` and `libsl_engine.a`. Every ShapeLogic source in dev has
  `-Werror` (the 152 compile lines without it are all under `_deps/`);
  asan has `-fsanitize=address,undefined` on all 221 and time limits six
  times dev's (720/1800/3600/7200 s against 120/300/600/1200 s); a scratch
  configure with the wheel's defines builds no tests and flags only a
  planted `BT_PYTHON` as unused. Dev and release built without a warning,
  `just test` 31 of 31, `just check` clean (226 files), `just
  python-wheel` 43 tests (1 skipped), `just regress --tier fast` 19 of
  19, `just site-build` 8 of 8; the demo rebuilt (only its
  `manifest.json` changed). No workflow was dispatched: release.yml's
  `-DSL_WERROR=OFF` runs only on a tag or by hand, so the next release is
  its first run. The options the user types: `-DSL_PYTHON=ON|OFF|AUTO`,
  `-DSL_WERROR`, `-DSL_SANITIZE`, `-DSL_BUILD_TESTS`, `-DSL_UNIT_SHARDS`,
  `-DSL_TEST_TIMEOUT_SCALE`.

### Step 4: environment variables (Opus 5.5 High)

Scope: `web/` (main, tests, tools, the vite and Playwright configs),
`tests/` (`gate.sh`, `bench/bench.py`, four unit tests), the justfile and
its recipe help text, the live docs.

- [x] Rename every variable in the map's step 4 to `SHAPELOGIC_*`:
      `rename.py 4 .`. Where a variable is read in more than one place
      (`BT_ENGINE` in the app, the e2e setup and the justfile), all of
      them change in this step.
- [x] `BT_USER_DATA`, by hand after the script (see "What stays"): in
      `switches()` of `web/src/main/security.ts` the data folder is
      `SHAPELOGIC_USER_DATA`, else `BT_USER_DATA`, with a comment saying
      why the old name is read, and a case in `security.test.ts` for each
      (the new name wins when both are set). Nothing else reads the old
      name.
- [x] README.md:89 names `just oracle` and `BT_OLD_REPO`, both gone:
      write the line for `just burrtools-compare` and `BURRTOOLS_DIR`.
- [x] The longer names move line breaks: `npm run format` in `web/`
      (Prettier), then read what it changed.
- [x] Test: `just test`, `just app-test`, `just app-check`, and these e2e
      specs (they start the app with `BT_USER_DATA` and `BT_ENGINE`, and
      check autosave, the background app and the logs):
      `just app-e2e files.e2e.ts logs.e2e.ts windows.e2e.ts`. Then
      `just app-dist --dir` and `just app-smoke` (`BT_PACKAGED_APP`).
- [x] Tell the user the new names, the ones they type in particular:
      `SHAPELOGIC_ENGINE`, `SHAPELOGIC_USER_DATA` (the old name still
      works), `SHAPELOGIC_BACKGROUND`, `SHAPELOGIC_OUT_DIR`. Update the
      memory notes that name the old ones.
- [x] Commit: "Rename the BT_ environment variables to SHAPELOGIC_".
- Done (2026-11-27): 194 replacements in 35 files, as the map counts.
  `switches()` takes `SHAPELOGIC_USER_DATA`, else `BT_USER_DATA`, with the
  reason in a comment; `security.test.ts` has a case for the old name
  alone (also beside an empty new one) and one for both set (the new one
  wins), packaged and not. Besides those two files `BT_USER_DATA` is named
  in `web/README.md` and the site's commands page ("still works"), so
  step 6's guard allows it there too. README.md names `just
  burrtools-compare` and `BURRTOOLS_DIR`. Prettier rewrapped two test
  files; the comments and docs the longer names pushed over their width
  were wrapped by hand. `just test` 31 of 31, `just app-check` clean,
  `just app-test` 698 tests in 69 files, `just app-e2e files.e2e.ts
  logs.e2e.ts windows.e2e.ts` 30 passed (`logs.e2e.ts` also matches
  `dialogs.e2e.ts`; the first run failed 8 launches with
  Electron's own install errors, its first unpacking racing the workers
  by the look of them, and passed whole when run again), `just app-dist --dir` and `just app-smoke` 1 passed, `just
  site-build` 8 of 8; the demo rebuilt (only its `manifest.json`
  changed). The names the user types: `SHAPELOGIC_ENGINE`,
  `SHAPELOGIC_USER_DATA` (`BT_USER_DATA` still works),
  `SHAPELOGIC_BACKGROUND`, `SHAPELOGIC_OUT_DIR`; the others are
  `SHAPELOGIC_CALL_LOG`, `SHAPELOGIC_AUTOSAVE_SECONDS`,
  `SHAPELOGIC_PACKAGED_APP`, `SHAPELOGIC_REQUIRE_ALL`,
  `SHAPELOGIC_DUMP_OUT`, `SHAPELOGIC_PANEX_BENCH_N`,
  `SHAPELOGIC_MINK_STRESS` and `SHAPELOGIC_BURR_GLAR`. The memory notes
  live outside the worktree and were left to the session that ran it.

### Step 5: the web app and the WASM engine (Opus 5.5 High)

Scope: `web/` (main, preload, renderer, demo, tests, vite configs),
`engine/wasm_main.cpp` and the `EXPORTED_FUNCTIONS` list in
`engine/CMakeLists.txt`, `site/` (its CSP check reads
`web/src/main/security.ts`), the live docs.

- [x] Rename the IPC channels, the preload globals and their types, the
      scheme (`APP_SCHEME` in `web/src/main/security.ts` and the CSP that
      names it), the vite defines and plugin names, the WASM exports and
      their callers in `web/src/demo/`, the tests' `btJson`:
      `rename.py 5 .`. Some rows of this step are short phrases limited
      to one file (`'bt'`, `const bt =`, `bt.solutions`): read their lines
      with `rename.py -n -v 5 .` first. A bare `bt` left in a comment or a
      test title of the e2e tests is the CLI's old name, and step 6's.
      Then `npm run format` in `web/`.
- [x] The scheme is the renderer's origin. At e7e7176 nothing in
      `web/src/` uses `localStorage`, `sessionStorage`, IndexedDB, cookies
      or a service worker, and the one partition (`updates`) is the update
      check's own, so no saved state is lost (the scheme's code cache is
      made again); check that again, and check the Electron session for
      anything kept per origin (permissions), before renaming it.
- [x] `just app-gen`, and check that `api.generated.ts` comes out the same
      (at e7e7176 it has none of the map's names; `BTSOLVE2` in a comment
      of it stays).
- [x] `just site-demo --check` to rebuild the demo bundle and drive it.
- [x] Test: `just app-check`, `just app-test` (`preload.test.ts` calls
      every `bt` and `btApp` method), `just site-build`, and the e2e specs
      that cover IPC, security and the preload:
      `just app-e2e ipc.e2e.ts security.e2e.ts files.e2e.ts exports.e2e.ts`.
- [ ] Ask the user to check the app (`just app-dev`): open, solve, save,
      export, check for updates. Nothing should look different.
- [x] Commit: "Rename the web app's bt IPC, globals, scheme and WASM
      exports to sl".
- Done (2026-11-27): 628 replacements in 82 files, as the map counts,
  and `window.bt` by hand in docs/FRONTEND-PLAN.md (a finished plan, but
  AGENTS.md sends agents to search it; its `BT_ENGINE` and `bt --json`
  are left). Prettier rewrapped `stacking.e2e.ts` (`fromCli` is longer).
  The origin: nothing in `web/src/` or `web/tests/` uses `localStorage`,
  `sessionStorage`, IndexedDB, cookies or a service worker; the default
  session grants no permission (`GRANTED` is empty, and the handlers
  store nothing); the one partition, `updates`, is in memory (no
  `persist:`) and only fetches; no menu has a zoom role, so no zoom level
  is kept per origin. Only the code cache is made again. `just app-gen`
  left `api.generated.ts` as it was. `just site-demo --check` drove every
  puzzle type (only `index.html`, `manifest.json` and the bundle's JS
  changed; the `.wasm` minifies its export names and came out the same).
  `just app-check` clean, `just app-test` 698 tests in 69 files, `just
  site-build` 8 of 8, `just app-e2e ipc.e2e.ts security.e2e.ts
  files.e2e.ts exports.e2e.ts` 25 passed, `just test` 31 of 31. The
  check of the app by hand is the user's (open, solve, save, export,
  check for updates).

### Step 6: leftovers and a guard (Opus 5.5 High)

- [x] Rename the test temp names, `BT` and `--bt` in
      `tests/cli/test_cli.py` (and its ctest line in
      `tests/CMakeLists.txt`), `bt-venv`, the include folder's old name in
      comments (`bt/io`), the local `bt` of `mesh/cube_mesh.cpp`, and the
      word "bt" for `shapelogic_cli` in comments, prose and test titles:
      `rename.py 6 .`. The e2e screenshots are named in the tests, not
      after their titles, so a changed title moves no file.
- [x] `bt/` and `bt` grow to `shapelogic/` and `shapelogic_cli`: wrap by
      hand the comment lines that pass 100 columns in C++, and
      `npm run format` in `web/`.
- [x] Scan again (`rename.py --scan`: no row of the map occurs but
      `BT_USER_DATA` where step 4 kept it, and no name is uncovered).
      Every name left must be on the "What stays" list.
- [x] Add a guard to `just check`: a short script (`tools/check-names.sh`)
      that fails on the old prefixes in tracked files, with an allow list
      for "What stays" and the dated docs (`stays.tsv` has both, and
      `rename.py`'s `KINDS` the patterns of the old kinds; `BT_USER_DATA`
      is allowed in `web/src/main/security.ts` and its test, and, as step
      4 left them, `web/README.md` and the site's
      `code-base/commands.md`). Agents copy
      names from old code and docs, so without it `BT_` comes back.
- [x] Test: `just check` (the guard passes, and fails on a planted
      `BT_ASSERT`), `just test`, `just app-test`.
- [x] Commit: "Rename the last bt names; check for them in just check".
- Done (2026-11-27): 129 replacements in 70 files, as the map counts;
  `cube_mesh.cpp`'s local is `bestTransform`. By hand: docs/FRONTEND-PLAN.md's
  `BT_ENGINE` (twice) and `bt --json` / `bt -q -n -d`, since AGENTS.md
  sends agents there. No C++ comment passed 100 columns; one e2e header
  comment was rewrapped, Prettier changed nothing (the four longer test
  titles are strings it leaves). `rename.py --scan`: only `BT_USER_DATA`
  where step 4 kept it (11), no name uncovered. The guard is
  `tests/check-names.sh` (no `tools/` folder; it sits beside `gate.sh`),
  `just check-names`, the first dependency of `just check`: `git grep -P`
  over the tracked text files for the old kinds of `rename.py`'s `KINDS`,
  minus the dated docs, the finished plans, this plan, the mockup, recorded
  data, generated files and lock files, with an allow list by path and
  name for "What stays" (the signatures, `btclassic`, the original's
  names, the CSS class `bt`, `BT_USER_DATA` in its four files, the names
  docs/CLEANUP-PLAN.md records as gone). The docs that describe the check
  name no old name, since it reads them too. `just check` clean (226
  files) and failed at once on a planted `BT_ASSERT`; `just build dev` and
  `just build release` without a warning, `just test` 31 of 31 (with
  `cli`, now `--cli`), `just app-check` clean, `just app-test` 698 tests in
  69 files, `just site-build` 8 of 8; the demo rebuilt (only its
  `manifest.json` changed).

### Step 7: whole-tree check and merge (Fable 5.2 High)

- [x] Review the branch's whole diff against main: every changed token is
      in the map, nothing under "What stays" changed (the file signatures
      above all), no string that is not a name changed, and the live docs
      match the code. Fix what is wrong in a commit of its own.
- [ ] `just gate` (about 75 minutes, nothing else running).
- [ ] Ask the user whether to run the full e2e suite (`just app-e2e`) as
      well.
- [x] Update CLAUDE.md and AGENTS.md for the new names (`bt_tests`,
      `btcore`, the env vars).
- [ ] Update the memory notes for the new names (they live outside the
      worktree: the main session's).
- [ ] With the user's go-ahead, merge `feature/sl-names` into main. Any
      branch opened later is made from the merged main.
- Review done (2026-11-27, e7e7176..e3e38a5): the map applied to each of
  the 531 changed files as e7e7176 had it gives HEAD's text exactly for
  510, each row as often as the map counts; what is left in the other 21
  is the hand edits the steps name (the `BT_USER_DATA` fallback and its
  tests, README.md's line, the `sl` locals of two tests, "an" before
  `sl::` and `sl_test_timeout()`, the guard and its lines in the
  justfile and the docs, docs/FRONTEND-PLAN.md), lines wrapped again and
  the demo. The eight signatures, their char arrays and every line that
  names one are as at e7e7176, with `tests/data/` and the other recorded
  files; none of the new names was in use at e7e7176. The 51 channels of
  the main process and the preload are the same set, and e7e7176's;
  `api/`, docs/API.md and `api.generated.ts` did not change; the Python
  module is still `shapelogic`; every `SL_` option a preset, the wheel or
  a workflow passes is declared; no code takes environment variables by
  a prefix. Outside the map's kinds no name of BurrTools' two letters is
  left (looked for in any case, also inside camelCase names), and the
  guard reports each kind when planted. Fixed: "a" before `SL_ASSERT`
  and `SL_ASSERT_MSG` (eight places, four of them C++ comments), five
  lines the longer names had left badly wrapped (docs/CLEANUP-PLAN.md,
  `web/README.md`, the header comment of `web/src/main/index.ts`); the
  demo rebuilt (only its `manifest.json` changed). Left as they are: 25
  comment lines the longer names took past their block's width but not
  past 100 columns (step 6 wrapped at 100), and `__SL_DEMO_BASE__`, which
  `web/demo.vite.config.ts` defines and nothing reads (so at e7e7176).
  AGENTS.md has a rule for the names (what carries `sl` and
  `SHAPELOGIC_`, the guard, the signatures, the old names in the dated
  docs); CLAUDE.md names none.

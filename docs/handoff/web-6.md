# Handoff: web stage 6 (menus, files, settings, recovery)

**Date:** 2026-10-09 · **Next:** the short audit of this stage's save,
recover and close paths (Fable 5.1, max effort; "Brief for the audit"
below), then stage 7, the exports.

## Done

### Two C++ commits first
- **`6e0a9b6` Engine: document.openBytes path and dirty.** A rescue copy
  reopened after the engine died stands for a file and holds changes no
  file has, and `document.openBytes` gave a clean document without a path
  (it would have looked saved, and its Save had nowhere to go).
  `document.openBytes` takes `path` (the document's file: where
  `document.save` writes) and `dirty` (`Document::markUnsaved`: dirty
  until saved), both optional; an empty path is refused
  (`invalid_argument`). The app test checks `markUnsaved` and a save
  after it; the contract session reopens its bytes with a path and dirty,
  saves to that path and is clean. Its finishing steps: `just test-all`
  6/6, the contract suite (95 methods), `python3 api/gen_api_md.py`,
  `just web-gen`, the staleness test.
- **`3035573` Core: a save replaces the file only once the new one is
  whole.** Found while building the save path: `document.save` wrote
  straight onto the puzzle's file (`gzopen(path, "wb9")`), so a save that
  failed or was cut off (a full disk, the engine killed mid-write) left a
  truncated file where the user's puzzle was; `savePausedSolve` even
  removed the file it failed to overwrite. Both now write `<file>.tmp`
  beside it and rename it over the file once whole, as the original's
  `writePuzzleFile` did (mainwindow_file.cpp); `io::replaceWith` is the
  rename. A unit test makes the temporary file's place a folder: the save
  fails with `IoError` and the old file is unchanged (with the old code
  the save succeeds, so the test fails), for a puzzle and a paused solve.
  Its finishing steps: `just test-all` 6/6, the contract suite,
  `just oracle-roundtrip` (365/365 byte-identical to the original's
  save), `just oracle-files --tier fast` (19/19), `just oracle-resume`
  (395/395). On Windows the rename over an existing file is
  `std::filesystem::rename` (MoveFileEx with replace in both MSVC's and
  libstdc++'s): to be seen in the Windows run.

### The main process (`web/src/main/`)
- `menu.ts`: the menus' template from the menu state the renderer sends,
  Electron's types only (unit-tested). From the original's mainmenu.cpp
  (its macOS table) and the mockup's placeholders: **File** (New…, Open…,
  Open Recent with Clear Menu, Save, Save As…, Import ▸ PuzzleSolver3D…,
  Puzzlecad…, Paused Solver State…, Export ▸ Puzzlecad…, Image…, Vector
  Image…, STL…, Solution Animation (GLB)…, Paused Solver State…, Close;
  Exit off macOS), **Edit** (Undo *label*, Redo *label*, Cut, Copy,
  Paste, Select All (the text fields need them), Notes…, Convert Brick
  Grid Type to Other…, Convert Assemblies to Pieces…; Settings… off
  macOS), **View** (Status Window…, Toggle 3D, the display modes:
  normally with shape colour, with colour constraint colours, the two
  anaglyph modes; the styles: each voxel separately, flat faces with
  edges, wire frames, like STL export), **Help** (Keyboard Shortcuts,
  Tutorial, BurrTools User Guide; About off macOS), and on macOS the
  application menu (About, Settings…, Services, Hide, Quit). Greyed until
  the core or a later stage has them: the two imports, the exports but
  the paused solver state (stage 7), grid conversion, the anaglyph modes,
  the STL-like style. The keys shown are the one table's (sent in the
  state as accelerators) with `registerAccelerator: false`: the press
  reaches the page and the table handles it once.
- `app-ipc.ts`: `window.btApp`'s main side: the file dialogs (filters by
  kind; a name without the kind's extension gets it, and a file that then
  exists is asked about, as the original's Save As did; the dialog starts
  where the last file was), the stored state, the recent files, the
  rescue copy, the help links (the renderer names a link, the main
  process holds the address), the menu state, the close; every message
  checked (`isMenuState`, `isRescueInfo`...), only the app's windows
  answered.
- `state-store.ts` (`StateStore`): `state.json` in the app's data
  directory: the renderer's settings and layouts, the recent files (ten,
  newest first, once each), the window's bounds; read once, written 300
  ms after a change to `state.json.tmp` renamed over it, flushed at quit;
  a file that is not JSON, or values of the wrong kind, read as absent.
- `rescue.ts` (`RescueStore`): `__rescue.xmpuzzle` (the document's bytes
  from `document.saveBytes`: plain XML, which BurrTools opens as any
  puzzle) and `__rescue.json` (its file, name, revision, time and the
  SHA-256 of the bytes), the record written first, each by rename; a copy
  whose record does not match (the app died between the two) is offered
  as a puzzle without a file, since its record may name another
  document's file.
- `index.ts`: the window's close is held and asked of the renderer
  (`close`); the renderer takes it at once (`closeTaken`) and answers
  `closeWindow` after its question, or `closeCancelled`; a renderer that
  has not taken it within 4 s (hung, crashed) gets the question natively
  ("The window does not answer. Close it anyway?"). A quit held by a
  window's question goes on once that window closes (Cmd+Q, then Don't
  Save, quits). Navigation away from the page and new windows are
  refused (a file dropped outside the drop zone used to navigate to it).
  macOS's `open-file` and a command line's `.xmpuzzle` / `.btsolve` open
  in the window; the window's bounds are kept; `setDocumentEdited` and
  `setRepresentedFilename` on macOS. `BT_USER_DATA` moves the app's data
  (each e2e launch has its own, so tests never touch the user's).
- `preload/index.ts` exposes `window.btApp` (`engine/app-bridge.ts`: the
  types and checks shared by both sides) beside `window.bt`; the path of
  a dropped file through Electron's `webUtils.getPathForFile`.

### The renderer
- `shell/files.ts` (`Files`): New (a dialog with the grids this version
  solves: the grid chosen once), Open, a path (Open Recent, the system's,
  a drop; a `.btsolve` file is imported), Save (Save As without a file),
  Save As, each replacing the puzzle after the original's
  `confirmDiscard` (Save, Don't Save, Cancel); a save while a run is
  under way is offered at a save point (the Solver's
  `Autosave.saveAtSavePoint(path, byUser)`: the runs pause where they can
  be saved, the puzzle is saved to its file or the one chosen, they carry
  on), while an analysis runs it says to stop or finish it; the window's
  close with the same question; Export and Import ▸ Paused Solver State
  (`solve.exportPaused` for the problem the Solver shows when it has a
  paused solve; `solve.importPaused` a new, untitled document, shown in
  the Solver); a file opened or saved goes first in Open Recent, a recent
  file the engine cannot read (`io_error`) leaves it; a dropped file
  without a path (from another application) is opened by its bytes.
- `shell/rescue.ts` (`Rescue`): the copy written 300 ms after a change,
  at most every 2 s, while the document is dirty; removed once it is
  clean (saved, or another document opened after its question); not
  written while a solve or an analysis is active, as the snapshot or the
  solve records say (a run launched counts before its first state is
  told, an analysis from its reply), or while an edit, a run's call or an
  autosave is under way (the engine would refuse `saveBytes`; the check
  and the call are in one task); writes and
  removals in one queue, so a removal is never overtaken by an earlier
  write. Offered after an engine's restart ("The engine stopped …
  Reopen it?") and at the start for a copy a session that died left
  ("… did not close properly"); a question that must be answered
  (Discard or Reopen; Esc does nothing); nothing is written or removed
  while it waits. Reopen: `document.openBytes {data, path, dirty}`
  (`Store.openRescue`). Not offered on a page reload whose document is
  dirty (the engine's document is newer than the copy).
- `shell/menus.ts` (`Menus`): the menu state (document, dirty, path, the
  labels of the next undo and redo from `edit.history`, solving, the
  Solver's problem paused, any solutions saved, the display mode and
  style, the 3D focus, the keys) sent when it changes (looked at on every
  store change, display change, layout change and once a second); a
  command with a key in the table runs that key's handlers
  (`keyboard.ts`'s new `runAction`), so a menu item and its key are one
  path; the others go to their handlers.
- `shell/dialog.ts`: modal dialogs in the mockup's `.cfm` look (a
  default button, Esc and a click beside it cancel unless the question
  must be answered, Tab kept inside, Enter in a field, the focus given
  back); `askChoice`. The keyboard map waits while one is open.
- `shell/help.ts`: the F1 dialog (Help ▸ Keyboard Shortcuts) over the one
  table with a search; About (the app's version, `engine.info`'s name,
  version and protocol, the credits of AUTHORS, the licence); the Notes
  dialog (`document.setNotes`, allowed while solving; a refused text
  keeps the dialog open with the engine's message).
- `solver/to-shapes.ts` and `to-shapes-form.ts`: Convert Assemblies to
  Pieces, the original's dialog (assmimportwindow.cpp) over
  `solution.toShapes`: source problem (those with saved solutions),
  destination (just add, a new problem, an existing one), the range
  (0 to 1), the filters with the original's defaults (drop disconnected
  and remove identical on; millable and notchable on the brick grid only),
  the shapes' size (0 to 1,000,000 cells); one undo step; refused while
  anything solves before any call.
- `shell/persist.ts`, `prefs.ts`'s `validPrefs`, `layout.ts`'s
  `layoutState` and `restoreLayout`: the settings and the layouts (the
  workspace shown, each workspace's cards) read before the shell starts
  (`main.ts`) and stored when they change; the engine's settings among
  them go to every engine the window starts (`Store.wantedSettings`:
  `settings.set` instead of `settings.get` when an engine runs).
- `state/store.ts`: `openPath`, `openRescue`, `importPaused`, and `save`
  (reads the snapshot again and waits for the save's revision: a save
  sends no `document.changed`, and the top bar must show it clean).
- `shell/keys.ts`: the File rows (Ctrl+N, Ctrl+O, Ctrl+S, Shift+Ctrl+S,
  Ctrl+W), which act in a text field too (`IN_TEXT`); `accelerator`;
  `docs/webui-spec.md` section 6 has the row. 43 chords.
- `shell/topbar.ts`: the temporary New…, Open… and Status… buttons gone;
  `#stage[data-dirty]` and `[data-path]` for the tests. The status
  window is View ▸ Status Window…; the debug statistics stay in the
  Solver (no setting: they are a button there).
- Two fixes on the way: closing the settings dialog when nothing had the
  focus before left it in the hidden search field, so every key counted
  as typing and the keyboard map was dead until a click (found by the
  keyboard e2e); New right after the start found no grids (engine.info
  is read beside the first document): it asks the engine then.

### Tests
- Unit (`tests/unit/app.test.ts`, 14): the store (written whole by
  rename, read back, the recent files' order and limit, a file not JSON),
  the rescue copy (record, removal, a record not matching the copy, no
  record), the menus (each platform's layout, what is greyed, the undo
  label, every key shown and none registered, every item's command one
  the renderer takes, the recent files' labels), the bridge's checks,
  the stored settings read back, the convert dialog's fields;
  `keys.test.ts`: the File keys, `IN_TEXT`, the accelerators, 43 chords.
- e2e (`tests/e2e/files.e2e.ts`, 8): File ▸ New on every grid (the five,
  Cancel keeps the puzzle); every fixture (19) opened by File ▸ Open with
  no `document.changed` after it and saved as a copy by Save As whose
  bytes equal a separate engine's own save of the fixture, clean
  afterwards, its path the copy's, no `.tmp` left, the last in Open
  Recent, an extension added, a cancelled dialog changing nothing, no
  refusal in the call log; an engine killed after edits (Ctrl+D, the
  Notes dialog) and the rescue copy offered, Esc not taking the question
  away, reopened with the edits, its file and its unsaved changes, then
  saved to its file and the copy gone; a copy left by a session that was
  killed offered at the next start and reopened, then Discard removing
  it; the window's close: Cancel keeps it (also after 4.5 s, and no
  native question was asked: a renderer that does not take the close
  fails this, checked by a mutation), Ctrl+W asks the same, Save writes
  the file and closes without a copy left, Don't Save closes without
  saving, a clean document closes without a question; the settings
  (theme, density, undo depth, rotation method, view cube, threads) and
  the layouts (the workspace shown, each workspace's cards) after a
  restart, the engine's settings given to the new engine, a store that
  is not JSON read as the defaults; a save while solving offered at a
  save point (the file holds the paused position, the solve carries on),
  Export ▸ Paused Solver State greyed while it runs and enabled once
  paused, the `.btsolve` imported as an untitled document and continued
  and aborted; the notes (a refused text keeps the dialog), the Undo
  item labelled by `edit.history`, Convert Assemblies to Pieces into a
  new problem with the range asked, About with the engine's version, the
  help links, the greyed items, a dropped file without a path.
- e2e (`tests/e2e/keymap.e2e.ts`, 1): **every row of the keyboard map**:
  the test walks `KEY_MAP`; each of the 43 rows with keys has a scenario
  that sets up what it needs (Bermuda, SolidSixPieceBurrs, Bermuda solved
  with take-apart or prepared, an explorer), presses the row's keys and
  checks what they did; each of the 20 rows of mouse actions only names
  the e2e test that drives it; a row without either, or a scenario whose
  row is gone, fails the test.
- `keys.e2e.ts`: F1 opens its own dialog (searchable), the settings
  page still prints the table. The e2e helpers: `launch` gives each app
  its own data directory and waits for the first document, `openFixture`
  is File ▸ Open Recent's path (a command) answering Don't Save when the
  test left changes, `newDocument` is File ▸ New, `menu`, `menuItem`,
  `answerDialogs` (the file dialogs answered in the main process),
  `command`, `engines`, `closeApp` (windows destroyed: a test's changes
  never ask).

## Gate results
| Check | Result |
|---|---|
| `just web-check` | clean, three times (three TypeScript projects, ESLint without warnings, Prettier) |
| `just web-test` (release engine) | **242/242**, three times, 18.6, 18.3, 18.5 s: 26 files (stage 5's 226; `unit/app.test.ts` 14, `keys.test.ts` 2 more) |
| `just web-e2e` | **50/50**, three times, 5.0, 5.0, 4.9 min (build included): stage 5's 41 and `files` 8, `keymap` 1 |
| perf (each e2e run) | mesh build 6.0, 5.8, 5.7 ms; orbit median 13.3 ms at a 13.3 ms display frame each time, p95 14.7/13.8/14.8 ms; 150–152 renders in 150–152 frames |
| C++ (two commits) | `just test-all` 6/6 after each; the contract suite 0 failed (95 methods); `api-docs`; `oracle-roundtrip` 365/365 byte-identical; `oracle-files --tier fast` 19/19; `oracle-resume` 395/395; `just web-gen` and the staleness test |
| before the gate | a first round found two things: the keys test's search word matched two rows (the test's), and the rescue copy's `saveBytes` was refused once beside a solve being started (`invalid_state` in the call log of the dominoes e2e: the stage's; the quiet check above); a start of the gate was stopped at an ESLint error in a test and run again whole; the next start's first round found the refusal again, once, in the two-problems e2e: after `solve.start`'s reply the record's state is `idle` until the first `solve.state` and the snapshot lags, so the check now asks `Solves.running` (launched counts) and `Solves.analysisActive`; the gate was then run again whole; its third round stalled once in the test of a copy left by a session that died (2 minutes, no line: an action without a time limit), which did not come back in 8 runs alone and 6 runs of the files suite: actions now have a 30 s limit suite-wide (`playwright.config.ts`) and `closeApp` fails, not hangs, when an app does not quit, so a stall names its place; the next start found the keyboard test pressing ↓ in Placements before the drawer had read its lists (a key then is dropped; the test now waits, as the explore e2e does: 8 of 8 after, 2 of 5 before), and the gate was run again whole |

## The reference screenshots
Unchanged: the 36 pass as they are (they are of the view, and the top
bar lost only its temporary buttons). The stage adds none; its dialogs
were looked at in this session's runs (New, the rescue offer, the close
question, the save point's offer, Notes, Convert, About, F1).

## Where this stage departs from the plan, and why
1. **Two C++ commits**: `document.openBytes`'s `path` and `dirty` (the
   rescue copy reopened as what it was), and the safe save (a found bug,
   in the save path this stage exists for).
2. **The rescue copy is written while the document has unsaved changes
   and removed once it is clean**, not "after each change" whatever the
   state: a clean document needs no rescue, and a copy left behind would
   be offered for nothing at the next start. It is also **offered at the
   start** when a session died (the original wrote `__rescue.xmpuzzle`
   when it crashed; here the app's own death is the same case as the
   engine's). It lives in the app's data directory
   (`~/Library/Application Support/BurrTools Alpha/` on macOS), not the
   working directory as the original's.
3. **The close question is the page's**, with the original's three
   answers (its `confirmDiscard`); the main process asks natively only
   when the page does not take the close within 4 s.
4. **The menus show the table's keys and register none**, so a key
   reaches the page once and the one table stays the one source; on
   macOS the system's own items (Quit, Hide, Cut, Copy, Paste) keep
   theirs.
5. **The anaglyph modes are greyed** (the plan left it to this stage):
   three.js's anaglyph effect renders a scene twice through one camera,
   and the view draws in passes (the faded copies' depth pass, two layers
   of solution models, overlays and edge lines on top): fitting it means
   reworking the renderer, for a mode few will use. Later, with the STL-
   like style, which waits for the STL meshes.
6. **Tutorial opens the user guide's "Getting Started" chapter**; the
   original's tutorial was a tab of text per grid (tutorials.cpp) written
   for its own window, buttons and tabs.
7. **One window**: New and Open replace the puzzle, as in the original;
   File ▸ Close closes the window (the app stays on macOS, quits
   elsewhere), as the original's Close / Quit.
8. "Show Notes" of the original is **Edit ▸ Notes…** (the mockup's "Edit
   comment"), a dialog; the original showed the notes in a panel.
9. **Debug statistics** keep their button in the Solver (no setting);
   the status window is View ▸ Status Window….

## What the mockup did differently, and why it changed
- Its top bar had the menu's placeholders (File, Toggle 3D, Export,
  Status, Edit comment, Help, About) as buttons without actions; they are
  Electron's menus (the plan), each placeholder an item: Toggle 3D is
  View ▸ Toggle 3D (the 3D focus, Ctrl+Space), Status the status window,
  Edit comment the notes.
- Its F1 opened the settings dialog's keyboard page (its note said the
  list "lives in Help ▸ Keyboard shortcuts (F1)"): F1 is that dialog now;
  the settings page stays.

## Windows: what to do by hand (not run in this session)
The plan asks for the dev app run once on Windows; this Mac cannot. On a
Windows 10 or 11 x64 machine:
1. Install MSYS2 (https://www.msys2.org) and, in its **UCRT64** shell:
   `pacman -S --needed git mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja`.
   Install Node 22 LTS for Windows (nodejs.org; the `npm` it brings).
2. Bring the repository over (it has no remote): on the Mac
   `git -C ~/git/burr-tools-rotations/burrtools_alpha bundle create ~/bt-alpha.bundle --all`,
   copy the file, then in UCRT64 `git clone bt-alpha.bundle burrtools_alpha`.
3. In UCRT64, in `burrtools_alpha`: `cmake --preset release` and
   `cmake --build --preset release` (zlib, nlohmann/json and Catch2 are
   downloaded). Check the engine by hand:
   `printf '{"jsonrpc":"2.0","id":1,"method":"engine.info"}\n' | build/release/engine/bt-engine.exe`
   prints one line with `"version"`. If the build fails, note the error
   and stop there.
4. In PowerShell, with `C:\msys64\ucrt64\bin` on `PATH` (the engine's
   runtime DLLs): `cd burrtools_alpha\web`, `npm ci`,
   `$env:BT_ENGINE = "..\build\release\engine\bt-engine.exe"`,
   `npm run dev`. (`just` recipes use `env -u`, which PowerShell lacks.)
5. In the app: the status bar leaves "Starting the engine…"; the menu
   bar is in the window (File, Edit, View, Help) with Ctrl keys shown;
   File ▸ Open… `tests\data\puzzles\Bermuda.xmpuzzle`: shapes listed
   and **drawn** in the 3D view (meshes are binary frames: a broken
   frame shows as nothing drawn or an error in the status bar); every
   style; the Solver: Solve with take-apart, the solution plays (pose
   frames); edit something, File ▸ Save As… to a new file in a folder
   with a space in its name, then Save again over it (the rename over an
   existing file); Ctrl+W with a change: the question, Cancel, then
   Don't Save; in Task Manager end `bt-engine.exe` after an edit: the
   app restarts it and offers the rescue copy; reopen it; quit and
   start again: the settings and layout kept; drop a `.xmpuzzle` from
   Explorer on the window.
6. Write down what happened (the working steps and anything wrong, with
   any messages from the PowerShell window) and give it to the next
   session (stage 7) to record in this handoff and fix.

## Known issues and notes
- **The last changes before an engine's death can be lost**: the copy is
  written at most every 2 s (300 ms after a change), so up to that much
  editing is not in it. Nothing is written while a solve or an analysis
  runs (the engine refuses `saveBytes`); the solve's own autosave (20
  minutes) keeps a solve's work, and edits are frozen then anyway (the
  notes are not: a note typed during a solve is in the copy only once
  the solve stops).
- **One rescue copy per app**: the app has one window at a time (a new
  one only when none is open, macOS's Dock); two windows would share the
  copy.
- When the host gives up on an engine (five quick deaths), there is no
  restart and so no offer this session; the copy stays and is offered at
  the next start.
- The native close question after 4 s is for a renderer that does not
  answer; it was checked only in that it is **not** asked when the page
  answers (the e2e's count), not by hanging a renderer.
- The window's bounds are restored without checking that the display
  they were on is still there.
- The Undo and Redo labels cost one `edit.history` call per change
  (coalesced); the menu state is also looked at once a second (the
  Solver's choice of problem tells no one).
- Open Recent is the app's own list; the system's recent documents
  (`app.addRecentDocument`) are not fed.
- On macOS the Help menu (role `help`) gets the system's search field.
- A key pressed in the explorer's drawer while it reads (`data-busy`) is
  dropped, not kept for when it is ready (stage 5's behaviour; the
  keyboard e2e waits).
- One stall of the e2e suite was not explained (above, gate results);
  the limits added make the next one say where it waits.
- From stage 5, still open: the player runs under an explorer;
  Placements reads every piece's list at once; the drawer covers the
  lower part of the view; a direction seen end on cannot be dragged.
  From stage 4: the analysis reads every id first, a solution picked
  while a run thins the list may be gone, every piece hidden at a
  disassembly's end. From stage 3: the maximum holes cannot be set. From
  stage 1b: the one look beside the original GUI.

## Brief for the audit of stage 6 (the save, recover and close paths)
Fable 5.1 at max effort, short: these paths only, findings with concrete
failure scenarios, fixes in a commit `Web 6 audit: <summary>` after the
stage's. Read the diff from `75f7f06` (Web 5) to the stage's commit:
`6e0a9b6` (engine), `3035573` (core), then `Web 6: …`. Where to look:
- **Save**: `shell/files.ts` (`save`, `saveAs`, `saveTo`,
  `saveWhileSolving`, `confirmDiscard`), `state/store.ts` (`save`: the
  reload and `whenRevision` after `document.save`, which sends no
  `document.changed`), `solver/autosave.ts` (`saveAtSavePoint(path,
  byUser)`: pauses, the generation check, `store.save`, the continues in
  `finally`), `main/app-ipc.ts` (`bt:choose-save`: the extension added
  after the dialog and the overwrite question), the core's
  `writeGzipFile` / `replaceWith` / `savePausedSolve` (the temporary
  file beside the target; what happens when the target is a symbolic
  link, read-only, or on another volume than the temporary file cannot
  be, since it is beside it). Questions: can a save be reported done when
  it was not (or the document shown clean when it is not)? Can Save,
  Save As or the close's Save write the wrong document (another one
  opened meanwhile, a rescue copy reopened with another file's path)?
  Can a save at a save point leave the solves paused, or continue a
  solve of another document?
- **Recover**: `shell/rescue.ts` (the queue, `held`, `closed`, `copy`,
  the quiet check, `offer` after a restart and at the start, `discard`),
  `main/rescue.ts` (record first, the hash, `read`), `Store.openRescue`
  and the engine's `document.openBytes {path, dirty}`. Questions: can a
  copy be removed while the document still has unsaved changes, or
  survive a clean close and be offered for nothing? Can a stale copy
  (an older document's) be reopened with a newer document's file as its
  path, so its Save overwrites that file? What happens when the engine
  dies during the offer, during a write, or twice in a row; when the
  restarted engine's empty document arrives before the old write
  finished?
- **Close and quit**: `main/index.ts` (the `close` handler, `allowClose`,
  `closeTimer`, `quitAfterClose`, `askNatively`, `before-quit`,
  `window-all-closed`), `shell/files.ts` (`close`, `closing`),
  `app-ipc.ts` (`bt:close`). Questions: can a window close with unsaved
  changes without the question (a second close while the first question
  is open, the 4 s timer firing after the page took it, a reload while
  the question is open)? Can the window become impossible to close? Does
  Cmd+Q quit after Don't Save, and not after Cancel? Does a solve still
  running at the close pause and leave the engine cleanly?
- Tools that worked in earlier audits: probes as plain `node probe.ts`
  importing the sources by absolute path (Node 22 strips types; renderer
  modules that touch the DOM need `globalThis.window/document` stubs);
  e2e mutations with a copy of the file kept aside (never `git checkout`
  a file with uncommitted work); `answerDialogs`, `menu`, `command`,
  `engines` in `tests/e2e/app.ts`; `BT_CALL_LOG` for refusals.
- The gate after the fixes, three times: `just web-check`, `just
  web-test` (release engine), `just web-e2e`; and for any C++ change
  AGENTS.md's finishing steps (`just test-all`, the contract suite,
  `oracle-roundtrip`, `oracle-files --tier fast`, `oracle-resume`).

## Brief for stage 7 (after the audit)
**Stage 7** (Opus 5.5, medium), FRONTEND-PLAN.md's "Stage 7": the
exports needing no core work: **Image** (the view at 1x and 2x, PNG),
**Vector Image** (three.js's `SVGRenderer` over the flat mesh), **Solution
Animation (GLB)** (`playback.track` keyframes and `playback.mesh` per
piece through three.js's `GLTFExporter`, the original's `gltfexport.cpp`
for the holds and the reassembly), **STL** from the flat mesh as an
interim (the dialog says the bevelled STL meshes come later). Where
things are:
- The menu items exist greyed: `main/menu.ts` (`export.image`,
  `export.vector`, `export.glb`, `export.stl`: make them `item(…)` with a
  new command in `engine/app-bridge.ts`'s `AppCommand`, `COMMANDS`, and
  `shell/menus.ts`'s `run`); the menu state may need a field (a solution
  with a disassembly shown, for GLB).
- Files are written by the main process: add a `btApp` call that writes
  bytes to a path chosen by `chooseSave` (a new `FileKind` per format in
  `app-ipc.ts`'s `KINDS`), written beside and renamed as the core now
  does; the renderer never gets a path any other way.
- The view: `view3d/renderer.ts` (`ViewRenderer`, render on demand),
  `view3d/view.ts`; the player's poses `solver/poses.ts`; the solution
  scene `view3d/solution.ts`.
- If the Windows run (above) was done, record what it found in this
  handoff's Windows section first.

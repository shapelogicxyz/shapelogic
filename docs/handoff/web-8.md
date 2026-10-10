# Handoff: web stage 8 (packaging)

**Date:** 2026-10-09 · **Next:** the security audit before the first
release (Fable 5.1, max effort; "Brief for the security audit" below).

## Done

Three commits: `19d2128` "Core: the AVX2 kernels build for x86-64",
`722df03` "Engine: a bt-engine to bundle: per-architecture macOS presets,
static runtimes", then the stage's "Web 8: …". No method was added, so the
schema, `docs/API.md` and the generated types did not move (`just web-gen`
left the file as it was).

### The two C++ commits
- **Core: the AVX2 kernels build for x86-64.** The first x86-64 build of
  the project (the Intel Mac's engine, below) failed: with
  `target("avx2")` only on the out-of-class definitions, Apple clang 21
  compiled `filterRowsAvx2` of `SimdExactCover` and `SimdHuangCover`
  without AVX and refused its intrinsics. The targets are now on the
  declarations too. Nothing changes on arm64. The x86-64 unit tests pass
  under Rosetta, but Rosetta offers no AVX2, so the scalar kernel ran
  there: **no machine of this project has run the AVX2 kernels yet**; the
  Windows and Linux steps below run the unit tests on a real x86-64.
- **Engine: a bt-engine to bundle.** `CMakePresets.json`: `release-x64`
  and `release-arm64` (macOS only), the release build for one
  architecture. `engine/CMakeLists.txt`: on MinGW `-static` (no
  libstdc++, libgcc or winpthread DLL beside the engine), on Linux with
  GCC `-static-libstdc++ -static-libgcc`; `cmake/Dependencies.cmake`: on
  MinGW MSYS2's zlib as its static library. The Windows and Linux options
  were not built here (steps below check them).

### Packaging (`web/tools/dist.ts`, `just web-dist`)
- electron-builder 26.15.3 (pinned), run from a script rather than a
  config file so that it can check before packing: the platform it runs on
  only (the engine is built natively); the architecture this machine's, or
  `--arch x64|arm64`; `--dir` the unpacked app only. It refuses to pack
  when `package.json`'s version is not CMakeLists.txt's, when `out/` is not
  built, or when the engine is missing, of another architecture
  (`binaryArch`: the Mach-O, ELF or PE header) or says another version.
- What is packed: `out/**` and `package.json` in `app.asar` (source maps
  left out), nothing from `node_modules` (three.js moved to the dev
  dependencies: it is in the renderer's bundle; the app has no runtime
  package); `bt-engine` (`bt-engine.exe`) from `build/release/engine/` (on
  macOS the other architecture's from `build/release-<arch>/`) as an extra
  resource, beside `app.asar`; the version CMakeLists.txt's
  (`tools/project-version.ts`, given as `extraMetadata`); the file types
  `.xmpuzzle` and `.btsolve` (role Editor, the original's document icon);
  the icons the original's (`web/packaging/`, its README says which).
- Electron's fuses: `runAsNode` off, `enableNodeOptionsEnvironmentVariable`
  off, `enableNodeCliInspectArguments` off,
  `enableEmbeddedAsarIntegrityValidation` on, `onlyLoadAppFromAsar` on,
  `enableCookieEncryption` on. `grantFileProtocolExtraPrivileges` stayed on:
  the page was loaded from `file://` (the audit below turned it off: the
  page is on the app's own scheme).
- Targets: macOS a .dmg and a .zip, signed ad hoc (`identity: '-'`, no
  hardened runtime) unless `CSC_NAME` or `CSC_LINK` names a certificate
  (then electron-builder's signing with the hardened runtime and its
  default entitlements, and notarisation when the `APPLE_API_*` variables
  are set); Windows an NSIS installer (per user, the folder can be
  chosen); Linux an AppImage and a .deb. `publish: null`: no auto-update.
- `just web-dist` (depends on `just build release`), `just web-smoke`.

### One version
CMakeLists.txt's `project(... VERSION 0.1.0)` is the one version: the
engine prints it (`bt::kVersion`), electron.vite.config.ts puts it into
the main bundle at build (`__BT_VERSION__`, `src/main/env.d.ts`), the main
process reports it (`btApp.version`, About) and requires it of the engine
(`NodeTransport.start(engine, { expectVersion })`: an engine of another
version is refused, so the host gives up after its retries), the packed
`package.json` gets it. `package.json`'s own version is kept equal by a
unit test (development reads it nowhere now).
Found on the way: launched as the e2e tests launch it (`electron
out/main/index.js`), `app.getVersion()` was Electron's own (44.7.0: the
About box of the e2e runs said so) and `app.getAppPath()` was `out/main`
(the repository's builds were looked for in the wrong place; harmless,
the tests set `BT_ENGINE`). Both are gone: the version is the constant,
the web directory is the bundle's `../..`.

### The security checklist (`web/src/main/security.ts`, `path-grants.ts`)
- **The window** (`windowOptions`, `webPreferences`): `sandbox`,
  `contextIsolation`, `nodeIntegration` (and in workers and subframes)
  off, `webSecurity` on, `allowRunningInsecureContent`,
  `experimentalFeatures`, `webviewTag`, `navigateOnDragDrop` off,
  `spellcheck` off, `safeDialogs`. `index.ts` makes its one window with
  them; navigation is refused, new windows denied, and for every web
  contents (`web-contents-created`) webviews refused and new windows
  denied.
- **No remote content**: `session.defaultSession.webRequest.onBeforeRequest`
  cancels what `allowedRequest` refuses: it lets through `file:` URLs
  **inside the renderer's directory only** (a page loaded from `file://`
  could otherwise read any local file), `devtools:`, `data:`, `blob:`, and
  in development the dev server's origin (its host, port and scheme, and
  `ws:` for its hot reload); nothing remote. The renderer names no remote
  address, fetches nothing, opens no socket (a test scans it).
- **Permissions**: none (`setPermissionRequestHandler` and
  `setPermissionCheckHandler` answer `permissionAllowed`: false).
- **The CSP** (tightened by the audit, finding 1) of `src/renderer/index.html`
  (`default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:`)
  is the one served: the built `out/renderer/index.html` has it, once, and
  loads only `./assets/…` (unit test); the packaged page has it (smoke
  test).
- **`shell.openExternal`** is called in one place, on `HELP_URLS[link]`
  (now in security.ts): the renderer names a link (`guide`, `tutorial`),
  never an address; both are https on burrtools.sourceforge.net.
- **The environment's switches** (`switches`): a packaged app takes
  `BT_USER_DATA` only (its data directory; the smoke test keeps the user's
  apart). `BT_ENGINE` (another program to run), `ELECTRON_RENDERER_URL`
  (another page, with `window.btApp`, in the window), `BT_CALL_LOG` (a file
  written) and `BT_AUTOSAVE_SECONDS` are development switches.
- **File paths only from dialogs and drops** (`PathGrants`): the engine
  opens and writes whatever path a call names, so the main process checks
  the five methods that name one (`document.open`, `document.openBytes`,
  `document.save`, `solve.importPaused`, `solve.exportPaused`; a unit test
  checks this is every schema method with a `path`) against the paths it
  handed that window: what its open and save dialogs gave for a puzzle or
  a paused solve; a file dropped on the window (the preload reports the
  path its `pathOf` read, which only a real dropped file has; a page cannot
  make a `File` with a path); every `openPath` the main process sends (Open
  Recent, the command line, macOS's `open-file`, a second start's files);
  the rescue record's file when it is read. Any other is refused by
  `serveEngine`'s new `admit` as `invalid_argument` ("x was not chosen in a
  file dialog or dropped on the window") before the engine sees it. So
  that grants cannot be laundered: `bt:recent add` keeps only a path the
  window was handed (Open Recent hands it back), and a rescue record keeps
  its path only when the window was handed it (else the copy is kept
  without a file).
- **The exports' write grants** (stage 7's `ExportGrants`): unchanged; an
  export's path is not granted to the engine.
- `tests/unit/security.test.ts` (21 tests) checks all of this, the IPC
  handlers with Electron stood in for (`vi.mock('electron')`): only the
  app's windows answered (a foreign sender gets nothing and is granted
  nothing on every channel), only what a dialog, the rescue record or a
  drop gave granted, an export's path written once and only there, the
  help opened by name only, recent files and rescue paths only when
  granted.

### Found and fixed: a file given at the start was lost (stage 6)
A file on the command line (Windows and Linux open files so; on macOS
Finder's `open-file` before the window is ready goes the same way) was
sent as `openPath` at `did-finish-load`, while the page still waited for
its stored settings (`main.ts`: `loadState().then(startApp)`), so nothing
listened; and once it listened, the engine was still starting and
`Files.ready()` refused ("The engine is not running"). The packaged smoke
test found it. Now the preload keeps the commands that come before the
page listens and hands them to the first handler a task later, in order;
the menus hold a command that replaces the document (New, Open, a file,
Import Paused) until the first document is shown. New e2e test: a file
given on the command line opens at the start (fails without the menus'
wait); `tests/unit/preload.test.ts` (fails without the preload's queue,
which the development build's timing does not show).

### The smoke test (`tests/e2e/packaged.smoke.ts`, `just web-smoke`)
The packaged app's fuses turn `--inspect` off, so Playwright's Electron
driver (which drives the main process through the inspector) cannot reach
it. The test starts the unpacked app (or `BT_PACKAGED_APP`) as a user
would, with `--remote-debugging-port=0` and Bermuda on its command line,
and drives the page with `chromium.connectOverCDP`. It checks: the fuses
(`@electron/fuses`), `app.asar`'s files (the bundles and package.json, no
source map: `@electron/asar`), `bt-engine --version` in the resources is
the project's, the page is the asar's `index.html` (not
`ELECTRON_RENDERER_URL`'s), the engine running is the resources' (not
`BT_ENGINE`'s: the app's child processes), with `BT_CALL_LOG`,
`ELECTRON_RUN_AS_NODE=1` and `NODE_OPTIONS=--require …` set and ignored;
`require` and `process` absent from the page, `window.bt`'s two methods,
`btApp.version`, the CSP; `fetch` of `/etc/hosts` and of a remote address
refused; a `document.save` to a path no dialog gave refused and nothing
written; the command line's fixture open; a solve with take-apart giving
`bt --json`'s counts and the player ready.

### Tests added
- Unit: `security.test.ts` 21, `preload.test.ts` 1, `dist.test.ts` 10 (one
  version in CMakeLists.txt, package.json, the release engine and the
  built main bundle; the configuration: files, extra resource, fuses, file
  types, no runtime dependency; the architecture from Mach-O, ELF and PE
  headers; where the app finds its engine, packaged and not).
- e2e (`files.e2e.ts`, 2): a path the window was not handed is refused
  (open, save, import) and the same file from the open dialog opens and
  saves; a file given on the command line opens at the start. The e2e
  helper `command()` sends `openPath` through the main process as macOS's
  `open-file` does (so it is granted as a real one is); the dominoes test
  of `solver.e2e.ts` gets its file from the save dialog before saving
  through the bridge.
- The smoke test, above.
- Checked by mutation (each guard broken in turn, the file copied aside
  and back): 13 unit mutants (sandbox off; the packaged app taking
  `BT_ENGINE`; any file allowed; remote allowed; a permission granted;
  `admit` ignored; grants not per window; the open dialog not granting; a
  foreign window's drop granted; an export's path granted to the engine;
  the help taking any address; a packaged app finding `BT_ENGINE`;
  `runAsNode` on), 2 more (recent files and rescue paths ungranted), 3 of
  the preload (no early queue; flushed inside `onCommand`; a drop not
  reported), 4 e2e (no `admit` wired; `openPath` not granted; the menus
  not waiting; the preload's queue, which **survived** the e2e: the
  development build's page listens before `did-finish-load` almost always,
  hence the preload unit test): every one killed but that one, which the
  unit test kills.

## Gate results
| Check | Result |
|---|---|
| C++ (the two commits) | `just test-all` 6/6 (dev, arm64; 334 s); `build/release-x64/tests/bt_tests "~[slow]"` 437/437 under Rosetta (scalar kernel); no schema change (`just web-gen`: no diff); the oracle tiers not run: no arm64 code changed |
| `just web-check` | clean, three times (three TypeScript projects, ESLint without warnings, Prettier), 12 s each |
| `just web-test` (release engine) | **289/289**, three times, 21, 20, 21 s: 31 files (stage 7's 257; `unit/security.test.ts` 21, `unit/dist.test.ts` 10, `unit/preload.test.ts` 1) |
| `just web-e2e` | **60/60**, three times, 5.2, 5.2, 5.3 min (build included): stage 7's 58 and `files.e2e.ts`'s two of stage 8 |
| `just web-dist --dir` + `just web-smoke` | packed in 10–12 s, **1/1** three times (2.9, 2.8, 2.9 s) |
| perf (each e2e run) | mesh build 5.9, 5.4, 4.9 ms; orbit median 13.3, 13.4, 13.3 ms at a 13.3–13.4 ms display frame, p95 14.9, 14.7, 14.9 ms; 152, 152, 151 renders in 153, 151, 151 frames |
| The packaged app on this Mac | `just web-dist` (arm64 .dmg and .zip) and `just web-dist --arch x64` (after `just build release-x64`): the smoke test passes on the unpacked arm64 app, on the app inside the arm64 .dmg (mounted read-only, `codesign --verify --deep --strict` clean) and on the Intel app under Rosetta (32 s: Rosetta's first translation); `bt-engine --version` in each bundle says `bt-engine 0.1.0`; `just web-dev` still loads (the request filter lets the dev server through) |
| before the gate | one full round (check, test, e2e, dist, smoke), clean; then the three rounds whole, each in that order, clean |
| Windows, Linux | **not run** (this Mac cannot build their engines): the steps below |

## The reference screenshots
None changed: no stage 8 test takes one, and the 38 of the earlier
stages pass unchanged (the packaging touches no drawing).

## Signing and notarisation (documented, not automated)
**macOS** (an Apple Developer Program membership):
1. In Xcode or developer.apple.com make a "Developer ID Application"
   certificate and install it in the login keychain
   (`security find-identity -v -p codesigning` lists it).
2. Make an App Store Connect API key (Users and Access ▸ Integrations ▸
   Keys, role Developer); keep the `.p8` file, its key id and the issuer
   id.
3. `export CSC_NAME="Developer ID Application: <name> (<team id>)"
   APPLE_API_KEY=/path/AuthKey_<id>.p8 APPLE_API_KEY_ID=<id>
   APPLE_API_ISSUER=<issuer>`, then `just web-dist` and, for Intel,
   `just build release-x64 && just web-dist --arch x64`. electron-builder
   signs every Mach-O file (bt-engine in Resources included) with the
   hardened runtime and its default entitlements
   (`node_modules/app-builder-lib/templates/entitlements.mac.plist`:
   allow-jit, allow-unsigned-executable-memory,
   disable-library-validation), notarises with notarytool and staples.
4. Check: `codesign --verify --deep --strict --verbose=2 "dist/mac-arm64/BurrTools Alpha.app"`,
   `spctl -a -vv -t exec` on it (says "Notarized Developer ID"),
   `xcrun stapler validate` on the .dmg; then the smoke test with
   `BT_PACKAGED_APP` on the mounted .dmg's app.
Unsigned (as built today) the app is signed ad hoc: it runs on the Mac that
built it; a downloaded copy is refused by Gatekeeper until the user allows
it in System Settings ▸ Privacy & Security.

**Windows**: an OV or EV code-signing certificate. With a .pfx:
`$env:CSC_LINK="C:\path\cert.pfx"; $env:CSC_KEY_PASSWORD="…"` before
`npm run dist`; electron-builder signs the app's executables and the
installer with signtool (an EV certificate on a token, or Azure Trusted
Signing, needs `win.signtoolOptions` or `win.azureSignOptions` in
`tools/dist.ts`'s configuration). Check with
`signtool verify /pa /v "dist\BurrTools Alpha Setup 0.1.0.exe"`.
Unsigned, SmartScreen warns ("Windows protected your PC" ▸ More info ▸ Run
anyway).

**Linux**: nothing is signed; a repository's own signing is the
distribution's affair.

## Where this stage departs from the plan, and why
1. **A packaged app ignores `BT_ENGINE`** (the plan: "the host finds it
   there, or `BT_ENGINE`"). An environment variable that makes a signed
   app run another program is what the `runAsNode` fuse exists to close
   (on macOS the program would run as the app, with what the user granted
   it); development keeps it. Likewise `BT_CALL_LOG`,
   `BT_AUTOSAVE_SECONDS` and `ELECTRON_RENDERER_URL`.
2. **The paths the engine may be given are checked in the main process**,
   beyond the plan's "file paths only from dialogs and drops": the recent
   files, the rescue record and the files the system opens with the app
   count, as the brief said, and are granted as they are handed over.
3. **The smoke test drives the page over DevTools**, not Playwright's
   Electron driver: the fuses turn the inspector off, and a test of a
   build with them on would not be the shipped app.
4. **Electron's fuses** and the request filter are not in the plan's
   checklist; they are Electron's recommendations for a packaged app.
5. **The version is a constant put in at build**, not `app.getVersion()`,
   which is not the app's in the e2e launch (above).
6. **The Windows and Linux builds were not made**: their engines must be
   built on them; the steps are written down below.
7. Packaging needed a core fix and build options (the two C++ commits).

## What the mockup did differently, and why it changed
Nothing: the mockup is a page; it had no packaging, window or process to
compare.

## Windows and Linux: what to do by hand (not run in this session)
web-6.md's Windows run was not given to this session either: these steps
take it in (its step 5's checks are in step 6 here).

### Windows (10 or 11, x64)
1. As web-6.md's steps 1 and 2: MSYS2 with, in its **UCRT64** shell,
   `pacman -S --needed git python mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-zlib`;
   Node 22 LTS for Windows; the repository brought over as a bundle
   (`git -C ~/git/burr-tools-rotations/burrtools_alpha bundle create ~/bt-alpha.bundle --all`
   on the Mac, `git clone bt-alpha.bundle burrtools_alpha` in UCRT64).
2. In UCRT64, in `burrtools_alpha`: `cmake --preset release`,
   `cmake --build --preset release`, then
   `ctest --preset release -LE slow` (the unit tests run the AVX2 kernels
   on a processor that has AVX2, the first run of them anywhere; the
   contract and cli tests need `python`). Note any failure and stop.
3. The engine stands alone: `objdump -p build/release/engine/bt-engine.exe | grep "DLL Name"`
   lists only Windows's own DLLs (KERNEL32, msvcrt or api-ms-win-crt-*,
   …), no `libstdc++-6.dll`, `libgcc_s_seh-1.dll`, `libwinpthread-1.dll`
   or `zlib1.dll`. Then, in **PowerShell without MSYS2 on PATH**:
   `.\build\release\engine\bt-engine.exe --version` prints
   `bt-engine 0.1.0`.
4. In PowerShell: `cd web`, `npm ci`, `npm run dist -- --dir` (the
   unpacked app in `dist\win-unpacked`), then
   `npx playwright test -c playwright.smoke.config.ts`: 1 passed (it skips
   the check of which engine runs, done by hand in step 6). Then
   `npm run dist`: `dist\BurrTools Alpha Setup 0.1.0.exe`.
5. Run the installer (SmartScreen: More info ▸ Run anyway); it installs
   for the user; the app starts from the Start menu.
6. In the installed app: the status bar leaves "Starting the engine…"; in
   Task Manager the app's `bt-engine.exe` is the one under
   `%LOCALAPPDATA%\Programs\BurrTools Alpha\resources\`; Help ▸ About says
   0.1.0; the menu bar in the window with Ctrl keys; File ▸ Open…
   `tests\data\puzzles\Bermuda.xmpuzzle`: shapes listed and drawn; every
   style; the Solver: Solve with take-apart, the solution plays; edit
   something, File ▸ Save As… into a folder with a space in its name, then
   Save again over it; Save over a file made read-only (its Properties):
   refused, "… is read-only"; Ctrl+W with a change: the question, Cancel,
   then Don't Save; end `bt-engine.exe` in Task Manager after an edit: the
   app restarts it and offers the rescue copy; reopen it; quit and start
   again: the settings and layout kept; drop a `.xmpuzzle` from Explorer on
   the window; **double-click a `.xmpuzzle` in Explorer** with the app
   closed (it opens in a new window: the file association) and with it
   open (it opens in the running window, the unsaved-changes question
   first, no second window); Help ▸ User Guide opens the browser; File ▸
   Export ▸ Image… writes a PNG. Uninstall from Settings ▸ Apps.
7. Write down what happened (each step, anything wrong, the messages) and
   give it to the next session to record here and fix.

### Linux (x64; Ubuntu 24.04 or a distribution of its age)
1. `sudo apt install git cmake ninja-build g++-14 zlib1g-dev python3`
   (GCC 14 for C++23; CMake 3.28 or later); Node 22 (nodejs.org's
   binaries, or `nvm install 22`). The repository from the bundle
   (`git clone bt-alpha.bundle burrtools_alpha`).
2. `CXX=g++-14 cmake --preset release`, `cmake --build --preset release`,
   `ctest --preset release -LE slow` (the AVX2 kernels, as on Windows).
3. `ldd build/release/engine/bt-engine` lists no `libstdc++.so` and no
   `libgcc_s.so`; `build/release/engine/bt-engine --version` prints
   `bt-engine 0.1.0`.
4. `cd web`, `npm ci`, `npm run dist -- --dir`, then
   `npx playwright test -c playwright.smoke.config.ts` in a desktop session
   (or `xvfb-run -a npx playwright test -c playwright.smoke.config.ts`). If
   the app ends at once with a message about the sandbox (Ubuntu 24.04
   restricts unprivileged user namespaces), note it, then
   `sudo chown root:root dist/linux-unpacked/chrome-sandbox && sudo chmod 4755 dist/linux-unpacked/chrome-sandbox`
   and run the test again. Then `npm run dist`: the AppImage and the
   `.deb` in `dist/`.
5. `sudo apt install ./dist/burrtools-alpha-web_0.1.0_amd64.deb`; start
   BurrTools Alpha from the applications menu; and the AppImage
   (`chmod +x`, run it; note whether it needs `--no-sandbox`).
6. The checks of the Windows step 6, with Files for Explorer and
   `ps -ef | grep bt-engine` for Task Manager (the engine is the one
   under `/opt/BurrTools Alpha/resources/`); `chmod a-w` for read-only;
   double-click a `.xmpuzzle` in Files (the .deb's file association).
   Remove with `sudo apt remove burrtools-alpha-web`.
7. Write down what happened, as for Windows.

## Known issues and notes
- **The page was loaded from `file://`**, so Electron's
  `grantFileProtocolExtraPrivileges` fuse stayed on (module scripts from
  `file://` need it). The request filter kept the page to its own
  directory; a custom protocol (`protocol.handle`, a privileged scheme)
  would let the fuse go off. Done by the audit (its finding 1).
- **The packaged app accepts `--remote-debugging-port`** (a Chromium
  switch; no fuse turns it off): whoever starts the app can drive its
  page, as the smoke test does. Only the user who starts it can.
- **macOS entitlements**: a signed build gets electron-builder's defaults,
  which include `disable-library-validation`; an entitlements file of our
  own with allow-jit only (Electron needs that much) is worth trying when
  signing for real. Not done (no certificate here).
- `BT_USER_DATA` is kept in a packaged app (the smoke test needs it): it
  moves the app's data directory, nothing more.
- The grants are exact strings, kept for the window's life, for reading
  and writing alike (a file opened may be saved over: Save). A path named
  another way (a symbolic link, `..`) is refused, not resolved.
- The rescue offer at the start and a file given on the command line can
  both come: the file opens once the first document is shown, and the
  offer, if a copy was left, is asked as well (Reopen replaces the file).
- `npm audit`: 8 moderate, all `sprintf-js` (a denial of service by a
  format string) under electron-builder's `@electron/get` → `global-agent`
  → `roarr`; build-time only, nothing of it is in the app; the fix
  `npm audit fix --force` proposes is an older electron-builder.
- The AVX2 kernels have run nowhere yet (Rosetta has no AVX2); the
  AVX-512 ones neither.
- The Linux sandbox on Ubuntu 24.04 (AppArmor and user namespaces) may
  need the setuid `chrome-sandbox` for the unpacked app and the AppImage;
  the .deb sets it. Unverified.
- Windows: asar integrity (Electron 30 and later), the fuses, NSIS, the
  file associations, the single-instance hand-over and the static engine
  are unverified; so is everything of web-6.md's Windows section.
- `dist/` holds about 130 MB per installer (ignored by git). The first
  `web-dist` downloads Electron's zip for the platform into
  electron-builder's cache (network needed).
- The `.deb`'s maintainer is "BurrTools Alpha" without an address;
  `package.json` has no `author` (electron-builder warns).
- From stage 7 and earlier, still open: as web-7.md's and web-6.md's
  "Known issues" list them.

## Brief for the security audit (before the first release)
Fable 5.1 at max effort, FRONTEND-PLAN.md's "Models, effort and audits":
the Electron security surface of the packaged app, findings with
concrete failure scenarios, each probed before it is called a finding,
fixed in a commit "Web 8 audit: …" after the stage's. Where to look:

- **The Electron surface.** `src/main/security.ts` (the window's options,
  `switches`, `allowedRequest`, `permissionAllowed`, `HELP_URLS`) and how
  `src/main/index.ts` applies them (`webRequest.onBeforeRequest` on the
  default session, the permission handlers, `will-navigate`,
  `setWindowOpenHandler`, `web-contents-created`, `did-start-navigation`
  re-serving the bridge); the page from `file://` with
  `grantFileProtocolExtraPrivileges` on (what a script injected into the
  page could read or do; is a custom protocol worth it now); the CSP
  (`style-src 'unsafe-inline'`; any way to run script in the page: the
  renderer builds HTML from template strings with names out of puzzle
  files — `shell/dom.ts`'s `setHtml`, the cards' render functions — so an
  `.xmpuzzle` with markup in a shape's or a problem's name is the first
  probe); the fuses (`tools/dist.ts`) and what stays open
  (`--remote-debugging-port`, `BT_USER_DATA`, Chromium's other switches);
  the single-instance hand-over (`second-instance`'s `files`, from
  another process's command line, are granted); macOS's `open-file`; the
  entitlements a signed build would get.
- **The IPC handlers' checks.** `src/main/app-ipc.ts` (every `bt:*`
  channel: `bt:app-info` (sync), `choose-open`, `choose-save`,
  `write-export`, `load-state`, `save-state`, `recent`, `rescue`,
  `dropped`, `help`, `menu-state`, `close`), `src/main/engine-ipc.ts`
  (`bt:call`: `isBridgeCall`, the method list, `admit`; the engine's own
  checks of what it is sent are decision 0012's), `src/engine/app-bridge.ts`
  (`isAppCommand`, `isMenuState`, `isRescueInfo`: what a compromised page
  could send, sizes included: `save-state`'s objects are stored and
  written to `state.json`), `src/preload/index.ts` (what `window.bt` and
  `window.btApp` expose; the early command queue).
- **The file paths.** `src/main/path-grants.ts` and its grants (dialogs,
  drops through the preload, `openPath` sends, the rescue record, recent
  files): can a page get a path granted it was not handed (the second
  start's files, a recent file read from `state.json` written by an
  earlier session, a rescue record left by an earlier session); what the
  engine does with a granted path (`document.save` after `openBytes`,
  `solve.exportPaused`); the exports' `ExportGrants` and
  `writeFileSafely` (symbolic links, read-only files, stage 7); the
  rescue copy and `state.json` in the data directory.
- **The packaging.** `tools/dist.ts` (what goes into `app.asar` and the
  resources, the asar integrity, the ad-hoc signature, the engine beside
  the archive and outside its integrity check: who can replace
  `Resources/bt-engine`, and does the app check anything but its
  version), `web/packaging/`, the NSIS installer's options, the .deb and
  AppImage (the sandbox), `publish: null` (no updater).
- **Tests to start from**: `tests/unit/security.test.ts`,
  `tests/unit/preload.test.ts`, `tests/unit/dist.test.ts`,
  `tests/e2e/packaged.smoke.ts` (`just web-dist --dir && just web-smoke`;
  `BT_PACKAGED_APP` for another copy), `files.e2e.ts`'s two stage 8 tests,
  `exports.e2e.ts`'s refused path.
- The gate after the fixes, three times: `just web-check`, `just
  web-test` (release engine), `just web-e2e`, and `just web-dist --dir`
  with `just web-smoke`; the C++ steps of AGENTS.md if the engine or core
  changes.

## Audit

**Date:** 2026-10-09 · Fable 5.1 at max effort, on the diff
`6ff213a..b5a34a9` (`19d2128` Core: the AVX2 kernels build for x86-64,
`722df03` Engine: a bt-engine to bundle, `b5a34a9` the stage), the plan,
docs/ARCHITECTURE.md, docs/API.md, decisions 0012 and 0013, this note and
the audit and known-issues sections of web-7.md and web-6.md. The fixes
are two commits after the stage's: `Core: a save's temporary file is its
own` and `Web 8 audit: the page on its own scheme, exclusive temporary
files, markup in names tested`.

### How it was done
Read in the brief's order; the Electron surface, the IPC handlers, the
grants and the packaging traced by hand against the stage's tests; then
every suspicion probed before it was called a finding, with scripts kept
out of the repository and temporary tests (removed):
- the packaged app (`dist/mac-arm64`) started as the smoke test starts it
  and driven over DevTools by a temporary smoke test: CSP violation
  events listened for while the page was given a script and an image
  from `file:` URLs, a `data:` script, an inline script and an inline
  handler, `eval`, a stylesheet from `file:`, frames (`file:///etc/hosts`,
  the page itself, `srcdoc` with a script), `fetch` inside and outside the
  renderer's directory, `window.open`, `location.href`, a `<meta
  refresh>`, a `target=_blank` link; the device APIs the page has;
- a crafted `.xmpuzzle` whose three shapes' names, problem name and notes
  are markup (an element with a class, a quote that closes an attribute
  and adds one, a `<style>` that would outline the page, a
  `</textarea>`), checked to open in the engine with the names intact,
  then opened in the development build by a temporary e2e test that
  visited every card, menu, dialog and explorer counting the elements
  the markup would make;
- the export writer and the release engine over stdin, each with a
  symbolic link planted at `<file>.tmp` before the write;
- Electron 44's documentation (`electron.d.ts`) for the device choosers,
  `protocol.handle`, the scheme privileges and the fuses; `npm view` for
  the current Electron.
Each test added was then run against the stage's code by mutation (the
fixed file copied aside, the stage's version or a one-line mutant written
in its place, copied back): each fails without its fix.

### Findings, by severity
1. **The request filter was the only thing between a puzzle file and a
   script from disk** (medium: defence in depth; no way to run script in
   the page was found). The page was loaded from `file://`, where the
   CSP's `'self'` matches every `file:` URL: an `<img
   src="file:///etc/hosts">` and a `<script src="file:///…/probe.js">`
   raised no CSP violation, and only the request filter's
   `ERR_BLOCKED_BY_CLIENT` stopped them; the `grantFileProtocolExtraPrivileges`
   fuse let the page `fetch` other files too. Inline scripts, `data:`
   scripts, handlers and `eval` were CSP-blocked, markup in names is
   escaped everywhere (below), so a second bug was needed: one in
   `allowedRequest`, or an Electron change to what `webRequest` sees of
   `file:` loads (it has changed before), would have let a puzzle file
   whose name is markup load a script from a path it knows, with all of
   `window.bt` and `window.btApp` in reach (every file the window was
   handed saved over, the rescue copy, the dialogs). Fix: the page is
   served on a scheme of the app's own, `bt-app://app/index.html`
   (`security.ts`: `APP_SCHEME`, `appUrl`, `appFile`, the file of the
   renderer's directory an app URL names or null; `index.ts`:
   `protocol.registerSchemesAsPrivileged` before the app is ready,
   standard and secure, `protocol.handle` serving the renderer's directory
   through `net.fetch` of its files and a 404 for anything else); the
   fuse is off (`tools/dist.ts`); the CSP is `default-src 'none'` with
   each source named: scripts, styles and images `'self'`, inline styles
   (the mockup's) and `data:` images (the cursors), and no fetch, worker,
   frame, plugin, media, `<base>` or form at all. Probed after: the page
   is `bt-app://app/index.html`; a `file:` script or image is refused by
   Chromium itself ("Not allowed to load local resource"), before the CSP
   and the filter; the filter is the second check, not the only one.
2. **A save or an export wrote through a link planted at `<file>.tmp`**
   (low: it needs someone else who may write in the folder the file is
   saved to, a shared one). The core's `writeGzipFile` opened the
   temporary file with `gzopen` and `savePausedSolve` with an `ofstream`,
   which follow a symbolic link, and `writeFileSafely` with Node's default
   flags: a link left at `<file>.tmp` carried the puzzle's or the export's
   bytes into any file the user may write, which then held them, and the
   rename put the link in the puzzle's place. Probed with the release
   engine (Bermuda saved to `out.xmpuzzle` beside `out.xmpuzzle.tmp →
   victim.txt`: the victim held the gzip data, `out.xmpuzzle` became the
   link) and with the export writer the same way. Fix: the temporary file
   is the write's own: a file or link already at its name is removed (a
   directory stays, and fails the save as before), the new one is made
   exclusively (`O_EXCL`, `_O_EXCL` on Windows, Node's `wx`), so a link
   put there meanwhile is refused rather than followed; `writeFileWhole`
   is shared by the puzzle and paused-solve writers; the saved bytes are
   unchanged.

### Tests added (each fails without its fix)
- `tests/unit/io/test_puzzle_file.cpp`: a save never writes through a
  planted temporary file, puzzle and paused solve, and replaces a stale
  one (5 checks fail with the stage's writers).
- `web/tests/unit/app.test.ts`: the export writer the same (fails with
  the stage's `export-file.ts`).
- `web/tests/unit/security.test.ts` (26 now): the app's scheme (`appUrl`;
  `appFile` serving only the renderer's directory: other schemes and
  hosts, a path that leaves it, encoded or not, a NUL, the directory
  itself; a URL's own dot-segment resolution), `allowedRequest` for the
  scheme, the page loaded by `loadURL(appUrl(…))` and never `loadFile`,
  the scheme registered before ready and handled, the CSP as written and
  as built (fail with `appFile`'s containment check removed, and with the
  stage's `index.ts` and `index.html`); `dist.test.ts`: the fuse.
- `web/tests/e2e/packaged.smoke.ts`: the page's URL on the scheme, the
  fuse off, a `file:` script and image refused by Chromium itself (the
  script's `error`, nothing run, no CSP violation, the console's "Not
  allowed to load local resource" for both), the new CSP (the stage's
  page, probed, reported neither message and no violation either: the
  filter alone refused them).
- `web/tests/e2e/security.e2e.ts`: a puzzle file whose names hold markup
  shows them as text everywhere (fails with `esc()` dropped from the
  shapes list).

### Checked and found sound
- **Markup in names** (the brief's first probe): every place a shape's
  or a problem's name or the notes are shown escapes them (`esc` of
  `shell/dom.ts`, 97 uses): the shapes list and its rename field, the
  inspector, the status window, the notes dialog, the STL and GLB
  dialogs, the Puzzle's tiles, table, scene labels and menus, the
  Solver's problem, list, Manage menu and convert dialog, the explorers,
  the questions about unsaved changes and the rescue offer (`askChoice`
  escapes), the status bar (`textContent`), the window title. Nothing
  was injected, no style sheet applied; now the e2e test above.
- **What a page could do if it ran script**: inline scripts, `data:`
  scripts, inline handlers and `eval` CSP-blocked; `location.href`,
  `<meta refresh>` and `window.open` refused (`will-navigate`, the open
  handler); a `target=_blank` link opens nothing; a frame of
  `file:///etc/hosts` empty, the page in a frame of itself without
  `window.bt` (no preload in subframes), a `srcdoc` script blocked; the
  main bundle and `package.json` unreadable from the page.
- **Permissions and devices**: no permission granted; Electron 44 cancels
  a Bluetooth request when no `select-bluetooth-device` listener is
  added, and USB, HID and serial requests need a chooser the app has not;
  the permission check handler answers false for them too.
- **The IPC handlers** (`app-ipc.ts`): every channel answers only the
  app's windows; `bt:choose-open` and `choose-save` grant what the
  dialog gave (an export's path to the export grants, not the engine);
  `bt:write-export` writes a granted path once; `bt:recent add` and the
  rescue record keep only granted paths; `bt:dropped` is reachable only
  through the preload's `pathOf` with a `File` that has a path (one the
  user dropped, pasted or chose in a file chooser; a page cannot make
  one); `bt:help` by name; `bt:menu-state` checked; `bt:call` admits
  only schema methods and refuses the five path-taking ones for a path
  the window was not handed (exact strings; links and `..` refused, not
  resolved; a non-string `path` left to the engine's strict parameter
  check); the `openPath` grants come from the menus, the command line,
  `open-file` and a second start, never from the page.
- **The files**: the rescue copy and `state.json` are in the user's data
  directory (`~/Library/Application Support`, mode 700 on macOS); the
  record's hash binds it to the copy, and its path is granted on read
  only when they match; a recent file in `state.json` was granted when
  it was added. The core's save follows links and refuses read-only
  files (stage 6's audit) and now makes its temporary file itself.
- **The packaging**: `app.asar` holds the bundles and `package.json`
  only, no source map; the fuses as listed and now the `file:` one off;
  `bt-engine` checked for its architecture and version before packing;
  `publish: null`; Electron 44.7.0 is npm's `latest` (Chromium 152);
  `npm audit`'s eight moderate findings are build-time only (the stage's
  note).
- **The switches**: a packaged app takes `BT_USER_DATA` only (the smoke
  test sets every other one); `--remote-debugging-port` stays (below).

### Known issues left
- **Two e2e one-offs in the gate's runs, each once in about 180 tests,
  both in what the inspector does to the main process.** (1) The
  engine-kill test of `ipc.e2e.ts` ended in "the app did not quit within
  20 s of its windows going" after its own checks had passed (six repeats
  and every later round clean). The shape is the stage 6 audit's finding
  8: Playwright's `app.evaluate` destroys the windows through the
  inspector and can land between two statements of the main process,
  whose uncaught exception then holds it in Electron's dialog. Which
  statement is not known: a deterministic probe (a 600 ms
  `webContents.send` with the window destroyed meanwhile) showed that
  `send` does not throw, and nothing of the main process's output reached
  the log. So `tests/e2e/app.ts` now installs an `uncaughtException`
  handler in the main process at launch (Electron shows no dialog when
  one is installed) and `closeApp` fails the test with the stack when one
  was recorded: the next occurrence will say where. (2) The stage 6
  audit's own test, "the menus outlive a window destroyed while they are
  built", once saw none of its two 600 ms menu builds in 5 s of polling
  through the inspector (the state is sent 30 ms after the change and
  again when the Undo label arrives); it passed in every other round. Both
  involve the inspector's interrupt landing inside running JavaScript;
  nothing of a user's run does. Left for the next session: run the two
  tests a hundred times each with the main process's stderr echoed.
- **`--remote-debugging-port`** (and Chromium's other switches) are
  taken by the packaged app: no fuse turns them off; whoever starts the
  app can drive its page, as the smoke test does. Only the user who
  starts it can, and that user runs anything as themselves anyway.
- **The engine beside `app.asar`** is outside the archive's integrity
  check, as the Electron framework is: code signing is what protects it,
  and the app is signed ad hoc until a certificate is used. A swapped
  `Resources/bt-engine` runs with everything the user granted the app;
  the app checks only its version. A hash of the engine put into the
  bundle at build would not survive signing (the engine is signed too).
- **A second start's files** (`second-instance`'s data) are trusted: any
  process of the same user can hand the running app paths to open, as it
  could drive the app in other ways.
- `style-src 'unsafe-inline'` stays for the mockup's inline styles; a
  name that is markup is shown as text (tested), so no style sheet
  reaches the page from a file.
- `bt:save-state` and `bt:rescue write` take any size from the page (its
  own settings and document); a page running foreign script would be the
  problem, not these.
- The writers' temporary file keeps its fixed name: a stale one is
  replaced (by design), and a directory planted there fails the save, as
  before. Two saves of one file at once still share it (nothing in the
  app does that).
- The `.deb`'s maintainer and `package.json`'s `author`, the entitlements
  of a signed build, notarisation, and the Windows and Linux builds: the
  stage's list stands.

### Gate after the fixes
| Check | Result |
|---|---|
| C++ (the core fix) | `just test-all` 6/6 (dev, 330 s), `just test-asan` 6/6 (1140 s); the schema did not move (no `gen_api_md.py`, `just web-gen` would leave the types as they are); `just oracle-roundtrip` 365/365 byte-identical to the original's save; `just oracle-files --tier fast` 19/19; the `[io]` cases 53/53 dev; clang-tidy and clang-format are not installed |
| `just web-check` | clean, three times (three TypeScript projects, ESLint without warnings, Prettier), 12 s each |
| `just web-test` (release engine) | **292/292**, three times, 20, 21, 20 s: the stage's 289 and the audit's three (the export writer's planted link, the app's scheme and its files) |
| `just web-e2e` | **61/61**, three times, 5.2 min each (build included): the stage's 60 and `security.e2e.ts`; two earlier attempts at the three rounds each lost one round to a one-off (above, "Known issues left"), and were run again whole |
| `just web-dist --dir` + `just web-smoke` | packed in 11–13 s, **1/1** three times (3.3–3.5 s), with the new checks of the page's scheme, the fuse and the `file:` probe |
| perf (each e2e run) | mesh build 5.7, 5.1, 4.8 ms; orbit median 13.3 ms at a 13.3–13.4 ms display frame, p95 14.9, 14.9, 14.7 ms; 151, 150, 151 renders in 151, 150, 153 frames |
| The reference screenshots | none changed (the page's scheme and CSP draw nothing differently; the 38 pass unchanged) |

## Brief for the first release (after the audit)
Opus 5.5 at medium (FRONTEND-PLAN.md's packaging row), in accept-edits
mode: the user's machines and certificates are needed. Version 0.1.0 as
CMakeLists.txt has it, from the audit's commit.
- **The other platforms by hand first**: the Windows and Linux steps of
  this note (above), on the audit's commit (its changes to the page's
  scheme and the writers are platform code: `_wopen` with `_O_EXCL`, the
  `bt-app:` page, the smoke test's `file:` probe on `C:\Windows\win.ini`),
  each step's outcome written down and anything wrong fixed in its own
  commit with a test; the AVX2 kernels' first run is among them.
- **Signing**: on macOS with a Developer ID certificate as "Signing and
  notarisation" says, with an entitlements file of the project's own
  (allow-jit only) tried first, falling back to electron-builder's
  defaults; `codesign --verify --deep --strict`, `spctl`, `stapler`, then
  the smoke test on the notarised app; on Windows with the certificate
  at hand, else unsigned with the SmartScreen note in the release notes.
- **The artefacts**: `just web-dist` (arm64), `just build release-x64 &&
  just web-dist --arch x64`, the Windows installer, the AppImage and
  `.deb`; their SHA-256 sums; a tag `v0.1.0` on the commit they were
  built from; release notes naming what this version does and does not
  solve (sliding and stacking puzzles, the interim STL, the imports), the
  Gatekeeper and SmartScreen notes for unsigned copies, the audit's known
  issues that concern users (`--remote-debugging-port`, an unsigned
  engine).
- Record it in `docs/handoff/release-0.1.0.md`: what was built where,
  the checks, what was fixed on Windows and Linux, the known issues
  carried, and the brief for what comes next (FRONTEND-PLAN.md's
  "Later").

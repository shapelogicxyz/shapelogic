# Handoff: the first release (0.1.0), macOS so far

**Date:** 2026-10-15 · **State:** macOS signed, notarisation pending at
Apple; Windows, Linux, the artefacts, the release notes and the tag not
started. **Not tagged.** Version 0.1.0 as CMakeLists.txt has it.

The release is being done one operating system at a time, as the owner
asked: macOS first, then Windows, then Linux. The Python wheel and the help
pages (shapelogic.xyz is a static page for now) are separate later steps,
decided by the owner. Nothing here needed the original BurrTools checkout
(`../burrtools_rotations` is not on this Mac, so no oracle tier ran).

## Done

### Commits since the rename (`ef67313`)
- `7778fe2` **CLI: bt is now shapelogic_cli.** The executable
  (`build/<preset>/cli/shapelogic_cli`), its usage text, message prefixes
  and `--version` line; ctest `cli`, the oracle and bench defaults, the web
  e2e helper and the READMEs follow. Old handoff notes still say `bt`.
- `f697580` **Web: Command Line Tool dialog, shapelogic_cli packaged and
  linkable.** ShapeLogic ▸ Install Command Line Tool… (Help ▸ Command Line
  Tool… on Windows and Linux): says what the tool is, its main options,
  where it is and what to type; Install and Cancel below (Remove and Close
  once linked). Install makes `/usr/local/bin/shapelogic_cli` a symbolic
  link to the copy in `Resources` (macOS only for now; osascript's
  administrator prompt where the folder is not the user's). Main side
  `web/src/main/cli-tool.ts`, one IPC channel `bt:cli-tool` (the page names
  an operation, never a path), renderer `shell/cli-tool.ts`. `dist.ts`
  packs the tool beside `bt-engine` and checks its architecture and version;
  the smoke test runs it. Also brought three tests up to date with the
  rename commit (the window icon in `app.asar`, `new BrowserWindow({...`).
- `d9591b6` **Web: a signed macOS build gets the project's own
  entitlements (allow-jit only).** `web/packaging/entitlements.mac.plist`;
  the signed, hardened app passes the smoke test with it, so
  electron-builder's broader defaults are not used.

### Checks
| Check | Result |
|---|---|
| `just build release` + `just test-all` on the rename commit | 7/7, 10 min 19 s |
| `ctest -R cli` after the rename | passed |
| `just web-check`, `npx vitest run` | clean; 336/336 (34 files) |
| `files.e2e.ts` (includes the new dialog) | 16/16; the whole e2e suite was **not** re-run |
| `just web-dist --dir` signed with Developer ID + `just web-smoke` | 1/1; `codesign --verify --deep --strict` clean; `shapelogic_cli` and `bt-engine` signed with the hardened runtime |
| Mutation: window check removed from `bt:cli-tool` | the foreign-window test fails, restored |
| Real Install clicked in the packaged app | **not done** (writes `/usr/local/bin`, asks the administrator's password; the owner's go-ahead is needed) |
| The three-rounds gate (check, test, e2e, dist, smoke) | **not done** |

### Signing (macOS)
- Certificate: Developer ID Application: Colin Deisenroth (L6GD6WDF8J),
  valid 2026-10-14 to 2027-11-13, SHA-1 `96F4FAD9E53724785EC4D7370A2622BCF8CC0065`.
  The keychain listed it twice (System and login keychains) and as invalid
  until Apple's *Developer ID G2* intermediate (apple.com/certificateauthority,
  SHA-256 `F16CD3C5…D2DF3A`) was added to the login keychain. Select the
  identity by its hash: `CSC_NAME=<hash>`.
- The certificate files and the `.p12` live outside the repository in
  `/Users/colin/git/shapelogic/certificates/` and must stay out of it.

### Notarisation: pending
- Credentials: Apple ID **jdcolin@gmail.com** (the account holder),
  team `L6GD6WDF8J`, an app-specific password in
  `certificates/passwords/apple_app_specific_password`
  (`shapelogic.xyz@gmail.com` is not the Apple ID that password belongs
  to: Apple answered 401). electron-builder takes `APPLE_ID`,
  `APPLE_TEAM_ID`, `APPLE_APP_SPECIFIC_PASSWORD`; the environment
  variables this note's older sections name (`APPLE_API_*`) are not used.
- Submission **`12d51f10-6422-4405-848c-9d658036944d`** (ShapeLogic.zip,
  arm64, submitted 2026-10-15 05:41 UTC) was still **In Progress** at
  16:44 UTC. `just web-dist` with the credentials signed the app and then
  failed when its poll of the notary service timed out on the network; no
  `.dmg` or `.zip` was produced.
- **The password was exposed in a session transcript** (electron-builder
  passes it to `notarytool` as an argument and a process listing printed
  it). Revoke it at account.apple.com ▸ Sign-In and Security ▸
  App-Specific Passwords after the release and make a new one; never list
  processes' command lines while a notarisation runs.

## Brief for the next session
Sonnet 5.5 or Opus 5.5 at medium effort, accept-edits mode.

1. **Notarisation.** Read the submission's status (`xcrun notarytool info
   12d51f10-… --apple-id jdcolin@gmail.com --team-id L6GD6WDF8J --password
   "$(tr -d '\n\r' < certificates/passwords/apple_app_specific_password)"`,
   from the repository's parent folder). If **Accepted**: staple the app
   (`xcrun stapler staple web/dist/mac-arm64/ShapeLogic.app`), then the
   `.dmg` and `.zip` must be made from that *same* notarised app: re-running
   `just web-dist` signs again and starts a new submission. Prefer a
   `--prepackaged` option for `tools/dist.ts` (electron-builder's
   `prepackaged`), in its own commit with a test; the fallback is the
   rebuild and another wait. If **Invalid**: `notarytool log <id>`, fix what
   it names. If it is still in progress after a day, ask the owner whether to
   cancel and resubmit; do not upload a second copy before then.
2. Check: `codesign --verify --deep --strict --verbose=2`, `spctl -a -vv -t
   exec` ("Notarized Developer ID"), `xcrun stapler validate` on the `.app`
   and the `.dmg`, then the smoke test on the app inside the mounted `.dmg`
   (`BT_PACKAGED_APP`), and `shapelogic_cli --version` from it.
3. **Real Install**, with the owner present: the dialog's Install button,
   the administrator's password, `shapelogic_cli --version` in Terminal,
   then Remove. Record it here.
4. The Intel build: `just build release-x64 && just web-dist --arch x64`
   with the same credentials (another notarisation wait).
5. The three-rounds gate of AGENTS.md for the web changes (check, test, e2e,
   dist, smoke).
6. SHA-256 sums of the `.dmg` and `.zip` of each architecture.
7. Then, in the owner's order: **Windows**, **Linux** (web-8.md's by-hand
   steps, which assume the repository names of before the rename:
   `burrtools_alpha` is now `shapelogic`, `burr-tools-rotations` the
   original; `Install` of the command line tool is macOS only and the
   Windows installer's PATH entry and the .deb's `/usr/bin` link are open
   work), then the **Python wheel** decision, the **help pages**
   (`https://shapelogic.xyz/gui-doc/…`: the pages are not hosted yet), the
   release notes, **asking the owner before tagging `v0.1.0`**.

## Release notes: draft points (not yet written)
Name what the deferred parts added: sliding and stacking/Panex puzzles, the
STL meshes and exports (both dialogs, the STL-like style), the imports
(PuzzleSolver3D, Puzzlecad), the Puzzlecad export and grid conversion, the
Python module; the command line tool `shapelogic_cli` (BurrTools'
`burrTxt`/`burrTxt2`) and its Install menu item; the two solver types
(decision 0019: Complete and Fast (Crowell), BurrTools 2 removed).
User-facing known issues to carry: Gatekeeper and SmartScreen notes for any
unsigned copy, `--remote-debugging-port` taken by the packaged app, the
engine beside `app.asar` outside its integrity check, the online guide not
hosted yet, the AVX2 kernels' first run on real x86-64 still to come, the
Linux sandbox on Ubuntu 24.04 unverified.

## Known issues and notes
- `just web-dist` has no timeout of its own on Apple's queue; a network
  hiccup aborts a notarisation that Apple goes on with.
- The owner's uncommitted icon change (`assets/sl_icon.png` →
  `assets/puzzle_icon.jpg`) was not touched by this work.
- The oracle tiers could not run (no original checkout); the rename commit's
  oracle state is unverified.

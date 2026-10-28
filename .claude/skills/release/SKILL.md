---
name: release
description: Make a ShapeLogic release from start to finish when the user asks for one ("release v1.5", "do a v1.4.1 patch", "new release"). Writes the notes and the news post, rebuilds a stale demo, runs `just release`, watches the GitHub build, re-runs jobs that failed for want of a runner, falls back to the Staple workflow, publishes the draft, commits and pushes the news post, and keeps a copy of the files.
---

# Releasing ShapeLogic

The user asks only for the version ("release v1.5", "patch v1.4.1"). Everything
below is done without asking: **a release request is the user's standing
permission** to commit and push the release's files to main, push the tag,
re-run jobs, run the Staple workflow and publish the draft. Stop and ask only
where this file says so.

The repo is `shapelogicxyz/shapelogic`; work in the repo base
(`/Users/colin/git/shapelogic/shapelogic`). `just release` with no version
prints the steps and the next version; `.github/scripts/release.sh`,
`.github/workflows/release.yml`, `staple.yml` and `patch.yml` are the
machinery. Keep the user posted in a short line at each stage.

## 0. Check

- `just release` (no version): the next version it names. A new release is
  `vX.Y` (the highest tag + 0.1); a patch is `vX.Y.Z` (a fix to the published
  `vX.Y`). If the user's version does not fit, **ask**.
- `git status`: on `main`, in step with `origin/main`. Uncommitted changes that
  are not this release's (half-done work) → **ask** before going on; never
  commit them as part of the release.
- `git log <last tag>..HEAD`: what the release holds. Nothing since the last
  tag → **ask**.

## 1. Notes and news post

Follow the release-notes rules (the user's memory "Release notes"):

- **New release**: `release/notes/vX.Y.md`, `# ShapeLogic vX.Y`, `## What's new`
  with bold-led bullets in plain words for the people who use the app (what
  they can do now, not how it is built), then "Many small bug fixes and
  improvements." No download tables (`just release` adds them). Read the
  commits since the last tag, and their diffs where a message is unclear.
  A new `<article class="post">` at the top of the News section of
  `site/src/pages/index.astro`, in the style of the ones below it, dated today,
  linking to a guide page and to `https://github.com/shapelogicxyz/shapelogic/releases/tag/vX.Y`.
- **Patch**: `release/notes/vX.Y.Z.md`, bullets only. Edit the existing `vX.Y`
  post instead of adding one: an "Updated to vX.Y.Z" paragraph and the GitHub
  link pointed at `/releases/tag/vX.Y.Z`.
- `release/` is git-ignored: the notes are never committed.
- `just site-build` must pass with the post in place.
- Then set the post aside so the tree is clean for `just release`:
  `git stash push -m "vX.Y news post" -- site/src/pages/index.astro`.

## 2. Demo

`just site-build` checks the demo's hash. If the demo is stale (the check
fails on `site/public/demo/manifest.json`), run `just site-demo`, then commit
`site/public/demo` ("Demo rebuilt for vX.Y") and push main before tagging, so
the release's commit carries a current demo.

## 3. Start

`echo y | just release vX.Y` (the script's one question is its [y/N]). It
checks, pushes the tag and makes the draft release. Then find the Release run:

```
gh run list -R shapelogicxyz/shapelogic --workflow release.yml --branch vX.Y --limit 1 --json databaseId,status,url
```

(it may take a few seconds to appear).

## 4. Watch the build (30–40 minutes, longer with Apple)

Wait in the background, so the user can keep working:

```
gh run watch <run> -R shapelogicxyz/shapelogic --exit-status --interval 60
```

with `run_in_background`; the result comes back when it ends. On a success go
to step 5. On a failure read `gh run view <run>` (and `--log-failed`) and:

- **No runner** ("The job was not acquired by Runner of type hosted", most
  often on macOS, with capacity warnings): `gh run rerun <run> --failed`, then
  watch again. Up to three re-runs, the later ones after a pause of 10–20
  minutes. Still failing → report to the user and stop.
- **Apple too slow** (the macOS job says notarisation did not finish in time;
  the run's summary gives the submission id and the Staple command): check
  Apple's queue with

  ```
  xcrun notarytool history --keychain-profile shapelogic-notary
  ```

  (or `xcrun notarytool info <id> --keychain-profile shapelogic-notary`). The
  profile is in the user's login keychain; if it is missing, ask the user to
  create it (`xcrun notarytool store-credentials shapelogic-notary`) and never
  put the Apple ID, team ID or password in a command or a file. Once
  it says **Accepted**, run the summary's
  `gh workflow run staple.yml -f run_id=<run> -f tag=vX.Y`, find that run and
  watch it the same way. **Invalid** or **Rejected** → report and stop.
- **A real failure** (a compile error, a failed step that is not
  `continue-on-error`, a signing error): report what failed with the log lines
  and stop. Do not re-run it.
- Known and harmless: the Windows job's "Unit tests" step fails `unit` and
  `python` (the Python module's DLL) and is `continue-on-error`; the
  Node.js 20 and MINGW64 deprecation warnings.

## 5. Check and publish the draft

```
gh release view vX.Y -R shapelogicxyz/shapelogic --json isDraft,assets,body
```

It must be a draft with **9 files**: 4 macOS (`.dmg` and `.zip`, arm64 and
x64), 1 Windows `-setup.exe`, 4 Linux (AppImage and `.deb`, x64 and arm64);
names as in `.github/scripts/release-downloads.md`. The body must hold the
notes and the download tables. Anything missing → report and stop.

Then publish: `gh release edit vX.Y -R shapelogicxyz/shapelogic --draft=false`.

For a **patch**, publishing starts the Patch workflow (it folds the patch into
`vX.Y`): find it (`gh run list --workflow patch.yml --limit 1`), watch it, and
check `gh release view vX.Y` shows the new files and the patch section.

## 6. Website

`git stash pop`, `just site-build`, then commit the post
("Site: news post for vX.Y"; a patch: "Site: the vX.Y news post updated to
vX.Y.Z") and push main. Cloudflare deploys it; if a `site.yml` run starts,
watch it as in step 4.

## 7. Keep a copy, report

`just release-fetch vX.Y` (the files into `release/vX.Y`). Then tell the user
in a few lines: the version, the release page link, what was re-run or
stapled, anything that went wrong, and that the news post is live.

## Commits

Commit messages end with the attribution lines the session gives. A release
request permits only the commits named above (the demo, the news post); other
work in the tree stays uncommitted unless the user asks.

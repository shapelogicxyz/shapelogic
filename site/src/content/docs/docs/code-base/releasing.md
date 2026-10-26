---
title: Creating a New Release
description: "How a ShapeLogic release is made with `just release`: a new version or a patch to the current one, what runs on GitHub, and what to do when a step fails."
---

A release is the set of installers on the project's
[GitHub Releases](https://github.com/shapelogicxyz/shapelogic/releases)
page, with notes that say what changed. One command, `just release`,
starts it. GitHub Actions builds, signs and notarises the installers and
attaches them to a draft release. A person checks the draft and publishes
it.

Run `just release` with no version to print the next version number and
the steps below.

## Two kinds of release

| | A new release | A patch release |
|---|---|---|
| Version | `vX.Y`, such as `v1.4` | `vX.Y.Z`, such as `v1.3.1` |
| Use it for | new features: what the news post announces | a small fix to the release that is already out |
| Command | `just release v1.4` | `just release v1.3.1` |
| Release notes | the title and a What's new list | only the bullets of what the patch changes |
| On the Releases page | a new release | the existing release is updated in place: it moves to the new tag, with the new installers and a new section in its notes |
| Front page | a new news post | the existing post is edited |

Both kinds get a git tag of their own, and the tag is never moved. The
app's version is the tag's: `v1.4` is version 1.4.0, `v1.3.1` is 1.3.1.

## Before the first release

- **The GitHub CLI**, signed in with push access: `brew install gh`, then
  `gh auth login`.
- **The repository's secrets** for signing and notarising the macOS
  builds: `MACOS_CERT_P12_BASE64`, `MACOS_CERT_PASSWORD`,
  `APPLE_API_KEY_P8`, `APPLE_API_KEY_ID` and `APPLE_API_ISSUER`. The
  header of `.github/workflows/release.yml` says what each one is.
- **The tags**, so the local build knows its version: `git fetch --tags`.

## The release folder

Release notes and other release files live in `release/` at the root of
the repository. The folder is git-ignored, so nothing in it is ever
committed. `just release` creates it when it is missing.

| Path | What it holds |
|---|---|
| `release/notes/v1.4.md` | the notes you write for a release or a patch |
| `release/notes/v1.4.full.md` | the whole page `just release` sent to GitHub: your notes with the download tables |
| `release/v1.4/` | a copy of a published release's installers, from `just release-fetch v1.4` |

The notes reach GitHub without git: `just release` sends them with
`gh release create` when it makes the draft. Nothing that runs on GitHub
reads `release/`. The download tables ("Which file to download") are added
to every release's notes from `.github/scripts/release-downloads.md`, which
is tracked, so the file names in them always match the version.

## A new release, step by step

1. **Write the notes.** Put them in `release/notes/v1.4.md`: a title
   (`# ShapeLogic v1.4`), a "What's new" list of bullets that each start
   with a bold phrase, and "Many small bug fixes and improvements." Leave
   out the download tables. Also write a news post for the front page
   (`site/src/pages/index.astro`), but do not commit it yet: the release it
   links to does not exist until step 4.
2. **Commit and push the code to `main`.** The release is built from what
   GitHub has.
3. **Run `just release v1.4`.** It checks the following, and stops with a
   message if one fails:
   - the version is `vX.Y`, and newer than every tag;
   - `release/notes/v1.4.md` exists and is not empty;
   - `gh` is installed and signed in;
   - you are on `main`, with no uncommitted changes, and `main` is the
     same as `origin/main`;
   - neither the tag nor a release `v1.4` exists yet.

   It then shows the commit and the notes file and asks you to confirm.
   When you answer `y`, it pushes the tag `v1.4` and creates the **draft**
   release with your notes and the download tables.
4. **Wait about 30 to 40 minutes.** The tag starts the Release workflow
   (below). When it finishes, the draft has nine installers.
5. **Check the draft and publish it.** On the Releases page, open the
   draft, read the notes, check the nine files are there, and click
   **Publish release**. Only a person publishes: the workflows never do.
6. **Commit the news post** to `main`. Cloudflare deploys the website from
   `main`, so the post goes live a few minutes later.

From then on, the app's update check offers v1.4 to everyone on an older
version.

## A patch release, step by step

A patch ships a small fix without a new release on the Releases page or
a new news post. Use the next number after the release that is out:
`v1.3.1` after `v1.3`, then `v1.3.2`.

1. **Write the patch's notes.** Put only the bullets of what changed in
   `release/notes/v1.3.1.md`, with no heading. Edit the release's existing
   news post rather than adding one: add a line saying it was updated to
   v1.3.1 and what changed, and point its GitHub link at
   `/releases/tag/v1.3.1`. Do not commit the post yet.
2. **Commit and push the fix to `main`.**
3. **Run `just release v1.3.1`.** It runs the same checks as for a new
   release. It also finds the published release it updates (`v1.3`, or
   `v1.3.1` when you make `v1.3.2`) and lists the commits since it. It then
   makes the draft `v1.3.1`, whose notes are the whole page as it will be:
   - the published release's notes, with the title changed to v1.3.1;
   - a section `## v1.3.1 (date)` with your bullets;
   - the download tables with the v1.3.1 file names.

   The first line of the draft's notes is a hidden comment,
   `<!-- shapelogic-patch-of: v1.3 -->`, that names the release to update.
   It does not show on the page.
4. **Wait about 30 to 40 minutes** for the build, as for a new release.
5. **Check the draft and publish it.** Publishing starts the Patch
   workflow, which within a minute or two folds the draft into the
   published release:
   - the release `v1.3` moves to the tag `v1.3.1` and is renamed
     "ShapeLogic v1.3.1";
   - its notes become the draft's, without the hidden comment;
   - its nine installers are replaced by the new ones;
   - it stays the Latest release, with its original date;
   - the draft's own release is deleted. Its tag `v1.3.1` stays.

   The tag `v1.3` stays where it was, on the code of the original release.
6. **Commit the edited news post** to `main`.

The release page ends up as one release that reads like a short history:
the original What's new, then a section for each patch, then the download
tables. The app's update check reads the latest release's tag, so it
offers v1.3.1 to everyone on v1.3.0 or older.

Links to `/releases/tag/v1.3`, the old tag, now show the bare tag instead
of the release page. Links to `/releases/latest`, which the front page's
Download button and the app use, keep working.

## What runs on GitHub

The four release workflows are in `.github/workflows/`, and the scripts
they share are in `.github/scripts/`.

- **Release** (`release.yml`) starts when a `v*` tag is pushed.
  - The `version` job takes the version from the tag and passes it to every
    build as `SHAPELOGIC_VERSION`.
  - The Linux (x64 and arm64), Windows (x64) and macOS (Apple silicon and
    Intel) jobs build the engine and the app, and keep the installers as
    workflow artifacts.
  - Each macOS job signs the app with the Developer ID certificate, sends
    the disk image to Apple's notary service and waits for Apple's answer,
    for up to an hour. It then staples the ticket onto the `.dmg` and the
    app in the `.zip` (`staple.sh`), and checks that Gatekeeper accepts them.
  - The `publish` job runs last.
- **Publish** (`publish.yml`) collects the five artifacts and renames the
  files with `prepare-release.py` (`ShapeLogic-v1.4-linux-x64.deb` and so
  on). It attaches the nine files to the tag's draft release and replaces
  any of the same name. It never touches a published release, and it
  leaves the draft's notes alone.
- **Patch** (`patch.yml`) starts whenever a release is published. It does
  nothing unless the release's notes begin with the patch comment. For a
  patch, it folds the release in as described above. Before it changes
  anything, it keeps the patch's files and notes as an artifact of its run.
- **Staple** (`staple.yml`) is run by hand when Apple takes longer than the
  Release workflow waits (below).

Release can also be run by hand from the Actions tab (Release ▸ Run
workflow) to make test builds of a branch. These builds are workflow
artifacts only: they are not attached to any release. They take the newest
tag's version.

## When a step fails

| What happened | What to do |
|---|---|
| `just release` stops with a message | Fix what it says and run it again. Nothing was pushed: the checks all run before the question. |
| A build job failed (a test, a compiler error, a network error) | Open the run on the Actions tab. For a passing problem, choose **Re-run failed jobs**. For a fault in the code, see the last row. |
| A macOS job says Apple has not finished | Apple took longer than an hour. When `xcrun notarytool info <id>` says **Accepted** (the run's summary gives the id), run `gh workflow run staple.yml -f run_id=<the run's number> -f tag=v1.4`. It staples the builds and attaches all the files to the draft. |
| A macOS job says Apple refused the submission (Invalid) | The job prints Apple's log of what it found. Fix the signing, then start again (last row). |
| The draft has no notes, only the download tables | The tag was pushed without `just release`. Edit the draft's notes on GitHub before publishing. |
| The Patch workflow failed partway | Its run keeps the patch's files and notes as an artifact named `patch-v1.3.1`. If the patch release still exists, run `gh workflow run patch.yml -f tag=v1.3.1`. If not, finish by hand from the artifact with `gh release upload` and `gh release edit`. |
| The release needs a code fix before it is published | Delete the draft and its tag: `gh release delete v1.4 --cleanup-tag --yes` and `git tag -d v1.4`. Then fix, commit, push, and run `just release v1.4` again. |
| A published release needs a fix | Make a patch release. |

## The version in the code

The version is not written in any file: it comes from the git tag.
`cmake/ProjectVersion.cmake` decides it for the engine, the command line
tool and the Python module, and `web/tools/project-version.ts` follows the
same rule for the app:

1. `SHAPELOGIC_VERSION`, when it is set (the Release workflow sets it from
   the tag);
2. otherwise the newest `vX.Y` or `vX.Y.Z` tag that `git describe` finds;
3. otherwise `0.0.0`, in a source tree without git.

A build between two releases says the version of the last one. The app
checks at startup that the engine says the same version as itself, so
after you pull a new tag, build the engine again (`just build release`).

**About ShapeLogic** shows the version. The app asks GitHub for the
latest release a few seconds after it starts (unless that is turned off in
Settings ▸ General), and **Check for Updates…** asks at any time. Both compare the latest release's tag with the app's
version (`web/src/main/updates.ts`).

## Commands

```bash
just release                # the next version and these steps
just release v1.4           # a new release: check, push the tag, make the draft
just release v1.3.1         # a patch: the same, updating the published v1.3 on Publish
just release-fetch v1.4     # a copy of a release's installers in release/v1.4
```

#!/usr/bin/env bash
# `just release [vX.Y | vX.Y.Z]`: starts a ShapeLogic release. With no
# version it prints how, and the release steps. With one it checks
# everything, asks, then pushes the tag (which starts the Release workflow,
# release.yml) and makes the DRAFT GitHub release with the notes from
# release/notes/<version>.md (git-ignored, local only) and the download
# tables (.github/scripts/release-downloads.md). The workflow attaches the
# files to the draft; a person publishes it.
#
# A patch (vX.Y.Z, Z > 0) updates the published vX.Y release instead of
# making a new one. The draft's notes are that release's (its heading made
# vX.Y.Z), then a "vX.Y.Z (date)" section of the patch's notes, then fresh
# download tables; their first line is a hidden marker naming the release
# it updates. Publishing the draft starts the Patch workflow (patch.yml),
# which moves the vX.Y release onto the new tag with the new files and
# notes, and removes the draft's release. The old tag stays where it was.
set -euo pipefail
trap 'echo "release: stopped (line ${LINENO}): nothing after this step was done" >&2' ERR

REPO=shapelogicxyz/shapelogic
root="$(git rev-parse --show-toplevel)"
cd "${root}"
notes_dir=release/notes
mkdir -p "${notes_dir}"

newest_tag() {
  git tag --list 'v[0-9]*' | grep -E '^v[0-9]+\.[0-9]+(\.[0-9]+)?$' | sort -V | tail -1
}

# the highest tag + 0.1 (v1.3 → v1.4, v1.3.2 → v1.4)
next_version() {
  local t="$1"
  if [ -z "${t}" ]; then
    echo v1.0
    return
  fi
  local major minor patch
  IFS=. read -r major minor patch <<< "${t#v}"
  echo "v${major}.$((minor + 1))"
}

# the next patch of a release (v1.4 → v1.4.1, v1.4.1 → v1.4.2)
next_patch() {
  local t="$1"
  if [ -z "${t}" ]; then
    echo v1.0.1
    return
  fi
  local major minor patch
  IFS=. read -r major minor patch <<< "${t#v}"
  echo "v${major}.${minor}.$((${patch:-0} + 1))"
}

# whether $2 is a later version than $1
newer() {
  [ "$1" != "$2" ] && [ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | tail -1)" = "$2" ]
}

usage() {
  local newest next patch
  newest="$(newest_tag)"
  next="$(next_version "${newest}")"
  patch="$(next_patch "${newest}")"
  cat << EOF
Start a ShapeLogic release:

    just release ${next}

The newest release tag is ${newest:-none}, so the next is ${next}. A small fix
to the published release is a patch, \`just release ${patch}\`, which
updates that release in place instead of making a new one (below).

The release steps:
  1. Notes. Ask AI for the release notes. It writes ${notes_dir}/${next}.md
     (the title and What's new; the download tables are added) and a news
     <article> for site/src/pages/index.astro, left uncommitted. The notes
     need no commit: release/ is git-ignored.
  2. Start. Commit and push the code to main, then run \`just release ${next}\`.
     It checks, asks, pushes the tag ${next} (which starts the Release
     workflow) and makes the draft release with the notes.
  3. Wait about 30 to 40 minutes: Actions builds every platform, notarises
     and staples the macOS builds, and attaches the 9 files to the draft.
     Then open the draft on GitHub, check it, and click Publish.
  4. Website. Commit the news post to main; Cloudflare deploys it.
  5. Fallback, only when Apple took longer than an hour: once
     \`xcrun notarytool info <id>\` says Accepted, run
     \`gh workflow run staple.yml -f run_id=<run> -f tag=${next}\`
     (the Release run's summary gives the command); it staples and attaches
     the files.

A patch (\`just release ${patch}\`) takes the same steps, except:
  1. The notes in ${notes_dir}/${patch}.md are only what the patch changes
     (bullets). On the release page they become a "${patch}" section after
     the What's new, before the download tables. The release's news article
     is edited (an "Updated to ${patch}" line, the link to the new tag), not
     a new one added.
  3. Publishing the ${patch} draft folds it into the published release (the
     Patch workflow): the release moves to the tag ${patch} with the new
     files and notes, and the draft's release is removed. The old tag stays.

Keep a copy of the published files: \`just release-fetch <version>\`.
EOF
}

fail() {
  echo "release: $*" >&2
  exit 1
}

if [ $# -eq 0 ] || [ -z "${1:-}" ]; then
  usage
  exit 0
fi

version="$1"
[[ "${version}" =~ ^v[0-9]+\.[0-9]+(\.[0-9]+)?$ ]] ||
  fail "${version} is not a release version: vX.Y, or vX.Y.Z for a patch, such as $(next_version "$(newest_tag)")"
if [[ "${version}" =~ ^v[0-9]+\.[0-9]+\.0$ ]]; then
  fail "${version}: a release is ${version%.0}, a patch of it ${version%.0}.1"
fi

notes="${notes_dir}/${version}.md"
[ -s "${notes}" ] ||
  fail "no release notes in ${notes}: write them there first (ask AI for the release notes for ${version})"

command -v gh > /dev/null || fail "the GitHub CLI (gh) is needed: brew install gh"
gh auth status > /dev/null 2>&1 || fail "gh is not signed in: gh auth login"

[ "$(git branch --show-current)" = main ] || fail "not on main: a release is tagged on main"
[ -z "$(git status --porcelain --untracked-files=no)" ] ||
  fail "the tree has uncommitted changes: commit (and push) them, or stash them"
git fetch --quiet --tags origin main
[ "$(git rev-parse HEAD)" = "$(git rev-parse origin/main)" ] ||
  fail "main is not origin/main: push (or pull) first, so the tag is on what GitHub has"

if git rev-parse --verify --quiet "refs/tags/${version}" > /dev/null; then
  fail "the tag ${version} exists already"
fi
if git ls-remote --exit-code --tags origin "refs/tags/${version}" > /dev/null 2>&1; then
  fail "the tag ${version} exists on GitHub already"
fi
if gh release view "${version}" -R "${REPO}" > /dev/null 2>&1; then
  fail "a release ${version} exists on GitHub already"
fi

# a patch: vX.Y.Z with Z > 0 updates the published release of vX.Y
base=""
IFS=. read -r v_major v_minor v_patch <<< "${version#v}"
if [ -n "${v_patch}" ] && [ "${v_patch}" != 0 ]; then
  series="v${v_major}.${v_minor}"
  # the series' published release: tagged vX.Y, or the patch it last moved to
  base="$(gh release list -R "${REPO}" --limit 200 --json tagName,isDraft \
    --jq '.[] | select(.isDraft | not) | .tagName' |
    grep -E "^v${v_major}\.${v_minor}(\.[0-9]+)?$" | sort -V | tail -1 || true)"
  [ -n "${base}" ] || fail "${version} is a patch, but there is no published ${series} release to update"
  newer "${base}" "${version}" || fail "${version} is not newer than the release it updates, ${base}"
else
  newest="$(newest_tag)"
  if [ -n "${newest}" ] && ! newer "${newest}" "${version}"; then
    fail "${version} is not newer than ${newest}"
  fi
fi

full="${notes_dir}/${version}.full.md"
if [ -z "${base}" ]; then
  {
    cat "${notes}"
    echo
    sed "s/{version}/${version}/g" .github/scripts/release-downloads.md
  } > "${full}"
else
  # the patch's notes are bullets: a heading of their own is dropped
  patch_notes="$(grep -vE '^#{1,2} ' "${notes}" | awk 'NF { started = 1 } started' || true)"
  [ -n "${patch_notes}" ] || fail "${notes} has nothing but a heading: write what the patch changes, as bullets"
  base_notes="$(gh release view "${base}" -R "${REPO}" --json body --jq .body)" ||
    fail "could not read the notes of the release ${base}"
  day="$(date '+%d %B %Y' | sed 's/^0//')"
  {
    echo "<!-- shapelogic-patch-of: ${base} -->"
    # the release's notes up to its download tables, the heading made this
    # version, without trailing blank lines
    printf '%s\n' "${base_notes}" | tr -d '\r' |
      sed '/^<!-- shapelogic-patch-of:/d' |
      awk '/^## Which file to download/ { exit } { print }' |
      sed -E "s/^# ShapeLogic v[0-9]+\.[0-9]+(\.[0-9]+)?[[:space:]]*$/# ShapeLogic ${version}/" |
      awk '{ lines[NR] = $0 } NF { last = NR } END { for (i = 1; i <= last; i++) print lines[i] }'
    echo
    echo "## ${version} (${day})"
    echo
    printf '%s\n' "${patch_notes}"
    echo
    sed "s/{version}/${version}/g" .github/scripts/release-downloads.md
  } > "${full}"
fi

if [ -z "${base}" ]; then
  cat << EOF
Release ${version}
  commit  $(git log -1 --format='%h %s')
  notes   ${notes} (with the download tables: ${full})
  title   ShapeLogic ${version}

This pushes the tag ${version} to GitHub, which starts the Release workflow,
and makes the draft release ${version} with the notes.
EOF
else
  cat << EOF
Patch ${version}, updating the published release ${base}
  commit  $(git log -1 --format='%h %s')
  notes   ${notes}, added to ${base}'s (the whole page: ${full})
  title   ShapeLogic ${version}

What it adds since ${base}:
$(git log --format='  %h %s' "${base}..HEAD")

This pushes the tag ${version} to GitHub, which starts the Release workflow,
and makes the draft release ${version} with the notes. Publishing the draft
moves the ${base} release onto ${version}, with the new files and notes, and
removes the draft's release (the Patch workflow). The tag ${base} stays.
EOF
fi
read -r -p "Push tag ${version} and create the draft release? [y/N] " answer
case "${answer}" in
  y | Y | yes) ;;
  *)
    echo "Nothing done."
    exit 1
    ;;
esac

git tag -a "${version}" -m "ShapeLogic ${version}"
git push origin "refs/tags/${version}"
url="$(gh release create "${version}" -R "${REPO}" --draft --verify-tag \
  --title "ShapeLogic ${version}" --notes-file "${full}")"

cat << EOF

The tag ${version} is pushed and the draft release is made:
  draft     ${url}
  workflow  https://github.com/${REPO}/actions/workflows/release.yml

The files arrive on the draft in about 30 to 40 minutes. Then check the
draft and click Publish, and commit the news post to main.
EOF
if [ -n "${base}" ]; then
  cat << EOF
Publishing it folds it into the ${base} release:
  https://github.com/${REPO}/actions/workflows/patch.yml
EOF
fi

#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Every test suite of the repo, one after another, with a table of what
# passed at the end: `just gate` (the groups below that are on by default),
# `just gate unit oracle` (only those groups), `just gate all` (every group).
# A suite that fails does not stop the run. Each suite's output is kept in
# build/gate/<nn>-<name>.log, the table in build/gate/summary.txt; the exit
# code is 1 when a suite failed.
#
#   build       the dev and release presets
#   unit        just test-all, on both presets (the release one twice: a
#               test that fails one run in a few shows there)
#   sanitizers  just test-asan, just test-tsan
#   python      the Python module's tests and its wheel
#   oracle      every comparison with the original (needs ../burr-tools
#               built, or BT_OLD_REPO): summaries on all threads and one,
#               both take-aparts, sliding, stacking, resume, round trip,
#               solved files, text, imports, the Python transcripts, the
#               block packer, the Panex resume. The original's solved
#               files (tests/oracle/work, not committed) are recorded
#               first when they are not there.
#   app         just app-check, just app-test
#   check       just check (clang-tidy)
# Off unless named (or `all`):
#   e2e         just app-e2e: every Playwright spec, then the timed one
#   site        just site-build (fails on the demo's hash until the demo
#               is rebuilt for the version: just site-demo)
#   dist        just app-dist --dir, just app-smoke
#
# Run it on a quiet machine: the suites run side by side inside themselves
# and some time real solves.
set -u
cd "$(dirname "$0")/.."

DEFAULT="build unit sanitizers python oracle app check"
EVERY="$DEFAULT e2e site dist"
groups="$*"
[ -z "$groups" ] && groups="$DEFAULT"
[ "$groups" = "all" ] && groups="$EVERY"
for g in $groups; do
  case " $EVERY " in
    *" $g "*) ;;
    *) echo "gate: no group '$g' (the groups: $EVERY, or all)"; exit 2 ;;
  esac
done
wanted() { case " $groups " in *" $1 "*) return 0 ;; esac; return 1; }

OUT=build/gate
rm -rf "$OUT"
mkdir -p "$OUT"
SUMMARY="$OUT/summary.txt"
: > "$SUMMARY"
n=0
failed=0

# run <name> <command...>: one suite, its output in a log of its own
run() {
  n=$((n + 1))
  local name="$1"
  shift
  local log
  log="$OUT/$(printf '%02d' "$n")-$name.log"
  local t0=$SECONDS
  printf '%s  ....  %-28s %s\n' "$(date +%T)" "$name" "$*"
  "$@" > "$log" 2>&1
  local rc=$?
  local verdict=PASS
  if [ $rc -ne 0 ]; then
    verdict="FAIL($rc)"
    failed=$((failed + 1))
  fi
  printf '%-9s %-28s %5ss  %s\n' "$verdict" "$name" "$((SECONDS - t0))" "$*" | tee -a "$SUMMARY"
}

ORACLE="python3 tests/oracle/oracle.py"

if wanted build; then
  run build-dev just build dev
  run build-release just build release
fi
if wanted unit; then
  run test-all just test-all
  run test-all-release just test-all release
  run test-all-release-again just test-all release
fi
if wanted sanitizers; then
  run test-asan just test-asan
  run test-tsan just test-tsan
fi
if wanted python; then
  run python-test just python-test
  run python-test-release just python-test release
  run python-wheel just python-wheel
fi
if wanted oracle; then
  run oracle just oracle --tier all
  run oracle-one-thread just oracle --tier all --threads 1
  run oracle-crowell just oracle --tier all --solver crowell
  run oracle-crowell-one-thread just oracle --tier all --solver crowell --threads 1
  run oracle-sliding just oracle --tier sliding
  run oracle-sliding-one-thread just oracle --tier sliding --threads 1
  run oracle-stacking just oracle --tier stacking
  run oracle-stacking-panex just oracle --tier stacking --solver panex
  run oracle-resume just oracle-resume
  # The original's solved files: recorded once, kept under
  # tests/oracle/work. The text runs that read them (retake, ask) are
  # recorded with them: which of several solutions with the same moves the
  # original keeps depends on its threads, so their text goes with the
  # files it was made from (this rewrites baseline/text-*.json).
  work=tests/oracle/work
  if [ ! -d "$work/old/examples" ] || [ ! -d "$work/old/tictest" ]; then
    run baseline-files $ORACLE baseline-files --tier all
    run baseline-text $ORACLE baseline-text --tier all
  fi
  if [ ! -d "$work/old/alpha/tests/data/puzzles/sliding" ]; then
    run baseline-files-sliding $ORACLE baseline-files --tier sliding
    run baseline-text-sliding $ORACLE baseline-text --tier sliding
  fi
  if [ ! -d "$work/old/alpha/tests/data/puzzles/stacking" ]; then
    run baseline-files-stacking $ORACLE baseline-files --tier stacking
    run baseline-text-stacking $ORACLE baseline-text --tier stacking
  fi
  if [ ! -d "$work/old-panex" ]; then
    run baseline-files-panex $ORACLE baseline-files --tier stacking --solver panex
    run baseline-text-panex $ORACLE baseline-text --tier stacking --solver panex
  fi
  run oracle-roundtrip just oracle-roundtrip
  run oracle-files just oracle-files --tier all
  run oracle-files-sliding just oracle-files --tier sliding
  run oracle-files-stacking just oracle-files --tier stacking
  run oracle-files-panex just oracle-files --tier stacking --solver panex
  run oracle-text just oracle-text --tier all
  run oracle-text-sliding just oracle-text --tier sliding
  run oracle-text-stacking just oracle-text --tier stacking
  run oracle-text-panex just oracle-text --tier stacking --solver panex
  run oracle-imports just oracle-imports
  run oracle-python just oracle-python --tier all
  run oracle-blockpack $ORACLE blockpack-check
  run oracle-panex-resume $ORACLE panex-resume
fi
if wanted app; then
  run app-check just app-check
  run app-test just app-test
fi
if wanted e2e; then
  run app-e2e just app-e2e
fi
if wanted site; then
  run site-build just site-build
fi
if wanted dist; then
  run app-dist just app-dist --dir
  run app-smoke just app-smoke
fi
if wanted check; then
  run check just check
fi

echo
echo "---- $(date +%T): $((n - failed)) of $n passed (logs in $OUT) ----"
cat "$SUMMARY"
[ "$failed" -eq 0 ]

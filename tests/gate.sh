#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Every test suite of the repo, one after another, with a table of what
# passed at the end: `just gate` (the groups below that are on by default),
# `just gate unit regression` (only those groups), `just gate all` (every group).
# A suite that fails does not stop the run. Each suite's output is kept in
# build/gate/<nn>-<name>.log, the table in build/gate/summary.txt; the exit
# code is 1 when a suite failed. GATE_OUT names another folder for the
# logs and the table (two gates at once each need their own: the gate
# empties its folder first).
#
#   build       the dev and release presets
#   unit        just test-all, on both presets (the release one twice: a
#               test that fails one run in a few shows there)
#   sanitizers  just test-asan, just test-tsan
#   python      the Python module's tests and its wheel
#   regression  the solver against its recorded answers (tests/regression,
#               files of this repo only): summaries on all threads and one,
#               every solver, sliding, stacking, and the text runs
#   app         just app-check, just app-test
#   check       just check (clang-tidy)
# Off unless named (or `all`):
#   e2e         just app-e2e: every Playwright spec, then the timed one
#   site        just site-build (fails on the demo's hash until the demo
#               is rebuilt for the version: just site-demo)
#   dist        just app-dist --dir, just app-smoke
#   burrtools   just burrtools-compare: speed and counts against upstream
#               BurrTools (needs ../burr-tools built, or BURRTOOLS_DIR)
#
# Each suite has a time limit (GATE_STEP_TIMEOUT seconds, 2 hours by
# default; the longest take about 20 minutes): one that outlasts it is
# stopped and marked TIMEOUT, so the table always prints.
#
# Run it on a quiet machine: the suites run side by side inside themselves
# and some time real solves.
set -u
cd "$(dirname "$0")/.."

# A suite that cannot run fails instead of skipping: the web engine tests
# without an engine, the smoke test without a packaged app.
export SHAPELOGIC_REQUIRE_ALL=1

DEFAULT="build unit sanitizers python regression app check"
EVERY="$DEFAULT e2e site dist burrtools"
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

OUT="${GATE_OUT:-build/gate}"
rm -rf "$OUT"
mkdir -p "$OUT"
SUMMARY="$OUT/summary.txt"
: > "$SUMMARY"
n=0
failed=0
LIMIT="${GATE_STEP_TIMEOUT:-7200}"

# The running suite's process group and its watcher's, stopped with the
# gate (Ctrl-C).
step=""
watcher=""
stop_step() {
  [ -n "$step" ] && kill -TERM -- "-$step" 2>/dev/null
  [ -n "$watcher" ] && kill -TERM -- "-$watcher" 2>/dev/null
  exit 130
}
trap stop_step INT TERM

# run <name> <command...>: one suite, its output in a log of its own, in a
# process group of its own (macOS has no `timeout`): a watcher stops the
# whole group when the suite outlasts LIMIT.
run() {
  n=$((n + 1))
  local name="$1"
  shift
  local log
  log="$OUT/$(printf '%02d' "$n")-$name.log"
  local t0=$SECONDS
  printf '%s  ....  %-28s %s\n' "$(date +%T)" "$name" "$*"
  rm -f "$log.timeout"
  set -m
  "$@" > "$log" 2>&1 < /dev/null &
  step=$!
  (
    sleep "$LIMIT"
    touch "$log.timeout"
    echo "gate: stopped after ${LIMIT}s (GATE_STEP_TIMEOUT)" >> "$log"
    kill -TERM -- "-$step" 2>/dev/null
    sleep 10
    kill -KILL -- "-$step" 2>/dev/null
  ) > /dev/null 2>&1 &
  watcher=$!
  set +m
  wait "$step"
  local rc=$?
  kill -TERM -- "-$watcher" 2>/dev/null
  wait "$watcher" 2>/dev/null
  step=""
  watcher=""
  local verdict=PASS
  if [ -e "$log.timeout" ]; then
    rm -f "$log.timeout"
    verdict="TIMEOUT"
    failed=$((failed + 1))
  elif [ $rc -ne 0 ]; then
    verdict="FAIL($rc)"
    failed=$((failed + 1))
  fi
  printf '%-9s %-28s %5ss  %s\n' "$verdict" "$name" "$((SECONDS - t0))" "$*" | tee -a "$SUMMARY"
}

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
if wanted regression; then
  run regress just regress --tier all
  run regress-one-thread just regress --tier all --threads 1
  run regress-crowell just regress --tier all --solver crowell
  run regress-crowell-one-thread just regress --tier all --solver crowell --threads 1
  run regress-sliding just regress --tier sliding
  run regress-sliding-one-thread just regress --tier sliding --threads 1
  run regress-stacking just regress --tier stacking
  run regress-stacking-panex just regress --tier stacking --solver panex
  run regress-text just regress-text --tier all
  run regress-text-crowell just regress-text --tier all --solver crowell
  run regress-text-sliding just regress-text --tier sliding
  run regress-text-stacking just regress-text --tier stacking
  run regress-text-panex just regress-text --tier stacking --solver panex
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
if wanted burrtools; then
  run burrtools-compare just burrtools-compare
fi

echo
echo "---- $(date +%T): $((n - failed)) of $n passed (logs in $OUT) ----"
cat "$SUMMARY"
[ "$failed" -eq 0 ]

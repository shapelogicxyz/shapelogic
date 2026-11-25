#!/usr/bin/env bash
# Runs a Release run's jobs again when they got no runner: the Retry
# workflow (retry.yml) calls it when a Release run has failed.
#
#   .github/scripts/retry.sh <run id>
#
# A job that got no runner (GitHub's queue gave up on it, mostly a macOS
# one: "The job was not acquired by Runner … even after multiple attempts")
# ends canceled, with no step run, and the run fails. When every job of the
# run's latest attempt that did not succeed is so, this waits ten minutes
# and re-runs them (`gh run rerun --failed`: those jobs and the ones that
# need them, the publish job too), then follows the new attempt: once each
# re-run job has a runner it is done; if one got none again, it waits for
# the attempt's end and starts over. A job that ran a step and failed is a
# real failure: then nothing is re-run. It stops at the run's twelfth
# attempt, and when the run is going or has passed (another Retry run
# waited for this one: the concurrency group).
#
# Needs GH_TOKEN (actions: write) and GITHUB_REPOSITORY.
set -euo pipefail

run_id="$1"
repo="${GITHUB_REPOSITORY}"
max_attempts=12
wait_seconds=600

api() { gh api "repos/${repo}/actions/runs/${run_id}$1" "${@:2}"; }

# The jobs of an attempt that did not succeed: "<steps run> <name>" a line.
unfinished_jobs() {
  api "/attempts/$1/jobs" --paginate \
    --jq '.jobs[] | select(.conclusion == "failure" or .conclusion == "cancelled" or .conclusion == "timed_out") | "\(.steps | length) \(.name)"' # the API's word: british-ok
}

while true; do
  read -r status conclusion attempt < <(api "" --jq '"\(.status) \(.conclusion) \(.run_attempt)"')
  echo "run ${run_id}, attempt ${attempt}: ${status} ${conclusion}"
  if [ "${status}" != completed ] || [ "${conclusion}" != failure ]; then
    echo "nothing to re-run"
    exit 0
  fi
  if [ "${attempt}" -ge "${max_attempts}" ]; then
    echo "::warning::run ${run_id} failed on its attempt ${attempt}: not re-run past ${max_attempts} attempts (re-run it by hand)"
    exit 0
  fi
  jobs="$(unfinished_jobs "${attempt}")"
  if [ -z "${jobs}" ]; then
    echo "no job failed: nothing to re-run"
    exit 0
  fi
  echo "${jobs}"
  if grep -qv '^0 ' <<< "${jobs}"; then
    echo "a job ran and failed: a real failure, not re-run"
    exit 0
  fi
  names="$(cut -d' ' -f2- <<< "${jobs}")"
  echo "every failed job got no runner: re-running them in ${wait_seconds} s"
  sleep "${wait_seconds}"
  gh run rerun "${run_id}" --repo "${repo}" --failed
  next=$((attempt + 1))
  # follow the new attempt until each re-run job has a runner, or it ends
  while true; do
    sleep 60
    read -r status now < <(api "" --jq '"\(.status) \(.run_attempt)"')
    if [ "${now}" -lt "${next}" ]; then
      continue # the new attempt has not started yet
    fi
    if [ "${status}" = completed ]; then
      break
    fi
    # the jobs that have a runner (a runner's name, or a step run)
    started="$(api "/attempts/${next}/jobs" --paginate \
      --jq '.jobs[] | select((.runner_name // "") != "" or (.steps | length) > 0) | .name')"
    waiting=0
    while IFS= read -r name; do
      grep -qxF "${name}" <<< "${started}" || waiting=$((waiting + 1))
    done <<< "${names}"
    if [ "${waiting}" -eq 0 ]; then
      echo "attempt ${next}: every re-run job has a runner"
      exit 0
    fi
  done
done

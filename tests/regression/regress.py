#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""ShapeLogic's solver against its recorded answers: the regression suite.

Every puzzle of corpus.txt (files of this repository) is solved by
`shapelogic_cli` and its answers are compared with the ones recorded in
baseline/. Most were recorded from the original BurrTools while the port
was checked against it (decision 0007, amended); the rest are ShapeLogic's
own, where the original had no answer or a wrong one (each entry says which
in "from"). Nothing here reads or runs another program.

  summary  the `--json` totals: assemblies, solutions, levels, moves,
           rotations (baseline/<tier>[-<solver>].json)
  text     the text output of several runs of each puzzle, byte for byte
           with the solve time blanked (hashes in
           baseline/text-<tier>[-<solver>].json)

A change that alters an answer on purpose (a bug fixed) records the new one
with --update on the puzzles it touches (--filter), and says why in its
commit. Run through `just regress` and `just regress-text`; see README.md.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import contextlib
import glob
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
CORPUS = HERE / "corpus.txt"
BASELINE_DIR = HERE / "baseline"
# What a run writes (search folders, the text of runs that differ) goes to a
# folder of its own under build/regression, removed when the run passes, so
# two runs at once never share one. The text of recorded runs is kept beside
# it (not committed) to show where a later run differs.
RUN_ROOT = REPO / "build" / "regression"
RECORDED_TEXT = RUN_ROOT / "text-recorded"

# Fields that must match exactly. The solve time is shown, never compared.
COMPARED = ("assemblies", "solutions", "dotlevel", "level", "totalmoves", "moves", "rotations")
TIERS = ("fast", "full", "slow", "sliding", "stacking")
# The stacking tier's other solver; "classic", the default, is the Stacking
# Solver there, the sliding search on the sliding tier.
STACKING_SOLVERS = ("panex",)
SOLVERS = ("classic", "crowell", *STACKING_SOLVERS)

TEXT_TIME_RE = re.compile(rb"in [0-9]+\.[0-9]{3} seconds")


@dataclass(frozen=True)
class Entry:
    tier: str
    flags: str
    rel: str  # path relative to the repository


def load_corpus() -> list[Entry]:
    """The corpus's entries. A pattern that matches nothing ends the run
    (exit 2): a comparison of nothing must not pass."""
    entries: list[Entry] = []
    unmatched: list[str] = []
    for line in CORPUS.read_text().splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        tier, flags, pattern = line.split(None, 2)
        if tier not in TIERS:
            print(f"error: unknown tier {tier!r} in corpus.txt", file=sys.stderr)
            raise SystemExit(2)
        matches = sorted(glob.glob(str(REPO / pattern), recursive=True))
        if not matches:
            unmatched.append(pattern)
        entries += [Entry(tier, flags.replace("-", ""), str(Path(m).relative_to(REPO))) for m in matches]
    for pattern in unmatched:
        print(f"error: corpus pattern matched nothing: {pattern}", file=sys.stderr)
    if unmatched:
        raise SystemExit(2)
    return entries


def solver_tiers(tiers: list[str], solver: str) -> list[str]:
    """The tiers of the selection the solver runs on: the brick solvers on
    fast, full and slow, the sliding search on sliding, the stacking ones on
    stacking. "all" is fast, full and slow."""
    wanted = set(tiers)
    if "all" in wanted:
        wanted = (wanted - {"all"}) | {"fast", "full", "slow"}
    out = []
    for tier in sorted(wanted):
        if tier == "sliding":
            ok = solver == "classic"
        elif tier == "stacking":
            ok = solver in ("classic", *STACKING_SOLVERS)
        else:
            ok = solver not in STACKING_SOLVERS
        if ok:
            out.append(tier)
    return out


def select(entries: list[Entry], args: argparse.Namespace) -> list[Entry]:
    """The entries of the tiers the solver runs on that pass --filter; not
    a stacking entry flagged Q (no Panex columns) with the Panex Solver, nor
    one flagged C (a take-apart that does not end) with Crowell's."""
    wanted = set(solver_tiers(args.tier, args.solver))
    return [e for e in entries
            if e.tier in wanted and (not args.filter or args.filter in e.rel)
            and not (e.tier == "stacking" and "Q" in e.flags and args.solver == "panex")
            and not ("C" in e.flags and args.solver == "crowell")]


@contextlib.contextmanager
def search_env(entry: Entry, run_dir: Path):
    """The environment of one run: a sliding or stacking search gets a
    search folder of its own (SHAPELOGIC_SLIDE_DIR, SHAPELOGIC_PANEX_DIR),
    removed afterwards, so neither the user's cache nor another run's saved
    search is touched (decision 0035)."""
    if entry.tier not in ("sliding", "stacking"):
        yield None
        return
    root = run_dir / "search"
    root.mkdir(parents=True, exist_ok=True)
    folder = Path(tempfile.mkdtemp(dir=root))
    try:
        yield {**os.environ, "SHAPELOGIC_SLIDE_DIR": str(folder), "SHAPELOGIC_PANEX_DIR": str(folder)}
    finally:
        shutil.rmtree(folder, ignore_errors=True)


def solver_args(solver: str) -> list[str]:
    return [] if solver == "classic" else ["--solver", solver]


def flag_args(entry: Entry) -> list[str]:
    """R -R (the take-apart with rotations); N --nested-slides, P
    --partial-nested (sliding)."""
    return [opt for flag, opt in (("R", "-R"), ("N", "--nested-slides"), ("P", "--partial-nested"))
            if flag in entry.flags]


def baseline_path(tier: str, solver: str, text: bool = False) -> Path:
    name = ("text-" if text else "") + tier + ("" if solver == "classic" else f"-{solver}")
    return BASELINE_DIR / f"{name}.json"


def read_baselines(tiers: list[str], solver: str, text: bool) -> dict[str, dict]:
    out: dict[str, dict] = {}
    for tier in tiers:
        path = baseline_path(tier, solver, text)
        if path.exists():
            out.update(json.loads(path.read_text()))
    return out


def write_baseline(tier: str, solver: str, text: bool, updates: dict[str, dict]) -> None:
    path = baseline_path(tier, solver, text)
    data = json.loads(path.read_text()) if path.exists() else {}
    data.update(updates)
    path.write_text(json.dumps(dict(sorted(data.items())), indent=1) + "\n")
    print(f"wrote {path.relative_to(REPO)}: {len(updates)} entries recorded")


def compared_line(compared: int, total: int, differ: int, what: str) -> str:
    """The last line of every run: how much of its baseline it compared."""
    return f"{compared} of {total} {what} compared, {differ} differ"


def report_not_compared(expected: dict[str, dict], selected: set[str], args: argparse.Namespace) -> int:
    """A recorded entry of the selection that was not compared (its corpus
    line removed, a pattern that no longer matches it) is a failure."""
    missing = [rel for rel in sorted(expected)
               if rel not in selected and (not args.filter or args.filter in rel)]
    for rel in missing:
        print(f"{'NOT COMPARED':12} {rel}  recorded, but not in the corpus")
    return len(missing)


# ---- summaries --------------------------------------------------------------


def run_json(args: argparse.Namespace, entry: Entry, run_dir: Path) -> dict:
    argv = [str(args.cli), "--json", *solver_args(args.solver), *flag_args(entry),
            *(["-t", str(args.threads)] if args.threads is not None else []), str(REPO / entry.rel)]
    try:
        with search_env(entry, run_dir) as env:
            proc = subprocess.run(argv, capture_output=True, text=True, timeout=args.timeout, check=False,
                                  env=env)
    except subprocess.TimeoutExpired:
        return {"error": f"timeout after {args.timeout:.0f}s"}
    lines = [ln for ln in proc.stdout.splitlines() if ln.startswith("{")]
    if proc.returncode != 0 or not lines:
        tail = (proc.stderr or proc.stdout).strip().splitlines()[-3:]
        return {"error": f"exit {proc.returncode}: " + " | ".join(tail)}
    return json.loads(lines[-1])


def one_thread_only(entry: Entry, args: argparse.Namespace) -> bool:
    """A sliding puzzle with several starts (O): a shorter path to a picture
    already counted counts again, so on several threads its counts depend
    on the order the starts come in. Its answer is one thread's."""
    return "O" in entry.flags and args.threads != 1


def cmd_summary(args: argparse.Namespace, run_dir: Path) -> int:
    tiers = solver_tiers(args.tier, args.solver)
    entries = select(load_corpus(), args)
    expected = read_baselines(tiers, args.solver, text=False)
    if not entries:
        print("nothing selected", file=sys.stderr)
        return 2
    if args.update:
        return update_summary(args, entries, run_dir)

    runnable = [e for e in entries if e.rel in expected and "skip" not in expected[e.rel]
                and not one_thread_only(e, args)]
    results: dict[str, dict] = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(run_json, args, e, run_dir): e for e in runnable}
        for n, fut in enumerate(concurrent.futures.as_completed(futures), 1):
            e = futures[fut]
            results[e.rel] = fut.result()
            print(f"[{n}/{len(runnable)}] {e.rel}: {results[e.rel].get('error', 'ok')}", file=sys.stderr)

    rows = []
    for e in sorted(entries, key=lambda x: x.rel):
        want, got = expected.get(e.rel), results.get(e.rel)
        if want is None:
            rows.append((e.rel, "NO BASELINE", "run with --update to record it"))
        elif "skip" in want:
            rows.append((e.rel, "SKIPPED", want["skip"]))
        elif one_thread_only(e, args):
            rows.append((e.rel, "ONE THREAD", "several starts: compared with --threads 1 only"))
        elif "error" in got:
            rows.append((e.rel, "FAILED", got["error"]))
        else:
            diffs = [f"{k}: {want.get(k)} != {got.get(k)}" for k in COMPARED if want.get(k) != got.get(k)]
            if diffs:
                rows.append((e.rel, "MISMATCH", "; ".join(diffs)))
            else:
                speed = ""
                if want.get("solvetime") and got.get("solvetime") is not None:
                    speed = f"time {got['solvetime']:.3f}s (recorded {want['solvetime']:.3f}s, another machine)"
                rows.append((e.rel, "ok", speed))
    not_run = ("SKIPPED", "ONE THREAD")
    for rel, status, detail in rows:
        if status != "ok" or args.verbose:
            print(f"{status:12} {rel}  {detail}")
    considered = [status for _, status, _ in rows if status not in not_run]
    differ = sum(1 for status in considered if status != "ok")
    missing = report_not_compared(expected, {e.rel for e in entries}, args)
    compared = sum(1 for status in considered if status != "NO BASELINE")
    threads = "default threads" if args.threads is None else f"{args.threads} thread(s)"
    print(f"\n{len(considered) - differ}/{len(considered)} match the recorded answers "
          f"({threads}; solver {args.solver})")
    for status, why in (("SKIPPED", "no answer recorded"),
                        ("ONE THREAD", "several starts: run --threads 1")):
        count = sum(1 for _, s, _ in rows if s == status)
        if count:
            print(f"{count} not compared: {why} ({status} above)")
    if missing:
        print(f"{missing} recorded and not compared (NOT COMPARED above, counted as failures)")
    in_baseline = sum(1 for rel in expected if not args.filter or args.filter in rel)
    print(compared_line(compared, in_baseline, differ, "recorded answers"))
    return 1 if differ or missing or compared == 0 else 0


def update_summary(args: argparse.Namespace, entries: list[Entry], run_dir: Path) -> int:
    """Record the CLI's answers for the selected entries (ShapeLogic's)."""
    bad = [e.rel for e in entries if one_thread_only(e, args)]
    if bad:
        print(f"{len(bad)} sliding puzzles with several starts are recorded on one thread: add "
              f"--threads 1 or --filter them out (first: {bad[0]})", file=sys.stderr)
        return 2
    before = read_baselines(solver_tiers(args.tier, args.solver), args.solver, text=False)
    by_tier: dict[str, dict] = {}
    failed = 0
    for e in entries:
        got = run_json(args, e, run_dir)
        if "error" in got:
            print(f"{'FAILED':12} {e.rel}  {got['error']} (not recorded)")
            failed += 1
            continue
        entry = {"flags": e.flags, **{k: got.get(k) for k in COMPARED}, "solvetime": got.get("solvetime"),
                 "from": "shapelogic"}
        old = before.get(e.rel, {})
        changed = [f"{k}: {old.get(k)} -> {entry[k]}" for k in COMPARED if old.get(k) != entry[k]]
        if not changed and old:
            entry["from"] = old.get("from", "shapelogic")
        print(f"{'recorded' if changed or not old else 'same':12} {e.rel}  {'; '.join(changed)}")
        by_tier.setdefault(e.tier, {})[e.rel] = entry
    for tier, data in by_tier.items():
        write_baseline(tier, args.solver, False, data)
    return 1 if failed else 0


# ---- text output --------------------------------------------------------------


def text_runs(e: Entry) -> list[tuple[str, list[str]]]:
    """(name, arguments) of the text runs of a puzzle, on one thread: the
    order in which solutions are printed is the order the search hands them
    over. "all" (every problem of the file) only on the fast and sliding
    tiers and not on stacking puzzles flagged L: some files hold further
    problems that run for minutes or hours."""
    one = ["-t", "1"]
    extra = flag_args(e)
    every = e.tier in ("fast", "sliding") or (e.tier == "stacking" and "L" not in e.flags)
    if "A" in e.flags:
        # assemble only: no take-apart on the grid (D: the derived one runs
        # in the summaries)
        take = []
        retake = []
    else:
        take = ["-d"]
        retake = [("retake-saved", [*one, "-x", *extra])]
    return [("solve", [*one, *take, "-s", *(["-p"] if take else []), *extra]),
            ("quiet", [*one, "-q", "-n", "-r", *take, *extra]),
            *([("all", [*one, "-q", "-o", "all", *take, *extra])] if every else []),
            *retake,
            ("comment", ["-a", "c"])]


def run_text(args: argparse.Namespace, e: Entry, run_args: list[str], run_dir: Path) -> dict:
    """One run: its stdout and stderr with the solve time and the file's
    path blanked, and its exit code."""
    puzzle = REPO / e.rel
    argv = [str(args.cli), *run_args, *solver_args(args.solver), str(puzzle)]
    try:
        with search_env(e, run_dir) as env:
            proc = subprocess.run(argv, capture_output=True, timeout=args.timeout, check=False, env=env)
    except subprocess.TimeoutExpired:
        return {"error": f"timeout after {args.timeout:.0f}s"}

    def clean(b: bytes) -> bytes:
        return TEXT_TIME_RE.sub(b"in T seconds", b).replace(str(puzzle).encode(), b"<file>")

    return {"exit": proc.returncode, "out": clean(proc.stdout), "err": clean(proc.stderr)}


def text_digest(r: dict) -> dict:
    if "error" in r:
        return {"error": r["error"]}
    return {"exit": r["exit"], "out": hashlib.sha256(r["out"]).hexdigest(),
            "err": hashlib.sha256(r["err"]).hexdigest(), "lines": r["out"].count(b"\n")}


def text_name(e: Entry, run: str, solver: str) -> str:
    return f"{e.rel}{'' if solver == 'classic' else '.' + solver}.{run}"


def first_text_difference(old: bytes, new: bytes) -> str:
    a, b = old.split(b"\n"), new.split(b"\n")
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return f"line {i + 1}: {x[:100]!r} != {y[:100]!r}"
    return f"{len(a)} lines != {len(b)} lines"


def cmd_text(args: argparse.Namespace, run_dir: Path) -> int:
    tiers = solver_tiers(args.tier, args.solver)
    entries = select(load_corpus(), args)
    if not entries:
        print("nothing selected", file=sys.stderr)
        return 2
    expected = read_baselines(tiers, args.solver, text=True)
    jobs = [(e, name, a) for e in entries for name, a in text_runs(e)]

    def one(job):
        e, name, a = job
        return job, run_text(args, e, a, run_dir)

    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for n, (job, r) in enumerate(pool.map(one, jobs), 1):
            results.append((job, r))
            if args.verbose or "error" in r:
                print(f"[{n}/{len(jobs)}] {job[0].rel} {job[1]}: {r.get('error', 'ok')}", file=sys.stderr)

    if args.update:
        by_tier: dict[str, dict] = {}
        failed = 0
        for (e, name, _), r in results:
            if "error" in r:
                print(f"{'FAILED':12} {e.rel} {name}  {r['error']} (not recorded)")
                failed += 1
                continue
            d = text_digest(r)
            old = expected.get(e.rel, {}).get(name)
            if old != d:
                print(f"{'recorded':12} {e.rel} {name}  {d['lines']} lines, exit {d['exit']}")
            by_tier.setdefault(e.tier, {}).setdefault(e.rel, {})[name] = d
            base = RECORDED_TEXT / text_name(e, name, args.solver)
            base.parent.mkdir(parents=True, exist_ok=True)
            base.with_name(base.name + ".out").write_bytes(r["out"])
            base.with_name(base.name + ".err").write_bytes(r["err"])
        for tier, data in by_tier.items():
            # a puzzle's runs are recorded together: a run no longer made goes
            write_baseline(tier, args.solver, True, data)
        return 1 if failed else 0

    rows = []
    for (e, name, _), r in sorted(results, key=lambda x: (x[0][0].rel, x[0][1])):
        want = expected.get(e.rel, {}).get(name)
        got = text_digest(r)
        if want is None:
            rows.append((e.rel, name, "NO BASELINE", "run with --update to record it"))
            continue
        if "error" in got:
            rows.append((e.rel, name, "FAILED", got["error"]))
            continue
        diffs = [k for k in ("exit", "out", "err") if want.get(k) != got.get(k)]
        if not diffs:
            rows.append((e.rel, name, "ok", f"{got['lines']} lines"))
            continue
        base = run_dir / "text" / text_name(e, name, args.solver)
        base.parent.mkdir(parents=True, exist_ok=True)
        detail = ", ".join(diffs)
        if "exit" in diffs:
            detail += f" (exit {want.get('exit')} != {got['exit']})"
        for k in ("out", "err"):
            base.with_name(base.name + f".{k}").write_bytes(r[k])
            recorded = RECORDED_TEXT / (text_name(e, name, args.solver) + f".{k}")
            if k in diffs and recorded.exists():
                detail += f"; {k} {first_text_difference(recorded.read_bytes(), r[k])}"
        detail += f"; this run's text: {base.relative_to(REPO)}.out"
        rows.append((e.rel, name, "MISMATCH", detail))
    made = {(e.rel, name) for e, name, _ in jobs}
    selected = {e.rel for e in entries}
    not_made = sorted((rel, name) for rel, runs in expected.items() if rel in selected
                      for name in runs if (rel, name) not in made)
    for rel, name, status, detail in rows:
        if status != "ok" or args.verbose:
            print(f"{status:12} {rel} {name}  {detail}")
    for rel, name in not_made:
        print(f"{'NOT RUN':12} {rel} {name}  recorded, but no longer made")
    differ = sum(1 for *_, status, _ in rows if status != "ok")
    missing = report_not_compared(expected, selected, args)
    print(f"\n{len(rows) - differ}/{len(rows)} text runs match the recorded ones "
          f"({len(entries)} puzzles; solver {args.solver})")
    if not_made or missing:
        print(f"{len(not_made)} runs and {missing} puzzles recorded and not run (counted as failures)")
    in_baseline = sum(1 for rel in expected if not args.filter or args.filter in rel)
    ran = {rel for rel, _, status, _ in rows if rel in expected}
    print(compared_line(len(ran), in_baseline, differ, "recorded puzzles"))
    return 1 if differ or not_made or missing or not rows else 0


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--cli", type=Path, default=REPO / "build" / "release" / "cli" / "shapelogic_cli")
    p.add_argument("--work", type=Path, help="keep what the run writes in this folder (default: a folder "
                                             "of its own under build/regression, removed when the run passes)")
    sub = p.add_subparsers(dest="command", required=True)
    for name, jobs in (("summary", 1), ("text", os.cpu_count() or 4)):
        s = sub.add_parser(name, help=f"compare the {name}s with the recorded ones")
        s.add_argument("--tier", nargs="+", default=["fast"], choices=[*TIERS, "all"],
                       help="all = fast, full and slow; sliding and stacking are run by name")
        s.add_argument("--solver", choices=SOLVERS, default="classic")
        s.add_argument("--filter", help="only puzzles whose path contains this text")
        s.add_argument("--jobs", "-j", type=int, default=jobs, help="puzzles (runs) at once")
        s.add_argument("--timeout", type=float, default=900.0, help="seconds per run")
        s.add_argument("--verbose", "-v", action="store_true")
        s.add_argument("--update", action="store_true",
                       help="record the CLI's answers for the selection instead (an answer changed on purpose)")
        if name == "summary":
            s.add_argument("--threads", "-t", type=int, metavar="N", help="the CLI's -t (default: its own)")
    args = p.parse_args()
    if not args.cli.exists():
        print(f"shapelogic_cli not found at {args.cli} (just build release)", file=sys.stderr)
        return 2
    run_dir = args.work or RUN_ROOT / f"run-{os.getpid()}"
    run_dir.mkdir(parents=True, exist_ok=True)
    code = (cmd_summary if args.command == "summary" else cmd_text)(args, run_dir)
    if args.work is None:
        if code == 0:
            shutil.rmtree(run_dir, ignore_errors=True)
        elif any(run_dir.iterdir()):
            print(f"what this run wrote is kept in {run_dir.relative_to(REPO)}", file=sys.stderr)
        else:
            run_dir.rmdir()
    return code


if __name__ == "__main__":
    sys.exit(main())

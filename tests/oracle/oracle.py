#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare the new `bt` with the original BurrTools `burrTxt`.

A ported component counts as done only when the two agree on the corpus
(docs/decisions/0007-port-against-the-original.md). Two levels:

  summary  `--json` totals: assemblies, solutions, best level and moves.
           `baseline` records the original's answers in baseline/<tier>.json
           (committed); `compare` runs the new bt and diffs.
  files    every solution and its disassembly, from the solved puzzle file the
           original's burrTxt2 writes (`baseline-files`, under work/old, not
           committed) and the one the new core writes the same way
           (`compare-files`, with `bt --save`): both are read with the new
           core (bt_solutions) and compared solution by solution.
  roundtrip  (stage 2) every corpus file, plus the files that are not solved
           yet and the solved files under work/old, is loaded and saved by the
           original and by the new core; the saves must be the same XML
           (decision 0006).

Run through `just oracle` / `just oracle-baseline`; see README.md.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import contextlib
import glob
import gzip
import hashlib
import json
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile
import time
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
CORPUS = HERE / "corpus.txt"
BASELINE_DIR = HERE / "baseline"
WORK_DIR = HERE / "work"

# Fields that must match exactly. solvetime is reported, never compared.
COMPARED = ("assemblies", "solutions", "dotlevel", "level", "totalmoves", "moves", "rotations")


@dataclass(frozen=True)
class Entry:
    tier: str
    flags: str
    rel: str  # path relative to the old repo, or "alpha:" and relative to this one


# A corpus path "alpha:<path>" is a file of this repository: the sliding
# puzzles the original's code made (`oracle.py sliding-puzzles`).
ALPHA_PREFIX = "alpha:"


def entry_path(old_repo: Path, rel: str) -> Path:
    if rel.startswith(ALPHA_PREFIX):
        return REPO / rel[len(ALPHA_PREFIX):]
    return old_repo / rel


def work_name(rel: str) -> str:
    """Where a corpus file's copies go under work/: its path, this repo's
    files under alpha/."""
    return rel.replace(ALPHA_PREFIX, "alpha/", 1)


def load_corpus(old_repo: Path) -> list[Entry]:
    entries: list[Entry] = []
    slow: set[str] = set()
    for line in CORPUS.read_text().splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        tier, flags, pattern = line.split(None, 2)
        alpha = pattern.startswith(ALPHA_PREFIX)
        root = REPO if alpha else old_repo
        matches = sorted(glob.glob(str(root / pattern.removeprefix(ALPHA_PREFIX)), recursive=True))
        if not matches:
            print(f"warning: corpus pattern matched nothing: {pattern}", file=sys.stderr)
        for m in matches:
            rel = (ALPHA_PREFIX if alpha else "") + str(Path(m).relative_to(root))
            if tier == "slow":
                slow.add(rel)
            entries.append(Entry(tier, flags, rel))
    # A puzzle listed as slow is not also part of the full tier.
    return [e for e in entries if not (e.tier == "full" and e.rel in slow)]


# The sliding solvers: their own tier (`--tier sliding`); "classic", the
# default, runs both CLIs without --solver, which for a sliding puzzle is
# the full search.
SLIDING_SOLVERS = ("fast", "deep", "full")
# The stacking tier's other solver (`--tier stacking --solver panex`);
# "classic", the default, runs the Stacking Solver.
STACKING_SOLVERS = ("panex",)


@contextlib.contextmanager
def panex_env(entry: "Entry"):
    """The environment of a run: for a stacking puzzle, a Panex folder of
    the run's own (BURRTOOLS_PANEX_DIR), removed afterwards, so neither the
    user's cache nor another run's saved search is ever touched or carried
    on. None for other puzzles (the inherited environment)."""
    if entry.tier != "stacking":
        yield None
        return
    root = WORK_DIR / "panex-tmp"
    root.mkdir(parents=True, exist_ok=True)
    folder = Path(tempfile.mkdtemp(dir=root))
    try:
        yield {**os.environ, "BURRTOOLS_PANEX_DIR": str(folder)}
    finally:
        shutil.rmtree(folder, ignore_errors=True)


def select(entries: list[Entry], tiers: list[str], name_filter: str | None,
           solver: str = "classic") -> list[Entry]:
    """The entries of the tiers, those the solver runs on: the brick solvers
    on the brick tiers, the sliding ones on the sliding tier, the stacking
    ones on the stacking tier; a sliding entry flagged L (a full search would
    not end) not with the full search, a stacking one flagged Q (no Panex
    columns) not with the Panex Solver."""
    wanted = set(tiers)
    if "all" in wanted:
        wanted = (wanted - {"all"}) | {"fast", "full", "slow"}
    out = []
    for e in entries:
        if e.tier not in wanted or (name_filter and name_filter not in e.rel):
            continue
        if e.tier == "sliding":
            if solver in ("crowell", "bt2", *STACKING_SOLVERS):
                continue
            if "L" in e.flags and solver in ("classic", "full"):
                continue
        elif e.tier == "stacking":
            # the Stacking Solver (classic) on all, the Panex Solver on the
            # puzzles it takes (not Q)
            if solver not in ("classic", *STACKING_SOLVERS):
                continue
            if "Q" in e.flags and solver == "panex":
                continue
        elif solver in SLIDING_SOLVERS or solver in STACKING_SOLVERS:
            continue
        out.append(e)
    return out


def solver_args(solver: str) -> list[str]:
    """The --solver argument of both CLIs; none for the default (classic)."""
    return [] if solver == "classic" else ["--solver", solver]


def flag_args(entry: Entry) -> list[str]:
    """The options a corpus entry's flags stand for: R -R; N --nested-slides,
    P --partial-nested (sliding)."""
    args = []
    if "R" in entry.flags:
        args.append("-R")
    if "N" in entry.flags:
        args.append("--nested-slides")
    if "P" in entry.flags:
        args.append("--partial-nested")
    return args


def cli_args(entry: Entry, threads: int | None = None, solver: str = "classic") -> list[str]:
    args = ["--json", *solver_args(solver), *flag_args(entry)]
    if threads is not None:
        args += ["-t", str(threads)]
    return args


# Exit code of the new bt for a problem that needs a part not ported yet
# (none since stage 3b). compare reports those as UNPORTED.
EXIT_NOT_PORTED = 3


SUMMARY_RE = re.compile(r"(\d+) assemblies and (\d+) solutions found with \d+ iterations in ([\d.]+) seconds")


def run_json(binary: Path, puzzle: Path, entry: Entry, timeout: float, old: bool,
             threads: int | None = None, solver: str = "classic") -> dict:
    """Run a burrTxt-compatible binary and return its result as a --json-style object.

    For assemble-only ("A") puzzles the original burrTxt is run with -q and
    its text summary parsed, because its --json aborts on grids without
    take-apart (see corpus.txt)."""
    text_mode = old and "A" in entry.flags
    # The original's sliding answers on several threads depend on the order
    # the starts come in (a shorter path to a picture already counted counts
    # again): its baseline is one thread's.
    if old and entry.tier == "sliding":
        threads = 1
    argv = ([str(binary), "-q", *solver_args(solver), str(puzzle)] if text_mode
            else [str(binary), *cli_args(entry, threads, solver), str(puzzle)])
    start = time.monotonic()
    try:
        with panex_env(entry) as env:
            proc = subprocess.run(argv, capture_output=True, text=True, timeout=timeout, check=False,
                                  env=env)
    except subprocess.TimeoutExpired:
        return {"error": f"timeout after {timeout:.0f}s"}
    wall = round(time.monotonic() - start, 3)
    if text_mode:
        m = SUMMARY_RE.search(proc.stdout)
        if proc.returncode != 0 or not m:
            return {"error": f"exit {proc.returncode}: no summary line"}
        return {"assemblies": int(m.group(1)), "solutions": int(m.group(2)), "dotlevel": None,
                "level": None, "totalmoves": None, "solvetime": float(m.group(3)), "wall": wall}
    lines = [ln for ln in proc.stdout.splitlines() if ln.startswith("{")]
    if not old and proc.returncode == EXIT_NOT_PORTED:
        lines_err = (proc.stderr or "").strip().splitlines()
        return {"unported": lines_err[-1] if lines_err else "not ported yet"}
    if proc.returncode != 0 or not lines:
        tail = (proc.stderr or proc.stdout).strip().splitlines()[-3:]
        return {"error": f"exit {proc.returncode}: " + " | ".join(tail)}
    result = json.loads(lines[-1])
    result["wall"] = wall
    return result


def baseline_path(tier: str, solver: str = "classic") -> Path:
    """baseline/<tier>.json for the default solver, baseline/<tier>-<solver>.json for another."""
    return BASELINE_DIR / (f"{tier}.json" if solver == "classic" else f"{tier}-{solver}.json")


def run_many(binary: Path, old_repo: Path, entries: list[Entry], jobs: int, timeout: float, old: bool,
             threads: int | None = None, solver: str = "classic") -> dict[str, dict]:
    results: dict[str, dict] = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = {pool.submit(run_json, binary, entry_path(old_repo, e.rel), e, timeout, old, threads,
                               solver): e
                   for e in entries}
        for n, fut in enumerate(concurrent.futures.as_completed(futures), 1):
            e = futures[fut]
            results[e.rel] = fut.result()
            status = results[e.rel].get("error", "unported" if "unported" in results[e.rel] else "ok")
            print(f"[{n}/{len(entries)}] {e.rel}: {status}", file=sys.stderr)
    return results


def cmd_baseline(args: argparse.Namespace) -> int:
    old_bin = args.old_repo / "build" / "burrTxt"
    if not old_bin.exists():
        print(f"original burrTxt not found at {old_bin}; build the old repo first (just build)", file=sys.stderr)
        return 2
    entries = select(load_corpus(args.old_repo), args.tier, args.filter, args.solver)
    results = run_many(old_bin, args.old_repo, entries, args.jobs, args.timeout, old=True, solver=args.solver)
    BASELINE_DIR.mkdir(exist_ok=True)
    by_tier: dict[str, dict] = {}
    for e in entries:
        by_tier.setdefault(e.tier, {})[e.rel] = {"flags": e.flags, **results[e.rel]}
    for tier, data in by_tier.items():
        path = baseline_path(tier, args.solver)
        merged = json.loads(path.read_text()) if path.exists() and args.filter else {}
        merged.update(data)
        path.write_text(json.dumps(dict(sorted(merged.items())), indent=1) + "\n")
        errors = sum(1 for v in data.values() if "error" in v)
        print(f"wrote {path.relative_to(HERE.parent.parent)}: {len(data)} puzzles, {errors} errors")
    return 0


def cmd_compare(args: argparse.Namespace) -> int:
    if not args.new_bt.exists():
        print(f"new bt not found at {args.new_bt}", file=sys.stderr)
        return 2
    entries = select(load_corpus(args.old_repo), args.tier, args.filter, args.solver)
    expected: dict[str, dict] = {}
    for tier in {e.tier for e in entries}:
        path = baseline_path(tier, args.solver)
        if not path.exists():
            print(f"no baseline for tier '{tier}' ({args.solver}): run "
                  f"`just oracle-baseline --tier {tier} --solver {args.solver}`", file=sys.stderr)
            return 2
        expected.update(json.loads(path.read_text()))
    fields = COMPARED if args.fields is None else tuple(args.fields)
    unknown = [f for f in fields if f not in COMPARED]
    if unknown:
        print(f"unknown field(s): {', '.join(unknown)}; choose from {', '.join(COMPARED)}", file=sys.stderr)
        return 2
    # what the original could not answer is not run (Crowell's take-apart of
    # some puzzles outlasts the baseline's time limit); a sliding puzzle
    # with several starts (O) only on one thread, as its baseline was made
    order = [e for e in entries if "O" in e.flags and args.threads != 1]
    runnable = [e for e in entries if "error" not in expected.get(e.rel, {}) and e not in order]
    results = run_many(args.new_bt, args.old_repo, runnable, args.jobs, args.timeout, old=False,
                       threads=args.threads, solver=args.solver)
    for e in entries:
        results.setdefault(e.rel, {"skipped": True})

    failures = 0
    unported = 0
    rows = []
    for e in sorted(entries, key=lambda x: x.rel):
        want, got = expected.get(e.rel), results[e.rel]
        if e in order:
            rows.append((e.rel, "ONE THREAD", "several starts: compared with --threads 1 only"))
            continue
        if want is None:
            rows.append((e.rel, "NO BASELINE", ""))
            failures += 1
        elif "error" in want:
            rows.append((e.rel, "OLD FAILED", want["error"]))
        elif "unported" in got:
            rows.append((e.rel, "UNPORTED", got["unported"]))
            unported += 1
            if not args.allow_unported:
                failures += 1
        elif "error" in got:
            rows.append((e.rel, "NEW FAILED", got["error"]))
            failures += 1
        else:
            diffs = [f"{k}: {want.get(k)} != {got.get(k)}" for k in fields if want.get(k) != got.get(k)]
            if diffs:
                rows.append((e.rel, "MISMATCH", "; ".join(diffs)))
                failures += 1
            else:
                speed = ""
                if want.get("solvetime") and got.get("solvetime") is not None:
                    speed = f"time {got['solvetime']:.3f}s vs {want['solvetime']:.3f}s (baseline machine)"
                rows.append((e.rel, "ok", speed))
    for rel, status, detail in rows:
        if status != "ok" or args.verbose:
            print(f"{status:12} {rel}  {detail}")
    oldFailed = sum(1 for _, status, _ in rows if status == "OLD FAILED")
    oneThread = sum(1 for _, status, _ in rows if status == "ONE THREAD")
    compared = len(rows) - oldFailed - oneThread - (unported if args.allow_unported else 0)
    what = "all fields" if args.fields is None else ", ".join(fields)
    threads = "default threads" if args.threads is None else f"{args.threads} thread(s)"
    print(f"\n{compared - failures}/{compared} match the original ({what}; {threads}; solver {args.solver})"
          + ("" if failures == 0 else f", {failures} do not"))
    if oldFailed:
        print(f"{oldFailed} not compared: the original gave no answer (OLD FAILED above)")
    if oneThread:
        print(f"{oneThread} sliding puzzles with several starts not compared here: their counts "
              "depend on the order of the starts (ONE THREAD above; run --threads 1)")
    if unported:
        print(f"{unported} need a part that is not ported yet"
              + (" (not counted: --allow-unported)" if args.allow_unported else " (counted as failures)"))
    return 1 if failures else 0


def files_dir(side: str, solver: str) -> Path:
    """work/old or work/new for the default solver, work/<side>-<solver> for another."""
    return WORK_DIR / (side if solver == "classic" else f"{side}-{solver}")


def slide_solve_flags(entry: Entry, solver: str) -> list[str]:
    """slide_solve_old's flags for a sliding entry and solver (the solve
    thread's PAR_ flags; the default is the full search, as bt's)."""
    flags = {"fast": [], "deep": ["--deep"]}.get(solver, ["--full"])
    if "N" in entry.flags:
        flags.append("--nested")
    if "P" in entry.flags:
        flags.append("--partial")
    return flags


def cmd_baseline_files(args: argparse.Namespace) -> int:
    """Solve each puzzle with the original burrTxt2, keeping its solved file;
    sliding puzzles with the original's solve thread (slide_solve_old.cpp),
    as burrTxt2 refuses -d on their grid, and stacking ones too
    (stack_solve_old.cpp: burrTxt2 has no stacking solver)."""
    entries = select(load_corpus(args.old_repo), args.tier, args.filter, args.solver)
    # a stacking puzzle whose setup is not ready (I): the GUI refused to solve it
    entries = [e for e in entries if "I" not in e.flags]
    old_bin = args.old_repo / "build" / "burrTxt2"
    if any(e.tier not in ("sliding", "stacking") for e in entries) and not old_bin.exists():
        print(f"original burrTxt2 not found at {old_bin}", file=sys.stderr)
        return 2
    slide_tool = WORK_DIR / "slide_solve_old"
    if any(e.tier == "sliding" for e in entries):
        WORK_DIR.mkdir(exist_ok=True)
        if not build_against_old(args.old_repo, HERE / "slide_solve_old.cpp", slide_tool):
            return 2
    stack_tool = WORK_DIR / "stack_solve_old"
    if any(e.tier == "stacking" for e in entries):
        WORK_DIR.mkdir(exist_ok=True)
        if not build_against_old(args.old_repo, HERE / "stack_solve_old.cpp", stack_tool):
            return 2
    out_root = files_dir("old", args.solver)
    for n, e in enumerate(entries, 1):
        dst = out_root / work_name(e.rel)
        dst.parent.mkdir(parents=True, exist_ok=True)
        # burrTxt2 writes <file>ttt beside its input
        shutil.copyfile(entry_path(args.old_repo, e.rel), dst)
        if e.tier == "sliding":
            # every solution kept, one thread: one order of the starts
            argv = [str(slide_tool), str(dst), str(dst) + "ttt", "-d", "--limit", "0",
                    *slide_solve_flags(e, args.solver)]
        elif e.tier == "stacking":
            argv = [str(stack_tool), str(dst), str(dst) + "ttt", "-d",
                    *(["--panex"] if args.solver == "panex" else [])]
        else:
            # -R starts over (a file may hold solutions or a paused search);
            # "A" grids have no take-apart, -d makes the original abort on them
            flags = (["-R"] + ([] if "A" in e.flags else ["-d"]) + (["--rotations"] if "R" in e.flags else [])
                     + solver_args(args.solver))
            argv = [str(old_bin), *flags, str(dst)]
        try:
            with panex_env(e) as env:
                proc = subprocess.run(argv, capture_output=True, text=True,
                                      timeout=args.timeout, check=False, env=env)
            status = "ok" if proc.returncode == 0 else f"exit {proc.returncode}"
        except subprocess.TimeoutExpired:
            status = "timeout"
        print(f"[{n}/{len(entries)}] {e.rel}: {status}", file=sys.stderr)
    print(f"solved files in {out_root}")
    return 0


# burrTxt2 keeps the best ten solutions by moves (the solve thread's default)
SOLUTION_LIMIT = 10


def read_solutions(tool: Path, path: Path, timeout: float) -> dict:
    proc = subprocess.run([str(tool), str(path)], capture_output=True, text=True, timeout=timeout, check=False)
    if proc.returncode != 0:
        raise RuntimeError((proc.stderr or proc.stdout).strip()[-200:] or f"exit {proc.returncode}")
    return json.loads(proc.stdout)


def compare_solved(old: dict, new: dict, assemble_only: bool) -> str:
    """Empty when the new solved problem 0 matches the original's, else what differs.

    The original keeps the ten best solutions by moves; which of several with
    the same moves it keeps depends on the order its threads took them apart,
    so the new file keeps every solution, sorted the same way: the original's
    moves must be the top of the new list, and each solution the original
    kept must be in the new file with the very same assembly, separation
    tree, level text and move counts."""
    po, pn = old["problems"][0], new["problems"][0]
    for key in ("state", "assemblies", "solutions"):
        if po.get(key) != pn.get(key):
            return f"{key}: {po.get(key)} != {pn.get(key)}"
    if assemble_only:
        return ""
    olds, news = po["saved"], pn["saved"]
    if len(olds) != min(len(news), SOLUTION_LIMIT):
        return f"saved solutions: {len(olds)} != min({len(news)}, {SOLUTION_LIMIT})"
    om = [s.get("moves") for s in olds]
    nm = [s.get("moves") for s in news[:len(olds)]]
    if om != nm:
        return f"moves of the saved solutions: {om} != {nm}"
    by_assembly: dict[str, list[dict]] = {}
    for s in news:
        by_assembly.setdefault(s["assembly"], []).append(s)
    keys = ("tree", "level", "moves", "rotations")
    for i, s in enumerate(olds):
        candidates = by_assembly.get(s["assembly"])
        if not candidates:
            return f"solution {i}: its assembly is not among the new solutions"
        if not any(all(c.get(k) == s.get(k) for k in keys) for c in candidates):
            c = candidates[0]
            diff = [k for k in keys if c.get(k) != s.get(k)]
            return f"solution {i} ({s.get('level')}): differs in {', '.join(diff)}"
    return ""


def compare_solved_exactly(old: dict, new: dict) -> str:
    """Empty when the new solved problem 0 is the original's, solution for
    solution in the same order (a sliding solve on one thread: every start
    searched in the assembler's order, the paths and their every state)."""
    po, pn = old["problems"][0], new["problems"][0]
    for key in ("state", "assemblies", "solutions"):
        if po.get(key) != pn.get(key):
            return f"{key}: {po.get(key)} != {pn.get(key)}"
    olds, news = po["saved"], pn["saved"]
    if len(olds) != len(news):
        return f"saved solutions: {len(olds)} != {len(news)}"
    for i, (a, b) in enumerate(zip(olds, news)):
        diff = [k for k in sorted(set(a) | set(b)) if a.get(k) != b.get(k)]
        if diff:
            return f"solution {i} ({a.get('level')}): differs in {', '.join(diff)}"
    return ""


def compare_files_one(e: Entry, args: argparse.Namespace) -> dict:
    old_file = files_dir("old", args.solver) / (work_name(e.rel) + "ttt")
    if not old_file.exists():
        return {"status": "NO BASELINE", "detail": f"no {old_file.relative_to(HERE)}: run baseline-files"}
    new_in = files_dir("new", args.solver) / work_name(e.rel)
    new_in.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(entry_path(args.old_repo, e.rel), new_in)
    new_file = new_in.with_name(new_in.name + "ttt")
    if e.tier == "sliding":
        # the solve thread's run of slide_solve_old: one thread, every
        # solution kept, the full search unless the solver says
        flags = ["-q", "--restart", "-t", "1", "-d", "--limit", "0", *solver_args(args.solver),
                 *flag_args(e)]
    elif e.tier == "stacking":
        # the solve thread's stacking run (stack_solve_old)
        flags = ["-q", "--restart", "-d", *solver_args(args.solver)]
    else:
        # every solution is kept when taking apart (see compare_solved); saved
        # assemblies are not compared and keep the solve thread's limit
        flags = (["-q", "--restart"] + ([] if "A" in e.flags else ["-d", "--limit", "0"])
                 + (["-R"] if "R" in e.flags else []) + solver_args(args.solver))
    with panex_env(e) as env:
        err = run_tool([str(args.new_bt), "--save", str(new_file), *flags, str(new_in)], args.timeout,
                       env)
    if err:
        return {"status": "NEW FAILED", "detail": err}
    try:
        old = read_solutions(args.dump_tool, old_file, args.timeout)
        new = read_solutions(args.dump_tool, new_file, args.timeout)
    except (RuntimeError, subprocess.TimeoutExpired, json.JSONDecodeError) as exc:
        return {"status": "MISMATCH", "detail": f"cannot read: {exc}"}
    diff = (compare_solved_exactly(old, new) if e.tier in ("sliding", "stacking")
            else compare_solved(old, new, "A" in e.flags))
    if diff:
        return {"status": "MISMATCH", "detail": diff}
    po = old["problems"][0]
    return {"status": "ok", "detail": f"{po.get('assemblies')} assemblies, {po.get('solutions')} solutions, "
                                      f"{len(po['saved'])} saved"}


def cmd_compare_files(args: argparse.Namespace) -> int:
    """Solve each puzzle with the new core as burrTxt2 did and compare the solved files."""
    for tool in (args.new_bt, args.dump_tool):
        if not tool.exists():
            print(f"{tool} not found: build the release preset", file=sys.stderr)
            return 2
    entries = select(load_corpus(args.old_repo), args.tier, args.filter, args.solver)
    entries = [e for e in entries if "I" not in e.flags]
    results: dict[str, dict] = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(compare_files_one, e, args): e for e in entries}
        for n, fut in enumerate(concurrent.futures.as_completed(futures), 1):
            e = futures[fut]
            results[e.rel] = fut.result()
            print(f"[{n}/{len(entries)}] {e.rel}: {results[e.rel]['status']}", file=sys.stderr)
    failures = 0
    for rel in sorted(results):
        r = results[rel]
        if r["status"] != "ok":
            failures += 1
        if r["status"] != "ok" or args.verbose:
            print(f"{r['status']:12} {rel}  {r.get('detail', '')}")
    print(f"\n{len(results) - failures}/{len(results)} solved files match the original's "
          f"(solver {args.solver})" + ("" if failures == 0 else f", {failures} do not"))
    return 1 if failures else 0


# ---- text output (stage 8) ------------------------------------------------
#
# burrTxt's text output against bt's, byte for byte: stdout, stderr and the
# exit code of a set of runs per puzzle, on one thread (the order in which
# solutions are printed is the order the search finds them). The solve time
# is blanked. `baseline-text` records the original's runs once (hashes in
# baseline/text-<tier>[-<solver>].json, committed; the text itself under
# work/text-old*, for showing a difference); `compare-text` runs bt.

TEXT_TIME_RE = re.compile(rb"in [0-9]+\.[0-9]{3} seconds")
# assembler 1 says on stderr when it narrows piece ranges; bt does not
TEXT_OLD_STDERR_SKIP = re.compile(rb"^range optimisation used min \d+, max \d+\n", re.M)


def text_runs(e: Entry) -> list[tuple[str, list[str], str]]:
    """(name, arguments, input) of the text runs of a puzzle. The input is
    "corpus" (the corpus file) or "solved" (burrTxt2's solved file under
    work/old, see baseline-files: up to ten saved solutions with their
    take-aparts, for -x and -a)."""
    one = ["-t", "1"]
    # -o all only on the fast and sliding tiers: some TIC files hold more
    # problems that take far longer than the first
    every = e.tier in ("fast", "sliding")
    if e.tier == "sliding":
        # the slide search's options on every run that searches
        nest = flag_args(e)
        return [("solve", [*one, "-dsp", *nest], "corpus"),
                ("quiet", [*one, "-q", "-n", "-r", "-d", *nest], "corpus"),
                ("all", [*one, "-q", "-o", "all", "-d", *nest], "corpus"),
                ("retake", [*one, "-x", "-sp", *nest], "solved"),
                ("retake-saved", [*one, "-x", *nest], "corpus"),
                ("ask", ["-a", "s1"], "solved"),
                ("comment", ["-a", "c"], "corpus")]
    if e.tier == "stacking":
        # -o all not on puzzles whose further problems run for hours (L);
        # no solved file for a setup that is not ready (I)
        solved = [] if "I" in e.flags else [("retake", [*one, "-x", "-sp"], "solved"),
                                             ("ask", ["-a", "s1"], "solved")]
        return [("solve", [*one, "-dsp"], "corpus"),
                ("quiet", [*one, "-q", "-n", "-r", "-d"], "corpus"),
                *([("all", [*one, "-q", "-o", "all", "-d"], "corpus")] if "L" not in e.flags else []),
                *solved,
                ("retake-saved", [*one, "-x"], "corpus"),
                ("comment", ["-a", "c"], "corpus")]
    if "A" in e.flags:
        # no -d: the original's take-apart aborts on these grids
        return [("solve", [*one, "-s"], "corpus"),
                ("quiet", [*one, "-q", "-n", "-r"], "corpus"),
                *([("all", [*one, "-q", "-o", "all"], "corpus")] if every else []),
                ("ask", ["-a", "s1"], "solved"),
                ("comment", ["-a", "c"], "corpus")]
    rot = ["-R"] if "R" in e.flags else []
    return [("solve", [*one, "-dsp", *rot], "corpus"),
            ("quiet", [*one, "-q", "-n", "-r", "-d", *rot], "corpus"),
            *([("all", [*one, "-q", "-o", "all", "-d", *rot], "corpus")] if every else []),
            ("retake", [*one, "-x", "-sp", *rot], "solved"),
            ("retake-saved", [*one, "-x", *rot], "corpus"),
            ("ask", ["-a", "s1"], "solved"),
            ("comment", ["-a", "c"], "corpus")]


def text_input(e: Entry, where: str, old_repo: Path, solver: str = "classic") -> Path:
    if where == "corpus":
        return entry_path(old_repo, e.rel)
    # a sliding or stacking puzzle's solved file is its solver's
    # (slide_solve_old, stack_solve_old)
    side = solver if e.tier in ("sliding", "stacking") else "classic"
    return files_dir("old", side) / (work_name(e.rel) + "ttt")


def text_dir(side: str, solver: str) -> Path:
    return WORK_DIR / (f"text-{side}" if solver == "classic" else f"text-{side}-{solver}")


def text_baseline_path(tier: str, solver: str) -> Path:
    return BASELINE_DIR / (f"text-{tier}.json" if solver == "classic" else f"text-{tier}-{solver}.json")


def run_text(binary: Path, args: list[str], puzzle: Path, timeout: float, old: bool,
             entry: "Entry | None" = None) -> dict:
    """One run: its normalised stdout and stderr, and the exit code."""
    argv = [str(binary), *args, *([] if old else ["--original-drawings"]), str(puzzle)]
    try:
        with (panex_env(entry) if entry is not None else contextlib.nullcontext()) as env:
            proc = subprocess.run(argv, capture_output=True, timeout=timeout, check=False, env=env)
    except subprocess.TimeoutExpired:
        return {"error": f"timeout after {timeout:.0f}s"}
    out = TEXT_TIME_RE.sub(b"in T seconds", proc.stdout)
    err = TEXT_TIME_RE.sub(b"in T seconds", proc.stderr)
    if old:
        err = TEXT_OLD_STDERR_SKIP.sub(b"", err)
    # a path in a message is the run's own
    out = out.replace(str(puzzle).encode(), b"<file>")
    err = err.replace(str(puzzle).encode(), b"<file>")
    return {"exit": proc.returncode, "out": out, "err": err}


def text_digest(r: dict) -> dict:
    """What is kept of a run. The original dies of a signal when it draws a
    take-apart with a piece that is not placed (-p with piece ranges: its
    print() transforms every piece); then what it wrote before is kept, and
    bt's output must begin with it."""
    if "error" in r:
        return {"error": r["error"]}
    if r["exit"] < 0:
        return {"crashed": -r["exit"], "out": hashlib.sha256(r["out"]).hexdigest(),
                "outBytes": len(r["out"])}
    return {"exit": r["exit"], "out": hashlib.sha256(r["out"]).hexdigest(),
            "err": hashlib.sha256(r["err"]).hexdigest(), "lines": r["out"].count(b"\n")}


def text_jobs(entries: list[Entry], old_repo: Path,
              solver: str = "classic") -> list[tuple[Entry, str, list[str], Path]]:
    jobs = []
    for e in entries:
        for name, args, where in text_runs(e):
            jobs.append((e, name, args, text_input(e, where, old_repo, solver)))
    return jobs


def cmd_baseline_text(args: argparse.Namespace) -> int:
    """Record the original burrTxt's text runs (hashes committed, text under work/)."""
    old_bin = args.old_repo / "build" / "burrTxt"
    if not old_bin.exists():
        print(f"original burrTxt not found at {old_bin}", file=sys.stderr)
        return 2
    entries = select(load_corpus(args.old_repo), args.tier, args.filter, args.solver)
    jobs = text_jobs(entries, args.old_repo, args.solver)
    missing = sorted({str(p) for _, _, _, p in jobs if not p.exists()})
    if missing:
        print(f"{len(missing)} solved files missing (first: {missing[0]}): "
              "run `oracle.py baseline-files --tier all` first", file=sys.stderr)
        return 2
    store = text_dir("old", args.solver)
    results: dict[str, dict] = {}

    def one(job):
        e, name, a, puzzle = job
        r = run_text(old_bin, [*a, *solver_args(args.solver)], puzzle, args.timeout, old=True, entry=e)
        if "error" not in r:
            base = store / f"{work_name(e.rel)}.{name}"
            base.parent.mkdir(parents=True, exist_ok=True)
            base.with_name(base.name + ".out").write_bytes(r["out"])
            base.with_name(base.name + ".err").write_bytes(r["err"])
        return job, text_digest(r)

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for n, (job, d) in enumerate(pool.map(one, jobs), 1):
            e, name = job[0], job[1]
            results.setdefault(e.rel, {"tier": e.tier})[name] = d
            if "error" in d or args.verbose:
                print(f"[{n}/{len(jobs)}] {e.rel} {name}: {d.get('error', 'ok')}", file=sys.stderr)
    BASELINE_DIR.mkdir(exist_ok=True)
    for tier in sorted({e.tier for e in entries}):
        path = text_baseline_path(tier, args.solver)
        data = {rel: {k: v for k, v in r.items() if k != "tier"} for rel, r in results.items()
                if r["tier"] == tier}
        merged = json.loads(path.read_text()) if path.exists() and args.filter else {}
        merged.update(data)
        path.write_text(json.dumps(dict(sorted(merged.items())), indent=1) + "\n")
        print(f"wrote {path.relative_to(HERE.parent.parent)}: {len(data)} puzzles")
    return 0


def first_text_difference(old: bytes, new: bytes) -> str:
    a, b = old.split(b"\n"), new.split(b"\n")
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return f"line {i + 1}: {x[:100]!r} != {y[:100]!r}"
    return f"{len(a)} lines != {len(b)} lines"


def cmd_compare_text(args: argparse.Namespace) -> int:
    """Run bt's text runs and compare them with the original's recorded ones."""
    if not args.new_bt.exists():
        print(f"new bt not found at {args.new_bt}", file=sys.stderr)
        return 2
    entries = select(load_corpus(args.old_repo), args.tier, args.filter, args.solver)
    expected: dict[str, dict] = {}
    for tier in {e.tier for e in entries}:
        path = text_baseline_path(tier, args.solver)
        if not path.exists():
            print(f"no text baseline for tier '{tier}' ({args.solver}): run "
                  f"`oracle.py baseline-text --tier {tier} --solver {args.solver}`", file=sys.stderr)
            return 2
        expected.update(json.loads(path.read_text()))
    jobs = text_jobs(entries, args.old_repo, args.solver)
    store = text_dir("new", args.solver)
    old_store = text_dir("old", args.solver)

    def one(job):
        e, name, a, puzzle = job
        want = expected.get(e.rel, {}).get(name)
        if want is None:
            return job, "NO BASELINE", ""
        if "error" in want:
            return job, "OLD FAILED", want["error"]
        if not puzzle.exists():
            return job, "NO INPUT", f"{puzzle} (run baseline-files --tier all)"
        r = run_text(args.new_bt, [*a, *solver_args(args.solver)], puzzle, args.timeout, old=False,
                     entry=e)
        got = text_digest(r)
        if "error" in got:
            return job, "NEW FAILED", got["error"]
        if "crashed" in want:
            prefix = hashlib.sha256(r["out"][:want["outBytes"]]).hexdigest()
            if got.get("exit") == 0 and prefix == want["out"]:
                return job, "ok", f"the original's {want['outBytes']} bytes before it crashed (signal {want['crashed']})"
            diffs = ["exit"] if got.get("exit") != 0 else ["out"]
            want = {"exit": 0, **{k: v for k, v in want.items() if k != "exit"}}
            r["out"] = r["out"][:want["outBytes"]]
        else:
            diffs = [k for k in ("exit", "out", "err") if want.get(k) != got.get(k)]
        if not diffs:
            return job, "ok", f"{got.get('lines')} lines"
        base = store / f"{work_name(e.rel)}.{name}"
        base.parent.mkdir(parents=True, exist_ok=True)
        base.with_name(base.name + ".out").write_bytes(r["out"])
        base.with_name(base.name + ".err").write_bytes(r["err"])
        detail = ", ".join(diffs)
        if "exit" in diffs:
            detail += (f" (exit {want.get('exit', 'signal ' + str(want.get('crashed')))} != "
                       f"{got.get('exit', 'signal ' + str(got.get('crashed')))})")
        for k in ("out", "err"):
            old_file = old_store / f"{work_name(e.rel)}.{name}.{k}"
            if k in diffs and old_file.exists():
                detail += f"; {k} {first_text_difference(old_file.read_bytes(), r[k])}"
        return job, "MISMATCH", detail

    rows = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for n, (job, status, detail) in enumerate(pool.map(one, jobs), 1):
            rows.append((job[0].rel, job[1], status, detail))
            if status != "ok":
                print(f"[{n}/{len(jobs)}] {job[0].rel} {job[1]}: {status}", file=sys.stderr)
    failures = 0
    old_failed = 0
    for rel, name, status, detail in sorted(rows):
        if status == "OLD FAILED":
            old_failed += 1
        elif status != "ok":
            failures += 1
        if status != "ok" or args.verbose:
            print(f"{status:12} {rel} {name}  {detail}")
    compared = len(rows) - old_failed
    print(f"\n{compared - failures}/{compared} text runs match the original's "
          f"({len(entries)} puzzles; solver {args.solver})" + ("" if failures == 0 else f", {failures} do not"))
    if old_failed:
        print(f"{old_failed} not compared: the original gave no answer (OLD FAILED above)")
    return 1 if failures else 0


def build_against_old(old_repo: Path, source: Path, out: Path) -> bool:
    """Compile a tool from this repo against the original's library (core + manifold)."""
    lib = old_repo / "build" / "libburr_core.a"
    manifold = next(iter(sorted((old_repo / "build" / "subprojects").glob("manifold-*/libmanifold.a"))), None)
    if not lib.exists() or manifold is None:
        print(f"original library not found under {old_repo / 'build'}; build the old repo first (just build)", file=sys.stderr)
        return False
    cxx = os.environ.get("CXX", "c++")
    cmd = [cxx, "-std=c++20", "-O1", "-w", f"-I{old_repo / 'src'}", f"-I{old_repo / 'src' / 'lib'}",
           str(source), str(lib), str(manifold), "-lz", "-o", str(out)]
    if sys.platform == "darwin":
        cmd.insert(1, "-mmacosx-version-min=11.0")
    proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if proc.returncode != 0:
        print(proc.stderr, file=sys.stderr)
        return False
    return True


def cmd_grid_dump(args: argparse.Namespace) -> int:
    """Record tests/data/grid_dump.txt: the grid dump (tests/unit/grid/grid_dump.h) of the original."""
    WORK_DIR.mkdir(exist_ok=True)
    exe = WORK_DIR / "grid_dump_old"
    if not build_against_old(args.old_repo, HERE / "grid_dump_old.cpp", exe):
        return 2
    out = HERE.parent / "data" / "grid_dump.txt"
    with open(out, "w") as f:
        subprocess.run([str(exe)], stdout=f, check=True)
    print(f"wrote {out}")
    return 0


FIXTURES = HERE.parent / "data" / "puzzles"


def cmd_model_dump(args: argparse.Namespace) -> int:
    """Record tests/data/model_dump.txt: the model dump of the original for the fixture puzzles."""
    WORK_DIR.mkdir(exist_ok=True)
    exe = WORK_DIR / "model_dump_old"
    if not build_against_old(args.old_repo, HERE / "model_dump_old.cpp", exe):
        return 2
    files = sorted(FIXTURES.glob("*.xmpuzzle"))
    out = HERE.parent / "data" / "model_dump.txt"
    with open(out, "w") as f:
        subprocess.run([str(exe), *map(str, files)], stdout=f, check=True)
    print(f"wrote {out}")
    return 0


def cmd_app_dump(args: argparse.Namespace) -> int:
    """Record tests/data/app_dump.txt: the original's meshes, status table, fit line and
    playback tracks for the fixture puzzles (stage 6)."""
    WORK_DIR.mkdir(exist_ok=True)
    exe = WORK_DIR / "app_dump_old"
    if not build_against_old(args.old_repo, HERE / "app_dump_old.cpp", exe):
        return 2
    files = sorted(FIXTURES.glob("*.xmpuzzle"))
    out = HERE.parent / "data" / "app_dump.txt"
    with open(out, "w") as f:
        subprocess.run([str(exe), *map(str, files)], stdout=f, check=True)
    print(f"wrote {out}")
    return 0


def cmd_stl_dump(args: argparse.Namespace) -> int:
    """Record tests/data/stl_dump.txt: the original's STL meshes of the fixture shapes with each
    grid's default parameters, the view's STL style and one text STL (deferred part 5)."""
    WORK_DIR.mkdir(exist_ok=True)
    exe = WORK_DIR / "stl_dump_old"
    if not build_against_old(args.old_repo, HERE / "stl_dump_old.cpp", exe):
        return 2
    files = sorted(FIXTURES.glob("*.xmpuzzle"))
    out = HERE.parent / "data" / "stl_dump.txt"
    with open(out, "w") as f:
        subprocess.run([str(exe), *map(str, files)], stdout=f, check=True)
    print(f"wrote {out}")
    return 0


def cmd_stl_solution(args: argparse.Namespace) -> int:
    """Record tests/data/stl_solution_by_original/CubeInCage/: the files the original's Export
    Solution Pieces to STL writes for CubeInCage's first solution, defaults, binary (deferred part 5)."""
    WORK_DIR.mkdir(exist_ok=True)
    exe = WORK_DIR / "stl_solution_old"
    if not build_against_old(args.old_repo, HERE / "stl_solution_old.cpp", exe):
        return 2
    out = HERE.parent / "data" / "stl_solution_by_original" / "CubeInCage"
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    subprocess.run([str(exe), str(FIXTURES / "CubeInCage.xmpuzzle"), "0", "0", str(out)], check=True)
    print(f"wrote {out}: {', '.join(sorted(p.name for p in out.iterdir()))}")
    return 0


def cmd_record_saves(args: argparse.Namespace) -> int:
    """Record tests/data/saved_by_original/: what the original writes for each fixture puzzle."""
    WORK_DIR.mkdir(exist_ok=True)
    exe = WORK_DIR / "roundtrip_old"
    if not build_against_old(args.old_repo, HERE / "roundtrip_old.cpp", exe):
        return 2
    out_dir = HERE.parent / "data" / "saved_by_original"
    out_dir.mkdir(exist_ok=True)
    for src in sorted(FIXTURES.glob("*.xmpuzzle")):
        tmp = WORK_DIR / "record.xmpuzzle"
        err = run_tool([str(exe), str(src), str(tmp)], 120)
        if err:
            print(f"{src.name}: {err}", file=sys.stderr)
            return 1
        (out_dir / (src.stem + ".xml")).write_bytes(read_xml_bytes(tmp))
    print(f"wrote {out_dir}")
    return 0


# The fixtures solution.analyse is checked on (stage 7): those of a grid with
# a take-apart that hold saved solutions, and how the original took them
# apart again (cb_AddAllDisasm's take-apart settings).
ANALYSED = (
    ("PelikanBurr", []), ("Prisgon", []), ("CubeInCage", []), ("AlPackino", []),
    ("Bermuda", []), ("DraculasDentalDesaster", []), ("SolidSixPieceBurrs", []),
    ("test_rotation_solvers_solved", ["--rotations"]),
    ("test_rotation_solvers_solved", ["--rotations", "--crowell"]),
)


def cmd_record_analysed(args: argparse.Namespace) -> int:
    """Record tests/data/analysed_by_original/: each fixture's saved assemblies taken
    apart again by the original, as "Add disassembly" for all solutions (stage 7)."""
    WORK_DIR.mkdir(exist_ok=True)
    exe = WORK_DIR / "analyse_old"
    if not build_against_old(args.old_repo, HERE / "analyse_old.cpp", exe):
        return 2
    out_dir = HERE.parent / "data" / "analysed_by_original"
    out_dir.mkdir(exist_ok=True)
    for name, flags in ANALYSED:
        tmp = WORK_DIR / "analysed.xmpuzzle"
        err = run_tool([str(exe), str(FIXTURES / (name + ".xmpuzzle")), str(tmp), *flags], 600)
        if err:
            print(f"{name}: {err}", file=sys.stderr)
            return 1
        suffix = "".join(f.replace("--", "_") for f in flags)
        (out_dir / (name + suffix + ".xml")).write_bytes(read_xml_bytes(tmp))
    print(f"wrote {out_dir}")
    return 0


# Files the round trip checks besides the corpus: features the corpus leaves
# out (sliding, stacking, rotation solutions). Never add the commercial sliding
# puzzle "One Way or Another"; test/test_sliding_solver.xmpuzzle stands in.
ROUNDTRIP_EXTRA = (
    "examples/SliderByGiiker.xmpuzzle",
    "examples/PanexLevel3to10.xmpuzzle",
    "test/test_sliding_solver.xmpuzzle",
    "test/test_sliding_nested.xmpuzzle",
    "test/test_stacking_hanoi.xmpuzzle",
    "test/test_rotation_solvers.xmpuzzle",
)


def read_xml_bytes(path: Path) -> bytes:
    data = path.read_bytes()
    return gzip.decompress(data) if data[:2] == b"\x1f\x8b" else data


def semantic(path: Path):
    """The XML tree of a puzzle file as nested tuples: names, attributes as a
    sorted list, and text where it is not only whitespace (leaf elements
    only, the format has no mixed content). Bytes are read as Latin-1 so that
    files with stray non-UTF-8 bytes compare too."""
    parser = ET.XMLParser(encoding="iso-8859-1")
    root = ET.fromstring(read_xml_bytes(path), parser=parser)

    def walk(e):
        children = [walk(c) for c in e]
        text = (e.text or "") if not children else ""
        if not text.strip():
            text = ""
        return (e.tag, tuple(sorted(e.attrib.items())), text, tuple(children))

    return walk(root)


def first_difference(a, b, where="") -> str:
    if a[0] != b[0]:
        return f"{where}: element {a[0]} != {b[0]}"
    here = f"{where}/{a[0]}"
    if a[1] != b[1]:
        da, db = dict(a[1]), dict(b[1])
        keys = sorted(set(da) | set(db))
        diffs = [f"{k}={da.get(k)!r}/{db.get(k)!r}" for k in keys if da.get(k) != db.get(k)]
        return f"{here}: attributes " + ", ".join(diffs)
    if a[2] != b[2]:
        return f"{here}: text {a[2][:40]!r} != {b[2][:40]!r}"
    for i, (ca, cb) in enumerate(zip(a[3], b[3])):
        if ca != cb:
            return first_difference(ca, cb, f"{here}[{i}]")
    if len(a[3]) != len(b[3]):
        return f"{here}: {len(a[3])} children != {len(b[3])}"
    return ""


def run_tool(argv: list[str], timeout: float, env: dict | None = None) -> str | None:
    """None on success, else the error text."""
    try:
        proc = subprocess.run(argv, capture_output=True, text=True, timeout=timeout, check=False,
                              env=env)
    except subprocess.TimeoutExpired:
        return "timeout"
    if proc.returncode != 0:
        lines = (proc.stderr or proc.stdout).strip().splitlines()
        return lines[-1] if lines else f"exit {proc.returncode}"
    return None


def roundtrip_one(old_tool: Path, new_tool: Path, src: Path, out: Path, timeout: float) -> dict:
    out.mkdir(parents=True, exist_ok=True)
    old_out, new_out = out / "old.xmpuzzle", out / "new.xmpuzzle"
    new2_out, old_of_new = out / "new2.xmpuzzle", out / "old_of_new.xmpuzzle"

    old_err = run_tool([str(old_tool), str(src), str(old_out)], timeout)
    new_err = run_tool([str(new_tool), str(src), str(new_out)], timeout)
    if old_err and new_err:
        return {"status": "both refuse", "detail": f"old: {old_err} | new: {new_err}"}
    if old_err or new_err:
        return {"status": "MISMATCH", "detail": f"old: {old_err or 'ok'} | new: {new_err or 'ok'}"}

    sem_old, sem_new = semantic(old_out), semantic(new_out)
    if sem_old != sem_new:
        return {"status": "MISMATCH", "detail": "new save != original's save: " + first_difference(sem_new, sem_old)}

    # the new save reloads to itself, and the original reads it to the same file
    err = run_tool([str(new_tool), str(new_out), str(new2_out)], timeout)
    if err or semantic(new2_out) != sem_new:
        return {"status": "MISMATCH", "detail": f"new save is not a fixpoint: {err or first_difference(semantic(new2_out), sem_new)}"}
    err = run_tool([str(old_tool), str(new_out), str(old_of_new)], timeout)
    if err or semantic(old_of_new) != sem_old:
        return {"status": "MISMATCH", "detail": f"original reads the new save differently: {err or first_difference(semantic(old_of_new), sem_old)}"}

    same_bytes = read_xml_bytes(old_out) == read_xml_bytes(new_out)
    same_as_input = semantic(src) == sem_new
    return {"status": "ok", "bytes": same_bytes, "input": same_as_input,
            "detail": "" if same_as_input else "both saves differ from the input: " + first_difference(sem_new, semantic(src))}


def cmd_roundtrip(args: argparse.Namespace) -> int:
    """Load and save every corpus file (and the extras) with both programs."""
    WORK_DIR.mkdir(exist_ok=True)
    old_tool = WORK_DIR / "roundtrip_old"
    if not build_against_old(args.old_repo, HERE / "roundtrip_old.cpp", old_tool):
        return 2
    if not args.new_tool.exists():
        print(f"new bt_roundtrip not found at {args.new_tool}", file=sys.stderr)
        return 2

    files: list[tuple[str, Path]] = []
    for e in select(load_corpus(args.old_repo), ["all", "sliding", "stacking"], args.filter):
        files.append((e.rel, entry_path(args.old_repo, e.rel)))
    for rel in ROUNDTRIP_EXTRA:
        if not args.filter or args.filter in rel:
            files.append((rel, args.old_repo / rel))
    solved = sorted((WORK_DIR / "old").rglob("*ttt"))
    for p in solved:
        rel = "solved:" + str(p.relative_to(WORK_DIR / "old"))
        if not args.filter or args.filter in rel:
            files.append((rel, p))
    if not solved:
        print("note: no solved files under work/old; run `oracle.py baseline-files --tier all` to include them",
              file=sys.stderr)

    out_root = WORK_DIR / "roundtrip"
    shutil.rmtree(out_root, ignore_errors=True)
    results: dict[str, dict] = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(roundtrip_one, old_tool, args.new_tool, src, out_root / f"{n:04d}", args.timeout): rel
                   for n, (rel, src) in enumerate(files)}
        for fut in concurrent.futures.as_completed(futures):
            rel = futures[fut]
            try:
                results[rel] = fut.result()
            except Exception as exc:  # a file Python cannot parse, etc.
                results[rel] = {"status": "MISMATCH", "detail": f"harness error: {exc}"}

    failures = 0
    for rel in sorted(results):
        r = results[rel]
        if r["status"] == "MISMATCH":
            failures += 1
        if r["status"] != "ok" or args.verbose:
            print(f"{r['status']:12} {rel}  {r.get('detail', '')}")
    ok = [r for r in results.values() if r["status"] == "ok"]
    print(f"\n{len(results) - failures}/{len(results)} files: the new core saves what the original saves"
          + ("" if failures == 0 else f"; {failures} do not"))
    print(f"  {sum(1 for r in ok if r['bytes'])}/{len(ok)} byte-identical to the original's save")
    print(f"  {sum(1 for r in ok if r['input'])}/{len(ok)} the same as their input "
          "(the rest differ in what the original also normalises; -v lists them)")
    return 1 if failures else 0


# Puzzles for resume-check, with enough assemblies to pause in the middle,
# solving in well under a second each: assembler 0 puzzles (searched with
# the Classic solver and with BurrTools 2) and assembler 1 puzzles (the
# Classic solver gives them assembler 1; so would BurrTools 2).
RESUME_SET = (
    "examples/PelikanBurr.xmpuzzle",
    "examples/DraculasDentalDesaster.xmpuzzle",
    "tictest/performance/puzzles/5x5x5_TICs/TIC_Boxes/TIC_Box_IV.xmpuzzle",
    "tictest/performance/puzzles/ARCparent_Cubes/bACon1.xmpuzzle",
    "tictest/performance/puzzles/ARCparent_Cubes/AngleCube.xmpuzzle",
    "tictest/performance/puzzles/ARCparent_Cubes/ACornBox2.xmpuzzle",
    "tictest/performance/puzzles/RIBs/RIBlet.xmpuzzle",
)
RESUME_SET_ASSEMBLER1 = (
    "examples/CubeInCage.xmpuzzle",
    "examples/DemoPieceGenerator.xmpuzzle",
    "examples/SolidSixPieceBurrs.xmpuzzle",
)

# (solver, threads, plain). Classic: the plain search on one thread (flag 0),
# the vector search on one thread (flag 3), and the split search with and
# without it (assembler 0 flag 2, assembler 1 flag 4). BurrTools 2: one
# thread and four (its position never moves from the start, see below).
RESUME_MODES = (("classic", 1, 1), ("classic", 1, 0), ("classic", 4, 0), ("classic", 4, 1))
RESUME_MODES_BT2 = (("bt2", 1, 0), ("bt2", 4, 0))

# AssemblerError::CannotRestoreInterrupted in the new core (assembler.h).
ERR_INTERRUPTED = "6"


def run_resume_tool(tool: Path, args: list[str], timeout: float) -> dict:
    """Run resume_old / bt_resume; the assemblies (a sorted list, as a
    multiset), the saved position, whether it finished, or an error."""
    try:
        proc = subprocess.run([str(tool), *args], capture_output=True, text=True, timeout=timeout, check=False)
    except subprocess.TimeoutExpired:
        return {"error": "timeout"}
    if proc.returncode != 0:
        return {"error": (proc.stderr or proc.stdout).strip()[-200:] or f"exit {proc.returncode}"}
    out: dict = {"assemblies": []}
    for line in proc.stdout.splitlines():
        key, _, rest = line.partition(" ")
        if key == "A":
            out["assemblies"].append(rest)
        elif key in ("VERSION", "TEXT", "ERR", "FINISHED"):
            out[key.lower()] = rest
    out["assemblies"].sort()
    return out


def cmd_resume_check(args: argparse.Namespace) -> int:
    """Pause assembler searches in one program and continue them in the other."""
    WORK_DIR.mkdir(exist_ok=True)
    old_tool = WORK_DIR / "resume_old"
    if not build_against_old(args.old_repo, HERE / "resume_old.cpp", old_tool):
        return 2
    if not args.new_tool.exists():
        print(f"new bt_resume not found at {args.new_tool}", file=sys.stderr)
        return 2
    tools = {"old": old_tool, "new": args.new_tool}
    pos_file = WORK_DIR / "resume_position.txt"

    runs = [(rel, mode) for rel in RESUME_SET for mode in RESUME_MODES + RESUME_MODES_BT2]
    runs += [(rel, mode) for rel in RESUME_SET_ASSEMBLER1 for mode in RESUME_MODES]

    checks = failures = same_text = known = bt2_refused = 0
    for rel, (solver, threads, plain) in runs:
        if args.filter and args.filter not in rel:
            continue
        puzzle = str(args.old_repo / rel)
        mode = f"{solver}, {threads} thread(s){', plain' if plain else ''}"
        base = [puzzle, solver, str(threads), str(plain), "1"]
        full = {k: run_resume_tool(t, ["full", *base], args.timeout) for k, t in tools.items()}
        if any("error" in r for r in full.values()) or full["old"]["assemblies"] != full["new"]["assemblies"]:
            print(f"MISMATCH     {rel} [{mode}]: full runs differ or fail")
            failures += 1
            continue
        everything = full["old"]["assemblies"]
        n = len(everything)
        for stop_after in sorted({1, max(1, n // 3), max(1, 2 * n // 3)}):
            if stop_after >= n:
                continue
            paused = {k: run_resume_tool(t, ["pause", *base, str(stop_after)], args.timeout)
                      for k, t in tools.items()}
            if "error" in paused["new"] or "text" not in paused["new"]:
                print(f"MISMATCH     {rel} [{mode}] stop {stop_after}: the new pause failed")
                failures += 1
                continue
            version = paused["new"]["version"]

            # BurrTools 2 saves the original's dancing links position, which
            # its dancing cells search never moves: "0 <iterations> (0 0)".
            # The original takes it up as a search from the start and reports
            # everything again; the new core refuses it (start over, the
            # partial results dropped: docs/handoff/stage-3b.md). One thread
            # must still save the same text in both (the same iterations).
            if version == "2.0-bt2":
                if threads == 1:
                    checks += 1
                    if paused["old"].get("text") != paused["new"]["text"] or \
                            paused["old"]["assemblies"] != paused["new"]["assemblies"]:
                        print(f"MISMATCH     {rel} [{mode}] stop {stop_after}: positions differ\n"
                              f"  old: {paused['old'].get('text', '')[:120]}\n  new: {paused['new']['text'][:120]}")
                        failures += 1
                    else:
                        same_text += 1
                for src in ("old", "new"):
                    if "text" not in paused[src]:
                        continue
                    checks += 1
                    pos_file.write_text(paused[src]["version"] + "\n" + paused[src]["text"] + "\n")
                    rest = run_resume_tool(tools["new"], ["resume", *base, str(pos_file)], args.timeout)
                    if rest.get("err") != ERR_INTERRUPTED:
                        print(f"MISMATCH     {rel} [{mode}] stop {stop_after}: a BurrTools 2 position paused "
                              f"by {src} was not refused by the new program")
                        failures += 1
                    else:
                        bt2_refused += 1
                continue

            # Known bug of the original: an assembler 0 plain search stopped
            # right after placing the last piece stands at pos == pieces, and
            # the original reads (saving) and writes (restoring) one entry
            # past its row list there; its checked build traps. The new core
            # writes "(0 0)" for that entry. Such a position is only
            # continued by the new program.
            pieces = everything[0].count(";")
            fields = paused["new"]["text"].split()
            past_end = version == "1.5" and fields[0] == "0" and int(fields[1]) == pieces
            if past_end:
                known += 1
                if "error" not in paused["old"]:
                    print(f"NOTE         {rel} [{mode}] stop {stop_after}: the original saved a "
                          "position after the last piece without failing")
                directions = (("new", "new"),)
            else:
                if "error" in paused["old"] or "text" not in paused["old"]:
                    print(f"MISMATCH     {rel} [{mode}] stop {stop_after}: the original's pause failed")
                    failures += 1
                    continue
                directions = (("old", "new"), ("new", "old"))
                # the deterministic searches save the very same position
                if threads == 1:
                    checks += 1
                    if paused["old"]["text"] != paused["new"]["text"] or \
                            paused["old"]["assemblies"] != paused["new"]["assemblies"]:
                        print(f"MISMATCH     {rel} [{mode}] stop {stop_after}: positions differ\n"
                              f"  old: {paused['old']['text'][:120]}\n  new: {paused['new']['text'][:120]}")
                        failures += 1
                    else:
                        same_text += 1
            for src, dst in directions:
                checks += 1
                pos_file.write_text(paused[src]["version"] + "\n" + paused[src]["text"] + "\n")
                rest = run_resume_tool(tools[dst], ["resume", *base, str(pos_file)], args.timeout)
                label = f"{rel} [{mode}] stop {stop_after}: paused by {src}, continued by {dst}"
                if "error" in rest or "err" in rest:
                    print(f"MISMATCH     {label}: {rest.get('error') or 'cannot restore, code ' + rest['err']}")
                    failures += 1
                    continue
                both = sorted(paused[src]["assemblies"] + rest["assemblies"])
                if both != everything or rest.get("finished") != "1":
                    print(f"MISMATCH     {label}: {len(paused[src]['assemblies'])} + "
                          f"{len(rest['assemblies'])} assemblies, {n} expected"
                          + ("" if both != everything else " (not finished)"))
                    failures += 1
                elif args.verbose:
                    print(f"ok           {label}")
    print(f"\n{checks - failures}/{checks} resume checks pass; "
          f"{same_text} single-thread pauses saved the same text in both programs")
    if known:
        print(f"{known} pauses stood after the last piece, which the original cannot save "
              "(a known bug, see docs/handoff/stage-3a.md); those were continued by the new program only")
    if bt2_refused:
        print(f"{bt2_refused} BurrTools 2 positions were refused by the new program as they should be "
              "(a started search cannot be continued; see docs/handoff/stage-3b.md)")
    return 1 if failures else 0


# Fixtures whose paused positions record-positions takes from the original:
# (fixture, solver, modes). Assembler 0 and BurrTools 2 on the first two,
# assembler 1 on the others.
POSITION_FIXTURES = (
    ("PelikanBurr.xmpuzzle", "classic", ((1, 1), (1, 0), (4, 0))),
    ("DraculasDentalDesaster.xmpuzzle", "classic", ((1, 1), (1, 0), (4, 0))),
    ("PelikanBurr.xmpuzzle", "bt2", ((1, 0),)),
    ("DraculasDentalDesaster.xmpuzzle", "bt2", ((1, 0),)),
    ("CubeInCage.xmpuzzle", "classic", ((1, 1), (1, 0), (4, 0), (4, 1))),
    ("DemoPieceGenerator.xmpuzzle", "classic", ((1, 1), (4, 1))),
)


def cmd_record_positions(args: argparse.Namespace) -> int:
    """Record tests/data/positions_by_original.txt: searches the original paused,
    with the position it saved and the assemblies it reported before."""
    WORK_DIR.mkdir(exist_ok=True)
    tool = WORK_DIR / "resume_old"
    if not build_against_old(args.old_repo, HERE / "resume_old.cpp", tool):
        return 2
    blocks = ["# Searches paused by the original (oracle.py record-positions):",
              "# POS <fixture> <solver> <threads> <plain> <reduce> <stopAfter>, the saved position,",
              "# the assemblies reported before the pause. Read by tests/unit/solve/test_positions.cpp."]
    for name, solver, modes in POSITION_FIXTURES:
        puzzle = str(FIXTURES / name)
        for threads, plain in modes:
            base = [puzzle, solver, str(threads), str(plain), "1"]
            n = len(run_resume_tool(tool, ["full", *base], args.timeout).get("assemblies", []))
            for stop_after in sorted({1, max(1, n // 3), max(1, 2 * n // 3)}):
                r = run_resume_tool(tool, ["pause", *base, str(stop_after)], args.timeout)
                if "error" in r or "text" not in r:
                    continue  # a position the original cannot save (after the last piece)
                blocks.append(f"POS {name} {solver} {threads} {plain} 1 {stop_after}")
                blocks.append(f"VERSION {r['version']}")
                blocks.append(f"TEXT {r['text']}")
                blocks += [f"A {a}" for a in r["assemblies"]]
                blocks.append("END")
    out = HERE.parent / "data" / "positions_by_original.txt"
    out.write_text("\n".join(blocks) + "\n")
    print(f"wrote {out}")
    return 0


SLIDING_FIXTURES = FIXTURES / "sliding"


def cmd_sliding_puzzles(args: argparse.Namespace) -> int:
    """Make the sliding puzzles of the original's test_sliding.cpp (and a few
    more) with the original's code: tests/data/puzzles/sliding/ (the sliding
    tier's generated part)."""
    WORK_DIR.mkdir(exist_ok=True)
    exe = WORK_DIR / "sliding_puzzles_old"
    if not build_against_old(args.old_repo, HERE / "sliding_puzzles_old.cpp", exe):
        return 2
    proc = subprocess.run([str(exe), str(SLIDING_FIXTURES)], capture_output=True, text=True, check=False)
    if proc.returncode != 0:
        print(proc.stderr, file=sys.stderr)
        return 1
    names = proc.stdout.split()
    print(f"wrote {len(names)} puzzles into {SLIDING_FIXTURES.relative_to(REPO)}")
    return 0


# The solves record-slide-routes takes from the original: (puzzle in
# tests/data/puzzles, slide_solve_old's flags); the solved files go to
# tests/data/slide_solved_by_original/, their routes to slide_routes.txt.
SLIDE_ROUTE_SET = (
    ("test_sliding_solver", ["--full"]),
    ("sliding/test_sliding_nested", ["--full", "--nested"]),
    ("sliding/slide_klotski", ["--full"]),
    ("sliding/slide_turn", ["--full"]),
    ("sliding/slide_caterpillar", ["--full"]),
    ("sliding/slide_pocket_nested", ["--full", "--nested"]),
    ("sliding/slide_pocket_partial", ["--full", "--partial"]),
    ("sliding/slide_two_layers_open", ["--full"]),
    ("sliding/slide_shorter_path", []),
    ("sliding/slide_many_starts", []),
    ("sliding/slide_fuzz_00", ["--full", "--nested"]),
    ("sliding/slide_fuzz_03", ["--full", "--nested"]),
)


def cmd_record_slide_routes(args: argparse.Namespace) -> int:
    """Record tests/data/slide_solved_by_original/ and tests/data/slide_routes.txt:
    sliding puzzles solved by the original's solve thread, and the route of
    every move with where its player put the pieces at quarter steps."""
    WORK_DIR.mkdir(exist_ok=True)
    solve = WORK_DIR / "slide_solve_old"
    routes = WORK_DIR / "slide_routes_old"
    if not (build_against_old(args.old_repo, HERE / "slide_solve_old.cpp", solve)
            and build_against_old(args.old_repo, HERE / "slide_routes_old.cpp", routes)):
        return 2
    out_dir = HERE.parent / "data" / "slide_solved_by_original"
    out_dir.mkdir(exist_ok=True)
    solved = []
    for name, flags in SLIDE_ROUTE_SET:
        out = out_dir / (Path(name).name + ".xmpuzzle")
        err = run_tool([str(solve), str(FIXTURES / (name + ".xmpuzzle")), str(out), "-d", "--limit", "0",
                        *flags], 600)
        if err:
            print(f"{name}: {err}", file=sys.stderr)
            return 1
        solved.append(out)
    dump = HERE.parent / "data" / "slide_routes.txt"
    with open(dump, "w") as f:
        f.write("# Routes and quarter-step positions of sliding solutions, from the original\n"
                "# (oracle.py record-slide-routes). Read by tests/unit/solve/test_slide_routes.cpp.\n")
        f.flush()
        subprocess.run([str(routes), *map(str, solved)], stdout=f, check=True)
    print(f"wrote {out_dir.relative_to(REPO)} and {dump.relative_to(REPO)}")
    return 0


STACKING_FIXTURES = FIXTURES / "stacking"


def cmd_stacking_puzzles(args: argparse.Namespace) -> int:
    """Make the stacking puzzles of the original's test_stacking.cpp (and a
    few more) with the original's code: tests/data/puzzles/stacking/ (the
    stacking tier's generated part)."""
    WORK_DIR.mkdir(exist_ok=True)
    exe = WORK_DIR / "stacking_puzzles_old"
    if not build_against_old(args.old_repo, HERE / "stacking_puzzles_old.cpp", exe):
        return 2
    proc = subprocess.run([str(exe), str(STACKING_FIXTURES)], capture_output=True, text=True, check=False)
    if proc.returncode != 0:
        print(proc.stderr, file=sys.stderr)
        return 1
    names = proc.stdout.split()
    print(f"wrote {len(names)} puzzles into {STACKING_FIXTURES.relative_to(REPO)}")
    return 0


# Panex searches for panex-resume: (puzzle, problem, solver). They take a
# second or less each; Panex 6 of the example file goes 881 moves deep.
PANEX_RESUME_SET = (
    ("alpha:tests/data/puzzles/stacking/stack_panex_swap_5.xmpuzzle", 0, "panex"),
    ("alpha:tests/data/puzzles/stacking/stack_panex_jr.xmpuzzle", 0, "panex"),
    ("alpha:tests/data/puzzles/stacking/stack_panex_channel_4.xmpuzzle", 0, "panex"),
    ("alpha:tests/data/puzzles/stacking/stack_plain_r1_n6.xmpuzzle", 0, "stacking"),
    ("examples/PanexLevel3to10.xmpuzzle", 3, "panex"),
)
# (keepLimit, diskBudget, threads): the levels kept in memory; kept on disk
# only, every level; kept on disk thinned out to every few levels.
PANEX_RESUME_MODES = ((0, 0, 1), (10, 0, 4), (10, 20000, 4))
# where the milliseconds of solving lie in state.bin (BTPANEX4): the one
# thing the two programs save differently
PANEX_STATE_MS = slice(36, 44)


def run_panex_tool(tool: Path, args: list[str], timeout: float) -> dict:
    """Run panex_resume_old / bt_panex_resume: its fields, the path's frames."""
    try:
        proc = subprocess.run([str(tool), *args], capture_output=True, text=True, timeout=timeout,
                              check=False)
    except subprocess.TimeoutExpired:
        return {"error": "timeout"}
    if proc.returncode != 0:
        return {"error": (proc.stderr or proc.stdout).strip()[-200:] or f"exit {proc.returncode}"}
    out: dict = {"states": []}
    for line in proc.stdout.splitlines():
        key, _, rest = line.partition(" ")
        if key == "STATE":
            out["states"].append(rest)
        elif key == "FOLDER":
            out["folder"] = Path(rest).name
        else:
            out[key.lower()] = rest
    return out


def saved_files(folder: Path) -> dict[str, bytes]:
    """A saved search's files, state.bin without its solving time."""
    files = {}
    for f in sorted(folder.iterdir()) if folder.exists() else []:
        data = f.read_bytes()
        if f.name == "state.bin":
            data = data[:PANEX_STATE_MS.start] + bytes(8) + data[PANEX_STATE_MS.stop:]
        files[f.name] = data
    return files


def cmd_panex_resume(args: argparse.Namespace) -> int:
    """Stop Panex searches in one program and carry them on in the other;
    both must save the same files and find the same paths."""
    WORK_DIR.mkdir(exist_ok=True)
    old_tool = WORK_DIR / "panex_resume_old"
    if not build_against_old(args.old_repo, HERE / "panex_resume_old.cpp", old_tool):
        return 2
    if not args.new_tool.exists():
        print(f"new bt_panex_resume not found at {args.new_tool}", file=sys.stderr)
        return 2
    tools = {"old": old_tool, "new": args.new_tool}
    root = WORK_DIR / "panex-resume"
    rng = random.Random(3)
    checks = failures = 0

    def folder(name: str) -> Path:
        p = root / name
        shutil.rmtree(p, ignore_errors=True)
        p.mkdir(parents=True)
        return p

    def same(a: dict, b: dict) -> str:
        diff = [k for k in sorted(set(a) | set(b)) if a.get(k) != b.get(k)]
        return ", ".join(diff)

    for rel, problem, solver in PANEX_RESUME_SET:
        if args.filter and args.filter not in rel:
            continue
        puzzle = str(entry_path(args.old_repo, rel))
        for keep, disk, threads in PANEX_RESUME_MODES:
            mode = f"keep {keep}, disk {disk}, {threads} thread(s)"
            base = lambda work: [puzzle, str(problem), str(work), solver, str(keep), str(disk), str(threads)]
            full = {k: run_panex_tool(t, ["full", *base(folder(f"full-{k}"))], args.timeout)
                    for k, t in tools.items()}
            checks += 1
            if any("error" in r for r in full.values()) or same(full["old"], full["new"]):
                print(f"MISMATCH     {rel} [{mode}]: the straight searches differ: "
                      f"{full['old'].get('error') or full['new'].get('error') or same(full['old'], full['new'])}")
                failures += 1
                continue
            depth = int(full["old"]["depth"])
            stops = {1, depth // 4, depth // 2, 3 * depth // 4, depth - 1}
            stops |= {rng.randrange(1, max(2, depth)) for _ in range(2)}
            for stop in sorted(d for d in stops if 0 < d < depth):
                label = f"{rel} [{mode}] stopped at {stop}"
                paused = {}
                for k, t in tools.items():
                    work = folder(f"pause-{k}")
                    paused[k] = (work, run_panex_tool(t, ["pause", *base(work), str(stop)], args.timeout))
                checks += 1
                po, pn = paused["old"][1], paused["new"][1]
                if "error" in po or "error" in pn or same(po, pn):
                    print(f"MISMATCH     {label}: the stops differ: "
                          f"{po.get('error') or pn.get('error') or same(po, pn)}")
                    failures += 1
                    continue
                if po.get("saved") != "1":
                    continue  # through before the depth: nothing saved
                checks += 1
                fo = saved_files(paused["old"][0] / po["folder"])
                fn = saved_files(paused["new"][0] / pn["folder"])
                if fo != fn:
                    names = sorted(set(fo) ^ set(fn)) or [n for n in fo if fo[n] != fn.get(n)]
                    print(f"MISMATCH     {label}: the saved searches differ ({', '.join(names[:5])})")
                    failures += 1
                    continue
                # each saved search carried on by both programs
                results = {}
                for src in ("old", "new"):
                    for dst in ("old", "new"):
                        work = folder(f"resume-{src}-{dst}")
                        shutil.copytree(paused[src][0], work, dirs_exist_ok=True)
                        results[(src, dst)] = run_panex_tool(tools[dst], ["resume", *base(work)], args.timeout)
                first = results[("old", "old")]
                for key, r in results.items():
                    checks += 1
                    if "error" in r or same(first, r) or r.get("moves") != full["old"].get("moves"):
                        print(f"MISMATCH     {label}: stopped by {key[0]}, carried on by {key[1]}: "
                              f"{r.get('error') or same(first, r) or 'another length'}")
                        failures += 1
                    elif args.verbose:
                        print(f"ok           {label}: stopped by {key[0]}, carried on by {key[1]}")
    shutil.rmtree(root, ignore_errors=True)
    print(f"\n{checks - failures}/{checks} Panex resume checks pass")
    return 1 if failures else 0


def cmd_blockpack_check(args: argparse.Namespace) -> int:
    """Pack files with one program's blockpack and unpack them with the
    other's: a saved Panex search's state and levels (as a .btsolve export
    holds them), and odd files; the packed bytes compared too."""
    WORK_DIR.mkdir(exist_ok=True)
    old_tool = WORK_DIR / "blockpack_old"
    search_tool = WORK_DIR / "panex_resume_old"
    if not (build_against_old(args.old_repo, HERE / "blockpack_old.cpp", old_tool)
            and build_against_old(args.old_repo, HERE / "panex_resume_old.cpp", search_tool)):
        return 2
    if not args.new_tool.exists():
        print(f"new bt_blockpack not found at {args.new_tool}", file=sys.stderr)
        return 2
    tools = {"old": old_tool, "new": args.new_tool}
    root = WORK_DIR / "blockpack-check"
    shutil.rmtree(root, ignore_errors=True)
    root.mkdir(parents=True)
    # a saved search: the Panex 5 swap stopped half way, every level on disk
    work = root / "search"
    puzzle = REPO / "tests/data/puzzles/stacking/stack_panex_swap_5.xmpuzzle"
    out = run_panex_tool(search_tool,
                         ["pause", str(puzzle), "0", str(work), "panex", "10", "0", "4", "150"],
                         args.timeout)
    if "error" in out or out.get("saved") != "1":
        print(f"could not make a saved search: {out}", file=sys.stderr)
        return 2
    files = [(f, f.suffix == ".lvl") for f in sorted((work / out["folder"]).iterdir())]
    # odd files: random bytes, empty, unsorted keys read as keys
    rng = random.Random(5)
    odd = root / "odd.bin"
    odd.write_bytes(bytes(rng.randrange(256) for _ in range(123457)))
    empty = root / "empty.bin"
    empty.write_bytes(b"")
    unsorted = root / "unsorted.lvl"
    unsorted.write_bytes(bytes(rng.randrange(256) for _ in range(16 * 5000)))
    files += [(odd, False), (odd, True), (empty, True), (unsorted, True)]
    checks = failures = same_bytes = 0
    for f, keys in files:
        for chunk in ("0", str(16 * 997)):
            packed = {}
            for side, tool in tools.items():
                p = root / f"{f.name}.{side}.{int(keys)}.{chunk}.btz"
                err = run_tool([str(tool), "pack", str(f), str(p), str(int(keys)), chunk], args.timeout)
                if err:
                    print(f"MISMATCH     {f.name} (keys {keys}, chunk {chunk}): {side} cannot pack: {err}")
                    failures += 1
                packed[side] = p
            if any(not p.exists() for p in packed.values()):
                continue
            checks += 1
            if packed["old"].read_bytes() == packed["new"].read_bytes():
                same_bytes += 1
            for src, dst in (("old", "new"), ("new", "old"), ("new", "new")):
                checks += 1
                back = root / f"{f.name}.{src}-{dst}.back"
                err = run_tool([str(tools[dst]), "unpack", str(packed[src]), str(back)], args.timeout)
                if err or back.read_bytes() != f.read_bytes():
                    print(f"MISMATCH     {f.name} (keys {keys}, chunk {chunk}): packed by {src}, "
                          f"unpacked by {dst}: {err or 'other bytes'}")
                    failures += 1
                elif args.verbose:
                    print(f"ok           {f.name} (keys {keys}, chunk {chunk}): {src} -> {dst}")
    shutil.rmtree(root, ignore_errors=True)
    print(f"\n{checks - failures}/{checks} blockpack checks pass; "
          f"{same_bytes}/{len(files) * 2} packed byte for byte as the original packs")
    return 1 if failures else 0


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--old-repo", type=Path, default=Path(os.environ.get("BT_OLD_REPO", HERE.parent.parent.parent / "burrtools_rotations")))
    p.add_argument("--new-bt", type=Path, default=HERE.parent.parent / "build" / "release" / "cli" / "bt")
    sub = p.add_subparsers(dest="cmd", required=True)
    for name in ("baseline", "compare", "baseline-files", "compare-files", "baseline-text", "compare-text"):
        s = sub.add_parser(name)
        s.add_argument("--tier", nargs="+", default=["fast"],
                       choices=["fast", "full", "slow", "all", "sliding", "stacking"],
                       help="all is fast, full and slow; sliding and stacking are those puzzles' own")
        s.add_argument("--filter", help="only puzzles whose path contains this text")
        s.add_argument("--jobs", "-j", type=int, default=1, help="puzzles at once (1 keeps solve times comparable)")
        s.add_argument("--timeout", type=float, default=600.0, help="seconds per puzzle")
        s.add_argument("--verbose", "-v", action="store_true")
        s.add_argument("--solver", choices=["classic", "crowell", "bt2", *SLIDING_SOLVERS, *STACKING_SOLVERS],
                       default="classic",
                       help="the solver both programs run (baselines of another than classic are "
                            "baseline/<tier>-<solver>.json, solved files work/old-<solver>); "
                            "fast, deep and full are the sliding tier's (classic runs it with "
                            "the default, the full search), panex the stacking tier's (classic "
                            "runs it with the Stacking Solver)")
        if name == "compare-files":
            s.add_argument("--dump-tool", type=Path,
                           default=HERE.parent.parent / "build" / "release" / "tests" / "bt_solutions")
        if name in ("baseline-text", "compare-text"):
            s.set_defaults(jobs=os.cpu_count() or 4)
        if name == "compare":
            s.add_argument("--fields", nargs="+", metavar="FIELD",
                           help=f"compare only these fields (default all: {' '.join(COMPARED)})")
            s.add_argument("--threads", "-t", type=int, metavar="N",
                           help="run the new bt with -t N (default: its own, all cores)")
            s.add_argument("--allow-unported", action="store_true",
                           help="list puzzles that need parts not ported yet without failing on them")
    sub.add_parser("grid-dump", help="record tests/data/grid_dump.txt from the original's grid code")
    sub.add_parser("model-dump", help="record tests/data/model_dump.txt from the original's model code")
    sub.add_parser("app-dump", help="record tests/data/app_dump.txt: the original's meshes, status table, fit line and playback")
    sub.add_parser("stl-dump", help="record tests/data/stl_dump.txt: the original's STL meshes of the fixture shapes")
    sub.add_parser("stl-solution", help="record tests/data/stl_solution_by_original/: the original's solution STL export")
    sub.add_parser("record-saves", help="record tests/data/saved_by_original/ from the original's writer")
    sub.add_parser("record-analysed", help="record tests/data/analysed_by_original/: the original's Add disassembly")
    sub.add_parser("sliding-puzzles", help="make tests/data/puzzles/sliding/ with the original's sliding code")
    sub.add_parser("stacking-puzzles", help="make tests/data/puzzles/stacking/ with the original's stacking code")
    s = sub.add_parser("blockpack-check", help="pack files with one program's blockpack, unpack with the other's")
    s.add_argument("--new-tool", type=Path,
                   default=HERE.parent.parent / "build" / "release" / "tests" / "bt_blockpack")
    s.add_argument("--timeout", type=float, default=300.0, help="seconds per program run")
    s.add_argument("--verbose", "-v", action="store_true")
    s = sub.add_parser("panex-resume", help="stop Panex searches in one program and carry them on in the other")
    s.add_argument("--new-tool", type=Path,
                   default=HERE.parent.parent / "build" / "release" / "tests" / "bt_panex_resume")
    s.add_argument("--filter", help="only puzzles whose path contains this text")
    s.add_argument("--timeout", type=float, default=300.0, help="seconds per program run")
    s.add_argument("--verbose", "-v", action="store_true")
    sub.add_parser("record-slide-routes",
                   help="record tests/data/slide_solved_by_original/ and slide_routes.txt from the original")
    s = sub.add_parser("record-positions", help="record tests/data/positions_by_original.txt from the original's assembler")
    s.add_argument("--timeout", type=float, default=120.0, help="seconds per program run")
    s = sub.add_parser("resume-check", help="pause searches in one program and continue them in the other")
    s.add_argument("--new-tool", type=Path, default=HERE.parent.parent / "build" / "release" / "tests" / "bt_resume")
    s.add_argument("--filter", help="only puzzles whose path contains this text")
    s.add_argument("--timeout", type=float, default=120.0, help="seconds per program run")
    s.add_argument("--verbose", "-v", action="store_true")
    s = sub.add_parser("roundtrip", help="load and save every corpus file with both programs and compare")
    s.add_argument("--new-tool", type=Path, default=HERE.parent.parent / "build" / "release" / "tests" / "bt_roundtrip")
    s.add_argument("--filter", help="only files whose path contains this text")
    s.add_argument("--jobs", "-j", type=int, default=os.cpu_count() or 4)
    s.add_argument("--timeout", type=float, default=120.0, help="seconds per program run")
    s.add_argument("--verbose", "-v", action="store_true")
    args = p.parse_args()
    args.old_repo = args.old_repo.resolve()
    # the full search is the sliding default: its baselines are the default's
    if getattr(args, "solver", None) == "full":
        args.solver = "classic"
    return {"baseline": cmd_baseline, "compare": cmd_compare, "baseline-files": cmd_baseline_files,
            "compare-files": cmd_compare_files, "baseline-text": cmd_baseline_text,
            "compare-text": cmd_compare_text,
            "grid-dump": cmd_grid_dump, "roundtrip": cmd_roundtrip, "resume-check": cmd_resume_check,
            "model-dump": cmd_model_dump, "app-dump": cmd_app_dump, "stl-dump": cmd_stl_dump, "stl-solution": cmd_stl_solution, "record-saves": cmd_record_saves,
            "record-analysed": cmd_record_analysed,
            "sliding-puzzles": cmd_sliding_puzzles, "record-slide-routes": cmd_record_slide_routes,
            "stacking-puzzles": cmd_stacking_puzzles, "panex-resume": cmd_panex_resume,
            "blockpack-check": cmd_blockpack_check,
            "record-positions": cmd_record_positions}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())

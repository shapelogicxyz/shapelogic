#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare the new `bt` with the original BurrTools `burrTxt`.

A ported component counts as done only when the two agree on the corpus
(docs/decisions/0007-port-against-the-original.md). Two levels:

  summary  `--json` totals: assemblies, solutions, best level and moves.
           `baseline` records the original's answers in baseline/<tier>.json
           (committed); `compare` runs the new bt and diffs.
  files    every solution and its disassembly, from the solved puzzle file the
           original's burrTxt2 writes. `baseline-files` produces those files
           under work/ (not committed); the comparison itself arrives with the
           disassembly port (stage 4), once the new core can read them.

Run through `just oracle` / `just oracle-baseline`; see README.md.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path

HERE = Path(__file__).resolve().parent
CORPUS = HERE / "corpus.txt"
BASELINE_DIR = HERE / "baseline"
WORK_DIR = HERE / "work"

# Fields that must match exactly. solvetime is reported, never compared.
COMPARED = ("assemblies", "solutions", "dotlevel", "level", "totalmoves", "moves", "rotations")


@dataclass(frozen=True)
class Entry:
    tier: str
    flags: str
    rel: str  # path relative to the old repo


def load_corpus(old_repo: Path) -> list[Entry]:
    entries: list[Entry] = []
    slow: set[str] = set()
    for line in CORPUS.read_text().splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        tier, flags, pattern = line.split(None, 2)
        matches = sorted(glob.glob(str(old_repo / pattern), recursive=True))
        if not matches:
            print(f"warning: corpus pattern matched nothing: {pattern}", file=sys.stderr)
        for m in matches:
            rel = str(Path(m).relative_to(old_repo))
            if tier == "slow":
                slow.add(rel)
            entries.append(Entry(tier, flags, rel))
    # A puzzle listed as slow is not also part of the full tier.
    return [e for e in entries if not (e.tier == "full" and e.rel in slow)]


def select(entries: list[Entry], tiers: list[str], name_filter: str | None) -> list[Entry]:
    wanted = set(tiers)
    if "all" in wanted:
        wanted = {"fast", "full", "slow"}
    return [e for e in entries if e.tier in wanted and (not name_filter or name_filter in e.rel)]


def cli_args(entry: Entry) -> list[str]:
    args = ["--json"]
    if "R" in entry.flags:
        args.append("-R")
    return args


SUMMARY_RE = re.compile(r"(\d+) assemblies and (\d+) solutions found with \d+ iterations in ([\d.]+) seconds")


def run_json(binary: Path, puzzle: Path, entry: Entry, timeout: float, old: bool) -> dict:
    """Run a burrTxt-compatible binary and return its result as a --json-style object.

    For assemble-only ("A") puzzles the original burrTxt is run with -q and
    its text summary parsed, because its --json aborts on grids without
    take-apart (see corpus.txt)."""
    text_mode = old and "A" in entry.flags
    argv = [str(binary), "-q", str(puzzle)] if text_mode else [str(binary), *cli_args(entry), str(puzzle)]
    start = time.monotonic()
    try:
        proc = subprocess.run(argv, capture_output=True, text=True, timeout=timeout, check=False)
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
    if proc.returncode != 0 or not lines:
        tail = (proc.stderr or proc.stdout).strip().splitlines()[-3:]
        return {"error": f"exit {proc.returncode}: " + " | ".join(tail)}
    result = json.loads(lines[-1])
    result["wall"] = wall
    return result


def baseline_path(tier: str) -> Path:
    return BASELINE_DIR / f"{tier}.json"


def run_many(binary: Path, old_repo: Path, entries: list[Entry], jobs: int, timeout: float, old: bool) -> dict[str, dict]:
    results: dict[str, dict] = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = {pool.submit(run_json, binary, old_repo / e.rel, e, timeout, old): e for e in entries}
        for n, fut in enumerate(concurrent.futures.as_completed(futures), 1):
            e = futures[fut]
            results[e.rel] = fut.result()
            status = results[e.rel].get("error", "ok")
            print(f"[{n}/{len(entries)}] {e.rel}: {status}", file=sys.stderr)
    return results


def cmd_baseline(args: argparse.Namespace) -> int:
    old_bin = args.old_repo / "build" / "burrTxt"
    if not old_bin.exists():
        print(f"original burrTxt not found at {old_bin}; build the old repo first (just build)", file=sys.stderr)
        return 2
    entries = select(load_corpus(args.old_repo), args.tier, args.filter)
    results = run_many(old_bin, args.old_repo, entries, args.jobs, args.timeout, old=True)
    BASELINE_DIR.mkdir(exist_ok=True)
    by_tier: dict[str, dict] = {}
    for e in entries:
        by_tier.setdefault(e.tier, {})[e.rel] = {"flags": e.flags, **results[e.rel]}
    for tier, data in by_tier.items():
        path = baseline_path(tier)
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
    entries = select(load_corpus(args.old_repo), args.tier, args.filter)
    expected: dict[str, dict] = {}
    for tier in {e.tier for e in entries}:
        path = baseline_path(tier)
        if not path.exists():
            print(f"no baseline for tier '{tier}': run `just oracle-baseline --tier {tier}`", file=sys.stderr)
            return 2
        expected.update(json.loads(path.read_text()))
    results = run_many(args.new_bt, args.old_repo, entries, args.jobs, args.timeout, old=False)

    failures = 0
    rows = []
    for e in sorted(entries, key=lambda x: x.rel):
        want, got = expected.get(e.rel), results[e.rel]
        if want is None:
            rows.append((e.rel, "NO BASELINE", ""))
            failures += 1
        elif "error" in got:
            rows.append((e.rel, "NEW FAILED", got["error"]))
            failures += 1
        elif "error" in want:
            rows.append((e.rel, "OLD FAILED", want["error"]))
        else:
            diffs = [f"{k}: {want.get(k)} != {got.get(k)}" for k in COMPARED if want.get(k) != got.get(k)]
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
    print(f"\n{len(rows) - failures}/{len(rows)} match the original" + ("" if failures == 0 else f", {failures} do not"))
    return 1 if failures else 0


def cmd_baseline_files(args: argparse.Namespace) -> int:
    """Solve each puzzle with the original burrTxt2, keeping its solved file."""
    old_bin = args.old_repo / "build" / "burrTxt2"
    if not old_bin.exists():
        print(f"original burrTxt2 not found at {old_bin}", file=sys.stderr)
        return 2
    entries = select(load_corpus(args.old_repo), args.tier, args.filter)
    out_root = WORK_DIR / "old"
    for n, e in enumerate(entries, 1):
        dst = out_root / e.rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(args.old_repo / e.rel, dst)  # burrTxt2 writes <file>ttt beside its input
        flags = ["-d"] + (["-R", "--rotations"] if "R" in e.flags else [])
        try:
            proc = subprocess.run([str(old_bin), *flags, str(dst)], capture_output=True, text=True,
                                  timeout=args.timeout, check=False)
            status = "ok" if proc.returncode == 0 else f"exit {proc.returncode}"
        except subprocess.TimeoutExpired:
            status = "timeout"
        print(f"[{n}/{len(entries)}] {e.rel}: {status}", file=sys.stderr)
    print(f"solved files in {out_root}")
    return 0


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


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--old-repo", type=Path, default=Path(os.environ.get("BT_OLD_REPO", HERE.parent.parent.parent / "burrtools_rotations")))
    p.add_argument("--new-bt", type=Path, default=HERE.parent.parent / "build" / "release" / "cli" / "bt")
    sub = p.add_subparsers(dest="cmd", required=True)
    for name in ("baseline", "compare", "baseline-files"):
        s = sub.add_parser(name)
        s.add_argument("--tier", nargs="+", default=["fast"], choices=["fast", "full", "slow", "all"])
        s.add_argument("--filter", help="only puzzles whose path contains this text")
        s.add_argument("--jobs", "-j", type=int, default=1, help="puzzles at once (1 keeps solve times comparable)")
        s.add_argument("--timeout", type=float, default=600.0, help="seconds per puzzle")
        s.add_argument("--verbose", "-v", action="store_true")
    sub.add_parser("grid-dump", help="record tests/data/grid_dump.txt from the original's grid code")
    args = p.parse_args()
    args.old_repo = args.old_repo.resolve()
    return {"baseline": cmd_baseline, "compare": cmd_compare, "baseline-files": cmd_baseline_files,
            "grid-dump": cmd_grid_dump}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())

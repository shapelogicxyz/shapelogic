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
           (`compare-files`, with bt_solve_file): both are read with the new
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
import glob
import gzip
import json
import os
import re
import shutil
import subprocess
import sys
import time
import xml.etree.ElementTree as ET
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


def solver_args(solver: str) -> list[str]:
    """The --solver argument of both CLIs; none for the default (classic)."""
    return [] if solver == "classic" else ["--solver", solver]


def cli_args(entry: Entry, threads: int | None = None, solver: str = "classic") -> list[str]:
    args = ["--json", *solver_args(solver)]
    if "R" in entry.flags:
        args.append("-R")
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
    argv = ([str(binary), "-q", *solver_args(solver), str(puzzle)] if text_mode
            else [str(binary), *cli_args(entry, threads, solver), str(puzzle)])
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
        futures = {pool.submit(run_json, binary, old_repo / e.rel, e, timeout, old, threads, solver): e
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
    entries = select(load_corpus(args.old_repo), args.tier, args.filter)
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
    entries = select(load_corpus(args.old_repo), args.tier, args.filter)
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
    # some puzzles outlasts the baseline's time limit)
    runnable = [e for e in entries if "error" not in expected.get(e.rel, {})]
    results = run_many(args.new_bt, args.old_repo, runnable, args.jobs, args.timeout, old=False,
                       threads=args.threads, solver=args.solver)
    for e in entries:
        results.setdefault(e.rel, {"skipped": True})

    failures = 0
    unported = 0
    rows = []
    for e in sorted(entries, key=lambda x: x.rel):
        want, got = expected.get(e.rel), results[e.rel]
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
    compared = len(rows) - oldFailed - (unported if args.allow_unported else 0)
    what = "all fields" if args.fields is None else ", ".join(fields)
    threads = "default threads" if args.threads is None else f"{args.threads} thread(s)"
    print(f"\n{compared - failures}/{compared} match the original ({what}; {threads}; solver {args.solver})"
          + ("" if failures == 0 else f", {failures} do not"))
    if oldFailed:
        print(f"{oldFailed} not compared: the original gave no answer (OLD FAILED above)")
    if unported:
        print(f"{unported} need a part that is not ported yet"
              + (" (not counted: --allow-unported)" if args.allow_unported else " (counted as failures)"))
    return 1 if failures else 0


def files_dir(side: str, solver: str) -> Path:
    """work/old or work/new for the default solver, work/<side>-<solver> for another."""
    return WORK_DIR / (side if solver == "classic" else f"{side}-{solver}")


def cmd_baseline_files(args: argparse.Namespace) -> int:
    """Solve each puzzle with the original burrTxt2, keeping its solved file."""
    old_bin = args.old_repo / "build" / "burrTxt2"
    if not old_bin.exists():
        print(f"original burrTxt2 not found at {old_bin}", file=sys.stderr)
        return 2
    entries = select(load_corpus(args.old_repo), args.tier, args.filter)
    out_root = files_dir("old", args.solver)
    for n, e in enumerate(entries, 1):
        dst = out_root / e.rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(args.old_repo / e.rel, dst)  # burrTxt2 writes <file>ttt beside its input
        # -R starts over (a file may hold solutions or a paused search);
        # "A" grids have no take-apart, -d makes the original abort on them
        flags = (["-R"] + ([] if "A" in e.flags else ["-d"]) + (["--rotations"] if "R" in e.flags else [])
                 + solver_args(args.solver))
        try:
            proc = subprocess.run([str(old_bin), *flags, str(dst)], capture_output=True, text=True,
                                  timeout=args.timeout, check=False)
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


def compare_files_one(e: Entry, args: argparse.Namespace) -> dict:
    old_file = files_dir("old", args.solver) / (e.rel + "ttt")
    if not old_file.exists():
        return {"status": "NO BASELINE", "detail": f"no {old_file.relative_to(HERE)}: run baseline-files"}
    new_in = files_dir("new", args.solver) / e.rel
    new_in.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(args.old_repo / e.rel, new_in)
    new_file = new_in.with_name(new_in.name + "ttt")
    flags = (["--restart", "--limit", "0"] + ([] if "A" in e.flags else ["-d"])
             + (["--rotations"] if "R" in e.flags else []) + solver_args(args.solver))
    err = run_tool([str(args.new_tool), *flags, str(new_in), str(new_file)], args.timeout)
    if err:
        return {"status": "NEW FAILED", "detail": err}
    try:
        old = read_solutions(args.dump_tool, old_file, args.timeout)
        new = read_solutions(args.dump_tool, new_file, args.timeout)
    except (RuntimeError, subprocess.TimeoutExpired, json.JSONDecodeError) as exc:
        return {"status": "MISMATCH", "detail": f"cannot read: {exc}"}
    diff = compare_solved(old, new, "A" in e.flags)
    if diff:
        return {"status": "MISMATCH", "detail": diff}
    po = old["problems"][0]
    return {"status": "ok", "detail": f"{po.get('assemblies')} assemblies, {po.get('solutions')} solutions, "
                                      f"{len(po['saved'])} saved"}


def cmd_compare_files(args: argparse.Namespace) -> int:
    """Solve each puzzle with the new core as burrTxt2 did and compare the solved files."""
    for tool in (args.new_tool, args.dump_tool):
        if not tool.exists():
            print(f"{tool} not found: build the release preset", file=sys.stderr)
            return 2
    entries = select(load_corpus(args.old_repo), args.tier, args.filter)
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


def run_tool(argv: list[str], timeout: float) -> str | None:
    """None on success, else the error text."""
    try:
        proc = subprocess.run(argv, capture_output=True, text=True, timeout=timeout, check=False)
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
    for e in select(load_corpus(args.old_repo), ["all"], args.filter):
        files.append((e.rel, args.old_repo / e.rel))
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


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--old-repo", type=Path, default=Path(os.environ.get("BT_OLD_REPO", HERE.parent.parent.parent / "burrtools_rotations")))
    p.add_argument("--new-bt", type=Path, default=HERE.parent.parent / "build" / "release" / "cli" / "bt")
    sub = p.add_subparsers(dest="cmd", required=True)
    for name in ("baseline", "compare", "baseline-files", "compare-files"):
        s = sub.add_parser(name)
        s.add_argument("--tier", nargs="+", default=["fast"], choices=["fast", "full", "slow", "all"])
        s.add_argument("--filter", help="only puzzles whose path contains this text")
        s.add_argument("--jobs", "-j", type=int, default=1, help="puzzles at once (1 keeps solve times comparable)")
        s.add_argument("--timeout", type=float, default=600.0, help="seconds per puzzle")
        s.add_argument("--verbose", "-v", action="store_true")
        s.add_argument("--solver", choices=["classic", "crowell", "bt2"], default="classic",
                       help="the solver both programs run (baselines of another than classic are "
                            "baseline/<tier>-<solver>.json, solved files work/old-<solver>)")
        if name == "compare-files":
            s.add_argument("--new-tool", type=Path,
                           default=HERE.parent.parent / "build" / "release" / "tests" / "bt_solve_file")
            s.add_argument("--dump-tool", type=Path,
                           default=HERE.parent.parent / "build" / "release" / "tests" / "bt_solutions")
        if name == "compare":
            s.add_argument("--fields", nargs="+", metavar="FIELD",
                           help=f"compare only these fields (default all: {' '.join(COMPARED)})")
            s.add_argument("--threads", "-t", type=int, metavar="N",
                           help="run the new bt with -t N (default: its own, all cores)")
            s.add_argument("--allow-unported", action="store_true",
                           help="list puzzles that need parts not ported yet without failing on them")
    sub.add_parser("grid-dump", help="record tests/data/grid_dump.txt from the original's grid code")
    sub.add_parser("model-dump", help="record tests/data/model_dump.txt from the original's model code")
    sub.add_parser("record-saves", help="record tests/data/saved_by_original/ from the original's writer")
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
    return {"baseline": cmd_baseline, "compare": cmd_compare, "baseline-files": cmd_baseline_files,
            "compare-files": cmd_compare_files,
            "grid-dump": cmd_grid_dump, "roundtrip": cmd_roundtrip, "resume-check": cmd_resume_check,
            "model-dump": cmd_model_dump, "record-saves": cmd_record_saves,
            "record-positions": cmd_record_positions}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())

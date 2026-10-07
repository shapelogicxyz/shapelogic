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
           disassembly port (stage 4).
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
        # "A" grids have no take-apart; -d makes the original abort on them
        flags = ([] if "A" in e.flags else ["-d"]) + (["-R", "--rotations"] if "R" in e.flags else [])
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
    sub.add_parser("model-dump", help="record tests/data/model_dump.txt from the original's model code")
    sub.add_parser("record-saves", help="record tests/data/saved_by_original/ from the original's writer")
    s = sub.add_parser("roundtrip", help="load and save every corpus file with both programs and compare")
    s.add_argument("--new-tool", type=Path, default=HERE.parent.parent / "build" / "release" / "tests" / "bt_roundtrip")
    s.add_argument("--filter", help="only files whose path contains this text")
    s.add_argument("--jobs", "-j", type=int, default=os.cpu_count() or 4)
    s.add_argument("--timeout", type=float, default=120.0, help="seconds per program run")
    s.add_argument("--verbose", "-v", action="store_true")
    args = p.parse_args()
    args.old_repo = args.old_repo.resolve()
    return {"baseline": cmd_baseline, "compare": cmd_compare, "baseline-files": cmd_baseline_files,
            "grid-dump": cmd_grid_dump, "roundtrip": cmd_roundtrip,
            "model-dump": cmd_model_dump, "record-saves": cmd_record_saves}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())

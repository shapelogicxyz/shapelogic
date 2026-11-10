#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Record tests/data/movement_checks_by_v2.txt: the movement checks BurrTools V2
computes for its octahedral and rhombic grids (docs/V2-IDEAS-PLAN.md idea 4,
decision 0025).

    python3 tests/oracle/v2_movement_checks.py --v2-repo <checkout of BurrTools V2>

The checkout must be built (its build/libburrtoolslib.a and libtoolslib.a).
V2 reads its grids from Lua scripts (data/scripts/grid_*.lua); this turns the
two scripts into C++ initialisers, compiles movement_checks_v2.cpp against
V2's library and writes what it prints. tests/unit/solve/test_movement_checks.cpp
compares the recording with the checks the core derives.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
WORK_DIR = HERE / "work"

# variable in grids.inc, V2's script
GRIDS = (("octa", "grid_octahedral.lua"), ("rhombic", "grid_rhombic.lua"))
# grid.tetrahedron of V2's Lua library (src/gui/lua_functions.cpp)
TETRAHEDRON_FACES = "{{0, 1, 2}, {1, 0, 3}, {2, 1, 3}, {0, 2, 3}}"


def block(src: str, name: str) -> str:
    """The body of the Lua table assigned to `name`, without comments."""
    m = re.search(r"^" + name + r"\s*=\s*\{", src, re.M)
    if m is None:
        raise ValueError(f"no table {name}")
    depth, j = 1, m.end()
    while depth:
        depth += {"{": 1, "}": -1}.get(src[j], 0)
        j += 1
    return re.sub(r"--[^\n]*", "", src[m.end():j - 1])


def numbers(text: str) -> list[float]:
    """The numbers of a flat Lua list (sums like 1.0+0.0 included)."""
    out = []
    for tok in text.split(","):
        tok = tok.strip()
        if tok:
            if not re.fullmatch(r"[-+0-9. ]+", tok):
                raise ValueError(f"not a number: {tok}")
            out.append(float(eval(tok, {"__builtins__": {}}, {})))  # noqa: S307 (digits and signs only)
    return out


def triples(body: str) -> list[list[float]]:
    return [numbers(m) for m in re.findall(r"\{([^{}]*)\}", body)]


def initialisers(scripts: Path) -> str:
    """grids.inc: one function per grid that fills V2's RegisterInformation."""
    out = []
    for var, script in GRIDS:
        src = (scripts / script).read_text()
        m = re.search(r"^storageId\s*=\s*(\d+)", src, re.M)
        if m is None:
            raise ValueError(f"{script}: no storageId")
        ts = numbers(block(src, "translationalSymmetry"))
        pt = numbers(block(src, "physicalTranslations"))
        moves = triples(block(src, "movementVectors"))
        bases = triples(block(src, "basePositions"))
        shapes = [triples(s) for s in
                  re.findall(r"grid\.tetrahedron\(\s*\{(.*?)\}\s*\)", block(src, "physicalShapes"), re.S)]
        if len(ts) != 9 or len(pt) != 9 or len(bases) != len(shapes) or any(len(s) != 4 for s in shapes):
            raise ValueError(f"{script}: not the tables expected")

        def floats(v: list[float]) -> str:
            return "{" + ", ".join(repr(x) + "f" for x in v) + "}"

        def ints(v: list[float]) -> str:
            return "{" + ", ".join(str(int(x)) for x in v) + "}"

        out.append(f"static G::RegisterInformation {var}() {{\n  G::RegisterInformation g;\n")
        out.append(f"  g.storageId = G::StorageId{{{{{{{m.group(1)}}}}}}};\n  g.name = \"{var}\";\n")
        out.append("  g.translationalSymmetry = BurrTools::Maths::Matrix<int>(%s);\n"
                   % ", ".join(str(int(x)) for x in ts))
        out.append("  g.physicalTranslations = BurrTools::Maths::Matrix<float>(%s);\n"
                   % ", ".join(repr(x) + "f" for x in pt))
        out.append("  g.movementVectors = {%s};\n" % ", ".join(ints(v) for v in moves))
        out.append("  g.basePositions = {%s};\n" % ", ".join(ints(v) for v in bases))
        for s in shapes:
            out.append("  g.physicalShapes.push_back(G::Polyhedron{{%s}, %s});\n"
                       % (", ".join(floats(v) for v in s), TETRAHEDRON_FACES))
        out.append("  return g;\n}\n\n")
    return "".join(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--v2-repo", type=Path, default=os.environ.get("BT_V2_REPO"),
                    help="the built checkout of BurrTools V2 (or BT_V2_REPO)")
    args = ap.parse_args()
    if args.v2_repo is None:
        ap.error("--v2-repo (or BT_V2_REPO) is needed")
    v2: Path = args.v2_repo.resolve()
    libs = [v2 / "build" / "libburrtoolslib.a", v2 / "build" / "libtoolslib.a"]
    if not all(lib.exists() for lib in libs):
        print(f"V2's libraries not found under {v2 / 'build'}; build that checkout first", file=sys.stderr)
        return 2

    work = WORK_DIR / "v2"
    work.mkdir(parents=True, exist_ok=True)
    (work / "grids.inc").write_text(initialisers(v2 / "data" / "scripts"))
    exe = work / "movement_checks_v2"
    includes = [v2 / "src" / "lib", v2 / "src" / "tools", v2 / "src" / "maths", v2 / "src" / "octree",
                v2 / "external" / "crc-lib", v2 / "external" / "pssst", v2 / "external" / "sqlite-modern-cpp-lib",
                v2 / "build" / "include", work]
    cmd = [os.environ.get("CXX", "c++"), "-std=gnu++26", "-O1", "-w", "-DNDEBUG",
           *[f"-I{i}" for i in includes], "-isystem", str(v2 / "build" / "_deps" / "gsl-src" / "include"),
           str(HERE / "movement_checks_v2.cpp"), *[str(lib) for lib in libs], "-o", str(exe)]
    proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if proc.returncode != 0:
        print(proc.stderr, file=sys.stderr)
        return 2

    out = HERE.parent / "data" / "movement_checks_by_v2.txt"
    with open(out, "w") as f:
        subprocess.run([str(exe)], stdout=f, check=True)
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

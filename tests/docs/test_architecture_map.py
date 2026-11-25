#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""docs/ARCHITECTURE.md stays a map (cleanup item 3.2): every source file
has a map line, every map line names paths that exist, no file is named
twice, and its prose (every line that is not a map item) stays within
MAX_PROSE_LINES. The map grows a line with every file; the prose is what
must not grow back, so it alone is capped.

A map is a list under a line "Files in `<dir>/`:"; each item is one line,
"- `name`, `name`: what it is for", a name relative to the directory, with
brace groups for several files ("x.{h,cpp}", "voxel_{hex,cairo}.h") and a
trailing "/" for a directory. The list ends at the first line that is not
an item.

Run by ctest (architecture-map) and by the web unit tests
(web/tests/unit/architecture-map.test.ts). Exit 0 when the map is whole;
else each problem on stderr and exit 1.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DOC = ROOT / "docs" / "ARCHITECTURE.md"
# The trees whose sources need a map line, and what counts as a source.
TREES = ("core", "engine", "cli", "python/src", "web/src")
SOURCE = re.compile(r"\.(h|cpp|ts|html|css)$")
MAX_PROSE_LINES = 500

HEADER = re.compile(r"^Files in `([^`]+)`:\s*$")
ITEM = re.compile(r"^- ((?:`[^`]+`)(?:,? (?:and )?`[^`]+`)*): \S")
NAME = re.compile(r"`([^`]+)`")


def expand(name: str) -> list[str]:
    """`a.{h,cpp}` -> a.h, a.cpp; several groups multiply out."""
    m = re.search(r"\{([^{}]*)\}", name)
    if m is None:
        return [name]
    out: list[str] = []
    for alt in m.group(1).split(","):
        out.extend(expand(name[: m.start()] + alt + name[m.end() :]))
    return out


def sources() -> set[str]:
    found: set[str] = set()
    for tree in TREES:
        base = ROOT / tree
        for p in base.rglob("*"):
            if p.is_file() and SOURCE.search(p.name):
                found.add(p.relative_to(ROOT).as_posix())
    return found


def main() -> int:
    problems: list[str] = []
    lines = DOC.read_text(encoding="utf-8").splitlines()
    named: dict[str, int] = {}
    items = 0
    directory: str | None = None
    for number, line in enumerate(lines, start=1):
        header = HEADER.match(line)
        if header is not None:
            directory = header.group(1).rstrip("/")
            if not (ROOT / directory).is_dir():
                problems.append(f"line {number}: no directory {directory}/")
            continue
        if directory is None:
            continue
        item = ITEM.match(line)
        if item is None:
            # a wrapped or misspelled item would end the map silently and
            # leave the files after it "without a map line"
            if line.startswith(("- ", " ", "\t")) and line.strip():
                problems.append(
                    f"line {number}: not a map item of {directory}/ "
                    "(one line: - `name`: what it is for); the map ends here"
                )
            directory = None
            continue
        items += 1
        for token in NAME.findall(item.group(1)):
            for name in expand(token):
                path = f"{directory}/{name.rstrip('/')}"
                if name.endswith("/") and not (ROOT / path).is_dir():
                    problems.append(f"line {number}: {path}/ is not a directory")
                elif not name.endswith("/") and not (ROOT / path).is_file():
                    problems.append(f"line {number}: {path} does not exist")
                elif path in named:
                    problems.append(f"line {number}: {path} is named on line {named[path]} too")
                else:
                    named[path] = number

    for path in sorted(sources() - set(named)):
        problems.append(f"{path} has no map line in docs/ARCHITECTURE.md")
    prose = len(lines) - items
    if prose > MAX_PROSE_LINES:
        problems.append(
            f"{DOC.name} has {prose} lines besides its map, more than {MAX_PROSE_LINES}: "
            "keep the rules short (what the code does not say), not prose"
        )

    for p in problems:
        print(p, file=sys.stderr)
    if not problems:
        print(f"docs/ARCHITECTURE.md: {len(named)} paths in {items} map lines, {prose} other lines")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())

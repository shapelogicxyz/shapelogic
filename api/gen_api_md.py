#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generates docs/API.md from the schema in api/ (decision 0012).

    python3 api/gen_api_md.py           # writes docs/API.md
    python3 api/gen_api_md.py --check   # fails when docs/API.md is stale
"""

import pathlib
import sys

from apischema import API_DIR, Api

OUT = API_DIR.parent / "docs" / "API.md"

# the areas in the order the reference lists them
ORDER = ["engine.json", "document.json", "shape.json", "colour.json", "problem.json",
         "sliding.json", "stacking.json", "solve.json", "solution.json", "playback.json", "explore.json",
         "export.json"]

HEADER = """# shapelogic_engine API

*Generated from `api/*.json` by `api/gen_api_md.py`; do not edit. The schema
is the contract (decision 0012); this page is its reference.*

`shapelogic_engine` speaks JSON-RPC 2.0 over stdio: one request per line on stdin,
one response or notification per line on stdout, logs on stderr. Binary
data (meshes, poses) comes as a **frame**: the notification
`{"jsonrpc":"2.0","method":"frame","params":{"frame":N,"bytes":L}}`, then
exactly L raw bytes; the reply referring to frame N follows. A frame holds
little-endian typed arrays at offsets that are multiples of 4, described by
the reply's `Frame` object.

**Ids** are strings: shapes `s7`, colours `c2`, problems `p1`, solutions
`sol42` (unique within their problem), playbacks `pb1`, explorers `ax1` and
`mx1`. Labels (`S1`, `C1`, `P1`) follow the list order.

**Errors** carry `data.errc`:

| code | errc | meaning |
|---|---|---|
| -32700 | parse_error | the line is not JSON |
| -32600 | invalid_request | not a JSON-RPC request (batches are refused) |
| -32601 | method_not_found | no such method |
| -32602 | invalid_params | a parameter is missing, of the wrong type, or unknown |
| -32001 | invalid_argument | a value out of range or malformed |
| -32002 | not_found | no such shape, colour, problem, solution or handle |
| -32003 | parse_error | a file or bytes that are no puzzle |
| -32004 | io_error | reading or writing a file failed |
| -32005 | unsupported | valid, but not for this puzzle (what a grid or a solver lacks: a take-apart, an assembler, Panex columns) |
| -32006 | invalid_state | not now: pause the solve first, close the edit group... |
| -32603 | internal | a broken invariant in the engine or the core |

Every mutating method returns the document's new `revision` and is one undo
step (or part of the `edit.begin` / `edit.end` group); `document.changed`
follows it.
"""


def anchor(name):
    return name.lower().replace(".", "")


class Gen:
    def __init__(self, api):
        self.api = api

    def type_of(self, s, base):
        """A short description of a schema's type."""
        if s is True or s == {}:
            return "any"
        if "$ref" in s:
            file, _, pointer = s["$ref"].partition("#")
            name = pointer.rsplit("/", 1)[-1]
            if (file or base) == "types.json":
                return f"[{name}](#{anchor(name)})"
            return name
        if "const" in s:
            return f"`{s['const']!r}`".replace("'", '"')
        if "enum" in s:
            return " \\| ".join("`null`" if e is None else f'`"{e}"`' if isinstance(e, str) else f"`{e}`"
                                for e in s["enum"])
        if "oneOf" in s:
            return " or ".join(self.type_of(alt, base) for alt in s["oneOf"])
        t = s.get("type")
        if isinstance(t, list):
            return " or ".join(t)
        if t == "array":
            return self.type_of(s.get("items", {}), base) + "[]"
        if t == "object" and "properties" not in s and isinstance(s.get("additionalProperties"), dict):
            return "map of " + self.type_of(s["additionalProperties"], base)
        return t or "any"

    def deref(self, s, base):
        while isinstance(s, dict) and "$ref" in s and len(s) == 1:
            s, base = self.api.resolve(s["$ref"], base)
        return s, base

    def fields(self, s, base, lines, prefix=""):
        s, base = self.deref(s, base)
        props = s.get("properties", {})
        if not props:
            if s.get("type") == "object" or s == {}:
                lines.append("(none)")
            else:
                lines.append(self.type_of(s, base))
            lines.append("")
            return
        required = set(s.get("required", []))
        lines.append("| name | type | | description |")
        lines.append("|---|---|---|---|")
        nested = []
        for k, sub in props.items():
            desc = sub.get("description", "")
            if not desc and "$ref" in sub:
                target, _ = self.api.resolve(sub["$ref"], base)
                desc = target.get("description", "") if "properties" not in target else ""
            req = "required" if k in required else ""
            lines.append(f"| `{prefix}{k}` | {self.type_of(sub, base)} | {req} | {desc} |")
            inline = sub.get("items", {}) if sub.get("type") == "array" else sub
            if isinstance(inline, dict) and "properties" in inline and "$ref" not in inline:
                nested.append((k, inline))
        if isinstance(s.get("additionalProperties"), dict):
            lines.append(f"| *other* | {self.type_of(s['additionalProperties'], base)} | | |")
        lines.append("")
        for k, inline in nested:
            lines.append(f"`{prefix}{k}`:")
            lines.append("")
            self.fields(inline, base, lines, f"{prefix}{k}.")

    def result(self, s, base, lines):
        s2, base2 = self.deref(s, base)
        if "$ref" in s and s2.get("properties") is not None and len(s) == 1:
            lines.append(f"Result: {self.type_of(s, base)}")
            lines.append("")
            return
        lines.append("Result:")
        lines.append("")
        self.fields(s, base, lines)

    def render(self):
        lines = [HEADER]
        files = [f for f in ORDER if f in self.api.files] + \
                sorted(f for f in self.api.files if f not in ORDER and f != "types.json")
        lines.append("## Contents")
        lines.append("")
        for f in files:
            doc = self.api.files[f]
            lines.append(f"- [{doc['title']}](#{anchor(doc['title']).replace(' ', '-')})")
        lines.append("- [Types](#types)")
        lines.append("")
        for f in files:
            doc = self.api.files[f]
            lines.append(f"## {doc['title']}")
            lines.append("")
            lines.append(doc.get("description", ""))
            lines.append("")
            for name, entry in doc.get("methods", {}).items():
                lines.append(f"### `{name}`")
                lines.append("")
                lines.append(entry["summary"])
                lines.append("")
                lines.append("Params:")
                lines.append("")
                self.fields(entry["params"], f, lines)
                self.result(entry["result"], f, lines)
                if entry.get("errors"):
                    lines.append("Errors: " + ", ".join(f"`{e}`" for e in entry["errors"]))
                    lines.append("")
            for name, entry in doc.get("events", {}).items():
                lines.append(f"### event `{name}`")
                lines.append("")
                lines.append(entry["summary"])
                lines.append("")
                self.fields(entry["params"], f, lines)
            for name, sub in doc.get("$defs", {}).items():
                if "properties" in sub:
                    lines.append(f"#### {name}")
                    lines.append("")
                    if sub.get("description"):
                        lines.append(sub["description"])
                        lines.append("")
                    self.fields(sub, f, lines)
        lines.append("## Types")
        lines.append("")
        for name, sub in self.api.files["types.json"]["$defs"].items():
            lines.append(f"### {name}")
            lines.append("")
            if sub.get("description"):
                lines.append(sub["description"])
                lines.append("")
            if "properties" in sub:
                self.fields(sub, "types.json", lines)
            else:
                lines.append(self.type_of(sub, "types.json") +
                             (f" matching `{sub['pattern']}`" if "pattern" in sub else ""))
                lines.append("")
        return "\n".join(lines).rstrip() + "\n"


def main():
    text = Gen(Api()).render()
    if "--check" in sys.argv:
        current = OUT.read_text(encoding="utf-8") if OUT.exists() else ""
        if current != text:
            print(f"{OUT} is stale: run python3 api/gen_api_md.py")
            return 1
        print(f"{OUT} is up to date")
        return 0
    OUT.write_text(text, encoding="utf-8")
    print(f"wrote {OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

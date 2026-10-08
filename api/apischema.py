#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The engine API's schema (api/*.json) and a validator for the subset of
JSON Schema 2020-12 it uses (decision 0012), so that the contract tests
need no packages.

    python3 api/apischema.py            # checks the schema itself

Supported keywords: type, properties, required, additionalProperties,
items, enum, const, pattern, minimum, maximum, minItems, maxItems,
minLength, oneOf, $ref (within a file or to another file of api/), and
the annotations title, description, $schema, $id, $defs. A schema using
anything else fails the self-check, so nothing in it goes unchecked.
"""

import json
import pathlib
import re
import sys

API_DIR = pathlib.Path(__file__).resolve().parent

ASSERTIONS = {
    "type", "properties", "required", "additionalProperties", "items", "enum", "const",
    "pattern", "minimum", "maximum", "minItems", "maxItems", "minLength", "oneOf", "$ref",
}
ANNOTATIONS = {"title", "description", "$schema", "$id", "$defs"}
# the keys of a method or event entry (not JSON Schema)
ENTRY_KEYS = {"summary", "params", "result", "errors"}
ERRCS = {"invalid_argument", "not_found", "parse_error", "io_error", "unsupported",
         "invalid_state", "internal"}


class SchemaError(Exception):
    pass


def _type_ok(value, t):
    if t == "null":
        return value is None
    if t == "boolean":
        return isinstance(value, bool)
    if t == "integer":
        return isinstance(value, int) and not isinstance(value, bool)
    if t == "number":
        return isinstance(value, (int, float)) and not isinstance(value, bool)
    if t == "string":
        return isinstance(value, str)
    if t == "array":
        return isinstance(value, list)
    if t == "object":
        return isinstance(value, dict)
    raise SchemaError(f"unknown type {t!r}")


class Api:
    """The schema files, the methods and events they declare, validation."""

    def __init__(self, api_dir=API_DIR):
        self.files = {}
        for p in sorted(pathlib.Path(api_dir).glob("*.json")):
            with open(p, encoding="utf-8") as f:
                self.files[p.name] = json.load(f)
        self.methods = {}  # name -> (file, entry)
        self.events = {}
        for name, doc in self.files.items():
            for m, entry in doc.get("methods", {}).items():
                if m in self.methods:
                    raise SchemaError(f"method {m} declared twice")
                self.methods[m] = (name, entry)
            for e, entry in doc.get("events", {}).items():
                if e in self.events:
                    raise SchemaError(f"event {e} declared twice")
                self.events[e] = (name, entry)

    # ---- references --------------------------------------------------------------

    def resolve(self, ref, base):
        """The schema a $ref names, and the file it is in."""
        file, _, pointer = ref.partition("#")
        file = file or base
        if file not in self.files:
            raise SchemaError(f"$ref {ref!r}: no file {file}")
        node = self.files[file]
        for part in [p for p in pointer.split("/") if p]:
            if not isinstance(node, dict) or part not in node:
                raise SchemaError(f"$ref {ref!r} does not resolve (in {base})")
            node = node[part]
        return node, file

    # ---- validation --------------------------------------------------------------

    def errors(self, value, schema, base, path="$"):
        """What is wrong with value against schema (empty when it fits)."""
        out = []
        self._check(value, schema, base, path, out)
        return out

    def _check(self, v, s, base, path, out):
        if s is True or s == {}:
            return
        if "$ref" in s:
            target, file = self.resolve(s["$ref"], base)
            self._check(v, target, file, path, out)
        if "type" in s:
            types = s["type"] if isinstance(s["type"], list) else [s["type"]]
            if not any(_type_ok(v, t) for t in types):
                out.append(f"{path}: {json.dumps(v)[:80]} is not {'/'.join(types)}")
                return
        if "const" in s and v != s["const"]:
            out.append(f"{path}: {json.dumps(v)[:80]} is not {json.dumps(s['const'])}")
        if "enum" in s and not any(v == e and type(v) is type(e) for e in s["enum"]):
            out.append(f"{path}: {json.dumps(v)[:80]} is not one of {s['enum']}")
        if "oneOf" in s:
            matches = sum(1 for alt in s["oneOf"] if not self.errors(v, alt, base, path))
            if matches != 1:
                out.append(f"{path}: {json.dumps(v)[:80]} matches {matches} of the oneOf choices")
        if isinstance(v, str):
            if "pattern" in s and not re.search(s["pattern"], v):
                out.append(f"{path}: {v!r} does not match {s['pattern']}")
            if "minLength" in s and len(v) < s["minLength"]:
                out.append(f"{path}: {v!r} is shorter than {s['minLength']}")
        if isinstance(v, (int, float)) and not isinstance(v, bool):
            if "minimum" in s and v < s["minimum"]:
                out.append(f"{path}: {v} is below {s['minimum']}")
            if "maximum" in s and v > s["maximum"]:
                out.append(f"{path}: {v} is above {s['maximum']}")
        if isinstance(v, list):
            if "minItems" in s and len(v) < s["minItems"]:
                out.append(f"{path}: fewer than {s['minItems']} items")
            if "maxItems" in s and len(v) > s["maxItems"]:
                out.append(f"{path}: more than {s['maxItems']} items")
            if "items" in s:
                for i, item in enumerate(v):
                    self._check(item, s["items"], base, f"{path}[{i}]", out)
        if isinstance(v, dict):
            props = s.get("properties", {})
            for r in s.get("required", []):
                if r not in v:
                    out.append(f"{path}: '{r}' is missing")
            for k, item in v.items():
                if k in props:
                    self._check(item, props[k], base, f"{path}.{k}", out)
                elif "additionalProperties" in s:
                    extra = s["additionalProperties"]
                    if extra is False:
                        out.append(f"{path}: '{k}' is not allowed")
                    elif isinstance(extra, dict):
                        self._check(item, extra, base, f"{path}.{k}", out)

    def check_params(self, method, params):
        file, entry = self.methods[method]
        return self.errors(params, entry["params"], file, f"{method}.params")

    def check_result(self, method, result):
        file, entry = self.methods[method]
        return self.errors(result, entry["result"], file, f"{method}.result")

    def check_event(self, event, params):
        if event not in self.events:
            return [f"no event {event} in the schema"]
        file, entry = self.events[event]
        return self.errors(params, entry["params"], file, f"{event}.params")

    # ---- the schema itself -------------------------------------------------------

    def self_check(self):
        """Problems with the schema: keywords outside the subset, $refs that
        do not resolve, entries without summary, params or result."""
        out = []

        def walk(s, file, where):
            if isinstance(s, bool):
                return
            if not isinstance(s, dict):
                out.append(f"{file} {where}: a schema must be an object")
                return
            for k, v in s.items():
                if k not in ASSERTIONS and k not in ANNOTATIONS:
                    out.append(f"{file} {where}: keyword {k!r} is outside the supported subset")
                if k == "$ref":
                    try:
                        self.resolve(v, file)
                    except SchemaError as e:
                        out.append(str(e))
                elif k in ("properties", "$defs"):
                    for name, sub in v.items():
                        walk(sub, file, f"{where}.{name}")
                elif k in ("items", "additionalProperties") and isinstance(v, dict):
                    walk(v, file, f"{where}.{k}")
                elif k == "oneOf":
                    for i, sub in enumerate(v):
                        walk(sub, file, f"{where}.oneOf[{i}]")
                elif k == "type":
                    for t in v if isinstance(v, list) else [v]:
                        if t not in ("null", "boolean", "integer", "number", "string", "array", "object"):
                            out.append(f"{file} {where}: unknown type {t!r}")

        for file, doc in self.files.items():
            for k in doc:
                if k not in ANNOTATIONS | {"methods", "events"}:
                    out.append(f"{file}: top-level key {k!r} is not known")
            for name, sub in doc.get("$defs", {}).items():
                walk(sub, file, f"$defs.{name}")
            for kind in ("methods", "events"):
                for name, entry in doc.get(kind, {}).items():
                    need = {"summary", "params"} | ({"result"} if kind == "methods" else set())
                    for k in need - set(entry):
                        out.append(f"{file} {name}: no {k}")
                    for k in set(entry) - ENTRY_KEYS:
                        out.append(f"{file} {name}: unknown key {k!r}")
                    for e in entry.get("errors", []):
                        if e not in ERRCS:
                            out.append(f"{file} {name}: unknown error {e!r}")
                    for k in ("params", "result"):
                        if k in entry:
                            walk(entry[k], file, f"{name}.{k}")
        return out


def main():
    api = Api()
    problems = api.self_check()
    for p in problems:
        print(p)
    print(f"{len(api.methods)} methods, {len(api.events)} events, "
          f"{len(problems)} problems in {len(api.files)} files")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())

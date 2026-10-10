#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The contract tests of bt-engine (decision 0012): start the engine as a
subprocess, drive it through scripted sessions, and check every reply and
event against the schema in api/. Run by ctest:

    python3 tests/contract/test_engine.py --engine build/dev/engine/bt-engine [names...]
"""

import argparse
import base64
import pathlib
import shutil
import sys
import tempfile
import time
import traceback

from engine_client import ROOT, Api, ContractError, Engine

FIXTURES = ROOT / "tests" / "data" / "puzzles"
TESTS = []


def test(f):
    TESTS.append(f)
    return f


def check(cond, what):
    if not cond:
        raise ContractError(what)


def changed_event(e, revision):
    """The document.changed event of a revision (it follows the reply)."""
    return e.wait_event("document.changed", lambda p: p["revision"] == revision, timeout=10)


# ---- engine and settings -------------------------------------------------------------------

@test
def engine_info_and_settings(e):
    hello = e.call("engine.hello", {"protocol": 1})
    check(hello["protocol"] == 1 and hello["name"] == "bt-engine", f"hello {hello}")
    e.fails("engine.hello", {"protocol": 2}, "unsupported")
    e.fails("engine.hello", {"protocol": 0}, "invalid_params")
    info = e.call("engine.info")
    check(info["protocol"] == hello["protocol"], "engine.info names the same protocol")
    methods = set(info["methods"])
    unknown = methods - set(e.api.methods)
    check(not unknown, f"methods the schema does not have: {sorted(unknown)}")
    missing = set(e.api.methods) - methods
    check(not missing, f"methods of the schema the engine does not have: {sorted(missing)}")
    check({g["grid"] for g in info["grids"]} == {"bricks", "prisms", "spheres", "rhombic",
                                                  "tetraOcta", "sliding", "stacking"}, "the grids")
    check(info["hardwareThreads"] >= 1, "hardware threads")
    supported = {g["grid"]: g["supported"] for g in info["grids"]}
    check(supported["sliding"] and not supported["stacking"], f"what is solved {supported}")
    check([x["solver"] for x in info["slidingSolvers"]] == ["fast", "deep", "full"] and
          info["slidingSolvers"][2]["label"] == "Sliding Full Solver (full depth)",
          f"the sliding solvers {info['slidingSolvers']}")

    s = e.call("settings.get")
    folder = s.pop("slideFolder")
    check(folder == "" or folder.endswith("sliding"), f"the slide folder {folder!r}")
    check(s == {"undoDepth": 25, "threads": 0, "solver": "classic", "slideDiskGB": 0},
          f"default settings {s}")
    s = e.call("settings.set", {"undoDepth": 2, "solver": "bt2", "slideFolder": "", "slideDiskGB": 1.5})
    check(s == {"undoDepth": 2, "threads": 0, "solver": "bt2", "slideFolder": "", "slideDiskGB": 1.5},
          f"settings after set {s}")
    e.fails("settings.set", {"threads": 1000}, "invalid_params")
    e.fails("settings.set", {"solver": "fortran"}, "invalid_params")
    e.fails("settings.set", {"slideDiskGB": -1}, "invalid_params")
    e.call("settings.set", {"slideFolder": folder, "slideDiskGB": 0})

    # the undo depth reaches the document: three steps, two kept
    e.call("document.new", {"grid": "bricks"})
    for name in ("a", "b", "c"):
        e.call("document.setNotes", {"text": name})
    check(len(e.call("edit.history")["undo"]) == 2, "the undo depth")
    e.call("settings.set", {"undoDepth": 25, "solver": "classic"})


# ---- the protocol --------------------------------------------------------------------------

@test
def protocol_errors(e):
    def raw(line, errc, rid=None):
        e.send_raw(line + "\n")
        m = e._next(10)
        check("error" in m and m["error"]["data"]["errc"] == errc, f"{line!r}: {m}")
        check(m["id"] == rid, f"{line!r}: id {m['id']}")

    raw("this is not json", "parse_error")
    raw('[{"jsonrpc":"2.0","id":1,"method":"engine.info"}]', "invalid_request")
    raw('{"id":7,"method":"engine.info"}', "invalid_request", 7)
    raw('{"jsonrpc":"2.0","id":8,"method":"engine.info","extra":1}', "invalid_request", 8)
    raw('{"jsonrpc":"2.0","id":9,"method":"engine.info","params":[1]}', "invalid_params", 9)
    e.fails("no.such.method", {}, "method_not_found")
    e.fails("shape.get", {"shape": "c1"}, "invalid_params")
    e.fails("shape.get", {"shape": "s1", "colour": 3}, "invalid_params")
    e.fails("shape.get", {}, "invalid_params")
    e.fails("shape.rename", {"shape": "s1", "name": 5}, "invalid_params")
    e.fails("shape.get", {"shape": "s999"}, "not_found")
    # a notification gets no reply, but does its work
    e.request("document.setNotes", {"text": "by notification"}, notification=True)
    snap = e.call("document.get")
    check(snap["notes"] == "by notification", "the notification ran")
    # string ids are answered with the same id
    e.send_raw('{"jsonrpc":"2.0","id":"abc","method":"settings.get"}\n')
    m = e._next(10)
    check(m["id"] == "abc" and "result" in m, f"a string id: {m}")
    # blank lines are ignored
    e.send_raw("\n   \n")
    e.call("engine.info")
    # params nested 50,000 deep are refused, with the engine alive (finding 2 of the audit)
    raw('{"jsonrpc":"2.0","id":1,"method":"settings.get","params":{"x":' + "[" * 50000 + "]" * 50000 + "}}",
        "invalid_request")
    e.call("engine.info")
    # a line over the limit (64 MiB) is refused once its newline arrives, and the engine goes on
    e.send_raw(b"x" * (64 * 1024 * 1024 + 1) + b"\n")
    m = e._next(120)
    check("error" in m and m["error"]["code"] == -32600 and m["id"] is None, f"a long line: {m}")
    e.call("engine.info")
    # a notification that would produce a frame gets none (nothing would refer to it)
    s1 = e.call("shape.create")["shape"]
    frames = len(e.frames)
    e.request("shape.mesh", {"shape": s1}, notification=True)
    e.call("settings.get")
    check(len(e.frames) == frames, "no frame for a notification")
    for _ in range(50):
        if any("notification" in line for line in e.stderr):
            break
        time.sleep(0.05)
    check(any("shape.mesh" in line and "notification" in line for line in e.stderr),
          f"the refusal is logged: {e.stderr}")


# ---- the document ------------------------------------------------------------------------------

@test
def document_files(e):
    with tempfile.TemporaryDirectory() as tmp:
        # a copy of the fixture: the document remembers the path it was
        # opened from, and a later save without a path goes there
        fixture = pathlib.Path(tmp) / "Bermuda.xmpuzzle"
        shutil.copy(FIXTURES / "Bermuda.xmpuzzle", fixture)
        snap = e.call("document.open", {"path": str(fixture)})
        check(snap["grid"] == "prisms" and len(snap["shapes"]) > 0 and not snap["dirty"], "opened")
        check(snap["path"] == str(fixture), "its path")
        rev0 = snap["revision"]

        r = e.call("document.setNotes", {"text": "changed notes"})
        check(r["revision"] > rev0, "the revision went up")
        ev = changed_event(e, r["revision"])
        check(ev["what"] == ["notes"], f"the event {ev}")
        check(e.call("document.get")["dirty"], "dirty after an edit")

        path = pathlib.Path(tmp) / "saved.xmpuzzle"
        r = e.call("document.save", {"path": str(path)})
        check(path.exists() and r["path"] == str(path), "saved")
        check(not e.call("document.get")["dirty"], "clean after saving")
        e.call("document.save")  # to the same path again
        # the bytes open as the same puzzle
        data = e.call("document.saveBytes")["data"]
        again = e.call("document.openBytes", {"data": data})
        check(again["path"] is None and again["notes"] == "changed notes", "openBytes")
        check([s["name"] for s in again["shapes"]] == [s["name"] for s in snap["shapes"]], "shapes")
        check(again["revision"] > r["revision"], "revisions go on across documents")
        # a rescue copy: the bytes standing for a file, with unsaved changes
        rescued = e.call(
            "document.openBytes", {"data": data, "path": str(path), "dirty": True}
        )
        check(rescued["path"] == str(path) and rescued["dirty"], "openBytes with a path, dirty")
        e.call("document.setNotes", {"text": "rescued notes"})
        check(e.call("document.save")["path"] == str(path), "saved to the path given")
        check(not e.call("document.get")["dirty"], "clean after saving the rescue copy")
        e.call("document.setNotes", {"text": "changed notes"})
        e.call("document.save")
        e.fails("document.openBytes", {"data": data, "path": ""}, "invalid_argument")
        reopened = e.call("document.open", {"path": str(path)})
        check(reopened["notes"] == "changed notes", "the saved file")

    e.fails("document.open", {"path": str(FIXTURES / "no such file.xmpuzzle")}, "io_error")
    e.fails("document.open", {"path": str(ROOT / "api" / "types.json")}, "parse_error")
    e.fails("document.openBytes", {"data": "%%%"}, "invalid_argument")
    e.fails("document.openBytes", {"data": base64.b64encode(b"<puzzle").decode()}, "parse_error")
    e.call("document.new", {"grid": "bricks"})
    e.fails("document.save", {}, "invalid_argument")  # no path yet
    e.fails("document.new", {"grid": "hexagons"}, "invalid_params")
    # names and notes: what the file cannot hold is refused
    s1 = e.call("shape.create")["shape"]
    e.fails("shape.rename", {"shape": s1, "name": "line\nbreak"}, "invalid_argument")
    e.fails("shape.rename", {"shape": s1, "name": "a\x00b"}, "invalid_argument")
    e.fails("document.setNotes", {"text": "\x01"}, "invalid_argument")
    e.call("shape.rename", {"shape": s1, "name": "tab\there"})
    e.call("document.setNotes", {"text": "two\nlines"})


@test
def undo_redo_and_groups(e):
    e.call("document.new", {"grid": "bricks"})
    s1 = e.call("shape.create", {"size": [3, 3, 3]})["shape"]
    e.fails("edit.end", {}, "invalid_state")
    e.call("edit.begin", {"label": "Drag"})
    e.fails("edit.begin", {}, "invalid_state")
    for c in ([0, 0, 0], [1, 0, 0], [2, 0, 0]):
        r = e.call("shape.setCells", {"shape": s1, "tool": "fixed", "cells": [c]})
        check(r["changed"] == 1, "a cell set")
    e.fails("edit.undo", {}, "invalid_state")  # inside a group
    e.call("edit.end")
    h = e.call("edit.history")
    check(h["undo"] == ["New shape", "Drag"] and h["redo"] == [], f"history {h}")
    check(len(e.call("shape.get", {"shape": s1})["cells"]) == 3, "three cells")
    r = e.call("edit.undo")
    ev = changed_event(e, r["revision"])
    check("shapes" in ev["what"] and ev["shapes"] == [], f"undo's event {ev}")
    check(len(e.call("shape.get", {"shape": s1})["cells"]) == 0, "undone in one step")
    check(e.call("edit.history")["redo"] == ["Drag"], "redo")
    e.call("edit.redo")
    check(len(e.call("shape.get", {"shape": s1})["cells"]) == 3, "redone")
    e.call("edit.undo")
    e.call("edit.undo")
    e.fails("edit.undo", {}, "invalid_state")  # nothing left
    check(e.call("document.get")["shapes"] == [], "back to empty")
    e.call("edit.redo")
    e.fails("shape.get", {"shape": "s999"}, "not_found")
    # an empty group leaves no step
    e.call("edit.begin")
    e.call("edit.end")
    check(len(e.call("edit.history")["undo"]) == 1, "an empty group")


# ---- shapes ------------------------------------------------------------------------------------

@test
def shapes(e):
    e.call("document.new", {"grid": "bricks"})
    a = e.call("shape.create")
    snap = e.call("document.get")
    check(snap["shapes"][0]["size"] == [4, 4, 4] and snap["shapes"][0]["label"] == "S1", "default size")
    s1 = a["shape"]
    ev = changed_event(e, a["revision"])
    check(ev["what"] == ["shapes"] and ev["shapes"] == [s1], f"create's event {ev}")

    r = e.call("shape.setCells", {"shape": s1, "tool": "fixed", "cells": [[0, 0, 0], [9, 9, 9], [-5, 0, 0]]})
    check(r["changed"] == 1, "cells outside are left out")
    r = e.call("shape.setCells", {"shape": s1, "tool": "variable", "mirror": ["x"], "cells": [[0, 1, 0]]})
    check(r["changed"] == 2, "mirrored")
    r = e.call("shape.setCells", {"shape": s1, "tool": "fixed", "span": ["z"], "cells": [[3, 3, 0]]})
    check(r["changed"] == 4, "a span")
    r = e.call("shape.setCells", {"shape": s1, "tool": "fixed", "cells": [[0, 0, 0]]})
    check(r["changed"] == 0, "nothing to change")
    got = e.call("shape.get", {"shape": s1})
    check(len(got["cells"]) == 7, f"cells {got['cells']}")
    check({tuple(c["at"]) for c in got["cells"] if c["state"] == "variable"} == {(0, 1, 0), (3, 1, 0)}, "variable cells")
    layer = e.call("shape.get", {"shape": s1, "layer": 2})
    check([c["at"] for c in layer["cells"]] == [[3, 3, 2]], "one layer")
    e.fails("shape.setCells", {"shape": s1, "tool": "fixed", "mirror": ["w"], "cells": []}, "invalid_params")
    e.fails("shape.setCells", {"shape": s1, "tool": "fixed", "cells": [[1, 2]]}, "invalid_params")
    e.fails("shape.setCells", {"shape": s1, "tool": "glue", "cells": []}, "invalid_params")

    r = e.call("shape.setBox", {"shape": s1, "tool": "fixed", "from": [0, 0, 1], "to": [3, 3, 1]})
    check(r["changed"] == 15, f"box fill {r}")
    r = e.call("shape.setBox", {"shape": s1, "tool": "erase", "from": [9, 9, 1], "to": [0, 0, 1]})
    check(r["changed"] == 16, f"box clear {r}")

    # colours
    c1 = e.call("colour.add", {"rgb": "#FF0000"})["colour"]
    check(e.call("colour.list")["colours"] == [{"id": c1, "label": "C1", "rgb": "#ff0000"}], "colour list")
    r = e.call("shape.setCells", {"shape": s1, "tool": "paint", "colour": c1, "cells": [[0, 0, 0], [1, 1, 1]]})
    check(r["changed"] == 1, "paint changes only non-empty cells")
    painted = [c for c in e.call("shape.get", {"shape": s1})["cells"] if c.get("colour")]
    check([(c["at"], c["colour"]) for c in painted] == [([0, 0, 0], c1)], "the painted cell")
    e.call("colour.edit", {"colour": c1, "rgb": "#00ff00"})
    check(e.call("colour.list")["colours"][0]["rgb"] == "#00ff00", "colour edited")
    e.fails("colour.add", {"rgb": "red"}, "invalid_params")
    e.fails("colour.edit", {"colour": "c999", "rgb": "#000000"}, "not_found")
    e.call("colour.delete", {"colour": c1})
    check(not any(c.get("colour") for c in e.call("shape.get", {"shape": s1})["cells"]), "colour deleted")

    # the list
    s2 = e.call("shape.duplicate", {"shape": s1})["shape"]
    e.call("shape.rename", {"shape": s2, "name": "copy"})
    e.call("shape.setWeight", {"shape": s2, "weight": 3})
    e.call("shape.setColour", {"shape": s2, "rgb": "#123456"})
    e.call("shape.move", {"shape": s2, "toIndex": 0})
    first = e.call("shape.list")["shapes"][0]
    check(first["id"] == s2 and first["label"] == "S1" and first["name"] == "copy"
          and first["weight"] == 3 and first["colour"] == "#123456", f"the moved copy {first}")
    e.call("shape.setColour", {"shape": s2, "rgb": None})
    check(e.call("shape.list")["shapes"][0]["colour"] is None, "default colour")
    e.fails("shape.move", {"shape": s2, "toIndex": 5}, "invalid_argument")
    e.call("shape.delete", {"shape": s2})
    check([s["id"] for s in e.call("shape.list")["shapes"]] == [s1], "deleted")

    # size
    e.call("shape.resize", {"shape": s1, "size": [5, 4, 4]})
    check(e.call("shape.list")["shapes"][0]["size"] == [5, 4, 4], "resized")
    e.fails("shape.resize", {"shape": s1, "size": [0, 4, 4]}, "invalid_params")
    s3 = e.call("shape.create", {"size": [2, 2, 2]})["shape"]
    e.call("shape.resize", {"shape": s1, "size": [6, 4, 4], "toAll": True})
    sizes = {s["id"]: s["size"] for s in e.call("shape.list")["shapes"]}
    check(sizes[s3] == [3, 2, 2], f"apply to all grows the others {sizes}")


@test
def shape_tasks_and_views(e):
    default = {g["grid"]: g["defaultSize"] for g in e.call("engine.info")["grids"]}
    for grid in ("bricks", "prisms", "spheres", "rhombic", "tetraOcta"):
        e.call("document.new", {"grid": grid})
        tasks = e.call("shape.taskSet")["tasks"]
        check(tasks, f"{grid}: tasks")
        # room for a nudge (rhombic ones move by a block of 5 cells)
        s1 = e.call("shape.create", {"size": [2 * default[grid]] * 3})["shape"]
        size = e.call("shape.list")["shapes"][0]["size"]
        # one cell somewhere on the edge of the grid
        geo = e.call("grid.geometry", {"size": size, "layer": 0})
        check(geo["cells"], f"{grid}: a layer has cells")
        corner = geo["cells"][0]["at"]
        check(e.call("shape.setCells", {"shape": s1, "tool": "fixed", "cells": [corner]})["changed"] == 1,
              f"{grid}: a cell")
        nudges = [t["task"] for t in tasks if t["group"] == "Nudge"]
        results = []
        for t in nudges:
            try:
                results.append(e.call("shape.task", {"shape": s1, "task": t})["changed"])
                e.call("edit.undo")
            except Exception as x:  # noqa: BLE001
                check(getattr(x, "errc", None) == "invalid_argument", f"{grid} {t}: {x}")
                results.append("refused")
        check("refused" in results and True in results, f"{grid}: nudges {results}")
        mesh = e.call("shape.mesh", {"shape": s1})
        f = e.frame(mesh["frame"])
        indices = f.array("indices")
        check(len(indices) == 3 * mesh["triangles"] and mesh["triangles"] > 0, f"{grid}: triangles")
        check(max(indices) < mesh["vertices"], f"{grid}: indices")
        check(len(f.array("positions")) == 3 * mesh["vertices"], f"{grid}: positions")
        check(len(f.array("voxel")) == mesh["triangles"], f"{grid}: per triangle")
        flat = e.call("shape.mesh", {"shape": s1, "style": "flat"})
        check(len(e.frame(flat["frame"]).array("edgeLines")) > 0, f"{grid}: edge lines")
        e.call("shape.mesh", {"shape": s1, "style": "wireframe"})
        info = e.call("shape.info", {"shape": s1})
        check(info["fixed"] == 1 and info["statusLine"], f"{grid}: info {info}")
        check(len(e.call("shape.table")["rows"]) == 1, f"{grid}: the table")
        cells = geo["cells"]
        if grid == "spheres":
            check(all("circle" in c for c in cells), "spheres are circles")
        else:
            for i, c in enumerate(cells):
                check(len(c["neighbours"]) == len(c["polygon"]), f"{grid}: neighbours per edge")
                for n in c["neighbours"]:
                    check(n == -1 or i in cells[n]["neighbours"], f"{grid}: mutual neighbours")
        e.fails("grid.geometry", {"size": size, "layer": size[2]}, "invalid_argument")
        # the cell across each side the mesh draws of the corner cell: a cell
        # of the space, or none past its edge
        across = [e.call("grid.neighbour", {"size": size, "cell": corner, "face": face})["cell"]
                  for face in sorted({fc for fc in f.array("voxelFace") if fc >= 0})]
        check(None in across, f"{grid}: a side towards the outside")
        check(any(c is not None and all(0 <= c[i] < size[i] for i in range(3)) for c in across),
              f"{grid}: a side towards the space {across}")
        e.fails("grid.neighbour", {"size": size, "cell": corner, "face": 99}, "invalid_argument")
        e.fails("grid.neighbour", {"size": size, "cell": [size[0], 0, 0], "face": 0}, "invalid_argument")

    e.call("document.new", {"grid": "bricks"})
    s1 = e.call("shape.create", {"size": [3, 3, 3]})["shape"]
    e.call("shape.setBox", {"shape": s1, "tool": "fixed", "from": [0, 0, 0], "to": [2, 2, 2]})
    e.call("shape.setCells", {"shape": s1, "tool": "erase", "cells": [[1, 1, 1]]})
    r = e.call("shape.task", {"shape": s1, "task": "fillHoles"})
    check(r["changed"] and r["cellsChanged"] == 1, f"fill holes {r}")
    check(e.call("shape.info", {"shape": s1})["fixed"] == 27, "the hole filled")
    r = e.call("shape.task", {"shape": s1, "task": "double"})
    check(e.call("shape.list")["shapes"][0]["size"] == [6, 6, 6], "doubled")
    r = e.call("shape.task", {"shape": s1, "task": "minimize"})
    # by 2, then by 3: a full cube is one cell
    check(e.call("shape.list")["shapes"][0]["size"] == [1, 1, 1], "minimized")
    e.fails("shape.task", {"shape": s1, "task": "nudge27"}, "unsupported")  # not a cube task
    e.fails("shape.task", {"shape": s1, "task": "jump"}, "invalid_params")


# ---- problems ----------------------------------------------------------------------------------

@test
def problems(e):
    e.call("document.new", {"grid": "bricks"})
    box = e.call("shape.create", {"size": [2, 1, 1]})["shape"]
    e.call("shape.setBox", {"shape": box, "tool": "fixed", "from": [0, 0, 0], "to": [1, 0, 0]})
    cube = e.call("shape.create", {"size": [1, 1, 1]})["shape"]
    e.call("shape.setCells", {"shape": cube, "tool": "fixed", "cells": [[0, 0, 0]]})
    made = e.call("problem.create")
    p1 = made["problem"]
    ev = changed_event(e, made["revision"])
    check(ev["problems"] == [p1], f"create's event {ev}")
    check(e.call("problem.fit", {"problem": p1})["verdict"] == "noResult", "no result")
    e.call("problem.setResult", {"problem": p1, "shape": box})
    e.call("problem.setPieceCount", {"problem": p1, "shape": cube, "min": 2, "max": 2})
    fit = e.call("problem.fit", {"problem": p1})
    check(fit["verdict"] == "fits" and fit["piecesMin"] == 2 and fit["statusLine"], f"fit {fit}")
    e.fails("problem.setPieceCount", {"problem": p1, "shape": cube, "min": 3, "max": 2}, "invalid_argument")
    e.fails("problem.setPieceCount", {"problem": p1, "shape": box, "min": 1, "max": 1}, "invalid_argument")
    e.call("problem.addPiece", {"problem": p1, "shape": cube})
    e.call("problem.removePiece", {"problem": p1, "shape": cube})
    detail = e.call("problem.get", {"problem": p1})
    check(detail["result"] == box and detail["parts"] == [{"shape": cube, "min": 2, "max": 2, "groups": [], "required": False}],
          f"detail {detail}")
    check(detail["state"] == "unsolved" and detail["solve"] == "idle" and detail["pieces"] == 2, "state")
    e.call("problem.setGroup", {"problem": p1, "shape": cube, "group": 1})
    check(e.call("problem.get", {"problem": p1})["parts"][0]["groups"] == [{"group": 1, "count": 2}], "group")
    e.call("problem.setGroupCount", {"problem": p1, "shape": cube, "group": 2, "count": 1})
    e.call("problem.deleteGroup", {"problem": p1, "group": 1})
    check(e.call("problem.get", {"problem": p1})["parts"][0]["groups"] == [{"group": 2, "count": 1}], "groups")
    e.fails("problem.setGroup", {"problem": p1, "shape": box, "group": 1}, "invalid_argument")
    e.call("problem.setRangeAll", {"problem": p1, "min": 1, "max": 3})
    check(e.call("problem.get", {"problem": p1})["parts"][0]["max"] == 3, "range")
    e.call("problem.addOneOfEach", {"problem": p1})
    e.call("problem.removeAllPieces", {"problem": p1})
    check(e.call("problem.get", {"problem": p1})["parts"] == [], "no pieces")

    # colours and rules
    red = e.call("colour.add", {"rgb": "#ff0000"})["colour"]
    blue = e.call("colour.add", {"rgb": "#0000ff"})["colour"]
    rules = e.call("problem.get", {"problem": p1})["colourRules"]
    check(rules == [{"piece": red, "result": red}, {"piece": blue, "result": blue}], f"rules {rules}")
    e.call("problem.setColourRule", {"problem": p1, "pieceColour": red, "resultColour": blue, "allowed": True})
    check(len(e.call("problem.get", {"problem": p1})["colourRules"]) == 3, "a rule")
    e.call("problem.setColourRules", {"problem": p1, "preset": "clear"})
    check(e.call("problem.get", {"problem": p1})["colourRules"] == [], "cleared")
    e.call("problem.setColourRules", {"problem": p1, "preset": "allowAll"})
    check(len(e.call("problem.get", {"problem": p1})["colourRules"]) == 4, "all")

    # the stored solver settings
    settings = {"disassemble": True, "justCount": False, "solverType": "bt2", "sortBy": "level"}
    e.call("problem.setSolverSettings", {"problem": p1, "settings": settings})
    check(e.call("problem.get", {"problem": p1})["solverSettings"] == settings, "solver settings")
    e.fails("problem.setSolverSettings", {"problem": p1, "settings": {"disassemble": "yes"}}, "invalid_params")
    check(e.call("problem.kind", {"problem": p1}) == {"kind": "brick", "supported": True}, "kind")

    # the list
    p2 = e.call("problem.duplicate", {"problem": p1})["problem"]
    e.call("problem.rename", {"problem": p2, "name": "second"})
    e.call("problem.move", {"problem": p2, "toIndex": 0})
    listed = e.call("problem.list")["problems"]
    check([p["id"] for p in listed] == [p2, p1] and listed[0]["label"] == "P1"
          and listed[0]["name"] == "second", f"moved {listed}")
    e.call("problem.delete", {"problem": p2})
    check([p["id"] for p in e.call("problem.list")["problems"]] == [p1], "deleted")
    e.fails("problem.get", {"problem": p2}, "not_found")
    e.call("problem.setPieceCount", {"problem": p1, "shape": cube, "min": 1, "max": 1})
    other = e.call("shape.create")["shape"]
    e.call("problem.setPieceCount", {"problem": p1, "shape": other, "min": 1, "max": 1})
    e.call("problem.movePart", {"problem": p1, "shape": other, "delta": -1})
    check(e.call("problem.get", {"problem": p1})["parts"][0]["shape"] == other, "part moved")
    e.fails("problem.movePart", {"problem": p1, "shape": other, "delta": 2}, "invalid_params")

    # a sliding puzzle opens and is solved; a stacking one is not yet
    snap = e.call("document.open", {"path": str(FIXTURES / "test_sliding_solver.xmpuzzle")})
    check(snap["grid"] == "sliding", "sliding opened")
    kind = e.call("problem.kind", {"problem": snap["problems"][0]["id"]})
    check(kind == {"kind": "sliding", "supported": True}, f"sliding kind {kind}")
    snap = e.call("document.open", {"path": str(FIXTURES / "test_stacking_hanoi.xmpuzzle")})
    kind = e.call("problem.kind", {"problem": snap["problems"][0]["id"]})
    check(kind == {"kind": "stacking", "supported": False}, f"stacking kind {kind}")


# ---- solving ----------------------------------------------------------------------------------

def wait_state(e, problem, states, start=0, timeout=600):
    return e.wait_event("solve.state", lambda p: p["problem"] == problem and p["state"] in states,
                        timeout=timeout, start=start)["state"]


def domino_box(e, x, y, z):
    """A box of x*y*z cells to fill with dominoes: many assemblies, all of
    which come apart; the run time grows fast with the size."""
    e.call("document.new", {"grid": "bricks"})
    box = e.call("shape.create", {"size": [x, y, z]})["shape"]
    e.call("shape.setBox", {"shape": box, "tool": "fixed", "from": [0, 0, 0], "to": [x - 1, y - 1, z - 1]})
    d = e.call("shape.create", {"size": [2, 1, 1]})["shape"]
    e.call("shape.setBox", {"shape": d, "tool": "fixed", "from": [0, 0, 0], "to": [1, 0, 0]})
    p = e.call("problem.create")["problem"]
    e.call("problem.setResult", {"problem": p, "shape": box})
    n = x * y * z // 2
    e.call("problem.setPieceCount", {"problem": p, "shape": d, "min": n, "max": n})
    return p, box, d


def sorted_by(rows, key, descending):
    values = [key(r) for r in rows]
    return values == sorted(values, reverse=descending)


@test
def solving_session(e):
    """The scripted session: solve with take-apart, page the solutions
    sorted, play one back, explore, analyse, turn into shapes, remove."""
    snap = e.call("document.open", {"path": str(FIXTURES / "DemoPieceGenerator.xmpuzzle")})
    p = snap["problems"][0]["id"]
    start = len(e.events)
    e.call("solve.start", {"problem": p, "options": {"disassemble": True, "keepAtMost": 0, "sortBy": "moves"}})
    check(wait_state(e, p, ("finished", "error"), start) == "finished", "solved")
    seen = [x["params"]["state"] for x in e.events[start:] if x["method"] == "solve.state"]
    check(seen[-1] == "finished", f"states {seen}")
    e.wait_event("solve.progress", lambda q: q["problem"] == p, start=start)
    e.wait_event("solve.solutionsAdded", lambda q: q["problem"] == p, start=start)
    e.drain()
    status = e.call("solve.status", {"problem": p})
    check(status["state"] == "finished" and status["failure"] is None, f"status {status['state']}")
    detail = e.call("problem.get", {"problem": p})
    check(detail["state"] == "solved" and detail["saved"] == detail["solutions"] > 1, f"solved {detail}")
    # the saved count never goes back (nothing is thinned here) and the
    # last solutionsAdded of the run carries the final count
    added = [x["params"] for x in e.events[start:]
             if x["method"] == "solve.solutionsAdded" and x["params"]["problem"] == p]
    check(all(a["saved"] <= b["saved"] for a, b in zip(added, added[1:])), f"saved counts {added}")
    check(added[-1]["saved"] == detail["saved"], f"the last solutionsAdded's count {added[-1]}")
    # the run's end came with a revision of its own (after solve.start's),
    # after the run's last solve.state
    changes = [x["params"] for x in e.events[start:] if x["method"] == "document.changed"]
    check(len(changes) >= 2 and changes[-1]["revision"] == e.call("document.get")["revision"]
          and p in changes[-1]["problems"], f"the run's end {changes}")
    order = [x["method"] for x in e.events[start:]
             if x["method"] in ("solve.state", "document.changed")]
    check(order[-1] == "document.changed" and order[-2] == "solve.state", f"the end's order {order}")
    total = detail["saved"]
    # a finished solve is not aborted (nothing is under way): the solutions stay
    e.fails("solve.abort", {"problem": p}, "invalid_state")
    check(e.call("problem.get", {"problem": p})["saved"] == total, "the solutions stay")

    # paging and sorting (a view: the stored order stays)
    page = e.call("solution.page", {"problem": p, "sortBy": "moves", "descending": True, "limit": total})
    check(page["total"] == total and len(page["rows"]) == total, "one page of all")
    check(page["solutionsVersion"] == added[-1]["solutionsVersion"], "the page is of the version told")
    check(sorted_by(page["rows"], lambda r: r["moves"], True), "by moves, descending")
    for key, field in (("level", None), ("assembly", "assembly"), ("pieces", None), ("list", "index")):
        rows = e.call("solution.page", {"problem": p, "sortBy": key, "limit": total})["rows"]
        check(len(rows) == total, f"sorted by {key}")
        if field:
            check(sorted_by(rows, lambda r: r[field], False), f"sorted by {key}")
    first = e.call("solution.page", {"problem": p, "limit": 5})
    second = e.call("solution.page", {"problem": p, "offset": 5, "limit": 5})
    check([r["index"] for r in first["rows"] + second["rows"]] == list(range(10)), "pages follow on")
    check(e.call("solution.page", {"problem": p, "offset": total})["rows"] == [], "past the end")
    row = page["rows"][0]
    check(row["disassembly"] == "tree" and row["movesText"], f"a row {row}")
    got = e.call("solution.get", {"problem": p, "solution": row["id"]})
    check(got["row"] == row and len(got["placements"]) == detail["pieces"], "solution.get")
    e.fails("solution.get", {"problem": p, "solution": "sol99999"}, "not_found")
    # a placed piece is drawn as its shape's mesh in the placement's transformation
    placed = got["placements"][0]
    shape = detail["parts"][0]["shape"]  # pieces are numbered part by part
    plain = e.call("shape.mesh", {"shape": shape})
    turned = e.call("shape.mesh", {"shape": shape, "transformation": placed["transformation"]})
    check(turned["triangles"] == plain["triangles"] and turned["vertices"] == plain["vertices"],
          "the turned shape has the same faces")
    flat = e.call("shape.mesh", {"shape": shape, "transformation": 1, "style": "flat"})
    check(e.frame(flat["frame"]).array("edgeLines"), "edge lines in an orientation")
    e.fails("shape.mesh", {"shape": shape, "transformation": 48}, "invalid_argument")

    # playback
    pb = e.call("playback.open", {"problem": p, "solution": row["id"]})
    n = len(pb["pieces"])
    check(pb["steps"] > 0 and n == detail["pieces"], f"playback {pb['steps']} {n}")
    mesh = e.call("playback.mesh", {"handle": pb["handle"], "piece": 0})
    check(e.frame(mesh["frame"]).array("indices"), "a piece mesh")
    poses = e.call("playback.poses", {"handle": pb["handle"], "samplesPerStep": 4})
    f = e.frame(poses["frame"])
    samples = pb["steps"] * 4 + 1
    check(poses["samples"] == samples and poses["pieces"] == n, "pose counts")
    check(len(f.array("translation")) == samples * n * 3 and len(f.array("rotation")) == samples * n * 4,
          "pose arrays")
    steps = f.array("step")
    check(steps[0] == 0 and abs(steps[-1] - pb["steps"]) < 1e-6, "steps")
    check(all(v == 1 for v in f.array("visible")[:n]), "all pieces shown assembled")
    assembled = f.array("translation")[:n * 3]
    # the assembled view without the player: each piece's mesh in its
    # placement's transformation, at the cell minus that mesh's hotspot (on
    # bricks a cell is a mesh unit), is where the player's first pose puts
    # it, relative to the result's centre
    size = next(x["size"] for x in snap["shapes"] if x["id"] == detail["result"])
    at = {q["piece"]: q for q in got["placements"]}
    for i, piece in enumerate(pb["pieces"]):
        q = at[piece["piece"]]
        check(q["placed"] and q["transformation"] == piece["orientation"], f"piece {piece}")
        drawn = e.call("shape.mesh", {"shape": piece["shape"], "transformation": q["transformation"]})
        played = e.call("playback.mesh", {"handle": pb["handle"], "piece": i})
        check(drawn["hotspot"] == played["hotspot"], f"one hotspot {drawn['hotspot']} {played['hotspot']}")
        want = [q["at"][k] - drawn["hotspot"][k] - size[k] / 2 for k in range(3)]
        check(all(abs(assembled[3 * i + k] - want[k]) < 1e-5 for k in range(3)),
              f"piece {i} at {assembled[3 * i:3 * i + 3]}, drawn at {want}")
    track = e.call("playback.track", {"handle": pb["handle"], "samplesPerStep": 2, "reassemble": False})
    times = e.frame(track["frame"]).array("time")
    check(all(a <= b for a, b in zip(times, times[1:])) and len(times) == track["samples"], "track times")
    part = e.call("playback.poses", {"handle": pb["handle"], "from": 1, "to": 2, "samplesPerStep": 1})
    check(part["samples"] == 2, "a part of the poses")
    e.fails("playback.poses", {"handle": pb["handle"], "from": 2, "to": 1}, "invalid_argument")
    e.fails("playback.mesh", {"handle": pb["handle"], "piece": n}, "invalid_argument")
    e.call("playback.mesh", {"handle": pb["handle"], "piece": 0, "style": "wireframe"})
    e.call("playback.close", {"handle": pb["handle"]})
    e.fails("playback.close", {"handle": pb["handle"]}, "not_found")

    # the explorers
    ax = e.call("explore.openAssembly", {"problem": p})
    check(ax["pieces"] == detail["pieces"], "assembly explorer")
    if ax["placementsSupported"]:
        check(e.call("explore.placements", {"handle": ax["handle"], "piece": 0})["placements"], "placements")
    stepped = e.call("explore.step", {"handle": ax["handle"], "steps": 100})
    check(len(stepped["placements"]) == detail["pieces"] and 0 <= stepped["finished"] <= 1, "a step")
    e.call("explore.step", {"handle": ax["handle"]})
    e.call("explore.close", {"handle": ax["handle"]})
    mx = e.call("explore.openMovements", {"problem": p, "solution": row["id"]})
    root = mx["root"]
    check(root["node"] == root["parent"] and len(root["pieces"]) == detail["pieces"], f"root {root['node']}")
    check(mx["directions"], "directions")
    check(len(mx["directions"]) == 2 * len(mx["vectors"]), "a vector per direction")
    children = e.call("explore.analyse", {"handle": mx["handle"], "node": root["node"]})["nodes"]
    check(children, "moves from the start")
    child = children[0]
    check(e.call("explore.node", {"handle": mx["handle"], "node": root["node"]})["children"], "the root's children")
    check(e.call("explore.node", {"handle": mx["handle"], "node": child["node"]}) == child, "explore.node")
    moved = e.call("explore.addMove", {"handle": mx["handle"], "node": root["node"],
                                       "pieces": [root["pieces"][0]["piece"]], "direction": 0})
    check(moved["parent"] == root["node"], "a move by hand")
    # a piece dragged one step: some piece can move some way
    found = []
    for q in root["pieces"]:
        for delta in [v for d in mx["vectors"] for v in (d, [-k for k in d])]:
            try:
                found.append(e.call("explore.findMatching", {"handle": mx["handle"], "node": root["node"],
                                                             "piece": q["piece"], "delta": delta}))
            except Exception as x:  # noqa: BLE001
                check(getattr(x, "errc", None) == "not_found", f"findMatching: {x}")
    check(found and all(n["parent"] == root["node"] for n in found), "matching moves")
    e.fails("explore.findMatching", {"handle": mx["handle"], "node": root["node"], "piece": 999,
                                     "delta": [1, 0, 0]}, "invalid_argument")
    e.call("explore.prune", {"handle": mx["handle"], "node": child["node"]})
    e.fails("explore.node", {"handle": mx["handle"], "node": moved["node"]}, "not_found")
    e.call("explore.close", {"handle": mx["handle"]})
    e.fails("explore.close", {"handle": mx["handle"]}, "not_found")
    e.fails("explore.close", {"handle": "pb1"}, "invalid_params")

    # take apart afterwards
    e.call("solution.deleteDisassembly", {"problem": p})
    rows = e.call("solution.page", {"problem": p, "limit": total})["rows"]
    check(all(r["disassembly"] == "summary" for r in rows), "only the summaries left")
    # a solution without its moves still plays: the assembled pose alone
    pb = e.call("playback.open", {"problem": p, "solution": row["id"]})
    check(pb["steps"] == 0 and pb["movesText"] is None and len(pb["pieces"]) == n, f"a single pose {pb}")
    poses = e.call("playback.poses", {"handle": pb["handle"]})
    f = e.frame(poses["frame"])
    check(poses["samples"] == 1 and all(v == 1 for v in f.array("visible")), "assembled, all shown")
    check(all(abs(a - b) < 1e-5 for a, b in zip(f.array("translation"), assembled)),
          "the same pose as step 0 of the disassembly")
    check(e.call("playback.mesh", {"handle": pb["handle"], "piece": 0, "style": "flat"})["triangles"] > 0,
          "a piece mesh")
    e.call("playback.close", {"handle": pb["handle"]})
    start = len(e.events)
    r = e.call("solution.analyse", {"problem": p, "which": "missing"})
    check(r["total"] == total, f"to analyse {r}")
    end = e.wait_event("solution.analyseEnded", lambda q: q["problem"] == p, start=start)
    check(end["done"] == total and end["separated"] == total and not end["stopped"]
          and end["failure"] is None, f"analysed {end}")
    e.wait_event("document.changed", lambda q: p in q["problems"], start=start, timeout=10)
    rows = e.call("solution.page", {"problem": p, "limit": total})["rows"]
    check(all(r["disassembly"] == "tree" for r in rows), "the moves are back")
    check(e.call("solution.analyseStatus", {"problem": p})["active"] is False, "nothing running")
    e.fails("solution.analyseStop", {"problem": p}, "invalid_state")
    start = len(e.events)
    e.call("solution.analyse", {"problem": p, "which": "one", "solution": row["id"], "rotations": True})
    e.wait_event("solution.analyseEnded", lambda q: q["problem"] == p, start=start)
    e.fails("solution.analyse", {"problem": p, "which": "one"}, "invalid_argument")
    # an analysis stopped at once keeps what it finished
    start = len(e.events)
    e.call("solution.analyse", {"problem": p, "which": "all", "threads": 1})
    e.call("solution.analyseStatus", {"problem": p})
    try:
        e.call("solution.analyseStop", {"problem": p})
    except Exception as x:  # noqa: BLE001  (it may have ended already)
        check(getattr(x, "errc", None) == "invalid_state", f"stop: {x}")
    end = e.wait_event("solution.analyseEnded", lambda q: q["problem"] == p, start=start)
    check(end["stopped"] == (end["done"] < end["total"]), f"stopped {end}")

    # assemblies to shapes, then undone
    shapes_before = len(e.call("shape.list")["shapes"])
    made = e.call("solution.toShapes", {"problem": p, "solution": row["id"], "addTo": "newProblem"})
    check(len(made["shapes"]) == 1, "a shape from the assembly")
    check(len(e.call("shape.list")["shapes"]) == shapes_before + 1, "added")
    e.call("edit.undo")
    check(len(e.call("shape.list")["shapes"]) == shapes_before, "undone")

    # removing solutions: each removal is a new version of the list
    page = e.call("solution.page", {"problem": p, "limit": total})
    ids = [r["id"] for r in page["rows"]]
    e.call("solution.delete", {"problem": p, "which": "one", "solution": ids[1]})
    check(e.call("solution.page", {"problem": p})["solutionsVersion"] > page["solutionsVersion"],
          "the version moved")
    e.call("solution.delete", {"problem": p, "which": "before", "solution": ids[2]})
    e.call("solution.delete", {"problem": p, "which": "after", "solution": ids[4]})
    left = [r["id"] for r in e.call("solution.page", {"problem": p})["rows"]]
    check(left == [ids[2], ids[3], ids[4]], f"left {left}")
    e.call("solution.delete", {"problem": p, "which": "withoutDisassembly"})
    e.fails("solution.delete", {"problem": p, "which": "one"}, "invalid_argument")
    e.call("solution.delete", {"problem": p, "which": "all"})
    check(e.call("solution.page", {"problem": p})["total"] == 0, "all removed")
    e.call("edit.undo")
    check(e.call("solution.page", {"problem": p})["total"] == 3, "removing is undone")

    # saved and opened again, the solutions are there
    with tempfile.TemporaryDirectory() as tmp:
        path = str(pathlib.Path(tmp) / "solved.xmpuzzle")
        e.call("document.save", {"path": path})
        again = e.call("document.open", {"path": path})
        check(again["problems"][0]["saved"] == 3, "saved solutions")


@test
def pause_continue_and_the_guards(e):
    # a 2x4x6 box on one thread runs for many seconds: the pause lands
    p, box, d = domino_box(e, 2, 4, 6)
    one = {"disassemble": True, "keepAtMost": 5, "threads": 1}
    other = e.call("problem.create")["problem"]
    c1 = e.call("colour.add", {"rgb": "#ff0000"})["colour"]
    e.fails("solve.abort", {"problem": p}, "invalid_state")  # nothing under way
    start = len(e.events)
    e.call("solve.start", {"problem": p, "options": one})
    state = wait_state(e, p, ("assembling", "finished"), start)
    check(state == "assembling", f"the run is long enough to be seen assembling ({state})")
    # frozen while a run is active (decision 0011)
    e.fails("shape.setCells", {"shape": d, "tool": "erase", "cells": [[0, 0, 0]]}, "invalid_state")
    e.fails("shape.create", {}, "invalid_state")
    e.fails("problem.create", {}, "invalid_state")
    e.fails("problem.rename", {"problem": p, "name": "x"}, "invalid_state")
    e.fails("edit.undo", {}, "invalid_state")
    e.fails("document.save", {"path": "/tmp/never.xmpuzzle"}, "invalid_state")
    e.fails("document.saveBytes", {}, "invalid_state")
    e.fails("solve.start", {"problem": p}, "invalid_state")
    e.fails("solution.analyse", {"problem": p, "which": "all"}, "invalid_state")
    e.fails("solve.exportPaused", {"problem": p, "path": "/tmp/never.btsolve"}, "invalid_state")
    # and what goes on
    e.call("document.setNotes", {"text": "while solving"})
    e.call("colour.edit", {"colour": c1, "rgb": "#00ff00"})
    e.call("problem.rename", {"problem": other, "name": "not solving"})
    e.call("shape.mesh", {"shape": d})
    e.call("problem.get", {"problem": p})
    e.call("solution.page", {"problem": p})
    check(e.call("document.get")["solving"], "the snapshot says so")
    e.call("solve.pause", {"problem": p})
    state = wait_state(e, p, ("paused", "finished"), start)
    check(state == "paused", f"the pause landed before the run ended ({state})")
    detail = e.call("problem.get", {"problem": p})
    check(detail["state"] == "solving" and detail["savedPosition"] and detail["solve"] == "paused", "paused")
    progress = e.call("solve.status", {"problem": p})["progress"]
    check(progress["stage"] == "paused", f"stage {progress['stage']}")
    with tempfile.TemporaryDirectory() as tmp:
        e.call("document.save", {"path": str(pathlib.Path(tmp) / "paused.xmpuzzle")})
    # carry on, pause at a save point, carry on, abort
    start = len(e.events)
    e.call("solve.continue", {"problem": p, "options": one})
    check(wait_state(e, p, ("assembling", "finished"), start) == "assembling", "carried on")
    e.call("solve.savePoint", {"problem": p})
    check(wait_state(e, p, ("paused", "finished"), start) == "paused", "paused at a save point")
    start = len(e.events)
    e.call("solve.continue", {"problem": p, "options": one})
    check(wait_state(e, p, ("assembling", "draining", "finished"), start) != "finished", "carried on again")
    r = e.call("solve.abort", {"problem": p})
    wait_state(e, p, ("idle",), start, timeout=30)
    detail = e.call("problem.get", {"problem": p})
    check(detail["state"] == "unsolved" and detail["saved"] == 0 and detail["solve"] == "idle", "aborted")
    # the abort's own document.changed, then the run's end from the poll
    e.drain()
    revisions = [x["params"]["revision"] for x in e.events[start:]
                 if x["method"] == "document.changed" and x["params"]["revision"] >= r["revision"]]
    check(len(revisions) == 2 and revisions[0] == r["revision"] and revisions[1] > r["revision"],
          f"two document.changed after an abort {revisions}")
    e.fails("solve.pause", {"problem": p}, "invalid_state")
    e.fails("solve.abort", {"problem": p}, "invalid_state")


@test
def prepare_export_import(e):
    p, _, _ = domino_box(e, 2, 4, 4)
    e.fails("solve.exportPaused", {"problem": p, "path": "/tmp/never.btsolve"}, "invalid_state")
    start = len(e.events)
    e.call("solve.prepare", {"problem": p})
    check(wait_state(e, p, ("paused", "error"), start) == "paused", "prepared")
    with tempfile.TemporaryDirectory() as tmp:
        path = str(pathlib.Path(tmp) / "carry.btsolve")
        e.call("solve.exportPaused", {"problem": p, "path": path})
        check(pathlib.Path(path).read_bytes().startswith(b"BTSOLVE2"), "the file's magic")
        imported = e.call("solve.importPaused", {"path": path})
        q = imported["problem"]
        doc = imported["document"]
        check(doc["path"] is None and doc["dirty"], "a new, untitled document")
        check([x["id"] for x in doc["problems"]] == [q] and doc["problems"][0]["state"] == "solving", "paused")
        start = len(e.events)
        e.call("solve.continue", {"problem": q, "options": {"disassemble": True}})
        check(wait_state(e, q, ("finished", "error"), start) == "finished", "carried on")
        detail = e.call("problem.get", {"problem": q})
        check(detail["assemblies"] == 2856 and detail["solutions"] == 2856, f"the whole search {detail}")
        e.fails("solve.importPaused", {"path": str(FIXTURES / "PelikanBurr.xmpuzzle")}, "parse_error")
        e.fails("solve.importPaused", {"path": str(pathlib.Path(tmp) / "none.btsolve")}, "io_error")


@test
def document_replaced_while_working(e):
    """A new document ends the old one's runs and analyses: after the
    reply their problems get a last event, then nothing more of them."""
    p, _, _ = domino_box(e, 2, 4, 4)
    start = len(e.events)
    e.call("solve.start", {"problem": p, "options": {"disassemble": True, "keepAtMost": 0, "threads": 2}})
    check(wait_state(e, p, ("finished", "error"), start) == "finished", "solved")
    e.drain()
    # an analysis of this problem and a solve of a copy, under way
    q = e.call("problem.duplicate", {"problem": p})["problem"]
    start = len(e.events)
    check(e.call("solution.analyse", {"problem": p, "which": "all", "threads": 1})["total"] == 2856, "analysing")
    e.call("solve.start", {"problem": q, "options": {"disassemble": True, "threads": 1}})
    wait_state(e, q, ("assembling", "finished"), start)
    start = len(e.events)
    snap = e.call("document.new", {"grid": "bricks"})
    check(snap["problems"] == [] and not snap["solving"], "the new document")
    ended = e.wait_event("solution.analyseEnded", lambda x: x["problem"] == p, start=start, timeout=10)
    check(ended["stopped"] and ended["done"] <= ended["total"] == 2856, f"the analysis ended {ended}")
    e.wait_event("solve.state", lambda x: x["problem"] == q and x["state"] == "idle", start=start, timeout=10)
    e.drain()
    after = [x["method"] for x in e.events[start:]]
    check("document.changed" not in after, f"no document.changed for the replacement {after}")
    e.fails("solve.status", {"problem": q}, "not_found")
    e.fails("solution.analyseStatus", {"problem": p}, "not_found")


@test
def sliding_solve_pause_continue_and_play(e):
    with tempfile.TemporaryDirectory() as tmp:
        # a full search's levels in a folder of the test's own
        was = e.call("settings.get")["slideFolder"]
        e.call("settings.set", {"slideFolder": tmp})
        snap = e.call("document.open", {"path": str(FIXTURES / "test_sliding_solver.xmpuzzle")})
        p = snap["problems"][0]["id"]
        start = len(e.events)
        e.call("solve.start", {"problem": p, "options": {"disassemble": True,
                                                          "sliding": {"solver": "full"}}})
        check(wait_state(e, p, ["finished", "error"], start) == "finished", "the sliding solve finished")
        st = e.call("solve.status", {"problem": p})
        check(st["note"] is None and st["progress"]["solutions"] == 1, f"its status {st}")
        page = e.call("solution.page", {"problem": p})
        check(len(page["rows"]) == 1 and page["rows"][0]["moves"] == 81, f"the solution {page['rows']}")
        pb = e.call("playback.open", {"problem": p, "solution": page["rows"][0]["id"]})
        check(pb["steps"] == 81 and pb["movesText"] == "81", f"the playback {pb}")
        poses = e.call("playback.poses", {"handle": pb["handle"], "from": 8, "to": 10, "samplesPerStep": 4})
        check(poses["samples"] == 9 and poses["pieces"] == 10, f"the poses {poses}")
        e.call("playback.close", {"handle": pb["handle"]})
        e.fails("solution.analyse", {"problem": p, "which": "all"}, "unsupported")

        # the nested puzzle: no solution without nested slides, and the note says so
        snap = e.call("document.open", {"path": str(FIXTURES / "sliding" / "test_sliding_nested.xmpuzzle")})
        p = snap["problems"][0]["id"]
        start = len(e.events)
        e.call("solve.start", {"problem": p, "options": {"disassemble": True}})
        check(wait_state(e, p, ["finished", "error"], start) == "finished", "the nested solve finished")
        note = e.call("solve.status", {"problem": p})["note"]
        check(note is not None and "try Allow Nested Slides" in note, f"the note {note!r}")
        start = len(e.events)
        e.call("solve.start", {"problem": p, "options": {"disassemble": True,
                                                          "sliding": {"nestedSlides": True}}})
        check(wait_state(e, p, ["finished", "error"], start) == "finished", "the nested solve finished")
        check(e.call("solution.page", {"problem": p})["rows"][0]["moves"] == 47, "47 moves nested")
        e.fails("solve.start", {"problem": p, "options": {"sliding": {"solver": "bfs"}}}, "invalid_params")
        e.fails("solve.start", {"problem": p, "options": {"sliding": {"depth": 3}}}, "invalid_params")

        # paused in the middle of a search, carried on: the deep search's limit,
        # with its note
        snap = e.call("document.open",
                      {"path": str(FIXTURES / "sliding" / "slide_swapped_fifteen.xmpuzzle")})
        p = snap["problems"][0]["id"]
        deep = {"disassemble": True, "sliding": {"solver": "deep"}}
        start = len(e.events)
        e.call("solve.start", {"problem": p, "options": deep})
        e.wait_event("solve.progress", lambda x: x["problem"] == p and x["progress"]["stage"] == "search"
                     and x["progress"]["searched"] > 0, start=start, timeout=600)
        e.call("solve.pause", {"problem": p})
        state = wait_state(e, p, ["paused", "finished", "error"], start)
        if state == "paused":
            # the start under way went back to be searched again: not counted yet
            check(e.call("problem.get", {"problem": p})["assemblies"] == 0, "the start not counted")
            start = len(e.events)
            e.call("solve.continue", {"problem": p, "options": deep})
            state = wait_state(e, p, ["finished", "error"], start)
        check(state == "finished", f"carried on to its end: {state}")
        note = e.call("solve.status", {"problem": p})["note"]
        check(note is not None and "limit of 1,000,000 arrangements" in note, f"the note {note!r}")
        e.call("settings.set", {"slideFolder": was})


@test
def sliding_start_goal_shapes(e):
    e.call("document.new", {"grid": "sliding"})
    # two pieces: one cell, and two cells side by side
    one = e.call("shape.create", {"size": [1, 1, 1]})["shape"]
    e.call("shape.setCells", {"shape": one, "tool": "fixed", "cells": [[0, 0, 0]]})
    two = e.call("shape.create", {"size": [2, 1, 1]})["shape"]
    e.call("shape.setCells", {"shape": two, "tool": "fixed", "cells": [[0, 0, 0], [1, 0, 0]]})
    made = e.call("sliding.addStartGoal", {"size": [4, 3]})
    tray = made["shape"]
    snap = e.call("document.get")
    row = next(s for s in snap["shapes"] if s["id"] == tray)
    check(row["startGoal"] == 1 and row["name"] == "" and row["size"] == [4, 3, 1], f"the tray {row}")
    check(all(s["startGoal"] is None for s in snap["shapes"] if s["id"] != tray), "pieces have no number")
    check(len(snap["problems"]) == 1, f"its problem {snap['problems']}")
    p = snap["problems"][0]["id"]
    e.call("shape.rename", {"shape": tray, "name": "Corner"})
    # one layer deep: no task that adds layers, no other Z, no piece named as a tray
    tasks = {t["task"] for t in e.call("shape.taskSet")["tasks"]}
    deep = {"nudge4", "nudge5", "rotate6", "rotate7", "rotate8", "rotate9", "double", "triple", "gridScale"}
    check(not tasks & deep and {"nudge0", "nudge1", "rotate10", "rotate11"} <= tasks, f"flat tasks {tasks}")
    e.fails("shape.task", {"shape": two, "task": "rotate6"}, "unsupported")
    e.fails("shape.resize", {"shape": two, "size": [2, 1, 2]}, "invalid_argument")
    e.fails("shape.rename", {"shape": two, "name": "sg:2:"}, "invalid_argument")
    t = e.call("sliding.tray", {"tray": tray})
    check(t["number"] == 1 and t["name"] == "Corner" and t["floor"] == 12, f"the tray {t}")
    check(t["starts"] == [None] * 12 and t["goals"] == [None] * 12, "nothing stamped")
    check(t["error"] is not None and t["overflow"] is None, f"not solvable yet {t}")
    # a wall in the top-right corner (cells are x + 4*y, y up)
    e.call("shape.setCells", {"shape": tray, "tool": "erase", "cells": [[3, 2, 0]]})
    check(e.call("sliding.tray", {"tray": tray})["floor"] == 11, "a wall")

    # the editor's preview and click
    pv = e.call("sliding.preview", {"tray": tray, "piece": two, "at": [2, 2], "goal": False})
    check(pv["cells"] == [[2, 2], [3, 2]] and not pv["fits"] and pv["why"] and not pv["there"],
          f"onto the wall {pv}")
    e.fails("sliding.place", {"tray": tray, "piece": two, "at": [2, 2], "goal": False}, "invalid_argument")
    pv = e.call("sliding.preview", {"tray": tray, "piece": two, "at": [0, 0], "goal": False})
    check(pv["fits"] and pv["why"] is None and not pv["there"], f"fits {pv}")
    placed = e.call("sliding.place", {"tray": tray, "piece": two, "at": [0, 0], "goal": False})
    check(placed["placed"], "placed")
    ch = changed_event(e, placed["revision"])
    check("problems" in ch["what"] and "shapes" in ch["what"] and "notice" not in ch, f"the event {ch}")
    check(e.call("sliding.preview", {"tray": tray, "piece": two, "at": [0, 0], "goal": False})["there"],
          "the piece is there")
    e.call("sliding.place", {"tray": tray, "piece": two, "at": [1, 2], "goal": True})
    e.call("sliding.place", {"tray": tray, "piece": one, "at": [3, 0], "goal": False})
    t = e.call("sliding.tray", {"tray": tray})
    check(t["starts"][0] == two and t["starts"][1] == two and t["starts"][3] == one, f"starts {t['starts']}")
    check(t["goals"][9] == two and t["goals"][10] == two and t["error"] is None, f"goals {t}")
    parts = {x["shape"]: x for x in e.call("problem.get", {"problem": p})["parts"]}
    check(parts[two]["required"] and parts[one]["required"], f"required parts {parts}")
    e.fails("problem.setPieceCount", {"problem": p, "shape": two, "min": 0, "max": 0}, "invalid_argument")
    e.fails("problem.setResult", {"problem": p, "shape": one}, "invalid_argument")
    # the problem buttons and the colour group the original greyed out: the
    # problems follow the start/goal shapes, the cell colours are the marks
    e.fails("problem.create", {}, "invalid_argument")
    e.fails("problem.duplicate", {"problem": p}, "invalid_argument")
    e.fails("problem.delete", {"problem": p}, "invalid_argument")
    colours = e.call("document.get")["colours"]
    check(len(colours) >= 2, f"the locks' colours {colours}")
    e.fails("colour.delete", {"colour": colours[0]["id"]}, "invalid_argument")
    check(len(e.call("document.get")["problems"]) == 1, "one problem still")
    # clicking where it is takes it off; undo puts it back
    off = e.call("sliding.place", {"tray": tray, "piece": one, "at": [3, 0], "goal": False})
    check(not off["placed"], "taken off")
    e.call("edit.undo")
    check(e.call("sliding.tray", {"tray": tray})["starts"][3] == one, "undone")
    # the right click
    check(not e.call("sliding.clearAt", {"tray": tray, "at": [2, 1], "goal": False})["cleared"], "nothing there")
    check(e.call("sliding.clearAt", {"tray": tray, "at": [3, 0], "goal": False})["cleared"], "cleared")
    parts = {x["shape"]: x for x in e.call("problem.get", {"problem": p})["parts"]}
    check(not parts[one]["required"], f"no longer required {parts[one]}")

    # an edit that changes a piece's shape takes its stamps off, and says so
    r = e.call("shape.setCells", {"shape": two, "tool": "erase", "cells": [[1, 0, 0]]})
    ch = changed_event(e, r["revision"])
    check(ch.get("notice") == "S2 changed shape: its start and goal were removed from S3.", f"the notice {ch}")
    t = e.call("sliding.tray", {"tray": tray})
    check(t["starts"] == [None] * 12 and t["goals"] == [None] * 12, "its stamps are gone")

    # refusals
    e.fails("sliding.tray", {"tray": one}, "invalid_argument")
    e.fails("sliding.place", {"tray": one, "piece": two, "at": [0, 0], "goal": False}, "invalid_argument")
    e.fails("sliding.place", {"tray": tray, "piece": tray, "at": [0, 0], "goal": False}, "invalid_argument")
    e.fails("sliding.preview", {"tray": tray, "piece": tray, "at": [0, 0], "goal": False}, "invalid_argument")
    e.fails("sliding.clearAt", {"tray": one, "at": [0, 0], "goal": True}, "invalid_argument")
    e.fails("sliding.tray", {"tray": "s999"}, "not_found")
    e.fails("sliding.addStartGoal", {"size": [0, 3]}, "invalid_params")
    e.fails("sliding.place", {"tray": tray, "piece": one, "at": [0, 0, 0], "goal": False}, "invalid_params")

    # opening a file whose sliding shape is deeper than a layer flattens it
    deep = e.call("shape.create", {"size": [1, 1, 2]})["shape"]
    e.call("shape.setCells", {"shape": deep, "tool": "fixed", "cells": [[0, 0, 0], [0, 0, 1]]})
    data = e.call("document.saveBytes")["data"]
    snap = e.call("document.openBytes", {"data": data})
    check(snap.get("notice", "").startswith("Sliding shapes are one layer deep. S4 had more than one layer")
          and snap["dirty"], f"the open notice {snap.get('notice')!r}")
    check("notice" not in e.call("document.get"), "only the opening tells it")

    # other grids have none
    e.call("document.new", {"grid": "bricks"})
    e.fails("sliding.addStartGoal", {}, "unsupported")
    s = e.call("shape.create")["shape"]
    e.fails("sliding.tray", {"tray": s}, "unsupported")
    e.fails("sliding.place", {"tray": s, "piece": s, "at": [0, 0], "goal": False}, "unsupported")
    e.fails("sliding.clearAt", {"tray": s, "at": [0, 0], "goal": False}, "unsupported")
    e.fails("sliding.preview", {"tray": s, "piece": s, "at": [0, 0], "goal": False}, "unsupported")
    check(e.call("document.get")["shapes"][0]["startGoal"] is None, "no start/goal shapes")


@test
def unsupported_and_failures(e):
    # a stacking puzzle is not solved yet, nor its placements explored
    snap = e.call("document.open", {"path": str(FIXTURES / "test_stacking_hanoi.xmpuzzle")})
    p = snap["problems"][0]["id"]
    e.fails("solve.start", {"problem": p}, "unsupported")
    e.fails("solve.continue", {"problem": p}, "unsupported")
    e.fails("explore.openAssembly", {"problem": p}, "unsupported")
    # spheres have no take-apart
    snap = e.call("document.open", {"path": str(FIXTURES / "BallRoom_solved.xmpuzzle")})
    p = snap["problems"][0]["id"]
    sol = e.call("solution.page", {"problem": p})["rows"][0]["id"]
    e.fails("solution.analyse", {"problem": p, "which": "all"}, "unsupported")
    e.fails("explore.openMovements", {"problem": p, "solution": sol}, "unsupported")
    # a solution that cannot have a disassembly still shows assembled
    pb = e.call("playback.open", {"problem": p, "solution": sol})
    check(pb["steps"] == 0 and pb["movesText"] is None, f"a single pose {pb}")
    e.call("playback.close", {"handle": pb["handle"]})
    e.fails("solve.start", {"problem": p, "options": {"threads": 999}}, "invalid_params")
    e.fails("solve.start", {"problem": p, "options": {"sortBy": "colour"}}, "invalid_params")
    e.fails("solve.start", {"problem": p, "options": 5}, "invalid_params")
    e.fails("solve.status", {"problem": "p999"}, "not_found")
    # a problem whose pieces cannot fill the result: the run ends in error
    e.call("document.new", {"grid": "bricks"})
    big = e.call("shape.create", {"size": [2, 2, 2]})["shape"]
    e.call("shape.setBox", {"shape": big, "tool": "fixed", "from": [0, 0, 0], "to": [1, 1, 1]})
    small = e.call("shape.create", {"size": [1, 1, 1]})["shape"]
    e.call("shape.setCells", {"shape": small, "tool": "fixed", "cells": [[0, 0, 0]]})
    q = e.call("problem.create")["problem"]
    e.call("problem.setResult", {"problem": q, "shape": big})
    e.call("problem.setPieceCount", {"problem": q, "shape": small, "min": 3, "max": 3})
    start = len(e.events)
    e.call("solve.start", {"problem": q})
    check(wait_state(e, q, ("error", "finished"), start) == "error", "an error")
    err = e.wait_event("solve.error", lambda x: x["problem"] == q, start=start)
    check(err["failure"]["assembler"] == "tooFewUnits", f"the failure {err['failure']}")
    status = e.call("solve.status", {"problem": q})
    check(status["state"] == "error" and status["failure"]["message"], "status after an error")


@test
def shutdown(e):
    e.call("engine.shutdown")
    try:
        e.proc.wait(timeout=60)
    except Exception as x:  # noqa: BLE001
        raise ContractError("the engine did not end after engine.shutdown") from x
    check(e.proc.returncode == 0, f"status {e.proc.returncode}")


# ---- the runner --------------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--engine", required=True)
    ap.add_argument("names", nargs="*")
    args = ap.parse_args()
    api = Api()
    problems = api.self_check()
    if problems:
        print("the schema has problems:\n" + "\n".join(problems))
        return 1
    failed = 0
    called = set()
    selected = [t for t in TESTS if not args.names or t.__name__ in args.names]
    for t in selected:
        e = Engine(args.engine, api)
        try:
            t(e)
            if e.proc.poll() is None:
                e.drain(0.1)
            e.close()
            print(f"PASS {t.__name__}")
        except Exception:  # noqa: BLE001
            failed += 1
            print(f"FAIL {t.__name__}")
            traceback.print_exc()
            if e.stderr:
                print("engine stderr:\n" + "".join(e.stderr[-30:]))
            if e.proc.poll() is None:
                e.proc.kill()
        called |= e.called
    if not args.names:
        # every method of the schema has been called with success at least once
        uncalled = sorted(set(api.methods) - called)
        if uncalled:
            failed += 1
            print(f"FAIL coverage: methods never called with success: {uncalled}")
        else:
            print(f"PASS coverage: {len(called)} methods called")
    print(f"{failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

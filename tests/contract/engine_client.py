# SPDX-License-Identifier: GPL-3.0-or-later
"""A client of shapelogic_engine for the contract tests: starts the engine, sends
requests, reads replies, events and binary frames (decision 0012), and
checks every message against the schema in api/ as it arrives."""

import json
import pathlib
import queue
import struct
import subprocess
import sys
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "api"))

from apischema import Api  # noqa: E402

# the errcs a method's schema entry declares (its "errors" list); the
# protocol's own (invalid_params, invalid_request, method_not_found and
# -32700 parse_error) are not listed per method
DECLARED_ERRCS = {"invalid_argument", "not_found", "parse_error", "io_error", "unsupported",
                  "invalid_state", "internal"}

# the codes of decision 0012, by errc
ERROR_CODES = {
    "parse_error": {-32700, -32003},
    "invalid_request": {-32600},
    "method_not_found": {-32601},
    "invalid_params": {-32602},
    "internal": {-32603},
    "invalid_argument": {-32001},
    "not_found": {-32002},
    "io_error": {-32004},
    "unsupported": {-32005},
    "invalid_state": {-32006},
}


class ContractError(AssertionError):
    pass


class RpcFailure(Exception):
    """An error reply."""

    def __init__(self, error):
        super().__init__(f"{error['data']['errc']}: {error['message']}")
        self.error = error
        self.errc = error["data"]["errc"]
        self.code = error["code"]


class Frame:
    """A binary frame and the arrays its reply describes."""

    FORMATS = {"f32": "f", "u32": "I", "i32": "i", "u8": "B"}

    def __init__(self, data, desc):
        self.data = data
        self.desc = desc
        if desc["bytes"] != len(data):
            raise ContractError(f"frame {desc['frame']}: {len(data)} bytes, the reply says {desc['bytes']}")
        for name, a in desc["arrays"].items():
            size = struct.calcsize("<" + self.FORMATS[a["type"]])
            if a["offset"] % 4 != 0:
                raise ContractError(f"frame array {name} at offset {a['offset']}")
            if a["offset"] + a["length"] * size > len(data):
                raise ContractError(f"frame array {name} runs past the frame")

    def array(self, name):
        a = self.desc["arrays"][name]
        fmt = "<" + str(a["length"]) + self.FORMATS[a["type"]]
        return list(struct.unpack_from(fmt, self.data, a["offset"]))


class Engine:
    """shapelogic_engine as a subprocess."""

    def __init__(self, path, api=None, cwd=ROOT):
        self.api = api or Api()
        self.proc = subprocess.Popen([str(path)], cwd=cwd, stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.next_id = 1
        self.messages = queue.Queue()
        self.frames = {}
        self.events = []        # every event, in order
        self.called = set()     # methods that answered with a result
        self.problems = []      # contract violations seen by the reader
        self.stderr = []
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()
        self.err_reader = threading.Thread(target=self._read_err, daemon=True)
        self.err_reader.start()

    # ---- reading ---------------------------------------------------------------------

    def _read(self):
        out = self.proc.stdout
        while True:
            line = out.readline()
            if not line:
                self.messages.put(None)
                return
            try:
                m = json.loads(line)
            except json.JSONDecodeError:
                self.problems.append(f"not JSON on stdout: {line[:100]!r}")
                continue
            if m.get("method") == "frame":
                n, size = m["params"]["frame"], m["params"]["bytes"]
                data = out.read(size)
                if len(data) != size:
                    self.problems.append(f"frame {n} cut short")
                self.frames[n] = data
            self.messages.put(m)

    def _read_err(self):
        for line in self.proc.stderr:
            self.stderr.append(line.decode("utf-8", "replace"))

    def _check(self, errors):
        if errors:
            raise ContractError("\n".join(errors))

    def _event(self, m):
        if m.get("jsonrpc") != "2.0" or "id" in m or set(m) - {"jsonrpc", "method", "params"}:
            raise ContractError(f"not a notification: {m}")
        self._check(self.api.check_event(m["method"], m.get("params")))
        self.events.append(m)

    def _next(self, timeout):
        try:
            m = self.messages.get(timeout=timeout)
        except queue.Empty:
            raise ContractError(f"no message within {timeout} s") from None
        if m is None:
            raise ContractError("the engine closed its output; stderr: " + "".join(self.stderr[-20:]))
        if self.problems:
            raise ContractError("; ".join(self.problems))
        return m

    # ---- calls -----------------------------------------------------------------------

    def send_raw(self, line):
        self.proc.stdin.write(line if isinstance(line, bytes) else line.encode("utf-8"))
        self.proc.stdin.flush()

    def request(self, method, params=None, notification=False):
        """Sends a request; returns its id (None for a notification)."""
        m = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            m["params"] = params
        rid = None
        if not notification:
            rid = self.next_id
            self.next_id += 1
            m["id"] = rid
        self.send_raw(json.dumps(m) + "\n")
        return rid

    def reply(self, rid, method=None, timeout=60):
        """The reply to request rid; events before it are checked and kept."""
        while True:
            m = self._next(timeout)
            if "id" not in m:
                self._event(m)
                continue
            if m.get("id") != rid:
                raise ContractError(f"a reply to {m.get('id')} while waiting for {rid}: {m}")
            if m.get("jsonrpc") != "2.0" or ("result" in m) == ("error" in m):
                raise ContractError(f"not a reply: {m}")
            if "error" in m:
                e = m["error"]
                if set(e) != {"code", "message", "data"} or not isinstance(e["message"], str):
                    raise ContractError(f"a malformed error: {e}")
                errc = e["data"].get("errc")
                if errc not in ERROR_CODES or e["code"] not in ERROR_CODES[errc]:
                    raise ContractError(f"error code {e['code']} with errc {errc!r}")
                # an error of the core must be one the method declares
                if (method is not None and errc in DECLARED_ERRCS and e["code"] != -32700
                        and errc not in self.api.methods[method][1].get("errors", [])):
                    raise ContractError(f"{method} answered {errc}, which its schema entry does not "
                                        f"declare: {e['message']}")
                raise RpcFailure(e)
            if method is not None:
                self._check(self.api.check_result(method, m["result"]))
                self.called.add(method)
            return m["result"]

    def call(self, method, params=None, timeout=60):
        """Calls a method that must succeed; returns its result."""
        if params is not None and method in self.api.methods:
            self._check(self.api.check_params(method, params))
        rid = self.request(method, params)
        return self.reply(rid, method, timeout)

    def fails(self, method, params=None, errc=None, timeout=60):
        """Calls a method that must fail; returns the error (checks its errc)."""
        rid = self.request(method, params)
        try:
            self.reply(rid, method, timeout)
        except RpcFailure as f:
            if errc is not None and f.errc != errc:
                raise ContractError(f"{method} failed with {f.errc}, expected {errc}: {f}") from None
            return f
        raise ContractError(f"{method} {params} succeeded; expected {errc}")

    def frame(self, desc):
        """The frame a reply refers to (it arrived before the reply)."""
        n = desc["frame"]
        if n not in self.frames:
            raise ContractError(f"frame {n} did not come before its reply")
        return Frame(self.frames[n], desc)

    # ---- events ----------------------------------------------------------------------

    def drain(self, quiet=0.2):
        """Reads the events that come within `quiet` seconds of each other."""
        while True:
            try:
                m = self.messages.get(timeout=quiet)
            except queue.Empty:
                return
            if m is None:
                raise ContractError("the engine closed its output")
            if "id" in m:
                raise ContractError(f"an unexpected reply: {m}")
            self._event(m)

    def wait_event(self, method, pred=lambda p: True, timeout=120, start=0):
        """The first event `method` (from index `start` of the events seen)
        whose params satisfy pred; reads on until it comes."""
        deadline = time.monotonic() + timeout
        seen = start
        while True:
            for i in range(seen, len(self.events)):
                e = self.events[i]
                if e["method"] == method and pred(e["params"]):
                    return e["params"]
            seen = len(self.events)
            left = deadline - time.monotonic()
            if left <= 0:
                raise ContractError(f"no event {method} within {timeout} s")
            m = self._next(left)
            if "id" in m:
                raise ContractError(f"an unexpected reply: {m}")
            self._event(m)

    # ---- end -------------------------------------------------------------------------

    def close(self):
        """Closes stdin: the engine must end by itself, with status 0."""
        if self.proc.poll() is None:
            self.proc.stdin.close()
            try:
                self.proc.wait(timeout=60)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                raise ContractError("the engine did not end at the end of its input") from None
        if self.proc.returncode != 0:
            raise ContractError(f"the engine ended with status {self.proc.returncode}")

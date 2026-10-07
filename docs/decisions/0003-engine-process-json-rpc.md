# 0003: The front end talks to a separate engine process over JSON-RPC

**Status:** accepted (stage 0); the protocol details are settled in stage 7

## Context
The new front end is a web application (HTML/TypeScript, three.js), most
likely shipped as a desktop app with Electron. It needs the C++ core for
editing, solving and playback. Options considered: a Node add-on (N-API)
loaded into Electron, or a separate process.

## Decision
- The core runs in its own process, **`bt-engine`**, speaking **JSON-RPC 2.0
  over stdio**: one JSON message per line on stdin/stdout, stderr for logs.
- Requests are calls (`shape.setVoxels`, `solve.start`…); **notifications**
  flow back for changes and progress (`document.changed`, `solve.progress`…).
- Large binary data (meshes, pose tracks) travels as **binary frames** on the
  same stream, announced by a JSON message carrying a length and an id, so
  meshes never go through JSON number encoding.
- The method and event list is defined once as JSON Schema in `api/`. It is
  the source for `docs/API.md` and for TypeScript types the front end uses.
- Long lists (solutions) are paged and sorted by the engine.

## Consequences
- A solver crash or assertion cannot take the UI down; the front end can
  restart the engine and reload the document.
- Works the same with Electron, Tauri, or a plain browser talking to a local
  engine over a WebSocket bridge. The same API can be compiled to WebAssembly
  later for a pure browser build, with a thin adapter.
- Every call is asynchronous for the front end. The core API (app layer) is
  designed so each call is quick; long work (solving, analysing) runs on
  worker threads and reports through events.
- Scriptable: tests and tools can drive the engine with any language.

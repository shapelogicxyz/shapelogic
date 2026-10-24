---
title: Code Base
description: "An introduction to the ShapeLogic source code for developers: what the parts are, how they fit together, and where to read next."
---

This section is for developers who want to read, build or change
ShapeLogic. The rest of the user guide explains how to use the app; these
pages explain how it is made.

ShapeLogic is three programs over one library:

```
 the desktop app   (web/: Electron + TypeScript + three.js)
        │  JSON-RPC 2.0 over stdio, binary frames for meshes
        ▼
 shapelogic_engine  (engine/)    shapelogic_cli  (cli/)    shapelogic  (python/, the Python module)
        │                             │                           │
        └──────────────┬──────────────┴───────────────────────────┘
                       ▼
                 libbtcore  (core/shapelogic/)
   app     Document, undo/redo, editing commands, queries, playback, explorers
   solve   assemblers, disassemblers, rotation rules, SolveSession
   mesh    half-edge meshes and drawing meshes for all grids
   io      XML, gzip, .xmpuzzle reader/writer
   model   Puzzle, Problem, Assembly, Disassembly, Solution
   grid    grid types, voxel spaces, symmetry tables
   base    StopToken, SolveState, Result/Error, BT_ASSERT, thread pool
```

- **The core** (`core/`) is a C++ library. It knows nothing about windows,
  JSON or files on disk beyond reading and writing puzzles. It never prints
  and has no global state.
- **The engine** (`engine/`) wraps one open puzzle (a `Document`) in a
  process that speaks JSON-RPC over its standard input and output. Every
  feature the app offers is a method of the engine.
- **The app** (`web/`) is an Electron application. Its window is a sandboxed
  web page that can only talk to the engine. The 3D view is three.js. The
  app never computes what the engine can answer: cells, fits, counts, meshes
  and sort orders all come from the engine.
- **The command line tool** (`cli/`) and the **Python module** (`python/`)
  use the same core directly, without the engine.
- **The website** (`site/`) is this user guide and the landing page, built
  with Astro Starlight, plus a browser demo of the app with the engine
  compiled to WebAssembly.

ShapeLogic is a rebuild of BurrTools. The original program is the
reference: ported code must give the same answers, and a comparison
harness checks that it does. Many files begin with a comment naming the
original file they were ported from.

## The pages in this section

- [Languages and Tools](/docs/code-base/stack/): C++23, TypeScript,
  Electron, three.js, CMake, just, and the libraries each part depends on.
- [Repository Layout](/docs/code-base/file-structure/): what is in each
  directory.
- [Build, Test and Tool Commands](/docs/code-base/commands/): the `just`
  recipes, the npm scripts, the build presets and the environment switches.
- [Where the Code Lives](/docs/code-base/core-functions/): the files and
  classes behind each feature, from the voxel grids to the Solver tab.
- [Engine Protocol](/docs/code-base/engine-protocol/): how the app and the
  engine talk, and the rules every call follows.
- [Example Solve Flow](/docs/code-base/example-solve-flow/): what happens,
  function by function, when you press **Solve** on a Brick puzzle.
- [Conventions and Further Reading](/docs/code-base/conventions/): the code
  rules, the testing rules, and the documents inside the repository that go
  deeper.

## Reading order

If you are new, read the pages in the order above. The
[Example Solve Flow](/docs/code-base/example-solve-flow/) ties the others
together: it crosses every layer, from a button in the web page to the
threads of the search and back.

The repository's own documents go further than these pages. Start with
`AGENTS.md` at the root (the working rules), then `docs/ARCHITECTURE.md`
(what exists, module by module), the decision records in `docs/decisions/`
and the hand-off notes in `docs/handoff/`. The engine's methods are
documented in `docs/API.md`, generated from the schema in `api/`.

# 0006: Full .xmpuzzle compatibility, both ways

**Status:** accepted (stage 0); implemented in stage 2

**Amended 2026-11-21: compatibility is one way.** ShapeLogic must open
every file the original wrote, converting on load; the original need not
read ShapeLogic's saves, for any feature. No shims, dual encodings or
limits are added so that the old program can read new files, and the
oracle round trip's "the original reads the new save" check is not a
requirement. The bullets below on writing files the original reads, and
on new data the old program ignores, hold only where nothing is lost by
them. Saved searches (sliding level files, `state.bin`, Panex folders)
and their folder names stay readable by the new code.

## Context
Users have years of `.xmpuzzle` files (gzip-compressed XML), some with saved
solutions, paused searches and features this project does not port yet
(sliding, stacking/Panex).

## Decision
- The new core **reads every file the original reads** and **writes files the
  original reads** with the same meaning.
- **Unknown or not-yet-supported content is preserved**: elements and
  attributes the new core does not model (sliding walls, stacking rod sets,
  Lua comments...) are kept verbatim and written back unchanged. Problems of
  an unsupported kind load, are marked unsupported (`Errc::Unsupported` when
  asked to solve), and save back untouched.
- Saved assembler positions (paused searches) stay format-compatible, so a
  search paused in one program can be continued in the other (stage 3).
- The writer produces the same element order and formatting as the original
  where practical, so diffs between files stay readable.

## Consequences
- Round-trip tests in stage 2: every corpus file is loaded and saved by both
  programs and the results compared semantically.
- New features that need new data add new elements or attributes; the old
  program ignores what it does not know, so files stay usable in both.

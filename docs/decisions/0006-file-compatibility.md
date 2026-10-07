# 0006: Full .xmpuzzle compatibility, both ways

**Status:** accepted (stage 0); implemented in stage 2

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

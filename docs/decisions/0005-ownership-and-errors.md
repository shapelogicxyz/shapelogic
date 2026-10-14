# 0005: Ownership, errors and invariants

**Status:** accepted (stage 0); `core/shapelogic/base/error.h`, `core/shapelogic/base/assert.h`

## Context
The original returns raw owning pointers (`gridType_c::getVoxel()`), writes
copy assignment by hand (missed fields caused bugs), and uses
`bt_assert`, which is compiled out in release builds.

## Decision
- **Ownership:** value types where possible, `std::unique_ptr` for
  polymorphic objects, `std::shared_ptr` only for genuinely shared immutable
  data. No owning raw pointers, no `new`/`delete` in project code, no
  hand-written copy operations that list members (use `= default` or make
  the type move-only).
- **Recoverable errors** (bad file, request not allowed now, missing item)
  return `bt::Result<T>` = `std::expected<T, bt::Error>`. They are never
  thrown. `bt::Errc` lists the categories; the engine maps them to JSON-RPC
  error codes.
- **Invariants** use `BT_ASSERT` / `BT_ASSERT_MSG`, **on in every build**,
  which throw `bt::InternalError`. The solve session catches it and moves the
  run to `SolveState::Error` with the message; the engine reports it as an
  `internal` error and keeps running.
- No exceptions other than `InternalError` and `std::bad_alloc` cross module
  boundaries.

## Consequences
- Release builds catch broken invariants instead of silently giving wrong
  answers; the checks are cheap compared with the searches. Hot inner loops
  keep assertions out of the innermost path where measurement shows a cost.
- APIs say in their types whether they can fail.

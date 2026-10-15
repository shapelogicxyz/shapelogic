# 0020: The sphere grid's symmetry registry

**Status:** accepted (from the original's 95b49d32)

## Context
The sphere grid's 240 transformations do not form a group: many
compositions leave the grid. A shape's symmetry list is therefore not a
subgroup, and the generated table of 241 lists (`tabs_2`) is only what the
original's generator happened to meet. A shape with a list outside it (the
orange piece of `tests/data/puzzles/test_sphere_symmetry.xmpuzzle`, from
the Geapple puzzle) failed `BT_ASSERT(symmetryKnown(pp))` in
`calculateSymmetry`. That assertion is on in every build here, so the CLI
aborted and a solve ended in an internal error; the original crashed its
GUI. The original fixed it with a registry of lists that grows at run time.

## Decision
- The sphere grid has its own `Symmetries` (`grid/symmetries_2.cpp`), no
  longer the shared table template. `calculateSymmetry` works out the
  shape's whole list and looks it up; a list not met before is appended
  after the generated ones, its tables derived by the generator's rules
  (`deriveSphereTables`, checked against all 241 generated entries).
  `symmetryKnown` is always true for spheres. `tabs_2/symcalc.inc` is gone.
- `Symmetry` is 16 bits and `kSymmetryInvalid` 0xFFFF, to number the
  appended lists.
- The registry is process-wide mutable state inside `core/`, an exception
  to the rule against it: it is an append-only memo of a pure function (a
  shape's list), shared so that a number means the same list to every
  thread and puzzle. Reads are lock free (blocks that never move, a count
  published with release); appending takes a mutex. Generated lists are
  read straight from the constant tables, so the rotation checks pay
  nothing for the common case.
- An appended list's number depends on the order shapes are met in, so
  symmetry numbers are never saved; the engine reports them only as
  information.

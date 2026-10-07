// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Transformations and symmetry groups of a grid.
/// Ported from src/lib/symmetries.h.
///
/// A transformation rotates (and maybe mirrors) a shape within its grid; it
/// is just a number. The lower numbers are the plain rotations, the upper
/// ones all contain a mirror. A symmetry group is the set of transformations
/// that map a shape onto itself; every possible set is listed in a
/// generated table, and shapes only carry the index into it (`Symmetry`).
/// A shape's group depends on its orientation.

#include "bt/grid/types.h"

namespace bt {

class Voxel;

/// The symmetry tables of one grid. Get the instance through
/// GridType::getSymmetries(); it is immutable and shared.
class Symmetries {
 public:
  Symmetries() = default;
  virtual ~Symmetries() = default;
  Symmetries(const Symmetries&) = delete;
  Symmetries& operator=(const Symmetries&) = delete;

  /// The number of plain rotations.
  [[nodiscard]] virtual unsigned int getNumTransformations() const = 0;

  /// The number of transformations including the mirrored ones (normally
  /// twice getNumTransformations()).
  [[nodiscard]] virtual unsigned int getNumTransformationsMirror() const = 0;

  /// True when group `s` contains transformation `t`.
  [[nodiscard]] virtual bool symmetrieContainsTransformation(Symmetry s, unsigned int t) const = 0;

  /// Applying every transformation for which this is true to a shape with
  /// group `s` gives each of its distinct orientations exactly once.
  [[nodiscard]] virtual bool isTransformationUnique(Symmetry s, unsigned int t) const = 0;

  /// The transformation equal to applying `t1` and then `t2`, or
  /// kTransformUndefined when that does not exist on this grid.
  [[nodiscard]] virtual unsigned char transAdd(unsigned char t1, unsigned char t2) const = 0;

  /// The smallest transformation that gives a shape with group `s` the same
  /// look as `trans` does. Used when rotating assemblies.
  [[nodiscard]] virtual unsigned char minimizeTransformation(Symmetry s,
                                                             unsigned char trans) const = 0;

  /// How much the symmetries of a result and a shape overlap; small when
  /// they share few.
  [[nodiscard]] virtual unsigned int countSymmetryIntersection(Symmetry resultSym,
                                                               Symmetry s2) const = 0;

  /// Whether the result and the shape share symmetries beyond the identity
  /// (like countSymmetryIntersection() > 1, but faster).
  [[nodiscard]] virtual bool symmetriesLeft(Symmetry resultSym, Symmetry s2) const = 0;

  /// True when group `sym` holds a mirror transformation: a shape with it can
  /// be mirrored by rotating it.
  [[nodiscard]] virtual bool symmetryContainsMirror(Symmetry sym) const = 0;

  /// The symmetry group of a voxel space in its current orientation.
  [[nodiscard]] virtual Symmetry calculateSymmetry(const Voxel& v) const = 0;

  /// True when the shape's group is one of the tabulated ones. A check that
  /// the tables are complete; calculateSymmetry asserts it.
  [[nodiscard]] virtual bool symmetryKnown(const Voxel& v) const = 0;
};

}  // namespace bt

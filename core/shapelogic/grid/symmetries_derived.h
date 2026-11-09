// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Symmetry tables derived at start-up from a grid's transformation
/// matrices, for the grids that have no generated tables (decision 0023:
/// the hexagonal and cairo prism grids). The matrices must form a group;
/// from them come the composition table, every subgroup (the symmetry
/// groups a shape can have), and per group the tables the generator of
/// the original made offline (unified, unique, minimizer), by its rules.
/// Internal to the grid module; use GridType::getSymmetries().

#include "shapelogic/grid/symmetries.h"

#include <array>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace bt::detail {

/// Symmetries over a list of matrices: the plain rotations first, then
/// the mirrors; the identity first of all. At most 64 transformations.
class DerivedSymmetries final : public Symmetries {
 public:
  /// A rotation or mirror matrix, row major.
  using Matrix = std::array<double, 9>;

  /// `plain` of the matrices are proper rotations (the first ones); the
  /// rest contain a mirror. The set must be closed under multiplication.
  DerivedSymmetries(std::vector<Matrix> matrices, unsigned int plain);

  unsigned int getNumTransformations() const override { return plain_; }
  unsigned int getNumTransformationsMirror() const override { return n_; }
  unsigned int getNumSymmetryGroups() const override {
    return static_cast<unsigned int>(groups_.size());
  }
  bool symmetrieContainsTransformation(Symmetry s, unsigned int t) const override;
  bool isTransformationUnique(Symmetry s, unsigned int t) const override;
  unsigned char transAdd(unsigned char t1, unsigned char t2) const override;
  unsigned char minimizeTransformation(Symmetry s, unsigned char trans) const override;
  unsigned int countSymmetryIntersection(Symmetry resultSym, Symmetry s2) const override;
  bool symmetriesLeft(Symmetry resultSym, Symmetry s2) const override;
  bool symmetryContainsMirror(Symmetry sym) const override;
  Symmetry calculateSymmetry(const Voxel& v) const override;
  bool symmetryKnown(const Voxel& v) const override;

  /// The matrix of a transformation.
  [[nodiscard]] const Matrix& matrix(unsigned int t) const { return m_[t]; }

  /// The transformation with this matrix (within 1e-6), if any.
  [[nodiscard]] std::optional<unsigned int> transformationOf(const Matrix& m) const;

  /// The transformations that map a shape onto itself, bit t for t.
  [[nodiscard]] std::uint64_t symmetryList(const Voxel& v) const;

 private:
  struct Group {
    std::uint64_t sym = 0;
    std::uint64_t unified = 0;
    std::uint64_t unique = 0;
    std::vector<unsigned char> minimizer;
  };

  [[nodiscard]] const Group& group(Symmetry s) const;

  std::vector<Matrix> m_;
  unsigned int plain_;
  unsigned int n_;
  /// mult_[t1][t2]: t1 then t2.
  std::vector<std::vector<unsigned char>> mult_;
  std::vector<Group> groups_;
  std::unordered_map<std::uint64_t, Symmetry> index_;
};

/// The matrices of a prism grid with `fold`-fold symmetry about z: the
/// turns about z, each also turned over (180 degrees about x), then all
/// of them mirrored through the origin. 2 * fold plain, 4 * fold in all.
[[nodiscard]] std::vector<DerivedSymmetries::Matrix> prismMatrices(unsigned int fold);

/// The hexagonal prism grid's symmetries (6-fold: 12 and 24).
[[nodiscard]] const DerivedSymmetries& hexagonalSymmetries();
/// The cairo prism grid's symmetries (4-fold: 8 and 16).
[[nodiscard]] const DerivedSymmetries& cairoSymmetries();

}  // namespace bt::detail

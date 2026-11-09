// SPDX-License-Identifier: GPL-3.0-or-later
// After the table generators of the original (src/lib/tabs_N/generator_N.cpp)
// and deriveSphereTables of symmetries_2.cpp, at run time for a group of
// matrices.
#include "shapelogic/grid/symmetries_derived.h"

#include "shapelogic/base/assert.h"
#include "shapelogic/grid/voxel.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>

namespace bt::detail {

namespace {

using Matrix = DerivedSymmetries::Matrix;

Matrix multiply(const Matrix& a, const Matrix& b) {
  Matrix r{};
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      double s = 0;
      for (int k = 0; k < 3; k++) {
        s += a[static_cast<std::size_t>(3 * i + k)] * b[static_cast<std::size_t>(3 * k + j)];
      }
      r[static_cast<std::size_t>(3 * i + j)] = s;
    }
  }
  return r;
}

bool sameMatrix(const Matrix& a, const Matrix& b) {
  for (std::size_t i = 0; i < 9; i++) {
    if (std::abs(a[i] - b[i]) > 1e-6) {
      return false;
    }
  }
  return true;
}

// The exact values of the turns' sines and cosines, so that a product of
// matrices is found again within the tolerance.
double tidy(double v) { return std::abs(v) < 1e-12 ? 0.0 : v; }

}  // namespace

std::vector<Matrix> prismMatrices(unsigned int fold) {
  std::vector<Matrix> out;
  const Matrix flip{1, 0, 0, 0, -1, 0, 0, 0, -1};
  for (int mirror = 0; mirror < 2; mirror++) {
    for (int over = 0; over < 2; over++) {
      for (unsigned int k = 0; k < fold; k++) {
        const double a = 2 * std::numbers::pi * k / fold;
        const double c = tidy(std::cos(a));
        const double s = tidy(std::sin(a));
        Matrix m{c, -s, 0, s, c, 0, 0, 0, 1};
        if (over != 0) {
          m = multiply(m, flip);
        }
        if (mirror != 0) {
          for (double& v : m) {
            v = -v;
          }
        }
        out.push_back(m);
      }
    }
  }
  return out;
}

DerivedSymmetries::DerivedSymmetries(std::vector<Matrix> matrices, unsigned int plain)
    : m_(std::move(matrices)), plain_(plain), n_(static_cast<unsigned int>(m_.size())) {
  BT_ASSERT(n_ >= 1 && n_ <= 64 && plain_ <= n_);
  BT_ASSERT(sameMatrix(m_[0], Matrix{1, 0, 0, 0, 1, 0, 0, 0, 1}));

  // t1 then t2: the matrix of t2 times that of t1
  mult_.assign(n_, std::vector<unsigned char>(n_, 0));
  for (unsigned int t1 = 0; t1 < n_; t1++) {
    for (unsigned int t2 = 0; t2 < n_; t2++) {
      const std::optional<unsigned int> r = transformationOf(multiply(m_[t2], m_[t1]));
      BT_ASSERT_MSG(r.has_value(), "the transformation matrices are not closed under products");
      mult_[t1][t2] = static_cast<unsigned char>(*r);
    }
  }

  // every subgroup: closures of a known subgroup with one more element,
  // until nothing new turns up
  const auto closure = [&](std::uint64_t set) {
    bool grown = true;
    while (grown) {
      grown = false;
      for (unsigned int a = 0; a < n_; a++) {
        if ((set & (std::uint64_t{1} << a)) == 0) {
          continue;
        }
        for (unsigned int b = 0; b < n_; b++) {
          if ((set & (std::uint64_t{1} << b)) == 0) {
            continue;
          }
          const std::uint64_t bit = std::uint64_t{1} << mult_[a][b];
          if ((set & bit) == 0) {
            set |= bit;
            grown = true;
          }
        }
      }
    }
    return set;
  };
  std::vector<std::uint64_t> subgroups{std::uint64_t{1}};
  for (std::size_t i = 0; i < subgroups.size(); i++) {
    const std::uint64_t g = subgroups[i];
    for (unsigned int t = 1; t < n_; t++) {
      if ((g & (std::uint64_t{1} << t)) != 0) {
        continue;
      }
      const std::uint64_t h = closure(g | (std::uint64_t{1} << t));
      if (std::ranges::find(subgroups, h) == subgroups.end()) {
        subgroups.push_back(h);
      }
    }
  }
  // the smallest first, so that the identity group is 0
  std::ranges::sort(subgroups, [](std::uint64_t a, std::uint64_t b) {
    const int ca = std::popcount(a);
    const int cb = std::popcount(b);
    return ca != cb ? ca < cb : a < b;
  });

  for (const std::uint64_t sym : subgroups) {
    Group g;
    g.sym = sym;
    const auto in = [&](unsigned int t) { return (sym & (std::uint64_t{1} << t)) != 0; };
    // the lowest transformation t with a symmetry followed by t equal to
    // trans gives the same orientation as trans
    g.minimizer.resize(n_);
    for (unsigned int trans = 0; trans < n_; trans++) {
      auto m = static_cast<unsigned char>(trans);
      for (unsigned int t = 0; t < trans && m == trans; t++) {
        for (unsigned int t2 = 0; t2 < n_; t2++) {
          if (in(t2) && mult_[t2][t] == trans) {
            m = static_cast<unsigned char>(t);
            break;
          }
        }
      }
      g.minimizer[trans] = m;
    }
    // if r, x, inverse of r is a symmetry of the shape, then x is a
    // symmetry of the shape turned by r
    g.unified = sym;
    for (unsigned int r = 1; r < n_; r++) {
      unsigned int rinv = 0;
      while (mult_[r][rinv] != 0) {
        rinv++;
      }
      for (unsigned int x = 0; x < n_; x++) {
        if (in(mult_[mult_[r][x]][rinv])) {
          g.unified |= std::uint64_t{1} << x;
        }
      }
    }
    // one transformation for each orientation: once r is taken, every
    // symmetry followed by r gives the same orientation and is skipped
    std::uint64_t seen = 0;
    for (unsigned int r = 0; r < n_; r++) {
      if ((seen & (std::uint64_t{1} << r)) != 0) {
        continue;
      }
      g.unique |= std::uint64_t{1} << r;
      for (unsigned int r2 = 0; r2 < n_; r2++) {
        if (in(r2)) {
          seen |= std::uint64_t{1} << mult_[r2][r];
        }
      }
    }
    index_.emplace(sym, static_cast<Symmetry>(groups_.size()));
    groups_.push_back(std::move(g));
  }
}

std::optional<unsigned int> DerivedSymmetries::transformationOf(const Matrix& m) const {
  for (unsigned int t = 0; t < n_; t++) {
    if (sameMatrix(m_[t], m)) {
      return t;
    }
  }
  return std::nullopt;
}

const DerivedSymmetries::Group& DerivedSymmetries::group(Symmetry s) const {
  BT_ASSERT(s < groups_.size());
  return groups_[s];
}

bool DerivedSymmetries::symmetrieContainsTransformation(Symmetry s, unsigned int t) const {
  BT_ASSERT(t < n_);
  return (group(s).sym & (std::uint64_t{1} << t)) != 0;
}

bool DerivedSymmetries::isTransformationUnique(Symmetry s, unsigned int t) const {
  BT_ASSERT(t < n_);
  return (group(s).unique & (std::uint64_t{1} << t)) != 0;
}

unsigned char DerivedSymmetries::transAdd(unsigned char t1, unsigned char t2) const {
  BT_ASSERT(t1 < n_ && t2 < n_);
  return mult_[t1][t2];
}

unsigned char DerivedSymmetries::minimizeTransformation(Symmetry s, unsigned char trans) const {
  BT_ASSERT(trans < n_);
  return group(s).minimizer[trans];
}

unsigned int DerivedSymmetries::countSymmetryIntersection(Symmetry resultSym, Symmetry s2) const {
  return static_cast<unsigned int>(std::popcount(group(resultSym).unified & group(s2).sym));
}

bool DerivedSymmetries::symmetriesLeft(Symmetry resultSym, Symmetry s2) const {
  // the identity (bit 0) does not count
  return ((group(resultSym).sym & group(s2).unified) & ~std::uint64_t{1}) != 0;
}

bool DerivedSymmetries::symmetryContainsMirror(Symmetry sym) const {
  const std::uint64_t mirrors = n_ == 64 ? ~((std::uint64_t{1} << plain_) - 1)
                                         : ((std::uint64_t{1} << n_) - 1) &
                                               ~((std::uint64_t{1} << plain_) - 1);
  return (group(sym).sym & mirrors) != 0;
}

std::uint64_t DerivedSymmetries::symmetryList(const Voxel& v) const {
  std::uint64_t s = 1;
  for (unsigned int t = 1; t < n_; t++) {
    auto copy = v.clone();
    if (copy->transform(t) && v.identicalInBB(*copy)) {
      s |= std::uint64_t{1} << t;
    }
  }
  return s;
}

Symmetry DerivedSymmetries::calculateSymmetry(const Voxel& v) const {
  const auto it = index_.find(symmetryList(v));
  BT_ASSERT_MSG(it != index_.end(), "a shape's symmetries are not a subgroup");
  return it->second;
}

bool DerivedSymmetries::symmetryKnown(const Voxel& v) const {
  return index_.contains(symmetryList(v));
}

const DerivedSymmetries& hexagonalSymmetries() {
  static const DerivedSymmetries s(prismMatrices(6), 12);
  return s;
}

const DerivedSymmetries& cairoSymmetries() {
  static const DerivedSymmetries s(prismMatrices(4), 8);
  return s;
}

}  // namespace bt::detail

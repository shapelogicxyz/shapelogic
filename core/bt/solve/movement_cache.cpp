// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/movementcache.cpp and gridType_c::getMovementCache in
// src/lib/gridtype.cpp.
#include "bt/solve/movement_cache.h"

#include "bt/base/assert.h"
#include "bt/grid/grid_type.h"
#include "bt/grid/symmetries.h"
#include "bt/grid/voxel.h"
#include "bt/model/problem.h"
#include "bt/model/puzzle.h"

#include <algorithm>

namespace bt {

// The original's hash, in unsigned arithmetic (the same bits).
std::size_t MovementCache::MoKeyHash::operator()(const MoKey& k) const noexcept {
  auto u = [](int v) { return static_cast<unsigned int>(v); };
  unsigned int val = u(k.dx) * 0x10101010U;
  val += u(k.dy) * 0x14814814U;
  val += u(k.dz) * 0x95145951U;
  val += static_cast<unsigned int>(static_cast<unsigned char>(k.t1)) * 0x1A54941AU;
  val += static_cast<unsigned int>(static_cast<unsigned char>(k.t2)) * 0x5AA59401U;
  val += k.s1 * 0x01059a04U;
  val += k.s2 * 0x9af42682U;
  return val;
}

MovementCache::MovementCache(const Puzzle& puzzle, const Problem& problem)
    : gridType_(puzzle.getGridType()),
      turned_(problem.getNumberOfParts()),
      pieces_(problem.getNumberOfPieces()) {
  const unsigned int transformations = gridType_.getSymmetries().getNumTransformations();
  for (unsigned int s = 0; s < problem.getNumberOfParts(); s++) {
    base_.push_back(&problem.partShape(puzzle, s));
    turned_[s].resize(transformations);
  }

  std::size_t pos = 0;
  for (unsigned int s = 0; s < problem.getNumberOfParts(); s++) {
    for (unsigned int i = 0; i < problem.getPartMaximum(s); i++) {
      pieces_[pos++] = s;
    }
  }
}

MovementCache::~MovementCache() = default;

const Voxel& MovementCache::getTransformedShape(unsigned int s, unsigned char t) {
  if (t == 0) {
    return *base_[s];
  }

  std::unique_ptr<Voxel>& sh = turned_[s][t];
  if (!sh) {
    sh = base_[s]->clone();
    const bool turned = sh->transform(t);
    BT_ASSERT(turned);
  }
  return *sh;
}

void MovementCache::getMoValue(int dx, int dy, int dz, unsigned char t1, unsigned char t2,
                               unsigned int p1, unsigned int p2, unsigned int* movements) {
  const unsigned int s1 = pieces_[p1];
  const unsigned int s2 = pieces_[p2];

  const MoKey key{s1, s2, dx, dy, dz, t1, t2};

  auto it = moCache_.find(key);
  if (it == moCache_.end()) {
    it = moCache_
             .emplace(key, moCalcValues(getTransformedShape(s1, t1), getTransformedShape(s2, t2), dx,
                                        dy, dz))
             .first;
  }

  std::copy_n(it->second.begin(), numDirections(), movements);
}

std::unique_ptr<MovementCache> makeMovementCache(const Puzzle& puzzle, const Problem& problem) {
  switch (puzzle.getGridType().getType()) {
    case GridKind::Bricks:
      return std::make_unique<MovementCacheBricks>(puzzle, problem);
    case GridKind::TriangularPrism:
      return std::make_unique<MovementCachePrisms>(puzzle, problem);
    case GridKind::Spheres:
    case GridKind::Rhombic:
    case GridKind::TetraOcta:
    case GridKind::Sliding:
    case GridKind::Stacking:
      break;
  }
  return nullptr;
}

}  // namespace bt

// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/movementcache.cpp and gridType_c::getMovementCache in
// src/lib/gridtype.cpp.
#include "shapelogic/solve/movement_cache.h"

#include "shapelogic/base/assert.h"
#include "shapelogic/grid/grid_type.h"
#include "shapelogic/grid/symmetries.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/solve/movement_checks.h"

#include <algorithm>

namespace bt {

namespace {

// The original's hash, in unsigned arithmetic (the same bits), of a key as
// the table holds it.
std::size_t moHash(const unsigned int* key) noexcept {
  unsigned int val = key[2] * 0x10101010U;
  val += key[3] * 0x14814814U;
  val += key[4] * 0x95145951U;
  val += (key[5] & 0xFFFFU) * 0x1A54941AU;
  val += (key[5] >> 16) * 0x5AA59401U;
  val += key[0] * 0x01059a04U;
  val += key[1] * 0x9af42682U;
  return val;
}

}  // namespace

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

unsigned int* MovementCache::moFind(const unsigned int* key) {
  for (std::size_t at = moHash(key) & moMask_;; at = (at + 1) & moMask_) {
    unsigned int* place = &moTable_[at * moStride_];
    if (place[0] == kEmpty || std::equal(key, key + kKeyWords, place)) {
      return place;
    }
  }
}

// Twice the places (or the first ones), what is there put in again.
void MovementCache::moGrow() {
  std::vector<unsigned int> old;
  old.swap(moTable_);
  const std::size_t places = old.empty() ? 1024 : (moMask_ + 1) * 2;
  moTable_.assign(places * moStride_, kEmpty);
  moMask_ = places - 1;
  for (std::size_t at = 0; at < old.size(); at += moStride_) {
    if (old[at] != kEmpty) {
      std::copy_n(&old[at], moStride_, moFind(&old[at]));
    }
  }
}

void MovementCache::getMoValue(int dx, int dy, int dz, unsigned char t1, unsigned char t2,
                               unsigned int p1, unsigned int p2, unsigned int* movements) {
  const unsigned int s1 = pieces_[p1];
  const unsigned int s2 = pieces_[p2];
  const unsigned int dirs = numDirections();

  if (moTable_.empty()) {
    moStride_ = kKeyWords + dirs;
    moGrow();
  }

  const unsigned int key[kKeyWords] = {s1,
                                       s2,
                                       static_cast<unsigned int>(dx),
                                       static_cast<unsigned int>(dy),
                                       static_cast<unsigned int>(dz),
                                       t1 | (static_cast<unsigned int>(t2) << 16)};
  unsigned int* place = moFind(key);
  if (place[0] == kEmpty) {
    const std::vector<unsigned int> values =
        moCalcValues(getTransformedShape(s1, t1), getTransformedShape(s2, t2), dx, dy, dz);
    BT_ASSERT(values.size() >= dirs);
    std::copy_n(key, kKeyWords, place);
    std::copy_n(values.begin(), dirs, place + kKeyWords);
    std::copy_n(place + kKeyWords, dirs, movements);
    moCount_++;
    if (moCount_ * 10 >= (moMask_ + 1) * 7) {
      moGrow();
    }
    return;
  }

  std::copy_n(place + kKeyWords, dirs, movements);
}

std::unique_ptr<MovementCache> makeMovementCache(const Puzzle& puzzle, const Problem& problem,
                                                 bool derivedChecks) {
  const GridKind kind = puzzle.getGridType().getType();
  if (!derivedChecks) {
    if (kind == GridKind::Bricks) {
      return std::make_unique<MovementCacheBricks>(puzzle, problem);
    }
    if (kind == GridKind::TriangularPrism) {
      return std::make_unique<MovementCachePrisms>(puzzle, problem);
    }
  }
  // not in the original: the checks derived from the cells (decision 0025);
  // spheres, and the sliding and stacking grids, have none
  const MovementChecks* checks = movementChecks(kind);
  if (checks == nullptr) {
    return nullptr;
  }
  return std::make_unique<MovementCacheDerived>(puzzle, problem, *checks);
}

}  // namespace bt

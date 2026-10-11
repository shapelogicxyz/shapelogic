// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/puzzle.cpp (with its rod sets: the constructor,
// addRodSet, removeRodSet, exchangeRodSets).

#include "bt/model/puzzle.h"

#include <utility>

namespace bt {

Puzzle::Puzzle(GridType gt) : gt_(gt) {
  if (gt_.getType() == GridKind::Stacking) {
    rodSets_.emplace_back();
  }
}

unsigned int Puzzle::addShape(std::unique_ptr<Voxel> p) {
  BT_ASSERT(p != nullptr);
  BT_ASSERT(p->getGridType() == gt_);
  shapes_.emplace_back(std::move(p));
  return static_cast<unsigned int>(shapes_.size() - 1);
}

unsigned int Puzzle::addShape(unsigned int sx, unsigned int sy, unsigned int sz) {
  return addShape(gt_.makeVoxel(sx, sy, sz, Voxel::kEmpty));
}

void Puzzle::removeShape(unsigned int idx) {
  BT_ASSERT(idx < shapes_.size());

  // first the problems, then the shape itself
  for (Problem& p : problems_) {
    p.removeShape(idx);
  }

  shapes_.erase(shapes_.begin() + idx);
}

std::unique_ptr<Voxel> Puzzle::replaceShape(unsigned int idx, std::unique_ptr<Voxel> v) {
  BT_ASSERT(idx < shapes_.size());
  BT_ASSERT(v != nullptr);
  BT_ASSERT(v->getGridType() == gt_);
  std::unique_ptr<Voxel> old = shapes_[idx].release();
  shapes_[idx].reset(std::move(v));
  return old;
}

void Puzzle::exchangeShapes(unsigned int s1, unsigned int s2) {
  BT_ASSERT(s1 < shapes_.size());
  BT_ASSERT(s2 < shapes_.size());

  std::swap(shapes_[s1], shapes_[s2]);

  for (Problem& p : problems_) {
    p.exchangeShapes(s1, s2);
  }
}

unsigned int Puzzle::addColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
  BT_ASSERT(colors_.size() < kMaxColors);  // colour 0 is "none"
  colors_.push_back(static_cast<std::uint32_t>(r) | static_cast<std::uint32_t>(g) << 8 |
                    static_cast<std::uint32_t>(b) << 16);
  return static_cast<unsigned int>(colors_.size() - 1);
}

void Puzzle::removeColor(unsigned int col) {
  BT_ASSERT(col >= 1 && col <= colors_.size());

  // the shapes stop using the colour, higher colours move down
  for (ClonePtr<Voxel>& shape : shapes_) {
    for (unsigned int p = 0; p < shape->getXYZ(); p++) {
      if (shape->getState(p) != Voxel::kEmpty) {
        if (shape->getColor(p) == col) {
          shape->setColor(p, 0);
        } else if (shape->getColor(p) > col) {
          shape->setColor(p, shape->getColor(p) - 1);
        }
      }
    }
  }

  // the colour rules: drop those with the colour, renumber the higher ones
  const auto n = static_cast<unsigned int>(colors_.size());
  for (Problem& prob : problems_) {
    for (unsigned int c = 0; c <= n; c++) {
      prob.disallowPlacement(*this, col, c);
      prob.disallowPlacement(*this, c, col);
    }

    for (unsigned int c1 = col + 1; c1 <= n; c1++) {
      for (unsigned int c2 = 0; c2 <= n; c2++) {
        if (prob.placementAllowed(*this, c1, c2)) {
          prob.disallowPlacement(*this, c1, c2);
          prob.allowPlacement(*this, c1 - 1, c2);
        }
      }
    }

    for (unsigned int c1 = col + 1; c1 <= n; c1++) {
      for (unsigned int c2 = 0; c2 <= n; c2++) {
        if (prob.placementAllowed(*this, c2, c1)) {
          prob.disallowPlacement(*this, c2, c1);
          prob.allowPlacement(*this, c2, c1 - 1);
        }
      }
    }
  }

  colors_.erase(colors_.begin() + (col - 1));
}

void Puzzle::changeColor(unsigned int idx, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
  BT_ASSERT(idx < colors_.size());
  colors_[idx] = static_cast<std::uint32_t>(r) | static_cast<std::uint32_t>(g) << 8 |
                 static_cast<std::uint32_t>(b) << 16;
}

Rgb Puzzle::getColor(unsigned int idx) const {
  BT_ASSERT(idx < colors_.size());
  const std::uint32_t c = colors_[idx];
  return Rgb{static_cast<std::uint8_t>(c & 0xFF), static_cast<std::uint8_t>((c >> 8) & 0xFF),
             static_cast<std::uint8_t>((c >> 16) & 0xFF)};
}

unsigned int Puzzle::addProblem() {
  problems_.emplace_back();
  return static_cast<unsigned int>(problems_.size() - 1);
}

unsigned int Puzzle::addProblem(const Problem& prob) {
  Problem copy = prob.copyDefinition();

  // a problem from another puzzle may name shapes this one does not have
  copy.dropShapesFrom(getNumberOfShapes());

  problems_.push_back(std::move(copy));
  return static_cast<unsigned int>(problems_.size() - 1);
}

unsigned int Puzzle::addRodSet(const stacking::RodSet& set) {
  rodSets_.push_back(set);
  return static_cast<unsigned int>(rodSets_.size() - 1);
}

void Puzzle::removeRodSet(unsigned int idx) {
  BT_ASSERT(idx < rodSets_.size());
  for (Problem& p : problems_) {
    const unsigned int id = p.getRodSetId();
    if (id == idx) {
      p.clearRodSet();
    } else if (id != stacking::kNoRodSet && id > idx) {
      p.setRodSetId(id - 1);
    }
  }
  rodSets_.erase(rodSets_.begin() + idx);
}

void Puzzle::exchangeRodSets(unsigned int a, unsigned int b) {
  BT_ASSERT(a < rodSets_.size());
  BT_ASSERT(b < rodSets_.size());
  std::swap(rodSets_[a], rodSets_[b]);
  for (Problem& p : problems_) {
    const unsigned int id = p.getRodSetId();
    if (id == a) {
      p.setRodSetId(b);
    } else if (id == b) {
      p.setRodSetId(a);
    }
  }
}

void Puzzle::removeProblem(unsigned int p) {
  BT_ASSERT(p < problems_.size());
  problems_.erase(problems_.begin() + p);
}

void Puzzle::exchangeProblems(unsigned int p1, unsigned int p2) {
  BT_ASSERT(p1 < problems_.size());
  BT_ASSERT(p2 < problems_.size());
  std::swap(problems_[p1], problems_[p2]);
}

}  // namespace bt

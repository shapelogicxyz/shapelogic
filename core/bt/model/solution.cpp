// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/solution.cpp.

#include "bt/model/solution.h"

#include <utility>

namespace bt {

Solution::Solution(Assembly assm, unsigned int assmNum)
    : assembly_(std::move(assm)), assemblyNum_(assmNum), solutionNum_(0) {}

Solution::Solution(Assembly assm, unsigned int assmNum, unsigned int solNum)
    : assembly_(std::move(assm)), assemblyNum_(assmNum), solutionNum_(solNum) {}

Solution::Solution(Assembly assm, unsigned int assmNum, Separation tree, unsigned int solNum)
    : assembly_(std::move(assm)),
      tree_(std::move(tree)),
      assemblyNum_(assmNum),
      solutionNum_(solNum) {}

Solution::Solution(Assembly assm, unsigned int assmNum, SeparationInfo info, unsigned int solNum)
    : assembly_(std::move(assm)),
      treeInfo_(std::move(info)),
      assemblyNum_(assmNum),
      solutionNum_(solNum) {}

const Disassembly* Solution::getDisassemblyInfo() const {
  if (tree_) {
    return &*tree_;
  }
  if (treeInfo_) {
    return &*treeInfo_;
  }
  return nullptr;
}

void Solution::removeDisassembly() {
  if (tree_) {
    if (!treeInfo_) {
      treeInfo_.emplace(*tree_);
    }
    tree_.reset();
  }
}

void Solution::setDisassembly(Separation sep) {
  tree_ = std::move(sep);
  treeInfo_.reset();
}

void Solution::exchangeShape(unsigned int s1, unsigned int s2) {
  assembly_.exchangeShape(s1, s2);
  if (tree_) {
    tree_->exchangeShape(s1, s2);
  }
}

void Solution::removePieces(unsigned int start, unsigned int count) {
  assembly_.removePieces(start, count);
  if (tree_) {
    tree_->removePieces(start, count);
  }
}

void Solution::addNonPlacedPieces(unsigned int start, unsigned int count) {
  assembly_.addNonPlacedPieces(start, count);
  if (tree_) {
    tree_->addNonPlacedPieces(start, count);
  }
}

}  // namespace bt

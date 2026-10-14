// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/disassemblernode.cpp.
#include "shapelogic/solve/disassembler_node.h"

#include "shapelogic/model/assembly.h"

#include <algorithm>

namespace bt {

DisassemblerNode::DisassemblerNode(unsigned int pn, NodeRef comefrom, int dir, int amount,
                                   int step)
    : comefrom_(std::move(comefrom)),
      piecenumber_(pn),
      dat_(std::make_unique_for_overwrite<std::int16_t[]>(4 * std::size_t{pn})),
      dir_(static_cast<unsigned int>(dir)),
      amount_(amount) {
  BT_ASSERT(comefrom_);
  waylength_ = comefrom_->waylength_ + static_cast<unsigned int>(step);
}

DisassemblerNode::DisassemblerNode(unsigned int pn)
    : piecenumber_(pn),
      dat_(std::make_unique_for_overwrite<std::int16_t[]>(4 * std::size_t{pn})),
      dir_(0),
      amount_(0),
      waylength_(0) {}

DisassemblerNode::DisassemblerNode(const Assembly& assembly)
    : piecenumber_(0), dir_(0), amount_(0), waylength_(0) {
  // the placed pieces where the assembly has them
  for (unsigned int j = 0; j < assembly.placementCount(); j++) {
    if (assembly.isPlaced(j)) {
      piecenumber_++;
    }
  }

  dat_ = std::make_unique_for_overwrite<std::int16_t[]>(4 * std::size_t{piecenumber_});

  std::size_t pc = 0;
  for (unsigned int j = 0; j < assembly.placementCount(); j++) {
    if (assembly.isPlaced(j)) {
      BT_ASSERT(std::abs(assembly.getX(j)) < kMaxMove && std::abs(assembly.getY(j)) < kMaxMove &&
                std::abs(assembly.getZ(j)) < kMaxMove);

      dat_[4 * pc + 0] = static_cast<std::int16_t>(assembly.getX(j));
      dat_[4 * pc + 1] = static_cast<std::int16_t>(assembly.getY(j));
      dat_[4 * pc + 2] = static_cast<std::int16_t>(assembly.getZ(j));
      dat_[4 * pc + 3] = static_cast<std::int16_t>(assembly.getTransformation(j));
      pc++;
    }
  }
}

DisassemblerNode::~DisassemblerNode() = default;

void DisassemblerNode::replaceNode(const DisassemblerNode& n) {
  // the same position, the whole puzzle maybe shifted
  BT_ASSERT(piecenumber_ == n.piecenumber_);
  BT_ASSERT(hashValue_ == n.hashValue_);

  std::copy_n(n.dat_.get(), 4 * std::size_t{piecenumber_}, dat_.get());
  dir_ = n.dir_;
  amount_ = n.amount_;
  waylength_ = n.waylength_;
  rotPiece_ = n.rotPiece_;
  rotPivotX_ = n.rotPivotX_;
  rotPivotY_ = n.rotPivotY_;
  rotPivotZ_ = n.rotPivotZ_;

  comefrom_ = n.comefrom_;
}

unsigned int DisassemblerNode::hash() const {
  if (hashValue_ != 0) {
    return hashValue_;
  }

  // the arithmetic of the original, in unsigned int
  auto u = [](int v) { return static_cast<unsigned int>(v); };
  const std::int16_t* d = dat_.get();

  unsigned int h = 0x17fe3b3c;

  // piece 0's orientation; its position is what the others are relative to
  h += u(d[3]);

  for (std::size_t i = 1; i < piecenumber_; i++) {
    h += u(d[4 * i + 0] - d[0]);
    h *= 1343;
    h += u(d[4 * i + 1] - d[1]);
    h *= 923;
    h += u(d[4 * i + 2] - d[2]);
    h *= 113;
    h += u(d[4 * i + 3]);
    h *= 23;
  }

  if (h == 0) {
    h = 1;
  }

  hashValue_ = h;
  return h;
}

bool DisassemblerNode::operator==(const DisassemblerNode& b) const {
  const std::int16_t* d = dat_.get();
  const std::int16_t* e = b.dat_.get();

  if (d[3] != e[3]) {
    return false;
  }

  for (std::size_t i = 1; i < piecenumber_; i++) {
    if (d[4 * i + 0] - d[0] != e[4 * i + 0] - e[0]) {
      return false;
    }
    if (d[4 * i + 1] - d[1] != e[4 * i + 1] - e[1]) {
      return false;
    }
    if (d[4 * i + 2] - d[2] != e[4 * i + 2] - e[2]) {
      return false;
    }
    if (d[4 * i + 3] != e[4 * i + 3]) {
      return false;
    }
  }

  return true;
}

bool DisassemblerNode::isSeparation() const {
  for (unsigned int i = 0; i < piecenumber_; i++) {
    if (isPieceRemoved(i)) {
      return true;
    }
  }
  return false;
}

}  // namespace bt

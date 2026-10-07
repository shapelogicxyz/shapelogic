// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/disassemblerhashes.cpp.
#include "bt/solve/node_hash.h"

namespace bt {

const DisassemblerNode* NodeHash::insert(const NodeRef& n) {
  // one lookup: an existing node comes back from insert()
  auto [it, inserted] = tab_.insert(n);

  if (!inserted) {
    DisassemblerNode* hn = it->get();

    // the same position: keep the shorter way to it
    if (hn->getWaylength() > n->getWaylength()) {
      hn->replaceNode(*n);
    }

    return hn;
  }

  return nullptr;
}

void CountingNodeHash::clear() {
  tab_.clear();
  order_.clear();
  scanPos_ = 0;
  scanActive_ = false;
}

bool CountingNodeHash::insert(const NodeRef& n) {
  if (!tab_.insert(n.get()).second) {
    return true;
  }
  order_.push_back(n);
  return false;
}

void CountingNodeHash::initScan() {
  BT_ASSERT(!scanActive_);
  scanPos_ = order_.size();
  scanActive_ = true;
}

const DisassemblerNode* CountingNodeHash::nextScan() {
  BT_ASSERT(scanActive_);

  if (scanPos_ == 0) {
    scanActive_ = false;
    return nullptr;
  }
  return order_[--scanPos_].get();
}

}  // namespace bt

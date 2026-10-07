// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/bt2_dancingcells.cpp. The search is the original's;
// the abort flag is a StopToken, the solution callback a parameter of
// solve(), and the copy that split() makes is a default copy with its step
// count and split share reset (the original's copy constructor left out the
// callback, which this class no longer keeps).
#include "bt/solve/dancing_cells.h"

#include "bt/base/int_cast.h"

#include <algorithm>
#include <limits>

namespace bt {

namespace {
constexpr float kBranchCoeff = 0.13F;
}  // namespace

void DancingCells::prepare(unsigned int primary, unsigned int secondary) {
  *this = DancingCells{};
  nPrimary_ = primary;
  nItems_ = primary + secondary;
}

void DancingCells::addOption(unsigned int rowId, const std::vector<unsigned int>& items) {
  pendingRows_.push_back(rowId);
  pendingItems_.push_back(items);
}

void DancingCells::finishSetup() {
  if (nItems_ == 0) {
    setupDone_ = true;
    frames_.assign(1, Frame());
    activeFrames_ = 1;
    return;
  }

  std::vector<unsigned int> count(nItems_, 0);
  for (const std::vector<unsigned int>& items : pendingItems_) {
    for (const unsigned int it : items) {
      if (it < nItems_) {
        count[it]++;
      }
    }
  }

  itemBase_.assign(nItems_, 0);
  itemSize_.assign(nItems_, 0);
  unsigned int pos = 0;
  for (unsigned int i = 0; i < nItems_; i++) {
    itemBase_[i] = pos;
    pos += count[i];
  }
  set_.assign(pos, 0);

  nodes_.clear();
  options_.clear();
  options_.reserve(pendingItems_.size());

  std::vector<unsigned int> fill(nItems_, 0);
  for (std::size_t o = 0; o < pendingItems_.size(); o++) {
    Option opt{pendingRows_[o], static_cast<unsigned int>(nodes_.size()), 0};
    for (const unsigned int it : pendingItems_[o]) {
      if (it >= nItems_) {
        continue;
      }
      Node n{it, itemBase_[it] + fill[it], static_cast<unsigned int>(o)};
      set_[n.loc] = static_cast<unsigned int>(nodes_.size());
      fill[it]++;
      nodes_.push_back(n);
      opt.len++;
    }
    options_.push_back(opt);
  }

  for (unsigned int i = 0; i < nItems_; i++) {
    itemSize_[i] = fill[i];
  }

  itemOrder_.resize(nItems_);
  itemIndex_.resize(nItems_);
  for (unsigned int i = 0; i < nItems_; i++) {
    itemOrder_[i] = i;
    itemIndex_[i] = i;
  }
  active_ = nItems_;

  // secondary items with no options can sit inactive
  for (unsigned int i = nItems_; i-- > 0;) {
    if (!isPrimary(i) && itemSize_[i] == 0) {
      deactivate(i);
    }
  }

  pendingItems_.clear();
  pendingRows_.clear();

  sol_.clear();
  solRows_.clear();
  frames_.assign(1, Frame());
  activeFrames_ = 1;
  setupDone_ = true;
}

void DancingCells::deactivate(unsigned int item) {
  const unsigned int idx = itemIndex_[item];
  if (idx >= active_) {
    return;
  }
  const unsigned int lastIdx = active_ - 1;
  const unsigned int last = itemOrder_[lastIdx];
  itemOrder_[idx] = last;
  itemOrder_[lastIdx] = item;
  itemIndex_[last] = idx;
  itemIndex_[item] = lastIdx;
  active_--;
}

void DancingCells::storeSizes(Save& s) const {
  s.size = itemSize_;
  s.active = active_;
}

void DancingCells::restoreSizes(const Save& s) {
  itemSize_ = s.size;
  active_ = s.active;
}

// Takes one node's option out of its item's live options. False when that
// leaves a primary item with none.
bool DancingCells::hideNode(unsigned int node) {
  const unsigned int item = nodes_[node].item;
  if (!isActiveItem(item)) {
    return true;
  }

  const unsigned int sz = itemSize_[item];
  if (sz == 0) {
    return !isPrimary(item);
  }

  const unsigned int loc = nodes_[node].loc;
  const unsigned int lastLoc = itemBase_[item] + sz - 1;
  // hidden already
  if (loc > lastLoc) {
    return true;
  }

  if (loc != lastLoc) {
    const unsigned int other = set_[lastLoc];
    set_[loc] = other;
    set_[lastLoc] = node;
    nodes_[other].loc = loc;
    nodes_[node].loc = lastLoc;
  }

  itemSize_[item] = sz - 1;
  return !(isPrimary(item) && itemSize_[item] == 0);
}

// Hides an option from every item it has, bar the one named in `covering`:
// that item is about to be covered, so what its set holds no longer matters.
bool DancingCells::discardOption(unsigned int opt, unsigned int covering) {
  const unsigned int start = options_[opt].start;
  const unsigned int end = start + options_[opt].len;
  for (unsigned int n = start; n < end; n++) {
    if (nodes_[n].item == covering) {
      continue;
    }
    if (!hideNode(n)) {
      return false;
    }
  }
  return true;
}

bool DancingCells::selectOption(unsigned int opt) {
  const unsigned int start = options_[opt].start;
  const unsigned int end = start + options_[opt].len;
  for (unsigned int n = start; n < end; n++) {
    const unsigned int item = nodes_[n].item;
    if (!isActiveItem(item)) {
      continue;
    }

    // the item's other options, noted first: discarding them reorders set_
    std::vector<unsigned int>& others = othersBuf_;
    others.clear();
    const unsigned int base = itemBase_[item];
    const unsigned int sz = itemSize_[item];
    for (unsigned int k = 0; k < sz; k++) {
      const unsigned int other = nodes_[set_[base + k]].option;
      if (other != opt) {
        others.push_back(other);
      }
    }
    for (const unsigned int other : others) {
      if (!discardOption(other, item)) {
        return false;
      }
    }
    deactivate(item);
  }
  sol_.push_back(opt);
  return true;
}

bool DancingCells::findBranch(std::vector<unsigned int>& force, unsigned int& chosen,
                              bool& contradiction) {
  force.clear();
  chosen = 0;
  contradiction = false;
  unsigned int minSize = std::numeric_limits<unsigned int>::max();
  bool have = false;

  for (unsigned int idx = 0; idx < active_; idx++) {
    const unsigned int item = itemOrder_[idx];
    if (!isPrimary(item)) {
      continue;
    }
    const unsigned int s = itemSize_[item];
    if (s == 0) {
      contradiction = true;
      return false;
    }
    if (s == 1) {
      force.push_back(item);
    } else if (s < minSize) {
      minSize = s;
      chosen = item;
      have = true;
    }
  }

  if (!force.empty()) {
    return true;
  }
  // no active primary item: a solution (or nothing to do)
  return have;
}

bool DancingCells::fastTrack(const std::vector<unsigned int>& force) {
  for (const unsigned int item : force) {
    if (!isActiveItem(item)) {
      continue;
    }
    std::vector<unsigned int>& opts = optsBuf_;
    opts.clear();
    const unsigned int sz = itemSize_[item];
    const unsigned int base = itemBase_[item];
    for (unsigned int k = 0; k < sz; k++) {
      opts.push_back(nodes_[set_[base + k]].option);
    }
    for (std::size_t k = 0; k < opts.size(); k++) {
      if (!selectOption(opts[k])) {
        return false;
      }
      if (k + 1 < opts.size() && !isActiveItem(item)) {
        return false;
      }
    }
  }
  return true;
}

void DancingCells::pushFrame() {
  if (activeFrames_ == frames_.size()) {
    frames_.emplace_back();
  }
  activeFrames_++;
  Frame& f = frames_[activeFrames_ - 1];
  f.state = 0;
  f.solMark = 0;
  f.x = 0;
}

void DancingCells::popFrame() {
  if (activeFrames_ > 0) {
    activeFrames_--;
  }
}

void DancingCells::convertSolution() {
  solRows_.resize(sol_.size());
  for (std::size_t i = 0; i < sol_.size(); i++) {
    solRows_[i] = options_[sol_[i]].rowId;
  }
}

std::unique_ptr<DancingCells> DancingCells::split() {
  if (!setupDone_ || finished()) {
    return nullptr;
  }

  // The copy counts its own steps and has no split behind it.
  auto copy = [this] {
    auto other = std::make_unique<DancingCells>(*this);
    other->iterations_ = 0;
    other->splitShare_ = 0;
    return other;
  };

  // The shallowest branch still open: an option chosen there, the search
  // without it not yet begun. The copy takes that other half (the largest
  // piece of work to give away) and this search keeps what it is in.
  for (unsigned int i = 0; i < activeFrames_; i++) {
    if (frames_[i].state != 3) {
      continue;
    }
    const float before = progress();
    std::unique_ptr<DancingCells> other = copy();
    Frame& f = other->frames_[i];
    other->activeFrames_ = i + 1;
    other->sol_.resize(f.solMark);
    other->restoreSizes(f.save);
    other->discardOption(f.x);
    f.state = 1;
    other->pushFrame();
    // here, coming back up to that frame is now the end of it
    frames_[i].state = 1;
    // No shallower branch is open, so all that was left lies under this
    // frame, and the copy's part of it is what its own estimate says is
    // left to it.
    splitShare_ = (before < 1.0F) ? (1.0F - other->progress()) / (1.0F - before) : 0.0F;
    if (splitShare_ > 1.0F) {
      splitShare_ = 1.0F;
    }
    return other;
  }

  // at the choose-frame: peel the first option of the MRV item
  if (activeFrames_ == 0) {
    return nullptr;
  }

  std::vector<unsigned int> force;
  unsigned int chosen = 0;
  bool contra = false;
  if (!findBranch(force, chosen, contra) || contra || !force.empty()) {
    return nullptr;
  }
  if (itemSize_[chosen] < 2) {
    return nullptr;
  }

  const unsigned int opt = firstOptionOf(chosen);
  std::unique_ptr<DancingCells> other = copy();
  other->storeSizes(other->frames_[other->activeFrames_ - 1].save);
  other->frames_[other->activeFrames_ - 1].x = opt;
  if (!other->selectOption(opt)) {
    return nullptr;
  }
  // State 1, not 3: the copy searches with opt chosen and stops there. The
  // searches without opt are this one's (it discards opt below).
  other->frames_[other->activeFrames_ - 1].state = 1;
  other->pushFrame();

  // one of the item's options, taken as all alike
  splitShare_ = 1.0F / static_cast<float>(itemSize_[chosen]);
  discardOption(opt);
  return other;
}

unsigned int DancingCells::remainingWork() const {
  if (!setupDone_ || finished()) {
    return 0;
  }
  unsigned int best = 0;
  for (unsigned int idx = 0; idx < active_; idx++) {
    const unsigned int item = itemOrder_[idx];
    if (!isPrimary(item)) {
      continue;
    }
    best = std::max(best, itemSize_[item]);
  }
  return best;
}

float DancingCells::progress() const {
  if (!setupDone_) {
    return 0;
  }
  if (finished()) {
    return 1.0F;
  }
  float res = 1.0F;
  for (int i = asInt(activeFrames_) - 1; i >= 0; --i) {
    switch (frames_[static_cast<std::size_t>(i)].state) {
      case 0:
        res = 0.0F;
        break;
      case 1:
        res = (1.0F - kBranchCoeff) * res + kBranchCoeff;
        break;
      case 3:
        res *= kBranchCoeff;
        break;
      default:
        break;
    }
  }
  return res;
}

void DancingCells::solve(unsigned int maxSteps, const StopToken& stop,
                         const SolutionFn& onSolution) {
  if (!setupDone_) {
    return;
  }

  unsigned int left = maxSteps;
  const bool limited = maxSteps != 0;
  if (limited) {
    left++;
  }

  while (activeFrames_ > 0) {
    if (stop.stopRequested()) {
      return;
    }

    Frame& f = frames_[activeFrames_ - 1];
    switch (f.state) {
      case 0: {
        if (limited) {
          if (left == 1) {
            return;
          }
          left--;
        }
        iterations_++;

        std::vector<unsigned int>& force = forceBuf_;
        unsigned int chosen = 0;
        bool contra = false;
        const bool branched = findBranch(force, chosen, contra);

        if (contra) {
          popFrame();
          break;
        }

        if (!force.empty()) {
          f.solMark = static_cast<unsigned int>(sol_.size());
          if (fastTrack(force)) {
            f.state = 2;
            pushFrame();
          } else {
            sol_.resize(f.solMark);
            popFrame();
          }
          break;
        }

        if (!branched) {
          convertSolution();
          if (onSolution) {
            onSolution(solRows_);
          }
          popFrame();
          break;
        }

        f.x = firstOptionOf(chosen);
        f.solMark = static_cast<unsigned int>(sol_.size());
        storeSizes(f.save);
        if (selectOption(f.x)) {
          f.state = 3;
          pushFrame();
        } else {
          restoreSizes(f.save);
          discardOption(f.x);
          f.state = 1;
          pushFrame();
        }
        break;
      }
      case 2:
        sol_.resize(f.solMark);
        popFrame();
        break;
      case 1:
        popFrame();
        break;
      case 3:
        if (!sol_.empty()) {
          sol_.pop_back();
        }
        restoreSizes(f.save);
        discardOption(f.x);
        f.state = 1;
        pushFrame();
        break;
      default:
        popFrame();
        break;
    }
  }
}

}  // namespace bt

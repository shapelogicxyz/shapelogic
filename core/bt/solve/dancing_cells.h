// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Knuth's dancing cells (TAOCP 7.2.2.3, a subset of algorithms C and F):
/// exact cover with optional secondary items, over sparse sets instead of
/// linked lists. The search of BurrTools 2. It can be run a slice of steps
/// at a time and split: a copy takes part of what is left, and the two
/// never visit the same branch.
/// Ported from src/lib/bt2_dancingcells.{h,cpp}; the abort flag became a
/// StopToken and the solution callback a parameter of solve().

#include "bt/base/stop.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace bt {

/// An exact cover search by dancing cells. Items 0 .. primary-1 must be
/// covered exactly once, the others (secondary) at most once; options are
/// sets of items. Set up with prepare(), addOption() and finishSetup(), then
/// solve() until finished().
class DancingCells {
 public:
  /// Called with the row ids of the options of each cover found.
  using SolutionFn = std::function<void(const std::vector<unsigned int>& rowIds)>;

  /// Starts a new setup with `primary` primary and `secondary` secondary
  /// items.
  void prepare(unsigned int primary, unsigned int secondary);
  /// Adds an option covering `items`; solutions report it as `rowId`.
  void addOption(unsigned int rowId, const std::vector<unsigned int>& items);
  /// Builds the search from the options; the search then stands at its
  /// start.
  void finishSetup();
  /// True after finishSetup().
  [[nodiscard]] bool ready() const { return setupDone_; }

  /// Searches `maxSteps` steps (0: to the end), or until `stop` is
  /// requested, handing every cover found to `onSolution`. Called again it
  /// carries on.
  void solve(unsigned int maxSteps, const StopToken& stop, const SolutionFn& onSolution);

  /// True when the search is through.
  [[nodiscard]] bool finished() const { return activeFrames_ == 0; }
  /// An estimate of how far the search is, 0..1.
  [[nodiscard]] float progress() const;
  /// A measure of what is left (the largest option count of a primary
  /// item); 0 when through.
  [[nodiscard]] unsigned int remainingWork() const;
  /// The search steps done since this search was made (a split-off copy
  /// counts from 0).
  [[nodiscard]] std::uint64_t iterations() const { return iterations_; }
  /// The depth of the search.
  [[nodiscard]] unsigned int depth() const { return activeFrames_; }

  /// Hands part of the search still to do to a copy; null when there is
  /// nothing to hand over. The two searches never visit the same branch.
  [[nodiscard]] std::unique_ptr<DancingCells> split();
  /// After a split: the part of this search's estimated remaining work, as
  /// it was before the split, that the copy took (0..1).
  [[nodiscard]] float lastSplitShare() const { return splitShare_; }

  /// The row ids of the last cover found.
  [[nodiscard]] const std::vector<unsigned int>& currentRowIds() const { return solRows_; }

 private:
  struct Option {
    unsigned int rowId;
    unsigned int start;
    unsigned int len;
  };

  /// One item of one option: where it sits in that item's part of set_.
  struct Node {
    unsigned int item;
    unsigned int loc;
    unsigned int option;
  };

  struct Save {
    std::vector<unsigned int> size;
    unsigned int active = 0;
  };

  /// One level of the search. state: 0 choose, 1 the chosen option was
  /// discarded and the rest is searched, 2 forced options were taken, 3 an
  /// option is chosen and searched.
  struct Frame {
    int state = 0;
    unsigned int solMark = 0;
    unsigned int x = 0;
    Save save;
  };

  /// No item has this number: discardOption() then hides from them all.
  static constexpr unsigned int kNoItem = 0xFFFFFFFFU;

  [[nodiscard]] bool isPrimary(unsigned int item) const { return item < nPrimary_; }
  [[nodiscard]] bool isActiveItem(unsigned int item) const { return itemIndex_[item] < active_; }

  void deactivate(unsigned int item);
  void storeSizes(Save& s) const;
  void restoreSizes(const Save& s);

  bool hideNode(unsigned int node);
  bool discardOption(unsigned int opt, unsigned int covering = kNoItem);
  bool selectOption(unsigned int opt);

  bool findBranch(std::vector<unsigned int>& force, unsigned int& chosen, bool& contradiction);
  bool fastTrack(const std::vector<unsigned int>& force);

  void pushFrame();
  void popFrame();
  void convertSolution();

  [[nodiscard]] unsigned int firstOptionOf(unsigned int item) const {
    return nodes_[set_[itemBase_[item]]].option;
  }

  unsigned int nPrimary_ = 0;
  unsigned int nItems_ = 0;
  bool setupDone_ = false;

  std::vector<Option> options_;
  std::vector<Node> nodes_;
  std::vector<unsigned int> itemBase_;
  std::vector<unsigned int> itemSize_;
  /// Each item's options, as node numbers, the live ones first (a sparse
  /// set). Holding the node, not the option, lets an option be hidden from
  /// an item without searching the option for that item.
  std::vector<unsigned int> set_;
  std::vector<unsigned int> itemOrder_;
  std::vector<unsigned int> itemIndex_;
  unsigned int active_ = 0;

  std::vector<std::vector<unsigned int>> pendingItems_;
  std::vector<unsigned int> pendingRows_;

  std::vector<unsigned int> sol_;
  std::vector<unsigned int> solRows_;

  std::vector<Frame> frames_;
  unsigned int activeFrames_ = 0;

  std::uint64_t iterations_ = 0;
  float splitShare_ = 0;

  /// Scratch lists, kept so the search allocates nothing per step.
  std::vector<unsigned int> othersBuf_;
  std::vector<unsigned int> optsBuf_;
  std::vector<unsigned int> forceBuf_;
};

}  // namespace bt

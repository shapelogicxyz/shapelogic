// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// How a vector search of one subtree hands branches of it on, so that a
/// search split into tasks can give them to other threads and keep what is
/// left of a stopped task (SplitTasks). The vector searches know rows by the
/// node ids the assembler named them with; a branch is the rows that lead
/// to it.

#include <vector>

namespace bt {

/// Branches of the subtree a search is in, none of them begun. The shares
/// are parts of the subtree as the search was given it, every choice of a
/// level taken as alike.
struct SubtreeBranches {
  /// The rows of each branch, the subtree's own first.
  std::vector<std::vector<unsigned int>> branches;
  /// For a search with rows taken out of play (SimdHuangCover): the rows
  /// hidden in each branch, besides those the subtree was given as hidden.
  std::vector<std::vector<unsigned int>> hidden;
  std::vector<double> shares;
  /// A hand-off: the branch the search goes on with (its rows, its hidden
  /// rows, its share) and how far the search is in it, 0..1.
  std::vector<unsigned int> rest;
  std::vector<unsigned int> restHidden;
  double restShare = 0;
  float restFraction = 0;
};

/// What a subtree search asks and tells while it runs.
class SubtreeSplitter {
 public:
  virtual ~SubtreeSplitter() = default;

  /// True when the search should hand branches on now (another thread has
  /// nothing to do). Asked every few hundred nodes.
  [[nodiscard]] virtual bool wanted() const = 0;

  /// The search hands these branches on and goes on with `rest`.
  virtual void handOff(SubtreeBranches&& branches) = 0;

  /// The search was stopped: these branches are all that is left of its
  /// subtree.
  virtual void leave(SubtreeBranches&& branches) = 0;
};

/// A hand-off leaves alone what has fewer rows than this still to place:
/// such a branch is searched sooner than it is handed on.
inline constexpr unsigned int kSplitLeafRows = 3;

}  // namespace bt

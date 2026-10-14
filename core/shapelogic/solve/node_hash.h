// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Sets of take-apart positions, found by position (DisassemblerNode's
/// relative comparison), holding a reference to each.
/// Ported from src/lib/disassemblerhashes.{h,cpp}.

#include "shapelogic/solve/disassembler_node.h"

#include <cstddef>
#include <unordered_set>
#include <vector>

namespace bt {

/// Hashes and compares nodes by position, through NodeRef or a plain
/// pointer (heterogeneous lookup: a lookup takes no reference).
struct NodePositionHash {
  using is_transparent = void;
  std::size_t operator()(const NodeRef& n) const noexcept { return n->hash(); }
  std::size_t operator()(const DisassemblerNode* n) const noexcept { return n->hash(); }
};

struct NodePositionEqual {
  using is_transparent = void;
  static const DisassemblerNode* ptr(const NodeRef& n) noexcept { return n.get(); }
  static const DisassemblerNode* ptr(const DisassemblerNode* n) noexcept { return n; }
  template <class A, class B>
  bool operator()(const A& a, const B& b) const noexcept {
    return *ptr(a) == *ptr(b);
  }
};

/// A set of positions. A position inserted again keeps the shorter of the
/// two ways to it.
class NodeHash {
 public:
  NodeHash() = default;
  NodeHash(const NodeHash&) = delete;
  NodeHash& operator=(const NodeHash&) = delete;

  /// Lets go of every node.
  void clear() { tab_.clear(); }

  /// Adds `n` and returns null, or, when its position is in the set
  /// already, returns the node there (which takes n's way when that is
  /// shorter) and leaves n out.
  const DisassemblerNode* insert(const NodeRef& n);

  /// The position of `n` is in the set.
  [[nodiscard]] bool contains(const DisassemblerNode* n) const { return tab_.contains(n); }

  [[nodiscard]] std::size_t size() const { return tab_.size(); }

 private:
  std::unordered_set<NodeRef, NodePositionHash, NodePositionEqual> tab_;
};

/// A set of positions that can also be gone through, newest first; one
/// pass at a time.
class CountingNodeHash {
 public:
  CountingNodeHash() = default;
  CountingNodeHash(const CountingNodeHash&) = delete;
  CountingNodeHash& operator=(const CountingNodeHash&) = delete;

  /// Lets go of every node and ends a pass.
  void clear();

  /// True when the position of `n` is in the set already (nothing
  /// changes); else adds it and returns false.
  bool insert(const NodeRef& n);

  /// Starts a pass over the nodes there are now; none may be running.
  void initScan();

  /// The next node of the pass, newest first; null at the end, which ends
  /// the pass.
  const DisassemblerNode* nextScan();

 private:
  /// The nodes, oldest first; the set only finds them.
  std::vector<NodeRef> order_;
  std::unordered_set<const DisassemblerNode*, NodePositionHash, NodePositionEqual> tab_;
  std::size_t scanPos_ = 0;
  bool scanActive_ = false;
};

}  // namespace bt

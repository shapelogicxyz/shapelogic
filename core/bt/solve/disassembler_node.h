// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The positions a take-apart search goes through: every piece's place and
/// orientation, the position it was reached from, and the move that led
/// here. The search builds a graph of these, so they are kept small and
/// shared by counted references (NodeRef).
/// Ported from src/lib/disassemblernode.{h,cpp}.

#include "bt/base/assert.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <utility>

namespace bt {

class Assembly;
class DisassemblerNode;

/// A counted reference to a node: copies count, and the last one to go
/// deletes the node (the original's incRefCount/decRefCount). The count is
/// not atomic: the take-apart touches a node's count from one thread at a
/// time (see Disassembler), which keeps the many references it makes cheap.
class NodeRef {
 public:
  NodeRef() noexcept = default;
  NodeRef(std::nullptr_t) noexcept {}  // NOLINT(google-explicit-constructor)
  NodeRef(const NodeRef& o) noexcept;
  NodeRef(NodeRef&& o) noexcept : p_(std::exchange(o.p_, nullptr)) {}
  NodeRef& operator=(const NodeRef& o) noexcept;
  NodeRef& operator=(NodeRef&& o) noexcept;
  ~NodeRef();

  /// A new node, made with these constructor arguments.
  template <class... Args>
  [[nodiscard]] static NodeRef make(Args&&... args) {
    return NodeRef(std::make_unique<DisassemblerNode>(std::forward<Args>(args)...).release());
  }

  /// Another reference to a node that is referenced already (a node the
  /// search holds without a reference of its own, such as the one it
  /// expands).
  [[nodiscard]] static NodeRef share(DisassemblerNode* node) noexcept;

  [[nodiscard]] DisassemblerNode* get() const noexcept { return p_; }
  DisassemblerNode& operator*() const noexcept { return *p_; }
  DisassemblerNode* operator->() const noexcept { return p_; }
  explicit operator bool() const noexcept { return p_ != nullptr; }

  /// Lets go of the node.
  void reset() noexcept;

 private:
  explicit NodeRef(DisassemblerNode* adopt) noexcept : p_(adopt) {}

  DisassemblerNode* p_ = nullptr;
};

/// One position of the take-apart search. Positions are compared without
/// regard to where the whole puzzle is: two nodes are equal when the pieces
/// lie the same relative to piece 0.
class DisassemblerNode {
 public:
  /// The direction codes of rotation moves start here: kRotationDirBase +
  /// axis * 2 + sense (axis 0..2, sense 0 is +90 degrees, 1 is -90).
  static constexpr unsigned int kRotationDirBase = 100;

  /// A node for `pn` pieces reached from `comefrom` by moving pieces
  /// `amount` steps in direction `dir`; its way length is comefrom's plus
  /// `step`.
  DisassemblerNode(unsigned int pn, NodeRef comefrom, int dir, int amount, int step = 1);

  /// The start node of an assembly: its placed pieces where they are.
  explicit DisassemblerNode(const Assembly& assembly);

  /// A start node for `pn` pieces, positions to be set.
  explicit DisassemblerNode(unsigned int pn);

  ~DisassemblerNode();

  DisassemblerNode(const DisassemblerNode&) = delete;
  DisassemblerNode& operator=(const DisassemblerNode&) = delete;

  /// Takes the data of `n`, which stands for the same position reached
  /// another way: the pieces, the move and the node it came from.
  void replaceNode(const DisassemblerNode& n);

  /// The hash of the position, relative like the comparison (equal nodes
  /// hash equal); computed once.
  [[nodiscard]] unsigned int hash() const;

  /// Equal when every piece lies as in `b` relative to piece 0.
  [[nodiscard]] bool operator==(const DisassemblerNode& b) const;

  [[nodiscard]] int getX(unsigned int i) const {
    BT_ASSERT(i < piecenumber_);
    return dat_[4 * std::size_t{i} + 0];
  }
  [[nodiscard]] int getY(unsigned int i) const {
    BT_ASSERT(i < piecenumber_);
    return dat_[4 * std::size_t{i} + 1];
  }
  [[nodiscard]] int getZ(unsigned int i) const {
    BT_ASSERT(i < piecenumber_);
    return dat_[4 * std::size_t{i} + 2];
  }
  /// The orientation of piece i.
  [[nodiscard]] unsigned int getTrans(unsigned int i) const {
    BT_ASSERT(i < piecenumber_);
    return static_cast<unsigned char>(dat_[4 * std::size_t{i} + 3]);
  }

  [[nodiscard]] unsigned int getPiecenumber() const { return piecenumber_; }

  /// Piece i leaves the puzzle in direction (x, y, z).
  void setRemove(unsigned int i, int x, int y, int z) {
    BT_ASSERT(i < piecenumber_);
    BT_ASSERT(std::abs(x) < kMaxMove && std::abs(y) < kMaxMove && std::abs(z) < kMaxMove);
    dat_[4 * std::size_t{i} + 0] = static_cast<std::int16_t>(x);
    dat_[4 * std::size_t{i} + 1] = static_cast<std::int16_t>(y);
    dat_[4 * std::size_t{i} + 2] = static_cast<std::int16_t>(z);
    dat_[4 * std::size_t{i} + 3] = kRemoved;
    hashValue_ = 0;
  }

  /// Puts piece i at (x, y, z) in orientation tr.
  void set(unsigned int i, int x, int y, int z, unsigned int tr) {
    BT_ASSERT(i < piecenumber_);
    BT_ASSERT(std::abs(x) < kMaxMove && std::abs(y) < kMaxMove && std::abs(z) < kMaxMove);
    dat_[4 * std::size_t{i} + 0] = static_cast<std::int16_t>(x);
    dat_[4 * std::size_t{i} + 1] = static_cast<std::int16_t>(y);
    dat_[4 * std::size_t{i} + 2] = static_cast<std::int16_t>(z);
    dat_[4 * std::size_t{i} + 3] = static_cast<std::int16_t>(tr);
    hashValue_ = 0;
  }

  /// Puts piece i where it was in the node this one came from, moved by
  /// (tx, ty, tz).
  void set(unsigned int i, int tx, int ty, int tz) {
    BT_ASSERT(i < piecenumber_);
    BT_ASSERT(comefrom_);
    const std::int16_t* from = comefrom_->dat_.get() + 4 * std::size_t{i};
    BT_ASSERT(std::abs(from[0] + tx) < kMaxMove && std::abs(from[1] + ty) < kMaxMove &&
              std::abs(from[2] + tz) < kMaxMove);
    dat_[4 * std::size_t{i} + 0] = static_cast<std::int16_t>(from[0] + tx);
    dat_[4 * std::size_t{i} + 1] = static_cast<std::int16_t>(from[1] + ty);
    dat_[4 * std::size_t{i} + 2] = static_cast<std::int16_t>(from[2] + tz);
    dat_[4 * std::size_t{i} + 3] = from[3];
    hashValue_ = 0;
  }

  /// Piece nr has left the puzzle in this position.
  [[nodiscard]] bool isPieceRemoved(unsigned int nr) const {
    BT_ASSERT(nr < piecenumber_);
    return dat_[4 * std::size_t{nr} + 3] == kRemoved;
  }

  /// Some piece has left the puzzle.
  [[nodiscard]] bool isSeparation() const;

  /// The node this one was reached from; null for a start node.
  [[nodiscard]] const DisassemblerNode* getComefrom() const { return comefrom_.get(); }

  [[nodiscard]] int getAmount() const { return amount_; }
  [[nodiscard]] unsigned int getDirection() const { return dir_; }
  [[nodiscard]] unsigned int getWaylength() const { return waylength_; }

  /// The move here was a rotation (dir >= kRotationDirBase).
  [[nodiscard]] bool isRotationMove() const { return dir_ >= kRotationDirBase; }

  /// For a rotation move: the piece that turned (its index in this node) and
  /// the pivot, in doubled cell units (a cell's centre is at 2x).
  [[nodiscard]] unsigned int getRotPiece() const { return rotPiece_; }
  [[nodiscard]] int getRotPivotX() const { return rotPivotX_; }
  [[nodiscard]] int getRotPivotY() const { return rotPivotY_; }
  [[nodiscard]] int getRotPivotZ() const { return rotPivotZ_; }

  void setRotationInfo(unsigned int piece, int px, int py, int pz) {
    rotPiece_ = piece;
    rotPivotX_ = static_cast<std::int16_t>(px);
    rotPivotY_ = static_cast<std::int16_t>(py);
    rotPivotZ_ = static_cast<std::int16_t>(pz);
  }

 private:
  friend class NodeRef;

  /// Pieces must stay closer than this to where they started (the int16
  /// positions would overflow).
  static constexpr int kMaxMove = 32767;
  /// The orientation entry of a piece that has left.
  static constexpr std::int16_t kRemoved = -1;  // 0xFFFF

  /// The node this one was reached from, by the shortest way known.
  NodeRef comefrom_;
  unsigned int piecenumber_;
  /// x, y, z and orientation of each piece, interleaved. A piece that has
  /// left has orientation kRemoved and the direction it left in instead of
  /// a position.
  std::unique_ptr<std::int16_t[]> dat_;
  /// The references to this node (NodeRef).
  unsigned int refcount_ = 1;
  /// The move from comefrom: its direction (meaningless when pieces moved
  /// in several) and length.
  unsigned int dir_;
  int amount_;
  /// The hash, 0 when not computed yet.
  mutable unsigned int hashValue_ = 0;
  /// The length of the way from the start (moves weighted by their step).
  unsigned int waylength_;
  /// For a rotation move, see getRotPiece(); 0xFFFF otherwise.
  unsigned int rotPiece_ = 0xFFFF;
  std::int16_t rotPivotX_ = 0;
  std::int16_t rotPivotY_ = 0;
  std::int16_t rotPivotZ_ = 0;
};

inline NodeRef::NodeRef(const NodeRef& o) noexcept : p_(o.p_) {
  if (p_ != nullptr) {
    p_->refcount_++;
  }
}

inline NodeRef& NodeRef::operator=(const NodeRef& o) noexcept {
  if (o.p_ != nullptr) {
    o.p_->refcount_++;
  }
  reset();
  p_ = o.p_;
  return *this;
}

inline NodeRef& NodeRef::operator=(NodeRef&& o) noexcept {
  if (this != &o) {
    reset();
    p_ = std::exchange(o.p_, nullptr);
  }
  return *this;
}

inline NodeRef::~NodeRef() { reset(); }

inline NodeRef NodeRef::share(DisassemblerNode* node) noexcept {
  if (node != nullptr) {
    node->refcount_++;
  }
  return NodeRef(node);
}

inline void NodeRef::reset() noexcept {
  DisassemblerNode* p = std::exchange(p_, nullptr);
  if (p != nullptr) {
    BT_ASSERT(p->refcount_ > 0);
    if (--p->refcount_ == 0) {
      // the node owns its comefrom reference; a long chain goes with it
      std::unique_ptr<DisassemblerNode> gone(p);
    }
  }
}

}  // namespace bt
